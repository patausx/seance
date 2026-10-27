#pragma once
#include "../../core/engine/unit.h"

namespace seance::platform::ui3ds {
// Capture once after input/selection synchronization and before drawing. Editor
// may be on any workspace, including sequencer lock entry or the parameter lens.
// Invalid context/selection (or selected < 0) clears the previous frame.
void fm_context_capture(int selected);
// Pure rendering of the captured scalar frame. False draws nothing; safe with
// g_eng/g_audio null after capture. No input, engine mutation or audio locking.
bool fm_context_draw_top();
// Shared FM readout mapping for the focused editor/lens: no engine reads.
// Caller supplies the parameter index and scalar value. Includes existing ms
// suffixes for attack/decay; ratio fine is a signed additive five-decimal value.
// Null/zero-size outputs are inert; unknown indices use the normalized scale.
void fm_context_value_text(char* out, unsigned size, int param, q15 raw);
} // namespace seance::platform::ui3ds
