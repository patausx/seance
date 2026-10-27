#pragma once
// Shared post-splash menu language. Pure UI helpers; no font engine or engine I/O.
#include <cstdint>
#include "page_layout.h"
#include "../../core/engine/unit.h"

namespace seance::platform::ui3ds::menu_style {
uint32_t ink();
uint32_t spot();
uint32_t ghost();
uint32_t branch();
// One optional 16 KiB texture; initialize before audio starts. On failure the
// original static paper remains available. Shutdown after fencing GPU readers.
bool ambient_init();
void ambient_shutdown();
// Call once AFTER successful synchronized FrameBegin, BEFORE any GPU draws.
// Freezes a shared absolute-time surface/title clock for both LCDs and all crops.
// palette::capture must already have frozen colors BEFORE UI DTO capture. This
// call uploads that palette's revision even within the normal 100ms motion bucket;
// it does not advance palette time or reset title entry animation on theme change.
void ambient_frame(uint64_t now_ms);
bool ambient_active();
// Shared paper lifetime: call once per frame, AFTER palette capture, outside
// audio guards and BEFORE GPU FrameBegin. Patch enters/leaves with a 240ms
// interruptible strength fade. Inactive pages end on the exact static paper.
// Modal opening freezes both the preceding visible phase and fade level; no
// inactive/modal catch-up. Returning resumes the same field, never a new seed.
// One 16 KiB GPU texture + 5120 bytes CPU grid/LUT; no frame allocations. Field
// work at most 10Hz; the short fade recolors/uploads without redoing its noise.
void patch_capture(bool enabled, bool frozen, uint64_t now_ms);
// Startup home: same shadow depth as Patch on light paper, quieter 40% on dark;
// 30s cycle instead of patch's 18s. Other pages fade out; same capture/upload order.
// frozen keeps the last visible phase/strength through a startup palette modal.
void startup_capture(bool home, uint64_t now_ms, bool frozen = false);
// Sample captured paper for a frozen blur cache. Coordinates are native pixel
// edges: a half-resolution pixel center is x*2+1,y*2+1. top=false crops +40,+256
// from the same 400x496 sheet. Follows the bounded fallback if no texture exists.
uint32_t patch_sample(bool top, float x, float y);
// Restore exact screen-space paper, including the captured strength fade.
// width is a native screen width (320/400) and establishes the LCD for later
// paper_rect calls; no drawing for an empty range. A visible field uses the
// shared 400x496 sheet and <=240/192 coarse quads if GPU storage is unavailable.
void paper(int width, int y0 = 0, int y1 = 240);
// Opaque restoration or alpha veil over the SAME frozen surface, not a flat box.
void paper_rect(int x, int y, int width, int height, int alpha = 255);
// Legacy base/inverse color only. To erase geometry use paper_rect, not this.
uint32_t paper_color(int y);
// Exact whole-label baked masks. Unknown text falls back to ordinary 6x8;
// heading_width reports that same fallback width. Null labels are inert.
void heading(int x, int y, const char* text, uint32_t color);
int heading_width(const char* text);
// Fixed page frame/baseline; directly reuses the existing 28ppem label mask.
void page_heading(page_layout::Title title, uint32_t color);
void marker(int x, int y, uint32_t color);
void footer(int width, const char* left, const char* right);
const char* module_name(UnitKind kind);
const char* module_description(UnitKind kind);
} // namespace seance::platform::ui3ds::menu_style
