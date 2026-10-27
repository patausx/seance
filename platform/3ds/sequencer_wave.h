#pragma once
#include <cstdint>
namespace seance::platform::ui3ds::seq_wave {
// Real published master-left scope, oldest to newest. Fixed +/-1 FS; no AGC,
// generated contour or audio processing. Only its ink responds to the observed
// peak of the same decimated ~50ms window, not a clipping/loudness measurement.
constexpr int CENTER_Y = 22, AMPLITUDE = 17;
constexpr uint8_t project(int32_t sample) {
    return static_cast<uint8_t>(CENTER_Y - sample * AMPLITUDE / 32767);
}
constexpr uint8_t accent_amount(uint32_t peak) {
    return static_cast<uint8_t>(((peak > 32767u ? 32767u : peak) * 255u + 16383u) / 32767u);
}
constexpr uint32_t ink(uint32_t base, uint32_t accent, uint8_t amount) {
    uint32_t out = base & 0xff000000u;
    for (unsigned shift = 0; shift < 24; shift += 8) {
        const unsigned a = (base >> shift) & 255u, b = (accent >> shift) & 255u;
        out |= ((a * (255u - amount) + b * amount + 127u) / 255u) << shift;
    }
    return out;
}
}
