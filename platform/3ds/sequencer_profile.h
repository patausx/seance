#pragma once
#include "sequencer_inspector.h"
#include "sequencer_layout.h"
#include "sequence_note.h"

namespace seance::platform::ui3ds::seq_profile {
// Approved menu composition moves the unchanged stored profile down 14px.
constexpr int X = 212, DX = 11, WIDTH = 9, Y = 86, HEIGHT = 48;
inline int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
inline int pitch_normal(q15 value, UnitKind kind, int param = -1) {
    if (sequence_note::absolute(kind,param)) {
        const int low=sequence_note::minimum(kind),high=sequence_note::maximum(kind);
        return clamp((sequence_note::midi(kind,value)-low)*32767/(high-low),0,32767);
    }
    if (kind == UnitKind::SamplePlayer || kind == UnitKind::Grain || kind == UnitKind::Stranular)
        return ((int)value + 32767) / 2; // explicitly signed speed for PLAY/GRAN
    return clamp(value, 0, 32767);
}
struct Point {
    int x = 0, y = 0;
    bool locked = false, trigger = false, active = false, conditional = false;
};
struct Profile {
    Point points[seq_layout::PAGE_STEPS];
    int first = 0, selected = 0, playhead = -1, base_y = Y + HEIGHT;
    bool available = true, pitch = false, level = false;
};
inline Profile capture(const EngineUiSnapshot& s, const SequencerInspector& d, int mode) {
    Profile out;
    if (d.chain < 0 || d.chain >= NUM_CHAINS || d.step < 0 || d.step >= SEQ_STEPS) {
        out.available = false; return out;
    }
    const int c = d.chain;
    out.first = d.step / 16 * 16; out.selected = d.step % 16;
    out.pitch = mode == 1; out.level = mode == 2;
    const int slot = out.pitch ? s.sequence_pitch_slot[c] : s.sequence_value_slot[c];
    const int param = out.pitch ? s.sequence_pitch_param[c] : s.sequence_value_param[c];
    const UnitKind kind = (UnitKind)s.sequence_pitch_kind[c];
    if (out.pitch || out.level) {
        out.available = slot >= 0 && slot < UNITS_PER_CHAIN && param >= 0 && param < d.modules[slot].count;
        if (out.available) {
            int base = d.modules[slot].params[param].base;
            base = out.pitch ? pitch_normal((q15)base, kind, param) : clamp(base, 0, 32767);
            out.base_y = Y + HEIGHT - base * HEIGHT / 32767;
        }
    } else if (mode == 5) out.base_y = Y + HEIGHT / 2;
    if (s.transport_playing && s.sequencer_enabled && s.sequence_step[c] / 16 == out.first / 16)
        out.playhead = s.sequence_step[c] % 16;
    for (int i = 0; i < 16; ++i) {
        auto& p = out.points[i]; const int step = out.first + i;
        const auto meta = s.sequence_meta[c][step];
        p.x = X + DX * i;
        p.trigger = (meta & SeqStep::Trig) != 0;
        p.conditional = (meta & SeqStep::ConditionMask) != 0;
        p.active = step < s.sequence_length[c];
        int v = p.trigger ? 32767 : 0;
        if (out.pitch || out.level) {
            const uint64_t mask = out.pitch ? s.sequence_pitch_lock_mask[c] : s.sequence_value_lock_mask[c];
            p.locked = (mask & (uint64_t(1) << step)) != 0;
            v = out.pitch ? pitch_normal(s.sequence_pitch[c][step], kind, param) : s.sequence_value[c][step];
        } else if (mode == 3) {
            v = ((meta & SeqStep::RetrigMask) >> SeqStep::RetrigShift) * 32767 / 3;
        } else if (mode == 5) {
            const int raw = (meta >> 9) & 0x3f;
            v = raw * 32767 / (2 * SEQ_MICROTIMING_MAX);
        }
        p.y = Y + HEIGHT - clamp(v, 0, 32767) * HEIGHT / 32767;
    }
    return out;
}
} // namespace seance::platform::ui3ds::seq_profile
