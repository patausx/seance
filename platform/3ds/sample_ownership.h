#pragma once
#include "../../core/engine/engine.h"
#include "../../core/engine/units.h"

namespace seance::platform::ui3ds::sample_ownership {
// UI clipboard clones keep a slot reference, not a PCM copy. Treat that held
// sound like an installed owner before replacing/clearing/recycling a slot.
inline int unit_slot(const Unit* unit) {
    if (!unit) return -1;
    switch (unit->kind) {
        case UnitKind::SamplePlayer: return static_cast<const PlayUnit*>(unit)->sample_slot;
        case UnitKind::Grain: return static_cast<const GrainUnit*>(unit)->sample_slot;
        case UnitKind::Recorder: return static_cast<const RecUnit*>(unit)->sample_slot;
        default: return -1;
    }
}
inline bool clipboard_holds(int slot, const Unit* clipboard) {
    return slot >= 0 && slot < trackr::synth::SAMPLE_BANK_SIZE && unit_slot(clipboard) == slot;
}
// Caller holds the audio guard. Engine already includes the current bounce hold.
inline int count(const Engine& engine, int slot, const Unit* clipboard, const Unit* ignore = nullptr) {
    return engine.sample_slot_refs(slot, ignore).total() +
        (clipboard_holds(slot, clipboard) && engine.external_sample_slot() != slot ? 1 : 0);
}
} // namespace seance::platform::ui3ds::sample_ownership
