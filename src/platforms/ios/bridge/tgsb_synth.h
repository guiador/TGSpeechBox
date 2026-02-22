/*
 * tgsb_synth.h — Synthesis-only C API (no eSpeak, no GPL).
 *
 * Takes IPA strings and produces PCM audio.
 * eSpeak phonemization happens in the XPC service.
 *
 * License: MIT
 */

#ifndef TGSB_SYNTH_H
#define TGSB_SYNTH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TgsbSynth TgsbSynth;

/* --- Lifecycle --- */
TgsbSynth *tgsb_synth_create(const char *packDir, int sampleRate);
void tgsb_synth_destroy(TgsbSynth *synth);

/* --- Configuration --- */
int tgsb_synth_set_language(TgsbSynth *synth, const char *tgsbLang);
int tgsb_synth_set_voice(TgsbSynth *synth, const char *voiceName);

/* --- Synthesis from IPA --- */

/**
 * Queue IPA clauses for synthesis.
 * `ipa` may contain multiple clauses separated by newlines.
 */
void tgsb_synth_queue_ipa(TgsbSynth *synth,
                           const char *ipa,
                           double speed,
                           double pitch);

/**
 * Pull synthesized PCM (s16le, mono) into outBuffer.
 * Returns number of samples written (0 = done).
 */
int tgsb_synth_pull_audio(TgsbSynth *synth,
                           int16_t *outBuffer,
                           int maxSamples);

void tgsb_synth_stop(TgsbSynth *synth);

/* --- Voice preset info --- */
int tgsb_synth_get_num_voices(void);
const char *tgsb_synth_get_voice_name(int index);

#ifdef __cplusplus
}
#endif

#endif /* TGSB_SYNTH_H */
