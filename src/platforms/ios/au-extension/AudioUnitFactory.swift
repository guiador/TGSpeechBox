/*
 * AudioUnitFactory.swift — AUAudioUnitFactory for the speech extension.
 *
 * The system instantiates this factory via NSExtensionPrincipalClass,
 * then calls createAudioUnit(with:) to get our AVSpeechSynthesisProviderAudioUnit.
 *
 * License: GPL-3.0
 */

import AVFoundation

public class AudioUnitFactory: NSObject, AUAudioUnitFactory {

    public func beginRequest(with context: NSExtensionContext) {}

    @objc public func createAudioUnit(
        with componentDescription: AudioComponentDescription
    ) throws -> AUAudioUnit {
        return try TGSBAudioUnit(
            componentDescription: componentDescription,
            options: []
        )
    }
}
