#pragma once
#include <cstdint>

namespace seance::platform::ui3ds {
// Relic is retired. Existing lifecycle/capture call sites intentionally remain
// safe and allocation-free; there is no editor, material cache or GPU resource.
inline bool relic_material_init() { return false; }
inline void relic_material_upload() {}
inline void relic_material_shutdown() {}
inline void relic_focus_capture() {}
inline void relic_focus_release() {}
inline void relic_focus_input(uint32_t, uint32_t, bool) {}
inline bool relic_focus_draw_top() { return false; }
} // namespace seance::platform::ui3ds
