#pragma once
#include <3ds.h>
#include "../../core/engine/units.h"

namespace seance::platform::ui3ds {
// Capture guarded destination/real chain metadata and pre-open geometry. Call
// BEFORE assigning Overlay::Picker. Session filter/scroll/stable-kind focus stay.
// Visible diagram/caption numbers are chain-order ordinals; commit and stale
// validation retain the separate physical slot identity, including sparse chains.
void picker_open();
// Cancel only; never place a module or close the overlay. Use on navigation,
// project/target/overlay changes, or when another modal owns the input frame.
void picker_cancel_input();
// Call every picker-owned frame, BEFORE touch dispatch (down may be zero).
// Left/right select filter/list column; up/down navigate that column. A applies
// a filter (never commits) or commits list focus; opening A only snaps first.
// B reverses the animation while retaining modal ownership. Buttons cancel pending
// touch and own that entire frame. Main must prevent underlying shortcuts/actions
// and must quarantine a held input tail when the overlay closes or opens.
void picker_buttons(u32 down);
void picker_touch_down(int tx, int ty);
void picker_touch_hold(int tx, int ty);
// TRUE physical lift only; cancellation must use picker_cancel_input instead.
void picker_touch_release();
// End-input, outside audio guard, BEFORE chains_capture/GPU. Validates ownership,
// advances shared motion, publishes both-screen metadata, completes B cancellation.
void picker_capture();
// Foreground ONLY, inclusive evolving frame bounds. Draw is engine/clock/heap
// independent; lower footer retains its original screen clip outside the frame.
void picker_draw_content(bool top, int left, int upper, int right, int bottom, float progress);
// Wrapper delegates the full lower surface to chains. Successful commits close
// immediately (not stale-chain collapse); B/close target use the reverse motion.
void picker_draw();
}
