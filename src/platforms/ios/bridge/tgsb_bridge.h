/*
 * tgsb_bridge.h — Plain C API for TGSpeechBox pipeline.
 *
 * Wraps eSpeak + nvspFrontend + speechPlayer into a simple interface
 * callable from Swift via a bridging header.
 *
 * License: GPL-3.0 (links eSpeak-ng GPL with TGSpeechBox MIT)
 */

#ifndef TGSB_BRIDGE_H
#define TGSB_BRIDGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TgsbEngine TgsbEngine;

/* --- Lifecycle --- */
TgsbEngine *tgsb_create(const char *espeakDataPath,
                         const char *packDir,
                         int sampleRate);
void tgsb_destroy(TgsbEngine *engine);

/* --- Configuration --- */
int tgsb_set_language(TgsbEngine *engine,
                      const char *espeakLang,
                      const char *tgsbLang);
int tgsb_set_voice(TgsbEngine *engine, const char *voiceName);

/* --- Synthesis --- */
void tgsb_queue_text(TgsbEngine *engine,
                     const char *text,
                     double speed,
                     double pitch);

/*
 * Pull synthesized PCM (s16le, mono) into outBuffer.
 * Returns number of samples written (0 = done).
 * Call in a loop until it returns 0.
 */
int tgsb_pull_audio(TgsbEngine *engine,
                    int16_t *outBuffer,
                    int maxSamples);

void tgsb_stop(TgsbEngine *engine);

/* --- Voice quality --- */
void tgsb_set_voicing_tone(TgsbEngine *engine,
    double voicedTiltDbPerOct,
    double noiseGlottalModDepth,
    double pitchSyncF1DeltaHz,
    double pitchSyncB1DeltaHz,
    double speedQuotient,
    double aspirationTiltDbPerOct,
    double cascadeBwScale,
    double tremorDepth);

void tgsb_set_frame_ex_defaults(TgsbEngine *engine,
    double creakiness,
    double breathiness,
    double jitter,
    double shimmer,
    double sharpness);

/* --- Pitch mode --- */
int tgsb_set_pitch_mode(TgsbEngine *engine, const char *mode);
void tgsb_set_legacy_pitch_inflection_scale(TgsbEngine *engine, double scale);

/* --- Inflection (pitch range scaling, 0..1) --- */
void tgsb_set_inflection(TgsbEngine *engine, double inflection);

/* --- Pause mode (0=off, 1=short, 2=long) --- */
void tgsb_set_pause_mode(TgsbEngine *engine, int mode);

/* --- Sample rate (reinitializes DSP only, keeps eSpeak/frontend) --- */
void tgsb_set_sample_rate(TgsbEngine *engine, int sampleRate);

/* --- Voice preset info --- */
int tgsb_get_num_voices(void);
const char *tgsb_get_voice_name(int index);

#ifdef __cplusplus
}
#endif

#endif /* TGSB_BRIDGE_H */
