// Absolute PLAY notes: users prepare samples at c4. No pitch detection or sample
// root metadata. The parameter range is c1..c7; ARP may extend final MIDI to 0..120.
#pragma once
#include "../audio/fixed.h"
#include <cstddef>
#include <cstdint>

namespace seance { namespace musical_note {
constexpr int MIN = 24, MAX = 96, ROOT = 60;
inline trackr::fx::q15 encode(int midi) {
    midi = midi < MIN ? MIN : midi > MAX ? MAX : midi;
    return (trackr::fx::q15)(((midi - MIN) * 32767 + (MAX - MIN) / 2) / (MAX - MIN));
}
inline int decode(trackr::fx::q15 value) {
    const int v = value < 0 ? 0 : value;
    return MIN + (v * (MAX - MIN) + 16383) / 32767;
}
inline void name(char* out, size_t size, int midi) {
    if (!out || !size) return;
    midi = midi < 0 ? 0 : midi > 127 ? 127 : midi;
    static const char* const names[12] = {"c","c#","d","d#","e","f","f#","g","g#","a","a#","b"};
    char label[5]; size_t length = 0;
    label[length++] = names[midi % 12][0];
    if (names[midi % 12][1]) label[length++] = '#';
    const int octave = midi / 12 - 1;
    if (octave < 0) label[length++] = '-';
    label[length++] = char('0' + (octave < 0 ? -octave : octave));
    size_t copied = 0;
    while (copied < length && copied + 1 < size) { out[copied] = label[copied]; ++copied; }
    out[copied] = '\0';
}
// Rounded 65536 * 2^(semitones/12), literal/immutable: no lazy libm work on the
// audio thread. Final clamp is deliberate even for hostile external callers.
inline int32_t ratio_q16(int semitones) {
    static constexpr int32_t ratios[121] = {
        2048,2170,2299,2435,2580,2734,2896,3069,3251,3444,3649,3866,
        4096,4340,4598,4871,5161,5468,5793,6137,6502,6889,7298,7732,
        8192,8679,9195,9742,10321,10935,11585,12274,13004,13777,14596,15464,
        16384,17358,18390,19484,20643,21870,23170,24548,26008,27554,29193,30929,
        32768,34716,36781,38968,41285,43740,46341,49097,52016,55109,58386,61858,
        65536,69433,73562,77936,82570,87480,92682,98193,104032,110218,116772,123715,
        131072,138866,147123,155872,165140,174960,185364,196386,208064,220436,233544,247431,
        262144,277732,294247,311744,330281,349920,370728,392772,416128,440872,467088,494862,
        524288,555464,588493,623487,660561,699841,741455,785544,832255,881744,934175,989724,
        1048576,1110928,1176987,1246974,1321123,1399681,1482910,1571089,1664511,1763488,1868350,1979448,
        2097152
    };
    const int n = semitones < -60 ? -60 : semitones > 60 ? 60 : semitones;
    return ratios[n + 60];
}
} } // namespace seance::musical_note
