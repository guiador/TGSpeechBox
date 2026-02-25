/*
 * PhonemizerService.swift — XPC service that wraps eSpeak phonemization.
 *
 * This runs in its own process. If eSpeak crashes, VoiceOver is unaffected.
 * The AU extension connects via NSXPCConnection and calls phonemize().
 *
 * License: GPL-3.0 (links eSpeak-NG)
 */

import Foundation

class PhonemizerService: NSObject, TGSBPhonemizerProtocol {

    private var initialized = false

    override init() {
        super.init()

        guard let bundle = Bundle(for: PhonemizerService.self).resourcePath else {
            return
        }
        let espeakDataPath = bundle + "/espeak-ng-data"

        guard FileManager.default.fileExists(atPath: espeakDataPath) else {
            return
        }

        initialized = tgsb_phonemizer_init(espeakDataPath) != 0
    }

    deinit {
        if initialized {
            tgsb_phonemizer_terminate()
        }
    }

    func phonemize(_ text: String,
                   language: String,
                   withReply reply: @escaping (String) -> Void) {
        guard initialized else {
            reply("")
            return
        }

        if tgsb_phonemizer_set_language(language) == 0 {
            // Fallback to en-us if language not available
            _ = tgsb_phonemizer_set_language("en-us")
        }

        guard let cResult = tgsb_phonemizer_phonemize(text) else {
            reply("")
            return
        }

        let ipa = String(cString: cResult)
        free(cResult)
        reply(ipa)
    }
}
