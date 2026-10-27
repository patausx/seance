#pragma once
#include <cstdint>
#include "../../core/engine/units.h"

namespace seance::platform::ui3ds {
Unit* edited_unit();
int sample_slot_for_unit(const Unit* unit);
uint32_t sample_slot_frames(int slot);
void set_sample_slot_for_unit(Unit* unit, int slot);
void scan_sample_files();
void sample_browser_touch_down(int tx, int ty);
void sample_browser_touch_hold(int tx, int ty);
void sample_browser_touch_release(bool commit_tap = true);
// Owns browser-local buttons; SELECT/START remain application controls.
bool sample_browser_buttons(uint32_t down);
bool sample_browser_assign_selected_slot();
bool sample_browser_load_selected();
bool sample_browser_back();
void sample_browser_stop_preview();
void sample_browser_tick();
// Capture bounded metadata before GPU work; draw reads only the frozen UI frame.
void sample_browser_capture();
void sample_browser_draw();
void sample_browser_draw_top();
void export_current_sample();
}
