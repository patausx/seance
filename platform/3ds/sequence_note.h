#pragma once
// Stored musical values for the score/ARP UI. Never measured audio pitch.
// Imported sample sources are assumed prepared at c4 by the user. Live-input
// transposers (STRN) and compatibility PLAY speed are deliberately not absolute.
#include "../../core/engine/units.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace seance::platform::ui3ds::sequence_note {
inline bool absolute(UnitKind kind, int param) {
    return kind == UnitKind::Osc || kind == UnitKind::Fm || kind == UnitKind::Inst ||
        kind == UnitKind::Bird || kind == UnitKind::Grain || (kind == UnitKind::Pent && param == PentUnit::P_ROOT) ||
        (kind == UnitKind::SamplePlayer && param == PlayUnit::P_NOTE);
}
inline int minimum(UnitKind kind) { return kind == UnitKind::Pent ? 0 : kind == UnitKind::Bird ? 72 : kind == UnitKind::Grain ? 0 : musical_note::MIN; }
inline int maximum(UnitKind kind) { return kind == UnitKind::Pent ? 120 : kind == UnitKind::Bird ? 114 : kind == UnitKind::Grain ? 84 : musical_note::MAX; }
inline int midi(UnitKind kind, q15 raw) {
    if (kind == UnitKind::Pent) return PentUnit::root_note(raw);
    if (kind == UnitKind::Bird)
        return int(std::lround(69.0 + 12.0 * std::log2(double(BirdUnit::pitch_millihz(raw)) / 440000.0)));
    if (kind == UnitKind::Grain)
        return int(std::lround(60.0 + 12.0 * std::log2(double(std::max(128, std::abs(int(raw)))) / 8192.0)));
    return kind == UnitKind::Inst ? InstUnit::note_midi(raw) : musical_note::decode(raw);
}
inline void name(char* out,size_t size,int note) {
    static const char* names[]={"c","c#","d","d#","e","f","f#","g","g#","a","a#","b"};
    int pitch=note%12,octave=note/12-1;if(pitch<0){pitch+=12;--octave;}
    std::snprintf(out,size,"%s%d",names[pitch],octave);
}
inline q15 value(UnitKind kind, int note, q15 previous = 0) {
    note = std::max(minimum(kind), std::min(maximum(kind), note));
    if (kind == UnitKind::Pent) return PentUnit::root_value(note);
    if (kind == UnitKind::Bird) {
        const double target = 440000.0 * std::pow(2.0, double(note - 69) / 12.0);
        int lo = 0, hi = 32767;
        while (lo < hi) { const int mid = (lo + hi) / 2;
            if (BirdUnit::pitch_millihz(q15(mid)) < target) lo = mid + 1; else hi = mid; }
        if (lo && std::abs(double(BirdUnit::pitch_millihz(q15(lo-1))) - target) <
                  std::abs(double(BirdUnit::pitch_millihz(q15(lo))) - target)) --lo;
        return q15(lo);
    }
    if (kind == UnitKind::Grain) {
        const int rate = std::min(32767, int((musical_note::ratio_q16(note - 60) + 4) / 8));
        return q15(previous < 0 ? -rate : rate);
    }
    return kind == UnitKind::Inst ? InstUnit::note_value(note) : musical_note::encode(note);
}
// The selected-step projection must use stored locks, never the playing voice's
// effective pitch/ARP offset. Geometry stays in the existing exact-window helper.
inline PlayWindow play_window(const trackr::synth::Sample& sample, const q15* values,
                              bool quantize, bool modern, const PlayEditState& edit,
                              PlayUnit::PitchMode mode, MusicalScale scale = {}) {
    auto window=seance::play_edit_window(sample,values[PlayUnit::P_SPEED],values[PlayUnit::P_START],
        values[PlayUnit::P_LEN],values[PlayUnit::P_SLICE],quantize,modern,edit,mode==PlayUnit::PitchMode::Legacy?scale:MusicalScale{});
    if(mode==PlayUnit::PitchMode::Legacy||values[PlayUnit::P_SCRUB]>16383)return window;
    const bool reverse=window.slice&&(sample.slice_reverse_mask&(1u<<(window.slice-1)));
    const uint16_t cut=window.slice?sample.slice_speed_q8[window.slice-1]:256;
    window.increment=PlayUnit::projection_increment(mode,values[PlayUnit::P_SPEED],values[PlayUnit::P_NOTE],reverse,cut);
    return window;
}
} // namespace seance::platform::ui3ds::sequence_note
