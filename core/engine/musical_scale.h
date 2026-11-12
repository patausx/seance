#pragma once
// Project-wide note vocabulary. Editing never rewrites stored notes; DSP uses
// this only where an existing qnt switch opts in. No clocks, heap, PCM or libm.
#include <cstdint>

namespace seance {
enum class ScaleMode : uint8_t {
    Chromatic = 0, Major, Minor, Dorian, Phrygian, Mixolydian, HarmonicMinor,
    MajorPentatonic, MinorPentatonic, Hirajoshi, Count
};
struct MusicalScale { uint8_t tonic = 0; ScaleMode mode = ScaleMode::Chromatic; };
namespace musical_scale {
inline bool valid(MusicalScale s) { return s.tonic < 12 && uint8_t(s.mode) < uint8_t(ScaleMode::Count); }
inline bool equal(MusicalScale a, MusicalScale b) { return a.tonic == b.tonic && a.mode == b.mode; }
inline bool active(MusicalScale s) { return valid(s) && s.mode != ScaleMode::Chromatic; }
inline const char* tonic_name(int tonic) {
    static constexpr const char* names[] = {"c","c#","d","d#","e","f","f#","g","g#","a","a#","b"};
    return names[tonic >= 0 && tonic < 12 ? tonic : 0];
}
inline const char* name(ScaleMode mode) {
    static constexpr const char* names[] = {"chromatic","major","minor","dorian","phrygian","mixolydian",
        "harmonic minor","major pent","minor pent","hirajoshi"};
    return names[uint8_t(mode) < uint8_t(ScaleMode::Count) ? uint8_t(mode) : 0];
}
inline const char* short_name(ScaleMode mode) {
    return mode == ScaleMode::Chromatic ? "chrom" : mode == ScaleMode::HarmonicMinor ? "harm minor" : name(mode);
}
inline uint16_t mask(ScaleMode mode) {
    static constexpr uint16_t masks[] = {
        0xfff,
        (1<<0)|(1<<2)|(1<<4)|(1<<5)|(1<<7)|(1<<9)|(1<<11),
        (1<<0)|(1<<2)|(1<<3)|(1<<5)|(1<<7)|(1<<8)|(1<<10),
        (1<<0)|(1<<2)|(1<<3)|(1<<5)|(1<<7)|(1<<9)|(1<<10),
        (1<<0)|(1<<1)|(1<<3)|(1<<5)|(1<<7)|(1<<8)|(1<<10),
        (1<<0)|(1<<2)|(1<<4)|(1<<5)|(1<<7)|(1<<9)|(1<<10),
        (1<<0)|(1<<2)|(1<<3)|(1<<5)|(1<<7)|(1<<8)|(1<<11),
        (1<<0)|(1<<2)|(1<<4)|(1<<7)|(1<<9),
        (1<<0)|(1<<3)|(1<<5)|(1<<7)|(1<<10),
        (1<<0)|(1<<2)|(1<<3)|(1<<7)|(1<<8)
    };
    return masks[uint8_t(mode) < uint8_t(ScaleMode::Count) ? uint8_t(mode) : 0];
}
inline bool contains(MusicalScale s, int note) {
    if (!valid(s)) return true; // defensive bypass; setters/loaders refuse invalid settings
    int pc = note % 12; if (pc < 0) pc += 12;
    return (mask(s.mode) & (1u << ((pc + 12 - s.tonic) % 12))) != 0;
}
// Nearest allowed note in the caller's actual pitch range. Exact ties choose
// the lower note. Search at most 25 integers, including negative MIDI offsets.
inline int snap_cents(MusicalScale s, int64_t cents, int low, int high) {
    if (high < low) return low;
    if (cents < int64_t(low)*100) cents = int64_t(low)*100;
    if (cents > int64_t(high)*100) cents = int64_t(high)*100;
    int center = int(cents / 100);
    int first = center - low < 12 ? low : center - 12;
    int last = high - center < 12 ? high : center + 12;
    int best = center; int64_t distance = INT64_MAX;
    for (int note = first; note <= last; ++note) if (contains(s, note)) {
        int64_t d = cents - int64_t(note)*100; if (d < 0) d = -d;
        if (d < distance) { best = note; distance = d; }
    }
    return best;
}
inline int snap_note(MusicalScale s, int note, int low, int high) {
    return snap_cents(s, int64_t(note)*100, low, high);
}
// A directional edit goes to the NEXT degree (also from an old off-scale note),
// not nearest+one. At an endpoint it retains the previous authored value.
inline int next_note(MusicalScale s, int note, int direction, int low, int high) {
    if (!direction || high < low) return note;
    const int step = direction > 0 ? 1 : -1;
    for (int d = 1; d <= 12; ++d) {
        const int64_t next = int64_t(note) + step*d;
        if (next < low || next > high) break;
        if (contains(s, int(next))) return int(next);
    }
    return note;
}
} // namespace musical_scale
} // namespace seance
