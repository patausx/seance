#pragma once
#include <3ds.h>

namespace seance::platform::ui3ds {
// Focused-card modulation layer; a separate controller DTO, not the audio snapshot.
bool param_lens_is_open();
bool param_lens_input(u32& down, u32 held, u32 up, bool blocked);
void param_lens_cancel();
void param_lens_capture();
bool param_lens_touch_down(int x, int y);
bool param_lens_touch_drag(int x, int y);
void param_lens_touch_release();
void param_lens_draw_bottom(); // overlay only, after the ordinary frozen module body
void param_lens_draw_entry();
bool param_lens_draw_top(); // delegates to the ordinary frozen module inspector
}
