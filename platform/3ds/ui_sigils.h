#pragma once
#include <3ds.h>
namespace seance::platform::ui3ds {
void sigils_view_touch_down(int, int);
void sigils_view_touch_drag(int, int);
void sigils_view_touch_release();
void sigils_forget_undo(); // successful new/load only; never on failed load or save
void sigils_close_mode_menu(); // cancel disclosure and current gesture
bool sigils_view_buttons(u32 down, u32 held); // true consumes the local frame
bool sigils_visual_init(); // optional fixed blur texture, before audio
void sigils_visual_shutdown(); // after GPU fence/cache reset
void sigils_gpu_frame(); // after SYNCDRAW FrameBegin, before any GPU draw
// Capture once before GPU work; both renderers consume only the bounded frame.
void sigils_capture();
void sigils_view_draw();
void sigils_top_draw();
}
