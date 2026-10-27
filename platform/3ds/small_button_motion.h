#pragma once
#include <cstdint>
#include "../../core/ui/draw.h"
#include "ui_palette.h"

namespace seance::platform::ui3ds::small_press {
// One passive, UI-owned pulse. Never routes input, changes hitboxes or reads a
// clock during drawing. A touch must START on the control, not drag into it.
struct Pulse {
    const void* owner = nullptr;
    uint64_t context = 0, began = 0;
    int x = -1, y = -1;
    bool active = false;
};
struct Frame { int x = -1, y = -1; unsigned weight = 0; int dy = 0; };
inline Pulse pulse;
inline Frame frame;
inline void begin(int x, int y, uint64_t now, const void* owner, uint64_t context) {
    pulse = {owner, context, now, x, y, true};
}
inline void cancel() { pulse = {}; frame = {}; }
inline void drag(int x, int y) {
    const int dx = x - pulse.x, dy = y - pulse.y;
    if (dx * dx + dy * dy > 36) cancel();
}
inline void capture(uint64_t now, const void* owner, uint64_t context, bool visible) {
    frame = {};
    if (!pulse.active) return;
    if (!visible || owner != pulse.owner || context != pulse.context ||
        now < pulse.began || now - pulse.began >= 160) { cancel(); return; }
    const unsigned age = unsigned(now - pulse.began);
    const unsigned left = age < 60 ? 100 : 160 - age;
    frame = {pulse.x, pulse.y, 255u * left * left / 10000u, age < 60 ? 1 : 0};
}
inline Frame pose(int x, int y, int w, int h, bool enabled = true) {
    if (!enabled || frame.x < x || frame.x >= x + w || frame.y < y || frame.y >= y + h)
        return {};
    return frame;
}
inline uint32_t mix(uint32_t a, uint32_t b, unsigned amount) {
    uint32_t out = a & 0xff000000u;
    for (unsigned shift = 0; shift < 24; shift += 8)
        out |= ((((a >> shift) & 255u) * (255 - amount) +
                 ((b >> shift) & 255u) * amount + 127) / 255) << shift;
    return out;
}
inline uint32_t tone(uint32_t color, unsigned weight) {
    return mix(color, color == palette::spot() ? palette::ink() : palette::spot(), weight / 3);
}
// Text-only controls retain their paper layout, alignment and original hit area.
inline void text(int bx, int by, int bw, int bh, int x, int y,
                 const char* label, uint32_t color, bool enabled = true) {
    const auto p = pose(bx, by, bw, bh, enabled);
    ui::text(x, y + p.dy, label, tone(color, p.weight));
}
} // namespace seance::platform::ui3ds::small_press
