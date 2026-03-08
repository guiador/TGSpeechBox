/*
TGSpeechBox — Prominence pass (stress scoring and duration/amplitude realization).
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#include "prominence.h"

#include <algorithm>
#include <cmath>

namespace nvsp_frontend::passes {

namespace {

static inline bool isVowel(const Token& t) {
  return t.def && ((t.def->flags & kIsVowel) != 0);
}

static inline bool isSilenceOrMissing(const Token& t) {
  return t.silence || !t.def;
}

static inline bool isNasal(const Token& t) {
  return t.def && ((t.def->flags & kIsNasal) != 0);
}

static inline bool isLiquid(const Token& t) {
  return t.def && ((t.def->flags & kIsLiquid) != 0);
}

static inline bool isSemivowel(const Token& t) {
  return t.def && ((t.def->flags & kIsSemivowel) != 0);
}

static inline bool isSonorant(const Token& t) {
  return isNasal(t) || isLiquid(t) || isSemivowel(t);
}

static inline bool isSyntheticGap(const Token& t) {
  return t.preStopGap || t.postStopAspiration || t.vowelHiatusGap;
}

}  // namespace

bool runProminence(PassContext& ctx, std::vector<Token>& tokens, std::string& outError) {
  (void)outError;

  const auto& lang = ctx.pack.lang;
  if (!lang.prominenceEnabled) return true;
  if (tokens.empty()) return true;

  // Realization parameters (used by passes 2 and 3).
  const double primaryW   = lang.prominencePrimaryStressWeight;
  const double secondaryW = lang.prominenceSecondaryStressWeight;

  // Score settings (phonological classification).
  const double secondaryLevel = lang.prominenceSecondaryStressLevel;
  const double longVowelLevel = lang.prominenceLongVowelWeight;
  const std::string& longVowelMode = lang.prominenceLongVowelMode;
  const double wordInitBoost  = lang.prominenceWordInitialBoost;
  const double wordFinalReduc = lang.prominenceWordFinalReduction;

  // ── Pass 1: Compute raw prominence score for each vowel token ──
  //
  // Score reflects phonological stress category:
  //   primary stress   → 1.0
  //   secondary stress → secondaryLevel (default 0.6)
  //   unstressed       → 0.0
  // Plus additive word-position tweaks.  Clamped to [0, 1].

  // Build word-boundary info for word-position adjustments.
  struct WordInfo {
    size_t start = 0;
    int lastSyllStart = -1;
  };
  std::vector<WordInfo> words;
  for (size_t i = 0; i < tokens.size(); ++i) {
    if (tokens[i].wordStart || (i == 0)) {
      WordInfo wi;
      wi.start = i;
      words.push_back(wi);
    }
  }
  // Fill in lastSyllStart for each word
  for (size_t w = 0; w < words.size(); ++w) {
    size_t wEnd = (w + 1 < words.size()) ? words[w + 1].start : tokens.size();
    int lastSyll = -1;
    for (size_t i = words[w].start; i < wEnd; ++i) {
      if (isSilenceOrMissing(tokens[i])) continue;
      if (tokens[i].syllableStart) lastSyll = static_cast<int>(i);
    }
    words[w].lastSyllStart = lastSyll;
  }

  // Helper: find which word a token index belongs to
  auto wordIndexOf = [&](size_t tokIdx) -> size_t {
    for (size_t w = words.size(); w > 0; --w) {
      if (tokIdx >= words[w - 1].start) return w - 1;
    }
    return 0;
  };

  // ── Compute prominence per vowel ──
  for (size_t i = 0; i < tokens.size(); ++i) {
    Token& t = tokens[i];
    if (isSilenceOrMissing(t)) continue;

    // Only vowels get prominence scores; consonants get 0.0 (neutral).
    if (!isVowel(t)) {
      t.prominence = 0.0;
      continue;
    }

    // Diphthong offglides: inherit prominence from the preceding nucleus.
    // Without this, /ɪ/ in /aɪ/ gets scored 0.0 (unstressed) and receives
    // amplitude reduction, creating a 2-beat artifact instead of a smooth glide.
    if (t.tiedFrom) {
      // Walk backward to find the tied-to nucleus.
      for (size_t j = i; j > 0; --j) {
        const Token& prev = tokens[j - 1];
        if (isSilenceOrMissing(prev)) continue;
        if (isVowel(prev) && prev.tiedTo) {
          t.prominence = prev.prominence;  // may still be -1.0 if not scored yet
          break;
        }
        break;  // no tied-to found immediately before, give up
      }
      if (t.prominence < 0.0) t.prominence = 0.5;  // safe fallback: neutral
      continue;
    }

    double score = 0.0;

    // Source 1: Stress marks → categorical level
    if (t.stress == 1) {
      score = 1.0;
    } else if (t.stress == 2) {
      score = secondaryLevel;
    } else {
      // Vowel might inherit stress from syllable start (eSpeak puts
      // stress on syllable-initial consonant, not vowel). Walk backward
      // to the syllable start to check.
      for (size_t j = i; j > 0; --j) {
        const Token& prev = tokens[j - 1];
        if (prev.syllableStart) {
          if (prev.stress == 1) score = 1.0;
          else if (prev.stress == 2) score = secondaryLevel;
          break;
        }
        if (prev.wordStart) break;  // don't cross word boundaries
        if (isSilenceOrMissing(prev)) continue;
        if (isVowel(prev)) break;  // hit another vowel = different syllable
      }
    }

    // Source 2: Vowel length (ː)
    if (t.lengthened > 0 && longVowelMode != "never") {
      bool apply = false;
      if (longVowelMode == "always") {
        apply = true;
      } else {
        // "unstressed-only" (default): only boost if stress didn't already
        // give this vowel high prominence
        apply = (score < 0.01);  // effectively unstressed
      }
      if (apply) {
        score = std::max(score, longVowelLevel);
      }
    }

    // Source 3: Word position adjustments
    size_t wIdx = wordIndexOf(i);
    const WordInfo& wi = words[wIdx];

    // Word-initial: is this vowel in the first syllable of the word?
    // Check if there's no earlier vowel in this word.
    if (wordInitBoost > 0.0) {
      bool isFirstVowel = true;
      for (size_t j = wi.start; j < i; ++j) {
        if (!isSilenceOrMissing(tokens[j]) && isVowel(tokens[j])) {
          isFirstVowel = false;
          break;
        }
      }
      if (isFirstVowel) {
        score += wordInitBoost;
      }
    }

    // Word-final: is this vowel in the last syllable of the word?
    if (wordFinalReduc > 0.0 && wi.lastSyllStart >= 0) {
      // Check if this token is at or after the last syllable start
      if (static_cast<int>(i) >= wi.lastSyllStart) {
        score -= wordFinalReduc;
      }
    }

    // Clamp to [0.0, 1.0]
    t.prominence = std::max(0.0, std::min(1.0, score));
  }

  // ── Pass 1b: Monosyllable prominence floor ──
  //
  // Content monosyllables ("box", "cat", "top") are always prominent
  // in English even when eSpeak omits the stress mark. Without this,
  // they score 0.0 and hit the reducedCeiling penalty, making them
  // sound clipped and sharp.
  //
  // Heuristic: if a word contains exactly one vowel and that vowel's
  // prominence is below secondaryLevel, boost it to secondaryLevel.
  // This prevents reduction without over-promoting — the vowel gets
  // secondary-stress treatment (adequate duration) rather than primary.

  const double monoFloor = (lang.prominenceMonosyllableFloor > 0.0)
                             ? lang.prominenceMonosyllableFloor
                             : secondaryLevel;  // fallback to 0.6

  for (size_t w = 0; w < words.size(); ++w) {
    const size_t wStart = words[w].start;
    const size_t wEnd   = (w + 1 < words.size()) ? words[w + 1].start : tokens.size();

    // Count vowels and find the single vowel if monosyllabic.
    int vowelCount = 0;
    int monoVowelIdx = -1;
    for (size_t i = wStart; i < wEnd; ++i) {
      if (isSilenceOrMissing(tokens[i])) continue;
      if (tokens[i].tiedFrom) continue;  // don't count diphthong offglides
      if (isVowel(tokens[i])) {
        vowelCount++;
        monoVowelIdx = static_cast<int>(i);
      }
    }

    if (vowelCount == 1 && monoVowelIdx >= 0) {
      // Check function word exclusion list before boosting.
      if (!lang.prominenceMonosyllableExclude.empty()) {
        std::u32string wordShape;
        for (size_t i = wStart; i < wEnd; ++i) {
          const Token& t = tokens[i];
          if (isSilenceOrMissing(t)) continue;
          if (t.baseChar != 0) {
            wordShape.push_back(t.baseChar);
            for (int l = 0; l < t.lengthened; ++l) wordShape.push_back(U'\u02D0'); // ː
          }
        }
        bool excluded = false;
        for (const auto& pat : lang.prominenceMonosyllableExclude) {
          if (wordShape == pat) { excluded = true; break; }
        }
        if (excluded) continue;
      }

      Token& v = tokens[monoVowelIdx];
      if (v.prominence >= 0.0 && v.prominence < monoFloor) {
        v.prominence = monoFloor;
      }
    }
  }

  // ── Pass 1c: Full-vowel protection ──
  //
  // In English, full vowels (not schwa/reduced-ɪ) are almost never
  // truly unstressed. When eSpeak omits secondary stress on compound
  // word second elements ("Firefox", "laptop", "desktop"), the full
  // vowel should not be reduced. Boost it to a minimum floor so it
  // avoids the reducedCeiling penalty and gets the duration floor.

  const double fullVowelFloor = lang.prominenceFullVowelFloor;
  if (fullVowelFloor > 0.0) {
    // Pre-build excluded word shapes for function-word check.
    // Reuse the monosyllable exclusion list — it already contains
    // function words whose full vowels should NOT be promoted
    // (e.g. "for" /fɔː/, "or" /ɔː/, "was" /wɒz/).
    const auto& fvExclude = lang.prominenceMonosyllableExclude;

    for (size_t i = 0; i < tokens.size(); ++i) {
      Token& t = tokens[i];
      if (isSilenceOrMissing(t) || !isVowel(t)) continue;
      if (t.tiedFrom) continue;
      if (t.prominence < 0.0) continue;
      if (t.prominence >= fullVowelFloor) continue;

      // Reduced vowels that genuinely deserve low prominence.
      // Everything NOT in this list is considered "full" and gets
      // the floor applied.
      bool isReduced = false;
      if (t.baseChar != 0) {
        switch (t.baseChar) {
          case U'\u0259':  // ə  schwa
          case U'\u0250':  // ɐ  near-open central
          case U'\u1D4A':  // ᵊ  modifier schwa
          case U'\u0268':  // ɨ  barred-i
          case U'\u1D7B':  // ᵻ  barred-ɪ
          case U'\u026A':  // ɪ  kit vowel (reduced in "crystal", "rabbit")
          case U'\u028A':  // ʊ  foot vowel (reduced in unstressed positions)
          case U'\u028C':  // ʌ  strut vowel ("of" /ʌv/ shouldn't be boosted)
            isReduced = true;
            break;
          default:
            break;
        }
      }

      if (isReduced) continue;

      // Function word check: don't promote full vowels in words like
      // "for", "or", "was" — they should stay reduced despite having
      // a non-schwa vowel.
      if (!fvExclude.empty()) {
        size_t wIdx = wordIndexOf(i);
        size_t wStart = words[wIdx].start;
        size_t wEnd = (wIdx + 1 < words.size()) ? words[wIdx + 1].start : tokens.size();
        std::u32string wordShape;
        for (size_t j = wStart; j < wEnd; ++j) {
          const Token& tj = tokens[j];
          if (isSilenceOrMissing(tj)) continue;
          if (tj.baseChar != 0) {
            wordShape.push_back(tj.baseChar);
            for (int l = 0; l < tj.lengthened; ++l) wordShape.push_back(U'\u02D0'); // ː
          }
        }
        bool excluded = false;
        for (const auto& pat : fvExclude) {
          if (wordShape == pat) { excluded = true; break; }
        }
        if (excluded) continue;
      }

      t.prominence = fullVowelFloor;
    }
  }

  // ── Pass 2: Duration realization ──
  //
  // Continuous prominence-to-duration scaling (lerp):
  //   prominence >= 0.3 → lerp between secondaryStressWeight and primaryStressWeight
  //   prominence <  0.3 → unstressed → apply reducedCeiling
  //
  // This eliminates the cliff between primary (0.9) and secondary (0.4)
  // brackets.  At any prominence level the scale is proportional, so
  // relative contrast is preserved even at high rates.

  const double floorMs        = lang.prominenceDurationProminentFloorMs;
  const double primaryFloorMs = lang.prominenceDurationPrimaryFloorMs;
  const double reducedCeil    = lang.prominenceDurationReducedCeiling;
  const double speed          = ctx.speed;

  for (Token& t : tokens) {
    if (isSilenceOrMissing(t) || !isVowel(t)) continue;
    if (t.prominence < 0.0) continue;  // not set

    // Skip tiedFrom tokens (diphthong offglides) — their short duration IS the glide.
    if (t.tiedFrom) continue;

    // Continuous stress-based duration scaling.
    // Prominence 0.0→1.0 maps linearly to secondaryW→primaryW.
    // Below 0.3 the reducedCeiling path handles reduction instead.
    if (t.prominence >= 0.3) {
      double scale = secondaryW + t.prominence * (primaryW - secondaryW);
      t.durationMs *= scale;
    }

    // Primary stress floor — prevents short monophthongs like /ɒ/ in
    // "box" from sounding clipped. Skips diphthong nuclei (tiedTo) since
    // they already have the offglide adding perceived duration.
    if (t.prominence >= 0.9 && primaryFloorMs > 0.0 && !t.tiedTo) {
      double effectivePFloor = primaryFloorMs / speed;
      t.durationMs = std::max(t.durationMs, effectivePFloor);
    }

    // Safety floor for prominent vowels
    if (t.prominence >= 0.4 && floorMs > 0.0) {
      double effectiveFloor = floorMs / speed;
      t.durationMs = std::max(t.durationMs, effectiveFloor);
    }

    // Non-prominent vowels: apply reduction ceiling
    if (reducedCeil < 1.0 && t.prominence < 0.3) {
      // Scale linearly: prominence 0.0 → full reduction, 0.3 → no reduction
      double blend = t.prominence / 0.3;
      double scale = reducedCeil + blend * (1.0 - reducedCeil);
      t.durationMs *= scale;
    }
  }

  // ── Pass 2b: Syllable-position duration shaping ──
  //
  // Onset consonants get slightly more time (they initiate the gesture),
  // coda consonants get less (they trail off).  Unstressed open syllables
  // (no coda) compress their nucleus — these are the lightest syllables
  // in natural speech rhythm.

  if (lang.syllableDurationEnabled) {
    const double onsetSc = lang.syllableDurationOnsetScale;
    const double codaSc  = lang.syllableDurationCodaScale;
    const double openSc  = lang.syllableDurationUnstressedOpenNucleusScale;

    for (size_t w = 0; w < words.size(); ++w) {
      const size_t wStart = words[w].start;
      const size_t wEnd   = (w + 1 < words.size()) ? words[w + 1].start : tokens.size();

      // Count syllables in this word.
      int maxSyll = -1;
      for (size_t i = wStart; i < wEnd; ++i) {
        if (tokens[i].syllableIndex > maxSyll) maxSyll = tokens[i].syllableIndex;
      }
      if (maxSyll < 1) continue;  // monosyllable or unassigned — skip

      // Process each syllable except the last — word-final syllables are
      // already shaped by wordFinalObstruentScale and phrase-final lengthening.
      // Compressing them further makes final syllables disappear.
      for (int syll = 0; syll < maxSyll; ++syll) {
        // Collect real phoneme indices in this syllable.
        int nucleusIdx = -1;
        bool syllStressed = false;
        bool hasCoda = false;

        // First pass: find nucleus and stress.
        for (size_t i = wStart; i < wEnd; ++i) {
          Token& t = tokens[i];
          if (t.syllableIndex != syll) continue;
          if (isSilenceOrMissing(t)) continue;
          if (t.preStopGap || t.clusterGap || t.vowelHiatusGap ||
              t.postStopAspiration || t.voicedClosure) continue;
          if (t.stress > 0) syllStressed = true;
          if (nucleusIdx < 0 && isVowel(t)) nucleusIdx = static_cast<int>(i);
        }
        if (nucleusIdx < 0) continue;  // no vowel — skip

        // Check for coda consonants (real phonemes after nucleus).
        for (size_t i = static_cast<size_t>(nucleusIdx) + 1; i < wEnd; ++i) {
          Token& t = tokens[i];
          if (t.syllableIndex != syll) break;
          if (isSilenceOrMissing(t)) continue;
          if (t.preStopGap || t.clusterGap || t.vowelHiatusGap ||
              t.postStopAspiration || t.voicedClosure) continue;
          if (!isVowel(t)) { hasCoda = true; break; }
        }

        // Apply scales.
        for (size_t i = wStart; i < wEnd; ++i) {
          Token& t = tokens[i];
          if (t.syllableIndex != syll) continue;
          if (isSilenceOrMissing(t)) continue;
          if (t.preStopGap || t.clusterGap || t.vowelHiatusGap ||
              t.postStopAspiration || t.voicedClosure) continue;

          if (!isVowel(t)) {
            // Consonant: onset or coda?
            if (static_cast<int>(i) < nucleusIdx) {
              t.durationMs *= onsetSc;
            } else if (static_cast<int>(i) > nucleusIdx) {
              t.durationMs *= codaSc;
            }
          } else if (!syllStressed && !hasCoda && !t.tiedFrom) {
            // Unstressed open-syllable vowel.
            t.durationMs *= openSc;
          }

          // Safety clamps.
          if (t.durationMs < 2.0) t.durationMs = 2.0;
          t.fadeMs = std::min(t.fadeMs, t.durationMs);
        }
      }
    }
  }

  // ── Pass 3: Amplitude realization ──
  //
  // Boost is scaled by primaryStressWeight so the weight knob controls
  // how much stressed vowels stand out.  Reduction is NOT scaled by
  // the weight — unstressed vowels get reduced regardless.

  const double boostDb = lang.prominenceAmplitudeBoostDb;
  const double reducDb = lang.prominenceAmplitudeReductionDb;
  const int vaIdx = static_cast<int>(FieldId::voiceAmplitude);

  if (boostDb > 0.0 || reducDb > 0.0) {
    for (Token& t : tokens) {
      if (isSilenceOrMissing(t) || !isVowel(t)) continue;
      if (t.prominence < 0.0) continue;

      double currentAmp = 0.0;
      if (t.setMask & (1ULL << vaIdx)) {
        currentAmp = t.field[vaIdx];
      } else if (t.def) {
        currentAmp = t.def->field[vaIdx];
      }
      if (currentAmp <= 0.0) continue;

      double dbChange = 0.0;
      if (t.prominence >= 0.5 && boostDb > 0.0) {
        // Scale boost by prominence level AND stress weight
        double factor = (t.prominence - 0.5) / 0.5;
        dbChange = boostDb * primaryW * factor;
      } else if (t.prominence < 0.3 && reducDb > 0.0) {
        // Scale reduction by how non-prominent: 0.3 → no reduction, 0.0 → full
        double factor = 1.0 - (t.prominence / 0.3);
        dbChange = -reducDb * factor;
      }

      if (dbChange != 0.0) {
        double linearScale = std::pow(10.0, dbChange / 20.0);
        t.field[vaIdx] = currentAmp * linearScale;
        t.setMask |= (1ULL << vaIdx);
      }
    }
  }

  // ── Sonorant-context amplitude boost ──
  // Unstressed vowels between sonorants get masked by smooth transitions.
  // A small amplitude boost keeps them audible at higher rates.
  const double sonAmpScale = lang.sonorantContextAmplitudeScale;
  if (sonAmpScale > 1.0) {
    for (size_t i = 0; i < tokens.size(); ++i) {
      Token& t = tokens[i];
      if (isSilenceOrMissing(t) || !isVowel(t)) continue;
      if (t.stress != 0) continue;

      bool prevSon = false, nextSon = false;
      for (size_t j = i; j > 0; --j) {
        const Token& p = tokens[j - 1];
        if (isSyntheticGap(p) || p.silence) continue;
        prevSon = isSonorant(p);
        break;
      }
      for (size_t j = i + 1; j < tokens.size(); ++j) {
        const Token& n = tokens[j];
        if (isSyntheticGap(n) || n.silence) continue;
        nextSon = isSonorant(n);
        break;
      }

      if (prevSon && nextSon) {
        double currentAmp = 0.0;
        if (t.setMask & (1ULL << vaIdx)) {
          currentAmp = t.field[vaIdx];
        } else if (t.def) {
          currentAmp = t.def->field[vaIdx];
        }
        if (currentAmp > 0.0) {
          t.field[vaIdx] = currentAmp * sonAmpScale;
          t.setMask |= (1ULL << vaIdx);
        }
      }
    }
  }

  return true;
}

}  // namespace nvsp_frontend::passes
