// Included by engine.cpp; no new build-list dependency. Only persistent bases
// are written: never snap/resolve/trigger/reset, alter routes, allocate, or call UI.
#include "delay_time.h"
#include "vca_timing.h"
#include "musical_note.h"
#include "play_mode.h"

namespace seance {
namespace module_random_detail {
enum Domain : uint8_t {
    Skip, Continuous, Note, SpeedNote, Voices, Bits16, Bits13, MicCut,
    DelayDivision, DelayFeel, VcaDivision, VcaHold, CompRelease,
    ArpRate, ArpGate, ArpOrder, ArpOctaves, ArpNote, PlayGate, InstNote, FractDivision,
    Colour4, Wave3, Toggle, TiltClearRange, TiltLegacyRange, BitLegacyRate
};
struct Policy {
    Domain domain[PARAM_BANK_SIZE] = {};
    uint64_t identity = 0;
    void range(int first, int end) {
        for (int p=first; p<end && p<PARAM_BANK_SIZE; ++p) domain[p]=Continuous;
    }
};
// Explicit opt-in per current variant. A new kind or appended parameter remains
// excluded until reviewed here. Commands (REC, FRZ, HOLD, RUN, KEEP), source ids,
// edit windows/cuts/slices, clocks, launch/revision/topology flags are untouched.
static Policy policy(const Unit& u) {
    Policy p;
    uint32_t mode=0;
    switch (u.kind) {
    case UnitKind::SamplePlayer: {
        const auto& v=static_cast<const PlayUnit&>(u);
        mode=uint32_t(v.modern()) | (uint32_t(v.pitch_mode())<<1) | (uint32_t(v.pitch_quantize)<<3) |
            (uint32_t(uint8_t(v.sample_slot))<<8);
        p.domain[PlayUnit::P_GAIN]=Continuous;
        if (v.pitch_mode()!=PlayUnit::PitchMode::Legacy && v.params[PlayUnit::P_SCRUB].base<=16383)
            p.domain[PlayUnit::P_NOTE]=Note;
        else p.domain[PlayUnit::P_SPEED]=v.pitch_quantize?SpeedNote:Continuous;
        if (v.modern() && v.params[PlayUnit::P_MODE].base>=16384) p.domain[PlayUnit::P_GATE]=PlayGate;
        break;
    }
    case UnitKind::Grain: {
        const auto& v=static_cast<const GrainUnit&>(u);
        mode=uint32_t(v.clean) | (uint32_t(v.clock_sync)<<1) | (uint32_t(v.launch_mode)<<2) |
            (uint32_t(v.window)<<4) | (uint32_t(v.burst_count)<<8) | (uint32_t(uint8_t(v.sample_slot))<<16);
        p.range(GrainUnit::P_SIZE, GrainUnit::P_SCAN);
        p.domain[GrainUnit::P_VOICES]=Voices;
        if (v.clock_sync) p.domain[GrainUnit::P_DENS]=Skip;
        if (v.clean) p.domain[GrainUnit::P_SCAN]=Continuous;
        break;
    }
    case UnitKind::Osc: {
        const auto& v=static_cast<const OscUnit&>(u);
        mode=uint32_t(v.clean) | (uint32_t(v.shape_morph)<<1) | (uint32_t(v.pitch_quantize)<<2) | (uint32_t(v.wave)<<3);
        p.range(0,OscUnit::P_SHAPE);
        if (v.pitch_quantize) p.domain[OscUnit::P_PITCH]=Note;
        if (v.wave!=OscUnit::Wave::Pulse && !v.shape_morph) p.domain[OscUnit::P_PW]=Skip;
        if (v.shape_morph) p.domain[OscUnit::P_SHAPE]=Continuous;
        break;
    }
    case UnitKind::Filter: mode=uint32_t(static_cast<const FilterUnit&>(u).type);p.range(0,4);break;
    case UnitKind::Vca: {
        const auto& v=static_cast<const VcaUnit&>(u);
        mode=uint32_t(v.modern()) | (uint32_t(v.flexible)<<1);
        p.range(0,v.flexible?VcaUnit::P_SYNC:3);
        if (v.flexible) p.domain[VcaUnit::P_DUCK]=Toggle;
        if (v.modern() && v.params[VcaUnit::P_SYNC].base>=16384) {
            p.domain[VcaUnit::P_ATTACK]=p.domain[VcaUnit::P_DECAY]=VcaDivision;
            p.domain[VcaUnit::P_HOLD]=VcaHold;
        }
        break;
    }
    case UnitKind::Delay: {
        const auto& v=static_cast<const DelayUnit&>(u);mode=v.modern();
        p.range(0,DelayUnit::P_HOLD);
        if (v.modern()) {
            p.domain[DelayUnit::P_TONE]=Continuous;
            if (v.params[DelayUnit::P_SYNC].base>16384) {
                p.domain[DelayUnit::P_TIME]=Skip;p.domain[DelayUnit::P_DIV]=DelayDivision;
                p.domain[DelayUnit::P_FEEL]=DelayFeel;
            }
        }
        break;
    }
    case UnitKind::Fold:
        mode=static_cast<const FoldUnit&>(u).modern();p.range(0,FoldUnit::P_COUNT);
        // MODE is a continuous waveshaper morph, NOT a revision/launch command.
        if (!mode) p.domain[FoldUnit::P_MODE]=Skip;
        break;
    case UnitKind::MicIn:
        p.domain[MicUnit::P_GAIN]=Continuous;p.domain[MicUnit::P_LOW_CUT]=MicCut;break;
    case UnitKind::Recorder:
        mode=uint32_t(uint8_t(static_cast<const RecUnit&>(u).sample_slot));
        p.domain[RecUnit::P_LEN]=Continuous;break; // REC and MON never randomized
    case UnitKind::Bitcrush:
        mode=static_cast<const BitUnit&>(u).modern();p.range(0,BitUnit::P_COUNT);
        p.domain[BitUnit::P_BITS]=mode?Bits16:Bits13;p.domain[BitUnit::P_COMP]=Skip;
        if (!mode) p.domain[BitUnit::P_RATE]=BitLegacyRate;
        break;
    case UnitKind::Chorus:p.range(0,ChorusUnit::P_COUNT);break;
    case UnitKind::Reverb:
        mode=static_cast<const ReverbUnit&>(u).modern();p.range(0,mode?ReverbUnit::P_COUNT:5);
        p.domain[ReverbUnit::P_FREEZE]=Skip;break;
    case UnitKind::Stranular: {
        const auto& v=static_cast<const StranularUnit&>(u);
        mode=uint32_t(v.long_mode) | (uint32_t(v.window_mode)<<1) | (uint32_t(v.sync_division)<<8);
        p.range(0,StranularUnit::P_HOLD);p.domain[StranularUnit::P_FREEZE]=Skip;
        if (v.params[StranularUnit::P_SYNC].base>16384) p.domain[StranularUnit::P_DENS]=Skip;
        break; // hold/sync/cluster stay fixed
    }
    case UnitKind::Ghost:
        mode=static_cast<const GhostUnit&>(u).long_tail;p.range(0,8);p.domain[GhostUnit::P_FREEZE]=Skip;break;
    case UnitKind::Ring: {
        const auto& v=static_cast<const RingUnit&>(u);mode=v.modern();p.range(0,RingUnit::P_COUNT);
        p.domain[RingUnit::P_MODE]=Skip;
        if (v.modern()) p.domain[RingUnit::P_WAVE]=Wave3;
        if (v.modern() && v.params[RingUnit::P_MODE].base>=16384) {
            p.domain[RingUnit::P_FREQ]=Skip;p.domain[RingUnit::P_NOTE]=Note;
        } else p.domain[RingUnit::P_NOTE]=Skip;
        break;
    }
    case UnitKind::Tilt: {
        const auto& v=static_cast<const TiltUnit&>(u);mode=uint32_t(v.revision());p.range(0,TiltUnit::P_COUNT);
        if (!v.extended()) p.domain[TiltUnit::P_RANGE]=v.modern()?TiltClearRange:TiltLegacyRange;
        break;
    }
    case UnitKind::Noise:
        mode=static_cast<const NoiseUnit&>(u).modern();p.range(0,NoiseUnit::P_COUNT);
        p.domain[NoiseUnit::P_COLOUR]=Colour4;p.domain[NoiseUnit::P_MODE]=Skip;break;
    case UnitKind::Fm: {
        const auto& v=static_cast<const FmUnit&>(u);
        mode=uint32_t(v.extended) | (uint32_t(v.drone)<<1) | (uint32_t(v.alias_safe)<<2) |
            (uint32_t(v.phase_reset)<<3) | (uint32_t(v.carrier_wave)<<4) | (uint32_t(v.mod_wave)<<8);
        p.range(0,v.extended?FmUnit::P_COUNT:10);
        p.domain[FmUnit::P_PITCH]=Note;
        if (v.drone) {p.domain[FmUnit::P_DECAY]=p.domain[FmUnit::P_INDEX_FALL]=p.domain[FmUnit::P_ATTACK]=
            p.domain[FmUnit::P_FEEDBACK_FALL]=p.domain[FmUnit::P_PITCH_ENV]=p.domain[FmUnit::P_PITCH_TIME]=Skip;}
        break;
    }
    case UnitKind::Motor: {
        const auto& v=static_cast<const MotorUnit&>(u);mode=v.modern();p.range(0,MotorUnit::P_COUNT);
        p.domain[MotorUnit::P_SOURCE]=p.domain[MotorUnit::P_SYNC]=p.domain[MotorUnit::P_MODE]=Skip;
        if (v.modern() && v.params[MotorUnit::P_MODE].base>=16384) {
            p.domain[MotorUnit::P_RPM]=Skip;p.domain[MotorUnit::P_NOTE]=Note;
        } else p.domain[MotorUnit::P_NOTE]=Skip;
        break;
    }
    case UnitKind::Strain:mode=static_cast<const StrainUnit&>(u).modern();p.range(0,StrainUnit::P_COUNT);break;
    case UnitKind::Shift:p.range(0,ShiftUnit::P_COUNT);break;
    case UnitKind::Bird:p.range(0,BirdUnit::P_COUNT);break;
    case UnitKind::Fract:
        p.range(0,FractUnit::P_SYNC);
        p.domain[FractUnit::P_LENGTH]=FractDivision;
        if (u.params[FractUnit::P_SYNC].base>16383) p.domain[FractUnit::P_SIZE]=FractDivision;
        break;
    case UnitKind::Comp:
        p.range(0,CompUnit::P_CLOCK);
        if (u.params[CompUnit::P_CLOCK].base>=16384) p.domain[CompUnit::P_RELEASE]=CompRelease;
        break;
    case UnitKind::Mouth:p.range(0,MouthUnit::P_COUNT);break;
    case UnitKind::Arp:
        p.domain[ArpUnit::P_RATE]=ArpRate;p.domain[ArpUnit::P_GATE]=ArpGate;
        p.domain[ArpUnit::P_ORDER]=ArpOrder;p.domain[ArpUnit::P_OCTAVES]=ArpOctaves;
        // Retain disabled note membership/recall; no topology change to the cycle.
        for (int n=ArpUnit::P_NOTE1;n<ArpUnit::P_COUNT;++n)
            if (ArpUnit::note_semitones(u.params[n].base)!=ArpUnit::NOTE_OFF) p.domain[n]=ArpNote;
        break;
    case UnitKind::Glitch:p.range(0,GlitchUnit::P_KEEP);break; // pattern/RNG/KEEP preserved
    case UnitKind::Inst:
        p.range(InstUnit::P_NOTE,InstUnit::P_COUNT);
        // Drum note chooses a source/sample identity. Preserve it and its inactive LEN.
        if (InstUnit::instrument_index(u.params[InstUnit::P_INSTRUMENT].base)==5)
            p.domain[InstUnit::P_NOTE]=p.domain[InstUnit::P_LENGTH]=Skip;
        else p.domain[InstUnit::P_NOTE]=InstNote;
        break;
    default: break;
    }
    // Stable context key for undo. All bases are compared separately. Runtime
    // phase, effective values and modulation/override state never affect policy.
    p.identity=(uint64_t(uint8_t(u.kind))<<56) | (uint64_t(uint8_t(u.num_params))<<48) | mode;
    for (int i=0;i<PARAM_BANK_SIZE;++i) {
        if (i>=u.num_params) p.domain[i]=Skip;
        p.identity ^= (uint64_t(p.domain[i])+uint64_t(u.params[i].bipolar)*31u) *
            (0x9e3779b97f4a7c15ULL + uint64_t(i)*0x100000001b3ULL);
    }
    return p;
}
static uint32_t next(uint32_t& state) {
    state^=state<<13;state^=state>>17;state^=state<<5;return state;
}
static int pick(uint32_t r,int count) {return int((uint64_t(r)*uint32_t(count))>>32);}
static q15 value(Domain d,const Param& param,uint32_t r) {
    switch(d) {
    case Note:return musical_note::encode(36+pick(r,49));
    case SpeedNote:return q15(musical_note::ratio_q16(pick(r,25)-12)/8);
    case Voices:return q15((pick(r,GrainUnit::MAX_GRAINS)*32767+3)/7);
    case Bits16:return q15(pick(r,16)*2048+1024);
    case Bits13:return q15((pick(r,13)*32768+16384)/13);
    case MicCut:return q15((pick(r,3)*32768+16384)/3);
    case DelayDivision:return delay_time::division_value(pick(r,8));
    case DelayFeel:return delay_time::feel_value(pick(r,3));
    case VcaDivision:return vca_timing::division_value(pick(r,vca_timing::COUNT));
    case VcaHold:return vca_timing::division_value(pick(r,vca_timing::COUNT+1)-1,true);
    case CompRelease:return CompUnit::release_value(pick(r,CompUnit::RELEASE_DIVISIONS));
    case ArpRate:return ArpUnit::rate_value(pick(r,ArpUnit::RATE_COUNT));
    case ArpGate:return ArpUnit::gate_value(5+pick(r,96));
    case ArpOrder:return ArpUnit::order_value(pick(r,ArpUnit::ORDER_COUNT));
    case ArpOctaves:return ArpUnit::octaves_value(1+pick(r,3));
    case ArpNote:return ArpUnit::note_value(pick(r,49)-24);
    case PlayGate:return play_mode::gate_value(pick(r,play_mode::GATE_COUNT));
    case InstNote:return InstUnit::note_value(36+pick(r,49));
    case FractDivision:return q15((pick(r,7)*32768+16384)/7);
    case Colour4:return q15(pick(r,4)*8192+4096);
    case Wave3:return q15((pick(r,3)*32768+16384)/3);
    case Toggle:return pick(r,2)?32767:0;
    case TiltClearRange:return q15((pick(r,24)*32768+16384)/24);
    case TiltLegacyRange:{const int index=pick(r,25);return index?q15(((index-1)*32768+16384)/24):0;}
    case BitLegacyRate:return q15(32767-(pick(r,32)*32767+30)/31);
    case Continuous:return q15(param.bipolar?pick(r,65535)-32767:pick(r,32768));
    default:return param.base;
    }
}
static bool address(int chain,int slot) {return (unsigned)chain<NUM_CHAINS && (unsigned)slot<UNITS_PER_CHAIN;}
}

void Engine::initialize_module_random(Unit& unit) {
    unit.module_random_={};
    // Independent installation identity; no sequencer/synth/global RNG consulted.
    uint32_t seed=0x4d524e44u+(++module_random_identity_)*0x9e3779b9u+uint32_t(unit.kind)*0x85ebca6bu;
    unit.module_random_.rng=seed?seed:1u;
    unit.module_random_.observed_source=unit.random_source;
}
bool Engine::randomize_module_bases(Unit& unit) {
    using namespace module_random_detail;
    if (!unit.module_random_.rng) initialize_module_random(unit);
    const Policy config=policy(unit);
    auto& rt=unit.module_random_;
    q15 draft[PARAM_BANK_SIZE];uint16_t mask=0;
    for (int p=0;p<PARAM_BANK_SIZE;++p) {
        draft[p]=unit.params[p].base;
        if (config.domain[p]==Skip) continue;
        draft[p]=value(config.domain[p],unit.params[p],next(rt.rng));
        if (draft[p]!=unit.params[p].base) mask|=uint16_t(1u<<p);
    }
    if (!mask) return false;
    for (int p=0;p<PARAM_BANK_SIZE;++p) {
        rt.before[p]=unit.params[p].base;rt.after[p]=draft[p];
        if (mask&(1u<<p)) unit.params[p].base=draft[p];
    }
    rt.policy=policy(unit).identity;rt.mask=mask;rt.undo_valid=true;
    ++module_random_revision_;
    return true;
}
bool Engine::module_random_available(int chain,int slot) const {
    if (!module_random_detail::address(chain,slot) || !chains[chain].units[slot] || recording_active_or_pending()) return false;
    const auto config=module_random_detail::policy(*chains[chain].units[slot]);
    for (auto domain:config.domain) if (domain!=module_random_detail::Skip) return true;
    return false;
}
bool Engine::randomize_module(int chain,int slot) {
    return module_random_detail::address(chain,slot) && chains[chain].units[slot] &&
        !recording_active_or_pending() && randomize_module_bases(*chains[chain].units[slot]);
}
bool Engine::set_module_random_source(int chain,int slot,ModSource source) {
    if (!module_random_detail::address(chain,slot) ||
        (source!=ModSource::None && mod_source_sigil(source)<0) || recording_active_or_pending()) return false;
    Unit* unit=chains[chain].units[slot];
    if (!unit || unit->random_source==source) return false;
    unit->random_source=unit->module_random_.observed_source=source;
    unit->module_random_.armed=false;
    return true;
}
bool Engine::module_random_undo_matches(const Unit& unit) {
    const auto& rt=unit.module_random_;
    if (!rt.undo_valid || rt.policy!=module_random_detail::policy(unit).identity) return false;
    for (int p=0;p<PARAM_BANK_SIZE;++p) if (unit.params[p].base!=rt.after[p]) return false;
    return true;
}
bool Engine::can_undo_module_randomize(int chain,int slot) const {
    return module_random_detail::address(chain,slot) && chains[chain].units[slot] &&
        !recording_active_or_pending() && module_random_undo_matches(*chains[chain].units[slot]);
}
bool Engine::undo_module_randomize(int chain,int slot) {
    if (!module_random_detail::address(chain,slot) || recording_active_or_pending()) return false;
    Unit* unit=chains[chain].units[slot];if (!unit) return false;
    if (!module_random_undo_matches(*unit)) {unit->module_random_.undo_valid=false;return false;}
    auto& rt=unit->module_random_;
    for (int p=0;p<PARAM_BANK_SIZE;++p) if (rt.mask&(1u<<p)) unit->params[p].base=rt.before[p];
    rt.undo_valid=false;++module_random_revision_;return true;
}
void Engine::step_module_random() {
    bool checked_recording=false, blocked=false;
    for (auto& chain:chains) for (Unit* unit:chain.units) {
        if (!unit) continue;
        auto& rt=unit->module_random_;
        // Cheap stale-base invalidation only. The complete variant/domain check
        // runs when undo is queried/applied, not 24x per control-rate tick.
        if (rt.undo_valid) for (int p=0;p<PARAM_BANK_SIZE;++p)
            if (unit->params[p].base!=rt.after[p]) {rt.undo_valid=false;break;}
        if (unit->random_source!=rt.observed_source) {
            rt.observed_source=unit->random_source;rt.armed=false;
        }
        if (mod_source_sigil(unit->random_source)<0) {rt.armed=false;continue;}
        // A bipolar sigil's negative lobe is LOW, not remapped to halfway/high.
        const q15 signal=modulation_value(unit->random_source);
        if (signal<=8192) rt.armed=true;
        else if (signal>=16384 && rt.armed) {
            rt.armed=false; // consume before every refusal; no delayed unmute fire
            if (chain.mute || unit->bypass) continue;
            if (!checked_recording) {blocked=recording_active_or_pending();checked_recording=true;}
            if (!blocked) randomize_module_bases(*unit);
        }
    }
}
} // namespace seance
