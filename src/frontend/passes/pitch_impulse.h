/*
TGSpeechBox — Impulse pitch model pass interface.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#ifndef TGSB_PASS_PITCH_IMPULSE_H
#define TGSB_PASS_PITCH_IMPULSE_H

#include <vector>
#include "../ipa_engine.h"

namespace nvsp_frontend {

// Impulse-style pitch contour pass.
//
// Multi-layer additive pitch model: proportional declination ramp +
// hat-pattern rise/fall around stressed words + count-based stress
// peaks + terminal gestures.  Single-pass IIR smoothing preserves
// contour shape.
void applyPitchImpulse(
  std::vector<Token>& tokens,
  const PackSet& pack,
  double speed,
  double basePitch,
  double inflection,
  char clauseType
);

} // namespace nvsp_frontend

#endif // TGSB_PASS_PITCH_IMPULSE_H
