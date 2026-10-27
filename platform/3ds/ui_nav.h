#pragma once
// Modal SELECT navigation menu: jump to any view/overlay from anywhere.
// Fixes the "Performance view traps navigation" dead-end.
#include <3ds.h>
namespace seance::platform::ui3ds {
enum class NavChoice : u8 { None, Chains, Sequencer, Performance, Sigils, Patch, Exit, Bounce, Patchbay };
// Theme selection lives only in startup settings; menu Y is inert outside clock focus.
bool nav_is_open();
void nav_open();
void nav_close();
// Choices never mutate g_view: main owns transition/entry and held-touch tails.
// Call buttons each modal frame BEFORE down/hold/lift dispatch. Except ZL, a
// button edge cancels the current clock draft and owns simultaneous touch.
// Menu choices stay immediate. Clock holds preview; lift applies once. X selects
// tempo/swing; focused dpad +/-1, +/-5; A/B returns to menu; SELECT closes.
void nav_buttons(u32 down, NavChoice& out);
void nav_touch_down(int tx, int ty, NavChoice& out);
void nav_touch_hold(int tx, int ty);
void nav_touch_release();
// Guarded <=96-byte clock copy plus published master scalars, before GPU drawing.
// Ferrofluid updates are bounded/20Hz; draw has no engine/lock/PCM access.
// Y from clock focus opens source/depth detail; X/A there selects base/source/depth.
// nav_close cancels a draft; also call it after a successful new/load transaction.
void nav_capture();
// After successful SYNCDRAW begin, BEFORE sigils_gpu_frame/cache upload or draws:
// freeze the preceding completed LCD pair once per open in the shared blur pair.
void nav_gpu_frame();
// BEFORE FrameEnd: remember the output-buffer addresses for the next fence.
void nav_remember_outputs();
void nav_draw();
// Exact pointer-free main-menu frame retained when a choice closes GO TO.
// Transition drawing never reopens input or reads engine/audio state.
void nav_draw_transition();
void nav_transition_clear();
// Historical entry-point names retained: GO TO owns both LCDs. Default upper is
// pixel ferrofluid over the frozen blur; clock focus temporarily opens the
// existing applied/base/modulation inspector on its normal paper.
bool nav_clock_top_active();
void nav_clock_draw_top();
}
