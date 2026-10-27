#pragma once
#include <3ds.h>

namespace seance::platform::ui3ds {
// Master or single-chain recorder page; neither a chain slot nor a modal editor.
void bounce_enter();
// Cancels touch/modal state and stops only the take audition, not recording.
void bounce_cancel_input();
void bounce_buttons(u32 down, u32 held);
void bounce_touch_down(int x, int y);
void bounce_touch_hold(int x, int y);
void bounce_touch_release(bool commit = true);
// Always capture the tiny recording badge; copy the bounded waveform only when
// this page is visible. All draw entry points consume frozen data exclusively.
void bounce_capture();
void bounce_draw();
void bounce_draw_top();
void bounce_draw_indicator();
// Unsaved held audio routes project replacement back here for save/release.
// Saved immutable takes release their hold safely before the bank transaction.
bool bounce_guard_project_change();
}
