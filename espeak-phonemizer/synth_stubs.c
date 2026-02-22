/*
 * synth_stubs.c — Stub implementations for eSpeak-ng synthesis code.
 *
 * The phonemizer-only build excludes wavegen.c, synthesize.c, intonation.c,
 * klatt.c, and other synthesis files. This file provides the extern globals
 * and function stubs that the included phonemizer files reference but that
 * are NOT already defined in the included .c files.
 *
 * Symbols defined in included files (DO NOT duplicate here):
 *   - wavefile_data, version_phdata  (synthdata.c)
 *   - event_list, MarkerEvent        (speech.c)
 *   - SetSpeed                       (setlengths.c)
 *
 * License: GPL-3.0 (same as eSpeak-ng)
 */

#include "config.h"

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <espeak-ng/espeak_ng.h>
#include <espeak-ng/speak_lib.h>

#include "phoneme.h"
#include "voice.h"
#include "synthesize.h"
#include "wavegen.h"

/* ---- Globals that no included .c file defines ---- */

int samplerate = 22050;

intptr_t wcmdq[N_WCMDQ][4];
int wcmdq_head = 0;
int wcmdq_tail = 0;

int echo_head = 0;
int echo_tail = 0;
int echo_amp = 0;
short echo_buf[N_ECHO_BUF];

unsigned char *out_ptr = NULL;
unsigned char *out_end = NULL;

/* formant_rate[9] is defined in voices.c */
SPEED_FACTORS speed = {0};

const unsigned char *const envelope_data[N_ENVELOPE_DATA] = {0};

/* Phoneme list globals (used by translate.c, phonemelist.c, dictionary.c, etc.) */
int n_phoneme_list = 0;
PHONEME_LIST phoneme_list[N_PHONEME_LIST + 1];

/* Embedded command value arrays (used by speech.c, readclause.c, setlengths.c) */
int embedded_value[N_EMBEDDED_VALUES] = {0};
const int embedded_default[N_EMBEDDED_VALUES] = {0};

/* env_fall: a simple falling envelope used by phonemelist.c */
const unsigned char env_fall[128] = {
    0xff, 0xfd, 0xfa, 0xf8, 0xf6, 0xf4, 0xf2, 0xf0,
    0xee, 0xec, 0xea, 0xe8, 0xe6, 0xe4, 0xe2, 0xe0,
    0xde, 0xdc, 0xda, 0xd8, 0xd6, 0xd4, 0xd2, 0xd0,
    0xce, 0xcc, 0xca, 0xc8, 0xc6, 0xc4, 0xc2, 0xc0,
    0xbe, 0xbc, 0xba, 0xb8, 0xb6, 0xb4, 0xb2, 0xb0,
    0xae, 0xac, 0xaa, 0xa8, 0xa6, 0xa4, 0xa2, 0xa0,
    0x9e, 0x9c, 0x9a, 0x98, 0x96, 0x94, 0x92, 0x90,
    0x8e, 0x8c, 0x8a, 0x88, 0x86, 0x84, 0x82, 0x80,
    0x7e, 0x7c, 0x7a, 0x78, 0x76, 0x74, 0x72, 0x70,
    0x6e, 0x6c, 0x6a, 0x68, 0x66, 0x64, 0x62, 0x60,
    0x5e, 0x5c, 0x5a, 0x58, 0x56, 0x54, 0x52, 0x50,
    0x4e, 0x4c, 0x4a, 0x48, 0x46, 0x44, 0x42, 0x40,
    0x3e, 0x3c, 0x3a, 0x38, 0x36, 0x34, 0x32, 0x30,
    0x2e, 0x2c, 0x2a, 0x28, 0x26, 0x24, 0x22, 0x20,
    0x1e, 0x1c, 0x1a, 0x18, 0x16, 0x14, 0x12, 0x10,
    0x0e, 0x0c, 0x0a, 0x08, 0x06, 0x04, 0x02, 0x00,
};

/* ---- Function stubs from wavegen.h ---- */

void WavegenInit(int rate, int wavemult_fact)
{
    (void)wavemult_fact;
    samplerate = rate > 0 ? rate : 22050;
}

void WavegenFini(void) {}

int WavegenFill(void) { return 0; }

void WavegenSetVoice(voice_t *v) { (void)v; }

int WcmdqFree(void) { return N_WCMDQ; }

void WcmdqStop(void) {
    wcmdq_head = 0;
    wcmdq_tail = 0;
}

int WcmdqUsed(void) { return 0; }

void WcmdqInc(void) {
    wcmdq_tail++;
    if (wcmdq_tail >= N_WCMDQ)
        wcmdq_tail = 0;
}

int GetAmplitude(void) { return 100; }

void InitBreath(void) {}

int PeaksToHarmspect(wavegen_peaks_t *peaks, int pitch, int *htab, int control)
{
    (void)peaks; (void)pitch; (void)htab; (void)control;
    return 0;
}

void SetPitch2(voice_t *voice, int pitch1, int pitch2, int *pitch_base, int *pitch_range)
{
    (void)voice; (void)pitch1; (void)pitch2;
    if (pitch_base) *pitch_base = pitch1;
    if (pitch_range) *pitch_range = pitch2 - pitch1;
}

/* ---- Function stubs from synthesize.h / synthesize.c ---- */

void SynthesizeInit(void) {}

int Generate(PHONEME_LIST *pl, int *n_ph, bool resume)
{
    (void)pl; (void)n_ph; (void)resume;
    return 0;
}

int SpeakNextClause(int control)
{
    (void)control;
    return 0;
}

void SetEmbedded(int control, int value) { (void)control; (void)value; }

int FormantTransition2(frameref_t *seq, int *n_frames, unsigned int data1,
                       unsigned int data2, PHONEME_TAB *other_ph, int which)
{
    (void)seq; (void)n_frames; (void)data1; (void)data2; (void)other_ph; (void)which;
    return 0;
}

void Write4Bytes(FILE *f, int value) { (void)f; (void)value; }

void DoEmbedded(int *embix, int sourceix) { (void)embix; (void)sourceix; }

void DoMarker(int type, int char_posn, int length, int value)
{
    (void)type; (void)char_posn; (void)length; (void)value;
}

void DoPhonemeMarker(int type, int char_posn, int length, char *name)
{
    (void)type; (void)char_posn; (void)length; (void)name;
}

int DoSample3(PHONEME_DATA *phdata, int length_mod, int amp)
{
    (void)phdata; (void)length_mod; (void)amp;
    return 0;
}

int DoSpect2(PHONEME_TAB *this_ph, int which, FMT_PARAMS *fmt_params,
             PHONEME_LIST *plist, int modulation)
{
    (void)this_ph; (void)which; (void)fmt_params; (void)plist; (void)modulation;
    return 0;
}

int PauseLength(int pause, int control)
{
    (void)control;
    return pause;
}

const char *WordToString(char buf[5], unsigned int word)
{
    (void)word;
    if (buf) buf[0] = '\0';
    return buf;
}

/* DoVoiceChange — called by voices.c, defined in synthesize.c */
espeak_ng_STATUS DoVoiceChange(voice_t *v)
{
    (void)v;
    return ENS_OK;
}

/* ---- Stub for intonation.c (CalcPitches) ---- */

void CalcPitches(Translator *tr, int clause_type)
{
    (void)tr; (void)clause_type;
}

#if USE_LIBSONIC
void DoSonicSpeed(int value) { (void)value; }
#endif
