#pragma once
// Stored-value policy for the score. No Engine, live frame or retained pointers.
// Formatting is control/UI work, never audio work. Scalar-safe inspector mappings
// are reused; frame-dependent FRACT/GHOST formatters are deliberately not called.
#include "../../core/engine/units.h"
#include "../../core/engine/play_mode.h"
#include "../../core/engine/vca_timing.h"
#include "module_inspector.h"
#include "osc_readout.h"
#include "play_readout.h"
#include "inst_readout.h"
#include "pent_readout.h"
#include "bit_readout.h"
#include "ring_readout.h"
#include "strain_readout.h"
#include "fold_editor_visual.h"
#include "tilt_readout.h"
#include "rev_readout.h"
#include "fm_context.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace seance::platform::ui3ds::seq_lock {
inline uint8_t variant(const Unit& unit) {
    // Only persistent structural configuration: changing a base/lock must not
    // change clipboard identity. Bits are local to each kind, not a common enum.
    switch (unit.kind) {
        case UnitKind::SamplePlayer: {
            const auto& u = static_cast<const PlayUnit&>(unit);
            return uint8_t(u.modern() | (u.pitch_quantize << 1) | (uint8_t(u.pitch_mode()) << 2));
        }
        case UnitKind::Rule: return static_cast<const RuleUnit&>(unit).config().phrase;
        case UnitKind::Pent: return uint8_t(static_cast<const PentUnit&>(unit).width);
        case UnitKind::Delay: return static_cast<const DelayUnit&>(unit).modern();
        // Same variant identity: legacy MOTOR cannot accept note/mode locks; modern can.
        case UnitKind::Motor: return static_cast<const MotorUnit&>(unit).modern();
        case UnitKind::Bitcrush: return static_cast<const BitUnit&>(unit).modern();
        case UnitKind::Ring: return static_cast<const RingUnit&>(unit).modern();
        case UnitKind::Fold: return static_cast<const FoldUnit&>(unit).modern();
        case UnitKind::Strain: return static_cast<const StrainUnit&>(unit).modern();
        case UnitKind::Tilt: return uint8_t(static_cast<const TiltUnit&>(unit).revision());
        case UnitKind::Vca: {
            const auto& u=static_cast<const VcaUnit&>(unit);
            return uint8_t(u.flexible | (u.modern()<<1));
        }
        case UnitKind::Osc: {
            const auto& u = static_cast<const OscUnit&>(unit);
            return uint8_t(u.shape_morph | (u.pitch_quantize << 1) | (int(u.wave) << 2) | (u.clean << 4));
        }
        case UnitKind::Fm: {
            const auto& u = static_cast<const FmUnit&>(unit);
            return uint8_t(u.extended | (u.drone << 1) | (u.alias_safe << 2) |
                (u.phase_reset << 3) | (int(u.carrier_wave) << 4) | (int(u.mod_wave) << 6));
        }
        case UnitKind::Grain: {
            const auto& u = static_cast<const GrainUnit&>(unit);
            return uint8_t(u.clock_sync | (u.sync_division << 1) | (u.clean << 3));
        }
        case UnitKind::Filter: return uint8_t(static_cast<const FilterUnit&>(unit).type);
        case UnitKind::Ghost: return static_cast<const GhostUnit&>(unit).long_tail;
        case UnitKind::Stranular: {
            const auto& u = static_cast<const StranularUnit&>(unit);
            return uint8_t(u.long_mode | (u.window_mode << 1) | (u.sync_division << 3));
        }
        default: return 0;
    }
}

namespace detail {
inline int uni(q15 value) { return std::max(0, int(value)); }
struct Domain { int index = -1, count = 0; };
inline Domain bins(int v, int count) { return {v * count / 32768, count}; }
inline Domain toggle(int v, int threshold) { return {v > threshold ? 1 : 0, 2}; }
inline Domain domain(UnitKind kind, uint8_t variant, int p, q15 value) {
    const int v = uni(value);
    switch (kind) {
        case UnitKind::Rule:
            if (p == RuleUnit::P_RULE) return {RuleUnit::rule_number(value),256};
            if (p == RuleUnit::P_SCALE) return {RuleUnit::scale_index(value),5};
            if (p == RuleUnit::P_VOICES) return {RuleUnit::voice_limit(value)-1,32};
            if (p == RuleUnit::P_MUTATE) return {v*64/32767,65};
            break;
        case UnitKind::Pent:
            if (p == PentUnit::P_ROOT) return {PentUnit::root_note(value), 121};
            break;
        case UnitKind::SamplePlayer:
            if (p == PlayUnit::P_NOTE) return {musical_note::decode(value)-musical_note::MIN, 73};
            if (p == PlayUnit::P_LOOP || p == PlayUnit::P_MODE || p == PlayUnit::P_SCRUB || p == PlayUnit::P_HOLD)
                return toggle(v, 16383);
            if (p == PlayUnit::P_SLICE) return bins(v, 17);
            if (p == PlayUnit::P_GATE) return {play_mode::gate_index(value), play_mode::GATE_COUNT};
            break;
        case UnitKind::Osc:
            if (p == OscUnit::P_PITCH && (variant & 2)) return {((v * 7200 / 32768) + 50) / 100, 73};
            break;
        case UnitKind::Fm:
            if (p == FmUnit::P_PITCH) return {((v * 7200 / 32768) + 50) / 100, 73};
            break;
        case UnitKind::Grain:
            if (p == GrainUnit::P_VOICES) return {(v * 7 + 16383) / 32767, 8};
            break;
        case UnitKind::Noise:
            if (p == NoiseUnit::P_COLOUR) return bins(v, 4);
            if (p == NoiseUnit::P_MODE) return {NoiseUnit::mode_index(value), 3};
            break;
        case UnitKind::Motor:
            if (p == MotorUnit::P_SOURCE) return bins(v, 3);
            if ((variant & 1) && p == MotorUnit::P_MODE) return toggle(v, 16383);
            if ((variant & 1) && p == MotorUnit::P_NOTE)
                return {musical_note::decode(value)-musical_note::MIN, musical_note::MAX-musical_note::MIN+1};
            break;
        case UnitKind::Ring:
            if ((variant & 1) && p == RingUnit::P_WAVE) return {RingUnit::wave_index(value),3};
            if ((variant & 1) && p == RingUnit::P_MODE) return toggle(v,16383);
            if ((variant & 1) && p == RingUnit::P_NOTE) return {musical_note::decode(value)-musical_note::MIN,73};
            break;
        case UnitKind::Vca:
            if ((variant & 1) && p == VcaUnit::P_DUCK) return toggle(v, 16383);
            if ((variant & 2) && (p == VcaUnit::P_SYNC || p == VcaUnit::P_RUN)) return toggle(v, 16383);
            if ((variant & 2) && p == VcaUnit::P_RETRIGGER) return {vca_timing::retrigger(value),3};
            break;
        case UnitKind::Delay:
            if (p == DelayUnit::P_HOLD || p == DelayUnit::P_SYNC || p == DelayUnit::P_MODE) return toggle(v, 16384);
            if (p == DelayUnit::P_DIV) return bins(v, 8);
            if (p == DelayUnit::P_FEEL) return bins(v, 3);
            break;
        case UnitKind::MicIn:
            if (p == MicUnit::P_MON) return toggle(v, 16384);
            if (p == MicUnit::P_LOW_CUT) return bins(v, 3);
            break;
        case UnitKind::Recorder:
            if (p == RecUnit::P_REC || p == RecUnit::P_MON) return toggle(v, 16383);
            break;
        case UnitKind::Bitcrush:
            if (p == BitUnit::P_BITS) return (variant & 1) ? Domain{BitUnit::bit_depth(value)-1,16} : bins(v, 13);
            if (!(variant & 1) && p == BitUnit::P_RATE) return {31 - (32767 - v) * 31 / 32767, 32};
            break;
        case UnitKind::Tilt:
            if (p == TiltUnit::P_RANGE && variant != uint8_t(TiltUnit::Revision::Extended))
                return (variant & 1) ? bins(v, 24) : Domain{v ? 1 + v * 24 / 32768 : 0, 25};
            break;
        case UnitKind::Reverb:
            if (p == ReverbUnit::P_FREEZE) return toggle(v, 16384);
            break;
        case UnitKind::Ghost:
            if (p == GhostUnit::P_FREEZE) return toggle(v, 16384);
            break;
        case UnitKind::Stranular:
            if (p == StranularUnit::P_FREEZE || p == StranularUnit::P_HOLD || p == StranularUnit::P_SYNC) return toggle(v, 16384);
            if (p == StranularUnit::P_CLUSTER) return {(v * 3 + 32766) / 32767, 4};
            break;
        case UnitKind::Inst:
            if (p == InstUnit::P_INSTRUMENT) return {InstUnit::instrument_index(value), InstUnit::INSTRUMENT_COUNT};
            if (p == InstUnit::P_NOTE) return {InstUnit::note_midi(value)-24, 73};
            break;
        case UnitKind::Glitch:
            if (p == GlitchUnit::P_KEEP) return toggle(v, 16383);
            break;
        case UnitKind::Arp:
            if (p == ArpUnit::P_RATE) return {ArpUnit::rate_index(v), ArpUnit::RATE_COUNT};
            if (p == ArpUnit::P_ORDER) return {ArpUnit::order_index(v), ArpUnit::ORDER_COUNT};
            if (p == ArpUnit::P_OCTAVES) return {ArpUnit::octaves(v)-1, 3};
            if (p == ArpUnit::P_RESET || p == ArpUnit::P_PLAY_MODE) return toggle(v, 16383);
            if (p >= ArpUnit::P_NOTE1 && p <= ArpUnit::P_NOTE4) {
                const int note=ArpUnit::note_semitones(v);
                return {note==ArpUnit::NOTE_OFF ? 0 : note+25, 50};
            }
            break;
        case UnitKind::Comp:
            if (p == CompUnit::P_CLOCK) return toggle(v, 16383);
            break;
        case UnitKind::Fract:
            if (p == FractUnit::P_SYNC || p == FractUnit::P_HOLD) return toggle(v, 16383);
            // LENGTH is always musical/discrete. SIZE is continuous in free
            // mode; per-step SYNC is not structural variant metadata, so do not
            // quantize all free SIZE edits to seven bins or disable their offset.
            if (p == FractUnit::P_LENGTH) return bins(v, 7);
            break;
        default: break;
    }
    return {};
}
inline void percent(q15 value, char* out, int n) {
    std::snprintf(out, n, "%d%%", (uni(value) * 100 + 16383) / 32767);
}
} // namespace detail

inline bool supports_offset(UnitKind kind, uint8_t variant, int param) {
    if (kind == UnitKind::SamplePlayer && param == PlayUnit::P_SPEED && (variant & 2) && !(variant & 12)) return false;
    return param >= 0 && param < PARAM_BANK_SIZE && kind != UnitKind::Empty &&
        detail::domain(kind, variant, param, 0).count == 0;
}
// Nudge/offset step is 328 raw q15 units (~1% full scale), not one physical
// hz/ms/db. Discrete controls instead cross one actual DSP bucket; boundary
// no-ops retain the exact input so the caller need not create a redundant lock.
inline q15 nudge_value(UnitKind kind, uint8_t variant, int param, q15 value, int direction, bool bipolar) {
    if (!direction) return value;
    const int sign = direction > 0 ? 1 : -1;
    if (kind == UnitKind::SamplePlayer && param == PlayUnit::P_SPEED && (variant & 2) && !(variant & 12)) {
        const auto rate = [](int raw) { return play_window(2, nullptr, 0, q15(raw), 0, 32767, 0, true).increment; };
        const int current = rate(value);
        // Signed quantized varispeed is monotonic, but has nonuniform bins and
        // no zero-speed bin. Cross the exact next DSP boundary, never stall on
        // a normalized increment that rounds back to the same semitone.
        int lo = -32767, hi = 32768;
        while (lo < hi) {
            const int mid = lo + (hi - lo) / 2;
            const bool before = sign > 0 ? rate(mid) <= current : rate(mid) < current;
            if (before) lo = mid + 1; else hi = mid;
        }
        const int next = sign > 0 ? lo : lo - 1;
        return next < -32767 || next > 32767 ? value : q15(next);
    }
    const auto d = detail::domain(kind, variant, param, value);
    if (!d.count) return q15(std::max(bipolar ? -32767 : 0, std::min(32767, int(value) + sign * 328)));
    const int next = std::max(0, std::min(d.count - 1, d.index + sign));
    if (next == d.index) return value; // don't create a lock on a boundary no-op
    if (next == 0) return 0;
    if (next == d.count - 1) return 32767;
    // Integer domain predicates exactly match DSP, including unequal bins and
    // singleton endpoints. Two bounded binary searches find a stable interior.
    int lo = 0, hi = 32768;
    while (lo < hi) { const int mid = (lo + hi) / 2;
        if (detail::domain(kind, variant, param, q15(mid)).index < next) lo = mid + 1; else hi = mid; }
    const int first = lo;
    lo = first; hi = 32768;
    while (lo < hi) { const int mid = (lo + hi) / 2;
        if (detail::domain(kind, variant, param, q15(mid)).index <= next) lo = mid + 1; else hi = mid; }
    return q15((first + lo - 1) / 2);
}

// Called with the stored clock of the edited step, never the playing clock.
inline q15 comp_release_nudge(q15 value, int direction, bool sync) {
    if (!direction) return value;
    const int sign=direction>0?1:-1;
    if (!sync) return q15(std::max(0,std::min(32767,int(value)+sign*328)));
    const int from=CompUnit::release_index(value);
    const int to=std::max(0,std::min(CompUnit::RELEASE_DIVISIONS-1,from+sign));
    return from==to?value:CompUnit::release_value(to);
}

inline void format_value(UnitKind kind, uint8_t variant, int param, q15 value, char* out, int capacity, MusicalScale scale = {}) {
    if (!out || capacity <= 0) return;
    if (param < 0 || param >= PARAM_BANK_SIZE) { std::snprintf(out, capacity, "--"); return; }
    const int v = detail::uni(value);
    if (kind == UnitKind::Pent) {pent_readout::value_text(out,size_t(capacity),param,value);return;}
    if (kind == UnitKind::Fold) {
        fold_editor_visual::value_text(out,size_t(capacity),param,value,(variant&1)!=0);return;
    }
    if (kind == UnitKind::Ring) {
        ring_readout::value_text(out,size_t(capacity),param,value,(variant&1)!=0);return;
    }
    if (kind == UnitKind::Bitcrush) {
        bit_readout::value_text(out,size_t(capacity),param,value,(variant&1)!=0);return;
    }
    if (kind == UnitKind::Strain) {
        strain_readout::value_text(out,size_t(capacity),param,value,(variant&1)!=0);return;
    }
    if (kind == UnitKind::Tilt) {
        // Range itself may vary among selected steps: normalized signed TILT
        // is honest here; never take the companion range from an editor frame.
        tilt_readout::value_text(out,size_t(capacity),param,value,TiltUnit::Revision(variant),0,false);return;
    }
    if (kind == UnitKind::Comp && param == CompUnit::P_RELEASE) {
        // Group steps may disagree about CLOCK. Explicitly retain BOTH stored
        // laws; neither an audible mode nor a stale module frame is authoritative.
        std::snprintf(out,capacity,"f%.0fms s%s",double(CompUnit::release_ms(value,false,120<<16)),CompUnit::release_label(value));
        return;
    }
    if (kind == UnitKind::Inst) {
        // Instrument itself is lockable, not a structural variant. Always show
        // the stored MIDI key/time, never borrow another step's live drum name.
        inst_readout::value_text(out, size_t(capacity), param, value);
        return;
    }
    if (kind == UnitKind::Glitch) {
        if (param == GlitchUnit::P_KEEP) std::snprintf(out, capacity, "%s", value > 16383 ? "keep" : "evolve");
        else detail::percent(value, out, capacity);
        return;
    }
    if (kind == UnitKind::Fract) {
        if (param == FractUnit::P_SIZE) {
            // Both laws, explicitly named: selected steps can have different SYNC
            // locks. Nominal note division is not a claim of uncapped audio time.
            std::snprintf(out, capacity, "f%ums s%s", unsigned((FractUnit::size_frames(value, false, 120 << 16) + 16) / 32), FractUnit::size_label(value));
        } else if (param == FractUnit::P_LENGTH) {
            static const char* beats[] = {"1/16", "1/8", "1/4", "1/2", "1", "2", "4"};
            std::snprintf(out, capacity, "%s beat", beats[v * 7 / 32768]);
        } else if (param == FractUnit::P_SPEED) std::snprintf(out, capacity, "%+.2fx", double(FractUnit::speed_q16(value)) / 65536.0);
        else if (param == FractUnit::P_SYNC) std::snprintf(out, capacity, "%s", value > 16383 ? "sync" : "free");
        else if (param == FractUnit::P_HOLD) std::snprintf(out, capacity, "%s", value > 16383 ? "on" : "off");
        else detail::percent(value, out, capacity);
        return;
    }
    if (kind == UnitKind::SamplePlayer) {
        play_readout::value_text(out, size_t(capacity), param, value, (variant & 1) != 0, (variant & 2) != 0,
            PlayUnit::PitchMode((variant >> 2) & 3), scale);
        return;
    }
    if (kind == UnitKind::Grain && param == GrainUnit::P_SCAN && !(variant & 8)) {
        std::snprintf(out, capacity, "v2 only"); return;
    }
    if (kind == UnitKind::Fm) { fm_context_value_text(out, unsigned(capacity), param, value); return; }
    if (kind == UnitKind::Osc) {
        const bool clean=(variant&16)!=0,quantize=(variant&2)!=0;
        if(param==OscUnit::P_PITCH){
            std::snprintf(out,capacity,"%.1fhz",osc_readout::hz(value,clean,quantize,scale));return;
        }
        if(osc_readout::value_text(out,size_t(capacity),param,value,clean,quantize,scale))return;
    }
    if (kind == UnitKind::Ghost) {
        if (param == GhostUnit::P_TONE) std::snprintf(out, capacity, "%uhz root", unsigned(GhostUnit::root_hz(value)));
        else if (param == GhostUnit::P_FINE) std::snprintf(out, capacity, "%+.2fct", double(value) * 50 / 32767);
        else if (param == GhostUnit::P_RATE) std::snprintf(out, capacity, "%.1fs", double(GhostUnit::drift_period_ms(value)) / 1000);
        else if (param == GhostUnit::P_FREEZE) std::snprintf(out, capacity, "%s", value > 16384 ? "on" : "off");
        else detail::percent(value, out, capacity);
        return;
    }
    if (kind == UnitKind::Reverb) {
        if (param >= ReverbUnit::P_PREDELAY && param < ReverbUnit::P_COUNT) {
            rev_readout::value(out,capacity,param,value,true);return;
        }
        if (param == ReverbUnit::P_SIZE) {
            const unsigned scale = 16384u + unsigned(v);
            std::snprintf(out, capacity, "%u-%ums", (733u + (1499u * scale >> 16) + 16) / 32, (1201u + (2309u * scale >> 16) + 16) / 32);
        } else if (param == ReverbUnit::P_DECAY) std::snprintf(out, capacity, "%.1f%% fb", double(6000 + (v * 24000 >> 15)) * 100 / 32768);
        else if (param == ReverbUnit::P_FREEZE) std::snprintf(out, capacity, "%s", value > 16384 ? "on" : "off");
        else detail::percent(value, out, capacity);
        return;
    }
    if (kind == UnitKind::Stranular) {
        if (param == StranularUnit::P_SIZE) std::snprintf(out, capacity, "%ums", unsigned((128 + uint64_t(v) * ((variant & 1 ? 32768 : 8192) - 128) / 32767 + 16) / 32));
        else if (param == StranularUnit::P_PITCH) std::snprintf(out, capacity, "%+.2fx", double(StranularUnit::pitch_increment_q16(value)) / 65536);
        else if (param == StranularUnit::P_FREEZE || param == StranularUnit::P_HOLD) std::snprintf(out, capacity, "%s", value > 16384 ? "on" : "off");
        else if (param == StranularUnit::P_SYNC) std::snprintf(out, capacity, "%s", value > 16384 ? "sync" : "free");
        else if (param == StranularUnit::P_CLUSTER) std::snprintf(out, capacity, "%u grains", unsigned(StranularUnit::cluster_count(value)));
        else if (param == StranularUnit::P_DENS) std::snprintf(out, capacity, "%.1f/s free", 32000.0 / (32 + uint64_t(32767 - v) * 2016 / 32767));
        else { std::snprintf(out, capacity, "q15 %d", int(value)); }
        return;
    }
    if (kind == UnitKind::Tilt && !(variant & 1) && (param == TiltUnit::P_FREQ || param == TiltUnit::P_DRIVE)) {
        // Legacy RANGE==0 changes both laws, and RANGE can itself be step locked.
        std::snprintf(out, capacity, "q15 %d", int(value)); return;
    }
    if (kind == UnitKind::Recorder && param == RecUnit::P_REC) {
        std::snprintf(out, capacity, "%s", value > 16383 ? "record" : "off"); return;
    }
    if (kind == UnitKind::Vca && (variant & 2) &&
        (param == VcaUnit::P_ATTACK || param == VcaUnit::P_HOLD || param == VcaUnit::P_DECAY)) {
        // SYNC can itself be step locked. This scalar score formatter has no
        // sibling lock context: never present a free-ms value as a synced time,
        // and never include a changing base/lock in clipboard variant identity.
        if(param==VcaUnit::P_HOLD && value<=0)std::snprintf(out,capacity,"off");
        else std::snprintf(out,capacity,"q15 %d",int(value));
        return;
    }
    if (module_inspector_supports(kind)) {
        module_inspector_value_text(out, size_t(capacity), kind, param, value,
            kind != UnitKind::Vca || (variant & 1), kind == UnitKind::Delay && (variant & 1),
            kind != UnitKind::Tilt || (variant & 1), false);
        return;
    }
    std::snprintf(out, capacity, "q15 %d", int(value));
}
} // namespace seance::platform::ui3ds::seq_lock
