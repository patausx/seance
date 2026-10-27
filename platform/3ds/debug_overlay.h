#pragma once
#include <cstdint>
struct C3D_RenderTarget_tag;

namespace seance::platform::debug_overlay {
// Explicit diagnostic builds may reserve ZR. Normal builds keep structural undo;
// no settings entry or persistence, and the dormant hooks do no per-frame work.
#ifdef SEANCE_DIAGNOSTIC_OVERLAY
constexpr bool Available = true;
#else
constexpr bool Available = false;
#endif
void init();
void shutdown();
void reset(); // keep visibility, discard startup/suspend timing boundaries
void input(uint32_t& down, uint32_t& held, uint32_t& up);
void before_frame();
void after_begin(); // only after a successful synchronized C3D_FrameBegin
void after_end();   // immediately after C3D_FrameEnd
void draw(C3D_RenderTarget_tag* top); // last pass, cached text only; never clears top
} // namespace seance::platform::debug_overlay
