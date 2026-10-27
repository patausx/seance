#pragma once
#include "step_focus_motion.h"

namespace seance::platform::ui3ds::step_feedback {
// Playback 03 matches the study: 30ms attack, quadratic release, 18 -> 16 face.
inline float hit_power(unsigned age, unsigned duration) {
    if (!duration || age >= duration) return 0;
    const float attack = duration * .3f < 30 ? duration * .3f : 30;
    if (age < attack) return age / attack;
    const float remaining = 1 - (age - attack) / (duration - attack);
    return remaining * remaining;
}
constexpr unsigned PRESS_MS = 110;
inline float press_power(unsigned age) {
    if (age >= PRESS_MS) return 0;
    const float remaining = 1 - float(age) / PRESS_MS;
    return remaining * remaining;
}
// Retrigger at the new value immediately; no queued animation or deformation
// accumulation. One latest edited cell, independent of focus movement/audio.
struct Press {
    uint64_t began = 0, last = 0;
    int context = -1, row = -1, step = -1;
    void reset() { context = row = step = -1; }
    void fire(uint64_t now, int key, int chain, int column) {
        began = last = now; context = key; row = chain; step = column;
    }
    void update(uint64_t now, int key) {
        if (key != context || now < last || now - last > 200 || now < began || now - began >= PRESS_MS) reset();
        last = now;
    }
    float power(uint64_t now, int chain, int column) const {
        if (context < 0 || chain != row || column != step || now < began) return 0;
        const uint64_t age = now - began;
        return age < PRESS_MS ? press_power(unsigned(age)) : 0;
    }
};
// Both effects act on the backing only. Combine by maximum, never additive
// shrink. The independent inverse mask keeps native selected glyph colors fixed.
inline step_focus::Box face(step_focus::Box box, float hit, float press) {
    const int inset = hit >= .16f || press >= .16f ? 1 : 0;
    return {box.x + inset, box.y + inset, box.w - 2 * inset, box.h - 2 * inset};
}
inline uint32_t tint(uint32_t base, uint32_t accent, float power) {
    if (power <= 0 || base == accent) return base;
    const float alpha = power * .28f;
    uint32_t out = base & 0xff000000u;
    for (unsigned shift = 0; shift < 24; shift += 8) {
        const int a = (base >> shift) & 255, b = (accent >> shift) & 255;
        out |= uint32_t(std::floor(a + (b - a) * alpha + .5f)) << shift;
    }
    return out;
}
static_assert(sizeof(Press) <= 32, "bounded scalar edit feedback");
} // namespace seance::platform::ui3ds::step_feedback
