#pragma once
#include <3ds.h>
#include "../../core/engine/project_library.h"
#include "theme_config.h"

namespace seance::platform::ui3ds {

constexpr int kStartRecentCount = 5;
struct StartProject {
    bool exists = false, valid = false;
    char name[PROJECT_NAME_BYTES] = {};
    char age[16] = {}; // known last-open age; "--" if absent/untrusted, never saved mtime
};
struct StartMenuData {
    StartProject projects[PROJECT_SLOTS];
    int active_slot = -1; // marker/initial browser page only; selection never changes it
    int recent_slots[kStartRecentCount] = {-1, -1, -1, -1, -1};
    int recent_count = 0;
    bool show_menu = true, menu_sound = true; // wordmark motion is always enabled
    theme_config::Settings theme;
};
enum class StartMenuAction { None, NewProject, LoadProject, PreferencesChanged, Exit };
struct StartMenuResult {
    StartMenuAction action = StartMenuAction::None;
    int slot = -1;
};

// Copies bounded metadata only. No engine pointers, filesystem, audio, or locks.
// Open starts home with the right panel closed, keyboard focus on Library.
// Quarantines existing held inputs
// until a neutral frame (ZL screenshots exempt). Call input on every frame;
// tx/ty need only be valid while KEY_TOUCH is held. Lift ignores coordinates.
void start_menu_open(const StartMenuData& data);
// Refresh without closing/repositioning a failed-load page; cancels pending touch.
// Strings are sanitized and recent slots deduplicated/filtered to valid projects.
// A palette-modal refresh cancels its draft and returns to the settings panel.
void start_menu_set_data(const StartMenuData& data);
void start_menu_error(const char* text);
// Theme edits are draft-only. Save emits PreferencesChanged once iff changed;
// B/cancel and unchanged Save return to settings without a persistence action.
StartMenuResult start_menu_input(u32 down, u32 held, int tx, int ty);
const StartMenuData& start_menu_data();
// Advance visual time once per displayed frame, never from either draw call.
// Overflow-safe phases; fresh input settles boot reveal immediately. Lower ink
// is native-mask-only and freezes during browser/palette/closing modal visits.
// Library/settings reveal retains visible title/caption fields on interruption;
// right-side touch waits for visibility, explicit keyboard entry settles it.
void start_menu_tick(u32 elapsed_ms);
// Capture the shared paper once before FrameBegin; panel switches keep its phase.
// Palette freezes the paper without catch-up. Draw never advances time.
void start_menu_capture(uint64_t now_ms);
void start_menu_draw_top();
void start_menu_draw_bottom();
// Composite last, after all screen content (including the lower stylus ring).
// Native width 320 or 400; pure one-rect black veil, omitted once fully revealed.
void start_menu_draw_reveal(int width);

} // namespace seance::platform::ui3ds
