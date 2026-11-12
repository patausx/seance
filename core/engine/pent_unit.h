// PENT: five continuously running voices, delayed PM on the five star edges.
// Config and twelve ordinary ParamBank entries persist; all DSP/visual clocks do not.
#pragma once
#include "unit.h"
#include <type_traits>

namespace seance {

class PentUnit : public Unit {
public:
    static constexpr int VOICES = 5;
    enum { P_ROOT = 0, P_SPREAD, P_BOND, P_DRIFT, P_DRIVE, P_GAIN,
           P_RATIO_A, P_RATIO_B, P_RATIO_C, P_RATIO_D, P_RATIO_E, P_DRIFT_DEPTH, P_COUNT };
    enum class Width : uint8_t { Mono = 0, Stereo = 1 };
    // Default mono keeps the existing mono effects available. Stereo is explicit;
    // Engine rejects an unsupported stereo chain instead of folding/bypassing it.
    Width width = Width::Mono;
    static constexpr q15 DEFAULT_SPREAD = 11796; // 18 / 50 cents
    static constexpr q15 DEFAULT_BOND = 20316;   // 62%, maximum PM depth 1.25 radians
    static constexpr q15 DEFAULT_DRIFT = 3277;   // 0.2 / 2 Hz
    static constexpr q15 DEFAULT_DRIVE = 7864;   // 24%, oscillator shaping
    static constexpr q15 DEFAULT_DRIFT_DEPTH = 3932; // +/-3 / 25 cents

    struct Visual {
        uint16_t phase[VOICES] = {};       // actual oscillator phase, not a UI timer
        uint16_t drift_phase[VOICES] = {}; // actual slow frequency-modulation phases
        uint16_t level[VOICES] = {};       // measured voice activity, 0..32767
        uint16_t bond[VOICES] = {};        // measured PM excursion, 0..32767
        uint32_t frequency_millihz[VOICES] = {}; // applied, including safety ceiling
        q15 effective[P_COUNT] = {};       // controls accepted by the rendered source
        uint64_t frames = 0;
        bool ready = false, audible = false, limited = false;
    };

    PentUnit();
    void process(q15* buf, int frames) override;
    bool supports_stereo() const override { return true; }
    bool process_stereo(q15* buf, int frames) override;
    uint8_t output_channels(uint8_t) const override { return width == Width::Stereo ? 2 : 1; }
    void control_tick() override;
    void trigger() override {} // continuous: a score step never resets phase/feedback
    void reset() override;
    bool set_width(Width value);
    SequencerProperties sequencer_properties() const override {
        return {true, false, SequenceLifecycle::Flow, P_ROOT, P_GAIN, P_BOND, -1};
    }
    Visual visual() const;
    // Shared scalar mappings for DSP, editor, locks and sequencer. ROOT has its
    // own MIDI0..120 q15 projection; legacy note domains remain unchanged.
    // Ratios range 1/4..4 with exact rational anchors.
    static int root_note(q15 value);
    static q15 root_value(int midi);
    static uint32_t root_millihz(q15 value);
    static uint32_t ratio_q16(q15 value);
    static q15 ratio_value(uint32_t numerator, uint32_t denominator = 1);
    static int spread_centicents(q15 value);
    static uint32_t drift_millihz(q15 value);
    static int drift_depth_centicents(q15 value);
    static uint16_t percent(q15 value);

private:
#ifdef SEANCE_PENT_TEST
    friend struct PentDspTest; // scalar oracle inspects real state; no release hook or alternate DSP
#endif
    static constexpr int FIR_TAPS = 47, RAMP_SAMPLES = 64;
    struct Ramp {
        int32_t value = 0, target = 0, step = 0;
        uint8_t remaining = 0;
        void aim(int32_t next, bool snap);
        int32_t tick();
    } controls_[P_COUNT], increment_[VOICES], bond_turns_, drift_increment_;
    uint32_t phase_[VOICES] = {}, drift_phase_[VOICES] = {};
    int32_t previous_[VOICES] = {}; // unscaled shaped voices, never post-gain feedback
    q15 fir_[2][FIR_TAPS] = {}; // mono is its own direct sum, not a stereo fold
    uint8_t fir_head_ = 0, control_clock_ = 0;
    bool configured_ = false, limited_ = false;
    Visual visual_{};
    uint32_t applied_increment_[VOICES] = {}, ratio_[VOICES] = {};
    int spread_ = 0, drift_depth_ = 0; // static control mappings cached until their raw target changes
    void configure(); // own 32-frame audio clock, independent of process partitions
    void synthesize(int32_t (&sum)[2], int channels);
    void render(q15* buf, int frames, int channels);
};

static_assert(PentUnit::P_COUNT == PARAM_BANK_SIZE, "PENT fits the fixed twelve-param bank");
static_assert(std::is_trivially_copyable<PentUnit::Visual>::value, "guard-copyable PENT visual DTO");
static_assert(sizeof(PentUnit::Visual) <= 160, "bounded controller-only PENT diagnostics");

} // namespace seance
