#include "sigil_transform.h"

namespace seance {

namespace {
trackr::fx::q15 clamp_q15(int32_t value) {
    if (value < -32767) return -32767;
    if (value > 32767) return 32767;
    return (trackr::fx::q15)value;
}
}

uint32_t jitter_sigil_curve(trackr::fx::q15 points[SIGIL_POINTS], uint32_t state,
                             trackr::fx::q15 amount) {
    if (!points) return state;
    int32_t strength = amount;
    if (strength < 0) strength = -strength;
    if (strength > 8192) strength = 8192;
    if (strength == 0) return state;

    constexpr int SPAN = 8;
    int32_t previous = 0;
    state = state * 1664525u + 1013904223u;
    int32_t next = ((int32_t)(int16_t)(state >> 16) * strength) >> 15;
    for (int base = 0; base < SIGIL_POINTS; base += SPAN) {
        previous = next;
        state = state * 1664525u + 1013904223u;
        next = ((int32_t)(int16_t)(state >> 16) * strength) >> 15;
        for (int offset = 0; offset < SPAN && base + offset < SIGIL_POINTS; ++offset) {
            const int32_t delta = previous + ((next - previous) * offset) / SPAN;
            points[base + offset] = clamp_q15((int32_t)points[base + offset] + delta);
        }
    }
    return state;
}

} // namespace seance
