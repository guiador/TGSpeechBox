/*
 * ContentView.swift — Main UI for TGSpeechBox.
 *
 * Text input, language picker, voice picker, speed/pitch controls.
 */

import SwiftUI

let kAppGroupId = "group.com.tgspeechbox.app"

struct ContentView: View {
    @StateObject private var engine = TgsbEngine()
    @State private var text = "Hello world. This is TGSpeechBox running on Apple."
    @State private var engineStarted = false
    @State private var errorMessage: String?
    @State private var systemVolume: Double = {
        let v = UserDefaults(suiteName: kAppGroupId)?.double(forKey: "systemVolume") ?? 0.0
        return v > 0.0 ? v : 1.0
    }()

    var body: some View {
        VStack(spacing: 16) {
            Text("TGSpeechBox")
                .font(.largeTitle)
                .accessibilityAddTraits(.isHeader)

            if let error = errorMessage {
                Text(error)
                    .foregroundColor(.red)
                    .accessibilityLabel("Error: \(error)")
            }

            // Text input
            Group {
                #if os(macOS)
                TextEditor(text: $text)
                    .frame(minHeight: 100)
                    .border(Color.gray.opacity(0.3))
                    .accessibilityLabel("Text to speak")
                #else
                TextEditor(text: $text)
                    .frame(minHeight: 100)
                    .overlay(RoundedRectangle(cornerRadius: 8)
                        .stroke(Color.gray.opacity(0.3)))
                    .accessibilityLabel("Text to speak")
                #endif
            }

            // Controls
            HStack(spacing: 12) {
                // Language picker
                Picker("Language", selection: $engine.selectedLanguage) {
                    ForEach(kLanguages) { lang in
                        Text(lang.displayName).tag(lang)
                    }
                }
                .accessibilityLabel("Language")
                #if os(macOS)
                .frame(maxWidth: 200)
                #endif

                // Voice picker
                Picker("Voice", selection: $engine.selectedVoice) {
                    ForEach(engine.voices) { voice in
                        Text(voice.displayName).tag(voice)
                    }
                }
                .accessibilityLabel("Voice")
                #if os(macOS)
                .frame(maxWidth: 150)
                #endif
            }

            // Speed and pitch sliders
            VStack(spacing: 8) {
                HStack {
                    Text("Speed: \(engine.speed, specifier: "%.1f")x")
                        .frame(width: 100, alignment: .leading)
                    Slider(value: $engine.speed, in: 0.3...3.0, step: 0.1)
                        .accessibilityLabel("Speed")
                        .accessibilityValue("\(engine.speed, specifier: "%.1f") times")
                }
                HStack {
                    Text("Pitch: \(Int(engine.pitch)) Hz")
                        .frame(width: 100, alignment: .leading)
                    Slider(value: $engine.pitch, in: 40...300, step: 5)
                        .accessibilityLabel("Pitch")
                        .accessibilityValue("\(Int(engine.pitch)) hertz")
                }
                HStack {
                    Text("Volume: \(Int(systemVolume * 100))%")
                        .frame(width: 100, alignment: .leading)
                    Slider(value: $systemVolume, in: 0.1...1.0, step: 0.05)
                        .accessibilityLabel("System voice volume")
                        .accessibilityValue("\(Int(systemVolume * 100)) percent")
                        .onChange(of: systemVolume) { newValue in
                            UserDefaults(suiteName: kAppGroupId)?.set(newValue, forKey: "systemVolume")
                        }
                }
            }

            // Speak / Stop buttons
            HStack(spacing: 16) {
                Button(action: {
                    if !engineStarted {
                        if engine.start() {
                            engineStarted = true
                            errorMessage = nil
                        } else {
                            errorMessage = "Failed to initialize engine"
                            return
                        }
                    }
                    engine.speak(text)
                }) {
                    Label("Speak", systemImage: "play.fill")
                        .frame(minWidth: 80)
                }
                .disabled(engine.isSpeaking || text.isEmpty)
                .keyboardShortcut(.return, modifiers: .command)
                .accessibilityLabel("Speak")
                .accessibilityHint("Press to speak the entered text")

                Button(action: {
                    engine.stopSpeaking()
                }) {
                    Label("Stop", systemImage: "stop.fill")
                        .frame(minWidth: 80)
                }
                .disabled(!engine.isSpeaking)
                .keyboardShortcut(.escape, modifiers: [])
                .accessibilityLabel("Stop")
                .accessibilityHint("Press to stop speaking")
            }
            .buttonStyle(.borderedProminent)
        }
        .padding(20)
        #if os(macOS)
        .frame(minWidth: 400, minHeight: 350)
        #endif
        .onDisappear {
            engine.shutdown()
        }
    }
}
