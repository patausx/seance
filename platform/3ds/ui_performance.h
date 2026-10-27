#pragma once
#include <3ds.h>
namespace seance::platform::ui3ds {
void performance_view_touch_down(int tx, int ty);
void performance_view_touch_hold(int tx, int ty);
void performance_view_touch_release(bool commit_tap = false);
void performance_view_touch_drag(int tx, int ty);
void performance_view_buttons(u32 down, u32 held, u32 up);
void performance_view_leave();
void performance_release_all();
void performance_view_reset();
void performance_view_draw();
void performance_view_draw_top();
// Capture motion once after the UI snapshot, before GPU. No engine/audio access.
void performance_view_capture();
void performance_view_tick();
}
