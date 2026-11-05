// units.h - the first wave of chain cards.
//
// PLAY  sample player: varispeed (+/-2 oct), start/len window, loop, retrig
// GRAN  poor man's granular: two overlapped PLAY heads with random position spray
// OSC   sine/tri/saw/pulse, pitch as q15 -> midi range
// FILT  svf wrapper (cutoff/res/type)
// VCA   gain + optional AD envelope fired by trigger()
// DLY   mono delay, time/feedback/mix (heap buffer, max 1s)
// FOLD  wavefolder drive + output trim
#pragma once
#include "unit.h"
#include "inst_unit.h"
#include "scratch_unit.h"
#include "paul_unit.h"
#include "play_mode.h"
#include "play_window.h"
#include "musical_note.h"
#include "play_stretch.h"
#include "../dsp/filter.h"
#include "../dsp/shift_ssb.h"
#include "../synth/sampler.h"     // Sample / SampleBank donors from descry

namespace seance {

namespace reverb_dsp { struct State; }

// Event module: audio passes unchanged. Capture Visual under the audio guard.
class ArpUnit : public Unit {
public:
    enum { P_RATE=0, P_GATE, P_ORDER, P_OCTAVES, P_RESET, P_PLAY_MODE,
           P_NOTE1, P_NOTE2, P_NOTE3, P_NOTE4, P_COUNT };
    static constexpr int RATE_COUNT=12, ORDER_COUNT=6, NOTE_OFF=-127, MAX_NOTES=24;
    enum class State : uint8_t { Stopped, NoTarget, AllOff, Running, Bypassed, Muted, Conflict };
    struct Visual {
        int8_t notes[MAX_NOTES] = {};
        uint8_t count = 0;
        int8_t current = -1, semitone = 0, target_slot = -1;
        bool active = false, gate = false;
        State state = State::Stopped;
        uint32_t events = 0; // accepted source notes since runtime reset
    };
    // Authored editor recall only. OFF remains the original raw zero; neither
    // modulation nor the note-event engine reads this metadata.
    struct NoteRecall { q15 values[4] = {}; uint8_t mask = 0; } note_recall;
    ArpUnit();
    void process(q15*, int) override {} // genuine pass-through, no pitch DSP
    void control_tick() override;
    void trigger() override;
    void reset() override;
    Visual visual() const { return visual_; }
    static bool supports(UnitKind kind);
    static bool accepts(const Unit* unit); // runtime eligibility (MOTOR requires modern note mode)
    static int rate_index(q15 value);
    static q15 rate_value(int index);
    static const char* rate_name(int index);
    static int gate_percent(q15 value);
    static q15 gate_value(int percent);
    static int order_index(q15 value);
    static q15 order_value(int index);
    static const char* order_name(int index);
    static int octaves(q15 value);
    static q15 octaves_value(int octaves);
    static bool reset_on_trigger(q15 value) { return value > 16383; }
    static q15 reset_value(bool on) { return on ? 32767 : 0; }
    static bool play_legato(q15 value) { return value > 16383; }
    static q15 play_mode_value(bool legato) { return legato ? 32767 : 0; }
    static int note_semitones(q15 value);
    static q15 note_value(int semitones);
private:
    friend class Engine;
    Visual visual_{};
    int8_t candidates_[12] = {};
    uint8_t candidate_count_ = 0;
    int order_ = -1, rate_ = -1;
    bool configured_ = false, restart_ = false, started_ = false, voice_started_ = false;
    bool pending_cycle_ = false; // latest pool/order is committed only on a note edge
    q15 config_[P_COUNT] = {};
    uint32_t rng_ = 0x41525031u, phase_remainder_ = 0;
    uint64_t next_phase_ = 0, gate_phase_ = 0;
    void make_cycle();
    uint32_t random();
    uint64_t interval();
    void suspend(State state, int target);
    bool advance(uint64_t phase, int target);
    uint64_t deadline() const;
};

// ---------- sources ----------

class PlayUnit : public Unit {
public:
    PlayUnit();
    void process(q15* buf, int frames) override;
    void control_tick() override;
    void trigger() override;
    // Audition displayed trim/cut without changing any Param or override layer.
    // Uses current speed/gain; one pass, no sequencer gate, including legacy PLAY.
    void preview(q15 start, q15 length, q15 slice);
    // Explicit existing-voice preview, never an independent bus. Caller owns
    // transport/recording/mute policy and must hold the audio guard.
    bool preview_frames(uint32_t start, uint32_t end, bool repeat, uint32_t expected_generation,
                        bool reverse = false, uint16_t speed_q8 = 256, bool scrub = false,
                        int8_t part_pitch = 0);
    // Nonallocating active-preview update. Keeps fractional progression and amp
    // when still inside the selection; clamps escaped heads with existing ramp.
    // Explicit independent semitone pitch; same model as actual musical playback.
    bool update_preview_frames(uint32_t first, uint32_t end, bool reverse, uint16_t rate,
                               int8_t part_pitch = 0);
    // Continuous private target, not a seek/retrigger. Does not edit any Param.
    // initialize places a new touch (silent in scratch, attack-ramped in hold), never drag updates.
    bool preview_scrub(q15 position, bool initialize = false);
    // Live editor cut multiplier. Never resets phase, fades or preview ownership.
    bool set_preview_speed(uint16_t rate) {
        if (!preview_ || !playing_ || !source_matches(voice_source_) ||
            rate < trackr::synth::Sample::CUT_SPEED_MIN || rate > trackr::synth::Sample::CUT_SPEED_MAX) return false;
        preview_speed_q8_ = rate; control_tick(); return true;
    }
    bool scrub_active() const { return scrub_; }
    bool preview_seek(uint32_t frame, bool seam = false) {
        if (!preview_ || !playing_ || !source_matches(voice_source_) || frame < win_start_ || frame >= win_end_) return false;
        preview_seam_ = seam;
        if (seam) voice_edit_.crossfade = edit_.crossfade < (win_end_ - win_start_ - 1) / 2 ? edit_.crossfade : (win_end_ - win_start_ - 1) / 2;
        pos_ = frame; frac_ = 0; amp_q15_ = 0; visual_source_frame_ = frame;
        stretch_.reset();
        return true;
    }
    const PlayEditState& edit_state() const { return edit_; }
    bool set_edit_state(const PlayEditState& edit, uint32_t expected_generation);
    // Loader staging cannot bind to the still-live previous project's bank.
    bool restore_edit_state(const PlayEditState& edit);
    // Loader-owned metadata survives absent PCM without borrowing the old bank's
    // provenance/cuts. Hydrated-missing entries additionally bind the empty slot.
    struct EditSourceMetadata {
        int slot = -1;
        trackr::synth::SampleProvenance provenance;
        uint32_t chops[trackr::synth::Sample::MAX_CHOPS] = {};
        uint16_t reverse = 0;
        uint16_t speed_q8[trackr::synth::Sample::MAX_CHOPS] = {
            256,256,256,256,256,256,256,256,256,256,256,256,256,256,256,256
        };
        int8_t pitch_semitones[trackr::synth::Sample::MAX_CHOPS] = {};
        bool exact_native_endpoint = false;
    };
    void stage_edit_source(const EditSourceMetadata& source, bool hydrated_missing = false);
    const EditSourceMetadata* pending_edit_source() const;
    // Call only after successful project hydration, or an explicit same-source
    // copy/detach. Never call automatically when browsing/replacing a slot.
    bool bind_edit_source();
    bool edit_source_matches() const;
    PlayWindow projected_window(q15 start, q15 length, q15 slice) const;
    uint32_t visual_source_frame() const { return visual_source_frame_; }
    void release(); // source-only 64-sample preview/gate release; effects keep running
    SequencerProperties sequencer_properties() const override {
        return {true, false, modern_ ? (play_mode::is_gate(params[P_MODE].control_source())
                ? SequenceLifecycle::Gate : (params[P_LOOP].control_source() > 16383
                ? SequenceLifecycle::Loop : SequenceLifecycle::Strike)) : SequenceLifecycle::Loop,
                pitch_mode_ == PitchMode::Legacy ? P_SPEED : P_NOTE, P_GAIN, -1, modern_ ? P_GATE : P_LEN};
    }
    void reset() override {
        playing_ = false;
        const bool restore_preview = preview_ && !modern_;
        preview_ = false;
        window_latched_ = false;
        latched_edit_ = false;
        preview_repeat_ = false;
        scrub_ = preview_scrub_forced_ = scrub_override_ = false;
        scrub_motion_ = 0; hold_phase_q16_ = 0;
        latched_slice_ = 0; preview_speed_q8_ = 256; rate_bound_ = false;
        loop_entered_ = false;
        preview_pitch_semitones_ = 0;
        stretch_.reset();
        visual_source_frame_ = 0;
        gate_deadline_ = UINT64_MAX;
        release_remaining_ = 0;
        if (restore_preview) control_tick();
        visual_position_ = visual_start_ = visual_end_ = 0;
        visual_speed_ = 0;
        visual_state_ = 0;
    }

    enum VisualState : uint8_t {
        VisualPlaying = 1, VisualReverse = 2, VisualLoop = 4
    };
    uint16_t visual_position() const { return visual_position_; }
    uint16_t visual_start() const { return visual_start_; }
    uint16_t visual_end() const { return visual_end_; }
    int16_t visual_speed() const { return visual_speed_; }
    uint8_t visual_state() const { return visual_state_; }
    bool playback_active() const { return playing_; }
    bool preview_active() const { return preview_; }
    // Normalized progress inside the effective playback window. Reverse playback
    // still moves 0 -> 0xffffffff from trigger to end, matching audible lifetime.
    uint32_t playback_phase() const {
        if (!playing_ || win_end_ <= win_start_ + 1) return 0;
        const int64_t last = (int64_t)win_end_ - (pitch_mode_ == PitchMode::Legacy ? 2 : 1);
        const uint64_t span = last > (int64_t)win_start_
            ? (uint64_t)(last - (int64_t)win_start_) : 1u;
        uint64_t numerator;
        if (inc_q16_ < 0)
            numerator = pos_ >= last ? 0u : pos_ <= (int64_t)win_start_ ? span : (uint64_t)(last - pos_);
        else
            numerator = pos_ <= (int64_t)win_start_ ? 0u : pos_ >= last ? span : (uint64_t)(pos_ - win_start_);
        if (numerator >= span) return 0xffffffffu;
        return (uint32_t)((numerator << 32) / span);
    }

    int sample_slot = 0;    // SampleBank slot
    bool pitch_quantize = false; // snap playback speed to semitone ratios

    // param indices. SLCE: 0 = window, 1..16 = Sample::chops[n-1] .. next chop/end.
    enum { P_SPEED = 0, P_START, P_LEN, P_LOOP, P_GAIN, P_SLICE, P_MODE, P_GATE,
           P_POSITION, P_SCRUB, P_HOLD, P_NOTE = 11, P_COUNT = 12 }; // old indices frozen; ParamBank stays 12
    enum class PitchMode : uint8_t { Legacy = 0, Normal = 1, Stretch = 2 };
    PitchMode pitch_mode() const { return pitch_mode_; }
    // A mode change stops the current voice; a later trigger starts fresh heads.
    void set_pitch_mode(PitchMode mode);
    // Unslewed nominal MIDI target plus one ARP offset, clamped to 0..120.
    // Preview excludes ARP. Scratch/hold retain their separate gesture semantics.
    int actual_note() const;
    // Pure stored-note projection for UI/step duration: no ARP or live source.
    // Normal returns pitch; Stretch returns temporal speed. Legacy/invalid modes
    // return 0: those callers retain play_window's historical quantization rules.
    static int32_t projection_increment(PitchMode mode, q15 speed, q15 note,
                                        bool reversed_slice, uint16_t cut_rate = 256,
                                        int8_t part_pitch = 0);
    bool modern() const { return modern_; }
    // Explicit opt-in only; preserves all controls. Loader gives legacy hidden
    // controls canonical gate / one-step defaults before a later user opt-in.
    void set_modern(bool enabled);

private:
    friend class Engine;
    void release_sequence_gate();
    struct SourceBinding {
        int slot = -1;
        uint32_t generation = 0, frames = 0;
        const q15* data = nullptr;
        size_t elements = 0;
        uint8_t channels = 0;
    } edit_source_, voice_source_;
    SourceBinding source_binding() const;
    bool source_matches(const SourceBinding& source) const;
    void latch_edit_window(const PlayWindow& window);
    void process_edit(q15* buf, int frames, const trackr::synth::Sample& sample);
    void process_scrub(q15* buf, int frames, const trackr::synth::Sample& sample);
    void process_hold(q15* buf, int frames, const trackr::synth::Sample& sample);
    void process_musical(q15* buf, int frames, const trackr::synth::Sample& sample);
    int32_t musical_increment(bool reverse, uint16_t cut_rate, int8_t part_pitch = 0) const;
    int64_t scrub_target() const;
    void begin_scrub();
    PlayEditState edit_;
    EditSourceMetadata pending_edit_source_;
    SourceBinding pending_empty_source_;
    bool pending_hydrated_missing_ = false;
    PlayWindow voice_edit_;
    bool latched_edit_ = false, preview_repeat_ = false, preview_seam_ = false, loop_entered_ = false;
    bool scrub_ = false, preview_scrub_forced_ = false, scrub_override_ = false;
    q15 scrub_position_ = 0, scrub_motion_ = 0;
    uint32_t hold_phase_q16_ = 0; // two complementary heads, no sample buffer
    uint8_t latched_slice_ = 0;
    uint16_t preview_speed_q8_ = 256;
    bool rate_bound_ = false; // opt-in stale-source guard, keeps old default-rate path unchanged
    int8_t preview_pitch_semitones_ = 0; // caller supplied; never infer base SLCE
    uint32_t visual_source_frame_ = 0;
    bool modern_ = true, window_latched_ = false, preview_ = false;
    PitchMode pitch_mode_ = PitchMode::Legacy; // explicit opt-in; old direct users stay byte-identical
    play_stretch::State stretch_;
    int32_t note_inc_q16_ = 65536; // audible grain pitch; inc_q16_ is stretch temporal progress
    bool latched_loop_ = false, latched_reverse_ = false, latched_gate_ = true;
    uint8_t latched_gate_quarters_ = 4, release_remaining_ = 0;
    bool gate_phase_ = false;
    uint64_t gate_deadline_ = UINT64_MAX; // frame or actual transport Q32 beat phase
    bool     playing_  = false;
    int64_t  pos_      = 0;         // frame index
    trackr::fx::uq16 frac_ = 0;
    int32_t  inc_q16_  = 1 << 16;   // signed varispeed
    uint32_t win_start_ = 0, win_end_ = 0;
    q15      amp_q15_  = 0;          // tiny declick ramp after trigger / loop wrap
    // Bounded audio->UI mailbox. Values are normalized to the current playable
    // sample and published only for the focused PLAY through EngineUiSnapshot.
    uint16_t visual_position_ = 0, visual_start_ = 0, visual_end_ = 0;
    int16_t visual_speed_ = 0;
    uint8_t visual_state_ = 0;
};

// Ambient-class granular cloud over one SampleBank slot. The original five
// indices are frozen because sequencer locks, scenes, and SNC7 address params by
// absolute ParamBank position; the cloud scheduler appends into free slots.
class GrainUnit : public Unit {
public:
    enum class LaunchMode : uint8_t { Flow, Gate, Burst };
    enum class Window : uint8_t { Linear, Hann };
    enum class Reading : uint8_t { SignedPitch=0, Forward=1, Reverse=2, Mixed=3 };
    GrainUnit();
    void process(q15* buf, int frames) override;
    void control_tick() override;
    void trigger() override;
    SequencerProperties sequencer_properties() const override {
        return {true, false, clean && launch_mode == LaunchMode::Gate ? SequenceLifecycle::Gate :
            clean && launch_mode == LaunchMode::Burst ? SequenceLifecycle::Strike : SequenceLifecycle::Flow,
            P_PITCH, P_GAIN, P_DENS, P_SIZE};
    }
    void reset() override;

    int sample_slot = 0;
    // Explicit persisted sound mode: new cards opt in; historical patches do not.
    bool clean = true;
    // Separate configuration: no parameter IDs, modulation or lock slots borrowed.
    LaunchMode launch_mode = LaunchMode::Flow;
    Window window = Window::Linear;
    uint8_t burst_count = 4;
    // Clean-only authored reading policy. SignedPitch keeps the historical RNG
    // and signed PTCH law; percentage is stored/dormant outside Mixed.
    Reading reading = Reading::SignedPitch;
    uint8_t reverse_percent = 50;
    void set_reading(Reading value);
    void set_reverse_percent(uint8_t value);
    void set_launch_mode(LaunchMode value);
    void set_window(Window value);
    void set_burst_count(uint8_t value);
    // Audio-guard ownership. A newer begin invalidates the preceding pad token.
    uint32_t pad_begin();
    void pad_end(uint32_t token);
    bool gate_open() const;
    uint8_t burst_remaining() const;
    // UI-rate clean window sampling; audio uses the same math with a latched step.
    static q15 visual_window_q15(uint32_t age, uint32_t length, uint32_t attack,
                                  uint32_t release, Window shape);
    // Optional transport-locked spawn interval. Kept as unit config (like PLAY's
    // pitch_quantize) rather than a param so the DENS knob stays continuous.
    bool clock_sync = false;
    uint8_t sync_division = 0;   // 0 = 1/8, 1 = 1/16, 2 = 1/32, 3 = 1/64
    uint32_t sync_interval_frames = 8000;
    void set_transport_bpm(int bpm);
    void set_transport_bpm_q16(int32_t bpm_q16);
    enum { P_POS = 0, P_SIZE, P_SPRAY, P_PITCH, P_GAIN,
           P_DENS, P_JITTER, P_ATTACK, P_RELEASE, P_PITCH_DEV, P_VOICES,
           P_SCAN, P_COUNT };
    // Eight concurrent grains plus eight declick tails is the Old-3DS budget.
    // MAX_GRAINS keeps its old name so existing UI/test call sites stay valid.
    static constexpr int MAX_GRAINS = 8;
    static constexpr int MAX_TAILS = 8;
    static constexpr uint32_t DECLICK_FRAMES = 96;   // ~3 ms at 32 kHz
    static uint8_t voice_limit(q15 value);
    static uint32_t grain_length_frames(q15 size);
    static uint32_t spawn_interval_frames(q15 density);
    static int32_t pdev_increment(int32_t base_increment, q15 deviation_sample);
    // Independent signed source frames/output frame, -4x..+4x; zero is hold.
    static int32_t scan_increment_q16(q15 value);
    // Nominal next spawn center, excluding spray/direction-aware head clamping.
    // Read under the same guard as capture_visual_grains; no PCM reads.
    q15 scan_position() const;

    // Read-only next-read state of the eight primary heads (not declick tails).
    // Caller must hold the platform audio guard and freeze this before GPU draw.
    // Signed Q16 source trajectories are nominal, not clipped to sample bounds;
    // envelope is the exact next-read window from each head's latched shape/ramps.
    struct VisualGrain {
        bool active = false;
        int64_t position_q16 = 0, start_q16 = 0, end_q16 = 0;
        uint32_t age = 0, length = 0, attack = 0, release = 0;
        q15 envelope = 0;
        Window window = Window::Linear; // latched at this head's launch
    };
    void capture_visual_grains(VisualGrain (&out)[MAX_GRAINS]) const;

    const uint8_t* visual_positions() const { return visual_positions_; }
    uint16_t visual_grains() const { return visual_grains_; }
    uint8_t visual_active() const { return visual_active_; }
    // Effective scheduler period in frames, not a normalized knob value. The UI
    // shows real spawn rate including modulation, jitter base, and clock sync.
    uint16_t visual_interval() const { return visual_interval_; }
    uint32_t visual_spawned() const { return visual_spawned_; }
    uint16_t visual_longest() const { return visual_longest_; }
    const uint8_t* visual_phases() const { return visual_phases_; }
    const int8_t* visual_pitch_offsets() const { return visual_pitch_offsets_; }

private:
    friend class Engine;
    void clear_launch_requests();
    void release_sequence_gate();
    void begin_sequence_gate(uint64_t deadline, bool phase);
    void synchronize_performance();
    void bind_performance_source();
    LaunchMode runtime_launch_mode_ = LaunchMode::Flow;
    bool runtime_clean_ = true;
    uint32_t pad_token_ = 0;
    uint8_t pending_launches_ = 0;
    bool sequence_burst_ = false;
    uint64_t gate_deadline_ = UINT64_MAX;
    bool gate_phase_ = false;
    struct Head {
        bool     active = false;
        int64_t  pos = 0;           // absolute frame index into the sample
        trackr::fx::uq16 frac = 0;
        int32_t  inc_q16 = 1 << 16; // signed per-grain increment
        uint32_t start = 0;         // grain window start frame
        uint32_t length = 0;        // total grain frames (window domain)
        uint32_t age = 0;           // frames already played
        uint32_t attack = 0;        // attack frames
        uint32_t release = 0;       // release frames
        // Reciprocals resolved once per grain: the render loop must stay free of
        // integer division on ARM11, which has no hardware divide.
        int32_t  attack_step = 0;   // q15 window gain per attack frame
        int32_t  release_step = 0;  // q15 window gain per release frame
        Window shape = Window::Linear;
        uint32_t hann_step = 0;     // q22 half-table position/frame, latched
        uint8_t  window_shift = 0; // legacy integer slope or clean q16 slope, latched
        int8_t   pitch_offset = 0; // signed semitones for focused visualization
    };
    struct Tail {
        Head head;
        uint32_t remaining = 0;
    };
    Head heads_[MAX_GRAINS];
    Tail tails_[MAX_TAILS];
    int      next_head_ = 0;
    uint32_t spawn_countdown_ = 0;
    uint32_t rng_ = 0xDEADBEEF;
    // Balanced fraction, 0..99; successful Mixed spawns only. Trigger/pad and
    // config edits preserve it; hard reset/source replacement/cold clone do not.
    uint8_t mix_phase_ = 0;
    int32_t  inc_q16_ = 1 << 16;
    // Positive wrapped offset, relative to effective POS; never persisted/cloned.
    int64_t scan_offset_q16_ = 0;
    int32_t scan_inc_q16_ = 0;
    int scan_source_slot_ = -1;
    uint32_t scan_source_generation_ = 0, scan_source_frames_ = 0;
    const q15* scan_source_data_ = nullptr;
    uint8_t scan_source_channels_ = 0;
    bool scan_source_matches() const;
    int64_t scan_center_q16(uint32_t nf) const;
    // Cached clock state. The control tick runs at 1 kHz across every chain, so
    // the sync period must not be recomputed (an ARM11 software divide) unless
    // the transport tempo or the selected division actually changed.
    int16_t  clock_bpm_ = 0;
    int32_t clock_bpm_q16_ = 0;
    uint16_t clock_countdown_fraction_ = 0;
    uint8_t  clock_division_ = 0xff;
    // Bounded audio->UI diagnostics. Positions are normalized to the source
    // sample so the focused lens page can show every live grain honestly.
    uint8_t visual_positions_[MAX_GRAINS] = {};
    uint16_t visual_grains_ = 0;
    uint8_t visual_active_ = 0;
    uint16_t visual_interval_ = 0;
    uint32_t visual_spawned_ = 0;
    uint16_t visual_longest_ = 0;
    uint8_t visual_phases_[MAX_GRAINS] = {};
    int8_t visual_pitch_offsets_[MAX_GRAINS] = {};

    void spawn_grain(uint32_t frames_total, int limit);
    void retire_into_tail(const Head& head);
    int  select_head(int limit);
    uint32_t rand_u32() { rng_ = rng_ * 1664525u + 1013904223u; return rng_; }
    int32_t rand_signed(int32_t amplitude);
};

class OscUnit : public Unit {
public:
    OscUnit();
    void process(q15* buf, int frames) override;
    void control_tick() override;
    SequencerProperties sequencer_properties() const override {
        return {true, false, SequenceLifecycle::Flow, P_PITCH, P_GAIN, -1, -1};
    }

    enum class Wave : uint8_t { Sine = 0, Tri, Saw, Pulse, Count };
    Wave wave = Wave::Sine;
    bool pitch_quantize = false; // snap pitch modulation to MIDI semitones
    bool shape_morph = true;     // UNIT bit1: continuous SHAPE path; false is exact legacy wave
    bool clean = true;           // UNIT bit2: independent opt-in; old morph is NOT clean

    struct Visual {
        static constexpr int COLUMNS = 96, HISTORY = 48;
        int16_t pre_min[COLUMNS] = {}, pre_max[COLUMNS] = {};
        int16_t out_min[COLUMNS] = {}, out_max[COLUMNS] = {};
        uint8_t occupied[COLUMNS] = {}; // zero means no real sample in this bin
        int16_t pitch_cents[HISTORY] = {};
        uint8_t pitch_count = 0, pitch_head = 0;
        uint16_t samples_per_cycle = 0; // two main periods, one sub period
        uint32_t generation = 0;
        bool valid = false;
        uint32_t frequency_millihz = 0;
    };
    void set_visual_capture(bool enabled);
    const Visual& visual() const { return visual_; }
    void reset() override; // clean state only; historical reset remains a no-op

    // Keep the original three indices stable: sequencer locks and SNC7 address
    // parameters by absolute ParamBank index. Continuous SHAPE appends after all
    // six historical OSC controls so old locks/routes keep their addresses.
    enum { P_PITCH = 0, P_PW, P_GAIN, P_FOLD, P_SUB, P_DRIFT, P_SHAPE, P_COUNT };

    static q15 shape_anchor(Wave value);
    static Wave nearest_shape_anchor(q15 value);

private:
    uint32_t phase_ = 0;
    uint32_t sub_phase_ = 0;
    uint32_t inc_   = 0;
    uint32_t drift_rng_ = 0x05C11A7Eu;
    uint16_t drift_count_ = 0;
    q15 drift_current_ = 0;
    q15 drift_target_ = 0;

    // Separate state keeps historical discrete and morph arithmetic untouched.
    static constexpr int CLEAN_TAPS = 47;
    float clean_value_[P_COUNT] = {}, clean_target_[P_COUNT] = {}, clean_step_[P_COUNT] = {};
    float clean_previous_[P_COUNT] = {};
    uint8_t clean_ramp_ = 0;
    bool clean_ready_ = false;
    uint32_t clean_phase_ = 0, clean_seed_ = 0, clean_rng_ = 0;
    uint32_t clean_drift_count_ = 0;
    float clean_drift_ = 0, clean_drift_target_ = 0, clean_drift_phase_error_ = 0;
    float clean_saw_ = 1, clean_tri_ = 0, clean_pulse_ = 1;
    float clean_previous_fold_ = 0, clean_previous_sub_ = 0, clean_previous_pre_ = 0;
    float clean_fir_[CLEAN_TAPS] = {}, clean_pre_fir_[CLEAN_TAPS] = {};
    uint32_t clean_phase_delay_[CLEAN_TAPS] = {};
    uint8_t clean_fir_head_ = 0;
    float clean_nominal_hz_ = 261.625565f;
    bool visual_enabled_ = false, visual_cycle_started_ = false;
    uint16_t visual_warmup_ = 0, visual_cycle_samples_ = 0, visual_history_clock_ = 0;
    uint32_t visual_last_phase_ = 0;
    Visual visual_{}, visual_work_{};
    void clean_control_tick();
    void clean_process(q15* buf, int frames);
    void collect_visual(uint32_t phase, q15 pre, q15 out, float frequency);
};

class NoiseUnit : public Unit {
public:
    explicit NoiseUnit(bool modern = true);
    bool modern() const { return modern_; }
    void set_modern(bool enabled);
    q15 control_value(int param) const;
    void process(q15* buf, int frames) override;
    void control_tick() override;
    void trigger() override;
    SequencerProperties sequencer_properties() const override {
        const auto life = drone_mode(params[P_MODE].control_source(),
                                     params[P_DECAY].control_source())
            ? SequenceLifecycle::Flow : SequenceLifecycle::Strike;
        return {true, false, life, -1, P_GAIN, -1, P_DECAY};
    }
    void reset() override;

    enum class Colour : uint8_t { White = 0, Pink, Crackle, Metal, Count };
    // Append-only: parameter indices are persisted in patches and step locks.
    enum { P_COLOUR = 0, P_DENSITY, P_TONE, P_GAIN, P_BODY, P_DECAY,
           P_ATTACK, P_MODE, P_RESONANCE, P_SWEEP, P_SWEEP_TIME, P_EVENT_LENGTH, P_COUNT };
    static constexpr int LEGACY_COUNT = 8;
    static uint32_t grain_millihz(q15 value); // modern: 30..32000Hz
    static uint32_t dust_millihz(q15 value);  // 0=off, then0.1..8000 events/s
    static uint32_t tone_millihz(q15 value);  // 25..14000Hz
    static uint32_t body_millihz(q15 value);  // 0=off, then20..6000Hz
    static uint32_t decay_frames(q15 value); // 5ms..10s, exact finite duration
    static uint32_t sweep_frames(q15 value); // 1ms..5s
    static uint32_t event_frames(q15 value); // 1..250ms per isolated event
    static int sweep_cents(q15 value);      // bipolar +/-2400 cents
    static float model_response_db(float hz, q15 tone, q15 body, q15 resonance, int cents = 0);
    static float model_envelope(uint32_t elapsed, uint32_t attack, uint32_t decay, bool drone);
    // MODE zero keeps historical DECAY modulation crossing into/out of drone.
    // UI authors only explicit hit (16384) or drone (32767).
    static int mode_index(q15 mode) { return mode < 8192 ? 0 : (mode < 24576 ? 1 : 2); }
    static bool drone_mode(q15 mode, q15 decay) {
        const int index = mode_index(mode);
        return index == 2 || (index == 0 && decay >= 32760);
    }
    static uint32_t attack_frames(q15 attack);
    uint16_t visual_activity() const { return visual_activity_; }
    uint16_t visual_envelope() const { return (uint16_t)envelope_; }

private:
    uint32_t rng_ = 0x4E4F4953u;
    int32_t held_ = 0;
    int32_t pink_ = 0;
    int32_t filtered_ = 0;
    int32_t body_low_ = 0;
    int32_t crackle_ = 0;
    q15 envelope_ = 32767;
    uint16_t hold_count_ = 0;
    uint16_t hold_period_ = 1;
    uint16_t tone_alpha_ = 8192;
    uint16_t body_alpha_ = 0;
    uint16_t decay_step_ = 0;
    bool drone_ = true;
    bool envelope_started_ = false;
    uint32_t attack_total_ = 0;
    uint32_t attack_elapsed_ = 0;
    uint32_t attack_error_ = 0;
    uint16_t attack_step_ = 0;
    uint16_t attack_remainder_ = 0;
    void begin_attack();
    uint16_t visual_activity_ = 0;
    // Frozen legacy state above; extended source has independent bounded state.
    bool modern_ = true, modern_started_ = false, modern_drone_ = true;
    bool processed_ = false;
    q15 last_out_ = 0, transition_from_ = 0;
    uint16_t transition_left_ = 0;
    uint32_t random_ = 0x9E3779B9u, grain_phase_ = 0, grain_increment_ = 0, dust_threshold_ = 0;
    bool full_rate_ = false;
    float grain_ = 0, pink_state_[3] = {}, event_amplitude_ = 0;
    uint32_t event_left_ = 0, event_total_ = 32, event_clock_ = 0;
    uint32_t env_total_ = 0, env_left_ = 0, env_phase_ = 0, env_step_ = 0, env_rem_ = 0, env_error_ = 0;
    uint8_t env_stage_ = 0; // idle/attack/decay/hold
    float env_start_ = 0, modern_env_ = 0;
    uint32_t latched_decay_ = 160, sweep_total_ = 32, sweep_left_ = 0;
    int sweep_amount_ = 0;
    uint8_t filter_clock_ = 0;
    float filter_a1_ = 1, filter_a2_ = 0, filter_a3_ = 0, filter_k_ = 1;
    float filter_ic1_ = 0, filter_ic2_ = 0, hp_alpha_ = 1, hp_input_ = 0, hp_output_ = 0;
    float applied_cutoff_ = -1, applied_q_ = -1, applied_body_ = -1;
    q15 filter_tone_ = -1, filter_body_ = -1, filter_res_ = -1;
    int filter_cents_ = 32767;
    void modern_reset();
    void modern_trigger();
    void modern_control();
    void modern_process(q15* buf, int frames);
    void update_filter();
    void begin_segment(uint8_t stage, uint32_t frames);
    void advance_envelope();
};

class FmUnit : public Unit {
public:
    FmUnit();
    void process(q15* buf, int frames) override;
    void control_tick() override;
    void trigger() override;
    SequencerProperties sequencer_properties() const override {
        return drone
            ? SequencerProperties{true, false, SequenceLifecycle::Flow, P_PITCH, P_GAIN, P_GAIN, -1}
            : SequencerProperties{true, false, SequenceLifecycle::Strike, P_PITCH, P_GAIN, -1, P_DECAY};
    }
    void reset() override;

    enum {
        P_PITCH = 0, P_RATIO, P_INDEX, P_FEEDBACK, P_DECAY, P_GAIN,
        P_INDEX_FALL, P_ATTACK, P_RATIO_FINE, P_FEEDBACK_FALL,
        P_PITCH_ENV, P_PITCH_TIME, P_COUNT
    };
    enum class Wave : uint8_t { Sine = 0, Triangle, Saw, Pulse, Count };
    bool extended = true;
    Wave carrier_wave = Wave::Sine, mod_wave = Wave::Sine;
    // Policy changes never rewrite stored parameters or reset either phase.
    void set_extended(bool enabled);
    static constexpr q15 DEFAULT_PITCH_TIME = 19824; // exactly 3200 samples / 100ms
    static uint32_t pitch_env_samples(q15 value); // exact latched duration, 32..64000
    static int pitch_env_cents(q15 value);        // rounded signed -4800..4800
    q15 visual_modulator() const { return last_mod_; }
    int visual_pitch_cents() const;
    uint32_t visual_pitch_remaining() const { return pitch_remaining_; }
    uint16_t visual_phase() const { return (uint16_t)(carrier_phase_ >> 16); }
    uint16_t visual_envelope() const { return (uint16_t)env_; }
    uint16_t visual_index_envelope() const { return (uint16_t)index_env_; }
    uint16_t visual_feedback_envelope() const { return (uint16_t)feedback_env_; }

    // Patch-persisted performance policies. False/false exactly preserves the
    // original free-running, raw two-operator voice.
    bool phase_reset = false;
    bool alias_safe = false;
    bool drone = false; // UNIT config bit 0x04; DECAY/ATK/IFALL/FFALL dormant

    // Control-rate only. Strike retains the original Q12 fine truncation;
    // drone adds the exact signed RFINE/(4*32767) ratio before final rounding.
    static uint32_t modulation_increment(uint32_t carrier, q15 ratio, q15 fine,
                                         bool drone_mode, bool safe);
    // Full-scale mode entry takes 512 samples / 16ms at 32kHz. Mode exit uses
    // the saved strike decay, capped to this per-sample fall until a real strike.
    static constexpr int DRONE_FADE_STEP = 64;

private:
    uint32_t carrier_phase_ = 0;
    uint32_t mod_phase_ = 0;
    uint32_t carrier_inc_ = 0;
    uint32_t mod_inc_ = 0;
    q15 last_mod_ = 0;
    q15 env_ = 0;
    q15 index_env_ = 32767;
    q15 feedback_env_ = 32767;
    uint16_t decay_step_ = 8;
    uint16_t attack_step_ = 32767;
    uint16_t index_fall_step_ = 0;
    uint16_t feedback_fall_step_ = 0;
    bool attacking_ = false;
    bool rendered_drone_ = false;
    bool drone_release_ = false;
    // Linear-in-log-pitch DDA: signed Q24 octaves, exact endpoint at duration.
    int32_t pitch_log2_ = 0, pitch_step_ = 0;
    uint32_t pitch_total_ = 0, pitch_remaining_ = 0;
    uint32_t pitch_remainder_ = 0, pitch_error_ = 0;
    int8_t pitch_sign_ = 0;
    uint32_t pitch_ratio_q12_ = 4096;
    void clear_pitch_envelope();
    void advance_pitch_envelope();
    uint32_t pitched_carrier_increment() const;
    struct WaveMix {
        Wave target = Wave::Sine;
        uint8_t remaining = 0;
        int32_t weight[4] = {65536, 0, 0, 0};
        int32_t start[4] = {65536, 0, 0, 0};
        void select(Wave wave, bool immediate);
        int32_t sample(uint32_t phase);
    };
    WaveMix carrier_mix_, mod_mix_;
    bool waves_started_ = false;
};

class MotorUnit : public Unit {
public:
    explicit MotorUnit(bool modern = true);
    void process(q15* buf, int frames) override;
    // Optional pre-resonator tap for a private UI reference, never a live PCM pointer.
    void process_with_raw(q15* out, q15* raw, int frames);
    void control_tick() override;
    SequencerProperties sequencer_properties() const override {
        return {true, false, SequenceLifecycle::Flow, int8_t(note_mode()?P_NOTE:-1), P_GAIN, P_GAIN, -1};
    }
    void reset() override;
    bool modern() const { return modern_; }
    void set_modern(bool enabled);
    bool note_mode() const { return modern_ && params[P_MODE].target >= 16384; }
    static uint32_t rate_millihz(q15 value);
    static uint32_t note_millihz(q15 value);
    static uint32_t width_q32(q15 value);
    static uint32_t sync_ratio_q16(q15 value);
    static uint32_t jitter_millihz(q15 value);
    uint32_t nominal_millihz() const;
    bool sync_limited() const { return sync_limited_; }

    enum class Source : uint8_t { Saw = 0, Pulse, Mix, Count };
    enum { P_RPM = 0, P_JITTER, P_RESONANCE, P_LOAD, P_GAIN, P_SOURCE,
           P_WIDTH, P_SYNC, P_JITTER_SPEED, P_NOTE, P_MODE, P_COUNT };
    uint16_t visual_phase() const { return (uint16_t)(phase_ >> 16); }
    uint16_t visual_energy() const { return visual_energy_; }

private:
    bool modern_ = true, sync_limited_ = false, processed_ = false;
    uint32_t phase_ = 0, phase_b_ = 0, phase_c_ = 0, slave_phase_ = 0;
    uint32_t base_inc_ = 0, ratio_ = 65536, width_ = 0x80000000u;
    uint32_t jitter_period_ = 256, jitter_smoothing_ = 64;
    uint32_t rng_ = 0x4D4F544Fu, jitter_count_ = 0;
    int16_t jitter_current_ = 0, jitter_target_ = 0;
    int32_t wander_q16_ = 0;
    int32_t resonator_ = 0, resonator_prev_ = 0, dc_x_ = 0, dc_y_ = 0;
    q15 last_out_ = 0, transition_from_ = 0;
    uint16_t transition_left_ = 0, visual_energy_ = 0;
};

// ---------- BIRD: one latched, finite call; gain alone remains live ----------
class BirdUnit : public Unit {
public:
    BirdUnit();
    enum { P_PITCH = 0, P_SWEEP, P_TIME, P_TRILL, P_AIR, P_GAIN, P_CONTOUR, P_SYLLABLE, P_COUNT };
    static uint32_t pitch_millihz(q15 value);
    static int32_t sweep_cents(q15 value);
    static uint32_t duration_samples(q15 value);
    static uint32_t trill_millihz(q15 value);
    struct Visual {
        bool active = false;
        uint32_t age = 0, duration = 0; // age is the NEXT sample; stops at duration
        q15 latched[P_COUNT] = {}; // effective values at strike; gain is still live
    };
    // Prepared outside the audio guard; immutable input for cheap pure points.
    struct CallShape {
        uint32_t duration = 0, inverse_duration = 0, base_inc = 0, trill_inc = 0;
        int32_t sweep_q30 = 0, contour_q16 = 0;
        uint32_t syllable_q16 = 0;
    };
    struct ShapePoint { uint32_t frequency_millihz = 0; q15 envelope_q15 = 0; };
    Visual visual() const;
    static CallShape prepare_shape(const q15* values); // P_COUNT elements
    static ShapePoint shape_point(const CallShape& shape, uint32_t age);
    void process(q15* buf, int frames) override;
    void control_tick() override;
    void trigger() override;
    void reset() override;
    SequencerProperties sequencer_properties() const override {
        return {true, false, SequenceLifecycle::Strike, P_PITCH, P_GAIN, -1, P_TIME};
    }
private:
    CallShape shape_;
    q15 latched_[P_COUNT] = {};
    uint32_t phase_ = 0, age_ = 0;
    uint32_t rng_ = 0x42495244u;
    int32_t air_ = 0, noise_low_ = 0, noise_slow_ = 0;
    int32_t last_ = 0, retrigger_from_ = 0;
    uint8_t retrigger_left_ = 0;
    shift_dsp::Ramp gain_;
};

// ---------- processors ----------

class FilterUnit : public Unit {
public:
    FilterUnit();
    void process(q15* buf, int frames) override;
    void control_tick() override;
    void reset() override;

    trackr::dsp::FilterType type = trackr::dsp::FilterType::LPF;

    enum { P_CUTOFF = 0, P_RES, P_DRIVE, P_MORPH };
    uint16_t visual_cutoff() const { return (uint16_t)cutoff_smooth_; }
    uint16_t visual_resonance() const { return (uint16_t)res_smooth_; }


private:
    q15 cutoff_smooth_ = 24000;
    q15 res_smooth_ = 4096;
    trackr::dsp::Svf2 svf_;
};

class VcaUnit : public Unit {
public:
    explicit VcaUnit(bool modern = true);
    void process(q15* buf, int frames) override;
    void trigger() override;
    SequencerProperties sequencer_properties() const override {
        // Duck and a retained floor shape an already-continuous signal; only
        // an open envelope with zero resting floor gates a flowing source.
        const bool gate = flexible ? (params[P_FLOOR].control_source() <= 0 &&
                                      params[P_DUCK].control_source() < 16384) :
            (params[P_ATTACK].control_source() >= 128 || params[P_DECAY].control_source() >= 128);
        return {false, gate, SequenceLifecycle::Gate, -1, P_GAIN, -1, P_DECAY};
    }
    void reset() override;
    // UNIT bit0 opts into AHD. Loader starts legacy-first; no automatic upgrade.
    bool flexible = true;
    void set_flexible(bool enabled); // explicitly selects frozen old AD/AHD
    bool modern() const { return modern_; }
    void set_modern(bool enabled);
    void set_clock_bpm_q16(int32_t bpm_q16);
    int32_t clock_bpm_q16() const { return bpm_q16_; }
    void stop_cycle() { cycle_armed_ = false; }
    bool visual_cycling() const { return cycle_armed_; }
    uint32_t visual_stage_frames(int p) const {
        return latched_frames_[p==P_ATTACK?0:p==P_HOLD?1:2];
    }

    enum { P_GAIN = 0, P_ATTACK, P_DECAY, P_HOLD, P_FLOOR, P_A_CURVE, P_D_CURVE, P_DUCK,
           P_SYNC, P_RETRIGGER, P_RUN, P_COUNT };
    static constexpr int LEGACY_COUNT=3, AHD_COUNT=8;
    enum VisualState : uint8_t {
        VisualActive = 1, VisualRising = 2, VisualGainOnly = 4,
        VisualHolding = 8, VisualDuck = 16
    };
    uint16_t visual_envelope() const { return visual_env_; }
    uint8_t visual_state() const { return visual_state_; }
    bool visual_ready() const { return visual_ready_; }
    uint8_t visual_stage() const { return visual_stage_; } // 0 idle, 1 attack, 2 hold, 3 decay
    uint32_t visual_progress_q16() const { return visual_progress_; } // local 0..65536
    uint16_t visual_gain() const { return visual_gain_; } // last multiplier, not PCM level
    q15 visual_effective(int p) const { return visual_effective_[p < 0 ? 0 : p >= P_COUNT ? P_COUNT - 1 : p]; }

private:
    bool modern_ = true, cycle_armed_ = false;
    int32_t bpm_q16_ = 120<<16;
    uint32_t latched_frames_[3] = {};
    q15 reset_from_ = 0, last_multiplier_ = 0;
    uint8_t reset_blend_left_ = 0;
    q15 resolved(int p) const;
    void process_legacy(q15* buf, int frames);
    void enter_stage(uint8_t stage);
    void prepare_live();
    // Modern phase is exact Q32 with a Bresenham remainder; no lost fractional
    // envelope steps even at 8 seconds. Only stage entry computes a quotient.
    uint64_t phase_ = 0;
    uint32_t phase_step_ = 0, phase_rem_ = 0, phase_error_ = 0;
    uint32_t stage_frames_ = 0, elapsed_ = 0, exponent_ = 1048576;
    uint8_t stage_ = 0;
    q15 attack_origin_ = 0;
    struct LiveRamp {
        int32_t value = 0, step = 0; // Q15 value with 16 fractional bits
        q15 target = 0;
        uint8_t remaining = 0;
        void aim(q15 to, bool snap);
        q15 advance();
    } live_[3]; // gain, floor, duck crossfade; finite 64-sample interpolation
    bool live_ready_ = false;
    q31      env_ = 0;              // canonical E, shared with exact old AD path
    bool     env_active_ = false;
    bool     rising_ = false;
    // Bounded scalar mailbox. Time/curve entries are latched at their stage entry;
    // gain/floor/duck are the last interpolated values actually used by audio.
    q15 latched_[P_COUNT] = {};
    q15 visual_effective_[P_COUNT] = {};
    uint16_t visual_env_ = 0, visual_gain_ = 0;
    uint32_t visual_progress_ = 0;
    uint8_t visual_state_ = 0, visual_stage_ = 0;
    bool visual_ready_ = false;
};

class DelayUnit : public Unit {
public:
    explicit DelayUnit(bool modern = true);
    ~DelayUnit() override;
    void process(q15* buf, int frames) override;
    void control_tick() override;
    void reset() override;
    bool ready() const override { return buf_ != nullptr; }

    enum { P_TIME = 0, P_FEEDBACK, P_MIX, P_WOW, P_SAT, P_HOLD,
           P_TONE, P_SYNC, P_DIV, P_FEEL, P_MODE, P_COUNT };
    bool modern() const { return modern_; }
    // Setup-only and transactional: no allocation/upgrade in process or UI.
    bool set_modern(bool modern);
    void set_clock_bpm_q16(int32_t bpm_q16);
    void request_clear();
    struct VisualState {
        bool ready = false, sync = false, clean = false, crossfading = false,
             clearing = false, limited = false;
        uint32_t target_frames = 0, read_a_q8 = 0, read_b_q8 = 0;
        int32_t bpm_q16 = 120 << 16;
        q15 hold_amount = 0, crossfade_q15 = 0;
    };
    VisualState visual_state() const;
    uint16_t visual_delay_frames() const { return visual_delay_frames_; }
    int16_t visual_wow_offset() const { return visual_wow_offset_; }
    uint16_t visual_wet_peak() const { return visual_wet_peak_; }
    bool visual_hold() const { return visual_hold_; }


private:
    void process_legacy(q15* buf, int frames);
    void process_modern(q15* buf, int frames);
    q15 read_modern(uint32_t delay_q8) const;
    void refresh_time();
    static constexpr int MAX_DELAY = 32000;   // frozen legacy allocation: 64kb
    static constexpr uint32_t MODERN_CAPACITY = 128002;
    static constexpr uint16_t RAMP_FRAMES = 320, CROSSFADE_FRAMES = 640;
    q15*     buf_ = nullptr;
    uint32_t capacity_ = MAX_DELAY;
    bool modern_ = false, modern_ready_ = false;
    // Audio history exists only after a positive legacy process, not on allocation.
    bool legacy_processed_ = false;
    bool sync_ = false, clean_ = false, limited_ = false;
    bool crossfading_ = false, clear_pending_ = false;
    int32_t bpm_q16_ = 120 << 16;
    int32_t time_cache_ = -1, tone_cache_ = -1;
    q15 division_ = 0, feel_ = 0;
    uint32_t read_a_q8_ = 8000u << 8, read_b_q8_ = 8000u << 8;
    uint32_t visual_read_a_q8_ = 0, visual_read_b_q8_ = 0;
    // Number of sequential fresh writes since clear. Reads older than this
    // return zero, so clearing never walks or memsets the 256kb audio ring.
    uint32_t valid_frames_ = 0;
    uint16_t crossfade_pos_ = 0, hold_pos_ = 0, clear_pos_ = 0;
    uint8_t clear_stage_ = 0; // 0 idle, 1 fade-out, 2 fade-in
    q15 hold_amount_ = 0, hold_origin_ = 0, hold_target_ = 0;
    int32_t tone_alpha_q15_ = 8192;
    uint32_t write_ = 0;
    uint32_t delay_frames_ = 8000;             // target from control_tick()
    int32_t  delay_cur_q16_ = 8000 << 16;      // slewed fractional read delay
    uint32_t wow_phase_ = 0;
    q15 feedback_lp_ = 0;
    // Bounded audio->UI diagnostics. EngineUiSnapshot publishes these through
    // AudioOut's snapshot mailbox; no delay-buffer access or UI-side timer is required.
    uint16_t visual_delay_frames_ = 8000;
    int16_t visual_wow_offset_ = 0;
    uint16_t visual_wet_peak_ = 0;
    bool visual_hold_ = false;
};

// Continuous mono peak compressor. All parameters use the normal engine layers.
// Audio owns runtime; visual() must be copied under the platform audio guard.
class CompUnit : public Unit {
public:
    enum { P_THRESHOLD=0, P_RATIO, P_ATTACK, P_RELEASE, P_MAKEUP, P_MIX,
           P_KNEE, P_HP, P_CLOCK, P_COUNT };
    static constexpr int RELEASE_DIVISIONS = 8;
    static constexpr int HISTORY_SIZE = 64;
    static constexpr int HISTORY_FRAMES = 1024;
    static constexpr int RAMP_FRAMES = 64;
    struct Visual {
        float input_db = -96.f, output_db = -96.f, detector_db = -96.f, gr_db = 0.f;
        bool clip = false;
        float history[HISTORY_SIZE] = {};
        uint8_t history_count = 0;
    };
    CompUnit();
    void process(q15* buf, int frames) override;
    void control_tick() override;
    void reset() override;
    // trigger intentionally inherits Unit's no-op: locks/retriggers never reset GR.
    Visual visual() const;
    static float threshold_db(q15 v);
    static float ratio(q15 v);
    static float attack_ms(q15 v);
    static float release_ms(q15 v, bool sync, int32_t bpm_q16);
    static float makeup_db(q15 v);
    static float knee_db(q15 v);
    static float hp_hz(q15 v);
    static const char* release_label(q15 v);
    static int release_index(q15 v);
    static q15 release_value(int index);
    void set_transport_bpm_q16(int32_t bpm);
    int32_t transport_bpm_q16() const { return bpm_q16_; }
private:
    struct Ramp {
        float value = 0.f, target = 0.f, step = 0.f;
        uint8_t remaining = 0;
        void set(float next, bool snap);
        float advance();
    };
    struct Peak {
        float value = 0.f;
        uint16_t hold = 0;
        void push(float amplitude);
    };
    q15 cached_[P_COUNT] = {};
    int32_t bpm_q16_ = 120 * 65536, cached_bpm_ = 0;
    float threshold_ = -18.f, slope_ = .75f, half_knee_ = 3.f, knee_scale_ = .0625f;
    float attack_ = 0.f, release_ = 0.f, hp_ = 0.f;
    float hp_input_ = 0.f, hp_output_ = 0.f, gr_ = 0.f;
    Ramp mix_, makeup_, enabled_;
    Peak input_peak_, output_peak_, detector_peak_;
    float history_[HISTORY_SIZE] = {}, bin_max_ = 0.f;
    uint16_t bin_frames_ = 0, clip_hold_ = 0;
    uint8_t history_write_ = 0, history_count_ = 0;
    bool audio_started_ = false;
};

// Three parallel formant bandpasses. All five controls use normal Param layers.
// Model is a scalar copy: capture under the audio guard, never read live in draw.
class MouthUnit : public Unit {
public:
    enum { P_VOWEL=0, P_SIZE, P_RESONANCE, P_MIX, P_OUT, P_COUNT };
    static constexpr int RAMP_FRAMES = 160; // 5 ms at 32 kHz, independent of blocks
    struct Model {
        float hz[3] = {}, b0[3] = {}, b2[3] = {}, a1[3] = {}, a2[3] = {};
        float wet_gain = 1.f, mix = 0.f, output_gain = 1.f;
    };
    MouthUnit();
    void process(q15* buf, int frames) override;
    void control_tick() override;
    void reset() override;
    void trigger() override {} // continuous filter: preserve tails across steps
    static float size_ratio(q15 value);
    static float resonance_q(q15 value);
    static float output_db(q15 value);
    static Model model_from_values(const q15* values);
    // Quasi-static combined complex response, including phase/dry/wet-only out.
    // Not an audio spectrum; interpolated hz[] are nominal centers during ramps.
    static float response_db(const Model& model, float hz);
    Model visual_model() const;
private:
    q15 cached_[P_COUNT] = {};
    Model current_, target_, step_;
    float input1_ = 0.f, input2_ = 0.f, y1_[3] = {}, y2_[3] = {};
    float enabled_ = 1.f, enabled_target_ = 1.f, enabled_step_ = 0.f;
    uint16_t remaining_ = 0, enabled_remaining_ = 0;
    bool audio_started_ = false;
    void retarget(const Model& next, bool snap);
    void advance();
};

// Live-input repeat only. Capture swaps two fixed rings; no audio allocation/copy.
class FractUnit : public Unit {
public:
    enum { P_SIZE=0, P_LENGTH, P_CHANCE, P_DAMAGE, P_SPEED, P_MIX, P_SYNC, P_HOLD, P_COUNT };
    static constexpr uint32_t CAPACITY = 32768, MIN_LOOP = 64, MAX_CAPTURE = 32000;
    static constexpr uint32_t MIN_GRAB_WINDOW = 2000; // 62.5ms, never enlarges a short loop
    static constexpr uint32_t ENVELOPE_BINS = 48, ENVELOPE_BLOCK = 64;
    static constexpr uint32_t ENVELOPE_CACHE_BYTES = 2 * (CAPACITY / ENVELOPE_BLOCK) * 2 * sizeof(q15);
    struct Visual {
        q15 wave[48] = {}; // actual signed samples spanning the CURRENT underlying loop
        uint32_t loop_frames = 0, capture_frames = 0;
        uint16_t phase = 0, repeats = 0, wet = 0, fill = 0; // phase/wet/fill: 0..32767
        bool active = false, manual = false, held = false;
        q15 history_wave[48] = {}, reader_wave[48] = {};
        uint32_t history_frames = 0, reader_frames = 0;
        uint16_t reader_phase = 0;
        bool window_active = false; // valid manual window, including its full-span endpoint
        q15 loop_low[ENVELOPE_BINS] = {}, loop_high[ENVELOPE_BINS] = {};
        q15 history_low[ENVELOPE_BINS] = {}, history_high[ENVELOPE_BINS] = {};
    };
    static_assert(sizeof(Visual) <= 704, "bounded FRACT frozen PCM snapshot");
    FractUnit();
    ~FractUnit() override;
    bool ready() const override { return banks_ != nullptr; }
    void process(q15* buf, int frames) override;
    void control_tick() override;
    void reset() override;
    void set_manual_grab(bool down); // runtime-only; call under audio guard
    // Manual-only runtime reader; 0..32767 linearly maps min(2000,loop)..loop.
    // Pending during capture warmup. Never touches Params, capture or DAMAGE clock.
    void set_grab_window(q15 fraction); // under audio guard, no PCM traversal
    Visual visual() const; // <=12240 PCM reads + 1000 complete-block summaries; under guard, never draw
    uint32_t underlying_loop_frames() const { return loop_frames_; } // scalar-only drag projection
    static uint32_t size_frames(q15 size, bool sync, int32_t bpm_q16);
    static const char* size_label(q15 size); // nominal sync note division, seven bins
    static uint32_t length_frames(q15 length, int32_t bpm_q16);
    static int32_t speed_q16(q15 speed); // bipolar; +/-8192 = +/-1x, zero = +.25x
    int32_t transport_bpm_q16() const { return bpm_q16_; }
    void set_transport_bpm_q16(int32_t bpm);
    // Engine-only clock snapshot at the next sample. Fraction/remainder make the
    // legacy integer beat clock exact under arbitrary render-block partitions.
    void set_transport_clock(bool running, uint64_t phase_q32, uint32_t increment_q32,
                             uint32_t remainder = 0, uint32_t divisor = 1);
    void stop_transport(); // cancel grabs smoothly; high HOLD must go low to rearm
    void set_enabled(bool enabled); // bypass/mute cancels and invalidates history
private:
    FractUnit(const FractUnit&) = delete;
    FractUnit& operator=(const FractUnit&) = delete;
    bool capture();
    q15 read_loop(int64_t phase) const;
    q15 read_window(int64_t phase, uint32_t span) const;
    q15 window_sample(int64_t phase, uint32_t span) const;
    q15 repeat_sample();
    void advance_loop(); // original shadow reader: only these wraps apply DAMAGE
    void update_grab_window();
    void advance_grab_window();
    void write_history(int bank, q15 dry);
    void envelope_range(int bank, uint32_t begin, uint32_t count, q15& low, q15& high) const;
    struct EnvelopeBlock { q15 low, high; };
    static_assert(ENVELOPE_CACHE_BYTES == 4096 && sizeof(EnvelopeBlock) == 4);
    EnvelopeBlock envelope_[2][CAPACITY / ENVELOPE_BLOCK]; // validity follows the PCM fill/span, not stale cache bytes
    q15* banks_ = nullptr;
    uint32_t write_ = 0, fill_[2] = {}, capture_start_ = 0;
    uint32_t capture_frames_ = 0, loop_frames_ = 0, auto_remaining_ = 0;
    uint32_t size_ = 4000, length_ = 16000, damage_ = 0, chance_ = 0;
    uint32_t rng_ = 0x46524143u;
    int32_t bpm_q16_ = 120 * 65536, speed_target_ = 65536, speed_ = 65536;
    int32_t wet_ = 0, mix_ = 32767;
    int64_t position_ = 0;
    uint64_t clock_phase_ = 0, last_grid_ = 0;
    uint32_t clock_increment_ = 0, clock_remainder_ = 0, clock_divisor_ = 1;
    uint32_t clock_fraction_step_ = 0;
    int live_bank_ = 0, frozen_bank_ = -1;
    uint16_t repeats_ = 0, edge_remaining_ = 0;
    q15 last_wet_ = 0, edge_from_ = 0;
    bool manual_ = false, held_ = false, pending_grab_ = false;
    bool hold_high_ = false, hold_blocked_ = false, running_ = false, suspended_ = false;
    bool reverse_ = false;
    // Separate temporary reader. Zero span means the untouched original DSP path.
    // Once squeezed, full re-expansion preserves its normalized phase until lift;
    // lift/stop hands back to position_ through an independent <=32-sample declick.
    uint32_t grab_frames_ = 0;
    int64_t grab_position_ = 0;
    q15 grab_fraction_ = 32767, grab_last_wet_ = 0, grab_edge_from_ = 0;
    uint16_t grab_edge_remaining_ = 0;
};

// Sixteen straight sixteenths: current dry input + slices of the previous full bar.
class GlitchUnit : public Unit {
public:
    enum { P_DENSITY=0, P_JUMP, P_REPEAT, P_DROP, P_REVERSE, P_PITCH, P_MIX, P_KEEP, P_COUNT };
    static constexpr int STEPS = 16;
    // 20bpm at 32k = 384000 frames; guards cover floor-Q32 clock quantization.
    static constexpr uint32_t CAPACITY = 384000 + 16;
    enum class Op : uint8_t { Dry=0, Jump, Repeat, Drop, Reverse, Pitch };
    struct Cell { Op op=Op::Dry; uint8_t source=0; int8_t semitones=0; uint8_t repeats=1; };
    struct PatternState {
        Cell cells[STEPS];
        uint32_t rng = 0x474c4954u, revision = 0;
    };
    struct Visual {
        Cell cells[STEPS];
        q15 trace[64] = {};
        uint16_t phase=0, fill=0, wet=0;
        uint8_t step=0, source=0;
        uint32_t revision=0;
        bool running=false, ready=false, kept=false, pending=false;
    };
    GlitchUnit();
    ~GlitchUnit() override;
    bool ready() const override { return banks_ != nullptr; }
    void process(q15* buf, int frames) override;
    void control_tick() override;
    void reset() override; // invalidate PCM/runtime; retain exact pattern + independent RNG
    void request_reroll(); // under guard; coalesced next bar, immediate if stopped
    Visual visual() const; // under guard: <=64 actual previous-bar PCM reads
    PatternState pattern_state() const { return pattern_; }
    static bool valid_pattern(const PatternState& state);
    bool restore_pattern(const PatternState& state); // transactional, never copies PCM
    void set_transport_clock(bool running, uint64_t phase_q32, uint32_t increment_q32,
                             uint32_t remainder=0, uint32_t divisor=1);
    void stop_transport();
    void set_enabled(bool enabled);
private:
    GlitchUnit(const GlitchUnit&) = delete;
    GlitchUnit& operator=(const GlitchUnit&) = delete;
    void invalidate();
    void generate();
    uint32_t random();
    void grid_edge(uint64_t grid);
    void enter_cell();
    q15 slice_sample();
    PatternState pattern_;
    q15* banks_ = nullptr;
    uint32_t cuts_[2][STEPS+1] = {};
    uint32_t write_=0, previous_frames_=0, read_start_=0, read_length_=0;
    int live_bank_=0, previous_bank_=-1;
    uint64_t clock_phase_=0, last_grid_=0, record_bar_=0;
    uint32_t clock_increment_=0, clock_remainder_=0, clock_divisor_=1, clock_fraction_step_=0;
    int64_t read_phase_=0;
    int32_t read_speed_=65536, wet_=0, mix_=32767;
    q15 last_wet_=0, seam_from_=0;
    uint8_t seam_left_=0, step_=0;
    uint16_t visual_phase_=0, visual_fill_=0;
    bool running_=false, suspended_=false, grid_seen_=false, recording_=false;
    bool kept_=false, pending_=false, overflow_=false;
};

class FoldUnit : public Unit {
public:
    explicit FoldUnit(bool modern = true);
    void process(q15* buf, int frames) override;
    void control_tick() override;
    void reset() override;
    enum { P_DRIVE = 0, P_TRIM, P_MODE, P_BIAS, P_MIX, P_SMOOTH, P_TONE, P_COUNT };
    static constexpr int LEGACY_COUNT = 5;
    bool modern() const { return modern_; }
    void set_modern(bool enabled); // caller initializes appended defaults/scenes on explicit conversion
    q15 control_value(int param) const;
    // Positive DRIVE values retain the historical nominal 1..8x range; modern
    // negative values add .25..1x attenuation, without remapping old base/scenes.
    static uint32_t drive_q16(q15 value, bool modern = true);
    static uint32_t tone_hz(q15 value); // 100..16000; 32767 is exact off
    // Actual modern memoryless branch before DC/tone/trim/mix; may exceed +/-1.
    static float transfer(float input, const q15* values);
private:
    void legacy_process(q15* buf, int frames);
    void modern_process(q15* buf, int frames);
    static constexpr int FIR_TAPS = 47;
    bool modern_=true, cache_valid_=false, processed_=false;
    q15 resolved_[P_COUNT]={}, last_out_=0, transition_from_=0;
    int transition_left_=0, fir_head_=0;
    float drive_=1, bias_=0, smooth_=0, trim_=1, mix_=1, tone_alpha_=1;
    float input_previous_=0, dc_x_=0, dc_y_=0, tone_state_=0;
    float fir_[FIR_TAPS]={};
};

// Multi-stage moving feedback distortion. The feedback tap is delayed by the
// engine's complete 32-sample control block, then DC-blocked, darkened and
// saturated before re-entry. All storage is inline and fixed-size.
class StrainUnit : public Unit {
public:
    explicit StrainUnit(bool modern = true);
    void process(q15* buf, int frames) override;
    void control_tick() override;
    void reset() override;

    enum { P_DRIVE = 0, P_BIAS, P_BODY, P_TEETH, P_FEEDBACK, P_MIX,
           P_MOTION, P_TRIM, P_RATE, P_COUNT };
    static constexpr int LEGACY_COUNT = 6, DEFAULT_TRIM = 24576, DEFAULT_RATE = 5702;
    bool modern() const { return modern_; }
    void set_modern(bool enabled); // caller initializes new stored defaults on explicit conversion
    q15 control_value(int param) const;
    static uint32_t rate_microhz(q15 value); // .01..8Hz, exact .032Hz default
    static uint32_t motion_increment(q15 value); // Q32 phase per 1kHz control tick
    static int trim_centidb(q15 value); // -18..+6dB, exact unity default
    static q15 model_motion(q15 body, q15 amount, uint32_t phase, q15 energy = 0);
    uint16_t visual_energy() const { return visual_energy_; }
    int16_t visual_motion() const { return visual_motion_; }

private:
    q15 feedback_delay_[CONTROL_INTERVAL] = {};
    uint8_t feedback_pos_ = 0;
    q31 pre_low_ = 0;
    q31 loop_low_ = 0;
    q31 post_low_ = 0;
    q31 loop_dc_x_ = 0;
    q31 loop_dc_y_ = 0;
    q31 wet_dc_x_ = 0;
    q31 wet_dc_y_ = 0;
    q31 energy_follow_ = 0;
    uint32_t motion_phase_ = 0;
    q15 pre_coeff_ = 4096;
    q15 loop_coeff_ = 4096;
    q15 post_coeff_ = 8192;
    uint16_t visual_energy_ = 0;
    int16_t visual_motion_ = 0;
    bool modern_ = true, processed_ = false;
    q15 last_out_ = 0, transition_from_ = 0;
    uint16_t transition_left_ = 0;
};

// mic input source. the platform layer drains MICU and feeds push();
// the unit reads from a shared static ring (there is one physical mic).
// everything happens under the audio lock, so plain indices are enough.
// if nothing is ever pushed (no mic / emulator) the unit outputs silence.
class MicUnit : public Unit {
public:
    MicUnit();
    void process(q15* buf, int frames) override;
    SequencerProperties sequencer_properties() const override {
        return {true, false, SequenceLifecycle::Flow, -1, P_GAIN, P_GAIN, -1};
    }

    enum { P_GAIN = 0, P_MON, P_LOW_CUT, P_COUNT };
    static int low_cut_mode(q15 raw);           // off / 80Hz / 160Hz, thirds

    static void push(const q15* src, int n);   // call under AudioOut::Guard
    static void reset_input();                 // clear shared device FIFO
    static constexpr int INPUT_VISUAL_SAMPLES = 1048; // ~32ms of native input
    // Non-consuming raw capture. Caller holds AudioOut::Guard, then reduces it
    // outside the guard. Returns only valid samples, oldest first, never PCM UI synthesis.
    static int copy_input(q15* dst, int capacity);
    static uint32_t input_serial() { return input_serial_; }
    uint16_t visual_peak() const { return visual_peak_; }
    uint16_t visual_output_peak() const { return visual_output_peak_; } // last actual block
    bool visual_clip() const { return clip_hold_samples_ != 0; }
    uint32_t visual_process_serial() const { return process_serial_; }
    uint16_t visual_fill() const { return visual_fill_; }
    uint32_t visual_dropped() const { return visual_dropped_; }

private:
    static constexpr uint32_t RING = 4096;     // ~128ms @ 32k, power of two
    static constexpr uint32_t INPUT_STEP_Q16 = 67028; // 32728.498 / 32000
    static q15      ring_[RING];
    static uint32_t wr_, epoch_, input_serial_;
    static uint16_t input_valid_;
    uint64_t read_q16_ = 0;                    // independent reader per MIC card
    uint32_t epoch_seen_ = 0;
    q31 low_state_q8_ = 0;                    // bounded first-order LP state for HP
    q15 visual_peak_ = 0;
    uint16_t visual_output_peak_ = 0;
    uint16_t clip_hold_samples_ = 0;
    uint32_t process_serial_ = 0;
    uint16_t visual_fill_ = 0;
    uint32_t visual_dropped_ = 0;
};

// recorder processor: passes input through while writing it into SampleBank.
// Trigger arms a fresh take. P_REC can also act as a manual gate.
class RecUnit : public Unit {
public:
    RecUnit();
    void process(q15* buf, int frames) override;
    void trigger() override;
    void reset() override;
    void stop();
    void clear_take();
    // Rebind recorder runtime only; never alter a restored/shared Sample's edits.
    void restore_take(uint32_t frames);
    // Initialize a newly imported, private take's sample metadata as well.
    void adopt_take(uint32_t frames);
    uint32_t take_generation() const { return take_generation_; }
    uint32_t saved_generation() const { return saved_generation_; }
    bool take_needs_save() const { return wr_frames_ > 0 && take_generation_ != saved_generation_; }
    bool can_start_fresh() const { return !writer_open() && wr_frames_ == 0; }
    void mark_take_saved(uint32_t generation) { if (!writer_open() && generation == take_generation_) saved_generation_ = generation; }
    bool is_recording() const { return recording_; }
    bool is_paused() const { return paused_; }
    bool writer_open() const { return recording_ || paused_; }
    bool pause();
    bool resume();
    // Runtime-only globally issued identities survive same-address reconstruction.
    // Session stays fixed across segments; state changes invalidate queued actions.
    uint64_t session_token() const { return session_token_; }
    uint64_t state_token() const { return state_token_; }
    uint32_t recorded_frames() const { return wr_frames_; }
    void capture_input(const q15* buf, int frames);
    uint32_t capacity_frames() const;
    uint32_t target_frames() const;
    uint32_t active_target_frames() const { return target_frames_; }
    // Audio-owned visualization cache; drawing still consumes EngineUiSnapshot.
    // Exact full-duration bins for captured takes. Imported/replaced PCM reduces
    // progressively (512 frames/bin/call), never a whole-take guarded scan.
    static constexpr int WAVE_POINTS = 64;
    void copy_visual_wave(q15* out) const;
    trackr::synth::SampleEditResult prepare_slot();
    static trackr::synth::SampleEditResult stage_workspace(uint32_t frames, trackr::synth::Sample& out);
    void adopt_workspace();
    uint32_t required_capacity_frames() const;

    int sample_slot = 16;                      // avoid overwriting loaded s00..s15 by default
    enum { P_REC = 0, P_LEN, P_MON };

private:
    friend class Engine;
    static constexpr uint32_t MAX_REC_FRAMES = ENGINE_SR * 20; // 20s cap, mono
    bool recording_ = false;
    bool paused_ = false;
    bool gate_started_ = false;
    uint64_t session_token_ = 0, state_token_ = 0;
    int writer_slot_ = -1;
    uint32_t writer_generation_ = 0, writer_capacity_ = 0;
    const q15* writer_data_ = nullptr;
    bool writer_storage_matches() const;
    void renew_session();
    uint32_t wr_frames_ = 0;
    uint32_t target_frames_ = 0;
    uint32_t take_generation_ = 0;
    uint32_t saved_generation_ = 0;

    static constexpr uint32_t WAVE_BLOCK = 256;
    static constexpr uint32_t WAVE_BLOCKS = (MAX_REC_FRAMES + WAVE_BLOCK - 1) / WAVE_BLOCK;
    struct WaveCache {
        // Only blocks fully covered by streamed_frames may be queried. New block
        // starts overwrite old peaks, so invalidation does not clear this array.
        q15 blocks[WAVE_BLOCKS];
        q15 points[WAVE_POINTS] = {};
        const q15* data = nullptr;
        size_t elements = 0;
        uint32_t bank_generation = 0;
        uint32_t streamed_frames = 0;
        uint32_t displayed_frames = UINT32_MAX;
        uint32_t reduced_per_bin = 0;
        int slot = -1;
        uint8_t channels = 0;
        bool streaming = false;
        bool ready = false;
    };
    mutable WaveCache wave_;
    void invalidate_visual_wave() const;
    void bind_visual_wave(const trackr::synth::Sample& sample) const;
};

class BitUnit : public Unit {
public:
    explicit BitUnit(bool modern = true);
    void process(q15* buf, int frames) override;
    // Private UI reference uses the exact processor. Optional per-sample flags:
    // bit0=captured source, bit1=dropout muted wet. No live PCM storage is added.
    void process_with_events(q15* buf, uint8_t* events, int frames);
    void control_tick() override;
    void reset() override;
    enum { P_BITS = 0, P_RATE, P_MIX, P_JITTER, P_DROPOUT, P_DRIFT, P_COMP,
           P_DRIVE, P_TONE, P_GAIN, P_DITHER, P_DROP_TIME, P_COUNT };
    static constexpr int LEGACY_COUNT = 7;
    bool modern() const { return modern_; }
    void set_modern(bool enabled);
    q15 control_value(int param) const;
    static int bit_depth(q15 value);
    static uint32_t rate_millihz(q15 value);
    static uint32_t tone_millihz(q15 value); // max=off
    static uint32_t drop_frames(q15 value);
    static int drive_centidb(q15 value);
    static int gain_centidb(q15 value); // raw26214 is exactunity
    static int drift_millihz(q15 value);

    uint8_t visual_bits() const { return visual_bits_; }
    uint8_t visual_period() const { return visual_period_; }
    q15 visual_held() const { return visual_held_; }
    bool visual_dropout() const { return visual_dropout_; }

private:
    q15 held_ = 0;
    uint32_t count_ = 0;
    uint32_t rng_ = 0xB17C0DEu;
    // Unsigned phase gives defined wrap for both positive and negative bipolar
    // DRIFT instead of eventually overflowing a signed accumulator.
    uint32_t drift_phase_ = 0;
    uint8_t visual_bits_ = 3;
    uint8_t visual_period_ = 1;
    q15 visual_held_ = 0;
    bool visual_dropout_ = false;
    bool modern_ = true, cache_valid_ = false, processed_ = false, legacy_drop_ = false;
    q15 resolved_[P_COUNT] = {}, last_out_ = 0, transition_from_ = 0;
    uint16_t transition_left_ = 0;
    uint32_t clock_rng_ = 0x0B17C0DEu, drop_rng_ = 0xD09F0A17u, dither_rng_ = 0xD17E1234u;
    uint32_t modern_phase_ = 0, phase_inc_ = 0, drop_left_ = 0, drop_total_ = 32;
    float remaining_ = 0, modern_held_ = 0, low_ = 0, dc_input_ = 0, dc_output_ = 0;
    float interval_ = 1, drive_ = 1, gain_ = 1, alpha_ = 1, jitter_ = 0, comp_ = 0, dither_ = 0;
    int depth_ = 16;
    bool neutral_ = false;
    void modern_process(q15* buf, uint8_t* events, int frames);
    void legacy_process(q15* buf, uint8_t* events, int frames);
};

class ChorusUnit : public Unit {
public:
    ChorusUnit();
    ~ChorusUnit() override;
    void process(q15* buf, int frames) override;
    void reset() override;
    bool ready() const override { return buf_ != nullptr; }
    enum { P_RATE = 0, P_DEPTH, P_MIX, P_SPREAD, P_COUNT };
    // Runtime-only compare; callers guard reads/writes with audio ownership.
    // Refresh true each owned UI frame. Missing refresh expires after 250ms of
    // processed audio; stopped/bypassed/context changes still require UI cancel.
    static constexpr uint32_t DRY_COMPARE_LEASE_FRAMES = ENGINE_SR / 4;
    static constexpr uint16_t DRY_COMPARE_RAMP_FRAMES = 128; // 4ms at 32kHz
    void set_dry_compare(bool held) { compare_lease_ = held ? DRY_COMPARE_LEASE_FRAMES : 0; }
    bool dry_compare_requested() const { return compare_lease_ != 0; }
    uint16_t visual_compare_q15() const { return compare_q15_; } // 0..32768, exact dry at end
    uint16_t visual_phase() const { return visual_phase_; }
    uint16_t visual_delay() const { return visual_delay_; } // voice 0, whole samples
    // Last actually processed sample, not a prediction from current parameters.
    // Copy together under the audio guard; invalid indices clamp to voice 0/1.
    uint32_t visual_delay_q16(int voice) const { return visual_delay_q16_[voice <= 0 ? 0 : 1]; }
    bool visual_ready() const { return visual_ready_; }
    q15 visual_effective(int param) const {
        return visual_effective_[param < P_RATE ? P_RATE : param > P_SPREAD ? P_SPREAD : param];
    }
private:
    static constexpr uint32_t SIZE = 2048;
    q15* buf_ = nullptr;
    uint32_t write_ = 0, phase_ = 0;
    uint16_t visual_phase_ = 0, visual_delay_ = 96;
    uint32_t visual_delay_q16_[2] = {96u << 16, 96u << 16};
    q15 visual_effective_[P_COUNT] = {}; // clamped raw values used by the last block
    uint32_t compare_lease_ = 0;
    uint16_t compare_q15_ = 0;
    bool visual_ready_ = false;
};

class ReverbUnit : public Unit {
public:
    explicit ReverbUnit(bool modern = true);
    ~ReverbUnit() override;
    void process(q15* buf, int frames) override;
    void reset() override;
    bool ready() const override { return modern_ ? dense_ != nullptr : buf_ != nullptr; }
    enum { P_SIZE = 0, P_DECAY, P_MIX, P_DAMP, P_FREEZE,
           P_PREDELAY, P_LOW_CUT, P_DUCK, P_COUNT };
    bool modern() const { return modern_; }
    // Setup-only compatibility selection. Failure preserves the entire live unit;
    // success clears its tail. No automatic upgrade or interactive mode gesture.
    bool set_modern(bool modern);
    // Last accepted nominal size control; during a transition this is the
    // interpolated control coordinate, not a claim of one physical delay time.
    // Copy under the audio guard along with the existing meters.
    q15 visual_size() const;
    bool visual_transition() const;
    q15 visual_energy() const { return visual_energy_; }
    q15 visual_damping() const { return visual_damping_; }
    q15 visual_freeze() const { return freeze_mix_; }
    bool visual_ready() const { return visual_ready_; }
    // Accepted raw targets, never UI targets awaiting processing. SIZE/PREDELAY
    // report the committed destination, not a coalesced pending edit. SIZE/DEC/
    // DAMP stay captured in modern freeze. Use visual_size/damping for ramps.
    q15 visual_effective(int p) const;
    q15 visual_return(int lane) const; // four raw tap envelopes; legacy returns 0
    q15 visual_duck_gain() const;      // applied wet-only gain; unity=32767
private:
    void process_legacy(q15* buf, int frames);
    void process_modern(q15* buf, int frames);
    bool modern_ = false;
    reverb_dsp::State* dense_ = nullptr;
    // Legacy storage/arithmetic are frozen for historical projects.
    static constexpr int N = 8192;
    q15* buf_ = nullptr;
    uint32_t a_ = 0, b_ = 0, c_ = 0;
    q31 damp_a_ = 0, damp_b_ = 0, damp_c_ = 0;
    q15 visual_size_ = -1; // unprocessed legacy sentinel, never serialized
    q15 freeze_mix_ = 0;
    q15 visual_energy_ = 0;
    q15 visual_damping_ = 0;
    q15 visual_effective_[P_COUNT] = {};
    bool visual_ready_ = false;
};

class TiltUnit : public Unit {
public:
    enum class Revision : uint8_t { Legacy=0, Clear=1, Extended=2 };
    TiltUnit(); // new cards only: extended reciprocal EQ
    explicit TiltUnit(bool modern); // preserved caller meaning: legacy5 / clear6
    explicit TiltUnit(Revision revision);
    void process(q15* buf, int frames) override;
    void reset() override;
    void control_tick() override;
    enum { P_TILT = 0, P_FREQ, P_DRIVE, P_MIX, P_RANGE, P_TRIM, P_COUNT };
    bool modern() const { return revision_ != Revision::Legacy; }
    bool extended() const { return revision_ == Revision::Extended; }
    Revision revision() const { return revision_; }
    void set_modern(bool modern); // compatibility, never means extended
    void set_revision(Revision revision); // setup/explicit extend, not modulation
    q15 control_value(int param) const;
private:
    void legacy_process(q15* buf,int frames);
    void extended_process(q15* buf,int frames);
    Revision revision_=Revision::Extended;
    bool modern_=true,cache_valid_=false,processed_=false;
    q31 low_=0; // original legacy/clear state, unchanged arithmetic
    int64_t eq_state_=0; // Q12 fractional sample precision
    q15 previous_input_=0,resolved_[P_COUNT]={},last_out_=0,transition_from_=0;
    int32_t b0_=0,b1_=0,a1_=0;
    uint32_t trim_gain_=65536;
    uint16_t transition_left_=0;
};

// First six indices and the legacy processor remain frozen. Modern controls
// append within the existing bank; old files opt in only through a versioned flag.
class RingUnit : public Unit {
public:
    explicit RingUnit(bool modern=true);
    void process(q15* buf, int frames) override;
    void process_with_carrier(q15* buf, q15* carrier, int frames);
    void reset() override;
    void control_tick() override;
    enum { P_FREQ=0, P_DEPTH, P_FEEDBACK, P_DIODE, P_MIX, P_FOLLOW,
           P_DRIVE, P_FOLLOW_TIME, P_WAVE, P_NOTE, P_MODE, P_FINE, P_COUNT };
    static constexpr int LEGACY_COUNT=6;
    bool modern() const { return modern_; }
    void set_modern(bool enabled);
    q15 control_value(int param) const;
    static uint32_t frequency_millihz(q15 value);
    static uint32_t note_millihz(q15 value);
    static uint32_t follow_time_us(q15 value);
    static int drive_centidb(q15 value);
    static int fine_centicents(q15 value);
    static int wave_index(q15 value);
    static uint32_t base_millihz(const q15* values);
    uint16_t visual_phase() const { return visual_phase_; }
    uint16_t visual_envelope() const { return visual_envelope_; }
private:
    void legacy_process(q15* buf, int frames);
    void modern_process(q15* buf, q15* carrier, int frames);
    bool modern_=true,cache_valid_=false,processed_=false;
    uint32_t phase_ = 0;
    q15 feedback_ = 0;
    q31 dc_x_ = 0, dc_y_ = 0;
    q15 envelope_ = 0;
    uint16_t visual_phase_ = 0, visual_envelope_ = 0;
    q15 resolved_[P_COUNT]={},last_out_=0,transition_from_=0;
    uint16_t transition_left_=0;
    uint32_t modern_phase_=0;
    float base_hz_=0,release_=0,drive_=1,trim_=1,amount_=0,feedback_gain_=0,diode_=0,depth_=0;
    float modern_env_=0,modern_feedback_=0,modern_dc_x_=0,modern_dc_y_=0;
    float input_previous_=0,sub_input_=0,triangle_=0,pulse_=1,last_carrier_=0;
    static constexpr int FIR_TAPS=47;
    float wet_fir_[FIR_TAPS]={},carrier_fir_[FIR_TAPS]={};
    int fir_head_=0,shape_=0;
};

// Signed frequency translation, not pitch transposition or two-sideband ring mod.
class ShiftUnit : public Unit {
public:
    ShiftUnit();
    void process(q15* buf, int frames) override;
    void control_tick() override;
    void reset() override;
    // Append-only: historical SHIFT patches have zero in the unused DAMP slot.
    enum { P_SHIFT = 0, P_FEEDBACK, P_MIX, P_DAMP, P_COUNT };
    static int32_t shift_millihz(q15 value);
    static uint16_t feedback_permille(q15 value);
    static int32_t damping_alpha(q15 value); // exact one-pole coefficient, Q30
private:
    shift_dsp::Allpass a_[4], b_[4];
    shift_dsp::Ramp increment_, feedback_amount_, mix_, damp_alpha_;
    uint32_t phase_ = 0;
    int32_t b_delay_ = 0, feedback_ = 0, input_dc_ = 0, output_dc_ = 0;
    int32_t damped_feedback_ = 0; // Q23, feedback-return-only lowpass state
    bool coefficients_ready_ = false;
};

class GhostUnit : public Unit {
public:
    GhostUnit();
    ~GhostUnit() override;
    void process(q15* buf, int frames) override;
    void control_tick() override;
    void reset() override;
    bool ready() const override { return memory_ != nullptr; }
    // Append-only: old patches contain zeroes in these formerly unused slots.
    enum { P_TONE = 0, P_DECAY, P_SMEAR, P_DRIFT, P_FREEZE, P_MIX, P_FINE, P_RATE };
    q15 visual_energy() const { return visual_energy_; }
    q15 visual_drift() const { return visual_drift_; }
    q15 visual_freeze() const { return freeze_mix_; }
    static uint16_t root_hz(q15 tone);
    static uint32_t root_millihz(q15 tone, q15 fine);
    static uint32_t drift_period_ms(q15 rate);
    static uint32_t drift_increment(q15 rate);
    static uint32_t fine_delay_ratio(q15 fine); // q24, +/-50 cents
    bool long_tail = true; // saved Ghost UNIT bit0; absent/zero loads exact legacy
private:
    int32_t damp_residue_[4] = {}; // long only: retain sub-LSB lowpass updates
    int32_t tap_residue_[4] = {}, feedback_residue_[4] = {};
    q15 tail_gain_ = 0;
    bool tail_ready_ = false, tail_mode_ = false, tail_ramping_ = false;
    static constexpr uint32_t SIZE = 4096;
    static constexpr int BANDS = 4;
    q15* memory_ = nullptr;
    uint32_t write_[BANDS] = {};
    uint32_t phase_ = 0;
    q15 damp_[BANDS] = {};
    q15 freeze_mix_ = 0;
    bool freeze_latched_ = false;
    uint16_t frozen_delay_[BANDS] = {};
    q15 visual_energy_ = 0;
    q15 visual_drift_ = 0;
};

class StranularUnit : public Unit {
public:
    StranularUnit();
    ~StranularUnit() override;
    void process(q15* buf, int frames) override;
    void reset() override;
    bool ready() const override { return buffer_ != nullptr; }
    SequencerProperties sequencer_properties() const override {
        // Live granular is a processor, not an overwrite-source. Its metadata
        // remains available as a standalone frozen-buffer sequencer fallback.
        return {false, false, SequenceLifecycle::Flow, P_PITCH, P_MIX, P_DENS, P_SIZE};
    }
    enum { P_POS = 0, P_SIZE, P_DENS, P_PITCH, P_FREEZE, P_MIX,
           P_SPRAY, P_REVERSE, P_PITCH_ENV, P_HOLD, P_SYNC, P_CLUSTER };
    // All performance controls are ordinary ParamBank entries and therefore
    // receive the same modulation, slew, scene, and SNC7 behavior.
    uint8_t sync_division = 0; // legacy migration/UI sub-mode: 0=1/16..3=1/2
    uint32_t sync_interval_frames = 4000;
    void set_transport_bpm(int bpm);
    void set_transport_bpm_q16(int32_t bpm_q16);
    static int32_t pitch_increment_q16(q15 pitch);
    static uint8_t cluster_count(q15 value);
    uint8_t window_mode = 0; // 0 triangle, 1 Hann-ish, 2 trapezoid
    bool long_mode = false;  // longer scan/grain range, same fixed allocation
    static constexpr int VIZ_POINTS = 40;
    static constexpr int MAX_GRAINS = 8;
    // Audio thread or caller holding the audio guard; copy before GPU drawing.
    // Lazily materializes the same last 64-sample view, not a newer ring snapshot.
    const q15* visual_wave() const;
    const uint8_t* visual_positions() const { return visual_positions_; }
    uint16_t visual_grains() const { return visual_grains_; }
    bool visual_frozen() const { return frozen_; }
    uint16_t visual_fill() const { return visual_fill_; }
    q15 visual_mix() const { return visual_mix_; }
    const int32_t* visual_increments() const { return visual_increments_; }
    // Read-only controller metadata; caller holds AudioOut::Guard. No DSP state changes.
    uint32_t scan_span_frames() const {
        return frozen_ ? freeze_span_ : (long_mode ? filled_ : (filled_ < 16384u ? filled_ : 16384u));
    }
    uint8_t active_grain_count() const {
        uint8_t count = 0;
        for (const auto& grain : grains_) if (grain.active) ++count;
        return count;
    }

private:
    // Two seconds and eight voices are deliberately fixed for Old 3DS. No audio
    // path allocation and no quality branch that silently overloads old hardware.
    static constexpr uint32_t BUFFER_SIZE = 65536; // power-of-two, ~2.05 s
    struct Grain {
        bool active = false;
        int64_t pos_q16 = 0;
        int32_t inc_q16 = 65536;
        int32_t pitch_delta_q16 = 0;
        uint32_t age = 0;
        uint32_t length = 0;
    } grains_[MAX_GRAINS];
    q15* buffer_ = nullptr;
    uint32_t write_ = 0;
    uint32_t filled_ = 0;
    uint32_t spawn_countdown_ = 0;
    int32_t clock_bpm_q16_ = 0;
    uint8_t clock_division_ = 0xff;
    uint16_t clock_countdown_fraction_ = 0;
    uint32_t rng_ = 0x51724A31u;
    bool frozen_ = false;
    uint32_t freeze_write_ = 0;
    uint32_t freeze_filled_ = 0;
    uint32_t freeze_start_ = 0;
    uint32_t freeze_span_ = 0;
    q15 freeze_mix_ = 0;
    mutable q15 visual_wave_[VIZ_POINTS] = {};
    uint16_t visual_grains_ = 0;
    uint8_t visual_positions_[MAX_GRAINS] = {};
    uint16_t visual_fill_ = 0;
    q15 visual_mix_ = 0;
    int32_t visual_increments_[MAX_GRAINS] = {};
    uint32_t visual_decim_ = 0;
    static constexpr uint32_t VIZ_INTERVAL = 64;
    // Until the next 64-sample tick at most 63 ring writes can occur. Only the
    // oldest wave point can be overwritten in that time; preserve it eagerly.
    static_assert((BUFFER_SIZE - 1) / (VIZ_POINTS - 1) > VIZ_INTERVAL,
                  "deferred wave requires all other points outside the write horizon");
    uint32_t visual_wave_span_ = 0, visual_wave_reference_ = 0;
    q15 visual_wave_oldest_ = 0;
    mutable bool visual_wave_dirty_ = false;

    uint32_t random_u32() { rng_ = rng_ * 1664525u + 1013904223u; return rng_; }
    void spawn_grain(uint32_t length, int32_t increment, uint32_t position, uint32_t spray);
};

// Factory and persistent-configuration clone used by the UI clipboard and tests.
// clone_unit deliberately starts with fresh DSP/runtime state; it copies only the
// controls that survive a patch save (params, bypass, and kind-specific modes).
Unit* make_unit(UnitKind k);
Unit* clone_unit(const Unit* source);

} // namespace seance
