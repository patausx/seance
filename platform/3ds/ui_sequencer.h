#pragma once
namespace seance { struct EngineUiSnapshot; }
namespace seance::platform::ui3ds {
void sequencer_view_enter();
// PLAY shortcut and local cuts-panel dismissal; neither writes a step/base value.
void sequencer_open_slices(int chain);
bool sequencer_view_back();
// Modal/navigation cleanup: cancel both pending touch edits and R gestures.
void sequencer_view_cancel_input();
void sequencer_view_touch_down(int tx, int ty);
void sequencer_view_touch_hold(int tx, int ty);
// commit_taps: true only from the real touch-lift path; cleanup callers keep
// false so a pending step tap is cancelled, not committed.
void sequencer_view_touch_release(bool commit_taps = false);
void sequencer_view_buttons(unsigned int down, unsigned int held, unsigned int up);
void sequencer_view_draw();
// Read-only controller context for the top-screen sequencer microscope.
int sequencer_selected_chain();
int sequencer_selected_step();
int sequencer_selected_layer();
const char *sequencer_selected_layer_name();
// Single score context consumed by the aligned 400px upper display.
int sequencer_current_page();
// Frozen edit-bank projection, separate from the audio worker's playing snapshot.
const EngineUiSnapshot& sequencer_snapshot();
int sequencer_edit_bank();
void sequencer_selected_pitch_text(char *out, int capacity);
// Invalidates volatile PIN/RECALL buffers after project replacement.
void sequencer_forget_pins();
} // namespace seance::platform::ui3ds
