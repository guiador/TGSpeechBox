/*
TGSpeechBox — Diphthong collapse pass.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#include "diphthong_collapse.h"

#include <algorithm>

namespace nvsp_frontend::passes {

namespace {

static inline bool tokIsVowel(const Token& t) {
  return t.def && ((t.def->flags & kIsVowel) != 0);
}

static inline bool tokIsVowelOrSemivowel(const Token& t) {
  return t.def && ((t.def->flags & (kIsVowel | kIsSemivowel)) != 0);
}

} // namespace

bool runDiphthongCollapse(
  PassContext& ctx,
  std::vector<Token>& tokens,
  std::string& outError
) {
  (void)outError;

  const auto& lp = ctx.pack.lang;
  if (!lp.diphthongCollapseEnabled) return true;

  const int cf1 = static_cast<int>(FieldId::cf1);
  const int cf2 = static_cast<int>(FieldId::cf2);
  const int cf3 = static_cast<int>(FieldId::cf3);
  const int cb1 = static_cast<int>(FieldId::cb1);
  const int cb2 = static_cast<int>(FieldId::cb2);
  const int cb3 = static_cast<int>(FieldId::cb3);
  const int pb1 = static_cast<int>(FieldId::pb1);
  const int pb2 = static_cast<int>(FieldId::pb2);
  const int pb3 = static_cast<int>(FieldId::pb3);
  const int pf1 = static_cast<int>(FieldId::pf1);
  const int pf2 = static_cast<int>(FieldId::pf2);
  const int pf3 = static_cast<int>(FieldId::pf3);
  const int vp  = static_cast<int>(FieldId::voicePitch);
  const int evp = static_cast<int>(FieldId::endVoicePitch);

  // Scan for tied vowel pairs: A.tiedTo && B.tiedFrom.
  // Onset (A) must be a vowel; offglide (B) can be vowel OR semivowel.
  // Semivowel offglides arise from pack replacements (e.g. Spanish ɪ→ɪ_es
  // where ɪ_es has _isSemivowel: true) and must still collapse for smooth
  // micro-frame glide emission instead of relying on crossfade alone.
  // Iterate by index (not iterator) because we erase token B in place.
  for (size_t i = 0; i + 1 < tokens.size(); /* advanced inside */) {
    Token& a = tokens[i];
    Token& b = tokens[i + 1];

    if (!a.tiedTo || !b.tiedFrom || !tokIsVowel(a) || !tokIsVowelOrSemivowel(b)) {
      ++i;
      continue;
    }

    // === Merge B into A ===

    // Duration: combined, scaled, with floor to ensure enough micro-frames for the glide.
    a.durationMs += b.durationMs;
    if (lp.diphthongDurationScale > 0.0 && lp.diphthongDurationScale != 1.0)
      a.durationMs *= lp.diphthongDurationScale;
    if (a.durationMs < lp.diphthongDurationFloorMs)
      a.durationMs = lp.diphthongDurationFloorMs;

    // Start formants: already in A's field[] (cf1/2/3, pf1/2/3).
    // End formants: take from B's field[] (what B's steady-state would be).
    // Use token-level field values when set, fall back to PhonemeDef.
    auto getField = [](const Token& t, int fid) -> double {
      if ((t.setMask & (1ull << fid)) != 0) return t.field[fid];
      if (t.def) {
        return t.def->field[fid];
      }
      return 0.0;
    };

    a.hasEndCf1 = true;  a.endCf1 = getField(b, cf1);
    a.hasEndCf2 = true;  a.endCf2 = getField(b, cf2);
    a.hasEndCf3 = true;  a.endCf3 = getField(b, cf3);

    // Parallel end targets: use B's parallel formants.
    // These will fall back to endCf in frame_emit if not explicitly set
    // on Token, but setting them here future-proofs for nasal diphthongs.
    a.hasEndPf1 = true;  a.endPf1 = getField(b, pf1);
    a.hasEndPf2 = true;  a.endPf2 = getField(b, pf2);
    a.hasEndPf3 = true;  a.endPf3 = getField(b, pf3);

    // End bandwidths: take B's cb1/2/3 so micro-frames can interpolate
    // bandwidths alongside frequencies.  Without this, onset bandwidths
    // are held constant — producing smeared, wobbly offsets.
    a.hasEndCb1 = true;  a.endCb1 = getField(b, cb1);
    a.hasEndCb2 = true;  a.endCb2 = getField(b, cb2);
    a.hasEndCb3 = true;  a.endCb3 = getField(b, cb3);
    a.hasEndPb1 = true;  a.endPb1 = getField(b, pb1);
    a.hasEndPb2 = true;  a.endPb2 = getField(b, pb2);
    a.hasEndPb3 = true;  a.endPb3 = getField(b, pb3);

    // Pitch: onset from A, offset from B.
    // A's voicePitch stays as-is.  Set endVoicePitch to B's pitch.
    double bPitch = getField(b, vp);
    if (bPitch > 0.0) {
      a.field[evp] = bPitch;
      a.setMask |= (1ull << evp);
    }

    // Flag it
    a.isDiphthongGlide = true;

    // Inherit A's syllableIndex, stress, wordStart, syllableStart (already there).
    // fadeMs from A (entry fade into the diphthong).
    // Clear tied flags — this is now a single merged token.
    a.tiedTo = false;
    a.tiedFrom = false;

    // Erase token B
    tokens.erase(tokens.begin() + static_cast<ptrdiff_t>(i + 1));

    // Do NOT double-merge triphthongs.
    // After collapsing [A,B] -> [AB], advance past the merged token.
    // If there was a triphthong [A,B,C] with A.tiedTo, B.tiedTo+tiedFrom,
    // C.tiedFrom, the first merge creates [AB,C].  AB has tiedTo=false,
    // so the next iteration won't merge AB+C.  Correct.
    ++i;
  }

  return true;
}

} // namespace nvsp_frontend::passes
