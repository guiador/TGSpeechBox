/*
 * TGSBPhonemizerProtocol.swift — XPC interface for the phonemizer service.
 *
 * The AU extension sends text + language, gets IPA back.
 * eSpeak (GPL) lives entirely in the XPC service process.
 *
 * License: MIT (protocol definition only; the service implementation
 * that links eSpeak is GPL)
 */

import Foundation

@objc public protocol TGSBPhonemizerProtocol {

    /// Phonemize text to IPA using eSpeak.
    /// - Parameters:
    ///   - text: Plain text to phonemize.
    ///   - language: eSpeak language tag (e.g. "en-us", "fr", "de").
    ///   - reply: Callback with the IPA string (empty on failure).
    func phonemize(_ text: String,
                   language: String,
                   withReply reply: @escaping (String) -> Void)
}
