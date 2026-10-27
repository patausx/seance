#pragma once
// Pure, bounded logo geometry. The original English endpoint is pixel exact;
// only contour distance changes between words, never a two-word crossfade.
#include "wordmark_morph_data.h"
#include <cstdint>
#include <cstring>

namespace seance::platform::ui3ds::wordmark_morph {
inline constexpr std::uint32_t kHoldMs = 4000;
inline constexpr std::uint32_t kMorphMs = 8000;
inline constexpr std::uint32_t kWordMs = kHoldMs + kMorphMs;
inline constexpr std::uint32_t kLoopMs = kWordMs * wordmark_morph_data::kWords;
inline constexpr int kOne = 4096;
struct Phase { int from, to, mix; };

inline Phase phase(std::uint32_t ms) {
    ms %= kLoopMs;
    const int from = int(ms / kWordMs);
    const auto local = ms % kWordMs;
    const std::int64_t t = local > kHoldMs ? local - kHoldMs : 0;
    constexpr std::int64_t d = kMorphMs;
    // Smoothstep in Q12, exact endpoints, no floating point or mutable clock.
    const int mix = int((kOne * t * t * (3 * d - 2 * t) + d * d * d / 2) / (d * d * d));
    return {from, (from + 1) % wordmark_morph_data::kWords, mix};
}
inline std::uint8_t coverage(int a, int b, int mix) {
    // Inputs are signed quarter-native-pixel distances. One native pixel of
    // edge coverage; endpoints' minimum |distance|=2 makes their masks binary.
    const int value = a * (kOne - mix) + b * mix;
    constexpr int edge = 2 * kOne;
    if (value <= -edge) return 0;
    if (value >= edge) return 255;
    return std::uint8_t(((value + edge) * 255 + edge) / (2 * edge));
}

template<class Rect>
inline void render(std::uint32_t ms, Rect&& rect) {
    using namespace wordmark_morph_data;
    const auto p = phase(ms);
    // Two native rows only: 768B stack, no heap, texture, cache or frame history.
    std::uint8_t rows[2][kWidth];
    auto* current = rows[0];
    auto* next = rows[1];
    const auto row = [&](int y, std::uint8_t* dst) {
        const auto* a = kDistance[p.from] + y * kWidth;
        const auto* b = kDistance[p.to] + y * kWidth;
        for (int x = 0; x < kWidth; ++x)
            dst[x] = coverage(a[x], b[x], p.mix);
    };
    const auto emit = [&](int y, int height) {
        for (int x = 0; x < kWidth;) {
            const auto alpha = current[x];
            int end = x + 1;
            while (end < kWidth && current[end] == alpha) ++end;
            if (alpha) rect(kX + x, kY + y, end - x, height, alpha);
            x = end;
        }
    };
    row(0, current);
    int first = 0;
    for (int y = 1; y < kHeight; ++y) {
        row(y, next);
        if (std::memcmp(current, next, kWidth) == 0) continue;
        emit(first, y - first);
        auto* swap = current; current = next; next = swap;
        first = y;
    }
    emit(first, kHeight - first);
}
} // namespace seance::platform::ui3ds::wordmark_morph
