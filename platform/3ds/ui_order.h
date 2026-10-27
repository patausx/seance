#pragma once
#include <3ds.h>
namespace seance::platform::ui3ds::order {
bool active();
void open();
void close();
void reset();
void tick();
void draw();
void draw_top();
void buttons(u32 down,u32 held,u32 up);
void touch_down(int x,int y);
void touch_hold(int x,int y);
void touch_drag(int x,int y);
void touch_release(bool commit);
}
