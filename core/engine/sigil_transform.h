#pragma once
#include "sigil.h"
#include <cstdint>

namespace seance {

// Bounded deterministic mutation for a drawn curve. Noise anchors are spaced
// across the table and interpolated, so the result wanders instead of adding
// harsh point-to-point white noise. Returns the advanced RNG state.
uint32_t jitter_sigil_curve(trackr::fx::q15 points[SIGIL_POINTS], uint32_t state,
                             trackr::fx::q15 amount);

} // namespace seance
