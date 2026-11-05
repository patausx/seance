// engine - 4 chains x 6 units, 4 sigils, control-rate modulation router.
//
// render path (called from the audio worker, mono per chain -> stereo master):
//   for each CONTROL_INTERVAL block:
//     1. env follower on mic/chain input
//     2. sigils step in order A->D; a sigil may modulate a LATER sigil's rate
//     3. every unit param resolves (base + mod)
//     4. units control_tick()
//     5. each chain processes its unit pipe, output accumulates to master
//
// no locks here - the engine is owned by the audio thread; ui talks to it
// through the same RecursiveLock pattern as descry's mixer.
#pragma once
#include "unit.h"
#include "units.h"
#include "sigil.h"
#include "clock_mod.h"
#include "transition_fx.h"
#include "bounce.h"
#include "sigil_config.h"
#include "../dsp/filter.h"
#include "../synth/sampler.h"

namespace seance {

class RecUnit;

constexpr int NUM_CHAINS      = 4;
constexpr int UNITS_PER_CHAIN = 6;
constexpr int NUM_SIGILS      = 8;
constexpr int CONTROL_RATE    = ENGINE_SR / CONTROL_INTERVAL;   // 1000 hz
constexpr int SEQ_STEPS       = 64;
constexpr int SEQ_LOCKS       = 4;
constexpr int NUM_PATTERNS    = 8;
constexpr int PATTERN_CHAIN_STEPS = 16;
// Signed per-step offset in 1/96ths of a sixteenth.  The limit stays just
// below a quarter-step so adjacent events cannot cross even at 75% swing.
constexpr int SEQ_MICROTIMING_DIVISIONS = 96;
constexpr int SEQ_MICROTIMING_MAX       = 23;

struct StepLock { uint8_t slot=0, param=0; q15 value=0; };
struct SeqStep {
    enum : uint8_t {
        Trig = 1u << 0,
        ConditionShift = 1,
        ConditionMask = 7u << ConditionShift,
        RetrigShift = 4,
        RetrigMask = 3u << RetrigShift
    };
    enum class Condition : uint8_t { Always = 0, Half, Quarter, ThreeQuarter, FirstOfTwo, SecondOfTwo, Fill, NotFill, Count };
    uint8_t flags = 0;
    uint8_t lock_count = 0;
    StepLock locks[SEQ_LOCKS];
    Condition condition() const { return (Condition)((flags & ConditionMask) >> ConditionShift); }
    void set_condition(Condition c) {
        flags = (uint8_t)((flags & ~ConditionMask) | (((uint8_t)c << ConditionShift) & ConditionMask));
    }
    uint8_t retrig_count() const { return (uint8_t)(((flags & RetrigMask) >> RetrigShift) + 1); }
    void set_retrig_count(uint8_t count) {
        if (count < 1) count = 1;
        if (count > 4) count = 4;
        flags = (uint8_t)((flags & ~RetrigMask) | (((count - 1u) << RetrigShift) & RetrigMask));
    }
};
enum class SequenceDirection : uint8_t { Forward = 0, Reverse, PingPong, Count };
struct Pattern {
    uint8_t length[NUM_CHAINS] = {16,16,16,16};
    SequenceDirection direction[NUM_CHAINS] = {
        SequenceDirection::Forward, SequenceDirection::Forward,
        SequenceDirection::Forward, SequenceDirection::Forward
    };
    SeqStep steps[NUM_CHAINS][SEQ_STEPS];
    int8_t microtiming[NUM_CHAINS][SEQ_STEPS] = {};
};
// Runtime-only event-selection blend, never serialized into a bank or patch.
struct PatternTransitionState {
    bool active = false;
    uint8_t source = 0, destination = 1;
    q15 position = 0;
};
struct PatternChainStep {
    uint8_t pattern = 0;
    uint8_t bars = 1;
};

struct PersistentScene {
    bool valid = false;
    UnitKind kind[UNITS_PER_CHAIN] = {};
    q15 base[UNITS_PER_CHAIN][MAX_PARAMS] = {};
    q15 volume = trackr::fx::Q15_ONE;
    q15 pan = 0;
};
struct MacroPadRoute { uint8_t chain=0, slot=0, param=0; q15 minimum=0, maximum=trackr::fx::Q15_ONE; };
struct MacroPad { static constexpr int MAX_ROUTES=8; char name[12]={}; uint8_t route_count=0; MacroPadRoute routes[MAX_ROUTES]; };

// Persistent physical-control map for the instrument-style Performance view.
// Controls are stable logical chords rather than raw platform key masks, keeping
// patch files portable and allowing the UI to learn/edit them safely.
enum class PerformanceControl : uint8_t {
    FaceA, FaceB, FaceX, FaceY,
    LFaceA, LFaceB, LFaceX, LFaceY,
    LRFaceA, LRFaceB, LRFaceX, LRFaceY,
    DpadUp, DpadRight, DpadDown, DpadLeft,
    LDpadUp, LDpadRight, LDpadDown, LDpadLeft,
    ShoulderR, Start, Count
};
enum class PerformanceAction : uint8_t {
    None, ChainTrigger, PatternLaunch, MacroPad, Fill, Transport, ChainMute, GlobalFx, Retrigger, Count
};
enum class GlobalFxTarget : uint8_t {
    Cutoff, Resonance, Reverb, Delay, Crush, Drive, Freeze, Wash, Count
};
enum class PerformanceMode : uint8_t { Press, Hold, Toggle, Count };
struct PerformanceBinding {
    PerformanceAction action = PerformanceAction::None;
    uint8_t target = 0;
    PerformanceMode mode = PerformanceMode::Press;
};
constexpr int NUM_PERFORMANCE_BINDINGS = (int)PerformanceControl::Count;

struct SampleSlotRefs {
    uint8_t play = 0, grain = 0, rec = 0, bounce = 0, external = 0;
    uint8_t scratch = 0; // appended: old aggregate field order retained
    uint8_t paul = 0; // append-only source alias counter
    uint8_t total() const { return (uint8_t)(play + grain + rec + bounce + external + scratch + paul); }
};

struct RecWorkspacePlan {
    RecUnit* rec = nullptr;
    int original_slot = -1;
    int target_slot = -1;
    uint32_t target_generation = 0;
    uint32_t original_generation = 0, take_generation = 0, recorded_frames = 0;
    uint64_t session_token = 0, state_token = 0;
    const q15* original_data = nullptr;
    const q15* target_data = nullptr;
    size_t original_elements = 0, target_elements = 0;
    uint32_t original_logical_frames = 0, target_logical_frames = 0;
    int8_t chain = -1, unit_slot = -1;
    uint32_t frames = 0;
    bool needs_commit = false;
    // Controller consent for this exact take only; never marks audio as saved.
    bool discard_unsaved = false;
    trackr::synth::Sample staged;
};

struct Chain {
    Unit*  units[UNITS_PER_CHAIN] = {};   // owned, heap (512k stack!)
    q15    volume = trackr::fx::Q15_ONE;
    q15    pan    = 0;                     // bipolar: -1 = left, 0 = center, +1 = right
    bool   mute   = false;
    q15    scratch[CONTROL_INTERVAL];     // block buffer
};

enum class LaunchQuantize : uint8_t { Immediate = 0, Sixteenth, Beat, Bar };
enum class RecorderCommand : uint8_t { None = 0, Start, Stop, Pause, Resume };

// Runtime-only accepted sequencer strike, published through the UI snapshot
// mailbox. Source bank/step belong to the scheduled event (including lookahead),
// not the currently selected bank. Expired/hidden records use all sentinels.
struct SequenceHit {
    uint16_t age_ms = 65535;
    uint8_t step = 255, bank = 255;
    uint16_t duration_ms = 140;
};

struct EngineUiSnapshot {
    bool transport_playing = false;
    uint64_t beat_count = 0;
    uint32_t frame_in_beat = 0;
    int bpm = 120;
    uint8_t beats_per_bar = 4;
    LaunchQuantize launch_quantize = LaunchQuantize::Immediate;
    uint8_t pending_triggers = 0, pending_rec = 0;
    uint8_t rec_recording = 0;
    uint8_t rec_has_take = 0;
    uint8_t rec_unsaved_take = 0;
    bool sequencer_enabled = false;
    bool sequencer_fill_active = false;
    uint8_t swing_percent = 50;
    uint8_t active_pattern = 0;
    uint8_t pending_pattern = 0xff;
    LaunchQuantize pattern_launch_quantize = LaunchQuantize::Bar;
    bool pattern_chain_playing = false;
    bool pattern_chain_start_pending = false;
    // Pending start's latched tuple; live bank remains current_pattern below.
    uint8_t pattern_chain_armed_pattern = 0xff;
    uint8_t pattern_chain_armed_bars = 0;
    bool pattern_chain_paused = false;
    bool pattern_chain_held = false;
    bool pattern_chain_next_pending = false;
    // Explicit NEXT's latched target, not position+1 after live edits. 0xff
    // position/pattern + zero bars denotes queued nonloop end (or no queue).
    uint8_t pattern_chain_next_position = 0xff;
    uint8_t pattern_chain_next_pattern = 0xff;
    uint8_t pattern_chain_next_bars = 0;
    uint8_t pattern_chain_current_bars = 0;
    uint8_t pattern_chain_current_pattern = 0;
    PatternChainStep pattern_chain_steps[PATTERN_CHAIN_STEPS];
    bool pattern_chain_loop = true;
    uint8_t pattern_chain_length = 0;
    uint8_t pattern_chain_position = 0;
    uint8_t pattern_chain_bars_remaining = 0;
    PatternTransitionState pattern_transition;
    uint8_t sequence_step[NUM_CHAINS] = {};
    uint8_t sequence_length[NUM_CHAINS] = {};
    uint8_t sequence_direction[NUM_CHAINS] = {};
    SequenceHit sequence_hit[NUM_CHAINS];
    q15 chain_peak[NUM_CHAINS] = {};
    // Post-limiter stereo peak follower + audio progress, visualization only.
    // Unlike chain meters this includes pan, summing/cancellation and master FX.
    uint16_t master_peak = 0;
    uint32_t master_serial = 0;
    // One focused PLAY cursor/window record. Static waveform/chops stay outside
    // this hot payload and are copied as a bounded DTO only when its page draws.
    int8_t play_focus_chain = -1;
    int8_t play_focus_slot = -1;
    bool play_focus_valid = false;
    uint8_t play_state = 0;
    uint16_t play_position = 0;
    uint16_t play_start = 0;
    uint16_t play_end = 0;
    int16_t play_speed = 0;
    uint8_t rec_slot[NUM_CHAINS][UNITS_PER_CHAIN] = {};
    uint32_t rec_frames[NUM_CHAINS][UNITS_PER_CHAIN] = {};
    uint32_t rec_capacity[NUM_CHAINS][UNITS_PER_CHAIN] = {};
    // Only the REC page currently in focus gets waveform samples. Metadata above
    // remains complete for all 24 positions, while the hot snapshot payload stays
    // bounded to one 64-point trace instead of 24 traces.
    static constexpr int REC_WAVEFORM_POINTS = 64;
    int8_t rec_waveform_chain = -1;
    int8_t rec_waveform_slot = -1;
    // Focused REC runtime state reuses rec_waveform_valid as a truthful activity
    // tag: bit0 waveform DTO valid, bit1 recording, bit2 pending start/stop,
    // bit3 unsaved take, bit4 paused. Metadata remains nonserialized and slot-specific.
    uint8_t rec_waveform_valid = 0;
    uint64_t rec_waveform_session = 0; // runtime identity of the published take
    q15 rec_waveform[REC_WAVEFORM_POINTS] = {};
    // Patch-state mirror for lock-free drawing. The audio worker publishes this
    // through AudioOut's ownership mailbox, so per-frame draw code never has to
    // contend with Engine::render for the engine lock.
    bool chain_mute[NUM_CHAINS] = {};
    q15 chain_volume[NUM_CHAINS] = {};
    q15 chain_pan[NUM_CHAINS] = {};
    // This 48-byte payload has two mutually exclusive meanings. Chains uses the
    // generic signed strongest-effect bank; Sigils uses one route count + unsigned
    // live activity byte per module. The view hint selects which record is valid,
    // keeping the already-full hot handoff bounded.
    struct SigilPatchMap {
        uint8_t route_count[NUM_CHAINS][UNITS_PER_CHAIN];
        uint8_t activity[NUM_CHAINS][UNITS_PER_CHAIN];
    };
    union {
        q15 unit_mod_effect[NUM_CHAINS][UNITS_PER_CHAIN] = {};
        SigilPatchMap sigil_patch;
    };
    uint16_t vca_envelope[NUM_CHAINS][UNITS_PER_CHAIN] = {};
    uint8_t vca_state[NUM_CHAINS][UNITS_PER_CHAIN] = {};
    int8_t filter_focus_chain = -1;
    int8_t filter_focus_slot = -1;
    bool filter_focus_valid = false;
    uint16_t filter_cutoff = 0;
    uint16_t filter_resonance = 0;
    uint16_t filter_drive = 0;
    uint16_t filter_morph = 0;
    int8_t fold_focus_chain = -1;
    int8_t fold_focus_slot = -1;
    bool fold_focus_valid = false;
    // exactly one card can hold focus, so effective values for FOLD, BIT, GRAN,
    // STRN, STRAIN, REVERB, CHORUS, TILT, and RING share one bank instead of bloating the hot snapshot payload.
    q15 focus_effective[PARAM_BANK_SIZE] = {};
    // Focused-only REVERB chamber diagnostics. Effective controls use the shared
    // bank above; these two values are actual DSP state, not a UI-side animation.
    int8_t reverb_focus_chain = -1;
    int8_t reverb_focus_slot = -1;
    bool reverb_focus_valid = false;
    uint16_t reverb_energy = 0;
    uint16_t reverb_freeze = 0;
    // One focused BIT/CHORUS/RING/GHOST/generator/STRAIN diagnostics record. The unioned names are safe:
    // exactly one card can own focus, and the focused kind defines interpretation.
    int8_t effect_focus_chain = -1;
    int8_t effect_focus_slot = -1;
    uint8_t effect_focus_kind = (uint8_t)UnitKind::Empty;
    bool effect_focus_valid = false;
    union { uint8_t bit_depth = 3; uint8_t chorus_phase_hi; uint8_t ring_phase_hi; uint8_t ghost_energy_hi; uint8_t generator_phase_hi; };
    union { uint8_t bit_period = 1; uint8_t chorus_phase_lo; uint8_t ring_phase_lo; uint8_t ghost_energy_lo; uint8_t generator_phase_lo; };
    union { q15 bit_held = 0; uint16_t chorus_delay; uint16_t ring_envelope; q15 ghost_drift; uint16_t mic_peak; uint16_t generator_energy; };
    union { bool bit_dropout = false; uint8_t ghost_freeze_hi; uint8_t mic_fill_hi; uint8_t fm_index_envelope; };
    union { uint8_t effect_payload_lo = 0; uint8_t mic_fill_lo; uint8_t fm_feedback_envelope; };
    bool effect_focus_audible = false;
    // Focused-only GRAN cloud field. Positions share the sample display domain
    // used by the lens page, so markers never mix scheduler and waveform space.
    static constexpr int GRAN_GRAIN_POINTS = 8;
    int8_t gran_focus_chain = -1;
    int8_t gran_focus_slot = -1;
    bool gran_focus_valid = false;
    uint8_t gran_positions[GRAN_GRAIN_POINTS] = {};
    uint8_t gran_phases[GRAN_GRAIN_POINTS] = {};
    uint16_t gran_grains = 0;
    uint8_t gran_active = 0;
    uint8_t gran_limit = 1;
    uint16_t gran_interval = 0;
    // Focused-only STRN live field. Waveform and grain positions share the same
    // chronological scan/freeze domain, so the renderer never mixes ring-space
    // markers with history-space audio.
    static constexpr int STRN_WAVE_POINTS = 40;
    static constexpr int STRN_GRAIN_POINTS = 8;
    int8_t strn_focus_chain = -1;
    int8_t strn_focus_slot = -1;
    bool strn_focus_valid = false;
    q15 strn_wave[STRN_WAVE_POINTS] = {};
    uint8_t strn_positions[STRN_GRAIN_POINTS] = {};
    uint16_t strn_grains = 0;
    uint16_t strn_fill = 0;
    q15 strn_mix = 0;
    bool strn_frozen = false;
    uint16_t delay_frames[NUM_CHAINS][UNITS_PER_CHAIN] = {};
    int16_t delay_wow_offset[NUM_CHAINS][UNITS_PER_CHAIN] = {};
    uint16_t delay_wet_peak[NUM_CHAINS][UNITS_PER_CHAIN] = {};
    uint8_t delay_hold[NUM_CHAINS][UNITS_PER_CHAIN] = {};
    uint8_t unit_kind[NUM_CHAINS][UNITS_PER_CHAIN] = {};   // UnitKind values
    uint8_t unit_bypass[NUM_CHAINS] = {};                  // bit per slot
    uint16_t step_trig[NUM_CHAINS] = {};                   // compatibility projection: steps 0..15
    // Truthful complete-pattern sequencer DTO. Explicit packing keeps the
    // audio->UI handoff bounded without leaking StepLock arrays:
    // bits 0..5 SeqStep::flags, 6..8 lock count, 9..14 timing + 23.
    // All 64 steps are published so page renderers never read live engine storage.
    uint16_t sequence_meta[NUM_CHAINS][SEQ_STEPS] = {};
    // Pitch is projected from an existing pitch/speed parameter lock. 0 means no
    // pitch-capable source; otherwise value is q15 lock/base and kind/slot identify
    // its exact semantic domain (OSC note, PLAY speed, GRAN/STRN semitones).
    q15 sequence_pitch[NUM_CHAINS][SEQ_STEPS] = {};
    uint64_t sequence_pitch_lock_mask[NUM_CHAINS] = {};
    int8_t sequence_pitch_slot[NUM_CHAINS] = {-1, -1, -1, -1};
    int8_t sequence_pitch_param[NUM_CHAINS] = {-1, -1, -1, -1};
    uint8_t sequence_pitch_kind[NUM_CHAINS] = {};
    uint8_t sequence_lifecycle[NUM_CHAINS] = {}; // SequenceLifecycle
    // Loop sources publish per-step normalized duration for score tails. Other
    // lifecycles leave this at zero; byte precision is ample at 17px/step.
    uint8_t sequence_duration[NUM_CHAINS][SEQ_STEPS] = {};
    int8_t sequence_duration_slot[NUM_CHAINS] = {-1, -1, -1, -1};
    int8_t sequence_duration_param[NUM_CHAINS] = {-1, -1, -1, -1};
    // One truthful primary value per chain. Event tracks project source level for
    // the velocity lens; continuous tracks project their defining density/level
    // parameter so all 16 positions can render and edit as a persistent curve.
    q15 sequence_value[NUM_CHAINS][SEQ_STEPS] = {};
    uint64_t sequence_value_lock_mask[NUM_CHAINS] = {};
    int8_t sequence_value_slot[NUM_CHAINS] = {-1, -1, -1, -1};
    int8_t sequence_value_param[NUM_CHAINS] = {-1, -1, -1, -1};
    uint8_t sequence_source_kind[NUM_CHAINS] = {};
    q15 scene_morph[NUM_CHAINS] = {};
    uint8_t scene_valid[NUM_CHAINS] = {};                  // bit0 = A, bit1 = B
    q15 scope[200] = {};
    int scope_pos = 0;
    uint32_t render_peak_us = 0;
    uint16_t dsp_load_permille = 0;
    uint32_t xrun_count = 0;
    q15 sigil_value[NUM_SIGILS] = {};
    uint32_t sigil_phase[NUM_SIGILS] = {};
};
static_assert(sizeof(EngineUiSnapshot) <= 3840,
              "EngineUiSnapshot hot handoff grew beyond its bounded budget");

// Persistent global follower configuration. ModSource numeric ids stay unchanged.
enum class EnvResponse : uint8_t { Fast = 0, Body, Slow, Count };

// Controller-captured scalar DTO; not part of the audio-worker snapshot. Capture
// under the platform audio guard and render the copy only. Range is POTENTIAL
// source extent around control, not sampled waveform extrema or a glide forecast.
struct ParamInspector {
    bool valid = false;
    int8_t chain = -1, slot = -1, param = -1;
    uint8_t kind = 0;
    char name[16] = {};
    q15 base = 0, control = 0, effective = 0, target = 0;
    q15 range_min = 0, range_max = 0, depth = 0, source_value = 0;
    ModSource source = ModSource::None;
    bool bipolar = false, clip_low = false, clip_high = false, temp = false;
    bool mod_enabled = true;
    // 0 base, 1 step lock, 2 sequence audition, 3 performance, 4 temp.
    uint8_t layer = 0;
    uint8_t env_chain = 0;
    EnvResponse env_response = EnvResponse::Fast;
    q15 env_level = 0;
};
static_assert(sizeof(ParamInspector) <= 64, "bounded controller parameter DTO");

class Engine {
public:
    Engine();
    ~Engine();

    Chain  chains[NUM_CHAINS];
    Sigil  sigils[NUM_SIGILS];
    PersistentScene scenes[NUM_CHAINS][2];
    q15 scene_morph[NUM_CHAINS] = {};
    void capture_scene(int chain, int which);
    bool scene_topology_compatible(int which) const;
    bool global_scene_topology_compatible() const;
    void apply_scene_morph(int chain, q15 morph);
    void capture_global_scene(int which);
    void apply_global_scene_morph(q15 morph);
    // Transition scene A/B uses the existing interpolation law, atomically
    // validates all topology, and retains mixer volume/ramp and live step locks.
    // Only base/pan/scene_morph change; normal control ticks update DSP.
    bool apply_transition_scene_morph(q15 morph);
    MacroPad macro_pads[8];
    void apply_macro_pad(int pad, q15 value);
    PerformanceBinding performance_bindings[NUM_PERFORMANCE_BINDINGS];
    void reset_performance_bindings();
    // Applies a normalized gesture to the master-bus section (Cutoff, Resonance,
    // Crush, Drive) or, for time-based targets (Reverb, Delay, Freeze, Wash),
    // to every installed unit matching the category. The master-bus targets are
    // always audible regardless of chain contents; the takeover targets need a
    // matching unit because a real delay line cannot be conjured on the audio path.
    void apply_global_fx(GlobalFxTarget target, q15 value);
    void apply_global_fx_mask(uint8_t active_mask);
    // Smoothed master-bus intensity for UI display (0 when inactive).
    q15 master_fx_value(GlobalFxTarget target) const {
        return (uint8_t)target < (uint8_t)GlobalFxTarget::Count
            ? master_fx_current_[(int)target] : 0;
    }
    // Runtime-only master transition gestures, independent of module takeovers,
    // banks/scenes/locks. Caller holds guard; no allocation in these methods.
    bool set_transition_fx(TransitionFxKind kind, int amount);
    void release_transition_fx();
    TransitionFxState transition_fx() const { return transition_fx_.state(); }
    // Audio-clocked live roll. division: 0=1/4, 1=1/8, 2=1/16, 3=1/32.
    void set_live_retrigger(int chain, int division, bool active);
    // Opt-in mixer-fader ramp: 128 audio samples, no persistent format change.
    // Legacy direct volume assignments retain their exact unsmoothed path.
    void set_chain_volume_smooth(int chain, q15 volume);

    // Caller holds the engine/audio guard. Base-only, bounded, no callbacks/IO.
    // Manual reroll/undo may edit bypassed or muted cards, never during recording.
    bool randomize_module(int chain, int slot);
    bool module_random_available(int chain, int slot) const;
    bool set_module_random_source(int chain, int slot, ModSource source);
    bool undo_module_randomize(int chain, int slot);
    bool can_undo_module_randomize(int chain, int slot) const;
    // UI polls under the guard and marks dirty on its own thread. Assignment is
    // reported by its setter; this counter only counts actual rand/undo base edits.
    uint64_t module_random_revision() const { return module_random_revision_; }

    // Elektron-style 1/16 sequencer. Locks are applied before the trig; a step
    // carrying only locks changes sound without retriggering it.
    Pattern pattern;
    Pattern pattern_banks[NUM_PATTERNS];
    // Active storage aliases pattern, not its last saved bank copy. Invalid or
    // omitted bank selects active storage. Caller holds the audio guard.
    Pattern& sequence_pattern(int bank = -1);
    const Pattern& sequence_pattern(int bank = -1) const;
    uint8_t active_pattern = 0;
    uint8_t pending_pattern = 0xff;
    LaunchQuantize pattern_launch_quantize = LaunchQuantize::Bar;
    bool sequencer_enabled = false;
    bool sequencer_fill_active = false;
    uint8_t swing_percent = 50;
    void set_sequencer_enabled(bool enabled);
    void set_sequencer_fill(bool active) { sequencer_fill_active = active; }
    bool chain_record_pending_start(int chain) const {
        return chain >= 0 && chain < NUM_CHAINS && (pending_rec_start & (1u << chain));
    }
    bool chain_record_pending_stop(int chain) const {
        return chain >= 0 && chain < NUM_CHAINS && (pending_rec_stop & (1u << chain));
    }
    void set_swing(int percent);
    void request_pattern(int index, bool quantized = true);
    // Caller holds audio guard. No bank writes/launch/clock reset. Selects one
    // complete native endpoint event per chain/tick, not interpolated step data.
    // Changing a contributing endpoint is refused; zero-weight replacement is
    // accepted only if it stays zero in the same call. Already scheduled events
    // (at most two per chain), their locks/retrigs and natural tails finish; rail
    // changes/clear never rewrite them. Position therefore has lookahead latency.
    // Refused while a pending launch or pattern chain owns playback.
    bool set_pattern_transition(int source, int destination, q15 position);
    void clear_pattern_transition();
    // Explicit keep-bank: endpoint/same-bank only, adopting its live storage as
    // active without resetting clock/tick/RNG or queued tuples. Up to two prior
    // accepted events per chain, active retrigs and natural tails may finish.
    bool finish_pattern_transition();
    PatternTransitionState pattern_transition() const { return pattern_transition_; }
    // Bounded performance chain: each entry holds one pattern for 1..16 bars.
    // Runtime transitions are bar-quantized and never allocate on the audio path.
    PatternChainStep pattern_chain[PATTERN_CHAIN_STEPS];
    uint8_t pattern_chain_length = 0;
    uint8_t pattern_chain_position = 0;
    uint8_t pattern_chain_bars_remaining = 0;
    bool pattern_chain_loop = true;
    bool pattern_chain_playing = false;
    bool pattern_chain_paused = false;
    bool pattern_chain_held = false;
    bool pattern_chain_next_pending = false;
    uint8_t pattern_chain_current_bars = 0;
    // Actual playing bank, not the selected/armed card's future bank.
    uint8_t pattern_chain_current_pattern = 0;
    // Live edits preserve current and queued card identity by bounded index
    // remapping. Bank/duration edits apply on next visit; repeated banks are
    // distinct cards. Removing/truncating current (playing or paused), armed
    // start, or queued next is refused. No persistent identity/hold fields.
    bool set_pattern_chain_step(int position, int pattern_index, int bars);
    bool insert_pattern_chain_step(int position, int pattern_index, int bars = 1);
    bool remove_pattern_chain_step(int position);
    bool move_pattern_chain_step(int from, int to);
    void clear_pattern_chain();
    bool set_pattern_chain_length(int length);
    // Legacy start intentionally clears manual blend/pending launch.
    bool start_pattern_chain(bool restart = true);
    // Safe ownership handoff: takeover changes no bank/clock/blend/events;
    // resume refuses any blend/pending manual launch, selects an exact card,
    // and restarts its full duration at the next bar (stopped: prepare only).
    bool take_over_pattern_chain();
    bool resume_pattern_chain(int position);
    bool set_pattern_chain_hold(bool held);
    bool next_pattern_chain();
    // Stops progression only. Transport, active bank and natural tails remain.
    void stop_pattern_chain();
    uint64_t sequence_tick = 0;
    uint8_t sequence_step(int chain) const;
    void set_sequence_length(int chain, int length, int bank = -1);
    void set_sequence_direction(int chain, SequenceDirection direction, int bank = -1);
    bool set_step_lock(int chain, int step, int slot, int param, q15 value);
    bool remove_step_lock(int chain, int step, int slot, int param, int bank = -1);
    // Caller holds the audio guard. Auditions current selected-step locks before
    // a safe audible trigger, even while stopped; never edits base/pattern/clock.
    // Performance overrides retain priority. Recording/pending recording or mute
    // refuses audition. Natural voice tails remain; overrides expire at 250 ms or
    // the next scheduled event on this chain. View exit must explicitly cancel.
    static constexpr uint32_t SEQUENCE_AUDITION_FRAMES = ENGINE_SR / 4;
    bool audition_sequence_step(int chain, int step, int bank = -1);
    void cancel_sequence_audition();

    // Caller holds the audio guard. One runtime-only parameter gesture, no
    // triggers/actions/sample changes. Refused on muted/bypassed cards or while
    // any REC is recording/pending. Release exposes current underlying layers
    // with normal slew, not a saved base or an exact audio-buffer restoration.
    bool begin_param_temp(int chain, int slot, int param);
    bool set_param_temp(q15 value);
    void cancel_param_temp();
    bool param_temp_active() const;
    q15 param_temp_value() const;
    void capture_param_inspector(int chain, int slot, int param, ParamInspector& out) const;

    // Guard-owned paired control transaction. The source-at-press entry installs
    // absolute parameter-domain x/y, then initializes the actual source head at
    // normalized source_position without an audible sweep. The origin entry is
    // for non-positional callers and captures the current control pair.
    uint32_t begin_scratch_gesture(int chain, int slot);
    uint32_t begin_scratch_gesture_at(int chain,int slot,q15 x,q15 y,q15 source_position);
    void end_scratch_gesture(uint32_t token);
    bool set_scratch_latch(int chain,int slot,bool enabled);
    bool set_scratch_gesture(uint32_t token, q15 x, q15 y);
    void cancel_scratch_gesture(uint32_t token = 0);
    bool scratch_gesture_active(uint32_t token) const;
    // Controller-only capture; hot EngineUiSnapshot budget stays unchanged.
    void capture_scratch_visual(int chain, int slot, ScratchUnit::Visual& out) const;
    void capture_paul_visual(int chain, int slot, PaulUnit::Visual& out) const;

    void clear_step_locks(int chain, int step);
    void set_step_microtiming(int chain, int step, int amount);
    int step_microtiming(int chain, int step) const;
    bool copy_step(int source_chain, int source_step, int target_chain, int target_step);
    bool copy_track(int source_chain, int target_chain);
    bool rotate_track(int chain, int amount);
    // Restore one complete chain atomically from a UI-owned bounded snapshot.
    // Resets live sequence lookahead when geometry changes during playback.
    bool restore_track(int chain, const SeqStep (&steps)[SEQ_STEPS],
                       const int8_t (&microtiming)[SEQ_STEPS], int length,
                       SequenceDirection direction);
    void clear_track(int chain);
    bool copy_pattern_bank(int source_bank, int target_bank);
    void clear_pattern_bank(int bank);
    void clear_step(int chain, int step, int bank = -1);

    // sample-accurate transport. The counter advances only while playing.
    bool transport_playing = false;
    uint8_t beats_per_bar = 4;
    uint64_t transport_frame = 0;
    uint64_t beat_count = 0;
    uint32_t frame_in_beat = 0;
    uint8_t pending_triggers = 0;
    uint8_t pending_rec_start = 0;
    uint8_t pending_rec_stop = 0;
    LaunchQuantize launch_quantize = LaunchQuantize::Immediate;
    void transport_play();
    void transport_stop();
    void transport_reset();
    void request_chain_trigger(int chain, LaunchQuantize quantize);
    trackr::synth::SampleEditResult request_chain_record(int chain, bool start, LaunchQuantize quantize);
    trackr::synth::SampleEditResult request_recorder_record(int chain, int slot, bool start,
                                                            LaunchQuantize quantize);
    trackr::synth::SampleEditResult request_recorder_command(int chain, int slot, RecorderCommand command,
                                                           LaunchQuantize quantize);
    RecorderCommand recorder_pending_command(int chain, int slot) const;
    bool recording_active_or_pending() const; // includes paused open-writer reservations
    void interrupt_recorders(); // immediate finalize + pending cancellation, no allocation/IO
    bool chain_trigger_pending(int chain) const;
    bool chain_record_pending(int chain) const;
    bool recorder_record_pending(int chain, int slot) const;
    uint64_t next_recorder_deadline() const {
        return pending_recorder_command_.valid ? pending_recorder_command_.deadline : UINT64_MAX;
    }
    uint32_t frames_per_beat() const;
    uint32_t beat_in_bar() const { return beats_per_bar ? (uint32_t)(beat_count % beats_per_bar) : 0; }
    uint64_t bar_count() const { return beats_per_bar ? beat_count / beats_per_bar : 0; }
    void capture_ui_snapshot(EngineUiSnapshot& out,
                             int focus_chain = -1,
                             int focus_slot = -1,
                             int focus_sigil = -1) const;
    // Replaces only sequence projections in a UI-owned copy of a published
    // snapshot. Never changes transport/focus/active identity; inactive bank
    // has sequence_step=255 (no playing head). Caller holds the audio guard.
    void capture_sequence_snapshot(EngineUiSnapshot& out, int bank = -1) const;

    // tempo clock and per-sigil synchronization/trigger routing.
    int bpm = 120;
    bool sigil_sync[NUM_SIGILS] = {};
    uint8_t sigil_div[NUM_SIGILS] = {};       // 0..5: 4,2,1,1/2,1/4,1/8 beats
    uint8_t step_dest[NUM_SIGILS] = {0, 1, 2, 3, 0, 1, 2, 3};
    void set_bpm(int value);
    ClockMod clock_mod(ClockTarget target) const;
    bool set_clock_mod(ClockTarget target, ModSource source, int depth);
    void capture_clock_inspector(ClockTarget target, ClockInspector& out) const;
    int effective_bpm() const;
    int32_t effective_bpm_q16() const;
    int32_t effective_swing_q16() const;
    void set_sigil_sync(int sigil, bool sync);
    void set_sigil_div(int sigil, int div);
    void set_step_dest(int sigil, int chain);

    // sigil-on-sigil routing: sigil i's RATE gets modulated by source (must be
    // an EARLIER sigil or None - enforced by set_sigil_mod)
    struct SigilMod { ModSource src = ModSource::None; q15 depth = 0; };
    SigilMod sigil_mods[NUM_SIGILS];
    void set_sigil_mod(int sigil, ModSource src, q15 depth);

    // Guard these scalar reads/writes together with engine access on platform.
    // Returns true only for an actual valid persistent edit; drawings are untouched.
    SigilConfig sigil_config[NUM_SIGILS];
    bool set_sigil_config(int index, const SigilConfig& config);
    void reset_sigil(int index); // runtime only, reproducible phase/random reset
    // Bounded physical-phase preview (0..UINT32_MAX), no state/RNG mutation.
    // False means scalar/silent rather than a periodic wave; invalid buffers/counts are untouched.
    bool capture_sigil_wave(int index, q15* out, int count) const;

    // swaps persistent patch state only (units, mixer, sigils, clock/routing).
    // meters/scope stay with each running Engine. Used for transactional loads.
    void swap_patch_state(Engine& other);

    // Bounce: callers guard every method except stage_bounce. Rendering only
    // writes the reserved preallocated source-sized slot; completed PCM is fixed.
    BounceSnapshot bounce_snapshot() const;
    BounceResult plan_bounce(int bars, BouncePlan& out, BounceSource source = BounceSource::Master) const;
    static BounceResult stage_bounce(BouncePlan& plan);
    // Optional successful stopped commit resets/starts at frame zero, no pre-roll.
    BounceResult commit_bounce(BouncePlan& plan, bool start_if_stopped = false);
    void stop_bounce(BounceReason reason = BounceReason::Stopped);
    BounceResult discard_bounce(trackr::synth::Sample& retired);
    const trackr::synth::Sample* bounce_sample(const BounceSnapshot& token) const;
    // Copies count packed frames: bounce_channels(token.source) q15 values/frame.
    bool read_bounce_frames(const BounceSnapshot& token, uint32_t first,
                            uint32_t count, q15* interleaved) const;
    bool mark_bounce_saved(const BounceSnapshot& token, const char* path);
    bool bounce_blocks_patch_change() const { return bounce_.view.slot >= 0; }
    // Stopped-only raw take audition: no copy, chain edits or second master pass.
    bool start_bounce_preview();
    void stop_bounce_preview();
    bool bounce_preview_active() const;
    // Next frame; retained at take length on natural finish, zero after stop/stale.
    uint32_t bounce_preview_position() const;
    // Explicit confirmed stopped-only replacement. Ordinary persisted PLAY and
    // lane events, never a hidden replay mode; stereo is folded to mono by PLAY.
    BounceResult plan_bounce_send(int chain, BounceSendPlan& out) const;
    static BounceResult stage_bounce_send(BounceSendPlan& plan);
    BounceResult commit_bounce_send(BounceSendPlan& plan);

    // Sample ownership policy lives with Engine because SampleBank cannot see
    // PLAY/GRAN/REC references. These calls run only on the controller thread.
    SampleSlotRefs sample_slot_refs(int sample_slot, const Unit* ignore = nullptr) const;
    // One platform-owned clipboard alias; update under the same audio guard as
    // clipboard publication. Runtime-only, never serialized or swapped as patch.
    void set_external_sample_slot(int slot = -1);
    int external_sample_slot() const { return external_sample_slot_; }
    trackr::synth::SampleEditResult plan_recorder(RecUnit& rec, bool auto_detach, RecWorkspacePlan& out,
                                                   int requested_slot = -1) const;
    trackr::synth::SampleEditResult plan_chain_record(int chain, RecWorkspacePlan* out, int& count) const;
    static trackr::synth::SampleEditResult stage_recorder(RecWorkspacePlan& plan);
    trackr::synth::SampleEditResult commit_recorder(RecWorkspacePlan& plan);
    // Allocation-free atomic consent/stage/identity/start preflight, then commit
    // and launch (or reserve a typed quantized Start). Destroy plan OUTSIDE guard.
    trackr::synth::SampleEditResult commit_recorder_restart(RecWorkspacePlan& plan, int chain, int slot,
                                                           LaunchQuantize quantize);
    trackr::synth::SampleEditResult commit_chain_record(RecWorkspacePlan* plans, int count);
    trackr::synth::SampleEditResult prepare_recorder(RecUnit& rec, bool auto_detach = true);
    trackr::synth::SampleEditResult retarget_recorder(RecUnit& rec, int sample_slot);
    trackr::synth::SampleEditResult clear_recorder(RecUnit& rec);
    bool recorder_is_private(const RecUnit& rec) const;

    // UI/controller swap: slot identity stays stable, so step locks and macro
    // routes remain attached to the slot. Captured scenes are not silently
    // rewritten; a kind change intentionally makes topology compatibility false
    // until the user captures new scene endpoints.
    Unit* replace_unit(int chain, int slot, Unit* u);
    // Insert into the first empty slot at/after `slot`, shifting installed units
    // right without exposing raw Chain ownership. Slot-based references at and
    // after the insertion point are remapped. Returns the consumed empty slot or -1.
    int insert_unit(int chain, int slot, Unit* u);
    // Move one installed unit immediately before another in the linear DSP path.
    // All persistent slot references move with their modules. Returns the moved
    // unit's final slot, or -1 when the request is invalid. before_slot ==
    // UNITS_PER_CHAIN inserts after the last occupied slot (also in sparse chains).
    int move_unit_before(int chain, int from_slot, int before_slot);
    // Remove one unit, compact the serial path and migrate/drop every slot-based
    // reference. Ownership of the removed unit returns to the caller.
    Unit* remove_unit(int chain, int slot);
    // UI-safe one-level structural history. The delete wrapper retains the removed
    // unit instead of freeing it, while both wrappers snapshot only the affected
    // chain's references into storage preallocated with Engine construction. A
    // later persistent edit or patch swap invalidates history and frees any
    // retained deleted unit.
    int move_unit_before_with_undo(int chain, int from_slot, int before_slot);
    bool remove_unit_with_undo(int chain, int slot);
    bool undo_structural_edit(int* restored_chain = nullptr, int* restored_slot = nullptr);
    bool structural_undo_available() const;
    // Invalidate is constant-time and safe while AudioOut is guarded. Reclaim
    // destroys a retained deleted unit and must run later on the controller thread.
    void invalidate_structural_undo();
    void reclaim_structural_undo();
    void discard_structural_undo();

    // ui thread: sigil rate (pre-mod). stored so sigil-on-sigil mod has a base.
    void set_sigil_rate(int sigil, q15 norm);
    q15  sigil_rate(int sigil) const {
        return (sigil >= 0 && sigil < NUM_SIGILS) ? rate_norm_[sigil] : 0;
    }

    // Controller-staged, non-persistent sample audition. `staged` is decoded
    // outside the audio lock; the swap is constant-time and returns the retired
    // preview for destruction after the guard. Preview bypasses project chains
    // and never consumes or mutates a SampleBank slot.
    void swap_sample_preview(trackr::synth::Sample& staged);
    void stop_sample_preview();
    bool sample_preview_active() const { return sample_preview_active_; }
    // Controller callers must hold the AudioOut guard, as for active()/stop().
    uint32_t sample_preview_position() const { return sample_preview_frame_; }
    uint32_t sample_preview_frames() const { return sample_preview_.playable_frames(); }

    // Private mono PLAY audition bus; never borrows a sequencer/chain voice.
    // Guarded controller calls. Token ownership prevents a stale editor stop.
    // Independent part pitch matches musical playback; old q8 remains tempo in
    // Stretch, dormant in Normal, varispeed in tape. Preview excludes ARP.
    uint32_t start_play_preview(int chain, int slot, uint32_t first, uint32_t end,
                                bool repeat, bool reverse = false, uint16_t speed_q8 = 256,
                                bool scrub = false, int8_t part_pitch = 0);
    bool scrub_play_preview(uint32_t token, q15 position, bool initialize = false) {
        return play_preview_active(token) && play_preview_.preview_scrub(position, initialize);
    }
    bool set_play_preview_speed(uint32_t token, uint16_t rate) {
        return play_preview_active(token) && play_preview_.set_preview_speed(rate);
    }
    // Live private drag audition: end exclusive, >=2 frames, rate64..1024.
    // No trigger/Param/score writes. Refuses stale source, recording, mute/bypass
    // or an already releasing voice. Independent semitones are validated -24..24.
    bool update_play_preview(uint32_t token, uint32_t first, uint32_t end,
                             bool reverse, uint16_t rate, int8_t part_pitch = 0);
    void stop_play_preview(uint32_t token = 0);
    bool play_preview_active(uint32_t token) const;
    uint32_t play_preview_position(uint32_t token) const;
    bool seek_play_preview(uint32_t token, uint32_t frame, bool seam = false) {
        return play_preview_active(token) && play_preview_.preview_seek(frame, seam);
    }
    bool play_editor_busy() const;

    // fire triggers (touch pads / sigil steps) into every unit of a chain
    void trigger_chain(int chain);

    // stereo interleaved render, frames arbitrary (chunked internally)
    void render(q15* out, int frames);

    // audio-worker timing telemetry: render plus UI snapshot capture cost.
    uint32_t render_last_us = 0;
    uint32_t render_peak_us = 0;
    uint16_t dsp_load_permille = 0;

    // last control-rate values for the ui (scope / canvas playheads)
    q15 env_follow_value() const { return env_value_; }
    // Return true only for a valid actual change; callers own dirty tracking.
    // Changing either setting clears the follower state. Fast retains the
    // original 3/4 attack, 1/64 release per rendered control fragment.
    bool set_env_follow_source(int chain);
    int env_follow_source() const { return env_chain_; }
    bool set_env_follow_response(EnvResponse response);
    EnvResponse env_follow_response() const { return env_response_; }

    // === ui-readable meters (written by render, torn reads are harmless) ===
    q15 chain_peak[NUM_CHAINS] = {};      // per-chain output peak, decaying
    static constexpr int SCOPE_LEN = 200; // master scope ring (every 8th frame)
    q15 scope[SCOPE_LEN] = {};
    int scope_pos = 0;

private:
    friend class ScratchUnit; // truthful sequence-deadline activity in controller visual()
    friend class PaulUnit;
    PaulUnit* bind_paul(int chain,int slot);
    BounceState bounce_;
    BounceSnapshot bounce_preview_take_;
    uint32_t bounce_preview_frame_ = 0;
    bool bounce_preview_active_ = false;
    void process_bounce_preview(q15* stereo, int frames);
    bool bounce_clock_safe() const;
    bool bounce_storage_valid() const;
    bool bounce_chain_safe(int chain) const;
    void capture_bounce(const q15* stereo, int frames);
    // Opt-in Q32 beat clock. Persistent routes are separate from these runtime
    // positions; original frame scheduler remains exact for untouched patches.
    ClockMod clock_mods_[2];
    bool clock_runtime_ = false;
    int32_t clock_applied_[2] = {120 * 65536, 50 * 65536};
    uint64_t clock_phase_ = 0, clock_audio_phase_ = 0;
    uint32_t clock_increment_ = 1;
    uint64_t clock_plan_phase_ = 0, clock_recorder_phase_ = 0, clock_roll_phase_ = 0;
    uint64_t clock_event_start_[NUM_CHAINS] = {}, clock_event_end_[NUM_CHAINS] = {};
    uint8_t clock_retrig_next_[NUM_CHAINS] = {1,1,1,1};
    bool clock_routes_active() const;
    void activate_clock_runtime();
    void reset_clock_runtime();
    void update_clock_modulation();
    void update_clock_deadlines();
    void update_clock_unit(Unit* unit);
    uint64_t clock_frame_for_phase(uint64_t phase) const;
    uint64_t clock_phase_for_frame(uint64_t frame) const;
    uint64_t clock_next_boundary(LaunchQuantize quantize) const;
    uint64_t clock_next_deadline_phase() const;
    void advance_clock_transport(int frames);
    void schedule_sequence_phase(uint64_t tick, uint64_t grid);
    void dispatch_sequence_phase();
    void fire_clock_retrigs();
    uint64_t module_random_revision_ = 0;
    uint32_t module_random_identity_ = 0;
    void initialize_module_random(Unit& unit);
    bool randomize_module_bases(Unit& unit);
    static bool module_random_undo_matches(const Unit& unit);
    void step_module_random();
    void control_step();                  // one 1khz tick: sigils + params + coeffs
    void advance_transport(int frames);
    void fire_pending(uint8_t mask);
    void fire_pending_rec(uint8_t mask);
    struct PendingRecorderCommand {
        RecUnit* rec = nullptr;
        uint32_t take_generation = 0, bank_generation = 0;
        uint64_t session_token = 0, state_token = 0;
        uint64_t deadline = 0;
        int8_t chain = -1, slot = -1, sample_slot = -1;
        RecorderCommand command = RecorderCommand::None;
        bool valid = false;
        const q15* data = nullptr;
        size_t elements = 0;
        uint8_t channels = 0;
    } pending_recorder_command_;
    PendingRecorderCommand pending_chain_recorders_[NUM_CHAINS][UNITS_PER_CHAIN];
    bool recorder_command_current(const PendingRecorderCommand& command) const;
    int external_sample_slot_ = -1;
    bool recorder_plan_current(const RecWorkspacePlan& plan) const;
    trackr::synth::SampleEditResult validate_recorder_plan(const RecWorkspacePlan& plan) const;
    trackr::synth::SampleEditResult validate_recorder_command(int chain, int slot, RecorderCommand command) const;
    void accept_recorder_command(int chain, int slot, RecorderCommand command, LaunchQuantize quantize);
    void cancel_pending_recorder();
    void activate_pattern(int index, bool start_now = false);
    void advance_pattern_chain_at_bar();
    bool pattern_chain_start_pending_ = false;
    PatternChainStep pattern_chain_start_step_;
    PatternChainStep pattern_chain_next_step_;
    uint8_t pattern_chain_next_position_ = 0xff; // 0xff = queued one-shot end
    bool arm_pattern_chain(int position, bool strict_next_bar);
    void begin_pattern_chain_step(const PatternChainStep& entry);
    PatternTransitionState pattern_transition_;
    int transition_bank_at_tick(int chain, uint64_t tick) const;
    void initialize_sequence_clock(bool start_now);
    // Queue/geometry rebuilds expire ordinary locks, not a valid written pitch.
    // Transport stop/reset explicitly request the full runtime release.
    void clear_sequence_runtime(bool cancel_temp = true, bool release_pitch = false);
    void schedule_sequence_tick(uint64_t tick, uint64_t nominal_frame);
    void dispatch_sequence_events();
    void fire_sequence_event(int chain, uint64_t tick, uint64_t nominal_frame);
    void advance_sequence_clock();
    void fire_sequence_retrigs();
    void release_sequence_locks(int chain = -1); // full release: audition, step locks, pitch
    void release_sequence_step_locks(int chain = -1);
    void release_sequence_pitch(int chain = -1);
    struct SequenceSource {
        int8_t slot = -1;
        SequencerProperties properties;
        SequenceLifecycle lifecycle = SequenceLifecycle::Strike;
        uint8_t pitch_domain = 0; // PLAY's reader mode; other kinds keep their raw domain
    };
    SequenceSource resolve_sequence_source(int chain) const;
    bool validate_sequence_pitch(int chain, const SequenceSource& source);
    bool sequence_audition_allowed(int chain) const;
    void refresh_audition_params(int chain, bool release);
    int arp_owner_for(int chain, int slot) const;
    int arp_trigger_owner_for(int chain, int slot) const;
    void clear_arp_runtime(int chain = -1);
    void update_arps();
    uint64_t arp_phase() const;
    int arp_render_limit(int frames) const;
    void trigger_chain_internal(int chain, bool audible_only);
    void trigger_sequence_chain(int chain);
    void release_play_gates(); // includes independently owned GRAN sequence requests
    void clear_grain_launch_requests(int chain);
    uint64_t next_play_gate_deadline();
    int8_t sequence_audition_chain_ = -1;
    uint64_t sequence_audition_end_frame_ = 0;
    struct ActiveStepLock {
        bool valid = false;
        uint8_t chain = 0, slot = 0, param = 0;
    };
    ActiveStepLock active_step_locks_[NUM_CHAINS * SEQ_LOCKS];
    // One primary written pitch per chain, outside the four ordinary-lock slots.
    // The value remains in Param::step_lock; this record owns only its lifetime.
    // Kind + parameter + bipolar identify the existing raw domain (PLAY note vs
    // varispeed, MOTOR note mode, SCRT hold); no new layer or persistent field.
    struct SequencePitchOwner {
        Unit* unit = nullptr;
        int8_t slot = -1, param = -1;
        UnitKind kind = UnitKind::Empty;
        bool bipolar = false;
        uint8_t domain = 0;
    } sequence_pitch_owner_[NUM_CHAINS];
    struct PendingSequenceEvent {
        bool valid = false;
        uint64_t tick = 0;
        uint64_t frame = 0;
        uint64_t nominal_frame = 0;
        bool condition_pass = false;
        uint8_t source_step = 255, source_bank = 255;
        uint64_t clock_phase = 0, clock_nominal = 0, clock_grid = 0;
        int64_t clock_micro = 0;
        SeqStep step;
    } pending_sequence_events_[NUM_CHAINS][2];
    uint32_t sequence_rng_ = 0x53455131u;
    bool active_sequence_pass_[NUM_CHAINS] = {};
    uint8_t active_sequence_source_step_[NUM_CHAINS] = {255,255,255,255};
    uint8_t active_sequence_source_bank_[NUM_CHAINS] = {255,255,255,255};
    SequenceHit sequence_hit_[NUM_CHAINS];
    uint64_t sequence_hit_audio_frame_[NUM_CHAINS] = {};
    SeqStep active_sequence_step_[NUM_CHAINS];
    uint64_t active_sequence_tick_[NUM_CHAINS] = {};
    uint64_t sequence_event_frame_[NUM_CHAINS] = {};
    uint32_t sequence_event_span_[NUM_CHAINS] = {1,1,1,1};
    bool sequence_clock_active_ = false;
    uint64_t sequence_plan_tick_ = 0;
    uint64_t sequence_plan_grid_frame_ = 0;
    uint8_t sequence_plan_subdivision_ = 1;
    uint64_t next_sequence_grid_frame(uint64_t frame) const;
    uint64_t next_sequence_deadline() const;
    uint8_t sequence_step_at_tick(int chain, uint64_t tick, int bank = -1) const;
    uint64_t sequence_cycle_at_tick(int chain, uint64_t tick, int bank = -1) const;
    bool step_condition_passes(const SeqStep& step, int chain, uint64_t tick, int bank = -1);

    struct SigilRuntime { uint32_t random_state=0, previous_phase=0; q15 from=0, to=0; };
    SigilRuntime sigil_runtime_[NUM_SIGILS];
    q15 modulation_value(ModSource source) const;
    void refresh_sigil_rate(int index);
    q15 sample_sigil_shape(int index, uint32_t phase) const;
    void step_sigil(int index, bool advance);
    q15  rate_norm_[NUM_SIGILS] = {};     // ui-set sigil rates (pre-mod)
    q15  env_value_ = 0;                  // envelope follower state
    uint8_t env_chain_ = 0;
    EnvResponse env_response_ = EnvResponse::Fast;
    bool param_temp_allowed(int chain, int slot, int param) const;
    int8_t temp_chain_ = -1, temp_slot_ = -1, temp_param_ = -1;
    struct ScratchGesture {
        ScratchUnit* unit=nullptr;
        ScratchUnit::Source source;
        ScratchUnit::Mode mode=ScratchUnit::Mode::Hold;
        ScratchUnit::Map map=ScratchUnit::Map::SpeedPitch;
        int8_t chain=-1, slot=-1, x=-1, y=-1;
        uint32_t token=0, epoch=0;
    } scratch_gesture_;
    uint32_t scratch_token_=0;
    uint32_t begin_scratch_gesture_impl(int chain,int slot,bool absolute,q15 x,q15 y,q15 source_position);
    int  control_countdown_ = 0;
    int  scope_decim_ = 0;                // scope decimation counter
    q15 master_gain_ = trackr::fx::Q15_ONE; // fast attack / slow release limiter
    uint16_t master_peak_ = 0; // post-limiter visualization; never feeds DSP
    struct VolumeRamp { q15 start=0, target=0; uint16_t remaining=0; };
    VolumeRamp volume_ramp_[NUM_CHAINS] = {};

    void process_sample_preview(q15* out, int frames);
    void process_play_preview(q15* out, int frames);
    bool play_preview_valid() const;
    PlayUnit play_preview_;
    const Unit* play_preview_owner_ = nullptr;
    int play_preview_chain_ = -1, play_preview_slot_ = -1;
    uint32_t play_preview_generation_ = 0, play_preview_token_ = 0;
    trackr::synth::Sample sample_preview_;
    uint32_t sample_preview_frame_ = 0;
    q15 sample_preview_amp_ = 0;
    bool sample_preview_active_ = false;

    // Master-bus performance FX (post-mix, pre-limiter). Targets are set by
    // apply_global_fx; currents slew toward them each block to avoid zipper
    // noise. Only Cutoff/Resonance/Crush/Drive live here; the rest stay as
    // unit-param takeovers.
    void process_master_fx(q15* out, int frames);
    TransitionFx transition_fx_; // once-allocated independent stereo bus history
    q15 master_fx_target_[(int)GlobalFxTarget::Count] = {};
    q15 master_fx_current_[(int)GlobalFxTarget::Count] = {};
    trackr::dsp::Svf2 master_svf_l_, master_svf_r_;
    int master_crush_hold_ = 0;
    q15 master_crush_l_ = 0, master_crush_r_ = 0;

    bool live_retrigger_active_ = false;
    uint8_t live_retrigger_chain_ = 0, live_retrigger_division_ = 2;
    uint64_t live_retrigger_next_frame_ = 0;
    uint64_t audio_frame_ = 0;
    void fire_live_retriggers();

    struct StructuralUndo;
    StructuralUndo* structural_undo_ = nullptr;
    Unit* structural_undo_retired_ = nullptr;
    bool structural_edit_in_progress_ = false;
    void capture_structural_undo(int chain, int selected_slot);
};

} // namespace seance
