#pragma once
#include "../../core/engine/engine.h"

namespace seance::platform::ui3ds {
// Held-B bank panel of the sequencer. Stored steps are copied under one short
// audio guard when the panel opens or its candidate/content changes; the staged
// track values are plain UI state. The upper screen reads only this frozen DTO.
struct SequencerBankPanel {
    bool open = false;
    uint8_t bank = 0, play = 0, track = 0;
    uint8_t empty = 0;                  // bit per bank: no trig and no lock in any track
    uint8_t length[NUM_CHAINS] = {};    // staged values of the candidate bank
    uint8_t direction[NUM_CHAINS] = {};
    uint64_t trig[NUM_CHAINS] = {};     // stored steps of the candidate bank
    uint64_t lock[NUM_CHAINS] = {};
};
static_assert(sizeof(SequencerBankPanel) <= 96, "bounded bank overview DTO");
const SequencerBankPanel& sequencer_bank_panel();
} // namespace seance::platform::ui3ds
