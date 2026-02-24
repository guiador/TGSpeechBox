/*
 * tgsb_bridge.cpp — C bridge implementation for macOS/iOS.
 *
 * Translates tgsb_jni.cpp (Android JNI bridge) to plain C functions.
 * Same pipeline: text -> eSpeak IPA -> nvspFrontend -> speechPlayer -> PCM
 *
 * License: GPL-3.0
 */

#include "tgsb_bridge.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <espeak-ng/speak_lib.h>
#include "speechPlayer.h"
#include "nvspFrontend.h"

/* ------------------------------------------------------------------ */
/* Voice presets (from tgsb_jni.cpp)                                   */
/* ------------------------------------------------------------------ */

typedef struct {
    size_t offset;
    double value;
    int isMultiplier; /* 1 = multiply, 0 = override */
} FrameOverride;

typedef struct {
    const char *name;
    const FrameOverride *overrides;
    int numOverrides;
    double voicedTiltDbPerOct;
    int hasVoicedTilt;
} VoicePreset;

#define OFF(field) offsetof(speechPlayer_frame_t, field)

static const FrameOverride kAdamOverrides[] = {
    { OFF(cb1), 1.3, 1 },
    { OFF(pa6), 1.3, 1 },
    { OFF(fricationAmplitude), 0.85, 1 },
};

static const FrameOverride kBenjaminOverrides[] = {
    { OFF(cf1), 1.01, 1 },
    { OFF(cf2), 1.02, 1 },
    { OFF(cf4), 3770.0, 0 },
    { OFF(cf5), 4100.0, 0 },
    { OFF(cf6), 5000.0, 0 },
    { OFF(cfNP), 0.9, 1 },
    { OFF(cb1), 1.3, 1 },
    { OFF(fricationAmplitude), 0.7, 1 },
    { OFF(pa6), 1.3, 1 },
};

static const FrameOverride kCalebOverrides[] = {
    { OFF(aspirationAmplitude), 1.0, 0 },
    { OFF(voiceAmplitude), 0.0, 0 },
};

static const FrameOverride kDavidOverrides[] = {
    { OFF(voicePitch), 0.75, 1 },
    { OFF(endVoicePitch), 0.75, 1 },
    { OFF(cf1), 0.75, 1 },
    { OFF(cf2), 0.85, 1 },
    { OFF(cf3), 0.85, 1 },
};

static const FrameOverride kRobertOverrides[] = {
    { OFF(voicePitch), 1.10, 1 },
    { OFF(endVoicePitch), 1.10, 1 },
    { OFF(cf1), 1.02, 1 }, { OFF(cf2), 1.06, 1 }, { OFF(cf3), 1.08, 1 },
    { OFF(cf4), 1.08, 1 }, { OFF(cf5), 1.10, 1 }, { OFF(cf6), 1.05, 1 },
    { OFF(cb1), 0.65, 1 }, { OFF(cb2), 0.68, 1 }, { OFF(cb3), 0.72, 1 },
    { OFF(cb4), 0.75, 1 }, { OFF(cb5), 0.78, 1 }, { OFF(cb6), 0.80, 1 },
    { OFF(glottalOpenQuotient), 0.30, 0 },
    { OFF(voiceTurbulenceAmplitude), 0.20, 1 },
    { OFF(fricationAmplitude), 0.75, 1 },
    { OFF(parallelBypass), 0.70, 1 },
    { OFF(pa3), 1.08, 1 }, { OFF(pa4), 1.15, 1 },
    { OFF(pa5), 1.20, 1 }, { OFF(pa6), 1.25, 1 },
    { OFF(pb1), 0.72, 1 }, { OFF(pb2), 0.75, 1 }, { OFF(pb3), 0.78, 1 },
    { OFF(pb4), 0.80, 1 }, { OFF(pb5), 0.82, 1 }, { OFF(pb6), 0.85, 1 },
    { OFF(pf3), 1.06, 1 }, { OFF(pf4), 1.08, 1 },
    { OFF(pf5), 1.10, 1 }, { OFF(pf6), 1.00, 1 },
};

#define PRESET(name, arr, tilt, hasTilt) \
    { name, arr, sizeof(arr)/sizeof(arr[0]), tilt, hasTilt }

static const VoicePreset kPresets[] = {
    PRESET("adam",     kAdamOverrides,     0.0,  0),
    PRESET("benjamin", kBenjaminOverrides, 0.0,  0),
    PRESET("caleb",    kCalebOverrides,    0.0,  0),
    PRESET("david",    kDavidOverrides,    0.0,  0),
    PRESET("robert",   kRobertOverrides,  -6.0,  1),
};
static const int kNumPresets = sizeof(kPresets) / sizeof(kPresets[0]);

static void applyOverrides(speechPlayer_frame_t *f,
                           const FrameOverride *ov, int count) {
    for (int i = 0; i < count; i++) {
        double *field = (double *)((char *)f + ov[i].offset);
        if (ov[i].isMultiplier)
            *field *= ov[i].value;
        else
            *field = ov[i].value;
    }
}

/* ------------------------------------------------------------------ */
/* Engine struct                                                       */
/* ------------------------------------------------------------------ */

struct TgsbEngine {
    speechPlayer_handle_t player;
    nvspFrontend_handle_t frontend;
    int sampleRate;
    volatile int stopRequested;
    int voiceIndex;

    /* User VoicingTone overrides (applied on top of voice preset defaults) */
    int hasUserTone;
    double userVoicedTiltDbPerOct;
    double userNoiseGlottalModDepth;
    double userPitchSyncF1DeltaHz;
    double userPitchSyncB1DeltaHz;
    double userSpeedQuotient;
    double userAspirationTiltDbPerOct;
    double userCascadeBwScale;
    double userTremorDepth;
};

/* Frame callback context */
typedef struct {
    TgsbEngine *engine;
    int frameCount;
} FrameCtx;

static void onFrame(
    void *userData,
    const nvspFrontend_Frame *frameOrNull,
    const nvspFrontend_FrameEx *frameExOrNull,
    double durationMs,
    double fadeMs,
    int userIndex
) {
    FrameCtx *ctx = (FrameCtx *)userData;
    if (!ctx || !ctx->engine || !ctx->engine->player) return;

    int sr = ctx->engine->sampleRate;
    unsigned int minSamples = durationMs > 0.0
        ? (unsigned int)(durationMs * sr / 1000.0 + 0.5) : 0;
    unsigned int fadeSamples = fadeMs > 0.0
        ? (unsigned int)(fadeMs * sr / 1000.0 + 0.5) : 0;

    if (frameOrNull) {
        speechPlayer_frame_t f;
        memcpy(&f, frameOrNull, sizeof(f));

        /* Apply voice preset overrides */
        const VoicePreset *vp = &kPresets[ctx->engine->voiceIndex];
        applyOverrides(&f, vp->overrides, vp->numOverrides);

        if (frameExOrNull) {
            speechPlayer_queueFrameEx(ctx->engine->player, &f,
                (const speechPlayer_frameEx_t *)frameExOrNull,
                (unsigned int)sizeof(nvspFrontend_FrameEx),
                minSamples, fadeSamples, userIndex, 0);
        } else {
            speechPlayer_queueFrame(ctx->engine->player, &f,
                minSamples, fadeSamples, userIndex, 0);
        }
    } else {
        speechPlayer_queueFrame(ctx->engine->player, NULL,
            minSamples, fadeSamples, userIndex, 0);
    }
    ctx->frameCount++;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

extern "C" {

TgsbEngine *tgsb_create(const char *espeakDataPath,
                         const char *packDir,
                         int sampleRate)
{
    /* Initialize eSpeak.
     * Pass espeakINITIALIZE_DONT_EXIT (0x8000) so eSpeak returns an error
     * instead of calling exit(1) if data files are not found. */
    int espeakSr = espeak_Initialize(AUDIO_OUTPUT_SYNCHRONOUS, 0,
                                      espeakDataPath, 0x8000);
    if (espeakSr <= 0) return NULL;

    /* Verify eSpeak actually loaded data — with DONT_EXIT, espeak_Initialize
     * can return a positive sample rate even if data loading failed.
     * SetVoiceByProperties will fail if internal state is NULL. */
    {
        espeak_VOICE voice_spec;
        memset(&voice_spec, 0, sizeof(voice_spec));
        voice_spec.languages = "en-us";
        if (espeak_SetVoiceByProperties(&voice_spec) != EE_OK) {
            espeak_Terminate();
            return NULL;
        }
    }

    /* Create TGSpeechBox components */
    speechPlayer_handle_t player = speechPlayer_initialize(sampleRate);
    nvspFrontend_handle_t fe = nvspFrontend_create(packDir);
    if (!fe) {
        speechPlayer_terminate(player);
        espeak_Terminate();
        return NULL;
    }
    nvspFrontend_setLanguage(fe, "en-us");

    /* Set default voicing tone */
    speechPlayer_voicingTone_t tone = speechPlayer_getDefaultVoicingTone();
    speechPlayer_setVoicingTone(player, &tone);

    TgsbEngine *engine = (TgsbEngine *)calloc(1, sizeof(TgsbEngine));
    engine->player = player;
    engine->frontend = fe;
    engine->sampleRate = sampleRate;
    engine->stopRequested = 0;
    engine->voiceIndex = 0; /* Adam */

    return engine;
}

void tgsb_destroy(TgsbEngine *engine)
{
    if (!engine) return;
    if (engine->frontend) nvspFrontend_destroy(engine->frontend);
    if (engine->player) speechPlayer_terminate(engine->player);
    espeak_Terminate();
    free(engine);
}

int tgsb_set_language(TgsbEngine *engine,
                      const char *espeakLang,
                      const char *tgsbLang)
{
    if (!engine) return 0;

    espeak_VOICE voice_spec;
    memset(&voice_spec, 0, sizeof(voice_spec));
    voice_spec.languages = espeakLang;
    espeak_SetVoiceByProperties(&voice_spec);

    return nvspFrontend_setLanguage(engine->frontend, tgsbLang);
}

int tgsb_set_voice(TgsbEngine *engine, const char *voiceName)
{
    if (!engine) return 0;

    for (int i = 0; i < kNumPresets; i++) {
        if (strcmp(kPresets[i].name, voiceName) == 0) {
            engine->voiceIndex = i;

            speechPlayer_voicingTone_t tone =
                speechPlayer_getDefaultVoicingTone();
            if (kPresets[i].hasVoicedTilt)
                tone.voicedTiltDbPerOct = kPresets[i].voicedTiltDbPerOct;
            speechPlayer_setVoicingTone(engine->player, &tone);
            return 1;
        }
    }
    return 0;
}

void tgsb_queue_text(TgsbEngine *engine,
                     const char *text,
                     double speed,
                     double pitch)
{
    if (!engine || !engine->player || !engine->frontend) return;
    if (!text || !*text) return;

    engine->stopRequested = 0;

    /* Purge stale frames from previous utterance */
    speechPlayer_queueFrame(engine->player, NULL, 0, 0, -1, true);

    /* Clamp parameters */
    if (speed < 0.1) speed = 0.1;
    if (speed > 5.0) speed = 5.0;
    if (pitch < 40.0) pitch = 40.0;
    if (pitch > 500.0) pitch = 500.0;

    /* Text -> IPA via eSpeak, clause by clause */
    FrameCtx ctx;
    ctx.engine = engine;
    ctx.frameCount = 0;

    const void *textPtr = text;
    while (textPtr && *(const char *)textPtr && !engine->stopRequested) {
        const char *ipa = espeak_TextToPhonemes(
            &textPtr, espeakCHARS_UTF8, 0x02 /* IPA */);
        if (!ipa || !*ipa) continue;

        nvspFrontend_queueIPA_Ex(
            engine->frontend, ipa,
            speed, pitch, 0.5, ".", 0,
            onFrame, &ctx
        );
    }
}

int tgsb_pull_audio(TgsbEngine *engine,
                    int16_t *outBuffer,
                    int maxSamples)
{
    if (!engine || !engine->player || engine->stopRequested) return 0;
    if (maxSamples <= 0) return 0;
    if (maxSamples > 4096) maxSamples = 4096;

    sample buf[4096];
    int n = speechPlayer_synthesize(engine->player,
                                    (unsigned int)maxSamples, buf);
    if (n <= 0) return 0;

    /* Apply volume gain with hard clipping.
     * macOS system TTS path is louder than standalone; keep modest. */
    static const double kGain = 1.7;
    for (int i = 0; i < n; i++) {
        double s = buf[i].value * kGain;
        if (s > 32767.0) s = 32767.0;
        if (s < -32767.0) s = -32767.0;
        outBuffer[i] = (int16_t)s;
    }

    return n;
}

void tgsb_stop(TgsbEngine *engine)
{
    if (engine) engine->stopRequested = 1;
}

int tgsb_get_num_voices(void)
{
    return kNumPresets;
}

const char *tgsb_get_voice_name(int index)
{
    if (index < 0 || index >= kNumPresets) return NULL;
    return kPresets[index].name;
}

void tgsb_set_voicing_tone(TgsbEngine *engine,
    double voicedTiltDbPerOct,
    double noiseGlottalModDepth,
    double pitchSyncF1DeltaHz,
    double pitchSyncB1DeltaHz,
    double speedQuotient,
    double aspirationTiltDbPerOct,
    double cascadeBwScale,
    double tremorDepth)
{
    if (!engine || !engine->player) return;

    engine->hasUserTone = 1;
    engine->userVoicedTiltDbPerOct = voicedTiltDbPerOct;
    engine->userNoiseGlottalModDepth = noiseGlottalModDepth;
    engine->userPitchSyncF1DeltaHz = pitchSyncF1DeltaHz;
    engine->userPitchSyncB1DeltaHz = pitchSyncB1DeltaHz;
    engine->userSpeedQuotient = speedQuotient;
    engine->userAspirationTiltDbPerOct = aspirationTiltDbPerOct;
    engine->userCascadeBwScale = cascadeBwScale;
    engine->userTremorDepth = tremorDepth;

    speechPlayer_voicingTone_t tone = speechPlayer_getDefaultVoicingTone();
    const VoicePreset *vp = &kPresets[engine->voiceIndex];
    if (vp->hasVoicedTilt)
        tone.voicedTiltDbPerOct = vp->voicedTiltDbPerOct;

    tone.voicedTiltDbPerOct += voicedTiltDbPerOct;
    tone.noiseGlottalModDepth = noiseGlottalModDepth;
    tone.pitchSyncF1DeltaHz = pitchSyncF1DeltaHz;
    tone.pitchSyncB1DeltaHz = pitchSyncB1DeltaHz;
    tone.speedQuotient = speedQuotient;
    tone.aspirationTiltDbPerOct = aspirationTiltDbPerOct;
    tone.cascadeBwScale = cascadeBwScale;
    tone.tremorDepth = tremorDepth;

    speechPlayer_setVoicingTone(engine->player, &tone);
}

void tgsb_set_frame_ex_defaults(TgsbEngine *engine,
    double creakiness, double breathiness,
    double jitter, double shimmer, double sharpness)
{
    if (!engine || !engine->frontend) return;
    nvspFrontend_setFrameExDefaults(
        engine->frontend,
        creakiness, breathiness, jitter, shimmer, sharpness);
}

int tgsb_set_pitch_mode(TgsbEngine *engine, const char *mode)
{
    if (!engine || !engine->frontend || !mode) return 0;
    return nvspFrontend_setPitchMode(engine->frontend, mode);
}

void tgsb_set_legacy_pitch_inflection_scale(TgsbEngine *engine, double scale)
{
    if (!engine || !engine->frontend) return;
    nvspFrontend_setLegacyPitchInflectionScale(engine->frontend, scale);
}

} /* extern "C" */
