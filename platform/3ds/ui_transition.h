#pragma once
#include <3ds.h>
namespace seance::platform::ui3ds::transition {
// Reserve 0/100 for exact endpoints, consistently on both screens.
inline int position_percent(int position) {
    if (position <= 0) return 0;
    if (position >= 32767) return 100;
    const int rounded = (position * 100 + 16383) / 32767;
    return rounded < 1 ? 1 : rounded > 99 ? 99 : rounded;
}
bool active();
void open();
void close();
void reset();
// Successful project replacement only; navigation/reset keeps a queued launch.
void forget_launch_tracking();
void touch_down(int x, int y);
void touch_drag(int x, int y);
void touch_release(bool commit);
void buttons(u32 down, u32 held, u32 up);
void tick();
void draw();
}
