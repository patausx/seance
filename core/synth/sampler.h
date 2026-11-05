// sampler: plays back a chunk of q15 samples with pitch-shift and loops
// samples are stored in SampleBank, the voice references an index
//
// supports:
//   - mono or stereo (interleaved L/R)
//   - hermite cubic interpolation (4-point Catmull-Rom)
//   - full ADSR envelope (exp release)
//   - loop crossfade (256 frames)
//   - reverse playback
//   - fine-tune cents and per-voice gain
//   - exponential velocity curve
#pragma once
#include "../audio/voice.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace trackr::synth {

constexpr int SAMPLER_SR = 32000;

// one sample in the bank. q15 PCM, mono or stereo (interleaved L/R)
struct Sample {
    std::vector<fx::q15> data;      // mono: data[frame], stereo: data[frame*2+ch]
    uint8_t channels = 1;           // 1 = mono, 2 = stereo
    int root_note = 60;             // the note at which the sample sounds "normal"
    uint32_t loop_start = 0;        // in frames (not samples!)
    uint32_t loop_end   = 0;        // 0 = no loop
    bool reversed = false;          // not used directly - playback is controlled by SamplerParams::reverse
    // Physical storage may be a preallocated REC workspace. UINT32_MAX means
    // every physical frame is playable (ordinary decoded/imported samples).
    uint32_t logical_frames = UINT32_MAX;

    // chop points - up to 16 markers in frames. 0xFFFFFFFF = empty
    static constexpr int MAX_CHOPS = 16;
    uint32_t chops[MAX_CHOPS] = {
        0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,
        0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,
        0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,
        0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,
    };
    // Per-interval direction follows the chop that begins that interval. Keeping
    // this beside chops makes aliases agree and lets seam insertion/removal shift
    // one compact bitset instead of touching PCM.
    uint16_t slice_reverse_mask = 0;
    // Stable cut-id varispeed multiplier; reverse remains a separate bit.
    static constexpr uint16_t CUT_SPEED_MIN = 64, CUT_SPEED_ONE = 256, CUT_SPEED_MAX = 1024;
    uint16_t slice_speed_q8[MAX_CHOPS] = {
        256,256,256,256,256,256,256,256,256,256,256,256,256,256,256,256
    };
    static uint16_t cut_speed(uint16_t value) {
        return value < CUT_SPEED_MIN || value > CUT_SPEED_MAX ? CUT_SPEED_ONE : value;
    }
    void reset_cut_speeds() { for (auto& speed : slice_speed_q8) speed = CUT_SPEED_ONE; }
    // Independent musical pitch, in signed semitones, stable cut-id; never q8 tempo.
    int8_t slice_pitch_semitones[MAX_CHOPS] = {};
    void reset_cut_pitches() { for (auto& pitch : slice_pitch_semitones) pitch = 0; }
    // Explicit exact-editor/export decode policy; source-wide and persisted in
    // optional SNC7 SEXT. False retains historical native endpoint behavior.
    bool exact_native_endpoint = false;

    // helpers
    uint32_t num_frames() const {
        return channels ? (uint32_t)(data.size() / channels) : 0;
    }
    uint32_t playable_frames() const {
        const uint32_t physical = num_frames();
        return logical_frames == UINT32_MAX || logical_frames > physical ? physical : logical_frames;
    }
    bool empty() const { return playable_frames() == 0; }
    bool has_storage() const { return !data.empty(); }
};

// sample bank - 64 slots, any of which may be empty
constexpr int SAMPLE_BANK_SIZE = 64;
enum class SampleOrigin : uint8_t { Runtime = 0, SdWav, Recording };
enum class SampleEditResult : uint8_t {
    Ok = 0, NoFreeSlot, MemoryBudget, SharedSlot, InvalidSlot, Busy
};

// Persistent provenance follows the SampleBank slot, not an individual PLAY.
// Multiple units may deliberately share one slot and therefore one source file.
struct SampleProvenance {
    SampleOrigin origin = SampleOrigin::Runtime;
    bool missing = false;
    char path[192] = {};
};

class SampleBank {
public:
    // 12 MiB leaves headroom for Engine/UI/decoder workspace on Old 3DS while
    // allowing nine full 20-second mono takes at 32 kHz.
    static constexpr std::size_t DEFAULT_PCM_BUDGET_BYTES = 12u * 1024u * 1024u;

    Sample& slot(int i) { return samples_[i]; }
    const Sample& slot(int i) const { return samples_[i]; }
    SampleProvenance& provenance(int i) { return provenance_[i]; }
    const SampleProvenance& provenance(int i) const { return provenance_[i]; }
    void set_provenance(int i, SampleOrigin origin, const char* path, bool missing = false);
    std::size_t pcm_bytes_used() const;
    std::size_t pcm_budget_bytes() const { return pcm_budget_bytes_; }
    void set_pcm_budget_for_test(std::size_t bytes) { pcm_budget_bytes_ = bytes; }
    bool can_replace(int slot, std::size_t elements) const;
    // Aggregate projection for distinct slot replacements. Unlike repeated
    // can_replace(), this accounts for all staged vectors simultaneously.
    bool can_replace_many(const int* slots, const Sample* samples, int count) const;
    bool can_replace_all(const Sample* samples, const bool* ready, int count) const;
    SampleEditResult replace_sample(int slot, Sample&& sample, SampleOrigin origin,
                                    const char* path = "", bool missing = false);
    // No-fail/non-allocating primitive after an aggregate preflight. `staged`
    // receives the retired live sample for destruction after the audio guard.
    void swap_sample_unchecked(int slot, Sample& staged, SampleOrigin origin,
                               const char* path = "", bool missing = false);
    // No-fail after can_replace_all(): swaps every slot so staged owns the old
    // bank and can destroy its large vectors after the platform guard is gone.
    void swap_all(Sample* staged, int count);
    void clear_slot(int slot);
    uint32_t generation(int slot) const { return slot >= 0 && slot < SAMPLE_BANK_SIZE ? generation_[slot] : 0; }
    static SampleBank& instance() { static SampleBank b; return b; }
private:
    Sample samples_[SAMPLE_BANK_SIZE];
    SampleProvenance provenance_[SAMPLE_BANK_SIZE];
    uint32_t generation_[SAMPLE_BANK_SIZE] = {};
    std::size_t pcm_budget_bytes_ = DEFAULT_PCM_BUDGET_BYTES;
};

// playback mode (M8-style). loop/reverse below are kept in sync with this
// in note_on, but play_mode is the source of truth for the engine.
enum class PlayMode : uint8_t {
    Fwd = 0,    // play once forward
    Rev,        // play once backward
    FwdLoop,    // loop forward
    RevLoop,    // loop backward
    Repitch,    // forward, slice stretched to note length (reserved; behaves as Fwd for now)
    Count
};

inline const char* play_mode_name(PlayMode m) {
    switch (m) {
        case PlayMode::Fwd:     return "FWD";
        case PlayMode::Rev:     return "REV";
        case PlayMode::FwdLoop: return "FWDLOOP";
        case PlayMode::RevLoop: return "REVLOOP";
        case PlayMode::Repitch: return "REPITCH";
        default:                return "?";
    }
}

struct SamplerParams {
    int sample_slot = 0;
    fx::q15 start  = 0;             // start position [0, Q15_ONE]
    fx::q15 length = fx::Q15_ONE;   // playback length
    bool loop = false;
    bool reverse = false;           // play backwards

    // M8-style play mode. source of truth; loop/reverse are derived in note_on.
    PlayMode play_mode = PlayMode::Fwd;
    // active slice: 0 = whole sample; 1..MAX_CHOPS = play chop[slice-1]..next.
    // when the instrument is set to "chromatic slices" the played note picks the
    // slice instead (handled in note_on via slice_from_note).
    uint8_t  slice = 0;
    bool     chromatic_slices = false;  // true: note (from root) selects the slice

    int8_t fine_cents = 0;          // -50..+50 cent offset
    fx::q15 gain = fx::Q15_ONE;     // per-voice trim

    // full ADSR (like wavsynth)
    uint32_t attack  = 0;           // in frames
    uint32_t decay   = 0;           // in frames
    fx::q15  sustain = fx::Q15_ONE; // sustain level (q15)
    uint32_t release = 4000;        // in frames
};

class Sampler : public audio::Voice {
public:
    void note_on(int note, int velocity) override;
    void note_off() override;
    bool render(fx::q15* out, std::size_t frames) override;

    int current_frame() const override { return (int)pos_hi_; }
    int current_sample_slot() const override { return params.sample_slot; }

    SamplerParams params;

private:
    // position in the source sample
    fx::uq16 pos_lo_  = 0;          // fractional part of the position (q16)
    int64_t  pos_hi_  = 0;          // integer part (frame index, signed for reverse)
    int32_t  pos_inc_ = 1 << 16;    // speed in q16.16, signed (negative for reverse)

    fx::q15  velocity_ = fx::Q15_ONE;
    bool     gated_ = false;

    // ADSR state
    enum class Stage : uint8_t { Idle, Attack, Decay, Sustain, Release };
    Stage   stage_ = Stage::Idle;
    fx::q31 env_   = 0;             // q31 amplitude envelope for smoothness
    uint32_t stage_pos_ = 0;
    fx::q31 release_start_env_ = 0; // remember the env at the moment of transition into release

    // mirrors of the bounds (computed in note_on so render doesn't recompute every sample)
    uint32_t play_start_  = 0;      // start frame
    uint32_t play_end_    = 0;      // end frame (exclusive)
    bool     loop_active_ = false;
    uint32_t loop_start_  = 0;
    uint32_t loop_end_    = 0;
};

} // namespace trackr::synth
