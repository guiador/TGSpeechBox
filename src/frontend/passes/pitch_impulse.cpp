/*
TGSpeechBox — Impulse pitch model pass.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

// =============================================================================
// Impulse Pitch Pass — pitch contour generation
// =============================================================================
//
// Multi-layer additive pitch model.  Four independent pitch layers are
// summed per token, each contributing a different aspect of natural
// intonation.  This architecture produces lively, speech-like contours
// by letting declination, phrasal hat patterns, lexical stress, and
// terminal gestures interact without fighting each other.
//
// Layer 1 — Proportional declination ramp:
//   Pitch starts elevated (+range/2) and ends depressed (-range/2),
//   distributed proportionally across voiced duration.  This replaces
//   the old fixed Hz/sec slope.
//
// Layer 2 — Hat-pattern rise/fall:
//   Pitch rises by impulseRiseHz at the onset of a word containing a
//   primary-stressed vowel.  It falls by riseHz * hatFallScale at the
//   next word boundary.  Because fall > rise, each cycle contributes
//   a net decline that reinforces the baseline ramp.
//
// Layer 3 — Stress peaks:
//   Additive Hz boost on the stressed vowel nucleus, declining by
//   count position (1st > 2nd > 3rd > 4th+).  All scaled by stressGain.
//   Terminal-position stress is INVERTED (pitch drops instead of rising).
//   Secondary stress gets a reduced peak.
//
// Layer 4 — Terminal gesture:
//   Statement: pitch drops on final vowel.
//   Question: pitch rises across final vowel.
//   Comma: slight continuation rise.
//
// Smoothing: single-pass forward IIR with alpha=0.55 (much lighter than
// the old two-pass alpha=0.3 which crushed the contour).

#include "pitch_impulse.h"
#include "pitch_common.h"
#include "../ipa_engine.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace nvsp_frontend {

void applyPitchImpulse(
    std::vector<Token>& tokens,
    const PackSet& pack,
    double speed,
    double basePitch,
    double inflection,
    char clauseType) {

  if (tokens.empty()) return;

  const auto& lang = pack.lang;

  // -------------------------------------------------------------------------
  // Read settings
  // -------------------------------------------------------------------------
  const double declinRangeHz      = lang.impulseDeclinationRangeHz;
  const double declinHzPerSec     = lang.impulseDeclinationHzPerSec;
  const double riseHz             = lang.impulseRiseHz;
  const double hatFallScale       = lang.impulseHatFallScale;
  const double firstBoost         = lang.impulseFirstStressBoostHz;
  const double secondBoost        = lang.impulseSecondStressBoostHz;
  const double thirdBoost         = lang.impulseThirdStressBoostHz;
  const double fourthBoost        = lang.impulseFourthStressBoostHz;
  const double stressGain         = lang.impulseStressGain;
  const double secStressScale     = lang.impulseSecondaryStressScale;
  const double termStressHz       = lang.impulseTerminalStressHz;
  const double questionReduction  = lang.impulseQuestionReduction;
  const double terminalFallHz     = lang.impulseTerminalFallHz;
  const double continuationRiseHz = lang.impulseContinuationRiseHz;
  const double questionRiseHz     = lang.impulseQuestionRiseHz;
  const double assertiveness      = lang.impulseAssertiveness;
  const double smoothAlpha        = std::max(0.01, std::min(1.0,
                                     lang.impulseSmoothAlpha));

  const size_t n = tokens.size();

  // =========================================================================
  // Phase 1: Structural scan
  // =========================================================================

  // 1a. Compute total phonetic duration.
  double totalPhoneticMs = 0.0;
  for (const auto& t : tokens) {
    if (!t.silence && t.def) totalPhoneticMs += t.durationMs;
  }

  // 1b. Find word boundaries and which words contain primary/secondary stress.
  // wordStartIndices[w] = token index of word start
  // wordHasStress[w]    = highest stress level in that word (1=primary, 2=secondary)
  std::vector<int> wordStartIndices;
  std::vector<int> wordHasStress;

  int curWordStress = 0;
  int pendingStress = 0;

  for (size_t i = 0; i < n; ++i) {
    const Token& t = tokens[i];

    if (t.wordStart) {
      // Close previous word's stress.
      if (!wordStartIndices.empty()) {
        wordHasStress.push_back(curWordStress);
      }
      wordStartIndices.push_back(static_cast<int>(i));
      curWordStress = 0;
    }

    if (t.syllableStart) {
      pendingStress = t.stress;
    }

    // Check for stress on vowel nuclei.
    if (tokenIsVowel(t) && pendingStress > 0) {
      if (pendingStress == 1) {
        curWordStress = std::max(curWordStress, 1);  // primary wins
      } else if (pendingStress == 2 && curWordStress == 0) {
        curWordStress = 2;  // secondary only if no primary yet
      }
      pendingStress = 0;
    }
  }
  // Close the last word.
  wordHasStress.push_back(curWordStress);

  const int numWords = static_cast<int>(wordStartIndices.size());

  // 1c. Find the terminal word (last word with a vowel) and its last vowel.
  int terminalWordIdx = numWords - 1;
  int finalVowelIdx = -1;
  for (int i = static_cast<int>(n) - 1; i >= 0; --i) {
    const Token& t = tokens[static_cast<size_t>(i)];
    if (t.silence || !t.def) continue;
    if (tokenIsVowel(t)) {
      finalVowelIdx = i;
      break;
    }
  }

  // 1d. Find the last primary-stressed vowel for terminal stress inversion.
  int lastPrimaryVowelIdx = -1;
  {
    int ps = 0;
    for (int i = static_cast<int>(n) - 1; i >= 0; --i) {
      const Token& t = tokens[static_cast<size_t>(i)];
      if (t.syllableStart) ps = t.stress;
      if (tokenIsVowel(t) && ps == 1) {
        lastPrimaryVowelIdx = i;
        break;
      }
    }
  }

  // =========================================================================
  // Phase 2: Build per-token pitch from 4 layers
  // =========================================================================

  std::vector<double> rawStart(n, basePitch);
  std::vector<double> rawEnd(n, basePitch);
  std::vector<bool>   isPhonetic(n, false);

  double elapsedMs = 0.0;
  int stressCount = 0;
  double hatOffset = 0.0;         // accumulated rise/fall offset
  int currentWordIdx = 0;         // which word we're in
  bool inStressedWord = false;    // current word has primary stress
  pendingStress = 0;

  for (size_t i = 0; i < n; ++i) {
    Token& t = tokens[i];

    // Track word transitions for hat pattern.
    if (t.wordStart && i > 0) {
      // Leaving previous word: if it was stressed, apply fall.
      if (inStressedWord) {
        double fallAmt = riseHz * hatFallScale * inflection * assertiveness;
        if (clauseType == '?') fallAmt *= 0.3;  // less fall in questions
        hatOffset -= fallAmt;
      }
      // Dampen hat accumulation: leaky integrator prevents unbounded
      // pitch decline over long phrases.  Each word boundary decays the
      // offset by 15%, so later words don't keep drilling down.
      hatOffset *= 0.85;
      currentWordIdx++;
    }

    // Entering a new word: check if it contains stress.
    if (t.wordStart && currentWordIdx < numWords) {
      inStressedWord = (wordHasStress[static_cast<size_t>(currentWordIdx)] == 1);

      // Hat rise at onset of stressed words.
      if (inStressedWord) {
        double riseAmt = riseHz * inflection;
        if (clauseType == '?') riseAmt *= questionReduction;
        hatOffset += riseAmt;
      }
    }

    if (t.syllableStart) {
      pendingStress = t.stress;
    }

    if (t.silence || !t.def) {
      rawStart[i] = basePitch + hatOffset;
      rawEnd[i]   = basePitch + hatOffset;
      continue;
    }

    isPhonetic[i] = true;

    // ---- Layer 1: Proportional declination ----
    double declinStart, declinEnd;
    if (declinRangeHz > 0.0 && totalPhoneticMs > 0.0) {
      // Proportional: distribute range across clause.
      double halfRange = (declinRangeHz * inflection) / 2.0;
      double progressStart = elapsedMs / totalPhoneticMs;
      double progressEnd = (elapsedMs + t.durationMs) / totalPhoneticMs;
      declinStart = halfRange - (progressStart * declinRangeHz * inflection);
      declinEnd   = halfRange - (progressEnd   * declinRangeHz * inflection);
    } else {
      // Legacy fixed Hz/sec mode.
      declinStart = -(declinHzPerSec * elapsedMs / 1000.0 * inflection * speed);
      declinEnd   = -(declinHzPerSec * (elapsedMs + t.durationMs) / 1000.0
                       * inflection * speed);
    }

    elapsedMs += t.durationMs;

    // ---- Layer 2: Hat offset (already accumulated above) ----
    // hatOffset is the running accumulation of rises and falls.

    // ---- Layer 3: Stress peaks ----
    double stressPeak = 0.0;
    bool isVowel = tokenIsVowel(t);

    if (isVowel && (pendingStress == 1 || pendingStress == 2)) {
      bool isPrimary = (pendingStress == 1);
      bool isTerminal = (static_cast<int>(i) == lastPrimaryVowelIdx &&
                         (clauseType == '.' || clauseType == '!'));

      if (isTerminal && isPrimary) {
        // Terminal primary stress DROPS pitch instead of boosting.
        stressPeak = termStressHz * inflection * assertiveness;
      } else if (isPrimary) {
        // Count-dependent boost.
        double boost = fourthBoost;
        if (stressCount == 0)      boost = firstBoost;
        else if (stressCount == 1) boost = secondBoost;
        else if (stressCount == 2) boost = thirdBoost;

        boost *= stressGain * inflection;
        if (clauseType == '?') boost *= questionReduction;

        stressPeak = boost;
        stressCount++;
      } else {
        // Secondary stress: scaled-down primary boost.
        double boost = fourthBoost;
        if (stressCount == 0)      boost = firstBoost;
        else if (stressCount == 1) boost = secondBoost;
        else if (stressCount == 2) boost = thirdBoost;

        boost *= stressGain * secStressScale * inflection;
        if (clauseType == '?') boost *= questionReduction;

        stressPeak = boost;
        // Don't increment stressCount for secondary.
      }

      pendingStress = 0;
    }

    // ---- Layer 4: Terminal gesture ----
    double terminalOffset = 0.0;
    double terminalEndOffset = 0.0;
    if (static_cast<int>(i) == finalVowelIdx) {
      if (clauseType == '.' || clauseType == '!') {
        terminalEndOffset = -(terminalFallHz * assertiveness * inflection);
      } else if (clauseType == '?') {
        // Question: two-stage rise for natural upturn.
        terminalOffset = questionRiseHz * 0.4 * inflection;
        terminalEndOffset = questionRiseHz * inflection;
      } else if (clauseType == ',') {
        terminalEndOffset = continuationRiseHz * inflection;
      }
    }

    // ---- Combine all layers ----
    // Stress peak applies fully at start of vowel, decays to baseline by end.
    double startPitch = basePitch + declinStart + hatOffset + stressPeak + terminalOffset;
    double endPitch   = basePitch + declinEnd   + hatOffset + terminalEndOffset;

    // Clamp: don't let pitch drop too far below the base.
    // 0.75 keeps a 100Hz voice above 75Hz (0.5 bottomed at 50Hz).
    const double floor = basePitch * 0.75;
    startPitch = std::max(startPitch, floor);
    endPitch   = std::max(endPitch,   floor);

    rawStart[i] = startPitch;
    rawEnd[i]   = endPitch;
  }

  // If the last word was stressed, apply the final hat fall.
  // (This handles the case where the clause ends on a stressed word.)
  if (inStressedWord) {
    // The terminal gesture already handles final pitch shaping,
    // so we don't add another hat fall on the final vowel.
    // But we do let the accumulated hatOffset carry through.
  }

  // =========================================================================
  // Phase 3: Single-pass forward IIR smoothing
  // =========================================================================
  // Much lighter than the old two-pass: preserves stress peaks and hat contour
  // while smoothing transitions between tokens.

  // Smooth startPitch.
  {
    double state = basePitch;
    for (size_t i = 0; i < n; ++i) {
      if (isPhonetic[i]) { state = rawStart[i]; break; }
    }
    for (size_t i = 0; i < n; ++i) {
      if (!isPhonetic[i]) continue;
      state += smoothAlpha * (rawStart[i] - state);
      rawStart[i] = state;
    }
  }

  // Smooth endPitch.
  {
    double state = basePitch;
    for (size_t i = 0; i < n; ++i) {
      if (isPhonetic[i]) { state = rawEnd[i]; break; }
    }
    for (size_t i = 0; i < n; ++i) {
      if (!isPhonetic[i]) continue;
      state += smoothAlpha * (rawEnd[i] - state);
      rawEnd[i] = state;
    }
  }

  // =========================================================================
  // Phase 4: Write back to tokens
  // =========================================================================
  double lastPitch = basePitch;
  for (size_t i = 0; i < n; ++i) {
    Token& t = tokens[i];
    if (t.silence || !t.def) {
      setPitchFields(t, lastPitch, lastPitch);
      continue;
    }
    setPitchFields(t, rawStart[i], rawEnd[i]);
    lastPitch = rawEnd[i];
  }
}

} // namespace nvsp_frontend
