#pragma once
#include "../../core/engine/engine.h"

namespace seance::platform::ui3ds::performance_return {
// Caller holds AudioOut::Guard for capture/restore. This is not a saved scene:
// no capture_scene(), lock release, PCM, allocation or engine runtime rewind.
struct Point {
    Engine* owner = nullptr;
    const Unit* units[NUM_CHAINS][UNITS_PER_CHAIN] = {};
    uint8_t counts[NUM_CHAINS][UNITS_PER_CHAIN] = {};
    PersistentScene sound[NUM_CHAINS];
    q15 morph[NUM_CHAINS] = {};
    PatternTransitionState rhythm, last_rhythm;
    uint8_t bank = 0, levels_changed = 0;
    bool sound_changed = false, rhythm_changed = false;

    bool active() const { return owner != nullptr; }
    void reset() { owner=nullptr;levels_changed=0;sound_changed=rhythm_changed=false; }
    void capture(Engine& engine) {
        reset();owner=&engine;bank=engine.active_pattern;
        rhythm=last_rhythm=engine.pattern_transition();
        for(int c=0;c<NUM_CHAINS;++c) {
            const auto& chain=engine.chains[c];
            sound[c].volume=chain.volume;sound[c].pan=chain.pan;
            morph[c]=engine.scene_morph[c];
            for(int s=0;s<UNITS_PER_CHAIN;++s) {
                const auto* unit=chain.units[s];units[c][s]=unit;
                counts[c][s]=unit?unit->num_params:0;
                sound[c].kind[s]=unit?unit->kind:UnitKind::Empty;
                for(int p=0;p<MAX_PARAMS;++p)
                    sound[c].base[s][p]=unit&&p<unit->num_params?unit->params[p].base:0;
            }
        }
    }
    bool matches(const Engine& engine) const {
        if(owner!=&engine)return false;
        for(int c=0;c<NUM_CHAINS;++c)for(int s=0;s<UNITS_PER_CHAIN;++s) {
            const auto* unit=engine.chains[c].units[s];
            if(unit!=units[c][s] || (unit && (unit->kind!=sound[c].kind[s] || unit->num_params!=counts[c][s])))return false;
        }
        return true;
    }
    bool restore(Engine& engine) {
        if(!matches(engine)) {reset();return false;}
        for(int c=0;c<NUM_CHAINS;++c) {
            if(levels_changed&(1u<<c))engine.set_chain_volume_smooth(c,sound[c].volume);
            if(!sound_changed)continue;
            auto& chain=engine.chains[c];chain.pan=sound[c].pan;engine.scene_morph[c]=morph[c];
            for(int s=0;s<UNITS_PER_CHAIN;++s)if(auto* unit=chain.units[s])
                for(int p=0;p<unit->num_params;++p)unit->params[p].base=sound[c].base[s][p];
        }
        bool restored=true;
        if(rhythm_changed) {
            const auto live=engine.pattern_transition();
            // A launch/reset that took ownership must never be resurrected by L.
            restored=engine.active_pattern==bank && engine.pending_pattern>=NUM_PATTERNS &&
                !engine.pattern_chain_playing && live.active && live.source==last_rhythm.source &&
                live.destination==last_rhythm.destination && live.position==last_rhythm.position;
            if(restored) {
                if(rhythm.active)restored=engine.set_pattern_transition(rhythm.source,rhythm.destination,rhythm.position);
                else engine.clear_pattern_transition();
            }
        }
        reset();return restored;
    }
};
static_assert(sizeof(Point)<=1536,"bounded held-L return point; no whole-engine copy");
} // namespace seance::platform::ui3ds::performance_return
