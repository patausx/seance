#pragma once
// Project file slots, independent of the sequencer's eight pattern banks.
namespace seance {
constexpr int PROJECT_SLOTS = 128;
constexpr int PROJECT_NAME_BYTES = 24; // 23 printable ASCII characters plus NUL
constexpr bool project_slot_valid(int slot) { return slot >= 0 && slot < PROJECT_SLOTS; }
} // namespace seance
