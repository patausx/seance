#pragma once
// Independent output-only voices. No Engine, sample bank, file I/O or allocation.
#include <cstddef>
#include <cstdint>
#include "startup_reveal.h"

namespace seance::platform::menu_sound {
constexpr unsigned kRate = 32000;
constexpr float kDroneGain = 0.22f; // about -13 dB; source RMS -> about -32 dBFS
constexpr float kEntryGain = 0.28f;
constexpr float kDuckedGain = 0.035f; // another -18 dB when the instrument sounds
struct Clip {
    const int16_t* pcm = nullptr; // immutable interleaved stereo at kRate
    size_t frames = 0;
    bool valid() const { return pcm && frames; }
};

class Player {
public:
    Player(Clip drone, Clip entry) : drone_(drone), entry_(entry), entry_pos_(entry.frames) {}
    // One UI-owned command word: flags low3, success-event serial in remaining bits.
    // Stable commands never retrigger. Disabled events are consumed, not deferred.
    void command(uint32_t command) {
        const bool enabled = command & 1u, menu = command & 2u;
        const uint32_t serial = command >> 3;
        if (serial != serial_) {
            serial_ = serial;
            if (enabled && !menu && entry_.valid() && entry_pos_ >= entry_.frames) {
                entry_pos_ = 0;
                ducked_ = (command & 4u) != 0;
                entry_gain_.value = 0;
                entry_gain_.target = 0;
            }
        }
        if (command & 4u) ducked_ = true; // do not swell a dying tail between notes
        drone_gain_.to(enabled && menu && drone_.valid() ? kDroneGain : 0,
                       enabled && menu ? startup_reveal::kDurationMs : enabled ? 900 : 180,
                       enabled && menu);
        entry_gain_.to(enabled && entry_pos_ < entry_.frames
                           ? (ducked_ ? kDuckedGain : kEntryGain) : 0,
                       !enabled ? 180 : ducked_ ? 40 : 12);
    }
    bool active() const {
        return drone_gain_.active() || (entry_pos_ < entry_.frames && entry_gain_.active());
    }
    void render(int16_t* out, size_t frames) {
        for (size_t i = 0; i < frames; ++i) {
            const float dg = drone_gain_.next(), eg = entry_gain_.next();
            const bool drone = drone_.valid() && dg > 0;
            const bool entry = entry_.valid() && entry_pos_ < entry_.frames;
            for (unsigned c = 0; c < 2; ++c) {
                const float value = (drone ? drone_.pcm[2*drone_pos_+c] * dg : 0) +
                                    (entry ? entry_.pcm[2*entry_pos_+c] * eg : 0);
                const int sample = static_cast<int>(value);
                out[2*i+c] = int16_t(sample < -32768 ? -32768 : sample > 32767 ? 32767 : sample);
            }
            if (drone && ++drone_pos_ == drone_.frames) drone_pos_ = 0;
            if (entry) {
                ++entry_pos_;
                if (!entry_gain_.active()) entry_pos_ = entry_.frames; // mute cancels, never resumes
                if (entry_pos_ == entry_.frames) entry_gain_ = {};
            }
        }
    }
private:
    struct Gain {
        float value = 0, target = 0, step = 0;
        float from = 0, phase = 0, phase_step = 0;
        bool smooth = false;
        unsigned remaining = 0;
        bool active() const { return value > 0 || target > 0; }
        void to(float dest, unsigned ms, bool eased = false) {
            if (target == dest) return;
            target = dest;
            remaining = kRate * ms / 1000;
            step = (target - value) / remaining;
            from = value; phase = 0; phase_step = 1.0f / remaining; smooth = eased;
        }
        float next() {
            if (remaining) {
                if (smooth) {
                    phase += phase_step;
                    const float t = phase < 1 ? phase : 1;
                    value = from + (target-from) * (t*t*(3-2*t));
                } else value += step;
                if (!--remaining) value = target;
            }
            return value;
        }
    };
    Clip drone_, entry_;
    size_t drone_pos_ = 0, entry_pos_;
    uint32_t serial_ = 0;
    bool ducked_ = false;
    Gain drone_gain_, entry_gain_;
};
} // namespace seance::platform::menu_sound
