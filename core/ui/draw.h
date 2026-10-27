// tiny immediate-mode draw helpers on top of citro2d.
// 3ds-only translation unit (core/ui is excluded from the host test build).
// Small text uses one nearest-filter atlas tile per nonempty 6x8 donor glyph.
// The original pixel-run backend remains available if atlas allocation fails.
#pragma once
#include <cstdint>

namespace seance::ui {

uint32_t rgb(uint8_t r, uint8_t g, uint8_t b);

void rect(float x, float y, float w, float h, uint32_t c);
// Snapped pixel edges, inside 1px outline; corners are never painted twice.
void frame(float x, float y, float w, float h, uint32_t c);
// Axis strokes: inclusive pixel endpoints, one integer rectangle; zero length
// draws a dot. Even thickness biases up/left. Diagonals retain one GPU quad.
void line(int x0, int y0, int x1, int y1, uint32_t c, int thickness = 1);
void circle(float x, float y, float radius, uint32_t c, bool filled = true);

// Optional immutable 32 KiB GPU atlas, initialized after C2D setup and before
// startup UI/audio. Failure keeps the original run renderer; no lazy draw-time
// allocation/retry. Ordinary host builds retain that backend and return false.
bool text_init();
// Call only after fencing GPU readers and resetting C2D/C3D texture caches.
void text_shutdown();

// 6x8 monospace, scale = integer pixel multiplier; original metrics/byte mapping.
void text(int x, int y, const char* s, uint32_t c, int scale = 1);
int  text_w(const char* s, int scale = 1);

// Pixel-snapped horizontal slider: track + fill + knob. norm in [0..32767],
// bipolar draws fill from the center notch. purely visual - hit testing
// is the caller's job.
void hslider(float x, float y, float w, float h, int32_t norm, bool bipolar,
             uint32_t track, uint32_t fill);

} // namespace seance::ui
