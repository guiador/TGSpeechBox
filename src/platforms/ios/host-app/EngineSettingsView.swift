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

    // Volume
    @State private var systemVolume: Double

    private var defaults: UserDefaults? { UserDefaults(suiteName: kAppGroupId) }

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

        let savedRate = d?.object(forKey: "adv_sampleRate") != nil
            ? d!.integer(forKey: "adv_sampleRate") : 22050
        let idx = kSampleRates.firstIndex { $0.value == savedRate } ?? 2  // default 22050 = index 2
        _sampleRateIndex = State(initialValue: Double(idx))

        let vol = d?.object(forKey: "systemVolume") != nil
            ? d!.double(forKey: "systemVolume") : 1.0
        _systemVolume = State(initialValue: vol > 0.0 ? vol : 1.0)
    }

    var body: some View {
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

                HStack {
                    Text("Sample Rate: \(kSampleRates[Int(sampleRateIndex)].label)")
                        .frame(width: 220, alignment: .leading)
                    Slider(value: $sampleRateIndex, in: 0...Double(kSampleRates.count - 1), step: 1)
                        .onChange(of: sampleRateIndex) { val in
                            let rate = kSampleRates[Int(val)].value
                            defaults?.set(rate, forKey: "adv_sampleRate")
                            engine.changeSampleRate(rate)
                            let d = defaults
                            let ver = (d?.integer(forKey: "adv_settingsVersion") ?? 0) + 1
                            d?.set(ver, forKey: "adv_settingsVersion")
                        }
                }
                .accessibilityElement(children: .combine)
                .accessibilityLabel("Sample rate")
                .accessibilityValue(kSampleRates[Int(sampleRateIndex)].label)

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
                    defaults?.set(val, forKey: "adv_\(key)")
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
