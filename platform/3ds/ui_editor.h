#pragma once
#include "../../core/engine/units.h"

namespace seance::platform::ui3ds {
Unit* edited_unit();
bool editor_back();
void editor_touch_down(int tx, int ty);
void editor_touch_drag(int tx, int ty);
void editor_touch_release();
// Navigation/transition cancellation, never a real stylus lift/button commit.
void editor_cancel_input();
void editor_draw();
void editor_ui_tick();
// Existing main audio guard held; acknowledges only the actual addressed request.
void pent_focus_chain_trigger_requested(int chain);
// Freeze passive button feedback before GPU submission; hidden/modal views cancel it.
void editor_capture_button_motion(bool visible);
}
