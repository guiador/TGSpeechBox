/*
 * TGSpeechBoxApp.swift — App entry point.
 */

import SwiftUI
import AudioToolbox

@main
struct TGSpeechBoxApp: App {
    init() {
        // Nudge the system into re-discovering our AU speech extension
        // after TestFlight / App Store updates.
        var desc = AudioComponentDescription(
            componentType: 0x61757370, // 'ausp'
            componentSubType: 0x74677362, // 'tgsb'
            componentManufacturer: 0x54475370, // 'TGSp'
            componentFlags: 0,
            componentFlagsMask: 0
        )
        if AudioComponentFindNext(nil, &desc) != nil {
            print("[TGSpeechBox] AU speech extension registered")
        }
    }

    var body: some Scene {
        WindowGroup {
            ContentView()
        }
    }
}
