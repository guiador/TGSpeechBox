/*
 * TGSBAudioUnit.swift — AVSpeechSynthesisProviderAudioUnit subclass.
 *
 * Registers TGSpeechBox as a system-wide speech synthesizer,
 * usable by VoiceOver, Spoken Content, and any AVSpeechSynthesizer client.
 *
 * Both macOS and iOS run the full pipeline (eSpeak + frontend + DSP)
 * in-process. Apple hosts AU extensions in a separate process from
 * VoiceOver on both platforms, so crash isolation is built in.
 *
 * License: GPL-3.0 (links eSpeak-ng)
 */

import AVFoundation
import CoreMedia

public class TGSBAudioUnit: AVSpeechSynthesisProviderAudioUnit {

    private var engine: OpaquePointer?    // TgsbEngine* (full pipeline)

    // Audio buffer — written by synthesizeSpeechRequest (synchronous),
    // read by render block. Protected by outputMutex.
    private var output: [Float32] = []
    private var outputOffset: Int = 0
    private var volume: Float32 = 1.0
    private let outputMutex = DispatchSemaphore(value: 1)

    private let sampleRate: Double = 22050.0

    // Audio Unit output bus.
    private let outputBus: AUAudioUnitBus
    private var _outputBusses: AUAudioUnitBusArray!
    private let outputFormat: AVAudioFormat

    // Language mapping: BCP-47 tag -> (espeakTag, tgsbTag)
    private static let languageMap: [(bcp47: String, espeak: String, tgsb: String)] = [
        ("en-US", "en-us", "en-us"),
        ("en-GB", "en-gb", "en-gb"),
        ("en-CA", "en-us", "en-us"),
        ("en-AU", "en",    "en"),
        ("fr-FR", "fr",    "fr"),
        ("fr-CA", "fr",    "fr"),
        ("es-ES", "es",    "es"),
        ("es-MX", "es-419","es-mx"),
        ("it-IT", "it",    "it"),
        ("pt-BR", "pt-br", "pt-br"),
        ("pt-PT", "pt",    "pt"),
        ("ro-RO", "ro",    "ro"),
        ("de-DE", "de",    "de"),
        ("nl-NL", "nl",    "nl"),
        ("da-DK", "da",    "da"),
        ("sv-SE", "sv",    "sv"),
        ("pl-PL", "pl",    "pl"),
        ("cs-CZ", "cs",    "cs"),
        ("sk-SK", "sk",    "sk"),
        ("bg-BG", "bg",    "bg"),
        ("hr-HR", "hr",    "hr"),
        ("ru-RU", "ru",    "ru"),
        ("uk-UA", "uk",    "uk"),
        ("hu-HU", "hu",    "hu"),
        ("fi-FI", "fi",    "fi"),
        ("zh-CN", "cmn",   "zh"),
    ]

    // MARK: - Initialization

    @objc
    public override init(componentDescription: AudioComponentDescription,
                         options: AudioComponentInstantiationOptions = []) throws {

        let asbd = AudioStreamBasicDescription(
            mSampleRate: 22050,
            mFormatID: kAudioFormatLinearPCM,
            mFormatFlags: kAudioFormatFlagsNativeFloatPacked
                        | kAudioFormatFlagIsNonInterleaved,
            mBytesPerPacket: 4,
            mFramesPerPacket: 1,
            mBytesPerFrame: 4,
            mChannelsPerFrame: 1,
            mBitsPerChannel: 32,
            mReserved: 0)

        outputFormat = AVAudioFormat(
            cmAudioFormatDescription: try CMAudioFormatDescription(
                audioStreamBasicDescription: asbd))
        outputBus = try AUAudioUnitBus(format: outputFormat)

        try super.init(componentDescription: componentDescription,
                       options: options)

        _outputBusses = AUAudioUnitBusArray(
            audioUnit: self,
            busType: .output,
            busses: [outputBus])

        initializeBackend()
    }

    private func initializeBackend() {
        guard let bundle = Bundle(for: TGSBAudioUnit.self).resourcePath else { return }
        let fm = FileManager.default

        let espeakDataPath = bundle + "/espeak-ng-data"
        let packDir = bundle + "/packs"
        guard fm.fileExists(atPath: espeakDataPath),
              fm.fileExists(atPath: packDir) else { return }
        engine = tgsb_create(espeakDataPath, packDir, Int32(sampleRate))
    }

    deinit {
        if let e = engine { tgsb_destroy(e) }
    }

    // MARK: - Voices

    public override var speechVoices: [AVSpeechSynthesisProviderVoice] {
        get {
            let voiceNames = ["Adam", "Benjamin", "Caleb", "David", "Robert"]
            var voices: [AVSpeechSynthesisProviderVoice] = []

            for name in voiceNames {
                for lang in Self.languageMap {
                    let voice = AVSpeechSynthesisProviderVoice(
                        name: "\(name) (\(lang.bcp47))",
                        identifier: "com.tgspeechbox.\(name.lowercased()).\(lang.bcp47.lowercased())",
                        primaryLanguages: [lang.bcp47],
                        supportedLanguages: [lang.bcp47]
                    )
                    voice.gender = .male
                    voices.append(voice)
                }
            }

            return voices
        }
        set { }
    }

    // MARK: - Audio Unit Bus

    public override var outputBusses: AUAudioUnitBusArray {
        return _outputBusses
    }

    public override func allocateRenderResources() throws {
        try super.allocateRenderResources()
    }

    // MARK: - Synthesis

    public override func synthesizeSpeechRequest(
        _ speechRequest: AVSpeechSynthesisProviderRequest
    ) {
        let plainText = extractPlainText(from: speechRequest.ssmlRepresentation)
        guard !plainText.isEmpty else { return }
        guard let eng = engine else { return }

        let parts = speechRequest.voice.identifier.split(separator: ".")
        let voiceName = parts.count >= 3 ? String(parts[2]) : "adam"
        let bcp47 = parts.count >= 4 ? String(parts[3]) : "en-us"

        let langEntry = Self.languageMap.first {
            $0.bcp47.lowercased() == bcp47.lowercased()
        }
        let espeakLang = langEntry?.espeak ?? "en-us"
        let tgsbLang = langEntry?.tgsb ?? "en-us"

        let ssml = speechRequest.ssmlRepresentation
        let speed = extractRate(from: ssml)
        let pitch = extractPitch(from: ssml)

        // Volume: SSML prosody if present, multiplied by shared app setting
        var vol = Float32(extractVolume(from: ssml))
        let savedVol = UserDefaults(suiteName: "group.com.tgspeechbox.app")?.double(forKey: "systemVolume") ?? 0.0
        if savedVol > 0.0 {
            vol *= Float32(savedVol)
        }

        // Synchronous: generate all audio, then hand to render block.
        tgsb_set_voice(eng, voiceName)
        tgsb_set_language(eng, espeakLang, tgsbLang)
        applyEngineSettings(eng)
        tgsb_queue_text(eng, plainText, speed, pitch)

        var samples: [Float32] = []
        samples.reserveCapacity(22050 * 5)

        let chunkSize = 4096
        var chunk = [Int16](repeating: 0, count: chunkSize)

        while true {
            let n = Int(tgsb_pull_audio(eng, &chunk, Int32(chunkSize)))
            if n <= 0 { break }
            for i in 0..<n {
                samples.append(Float32(chunk[i]) / 32768.0)
            }
        }

        // Hand complete buffer to the render block
        outputMutex.wait()
        output = samples
        outputOffset = 0
        volume = vol
        outputMutex.signal()
    }

    public override func cancelSpeechRequest() {
        if let e = engine { tgsb_stop(e) }

        outputMutex.wait()
        output.removeAll(keepingCapacity: true)
        outputOffset = 0
        outputMutex.signal()
    }

    // MARK: - Audio Render

    public override var internalRenderBlock: AUInternalRenderBlock {
        return {
            actionFlags, timestamp, frameCount, outputBusNumber,
            outputAudioBufferList, _, _ in

            self.outputMutex.wait()

            let available = self.output.count - self.outputOffset
            let toCopy = min(Int(frameCount), available)

            let outBuf = UnsafeMutableAudioBufferListPointer(
                outputAudioBufferList)[0]
            let outFrames = outBuf.mData!
                .assumingMemoryBound(to: Float32.self)

            if toCopy > 0 {
                let start = self.outputOffset
                let vol = self.volume
                self.output.withUnsafeBufferPointer { buf in
                    for i in 0..<toCopy {
                        outFrames[i] = buf[start + i] * vol
                    }
                }
                self.outputOffset += toCopy
            }

            // Zero-fill any remaining frames in this render call
            for i in toCopy..<Int(frameCount) { outFrames[i] = 0 }

            // Tell the system how many bytes we actually wrote
            outputAudioBufferList.pointee.mBuffers.mDataByteSize =
                UInt32(toCopy * MemoryLayout<Float32>.size)

            // Signal complete when buffer is fully drained
            if self.outputOffset >= self.output.count && !self.output.isEmpty {
                self.output.removeAll()
                self.outputOffset = 0
                self.outputMutex.signal()
                actionFlags.pointee = .offlineUnitRenderAction_Complete
                return noErr
            }

            self.outputMutex.signal()
            return noErr
        }
    }

    // MARK: - Engine Settings from AppGroup

    private func applyEngineSettings(_ eng: OpaquePointer) {
        let d = UserDefaults(suiteName: "group.com.tgspeechbox.app")

        func load(_ key: String, _ dflt: Double) -> Double {
            guard let d = d, d.object(forKey: "adv_\(key)") != nil
            else { return dflt }
            return d.double(forKey: "adv_\(key)")
        }

        // VoicingTone: convert 0–100 sliders to engine parameters
        let voiceTilt      = load("voiceTilt", 50)
        let speedQuotient  = load("speedQuotient", 50)
        let aspirationTilt = load("aspirationTilt", 50)
        let cascadeBwScale = load("cascadeBwScale", 50)
        let noiseGlottalMod = load("noiseGlottalMod", 0)
        let pitchSyncF1    = load("pitchSyncF1", 50)
        let pitchSyncB1    = load("pitchSyncB1", 50)
        let voiceTremor    = load("voiceTremor", 0)

        let tilt     = (voiceTilt - 50.0) * (24.0 / 50.0)
        let noiseMod = noiseGlottalMod / 100.0
        let psF1     = (pitchSyncF1 - 50.0) * 1.2
        let psB1     = (pitchSyncB1 - 50.0) * 1.0
        let sq       = speedQuotient <= 50.0
            ? 0.5 + (speedQuotient / 50.0) * 1.5
            : 2.0 + ((speedQuotient - 50.0) / 50.0) * 2.0
        let aspTilt  = (aspirationTilt - 50.0) * 0.24
        let bw       = cascadeBwScale <= 50.0
            ? 2.0 - (cascadeBwScale / 50.0) * 1.1
            : 0.9 - ((cascadeBwScale - 50.0) / 50.0) * 0.6
        let tremor   = (voiceTremor / 100.0) * 0.4

        tgsb_set_voicing_tone(eng, tilt, noiseMod, psF1, psB1,
                              sq, aspTilt, bw, tremor)

        // FrameEx: convert 0–100 sliders to engine parameters
        let creak    = load("creakiness", 0) / 100.0
        let breath   = load("breathiness", 0) / 100.0
        let jit      = load("jitter", 0) / 100.0
        let shim     = load("shimmer", 0) / 100.0
        let sharp    = load("glottalSharpness", 50) / 50.0

        tgsb_set_frame_ex_defaults(eng, creak, breath, jit, shim, sharp)

        // Pitch mode
        let mode = d?.string(forKey: "adv_pitchMode") ?? "espeak_style"
        tgsb_set_pitch_mode(eng, mode)

        let inflScale = load("inflectionScale", 58) / 100.0
        tgsb_set_legacy_pitch_inflection_scale(eng, inflScale)
    }

    // MARK: - Helpers

    private func extractPlainText(from ssml: String) -> String {
        var text = ssml.replacingOccurrences(of: "<[^>]+>", with: " ",
                                              options: .regularExpression)
        text = text.replacingOccurrences(of: "&apos;", with: "'")
        text = text.replacingOccurrences(of: "&quot;", with: "\"")
        text = text.replacingOccurrences(of: "&amp;",  with: "&")
        text = text.replacingOccurrences(of: "&lt;",   with: "<")
        text = text.replacingOccurrences(of: "&gt;",   with: ">")
        text = text.replacingOccurrences(of: "&#39;",  with: "'")
        text = text.replacingOccurrences(of: "&#34;",  with: "\"")
        text = text.replacingOccurrences(of: "\\s+", with: " ",
                                          options: .regularExpression)
        return text.trimmingCharacters(in: .whitespacesAndNewlines)
    }

    private func extractRate(from ssml: String) -> Double {
        guard let match = ssml.range(
            of: #"<prosody[^>]*\brate="([^"]+)""#,
            options: .regularExpression
        ) else { return 1.0 }

        let tag = String(ssml[match])
        guard let valRange = tag.range(
            of: #"rate="([^"]+)""#, options: .regularExpression
        ) else { return 1.0 }

        var val = String(tag[valRange])
            .replacingOccurrences(of: "rate=\"", with: "")
            .replacingOccurrences(of: "\"", with: "")
            .trimmingCharacters(in: .whitespaces)

        switch val {
        case "x-slow":  return 0.3
        case "slow":    return 0.6
        case "medium":  return 1.0
        case "fast":    return 2.0
        case "x-fast":  return 3.5
        default: break
        }

        if val.hasSuffix("%") {
            val.removeLast()
            if let pct = Double(val) { return max(0.1, pct / 100.0) }
        }
        if let num = Double(val) { return max(0.1, num) }
        return 1.0
    }

    private func extractPitch(from ssml: String) -> Double {
        let defaultPitch = 120.0
        guard let match = ssml.range(
            of: #"<prosody[^>]*\bpitch="([^"]+)""#,
            options: .regularExpression
        ) else { return defaultPitch }

        let tag = String(ssml[match])
        guard let valRange = tag.range(
            of: #"pitch="([^"]+)""#, options: .regularExpression
        ) else { return defaultPitch }

        var val = String(tag[valRange])
            .replacingOccurrences(of: "pitch=\"", with: "")
            .replacingOccurrences(of: "\"", with: "")
            .trimmingCharacters(in: .whitespaces)

        switch val {
        case "x-low":  return 70.0
        case "low":    return 90.0
        case "medium": return 120.0
        case "high":   return 160.0
        case "x-high": return 200.0
        default: break
        }

        if val.lowercased().hasSuffix("hz") {
            val = String(val.dropLast(2))
            if let hz = Double(val) { return max(40.0, min(hz, 500.0)) }
        }
        if val.hasSuffix("%") {
            val.removeLast()
            if let pct = Double(val) {
                return max(40.0, min(defaultPitch * (1.0 + pct / 100.0), 500.0))
            }
        }
        if let num = Double(val) { return max(40.0, min(num, 500.0)) }
        return defaultPitch
    }

    private func extractVolume(from ssml: String) -> Double {
        guard let match = ssml.range(
            of: #"<prosody[^>]*\bvolume="([^"]+)""#,
            options: .regularExpression
        ) else { return 1.0 }

        let tag = String(ssml[match])
        guard let valRange = tag.range(
            of: #"volume="([^"]+)""#, options: .regularExpression
        ) else { return 1.0 }

        var val = String(tag[valRange])
            .replacingOccurrences(of: "volume=\"", with: "")
            .replacingOccurrences(of: "\"", with: "")
            .trimmingCharacters(in: .whitespaces)

        switch val {
        case "silent": return 0.0
        case "x-soft": return 0.25
        case "soft":   return 0.5
        case "medium": return 1.0
        case "loud":   return 1.5
        case "x-loud": return 2.0
        default: break
        }

        if val.hasSuffix("%") {
            val.removeLast()
            if let pct = Double(val) { return max(0.0, min(pct / 100.0, 2.0)) }
        }
        if let num = Double(val) { return max(0.0, min(num, 2.0)) }
        return 1.0
    }
}
