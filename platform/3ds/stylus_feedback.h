#pragma once
#include <cstdint>

namespace seance::platform::ui3ds::stylus_feedback {
// One optional 16 KiB texture, allocated before menu/audio. Failure is quiet and
// keeps a small contact fallback; never retry allocation during a gesture.
bool init();
// Caller must fence GPU readers and clear C2D/C3D texture caches first.
void shutdown();
// Input/time/theme sampling is separate from painting and never owns input.
void capture(bool held, int x, int y, uint64_t now_ms, uint32_t accent);
// Call once AFTER a successful FrameBegin(SYNCDRAW), before any scene draws.
void upload();
// Final bottom-only pass. Frozen geometry/color; background response uses the
// current GPU destination, not a framebuffer capture or CPU pixel readback.
void draw();
}
