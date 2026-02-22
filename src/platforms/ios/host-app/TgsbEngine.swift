/*
 * TgsbEngine.swift — Swift wrapper around the C bridge.
 *
 * Manages the TGSpeechBox pipeline lifecycle and audio playback
 * via AVAudioPlayer (output-only, no microphone permission needed).
 */

import AVFoundation

/// Supported languages with their eSpeak and TGSB language tags.
struct TgsbLanguage: Identifiable, Hashable {
    let id: String          // display key, e.g. "en-us"
    let displayName: String
    let espeakTag: String
    let tgsbTag: String
}

let kLanguages: [TgsbLanguage] = [
    TgsbLanguage(id: "en-us", displayName: "English (US)",      espeakTag: "en-us", tgsbTag: "en-us"),
    TgsbLanguage(id: "en-gb", displayName: "English (GB)",      espeakTag: "en-gb", tgsbTag: "en-gb"),
    TgsbLanguage(id: "en-ca", displayName: "English (CA)",      espeakTag: "en-us", tgsbTag: "en-us"),
    TgsbLanguage(id: "fr",    displayName: "French",            espeakTag: "fr",    tgsbTag: "fr"),
    TgsbLanguage(id: "es",    displayName: "Spanish",           espeakTag: "es",    tgsbTag: "es"),
    TgsbLanguage(id: "es-mx", displayName: "Spanish (Mexico)",  espeakTag: "es-419",tgsbTag: "es-mx"),
    TgsbLanguage(id: "it",    displayName: "Italian",           espeakTag: "it",    tgsbTag: "it"),
    TgsbLanguage(id: "pt-br", displayName: "Portuguese (BR)",   espeakTag: "pt-br", tgsbTag: "pt-br"),
    TgsbLanguage(id: "pt",    displayName: "Portuguese (PT)",   espeakTag: "pt",    tgsbTag: "pt"),
    TgsbLanguage(id: "ro",    displayName: "Romanian",          espeakTag: "ro",    tgsbTag: "ro"),
    TgsbLanguage(id: "de",    displayName: "German",            espeakTag: "de",    tgsbTag: "de"),
    TgsbLanguage(id: "nl",    displayName: "Dutch",             espeakTag: "nl",    tgsbTag: "nl"),
    TgsbLanguage(id: "da",    displayName: "Danish",            espeakTag: "da",    tgsbTag: "da"),
    TgsbLanguage(id: "sv",    displayName: "Swedish",           espeakTag: "sv",    tgsbTag: "sv"),
    TgsbLanguage(id: "pl",    displayName: "Polish",            espeakTag: "pl",    tgsbTag: "pl"),
    TgsbLanguage(id: "cs",    displayName: "Czech",             espeakTag: "cs",    tgsbTag: "cs"),
    TgsbLanguage(id: "sk",    displayName: "Slovak",            espeakTag: "sk",    tgsbTag: "sk"),
    TgsbLanguage(id: "bg",    displayName: "Bulgarian",         espeakTag: "bg",    tgsbTag: "bg"),
    TgsbLanguage(id: "hr",    displayName: "Croatian",          espeakTag: "hr",    tgsbTag: "hr"),
    TgsbLanguage(id: "ru",    displayName: "Russian",           espeakTag: "ru",    tgsbTag: "ru"),
    TgsbLanguage(id: "uk",    displayName: "Ukrainian",         espeakTag: "uk",    tgsbTag: "uk"),
    TgsbLanguage(id: "hu",    displayName: "Hungarian",         espeakTag: "hu",    tgsbTag: "hu"),
    TgsbLanguage(id: "fi",    displayName: "Finnish",           espeakTag: "fi",    tgsbTag: "fi"),
    TgsbLanguage(id: "zh",    displayName: "Chinese (Mandarin)",espeakTag: "cmn",   tgsbTag: "zh"),
]

/// Voice preset names (from C bridge).
struct TgsbVoice: Identifiable, Hashable {
    let id: String
    let displayName: String
}

@MainActor
class TgsbEngine: ObservableObject {
    @Published var isSpeaking = false
    @Published var selectedLanguage: TgsbLanguage
    @Published var selectedVoice: TgsbVoice
    @Published var speed: Double = 1.0
    @Published var pitch: Double = 120.0

    let voices: [TgsbVoice]

    private var engine: OpaquePointer?
    private let sampleRate: Int = 22050

    // Audio playback (AVAudioPlayer — output only, no mic permission)
    private var audioPlayer: AVAudioPlayer?
    private var synthQueue = DispatchQueue(label: "com.tgspeechbox.synth",
                                           qos: .userInitiated)

    init() {
        // Gather voice names from C bridge
        let numVoices = tgsb_get_num_voices()
        var v: [TgsbVoice] = []
        for i in 0..<numVoices {
            if let namePtr = tgsb_get_voice_name(Int32(i)) {
                let name = String(cString: namePtr)
                v.append(TgsbVoice(id: name,
                                   displayName: name.capitalized))
            }
        }
        self.voices = v
        self.selectedVoice = v.first ?? TgsbVoice(id: "adam",
                                                   displayName: "Adam")
        self.selectedLanguage = kLanguages[0] // en-us
    }

    func start() -> Bool {
        guard engine == nil else { return true }

        guard let espeakDataDir = Self.resourcePath(for: "espeak-ng-data"),
              let packDir = Self.resourcePath(for: "packs") else {
            print("[TgsbEngine] Runtime data not found in bundle")
            return false
        }

        // espeak_Initialize accepts the espeak-ng-data directory directly
        print("[TgsbEngine] espeakData: \(espeakDataDir)")
        print("[TgsbEngine] packDir: \(packDir)")

        engine = tgsb_create(espeakDataDir, packDir, Int32(sampleRate))
        guard engine != nil else {
            print("[TgsbEngine] tgsb_create failed")
            return false
        }

        // Set initial language and voice
        tgsb_set_language(engine,
                          selectedLanguage.espeakTag,
                          selectedLanguage.tgsbTag)
        tgsb_set_voice(engine, selectedVoice.id)

        print("[TgsbEngine] Engine ready")
        return true
    }

    func shutdown() {
        stopSpeaking()
        if let e = engine {
            tgsb_destroy(e)
            engine = nil
        }
    }

    func speak(_ text: String) {
        guard let eng = engine else { return }
        stopSpeaking()

        // Apply current settings
        tgsb_set_language(eng,
                          selectedLanguage.espeakTag,
                          selectedLanguage.tgsbTag)
        tgsb_set_voice(eng, selectedVoice.id)

        isSpeaking = true

        let speed = self.speed
        let pitch = self.pitch
        let sr = self.sampleRate

        synthQueue.async { [weak self] in
            // Queue text (fast, <10ms)
            tgsb_queue_text(eng, text, speed, pitch)

            // Pull all PCM into a buffer
            let chunkSize = 4096
            var chunk = [Int16](repeating: 0, count: chunkSize)
            var allSamples = [Int16]()

            while true {
                let n = tgsb_pull_audio(eng, &chunk, Int32(chunkSize))
                if n <= 0 { break }
                allSamples.append(contentsOf: chunk.prefix(Int(n)))
            }

            guard !allSamples.isEmpty else {
                DispatchQueue.main.async { self?.isSpeaking = false }
                return
            }

            // Build WAV data in memory
            let wavData = Self.makeWAV(samples: allSamples,
                                        sampleRate: sr)

            DispatchQueue.main.async {
                self?.playWAV(wavData)
            }
        }
    }

    func stopSpeaking() {
        if let eng = engine {
            tgsb_stop(eng)
        }
        audioPlayer?.stop()
        audioPlayer = nil
        isSpeaking = false
    }

    // MARK: - WAV playback

    private func playWAV(_ data: Data) {
        do {
            let player = try AVAudioPlayer(data: data)
            player.delegate = audioDelegate
            let savedVol = UserDefaults(suiteName: kAppGroupId)?.double(forKey: "systemVolume") ?? 0.0
            if savedVol > 0.0 {
                player.volume = Float(savedVol)
            }
            player.play()
            self.audioPlayer = player
        } catch {
            print("[TgsbEngine] AVAudioPlayer error: \(error)")
            isSpeaking = false
        }
    }

    // Delegate to detect playback completion
    private lazy var audioDelegate = AudioDelegate { [weak self] in
        DispatchQueue.main.async {
            self?.isSpeaking = false
        }
    }

    // MARK: - WAV builder

    private static func makeWAV(samples: [Int16], sampleRate: Int) -> Data {
        let numSamples = samples.count
        let dataSize = numSamples * 2
        let fileSize = 36 + dataSize

        var data = Data(capacity: 44 + dataSize)

        func writeU16(_ v: UInt16) {
            var le = v.littleEndian
            data.append(Data(bytes: &le, count: 2))
        }
        func writeU32(_ v: UInt32) {
            var le = v.littleEndian
            data.append(Data(bytes: &le, count: 4))
        }
        func writeTag(_ s: String) {
            data.append(Data(s.utf8))
        }

        // RIFF header
        writeTag("RIFF")
        writeU32(UInt32(fileSize))
        writeTag("WAVE")

        // fmt  chunk
        writeTag("fmt ")
        writeU32(16)                             // chunk size
        writeU16(1)                              // PCM format
        writeU16(1)                              // mono
        writeU32(UInt32(sampleRate))
        writeU32(UInt32(sampleRate * 2))         // byte rate
        writeU16(2)                              // block align
        writeU16(16)                             // bits per sample

        // data chunk
        writeTag("data")
        writeU32(UInt32(dataSize))

        // PCM samples
        samples.withUnsafeBytes { raw in
            data.append(contentsOf: raw)
        }

        return data
    }

    // MARK: - Resource paths

    private static func resourcePath(for name: String) -> String? {
        // Check app bundle first
        if let path = Bundle.main.resourcePath {
            let full = (path as NSString).appendingPathComponent(name)
            if FileManager.default.fileExists(atPath: full) {
                return full
            }
        }

        // Development fallback: look for data relative to the source tree.
        let fm = FileManager.default
        let sourceRoot = (#filePath as NSString)
            .deletingLastPathComponent          // TGSpeechBox/
            .appending("/..")                   // apple/
        let candidates: [String]
        switch name {
        case "espeak-ng-data":
            candidates = [
                (sourceRoot as NSString).appendingPathComponent("Resources/espeak-ng-data"),
                (sourceRoot as NSString).appendingPathComponent("../data/espeak-ng-data"),
            ]
        case "packs":
            candidates = [
                (sourceRoot as NSString).appendingPathComponent("Resources/packs"),
                (sourceRoot as NSString).appendingPathComponent("../../tgspeechbox/packs"),
            ]
        default:
            candidates = []
        }
        for path in candidates {
            let resolved = (path as NSString).standardizingPath
            if fm.fileExists(atPath: resolved) {
                return resolved
            }
        }
        return nil
    }
}

// MARK: - Helpers

private class AudioDelegate: NSObject, AVAudioPlayerDelegate {
    let onFinish: () -> Void
    init(onFinish: @escaping () -> Void) { self.onFinish = onFinish }
    func audioPlayerDidFinishPlaying(_ player: AVAudioPlayer,
                                     successfully flag: Bool) {
        onFinish()
    }
}

private extension UInt16 {
    var littleEndianBytes: [UInt8] {
        let le = self.littleEndian
        return [UInt8(le & 0xFF), UInt8(le >> 8)]
    }
}

private extension UInt32 {
    var littleEndianBytes: [UInt8] {
        let le = self.littleEndian
        return [UInt8(le & 0xFF), UInt8((le >> 8) & 0xFF),
                UInt8((le >> 16) & 0xFF), UInt8((le >> 24) & 0xFF)]
    }
}
