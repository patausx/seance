#pragma once
// Fullscreen module material, explicitly selected by editor renderers only.
// No overlay/view globals, controller state, model access or font changes.
#include "menu_style.h"
#include "module_footer_layout.h"

namespace seance::platform::ui3ds::editor_style {
// One optional 64x64 RGBA8 texture (16 KiB), plus 5120 bytes of CPU grid/LUT.
// Call init before audio starts; phase-zero noise is computed only here.
// Failure retains the bounded, globally anchored 20px software material.
bool init();
void shutdown(); // only after the last GPU reader has been fenced
// After palette::capture and before audio-guarded DTO capture. Recolors the LUT
// only when the frozen palette revision changes; no noise/heap/GPU operations.
void capture();
// Only after successful synchronized FrameBegin, before any GPU draws. Uploads
// only changed colors. There are no allocations/noise/uploads in draw helpers.
void frame();
bool active(); // optional GPU texture is ready and contains the captured revision
// 68% main active-tile fill +32% phase-zero patch smoke in byte-space RGB.
// Native LCD selector for subsequent restoration; 400 top, 320 bottom. Both use
// the same phase-zero 400x496 sheet; bottom is the exact +40,+256 crop.
void paper(int width, int y0 = 0, int y1 = 240);
void paper_rect(int x, int y, int width, int height, int alpha = 255);
// Pixel-edge sample of the same surface, including the GPU-failure tile recipe.
uint32_t sample(bool top, float x, float y);
// Base/inverse color compatibility, NOT spatial erasure. Restore with paper_rect.
uint32_t paper_color(int y);

// Unchanged foreground roles, bitmap masks and original footer geometry.
using menu_style::ink;
using menu_style::spot;
using menu_style::ghost;
using menu_style::branch;
// Same baked bitmap mask/fallback, explicitly static: editor labels cannot
// enroll in menu title-entry animation or mutate menu screen/visit state.
void heading(int x, int y, const char* text, uint32_t color);
using menu_style::heading_width;
using menu_style::marker;
// Editor chrome is independent of the menu's wider 20px page insets.
using module_frame_layout::kLeft;
using module_frame_layout::kHeaderTextY;
using module_frame_layout::kHeaderMarkY;
using module_frame_layout::right_edge;
using module_footer_layout::kTextY;
// Bounded 6x8 captions; long text ellipsizes, never grows the frame or font.
void caption(int x, int y, int max_width, const char* text, uint32_t color);
void header_text(int x, int right, const char* text, uint32_t color);
void footer(int width, const char* left, const char* right);
using menu_style::module_name;
using menu_style::module_description;
} // namespace seance::platform::ui3ds::editor_style
