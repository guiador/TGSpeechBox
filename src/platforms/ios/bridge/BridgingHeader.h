/*
 * BridgingHeader.h — Exposes TGSpeechBox C APIs to Swift.
 *
 * tgsb_bridge.h — Full pipeline (text → IPA → PCM), used by standalone app.
 * tgsb_synth.h  — Synthesis only (IPA → PCM), used by AU extension with XPC.
 */

#include "tgsb_bridge.h"
#include "tgsb_synth.h"
