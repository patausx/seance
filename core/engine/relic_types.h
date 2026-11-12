#pragma once
// Shared bounded relic observations. No experiment headers, PCM ownership or HID.
#include "../audio/fixed.h"
#include <cstddef>
#include <cstdint>

namespace seance::relic {
using q15 = trackr::fx::q15;
constexpr uint32_t SAMPLE_RATE = 32000;
constexpr uint32_t MECHANICS_FRAMES = 320;
enum class Port : uint8_t { Pull = 0, Compress, Bend, Wave, Count };
struct Features {
    float pull = 0, compress = 0, bend = 0, wave = 0;
};
// Immutable borrowed source view. The caller retires readers before swapping PCM.
// first/end delimit the playable region in frames, end exclusive. Missing is a
// valid silent binding, never permission to borrow a previous project's sample.
struct Source {
    const q15* data = nullptr;
    std::size_t elements = 0;
    uint32_t frames = 0, first = 0, end = 0, generation = 0;
    uint8_t channels = 0;
};
// Material coordinates are Q7 native pixels (1/128 px), not screen coordinates.
struct Vertex { int16_t x = 0, y = 0, z = 0; };
constexpr int SURFACE_SIDES = 32, SURFACE_RINGS = 6;
constexpr int SURFACE_TOP = 1 + SURFACE_SIDES * SURFACE_RINGS;
constexpr int SURFACE_VERTICES = SURFACE_TOP + SURFACE_SIDES;
constexpr int SURFACE_FACES = SURFACE_SIDES * (2 * SURFACE_RINGS + 1);
struct Face { uint16_t a = 0, b = 0, c = 0; };
struct Surface {
    Vertex vertices[SURFACE_VERTICES] = {};
    Features features;
    uint64_t sample_clock = 0;
    uint32_t revision = 0, touch_token = 0;
    uint16_t face = 0;
    uint16_t bary[3] = {};
    bool hold = false, held = false;
};
static_assert(SURFACE_VERTICES == 225 && SURFACE_FACES == 416, "bounded relic surface");
static_assert(sizeof(Surface) <= 1440, "pointer-free focused relic shape budget");
} // namespace seance::relic
