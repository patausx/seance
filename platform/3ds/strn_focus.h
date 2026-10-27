#pragma once
#include <cstddef>
#include <cstdint>
// STRN controller capture happens once before GPU drawing; draw functions use a
// bounded copy only, never Unit/Engine pointers or AudioOut::Guard.
namespace seance::platform::ui3ds {
void stranular_focus_capture();
bool stranular_focus_draw_top();
bool stranular_focus_draw_bottom();
void stranular_focus_cancel();
void stranular_focus_release();
int stranular_focus_selected();
void stranular_focus_select(int param); // UI-only selection for the shared L layer
// Same native units/context as the frozen main surface; never reads the engine.
void stranular_focus_value_text(char* out, size_t size, int param, int16_t value);
}
