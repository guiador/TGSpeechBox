/*
 * EngineSettingsView.swift — Voice quality sliders + volume.
 *
 * 13 engine sliders (8 VoicingTone + 5 FrameEx), pitch mode picker,
 * inflection scale slider, and volume — all stored in AppGroup
 * UserDefaults so the AU extension picks them up for VoiceOver.
 */

import SwiftUI

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

        _voiceTilt       = State(initialValue: Self.load(d, "voiceTilt", 50))
        _speedQuotient   = State(initialValue: Self.load(d, "speedQuotient", 50))
        _aspirationTilt  = State(initialValue: Self.load(d, "aspirationTilt", 50))
        _cascadeBwScale  = State(initialValue: Self.load(d, "cascadeBwScale", 50))
        _noiseGlottalMod = State(initialValue: Self.load(d, "noiseGlottalMod", 0))
        _pitchSyncF1     = State(initialValue: Self.load(d, "pitchSyncF1", 50))
        _pitchSyncB1     = State(initialValue: Self.load(d, "pitchSyncB1", 50))
        _voiceTremor     = State(initialValue: Self.load(d, "voiceTremor", 0))

        _creakiness       = State(initialValue: Self.load(d, "creakiness", 0))
        _breathiness      = State(initialValue: Self.load(d, "breathiness", 0))
        _jitter           = State(initialValue: Self.load(d, "jitter", 0))
        _shimmer          = State(initialValue: Self.load(d, "shimmer", 0))
        _glottalSharpness = State(initialValue: Self.load(d, "glottalSharpness", 50))

        let savedMode = d?.string(forKey: "adv_pitchMode") ?? "espeak_style"
        _pitchMode = State(initialValue: savedMode)

        _inflectionScale = State(initialValue: Self.load(d, "inflectionScale", 58))
        _inflection = State(initialValue: Self.load(d, "inflection", 50))

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
                Text("This will reset all engine settings to their default values.")
            }

        ScrollView {
            VStack(alignment: .leading, spacing: 24) {

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
                    defaults?.set(val, forKey: "adv_pitchMode")
                    engine.setPitchMode(val)
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

                #if os(tvOS)
                HStack {
                    Text("Sample Rate: \(kSampleRates[Int(sampleRateIndex)].label)")
                        .frame(minWidth: 220, alignment: .leading)
                    Button("-") {
                        sampleRateIndex = max(0, sampleRateIndex - 1)
                        applySampleRate()
                    }
                    Button("+") {
                        sampleRateIndex = min(Double(kSampleRates.count - 1),
                                              sampleRateIndex + 1)
                        applySampleRate()
                    }
                }
                .accessibilityElement(children: .combine)
                .accessibilityLabel("Sample rate")
                .accessibilityValue(kSampleRates[Int(sampleRateIndex)].label)
                #else
                HStack {
                    Text("Sample Rate: \(kSampleRates[Int(sampleRateIndex)].label)")
                        .frame(width: 220, alignment: .leading)
                    Slider(value: $sampleRateIndex, in: 0...Double(kSampleRates.count - 1), step: 1)
                        .onChange(of: sampleRateIndex) { _ in
                            applySampleRate()
                        }
                }
                .accessibilityElement(children: .combine)
                .accessibilityLabel("Sample rate")
                .accessibilityValue(kSampleRates[Int(sampleRateIndex)].label)
                #endif

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
                    let d = defaults
                    let ver = (d?.integer(forKey: "adv_settingsVersion") ?? 0) + 1
                    d?.set(ver, forKey: "adv_settingsVersion")
                }

                #if os(tvOS)
                HStack {
                    Text("Volume: \(Int(systemVolume * 100))%")
                        .frame(minWidth: 180, alignment: .leading)
                    Button("-") {
                        systemVolume = max(0.1, systemVolume - 0.05)
                        defaults?.set(systemVolume, forKey: "systemVolume")
                    }
                    Button("+") {
                        systemVolume = min(1.0, systemVolume + 0.05)
                        defaults?.set(systemVolume, forKey: "systemVolume")
                    }
                }
                .accessibilityElement(children: .combine)
                .accessibilityLabel("System voice volume")
                .accessibilityValue("\(Int(systemVolume * 100)) percent")
                #else
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
                #endif
            }
            .padding(20)
        }
        .accessibilityLabel("Engine settings")
        } // VStack
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

        // Persist all defaults
        let d = defaults
        d?.set(voiceTilt,       forKey: "adv_voiceTilt")
        d?.set(speedQuotient,   forKey: "adv_speedQuotient")
        d?.set(aspirationTilt,  forKey: "adv_aspirationTilt")
        d?.set(cascadeBwScale,  forKey: "adv_cascadeBwScale")
        d?.set(noiseGlottalMod, forKey: "adv_noiseGlottalMod")
        d?.set(pitchSyncF1,     forKey: "adv_pitchSyncF1")
        d?.set(pitchSyncB1,     forKey: "adv_pitchSyncB1")
        d?.set(voiceTremor,     forKey: "adv_voiceTremor")
        d?.set(creakiness,      forKey: "adv_creakiness")
        d?.set(breathiness,     forKey: "adv_breathiness")
        d?.set(jitter,          forKey: "adv_jitter")
        d?.set(shimmer,         forKey: "adv_shimmer")
        d?.set(glottalSharpness, forKey: "adv_glottalSharpness")
        d?.set(pitchMode,       forKey: "adv_pitchMode")
        d?.set(inflectionScale, forKey: "adv_inflectionScale")
        d?.set(inflection,      forKey: "adv_inflection")
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
        #if os(tvOS)
        HStack {
            Text("\(label): \(Int(value.wrappedValue))")
                .frame(minWidth: 220, alignment: .leading)
            Button("-") {
                value.wrappedValue = max(0, value.wrappedValue - 1)
                defaults?.set(value.wrappedValue, forKey: "adv_\(key)")
                applyAllSettings()
            }
            Button("+") {
                value.wrappedValue = min(100, value.wrappedValue + 1)
                defaults?.set(value.wrappedValue, forKey: "adv_\(key)")
                applyAllSettings()
            }
        }
        .accessibilityElement(children: .combine)
        .accessibilityLabel(label)
        .accessibilityValue("\(Int(value.wrappedValue))")
        #else
        HStack {
            Text("\(label): \(Int(value.wrappedValue))")
                .frame(width: 180, alignment: .leading)
            Slider(value: value, in: 0...100, step: 1)
                .onChange(of: value.wrappedValue) { val in
                    defaults?.set(val, forKey: "adv_\(key)")
                    applyAllSettings()
                }
        }
        .accessibilityElement(children: .combine)
        .accessibilityLabel(label)
        .accessibilityValue("\(Int(value.wrappedValue))")
        #endif
    }

    // MARK: - Sample Rate

    private func applySampleRate() {
        let rate = kSampleRates[Int(sampleRateIndex)].value
        defaults?.set(rate, forKey: "adv_sampleRate")
        engine.changeSampleRate(rate)
        let d = defaults
        let ver = (d?.integer(forKey: "adv_settingsVersion") ?? 0) + 1
        d?.set(ver, forKey: "adv_settingsVersion")
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

        // Bump version so AU extension knows to re-read settings
        let d = defaults
        let ver = (d?.integer(forKey: "adv_settingsVersion") ?? 0) + 1
        d?.set(ver, forKey: "adv_settingsVersion")
    }

    // MARK: - UserDefaults loader

    private static func load(
        _ d: UserDefaults?, _ key: String, _ dflt: Double
    ) -> Double {
        guard let d = d, d.object(forKey: "adv_\(key)") != nil else {
            return dflt
        }
        return d.double(forKey: "adv_\(key)")
    }
}
