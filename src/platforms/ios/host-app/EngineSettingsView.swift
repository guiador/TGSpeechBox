/*
 * EngineSettingsView.swift — Voice quality sliders + volume.
 *
 * 13 engine sliders (8 VoicingTone + 5 FrameEx), pitch mode picker,
 * inflection scale slider, and volume — all stored in AppGroup
 * UserDefaults so the AU extension picks them up for VoiceOver.
 *
 * Settings are stored per-voice (Adam, Benjamin, etc.) so each voice
 * can be tuned independently. Output settings (sample rate, pause mode,
 * volume) are global.
 */

import SwiftUI

private let kVoiceNames = ["Adam", "Benjamin", "Caleb", "David", "Robert"]

private let kPitchModes = [
    ("espeak_style",   "eSpeak Style"),
    ("legacy",         "Legacy"),
    ("fujisaki_style", "Fujisaki Style"),
    ("impulse_style",  "Impulse Style"),
    ("klatt_style",    "Klatt Style"),
]

private let kPauseModes = [
    (0, "Off"),
    (1, "Short"),
    (2, "Long"),
]

private let kSampleRates: [(value: Int, label: String)] = [
    (11025, "11,025 Hz"),
    (16000, "16,000 Hz"),
    (22050, "22,050 Hz"),
    (44100, "44,100 Hz"),
]

struct EngineSettingsView: View {
    @ObservedObject var engine: TgsbEngine
    @Binding var engineStarted: Bool

    // Which voice we're editing
    @State private var selectedVoice: String

    // VoicingTone sliders (0–100)
    @State private var voiceTilt: Double
    @State private var speedQuotient: Double
    @State private var aspirationTilt: Double
    @State private var cascadeBwScale: Double
    @State private var noiseGlottalMod: Double
    @State private var pitchSyncF1: Double
    @State private var pitchSyncB1: Double
    @State private var voiceTremor: Double

    // FrameEx sliders (0–100)
    @State private var creakiness: Double
    @State private var breathiness: Double
    @State private var jitter: Double
    @State private var shimmer: Double
    @State private var glottalSharpness: Double

    // Pitch mode + inflection
    @State private var pitchMode: String
    @State private var inflectionScale: Double
    @State private var inflection: Double

    // Sample rate (slider index into kSampleRates: 0–3)
    @State private var sampleRateIndex: Double

    // Pause mode (0=off, 1=short, 2=long)
    @State private var pauseMode: Int

    // Volume
    @State private var systemVolume: Double

    private var defaults: UserDefaults? { UserDefaults(suiteName: kAppGroupId) }

    @State private var showResetAlert = false

    init(engine: TgsbEngine, engineStarted: Binding<Bool>) {
        _engine = ObservedObject(wrappedValue: engine)
        _engineStarted = engineStarted

        let d = UserDefaults(suiteName: kAppGroupId)

        let voice = d?.string(forKey: "adv_selectedVoice") ?? "adam"
        _selectedVoice = State(initialValue: voice)

        _voiceTilt       = State(initialValue: Self.loadV(d, "voiceTilt", 50, voice))
        _speedQuotient   = State(initialValue: Self.loadV(d, "speedQuotient", 50, voice))
        _aspirationTilt  = State(initialValue: Self.loadV(d, "aspirationTilt", 50, voice))
        _cascadeBwScale  = State(initialValue: Self.loadV(d, "cascadeBwScale", 50, voice))
        _noiseGlottalMod = State(initialValue: Self.loadV(d, "noiseGlottalMod", 0, voice))
        _pitchSyncF1     = State(initialValue: Self.loadV(d, "pitchSyncF1", 50, voice))
        _pitchSyncB1     = State(initialValue: Self.loadV(d, "pitchSyncB1", 50, voice))
        _voiceTremor     = State(initialValue: Self.loadV(d, "voiceTremor", 0, voice))

        _creakiness       = State(initialValue: Self.loadV(d, "creakiness", 0, voice))
        _breathiness      = State(initialValue: Self.loadV(d, "breathiness", 0, voice))
        _jitter           = State(initialValue: Self.loadV(d, "jitter", 0, voice))
        _shimmer          = State(initialValue: Self.loadV(d, "shimmer", 0, voice))
        _glottalSharpness = State(initialValue: Self.loadV(d, "glottalSharpness", 50, voice))

        let modeKey = "adv_pitchMode.\(voice)"
        let savedMode = d?.string(forKey: modeKey)
            ?? d?.string(forKey: "adv_pitchMode")
            ?? "espeak_style"
        _pitchMode = State(initialValue: savedMode)

        _inflectionScale = State(initialValue: Self.loadV(d, "inflectionScale", 58, voice))
        _inflection = State(initialValue: Self.loadV(d, "inflection", 50, voice))

        let savedPause = d?.object(forKey: "adv_pauseMode") != nil
            ? d!.integer(forKey: "adv_pauseMode") : 1  // default: short
        _pauseMode = State(initialValue: savedPause)

        let savedRate = d?.object(forKey: "adv_sampleRate") != nil
            ? d!.integer(forKey: "adv_sampleRate") : 22050
        let idx = kSampleRates.firstIndex { $0.value == savedRate } ?? 2  // default 22050 = index 2
        _sampleRateIndex = State(initialValue: Double(idx))

        let vol = d?.object(forKey: "systemVolume") != nil
            ? d!.double(forKey: "systemVolume") : 1.0
        _systemVolume = State(initialValue: vol > 0.0 ? vol : 1.0)
    }

    var body: some View {
        VStack(spacing: 0) {
        Button("Reset to Defaults") { showResetAlert = true }
            .padding(.horizontal, 20)
            .padding(.vertical, 10)
            .frame(maxWidth: .infinity, alignment: .leading)
            .alert("Reset to Defaults", isPresented: $showResetAlert) {
                Button("Reset", role: .destructive) { resetToDefaults() }
                Button("Cancel", role: .cancel) { }
            } message: {
                Text("This will reset engine settings for \(selectedVoice.capitalized) to their default values.")
            }

        ScrollView {
            VStack(alignment: .leading, spacing: 24) {

                // ── Voice Picker ───────────────────────────
                Text("Voice")
                    .font(.headline)
                    .accessibilityAddTraits(.isHeader)

                Picker("Editing voice", selection: $selectedVoice) {
                    ForEach(kVoiceNames, id: \.self) { name in
                        Text(name).tag(name.lowercased())
                    }
                }
                .pickerStyle(.segmented)
                .accessibilityLabel("Editing voice")
                .accessibilityValue(selectedVoice.capitalized)
                .onChange(of: selectedVoice) { voice in
                    defaults?.set(voice, forKey: "adv_selectedVoice")
                    loadSettingsForVoice(voice)
                }

                // ── Pitch ───────────────────────────────────
                Text("Pitch")
                    .font(.headline)
                    .accessibilityAddTraits(.isHeader)

                Picker("Pitch Mode", selection: $pitchMode) {
                    ForEach(kPitchModes, id: \.0) { mode in
                        Text(mode.1).tag(mode.0)
                    }
                }
                .accessibilityLabel("Pitch mode")
                .accessibilityValue(pitchModeDisplayName)
                .onChange(of: pitchMode) { val in
                    defaults?.set(val, forKey: "adv_pitchMode.\(selectedVoice)")
                    engine.setPitchMode(val)
                    bumpVersion()
                }

                toneSlider("Inflection Scale", $inflectionScale, "inflectionScale")

                Divider()

                // ── Voice Tone ──────────────────────────────
                Text("Voice Tone")
                    .font(.headline)
                    .accessibilityAddTraits(.isHeader)

                toneSlider("Voice Tilt", $voiceTilt, "voiceTilt")
                toneSlider("Speed Quotient", $speedQuotient, "speedQuotient")
                toneSlider("Aspiration Tilt", $aspirationTilt, "aspirationTilt")
                toneSlider("Formant Sharpness", $cascadeBwScale, "cascadeBwScale")
                toneSlider("Noise Modulation", $noiseGlottalMod, "noiseGlottalMod")
                toneSlider("Pitch-Sync F1", $pitchSyncF1, "pitchSyncF1")
                toneSlider("Pitch-Sync B1", $pitchSyncB1, "pitchSyncB1")
                toneSlider("Voice Tremor", $voiceTremor, "voiceTremor")

                Divider()

                // ── Voice Character ─────────────────────────
                Text("Voice Character")
                    .font(.headline)
                    .accessibilityAddTraits(.isHeader)

                toneSlider("Inflection", $inflection, "inflection")
                toneSlider("Creakiness", $creakiness, "creakiness")
                toneSlider("Breathiness", $breathiness, "breathiness")
                toneSlider("Jitter", $jitter, "jitter")
                toneSlider("Shimmer", $shimmer, "shimmer")
                toneSlider("Glottal Sharpness", $glottalSharpness, "glottalSharpness")

                Divider()

                // ── Output ──────────────────────────────────
                Text("Output")
                    .font(.headline)
                    .accessibilityAddTraits(.isHeader)

                HStack {
                    Text("Sample Rate: \(kSampleRates[Int(sampleRateIndex)].label)")
                        .frame(width: 220, alignment: .leading)
                    Slider(value: $sampleRateIndex, in: 0...Double(kSampleRates.count - 1), step: 1)
                        .onChange(of: sampleRateIndex) { val in
                            let rate = kSampleRates[Int(val)].value
                            defaults?.set(rate, forKey: "adv_sampleRate")
                            engine.changeSampleRate(rate)
                            bumpVersion()
                        }
                }
                .accessibilityElement(children: .combine)
                .accessibilityLabel("Sample rate")
                .accessibilityValue(kSampleRates[Int(sampleRateIndex)].label)

                Picker("Pause Mode", selection: $pauseMode) {
                    ForEach(kPauseModes, id: \.0) { mode in
                        Text(mode.1).tag(mode.0)
                    }
                }
                .accessibilityLabel("Pause mode")
                .accessibilityValue(kPauseModes.first { $0.0 == pauseMode }?.1 ?? "Short")
                .onChange(of: pauseMode) { val in
                    defaults?.set(val, forKey: "adv_pauseMode")
                    engine.setPauseMode(val)
                    bumpVersion()
                }

                HStack {
                    Text("Volume: \(Int(systemVolume * 100))%")
                        .frame(width: 180, alignment: .leading)
                    Slider(value: $systemVolume, in: 0.1...1.0, step: 0.05)
                        .onChange(of: systemVolume) { val in
                            defaults?.set(val, forKey: "systemVolume")
                        }
                }
                .accessibilityElement(children: .combine)
                .accessibilityLabel("System voice volume")
                .accessibilityValue("\(Int(systemVolume * 100)) percent")
            }
            .padding(20)
        }
        .accessibilityLabel("Engine settings")
        } // VStack
    }

    // MARK: - Load settings for voice switch

    private func loadSettingsForVoice(_ voice: String) {
        let d = defaults

        voiceTilt       = Self.loadV(d, "voiceTilt", 50, voice)
        speedQuotient   = Self.loadV(d, "speedQuotient", 50, voice)
        aspirationTilt  = Self.loadV(d, "aspirationTilt", 50, voice)
        cascadeBwScale  = Self.loadV(d, "cascadeBwScale", 50, voice)
        noiseGlottalMod = Self.loadV(d, "noiseGlottalMod", 0, voice)
        pitchSyncF1     = Self.loadV(d, "pitchSyncF1", 50, voice)
        pitchSyncB1     = Self.loadV(d, "pitchSyncB1", 50, voice)
        voiceTremor     = Self.loadV(d, "voiceTremor", 0, voice)

        creakiness       = Self.loadV(d, "creakiness", 0, voice)
        breathiness      = Self.loadV(d, "breathiness", 0, voice)
        jitter           = Self.loadV(d, "jitter", 0, voice)
        shimmer          = Self.loadV(d, "shimmer", 0, voice)
        glottalSharpness = Self.loadV(d, "glottalSharpness", 50, voice)

        pitchMode = d?.string(forKey: "adv_pitchMode.\(voice)")
            ?? d?.string(forKey: "adv_pitchMode")
            ?? "espeak_style"

        inflectionScale = Self.loadV(d, "inflectionScale", 58, voice)
        inflection      = Self.loadV(d, "inflection", 50, voice)
    }

    // MARK: - Reset

    private func resetToDefaults() {
        voiceTilt = 50;       speedQuotient = 50
        aspirationTilt = 50;  cascadeBwScale = 50
        noiseGlottalMod = 0;  pitchSyncF1 = 50
        pitchSyncB1 = 50;     voiceTremor = 0

        creakiness = 0;       breathiness = 0
        jitter = 0;           shimmer = 0
        glottalSharpness = 50

        pitchMode = "espeak_style"
        inflectionScale = 58; inflection = 50

        pauseMode = 1         // short
        sampleRateIndex = 2   // 22050 Hz
        systemVolume = 1.0

        // Persist per-voice defaults
        let d = defaults
        let v = selectedVoice
        d?.set(voiceTilt,       forKey: "adv_voiceTilt.\(v)")
        d?.set(speedQuotient,   forKey: "adv_speedQuotient.\(v)")
        d?.set(aspirationTilt,  forKey: "adv_aspirationTilt.\(v)")
        d?.set(cascadeBwScale,  forKey: "adv_cascadeBwScale.\(v)")
        d?.set(noiseGlottalMod, forKey: "adv_noiseGlottalMod.\(v)")
        d?.set(pitchSyncF1,     forKey: "adv_pitchSyncF1.\(v)")
        d?.set(pitchSyncB1,     forKey: "adv_pitchSyncB1.\(v)")
        d?.set(voiceTremor,     forKey: "adv_voiceTremor.\(v)")
        d?.set(creakiness,      forKey: "adv_creakiness.\(v)")
        d?.set(breathiness,     forKey: "adv_breathiness.\(v)")
        d?.set(jitter,          forKey: "adv_jitter.\(v)")
        d?.set(shimmer,         forKey: "adv_shimmer.\(v)")
        d?.set(glottalSharpness, forKey: "adv_glottalSharpness.\(v)")
        d?.set(pitchMode,       forKey: "adv_pitchMode.\(v)")
        d?.set(inflectionScale, forKey: "adv_inflectionScale.\(v)")
        d?.set(inflection,      forKey: "adv_inflection.\(v)")

        // Global output settings
        d?.set(pauseMode,       forKey: "adv_pauseMode")
        d?.set(22050,           forKey: "adv_sampleRate")
        d?.set(systemVolume,    forKey: "systemVolume")

        // Apply to engine
        engine.setPitchMode(pitchMode)
        engine.setPauseMode(pauseMode)
        engine.changeSampleRate(22050)
        applyAllSettings()
    }

    private var pitchModeDisplayName: String {
        kPitchModes.first { $0.0 == pitchMode }?.1 ?? pitchMode
    }

    // MARK: - Slider helper

    @ViewBuilder
    private func toneSlider(
        _ label: String,
        _ value: Binding<Double>,
        _ key: String
    ) -> some View {
        HStack {
            Text("\(label): \(Int(value.wrappedValue))")
                .frame(width: 180, alignment: .leading)
            Slider(value: value, in: 0...100, step: 1)
                .onChange(of: value.wrappedValue) { val in
                    defaults?.set(val, forKey: "adv_\(key).\(selectedVoice)")
                    applyAllSettings()
                }
        }
        .accessibilityElement(children: .combine)
        .accessibilityLabel(label)
        .accessibilityValue("\(Int(value.wrappedValue))")
    }

    // MARK: - Apply

    private func applyAllSettings() {
        engine.applyVoicingToneFromSliders(
            voiceTilt: voiceTilt,
            speedQuotient: speedQuotient,
            aspirationTilt: aspirationTilt,
            cascadeBwScale: cascadeBwScale,
            noiseGlottalMod: noiseGlottalMod,
            pitchSyncF1: pitchSyncF1,
            pitchSyncB1: pitchSyncB1,
            voiceTremor: voiceTremor)

        engine.applyFrameExFromSliders(
            creakiness: creakiness,
            breathiness: breathiness,
            jitter: jitter,
            shimmer: shimmer,
            glottalSharpness: glottalSharpness)

        engine.setInflectionScale(inflectionScale / 100.0)
        engine.setInflection(inflection / 100.0)

        bumpVersion()
    }

    private func bumpVersion() {
        let d = defaults
        let ver = (d?.integer(forKey: "adv_settingsVersion") ?? 0) + 1
        d?.set(ver, forKey: "adv_settingsVersion")
    }

    // MARK: - UserDefaults loader (per-voice with global fallback)

    /// Load a per-voice setting. Falls back to the old global key for
    /// migration, then to the provided default.
    private static func loadV(
        _ d: UserDefaults?, _ key: String, _ dflt: Double, _ voice: String
    ) -> Double {
        guard let d = d else { return dflt }
        let voiceKey = "adv_\(key).\(voice)"
        if d.object(forKey: voiceKey) != nil {
            return d.double(forKey: voiceKey)
        }
        // Fallback: old global key (pre-per-voice migration)
        let globalKey = "adv_\(key)"
        if d.object(forKey: globalKey) != nil {
            return d.double(forKey: globalKey)
        }
        return dflt
    }
}
