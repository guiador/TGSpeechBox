/*
 * tgsb_phonemizer.h — eSpeak phonemizer for XPC service.
 *
 * Isolates all GPL (eSpeak-NG) code behind a process boundary.
 * The AU extension never links against this.
 *
 * License: GPL-3.0 (links eSpeak-NG)
 */

#ifndef TGSB_PHONEMIZER_H
#define TGSB_PHONEMIZER_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize eSpeak with the given data directory.
 * Returns 1 on success, 0 on failure.
 */
int tgsb_phonemizer_init(const char *espeakDataPath);

/**
 * Shut down eSpeak. Call on service teardown.
 */
void tgsb_phonemizer_terminate(void);

/**
 * Set the eSpeak language (e.g. "en-us", "fr").
 * Returns 1 on success, 0 on failure.
 */
int tgsb_phonemizer_set_language(const char *espeakLang);

/**
 * Phonemize text to IPA.
 * Returns a malloc'd string the caller must free, or NULL on failure.
 * The IPA contains clause separators as newlines.
 */
char *tgsb_phonemizer_phonemize(const char *text);

#ifdef __cplusplus
}
#endif

#endif /* TGSB_PHONEMIZER_H */
