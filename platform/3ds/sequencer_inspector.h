#pragma once
#include "../../core/engine/engine.h"

namespace seance::platform::ui3ds {
// Copied once before drawing under a short audio guard. Not part of the hot
// audio-worker snapshot; no heap storage or retained Unit/Param pointers.
struct SequencerInspector {
    struct Parameter {
        char name[9] = {};
        q15 base = 0, live = 0, depth = 0;
        ModSource source = ModSource::None;
        bool bipolar = false;
    };
    struct Module {
        UnitKind kind = UnitKind::Empty;
        bool bypass = false;
        uint8_t variant = 0; // seq_lock::variant(), structural mode only
        int sample = -1;
        int count = 0;
        Parameter params[PARAM_BANK_SIZE];
    };
    Module modules[UNITS_PER_CHAIN];
    // Selected module only. Range excludes unlocked steps; focused is the stored
    // lock or base, never effective/modulated. Coverage is independent of range.
    struct LockRow {
        q15 minimum = 0, maximum = 0, focused = 0;
        uint8_t covered = 0;
        bool focused_locked = false;
    };
    LockRow rows[PARAM_BANK_SIZE];
    uint64_t selection = 0, paste_mask = 0;
    int selected_count = 1;
    int clipboard_count = 0;
    int copy_scope = 0; // 0 whole steps, 1 locks, 2 this parameter
    int clipboard_scope = 0; // immutable copied payload scope, not next-copy scope
    bool offset = false;
    bool can_undo = false, can_redo = false;
    char status[48] = {};
    StepLock locks[SEQ_LOCKS] = {};
    int chain = -1, step = -1, lock_count = 0;
    int slot = -1, param = -1;
    bool show_l_help = false;
    // Stored selected-step PLAY projection, not a live voice duration. Sample
    // metadata only; shared exact window math with the runtime PLAY module.
    struct Play {
        bool present = false, valid = false, loop = false, modulated = false;
        int sample = -1;
        uint8_t slice = 0;
        uint16_t start = 0, end = 0;
        int32_t increment = 0;
        uint64_t milliseconds = 0;
    } play;
};
static_assert(sizeof(SequencerInspector) <= 3072, "bounded sequencer controller DTO");
void sequencer_capture_inspector();
const SequencerInspector& sequencer_inspector();
} // namespace seance::platform::ui3ds
