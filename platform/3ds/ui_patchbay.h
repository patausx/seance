#pragma once
#include <3ds.h>
namespace seance::platform::ui3ds {
void patchbay_enter();
void patchbay_enter_sigil(int sigil); // reveal a-h from the universal editor
void patchbay_forget(); // successful new/load: same Engine address may now hold a new project
void patchbay_cancel_input();
void patchbay_buttons(u32 down, u32 held);
void patchbay_touch_down(int x, int y);
void patchbay_touch_hold(int x, int y);
void patchbay_touch_release();
bool patchbay_sigil_back(); // consumes B only after opening an existing sigil from a source
void patchbay_capture(); // once before GPU, no engine access in either draw
void patchbay_draw();
void patchbay_draw_top();
}
