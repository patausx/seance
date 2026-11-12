#pragma once
// Production source/FX only. Shape mechanics and hold ownership live outside Audio.
#include "relic_types.h"
#include <array>

namespace seance::relic {
constexpr int MAX_AUDIO_HEADS = 64;
struct Energy {
    float rms = 0, mid = 0, side = 0, low = 0, high = 0, dry = 0, wet = 0;
};
struct AudioMap {
    float body = 1, memory = .015f, detail = .008f;
    float memory_window = 4.8f, detail_window = .18f, space_send = .20f, space_return = .16f;
};
struct Reader {
    uint32_t id = 0, source_generation = 0;
    uint8_t layer = 0;
    float phase = 0, envelope = 0, energy = 0; // preceding completed 320-frame mean square
    float pan = 0, rate = 1, position = 0, window = 0; // position/window in seconds
    uint32_t frame = 0; // actual original source frame, not a display surrogate
};
struct Visual {
    uint64_t sample_clock = 0;
    Features features{}; // installed model publication, never a DSP hold clamp
    AudioMap map{};
    Energy energy{}; // actual preceding completed 320-frame output, including transport
    std::array<Reader, MAX_AUDIO_HEADS> readers{};
    uint32_t source_frames = 0, source_first = 0, source_end = 0, source_generation = 0;
    uint32_t births = 0, retirements = 0, refused_births = 0, clipped_samples = 0;
    uint16_t reader_count = 0, control_phase = 0, ramp_frames = 0, declick_frames = 0;
    uint8_t source_channels = 0;
    float depth = 1;
    bool ready = false, audio_available = false, playing = false, running = false;
    bool active = true, control_pending = false;
};
static_assert(sizeof(Reader) <= 44 && sizeof(Visual) <= 3072, "bounded pointer-free relic audio observation");

class Audio {
public:
    Audio() noexcept;
    ~Audio() noexcept;
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;
    bool ready() const noexcept;
    // Caller serializes every method at its audio/control boundary. No internal lock.
    // Borrowed immutable PCM; validate capacity, channels and [first,end) BEFORE mutation.
    // Canonical missing {nullptr,0,0,0,0,generation,0|1|2} is valid silent control.
    // Empty / one-frame regions bind successfully but are silent; no padding/downmix.
    // Full [0,320000) region explicitly retains selected 5s/.8..9.2s reader arithmetic.
    // Other regions use their midpoint, SAME seconds jitter/rates/windows; shorten
    // windows only as travel safety demands, minimum 2. Endpoint taps clamp, never
    // pad/resample source storage. A 2-frame Hann window has zero endpoint energy.
    // Same exact binding is a no-op; any changed identity retires ALL heads first.
    bool set_source(const Source&) noexcept;
    // Call BEFORE freeing borrowed PCM. Bounded tank clear (64 cells/sample), not a
    // tank memset. Rebinding/detach declick only from held last L/R, never old PCM.
    void detach_source() noexcept;
    // Finite features clamp independently to pull/compress [0,1], bend/wave [-1,1].
    // Invalid/nonfinite input or depth outside (0,1] fails without changing ANY state.
    // At phase 0: install immediately. Otherwise: bounded last-wins pending publication
    // installed after the next completed 320-frame tick. From that boundary the SAME
    // selected map/bend/center interpolation lasts 320 samples. Another phase-zero
    // setter before rendering replaces ONLY that target, retaining the actual prior
    // interpolation origin (including a just-installed pending publication).
    // No model stepping or
    // hold clamp here; held edits must already be published by the model. Transport
    // playing/active follow this same <=10ms control contract, even with 1kHz setters.
    bool set_shape(Features, float depth, bool playing, bool active = true) noexcept;
    void render(q15* interleaved, int frames) noexcept;
    void snapshot(Visual&) const noexcept;
    // Retains source binding, resets shape/transport/clock/RNG/counters, invalidates
    // heads, clears tank incrementally. No allocation, no full-size Impl temporary.
    void reset() noexcept;
private:
    struct Impl;
    Impl* impl_ = nullptr; // exactly one fixed DSP allocation, construction only
};
// Include relic_audio_impl.inc in ONE implementation TU (normally units.cpp).
// Header + implementation in one native TU preserves explicit historical link recipes.
} // namespace seance::relic
