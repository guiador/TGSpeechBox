# -*- coding: utf-8 -*-
"""eSpeak bridge — direct voice selection and IPA conversion.

Standalone functions that wrap NVDA's _espeak module for:
- Direct voice selection via ListVoices + SetVoiceByName (bypassing NVDA's async wrapper)
- Text-to-IPA conversion via espeak_TextToPhonemes
- Script-aware IPA conversion (switching eSpeak language for foreign-script runs)
"""

from __future__ import annotations

import ctypes

from logHandler import log
from synthDrivers import _espeak

from .text_utils import splitByScript

# ctypes c_char_p truncates at the first NUL byte, so our parser only sees
# the PRIMARY language tag in eSpeak's packed language list.  Secondary tags
# (e.g. "es-mx" inside the es-419 voice) are invisible.
# Map our pack tags to eSpeak's canonical primary tags here.
_ESPEAK_PRIMARY_TAG = {
    "es-mx": "es-419",
}


def espeakSetVoiceDirect(langTag: str) -> bool:
    """Set eSpeak voice using _espeak public API with accurate language matching.

    Returns True if the voice was set successfully.
    """
    try:
        voiceList = _espeak.getVoiceList()
    except Exception:
        return False
    if not voiceList:
        return False

    tag = langTag.lower().replace("_", "-")
    # ctypes c_char_p truncates at the first NUL, so we only see each voice's
    # primary language tag.  Map our pack tags to eSpeak's primary tags.
    tag = _ESPEAK_PRIMARY_TAG.get(tag, tag)
    base = tag.split("-")[0] if "-" in tag else ""

    exactId = None
    exactPri = 99
    baseId = None
    basePri = 99

    for voice in voiceList:
        try:
            rawLangs = voice.languages
            vid = voice.identifier
        except AttributeError:
            continue
        if not rawLangs or not vid:
            continue

        # Parse packed languages: [priority_byte][nul-terminated-tag] repeated.
        data = rawLangs
        i = 0
        while i < len(data):
            pri = data[i]
            ltag = data[i + 1:]
            nul = ltag.find(b"\x00")
            if nul >= 0:
                ltag = ltag[:nul]
            ltagStr = ltag.decode("utf-8", errors="ignore").lower()

            if ltagStr == tag:
                if pri < exactPri:
                    exactPri = pri
                    exactId = vid
            elif base and ltagStr == base:
                if pri < basePri:
                    basePri = pri
                    baseId = vid

            # Advance past priority byte + tag + NUL
            i += 1 + len(ltag) + 1
            if nul < 0:
                break

    chosenId = exactId or baseId
    if not chosenId:
        return False

    # Decode bytes identifier to str for the public API call.
    try:
        name = chosenId.decode("utf-8", errors="replace") if isinstance(chosenId, bytes) else chosenId
    except Exception:
        name = chosenId

    try:
        _espeak.setVoiceByName(name)
        log.debug("TGSpeechBox: eSpeak voice set directly: %r -> %r", tag, name)
        return True
    except Exception:
        log.debug("TGSpeechBox: setVoiceByName failed", exc_info=True)

    return False


def espeakTextToIPA(text: str, espeakDLL, phonemeMode: int) -> str:
    """Convert *text* to IPA using espeak_TextToPhonemes.

    Parameters
    ----------
    text : str
        The text to convert.
    espeakDLL :
        The ctypes-loaded eSpeak DLL (``_espeak.espeakDLL``).
    phonemeMode : int
        Phoneme mode flags for ``espeak_TextToPhonemes``.

    Returns an empty string on failure.
    """
    if not text:
        return ""
    if not espeakDLL:
        return ""

    textToPhonemes = getattr(espeakDLL, "espeak_TextToPhonemes", None)
    if not textToPhonemes:
        return ""

    textBuf = ctypes.create_unicode_buffer(text)
    textPtr = ctypes.c_void_p(ctypes.addressof(textBuf))
    chunks = []
    lastPtr = None
    while textPtr and textPtr.value:
        if lastPtr == textPtr.value:
            break
        lastPtr = textPtr.value
        try:
            phonemeBuf = textToPhonemes(
                ctypes.byref(textPtr),
                _espeak.espeakCHARS_WCHAR,
                phonemeMode,
            )
        except OSError:
            # Let OSError propagate so callers can disable eSpeak.
            raise
        if phonemeBuf:
            chunks.append(ctypes.string_at(phonemeBuf))
        else:
            break
    ipaBytes = b"".join(chunks)
    try:
        return ipaBytes.decode("utf8", errors="ignore").strip()
    except Exception:
        return ""


def espeakTextToIPA_scriptAware(
    text: str,
    espeakDLL,
    phonemeMode: int,
    espeakLang: str,
    latinFallback: str,
) -> str:
    """Convert text to IPA, switching eSpeak language for foreign-script runs.

    When the active language uses a non-Latin script (Russian, Bulgarian,
    Greek, etc.) and the text contains Latin-script words, eSpeak would
    produce garbage IPA for those words.  This function detects script
    boundaries, temporarily switches eSpeak to the Latin fallback language,
    and reassembles the IPA.
    """
    if not text:
        return ""

    segments = splitByScript(text, espeakLang, latinFallback)

    # Fast path: no script switching needed (single segment, base lang).
    if len(segments) == 1 and segments[0][1] is None:
        return espeakTextToIPA(text, espeakDLL, phonemeMode)

    ipaChunks = []
    currentLang = espeakLang

    for segText, langOverride in segments:
        if not segText or not segText.strip():
            continue

        # Switch eSpeak language if needed.
        targetLang = langOverride or espeakLang
        if targetLang != currentLang:
            if espeakSetVoiceDirect(targetLang):
                currentLang = targetLang
            else:
                try:
                    _espeak.setVoiceByLanguage(targetLang)
                    currentLang = targetLang
                except Exception:
                    pass  # Fall through with current language.

        ipa = espeakTextToIPA(segText, espeakDLL, phonemeMode)
        if ipa:
            ipaChunks.append(ipa)

    # Restore base language if we switched away.
    if currentLang != espeakLang:
        if not espeakSetVoiceDirect(espeakLang):
            try:
                _espeak.setVoiceByLanguage(espeakLang)
            except Exception:
                pass

    return " ".join(ipaChunks)
