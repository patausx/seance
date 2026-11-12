#pragma once
#include <cstddef>
#include "../../core/engine/unit.h"
#include "../../core/engine/musical_scale.h"

namespace seance::platform::ui3ds {
// First workflow batch only. No lower drawing, DSP or persistent parameter data.
bool module_inspector_supports(UnitKind kind);
int module_inspector_selected(UnitKind kind);
void module_inspector_select(UnitKind kind, int param);
// Call after input, before GPU submission. Invalid context clears the old frame.
void module_inspector_capture();
// Module-editor captures run once per frame, in a fixed order. Those whose kind is
// not the visible editor's only find out that it is not theirs; a run of them
// shares ONE AudioOut entry (the lock is recursive, their own guards nest). The
// visible kind's capture keeps its own guard, so its model work stays outside.
// Call order and every engine call are unchanged. UnitKind::Empty: no editor.
struct ModuleCapture { UnitKind kind; void (*capture)(); };
UnitKind module_capture_visible_kind();
void module_capture_runs(const ModuleCapture* list, size_t count);
// Draws only the captured copy; safe with engine/audio pointers detached.
bool module_inspector_draw_top();
// Pure scalar mappings, shared by the inspector and modulation readouts.
// PLAY callers pass their captured mode/qnt flags; formatting never reads a unit.
void module_inspector_value_text(char* out, size_t size, UnitKind kind, int param, q15 value,
                                 bool flexible_vca = true, bool modern_delay = false,
                                 bool modern_tilt = true, bool legacy_tilt_zero = false,
                                 bool modern_play = true, bool quantize_play = false, MusicalScale scale = {});
}
