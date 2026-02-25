/*
 * tgsb_phonemizer.c — eSpeak phonemizer implementation.
 *
 * License: GPL-3.0
 */

#include "tgsb_phonemizer.h"

#include <espeak-ng/speak_lib.h>
#include <stdlib.h>
#include <string.h>

static int g_initialized = 0;

int tgsb_phonemizer_init(const char *espeakDataPath)
{
    if (g_initialized) return 1;

    int sr = espeak_Initialize(AUDIO_OUTPUT_SYNCHRONOUS, 0,
                               espeakDataPath, 0x8000);
    if (sr <= 0) return 0;

    /* Verify data actually loaded */
    espeak_VOICE voice_spec;
    memset(&voice_spec, 0, sizeof(voice_spec));
    voice_spec.languages = "en-us";
    if (espeak_SetVoiceByProperties(&voice_spec) != EE_OK) {
        espeak_Terminate();
        return 0;
    }

    g_initialized = 1;
    return 1;
}

void tgsb_phonemizer_terminate(void)
{
    if (g_initialized) {
        espeak_Terminate();
        g_initialized = 0;
    }
}

int tgsb_phonemizer_set_language(const char *espeakLang)
{
    if (!g_initialized || !espeakLang) return 0;

    espeak_VOICE voice_spec;
    memset(&voice_spec, 0, sizeof(voice_spec));
    voice_spec.languages = espeakLang;
    return espeak_SetVoiceByProperties(&voice_spec) == EE_OK ? 1 : 0;
}

char *tgsb_phonemizer_phonemize(const char *text)
{
    if (!g_initialized || !text || !*text) return NULL;

    /* Accumulate IPA clauses into a growable buffer */
    size_t cap = 1024;
    size_t len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) return NULL;
    buf[0] = '\0';

    const void *textPtr = text;
    while (textPtr && *(const char *)textPtr) {
        const char *ipa = espeak_TextToPhonemes(
            &textPtr, espeakCHARS_UTF8, 0x02 /* IPA */);
        if (!ipa || !*ipa) continue;

        size_t ipaLen = strlen(ipa);
        /* +2 for newline separator + null */
        while (len + ipaLen + 2 > cap) {
            cap *= 2;
            char *tmp = (char *)realloc(buf, cap);
            if (!tmp) { free(buf); return NULL; }
            buf = tmp;
        }

        if (len > 0) {
            buf[len++] = '\n';
        }
        memcpy(buf + len, ipa, ipaLen);
        len += ipaLen;
        buf[len] = '\0';
    }

    return buf;
}
