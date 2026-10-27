#pragma once
#include <3ds.h>
namespace seance::platform::ui3ds {
// Shared module action; implemented with the parameter lens controller. Draw is
// frozen, and navigation cancellation is not a real lift/commit.
bool module_rand_touch_down(int x, int y);
bool module_rand_touch_drag(int x, int y);
bool module_rand_touch_release();
void module_rand_cancel();
void module_rand_draw();
void module_rand_poll(); // UI thread, including while another view is open
void module_rand_rebase(); // after an atomic project swap, never dirties the new project
}
