/*
 * tgsb_jni.cpp — JNI bridge for Android TTS Service.
 *
 * Full pipeline: text → eSpeak IPA → nvspFrontend → speechPlayer → PCM
 *
 * License: GPL-3.0 (links eSpeak-ng GPL code with TGSpeechBox MIT code;
 *          combined binary is GPL-3.0 per copyleft rules)
 */

#include <jni.h>
#include <android/log.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <espeak-ng/speak_lib.h>
#include "speechPlayer.h"
#include "nvspFrontend.h"

#define TAG "TgsbJNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

/* ------------------------------------------------------------------ */
/* Voice presets                                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    size_t offset;    /* offsetof the double field in speechPlayer_frame_t */
    double value;
    int isMultiplier; /* 1 = multiply, 0 = override */
} FrameOverride;

typedef struct {
    const char *name;
    const FrameOverride *overrides;
    int numOverrides;
    /* VoicingTone delta (applied on top of defaults) */
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
/* Engine instance                                                     */
/* ------------------------------------------------------------------ */

typedef struct {
    speechPlayer_handle_t player;
    nvspFrontend_handle_t frontend;
    int sampleRate;
    volatile int stopRequested;
    int voiceIndex;
} TgsbEngine;

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
/* JNI functions                                                       */
/* ------------------------------------------------------------------ */

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_tgspeechbox_tts_TgsbTtsService_nativeCreate(
    JNIEnv *env, jobject thiz,
    jstring espeakDataPath, jstring packDirPath, jint sampleRate
) {
    const char *dataPath = env->GetStringUTFChars(espeakDataPath, NULL);
    const char *packDir  = env->GetStringUTFChars(packDirPath, NULL);

    LOGI("nativeCreate: espeakData=%s packDir=%s sr=%d", dataPath, packDir, sampleRate);

    /* Initialize eSpeak */
    int espeakSr = espeak_Initialize(AUDIO_OUTPUT_SYNCHRONOUS, 0, dataPath, 0);
    if (espeakSr <= 0) {
        LOGE("espeak_Initialize failed: %d", espeakSr);
        env->ReleaseStringUTFChars(espeakDataPath, dataPath);
        env->ReleaseStringUTFChars(packDirPath, packDir);
        return 0;
    }
    {
        espeak_VOICE voice_spec;
        memset(&voice_spec, 0, sizeof(voice_spec));
        voice_spec.languages = "en-us";
        espeak_SetVoiceByProperties(&voice_spec);
    }
    LOGI("eSpeak initialized (sr=%d)", espeakSr);

    /* Create TGSpeechBox components */
    speechPlayer_handle_t player = speechPlayer_initialize(sampleRate);
    nvspFrontend_handle_t fe = nvspFrontend_create(packDir);
    if (!fe) {
        LOGE("nvspFrontend_create failed");
        speechPlayer_terminate(player);
        espeak_Terminate();
        env->ReleaseStringUTFChars(espeakDataPath, dataPath);
        env->ReleaseStringUTFChars(packDirPath, packDir);
        return 0;
    }
    int langOk = nvspFrontend_setLanguage(fe, "en-us");
    LOGI("nvspFrontend created, setLanguage('en-us')=%d", langOk);

    /* Set default voicing tone */
    speechPlayer_voicingTone_t tone = speechPlayer_getDefaultVoicingTone();
    speechPlayer_setVoicingTone(player, &tone);

    TgsbEngine *engine = (TgsbEngine *)calloc(1, sizeof(TgsbEngine));
    engine->player = player;
    engine->frontend = fe;
    engine->sampleRate = sampleRate;
    engine->stopRequested = 0;
    engine->voiceIndex = 0; /* Adam */

    env->ReleaseStringUTFChars(espeakDataPath, dataPath);
    env->ReleaseStringUTFChars(packDirPath, packDir);

    LOGI("Engine ready (handle=%p)", (void *)engine);
    return (jlong)(intptr_t)engine;
}

JNIEXPORT void JNICALL
Java_com_tgspeechbox_tts_TgsbTtsService_nativeDestroy(
    JNIEnv *env, jobject thiz, jlong handle
) {
    TgsbEngine *engine = (TgsbEngine *)(intptr_t)handle;
    if (!engine) return;

    if (engine->frontend) nvspFrontend_destroy(engine->frontend);
    if (engine->player) speechPlayer_terminate(engine->player);
    espeak_Terminate();
    free(engine);
    LOGI("Engine destroyed");
}

JNIEXPORT void JNICALL
Java_com_tgspeechbox_tts_TgsbTtsService_nativeSetVoice(
    JNIEnv *env, jobject thiz, jlong handle, jstring voiceName
) {
    TgsbEngine *engine = (TgsbEngine *)(intptr_t)handle;
    if (!engine) return;

    const char *name = env->GetStringUTFChars(voiceName, NULL);
    for (int i = 0; i < kNumPresets; i++) {
        if (strcmp(kPresets[i].name, name) == 0) {
            engine->voiceIndex = i;

            /* Apply VoicingTone changes for this preset */
            speechPlayer_voicingTone_t tone = speechPlayer_getDefaultVoicingTone();
            if (kPresets[i].hasVoicedTilt)
                tone.voicedTiltDbPerOct = kPresets[i].voicedTiltDbPerOct;
            speechPlayer_setVoicingTone(engine->player, &tone);

            LOGI("Voice set to: %s (index=%d)", name, i);
            break;
        }
    }
    env->ReleaseStringUTFChars(voiceName, name);
}

/*
 * nativeQueueText — Phase 1: text → IPA → frames (fast, <10ms).
 * Queues frames into speechPlayer; audio can be pulled immediately after.
 */
JNIEXPORT void JNICALL
Java_com_tgspeechbox_tts_TgsbTtsService_nativeQueueText(
    JNIEnv *env, jobject thiz,
    jlong handle, jstring text, jint speechRate, jint pitch
) {
    TgsbEngine *engine = (TgsbEngine *)(intptr_t)handle;
    if (!engine || !engine->player || !engine->frontend) return;

    engine->stopRequested = 0;

    /* Purge any stale frames from previous utterance so they don't
     * leak into the start of the new one on interruption. */
    speechPlayer_queueFrame(engine->player, NULL, 0, 0, -1, true);

    const char *textChars = env->GetStringUTFChars(text, NULL);
    if (!textChars || !*textChars) {
        if (textChars) env->ReleaseStringUTFChars(text, textChars);
        return;
    }

    /* Map Android rate/pitch to TGSpeechBox params */
    double speed = (double)speechRate / 100.0;
    if (speed < 0.1) speed = 0.1;
    if (speed > 5.0) speed = 5.0;

    double basePitch = 120.0 * ((double)pitch / 100.0);
    if (basePitch < 40.0) basePitch = 40.0;
    if (basePitch > 500.0) basePitch = 500.0;

    /* Text → IPA via eSpeak (clause by clause), then queue frames */
    FrameCtx ctx;
    ctx.engine = engine;
    ctx.frameCount = 0;

    const void *textPtr = textChars;
    while (textPtr && *(const char *)textPtr && !engine->stopRequested) {
        const char *ipa = espeak_TextToPhonemes(
            &textPtr, espeakCHARS_UTF8, 0x02 /* IPA */);
        if (!ipa || !*ipa) continue;

        nvspFrontend_queueIPA_Ex(
            engine->frontend, ipa,
            speed, basePitch, 0.5, ".", 0,
            onFrame, &ctx
        );
    }
    env->ReleaseStringUTFChars(text, textChars);
}

/*
 * nativePullAudio — Phase 2: pull PCM in small chunks (streaming).
 * Call in a loop until it returns 0. Each call fills outBuffer with
 * s16le PCM and returns bytes written.
 */
JNIEXPORT jint JNICALL
Java_com_tgspeechbox_tts_TgsbTtsService_nativePullAudio(
    JNIEnv *env, jobject thiz,
    jlong handle, jbyteArray outBuffer, jint maxBytes
) {
    TgsbEngine *engine = (TgsbEngine *)(intptr_t)handle;
    if (!engine || !engine->player || engine->stopRequested) return 0;

    /* maxBytes → max samples (2 bytes per sample) */
    int maxSamples = maxBytes / 2;
    if (maxSamples <= 0) return 0;
    if (maxSamples > 4096) maxSamples = 4096;

    sample buf[4096];
    int n = speechPlayer_synthesize(engine->player,
                                    (unsigned int)maxSamples, buf);
    if (n <= 0) return 0;

    /* Convert sample structs to raw s16le bytes with volume boost.
     * TGSpeechBox output is conservative (~60% headroom); scale up
     * so the engine sits at a normal volume without the user having
     * to crank accessibility volume. */
    static const double kGain = 3.0;
    int16_t pcm[4096];
    for (int i = 0; i < n; i++) {
        double s = buf[i].value * kGain;
        if (s > 32767.0) s = 32767.0;
        if (s < -32767.0) s = -32767.0;
        pcm[i] = (int16_t)s;
    }

    int byteLen = n * 2;
    env->SetByteArrayRegion(outBuffer, 0, byteLen, (jbyte *)pcm);
    return byteLen;
}

JNIEXPORT void JNICALL
Java_com_tgspeechbox_tts_TgsbTtsService_nativeStop(
    JNIEnv *env, jobject thiz, jlong handle
) {
    TgsbEngine *engine = (TgsbEngine *)(intptr_t)handle;
    if (engine) engine->stopRequested = 1;
}

JNIEXPORT jint JNICALL
Java_com_tgspeechbox_tts_TgsbTtsService_nativeSetLanguage(
    JNIEnv *env, jobject thiz, jlong handle,
    jstring espeakLang, jstring tgsbLang
) {
    TgsbEngine *engine = (TgsbEngine *)(intptr_t)handle;
    if (!engine) return -1;

    const char *eLang = env->GetStringUTFChars(espeakLang, NULL);
    const char *tLang = env->GetStringUTFChars(tgsbLang, NULL);

    int result = 0;
    {
        espeak_VOICE voice_spec;
        memset(&voice_spec, 0, sizeof(voice_spec));
        voice_spec.languages = eLang;
        espeak_ERROR err = espeak_SetVoiceByProperties(&voice_spec);
        if (err != EE_OK) {
            LOGE("espeak_SetVoiceByProperties failed for '%s': error=%d",
                 eLang, (int)err);
            result = -1;
        }
    }
    int langOk = nvspFrontend_setLanguage(engine->frontend, tLang);
    if (!langOk) {
        LOGE("nvspFrontend_setLanguage failed for '%s'", tLang);
        result = -1;
    }

    LOGI("Language set: espeak=%s tgsb=%s (frontendOk=%d result=%d)",
         eLang, tLang, langOk, result);

    env->ReleaseStringUTFChars(espeakLang, eLang);
    env->ReleaseStringUTFChars(tgsbLang, tLang);
    return result;
}

} /* extern "C" */
