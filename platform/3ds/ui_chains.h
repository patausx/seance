#pragma once
#include <3ds.h>
#include "ui_workspace.h"
namespace seance::platform::ui3ds {
// Read the active project identity, not the patch-browser cursor.
int project_active_slot();
bool chains_context_is_open();
bool chains_context_buttons(u32 down);
void chains_context_draw_top();
void chains_view_touch_down(int, int);
void chains_view_touch_hold(int, int);
void chains_view_touch_release(bool commit_tap=false);
void chains_close_context_branch();
// Editor X enters the same second-action delete confirmation as more > delete.
// It never removes a unit itself; the next X is inert in the modal branch.
bool chains_confirm_editor_delete();
// One bounded capture before GPU; both LCDs consume identical frozen geometry.
void chains_capture();
// Freeze the preceding selected geometry BEFORE assigning Overlay::Picker.
// The insertion target stays independent; never select/snap Add to get an origin.
void chains_picker_begin();
// Card -> fullscreen editor. Begin before assigning Editor; only a genuine
// visible chain origin is eligible. B calls back AFTER editor_back/subpage logic.
void chains_editor_begin();
bool chains_editor_back();
// Before main's input snapshot. Fresh opening input snaps without eating it;
// closing owns local input and drains held tails. True means skip touch dispatch.
bool chains_editor_input(u32& down, u32& held, u32& up);
// After input, before chains_capture. Validates identities and freezes one phase.
void chains_editor_capture();
// Composite OVER the ordinary native editor, never a screenshot/zoom. Draw-only.
void chains_editor_draw(bool top);
// Early phases are fully opaque: omit hidden module primitives from GPU pool.
bool chains_editor_covers();
void chains_draw_surface(bool top);
bool chains_picker_background();
void chains_view_draw();
void chains_workspace_buttons(u32 down);
}
