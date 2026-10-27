#pragma once
#include <cstddef>
#include "../../core/engine/unit.h"

namespace seance::platform::ui3ds {
// First workflow batch only. No lower drawing, DSP or persistent parameter data.
bool module_inspector_supports(UnitKind kind);
int module_inspector_selected(UnitKind kind);
void module_inspector_select(UnitKind kind, int param);
// Call after input, before GPU submission. Invalid context clears the old frame.
void module_inspector_capture();
// Draws only the captured copy; safe with engine/audio pointers detached.
bool module_inspector_draw_top();
// Pure scalar mappings, shared by the inspector and modulation readouts.
// PLAY callers pass their captured mode/qnt flags; formatting never reads a unit.
void module_inspector_value_text(char* out, size_t size, UnitKind kind, int param, q15 value,
                                 bool flexible_vca = true, bool modern_delay = false,
                                 bool modern_tilt = true, bool legacy_tilt_zero = false,
                                 bool modern_play = true, bool quantize_play = false);
}
