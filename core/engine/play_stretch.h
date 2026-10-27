// Bounded waveform-aligned overlap-add for PLAY. Two 64ms triangular grains,
// hop 32ms at 32kHz. The transport anchor is separate from each grain's read
// pitch. No PCM storage, heap, trig, or inferred musical note/root frequency.
// A fixed 49-candidate/16-landmark waveform match reduces periodic grain phase
// cancellation; this is not a phase vocoder or a transient/formant-preserving
// stretcher. Candidate lookaround is only +/-128 source frames (4ms at 1x).
#pragma once
#include "../audio/fixed.h"
#include <cstdint>

namespace seance { namespace play_stretch {
struct State {
    static constexpr int PERIOD = 2048, HOP = PERIOD / 2;
    static constexpr int SEARCH = 128, LANDMARKS = 16;
    int64_t phase[2] = {};
    uint16_t tick = 0;
    bool ready = false;

    void reset() { ready = false; tick = 0; phase[0] = phase[1] = 0; }

    // read() is supplied by PLAY and maps every candidate/interpolation partner
    // into its accepted source window. All work has fixed upper bounds.
    template<class Read>
    static int64_t align(int64_t anchor, int64_t previous, int32_t pitch, Read& read) {
        trackr::fx::q15 reference[LANDMARKS];
        for (int i = 0; i < LANDMARKS; ++i) reference[i] = read(previous + int64_t(i) * 4 * pitch);
        auto error = [&](int offset) -> uint32_t {
            uint32_t result = 0;
            for (int i = 0; i < LANDMARKS; ++i) {
                const int delta = int(read(anchor + int64_t(offset) * 65536 + int64_t(i) * 4 * pitch)) - reference[i];
                result += uint32_t(delta < 0 ? -delta : delta);
            }
            return result;
        };
        int best = 0;
        uint32_t minimum = error(0);
        for (int distance = 8; distance <= SEARCH && minimum; distance += 8) {
            for (int sign = -1; sign <= 1; sign += 2) {
                const int offset = distance * sign;
                const uint32_t candidate = error(offset);
                if (candidate < minimum) { minimum = candidate; best = offset; }
            }
        }
        const int coarse = best;
        for (int offset = coarse - 7; offset <= coarse + 7 && minimum; ++offset) {
            if (offset < -SEARCH || offset > SEARCH || offset == coarse) continue;
            const uint32_t candidate = error(offset);
            if (candidate < minimum) { minimum = candidate; best = offset; }
        }
        return anchor + int64_t(best) * 65536;
    }

    template<class Read>
    trackr::fx::q15 render(int64_t anchor, int32_t pitch, int32_t tempo, Read& read) {
        if (!ready) {
            phase[0] = phase[1] = anchor; tick = 0; ready = true;
        } else if (tick == 0 || tick == HOP) {
            const int head = tick == 0 ? 0 : 1;
            // At equal pitch/tempo, re-anchor exactly (bit-exact normal at 1x
            // from a fresh trigger). On a live rate change grains settle within
            // one period; never bypass stale overlap state conditionally.
            phase[head] = pitch == tempo ? anchor : align(anchor, phase[1-head], pitch, read);
        }
        const int weight = (tick <= HOP ? tick : PERIOD - tick) * (32768 / HOP);
        const int32_t mixed = int32_t(read(phase[0])) * weight + int32_t(read(phase[1])) * (32768 - weight);
        phase[0] += pitch; phase[1] += pitch;
        tick = uint16_t((tick + 1) % PERIOD);
        return trackr::fx::q15(mixed >> 15);
    }
};
static_assert(sizeof(State) <= 32, "bounded PLAY stretch runtime, no source copy");
} } // namespace seance::play_stretch
