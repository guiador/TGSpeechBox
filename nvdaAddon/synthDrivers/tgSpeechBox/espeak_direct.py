# -*- coding: utf-8 -*-
"""Direct eSpeak DLL access — bypasses NVDA's _espeak module entirely.

NVDA's synthDrivers._espeak module wraps espeak.dll but also creates a
WavePlayer, a background thread, and registers a synth callback — all
infrastructure for eSpeak-as-a-speech-synth.  TGSpeechBox only needs
espeak_TextToPhonemes (synchronous, no audio), so this module loads the
DLL directly with zero overhead.

This eliminates the synth-switch bug where _espeak.initialize() could
fail (WavePlayer creation, thread contention) and silently leave the
driver unable to convert text to IPA.
"""

from __future__ import annotations

import ctypes
import os

from logHandler import log

# ---------------------------------------------------------------------------
# Constants (copied from _espeak.py — pure values, no side effects)
# ---------------------------------------------------------------------------
AUDIO_OUTPUT_SYNCHRONOUS = 0x0002
espeakINITIALIZE_DONT_EXIT = 0x8000
espeakCHARS_WCHAR = 3

# espeak_SetParameter IDs
_espeakCAPITALS = 6
_espeakPUNCTUATION = 5
_espeakWORDGAP = 7


# ---------------------------------------------------------------------------
# espeak_VOICE struct (needed for ListVoices / SetVoiceByProperties)
# ---------------------------------------------------------------------------
class espeak_VOICE(ctypes.Structure):
    _fields_ = [
        ("name", ctypes.c_char_p),
        ("languages", ctypes.c_char_p),
        ("identifier", ctypes.c_char_p),
        ("gender", ctypes.c_byte),
        ("age", ctypes.c_byte),
        ("variant", ctypes.c_byte),
        ("xx1", ctypes.c_byte),
        ("score", ctypes.c_int),
        ("spare", ctypes.c_void_p),
    ]


# ---------------------------------------------------------------------------
# Module state
# ---------------------------------------------------------------------------
_dll = None           # ctypes CDLL handle
_dllHandle = 0        # raw Windows HMODULE for FreeLibrary
_initialized = False  # True after espeak_Initialize succeeds


def get_dll():
    """Return the loaded eSpeak DLL handle, or None."""
    return _dll


def is_ready() -> bool:
    """Return True if eSpeak is initialized and ready for phoneme conversion."""
    return _initialized and _dll is not None


# ---------------------------------------------------------------------------
# Lifecycle
# ---------------------------------------------------------------------------
def _drainDll(dllPath: str) -> None:
    """Force-unload espeak.dll if it's still resident in the process.

    Windows DLLs are reference-counted.  ctypes.cdll.LoadLibrary does NOT
    call FreeLibrary on garbage collection, so after NVDA's _espeak
    module terminates, the DLL stays mapped with all its C-level global
    state intact — including eSpeak's translator mode (which can be stuck
    in letter-spelling mode after an interrupted SSML session).

    We call GetModuleHandle to see if the DLL is still loaded, then
    FreeLibrary in a loop to drain the refcount and fully unmap it.
    The subsequent LoadLibrary in init() will then start with a clean
    module.
    """
    try:
        kernel32 = ctypes.windll.kernel32
        kernel32.GetModuleHandleW.restype = ctypes.c_void_p
        kernel32.GetModuleHandleW.argtypes = (ctypes.c_wchar_p,)
        # GetModuleHandleW accepts either a full path or just the filename.
        # Try the full path first, fall back to the bare filename.
        h = kernel32.GetModuleHandleW(dllPath)
        if not h:
            h = kernel32.GetModuleHandleW(os.path.basename(dllPath))
        freed = 0
        while h:
            kernel32.FreeLibrary(ctypes.c_void_p(h))
            freed += 1
            if freed > 16:
                break  # safety valve
            h = kernel32.GetModuleHandleW(dllPath)
            if not h:
                h = kernel32.GetModuleHandleW(os.path.basename(dllPath))
        if freed:
            log.debug("TGSpeechBox: drained %d stale espeak.dll handles", freed)
    except Exception:
        log.debug("TGSpeechBox: _drainDll failed", exc_info=True)


def init() -> bool:
    """Load espeak.dll and call espeak_Initialize.

    Returns True on success.  Safe to call multiple times — will
    re-initialize if previously terminated.
    """
    global _dll, _dllHandle, _initialized

    if _initialized and _dll is not None:
        return True

    try:
        import globalVars
        synthDir = os.path.join(globalVars.appDir, "synthDrivers")
        dllPath = os.path.join(synthDir, "espeak.dll")

        if not os.path.isfile(dllPath):
            log.error("TGSpeechBox: espeak.dll not found at %s", dllPath)
            return False

        # Force-release any leftover DLL handles that NVDA's _espeak (or
        # a previous session) may have left resident.  Windows refcounts
        # DLL loads — if the refcount is > 0 the DLL stays mapped with
        # stale C globals (translator stuck in letter-spelling mode, etc.).
        # We drain the refcount so the next LoadLibrary gets a truly
        # fresh module.
        _drainDll(dllPath)

        # Use ctypes.CDLL directly — ctypes.cdll.LoadLibrary caches
        # CDLL objects by path and would return a stale handle after
        # we drained the DLL above.
        _dll = ctypes.CDLL(dllPath)
        _dllHandle = _dll._handle

        # Set up prototypes for functions we use.
        _dll.espeak_Initialize.argtypes = (
            ctypes.c_int,   # output
            ctypes.c_int,   # buflength
            ctypes.c_char_p,  # path
            ctypes.c_int,   # options
        )
        _dll.espeak_Initialize.restype = ctypes.c_int

        _dll.espeak_Terminate.argtypes = ()
        _dll.espeak_Terminate.restype = ctypes.c_int

        _dll.espeak_TextToPhonemes.argtypes = (
            ctypes.POINTER(ctypes.c_void_p),  # textptr (pointer-to-pointer)
            ctypes.c_int,   # textmode (espeakCHARS_*)
            ctypes.c_int,   # phonememode
        )
        _dll.espeak_TextToPhonemes.restype = ctypes.c_void_p

        _dll.espeak_ListVoices.argtypes = (ctypes.c_void_p,)
        _dll.espeak_ListVoices.restype = ctypes.POINTER(
            ctypes.POINTER(espeak_VOICE)
        )

        _dll.espeak_SetVoiceByName.argtypes = (ctypes.c_char_p,)
        _dll.espeak_SetVoiceByName.restype = ctypes.c_int

        _dll.espeak_SetVoiceByProperties.argtypes = (
            ctypes.POINTER(espeak_VOICE),
        )
        _dll.espeak_SetVoiceByProperties.restype = ctypes.c_int

        _dll.espeak_Info.argtypes = (ctypes.c_char_p,)
        _dll.espeak_Info.restype = ctypes.c_char_p

        _dll.espeak_SetParameter.argtypes = (
            ctypes.c_int,   # parameter
            ctypes.c_int,   # value
            ctypes.c_int,   # relative (0 = absolute)
        )
        _dll.espeak_SetParameter.restype = ctypes.c_int

        _dll.espeak_SetSynthCallback.argtypes = (ctypes.c_void_p,)
        _dll.espeak_SetSynthCallback.restype = None

        # espeak_ng_Cancel resets internal translator/SSML state.
        # NVDA's own eSpeak driver calls this before each utterance to
        # recover from interrupted SSML that left parameters dirty.
        _dll.espeak_ng_Cancel.argtypes = ()
        _dll.espeak_ng_Cancel.restype = ctypes.c_int

        # Initialize the C library.
        # AUDIO_OUTPUT_SYNCHRONOUS: no audio output, just phoneme processing.
        # espeakINITIALIZE_DONT_EXIT: don't call exit() on errors.
        sampleRate = _dll.espeak_Initialize(
            AUDIO_OUTPUT_SYNCHRONOUS,
            0,  # buflength — unused in synchronous mode
            os.fsencode(synthDir),
            espeakINITIALIZE_DONT_EXIT,
        )
        if sampleRate <= 0:
            log.error(
                "TGSpeechBox: espeak_Initialize failed (code %d), path=%s",
                sampleRate, synthDir,
            )
            _dll = None
            return False

        # Reset eSpeak internal state that may persist from a previous
        # synth session (e.g. eSpeak NG).  The same DLL stays loaded in
        # the process, so espeak_Terminate/espeak_Initialize alone do NOT
        # clear all C-level state.  In particular, if eSpeak NG was
        # interrupted mid-SSML (e.g. <say-as interpret-as="characters">),
        # the translator can be stuck in letter/spelling mode, causing
        # espeak_TextToPhonemes to return letter-name IPA for every word.
        _resetState()

        _initialized = True
        log.debug(
            "TGSpeechBox: eSpeak initialized directly (sampleRate=%d)",
            sampleRate,
        )
        return True

    except Exception:
        log.error("TGSpeechBox: failed to initialize eSpeak directly", exc_info=True)
        _dll = None
        _initialized = False
        return False


def terminate():
    """Call espeak_Terminate and release the DLL handle."""
    global _dll, _initialized, _dllHandle

    if _dll is not None and _initialized:
        try:
            _dll.espeak_Terminate()
        except Exception:
            log.debug("TGSpeechBox: espeak_Terminate failed", exc_info=True)

    # Force-unload the DLL so the next init() gets a truly clean instance.
    # ctypes.cdll.LoadLibrary does NOT call FreeLibrary when the CDLL
    # object is garbage-collected, so the DLL stays resident with stale
    # C-level global state (translator mode, SSML parse state, etc.).
    # Calling FreeLibrary here decrements the Windows refcount.  If
    # NVDA's _espeak also released its handle, the DLL is fully unloaded
    # and the next LoadLibrary will map it fresh.
    if _dllHandle:
        try:
            ctypes.windll.kernel32.FreeLibrary(ctypes.c_void_p(_dllHandle))
        except Exception:
            log.debug("TGSpeechBox: FreeLibrary failed", exc_info=True)
        _dllHandle = 0

    _dll = None
    _initialized = False


def _resetState():
    """Reset eSpeak internal state after (re-)initialization.

    When switching synths (eSpeak NG → TGSpeechBox), the DLL stays loaded
    in the process.  espeak_Terminate + espeak_Initialize do NOT fully
    clear the C-level translator state.  Three things fix it:

    1. espeak_ng_Cancel — flushes pending SSML parse state.  If eSpeak NG
       was interrupted mid-``<say-as interpret-as="characters">``, the
       translator stays in letter/spelling mode.  Cancel resets it.

    2. espeak_SetSynthCallback(NULL) — clears the synth callback that
       NVDA's _espeak module registered.  We use synchronous mode and
       never call espeak_Synth, so no callback is needed.

    3. espeak_SetParameter — explicitly zero out CAPITALS, PUNCTUATION,
       and WORDGAP so parameters set by eSpeak NG don't bleed through.
    """
    if not _dll:
        return
    try:
        _dll.espeak_ng_Cancel()
    except Exception:
        log.debug("TGSpeechBox: espeak_ng_Cancel failed", exc_info=True)
    try:
        _dll.espeak_SetSynthCallback(None)
    except Exception:
        log.debug("TGSpeechBox: espeak_SetSynthCallback(NULL) failed", exc_info=True)
    try:
        _dll.espeak_SetParameter(_espeakCAPITALS, 0, 0)
        _dll.espeak_SetParameter(_espeakPUNCTUATION, 0, 0)
        _dll.espeak_SetParameter(_espeakWORDGAP, 0, 0)
    except Exception:
        log.debug("TGSpeechBox: espeak_SetParameter reset failed", exc_info=True)


# ---------------------------------------------------------------------------
# Voice selection (synchronous — no background thread, no _execWhenDone)
# ---------------------------------------------------------------------------
def getVoiceList():
    """Return a list of espeak_VOICE structs from espeak_ListVoices."""
    if not _dll:
        return []
    try:
        voices = _dll.espeak_ListVoices(None)
        result = []
        for voice in voices:
            if not voice:
                break
            result.append(voice.contents)
        return result
    except Exception:
        log.debug("TGSpeechBox: espeak_ListVoices failed", exc_info=True)
        return []


def setVoiceByName(name: str) -> bool:
    """Set eSpeak voice by name.  Returns True on success."""
    if not _dll:
        return False
    try:
        encoded = name.encode("utf-8") if isinstance(name, str) else name
        rc = _dll.espeak_SetVoiceByName(encoded)
        return rc == 0  # EE_OK
    except Exception:
        log.debug("TGSpeechBox: espeak_SetVoiceByName(%r) failed", name, exc_info=True)
        return False


def setVoiceByLanguage(lang: str) -> bool:
    """Set eSpeak voice by language tag.  Returns True on success."""
    if not _dll:
        return False
    try:
        v = espeak_VOICE()
        tag = lang.replace("_", "-")
        v.languages = tag.encode("utf-8")
        rc = _dll.espeak_SetVoiceByProperties(ctypes.byref(v))
        return rc == 0  # EE_OK
    except Exception:
        log.debug(
            "TGSpeechBox: espeak_SetVoiceByProperties(%r) failed",
            lang, exc_info=True,
        )
        return False


def info() -> str:
    """Return eSpeak version string."""
    if not _dll:
        return ""
    try:
        raw = _dll.espeak_Info(None)
        return raw.decode("utf-8") if raw else ""
    except Exception:
        return ""
