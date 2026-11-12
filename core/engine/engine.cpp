#include "engine.h"
#include "units.h"
#include <cstring>
#include <new>

#include "module_random_impl.inc"
#include "paul_engine_impl.inc"
#include "rule_engine_impl.inc"
#include "rot_engine_impl.inc"

namespace seance {

static constexpr uint64_t CLOCK_BEAT = 0x100000000ULL;
static constexpr uint64_t CLOCK_STEP = CLOCK_BEAT / 4;

struct Engine::StructuralUndo {
    bool valid = false;
    int8_t chain = -1;
    int8_t selected_slot = -1;
    Unit* units[UNITS_PER_CHAIN] = {};
    SeqStep steps[SEQ_STEPS];
    int8_t microtiming[SEQ_STEPS] = {};
    SeqStep bank_steps[NUM_PATTERNS][SEQ_STEPS];
    int8_t bank_microtiming[NUM_PATTERNS][SEQ_STEPS] = {};
    PersistentScene scenes[2];
    MacroPad macro_pads[8];
    Unit* retained = nullptr;
};

#include "relic_engine_impl.inc"

static void swap_pattern(Pattern& a, Pattern& b) {
    for (int chain = 0; chain < NUM_CHAINS; ++chain) {
        uint8_t length = a.length[chain]; a.length[chain] = b.length[chain]; b.length[chain] = length;
        SequenceDirection direction = a.direction[chain];
        a.direction[chain] = b.direction[chain]; b.direction[chain] = direction;
        for (int step = 0; step < SEQ_STEPS; ++step) {
            SeqStep data = a.steps[chain][step]; a.steps[chain][step] = b.steps[chain][step]; b.steps[chain][step] = data;
            int8_t timing = a.microtiming[chain][step];
            a.microtiming[chain][step] = b.microtiming[chain][step]; b.microtiming[chain][step] = timing;
        }
    }
}

// Resolve one ModSource to its current q15 value. Sequencer event dispatch also
// uses this so p-lock transitions can refresh trigger-time unit caches without
// dropping the modulation value that is live at that exact frame boundary.
q15 Engine::modulation_value(ModSource source) const {
    const int index = mod_source_sigil(source);
    return index >= 0 ? (sigil_config[index].enabled ? sigils[index].value : 0) :
        (source == ModSource::EnvFollow ? env_value_ : 0);
}

const char* unit_kind_name(UnitKind k) {
    switch (k) {
        case UnitKind::Empty:        return "----";
        case UnitKind::SamplePlayer: return "PLAY";
        case UnitKind::Grain:        return "GRAN";
        case UnitKind::Osc:          return "OSC";
        case UnitKind::Filter:       return "FILT";
        case UnitKind::Vca:          return "VCA";
        case UnitKind::Delay:        return "DLY";
        case UnitKind::Fold:         return "FOLD";
        case UnitKind::MicIn:        return "MIC";
        case UnitKind::Recorder:     return "REC";
        case UnitKind::Bitcrush:     return "BIT";
        case UnitKind::Chorus:       return "CHOR";
        case UnitKind::Reverb:       return "REV";
        case UnitKind::Stranular:     return "STRN";
        case UnitKind::Ghost:         return "GHST";
        case UnitKind::Ring:          return "RING";
        case UnitKind::Tilt:          return "TILT";
        case UnitKind::Noise:         return "NOISE";
        case UnitKind::Fm:            return "FM";
        case UnitKind::Motor:         return "MOTOR";
        case UnitKind::Shift:         return "SHIFT";
        case UnitKind::Bird:          return "BIRD";
        case UnitKind::Fract:         return "FRACT";
        case UnitKind::Comp:          return "COMP";
        case UnitKind::Mouth:         return "MOUTH";
        case UnitKind::Arp:           return "ARP";
        case UnitKind::Glitch:        return "GLITCH";
        case UnitKind::Inst:          return "INST";
        case UnitKind::Scratch:       return "SCRT";
        case UnitKind::Paul:          return "PAUL";
        case UnitKind::Pent:          return "PENT";
        case UnitKind::Rule:          return "RULE";
        case UnitKind::Rot:           return "ROT";
        case UnitKind::Relic:         return "RELIC";
        case UnitKind::Strain:        return "STRAIN";
        default:                     return "?";
    }
}

// Validate public PENT config literally, never repairing an invalid width.
static bool pent_install_valid(const Unit* unit) {
    if(unit && unit->kind==UnitKind::Rot) {
        const auto& r=*static_cast<const RotUnit*>(unit);
        if(!r.ready() || !RotUnit::valid_config(r.config()) || r.sample_slot<0 || r.sample_slot>=trackr::synth::SAMPLE_BANK_SIZE)return false;
        for(int p=0;p<RotUnit::P_COUNT;++p)if(r.params[p].base < (p==RotUnit::P_PITCH?-32767:0))return false;
    }
    if(unit && unit->kind==UnitKind::Rule) {
        const auto& r=*static_cast<const RuleUnit*>(unit);
        if(!RuleUnit::valid_config(r.config()) || r.sample_slot<0 || r.sample_slot>=trackr::synth::SAMPLE_BANK_SIZE)return false;
        for(int p=0;p<RuleUnit::P_COUNT;++p)if(r.params[p].base < (p==RuleUnit::P_ROOT?-32767:0))return false;
    }
    if (!unit || unit->kind != UnitKind::Pent) return true;
    const auto& pent = *static_cast<const PentUnit*>(unit);
    if (uint8_t(pent.width) > uint8_t(PentUnit::Width::Stereo)) return false;
    for (const auto& param : pent.params) if (param.base < 0) return false;
    return true;
}

void Engine::capture_pent_visual(int chain, int slot, PentUnit::Visual& out) const {
    out = {};
    if (unsigned(chain) >= NUM_CHAINS || unsigned(slot) >= UNITS_PER_CHAIN) return;
    const Unit* unit = chains[chain].units[slot];
    if (!unit || unit->kind != UnitKind::Pent) return;
    const auto& pent = *static_cast<const PentUnit*>(unit);
    out = pent.visual(); // no resolution, clock advancement, reset or UI synthesis
    const auto& pipe = chains[chain];
    if (!out.ready || !unit->ready() || !out.frames || pipe.mute || pipe.volume <= 0 || unit->bypass || bounce_preview_active() ||
        uint8_t(pent.width) > 1 || !pipe.rendered_channels || !chain_audio_channels(chain) ||
        pent_render_source_[chain] != unit ||
        pent_render_width_[chain] != uint8_t(pent.width) || resolve_sequence_source(chain).slot != slot ||
        (unit->arp_voice.owner >= 0 && !unit->arp_voice.gain)) out.audible = false;
}

Engine::Engine() {
    structural_undo_ = new(std::nothrow) StructuralUndo();
    reset_performance_bindings();
    for (int i = 0; i < NUM_SIGILS; ++i) {
        rate_norm_[i] = 8192;                       // ~1hz-ish default
        sigil_div[i] = 2;                           // one cycle per beat when synced
        step_dest[i] = (uint8_t)(i % NUM_CHAINS);
        reset_sigil(i);
    }
}

void Engine::bind_pitch_scale(Unit* unit) {
    if (unit && unit->kind == UnitKind::Osc) static_cast<OscUnit*>(unit)->pitch_scale = project_scale_;
    else if (unit && unit->kind == UnitKind::SamplePlayer) static_cast<PlayUnit*>(unit)->pitch_scale = project_scale_;
}

bool Engine::set_project_scale(MusicalScale scale) {
    if (!musical_scale::valid(scale) || musical_scale::equal(scale, project_scale_)) return false;
    project_scale_ = scale;
    for (auto& chain : chains) for (auto* unit : chain.units) bind_pitch_scale(unit);
    bind_pitch_scale(&play_preview_);
    return true;
}

void Engine::reset_performance_bindings() {
    for (auto& binding : performance_bindings) binding = {};
    auto set = [this](PerformanceControl control, PerformanceAction action,
                      int target, PerformanceMode mode) {
        performance_bindings[(int)control] = { action, (uint8_t)target, mode };
    };
    for (int i = 0; i < 4; ++i) {
        set((PerformanceControl)((int)PerformanceControl::FaceA + i),
            PerformanceAction::ChainTrigger, i, PerformanceMode::Press);
        set((PerformanceControl)((int)PerformanceControl::LFaceA + i),
            PerformanceAction::PatternLaunch, i, PerformanceMode::Press);
        set((PerformanceControl)((int)PerformanceControl::LRFaceA + i),
            PerformanceAction::PatternLaunch, i + 4, PerformanceMode::Press);
        set((PerformanceControl)((int)PerformanceControl::DpadUp + i),
            PerformanceAction::MacroPad, i, PerformanceMode::Hold);
        set((PerformanceControl)((int)PerformanceControl::LDpadUp + i),
            PerformanceAction::MacroPad, i + 4, PerformanceMode::Hold);
    }
    set(PerformanceControl::ShoulderR, PerformanceAction::Fill, 0, PerformanceMode::Hold);
    set(PerformanceControl::Start, PerformanceAction::Transport, 0, PerformanceMode::Press);
}

Engine::~Engine() {
    discard_structural_undo();
    delete structural_undo_;
    structural_undo_ = nullptr;
    for (auto& c : chains)
        for (auto& u : c.units) { delete u; u = nullptr; }
}

void Engine::capture_ui_snapshot(EngineUiSnapshot& out,
                                 int focus_chain,
                                 int focus_slot,
                                 int focus_sigil) const {
    out.transport_playing = transport_playing;
    out.beat_count = beat_count;
    out.frame_in_beat = frame_in_beat;
    out.bpm = effective_bpm();
    out.beats_per_bar = beats_per_bar;
    out.launch_quantize = launch_quantize;
    out.pending_triggers = pending_triggers;
    out.pending_rec = pending_rec_start | pending_rec_stop;
    out.rec_recording = out.rec_has_take = out.rec_unsaved_take = 0;
    out.play_focus_chain = -1;
    out.play_focus_slot = -1;
    out.play_focus_valid = false;
    out.play_state = 0;
    out.play_position = out.play_start = out.play_end = 0;
    out.play_speed = 0;
    out.filter_focus_chain = -1;
    out.filter_focus_slot = -1;
    out.filter_focus_valid = false;
    out.filter_cutoff = out.filter_resonance = 0;
    out.filter_drive = out.filter_morph = 0;
    out.fold_focus_chain = -1;
    out.fold_focus_slot = -1;
    out.fold_focus_valid = false;
    out.reverb_focus_chain = -1;
    out.reverb_focus_slot = -1;
    out.reverb_focus_valid = false;
    out.reverb_energy = out.reverb_freeze = 0;
    out.effect_focus_chain = -1;
    out.effect_focus_slot = -1;
    out.effect_focus_kind = (uint8_t)UnitKind::Empty;
    out.effect_focus_valid = false;
    std::memset(out.focus_effective, 0, sizeof out.focus_effective);
    out.bit_depth = 3;
    out.bit_period = 1;
    out.bit_held = 0;
    out.bit_dropout = false;
    out.effect_payload_lo = 0;
    out.effect_focus_audible = false;
    out.gran_focus_chain = -1;
    out.gran_focus_slot = -1;
    out.gran_focus_valid = false;
    out.strn_focus_chain = -1;
    out.strn_focus_slot = -1;
    out.strn_focus_valid = false;
    std::memset(out.strn_wave, 0, sizeof out.strn_wave);
    std::memset(out.strn_positions, 0, sizeof out.strn_positions);
    out.strn_grains = 0;
    out.strn_fill = 0;
    out.strn_mix = 0;
    out.strn_frozen = false;
    out.xrun_count = 0;
    out.rec_waveform_chain = -1;
    out.rec_waveform_slot = -1;
    out.rec_waveform_valid = 0;
    std::memset(out.rec_waveform, 0, sizeof out.rec_waveform);
    out.project_scale = project_scale_;
    out.sequencer_enabled = sequencer_enabled;
    out.sequencer_fill_active = sequencer_fill_active;
    out.swing_percent = (uint8_t)((effective_swing_q16() + 32768) / 65536);
    out.active_pattern = active_pattern;
    out.pattern_transition = pattern_transition_;
    out.pending_pattern = pending_pattern;
    out.pattern_launch_quantize = pattern_launch_quantize;
    out.pattern_chain_playing = pattern_chain_playing;
    out.pattern_chain_start_pending = pattern_chain_start_pending_;
    out.pattern_chain_armed_pattern = pattern_chain_start_pending_ ? pattern_chain_start_step_.pattern : 0xff;
    out.pattern_chain_armed_bars = pattern_chain_start_pending_ ? pattern_chain_start_step_.bars : 0;
    out.pattern_chain_paused = pattern_chain_paused;
    out.pattern_chain_held = pattern_chain_held;
    out.pattern_chain_next_pending = pattern_chain_next_pending;
    out.pattern_chain_next_position = pattern_chain_next_pending ? pattern_chain_next_position_ : 0xff;
    const bool queued_card = pattern_chain_next_pending && pattern_chain_next_position_ != 0xff;
    out.pattern_chain_next_pattern = queued_card ? pattern_chain_next_step_.pattern : 0xff;
    out.pattern_chain_next_bars = queued_card ? pattern_chain_next_step_.bars : 0;
    out.pattern_chain_current_bars = pattern_chain_current_bars;
    out.pattern_chain_current_pattern = active_pattern;
    std::memcpy(out.pattern_chain_steps, pattern_chain, sizeof pattern_chain);
    out.pattern_chain_loop = pattern_chain_loop;
    out.pattern_chain_length = pattern_chain_length;
    out.pattern_chain_position = pattern_chain_position;
    out.pattern_chain_bars_remaining = pattern_chain_bars_remaining;
    for (int chain = 0; chain < NUM_CHAINS; ++chain) {
        for (int slot = 0; slot < UNITS_PER_CHAIN; ++slot) {
            const Unit* unit = chains[chain].units[slot];
            if (unit && unit->kind == UnitKind::SamplePlayer &&
                chain == focus_chain && slot == focus_slot) {
                const auto* play = static_cast<const PlayUnit*>(unit);
                const auto& sample = trackr::synth::SampleBank::instance().slot(play->sample_slot);
                if (sample.playable_frames() >= 2) {
                    out.play_focus_chain = (int8_t)chain;
                    out.play_focus_slot = (int8_t)slot;
                    out.play_focus_valid = true;
                    out.play_state = (chains[chain].mute || unit->bypass)
                        ? 0 : play->visual_state();
                    out.play_position = play->visual_position();
                    out.play_start = play->visual_start();
                    out.play_end = play->visual_end();
                    out.play_speed = play->visual_speed();
                }
            }
            if (unit && unit->kind == UnitKind::Filter &&
                chain == focus_chain && slot == focus_slot) {
                const auto* filter = static_cast<const FilterUnit*>(unit);
                out.filter_focus_chain = (int8_t)chain;
                out.filter_focus_slot = (int8_t)slot;
                out.filter_focus_valid = true;
                out.filter_cutoff = filter->visual_cutoff();
                out.filter_resonance = filter->visual_resonance();
                out.filter_drive = (uint16_t)unit->params[FilterUnit::P_DRIVE].effective;
                out.filter_morph = (uint16_t)unit->params[FilterUnit::P_MORPH].effective;
            }
            if (unit && unit->kind == UnitKind::Fold &&
                chain == focus_chain && slot == focus_slot) {
                out.fold_focus_chain = (int8_t)chain;
                out.fold_focus_slot = (int8_t)slot;
                out.fold_focus_valid = true;
                for (int param = 0; param < unit->num_params; ++param)
                    out.focus_effective[param] = unit->params[param].effective;
            }
            if (unit && unit->kind == UnitKind::Reverb &&
                chain == focus_chain && slot == focus_slot) {
                const auto* reverb = static_cast<const ReverbUnit*>(unit);
                out.reverb_focus_chain = (int8_t)chain;
                out.reverb_focus_slot = (int8_t)slot;
                out.reverb_focus_valid = true;
                for (int param = 0; param < reverb->num_params; ++param)
                    out.focus_effective[param] = reverb->params[param].effective;
                out.reverb_energy = (chains[chain].mute || unit->bypass)
                    ? 0 : (uint16_t)reverb->visual_energy();
                out.reverb_freeze = (uint16_t)reverb->visual_freeze();
            }
            if (unit && unit->kind == UnitKind::MicIn &&
                chain == focus_chain && slot == focus_slot) {
                const auto* mic = static_cast<const MicUnit*>(unit);
                out.effect_focus_chain = (int8_t)chain;
                out.effect_focus_slot = (int8_t)slot;
                out.effect_focus_kind = (uint8_t)UnitKind::MicIn;
                out.effect_focus_valid = true;
                out.effect_focus_audible = !chains[chain].mute && !unit->bypass &&
                    unit->params[MicUnit::P_MON].effective > 16384;
                for (int param = 0; param < mic->num_params; ++param)
                    out.focus_effective[param] = mic->params[param].effective;
                out.mic_peak = out.effect_focus_audible ? mic->visual_peak() : 0;
                const uint16_t fill = mic->visual_fill();
                out.mic_fill_hi = (uint8_t)(fill >> 8);
                out.mic_fill_lo = (uint8_t)fill;
            }
            if (unit && unit->kind == UnitKind::Chorus &&
                chain == focus_chain && slot == focus_slot) {
                const auto* chorus = static_cast<const ChorusUnit*>(unit);
                out.effect_focus_chain = (int8_t)chain;
                out.effect_focus_slot = (int8_t)slot;
                out.effect_focus_kind = (uint8_t)UnitKind::Chorus;
                out.effect_focus_valid = true;
                out.effect_focus_audible = !chains[chain].mute && !unit->bypass;
                for (int param = 0; param < chorus->num_params; ++param)
                    out.focus_effective[param] = chorus->params[param].effective;
                const uint16_t phase = chorus->visual_phase();
                out.chorus_phase_hi = (uint8_t)(phase >> 8);
                out.chorus_phase_lo = (uint8_t)phase;
                out.chorus_delay = chorus->visual_delay();
            }
            if (unit && unit->kind == UnitKind::Tilt &&
                chain == focus_chain && slot == focus_slot) {
                out.effect_focus_chain = (int8_t)chain;
                out.effect_focus_slot = (int8_t)slot;
                out.effect_focus_kind = (uint8_t)UnitKind::Tilt;
                out.effect_focus_valid = true;
                out.effect_focus_audible = !chains[chain].mute && !unit->bypass;
                for (int param = 0; param < unit->num_params; ++param)
                    out.focus_effective[param] = unit->params[param].effective;
            }
            if (unit && unit->kind == UnitKind::Ring &&
                chain == focus_chain && slot == focus_slot) {
                const auto* ring = static_cast<const RingUnit*>(unit);
                out.effect_focus_chain = (int8_t)chain;
                out.effect_focus_slot = (int8_t)slot;
                out.effect_focus_kind = (uint8_t)UnitKind::Ring;
                out.effect_focus_valid = true;
                out.effect_focus_audible = !chains[chain].mute && !unit->bypass;
                for (int param = 0; param < ring->num_params; ++param)
                    out.focus_effective[param] = ring->params[param].effective;
                const uint16_t phase = ring->visual_phase();
                out.ring_phase_hi = (uint8_t)(phase >> 8);
                out.ring_phase_lo = (uint8_t)phase;
                out.ring_envelope = ring->visual_envelope();
            }
            if (unit && unit->kind == UnitKind::Ghost &&
                chain == focus_chain && slot == focus_slot) {
                const auto* ghost = static_cast<const GhostUnit*>(unit);
                out.effect_focus_chain = (int8_t)chain;
                out.effect_focus_slot = (int8_t)slot;
                out.effect_focus_kind = (uint8_t)UnitKind::Ghost;
                out.effect_focus_valid = true;
                out.effect_focus_audible = !chains[chain].mute && !unit->bypass;
                for (int param = 0; param < ghost->num_params; ++param)
                    out.focus_effective[param] = ghost->params[param].effective;
                const uint16_t energy = out.effect_focus_audible
                    ? (uint16_t)ghost->visual_energy() : 0;
                const uint16_t freeze = (uint16_t)ghost->visual_freeze();
                out.ghost_energy_hi = (uint8_t)(energy >> 8);
                out.ghost_energy_lo = (uint8_t)energy;
                out.ghost_drift = ghost->visual_drift();
                out.ghost_freeze_hi = (uint8_t)(freeze >> 8);
                out.effect_payload_lo = (uint8_t)freeze;
            }
            if (unit && unit->kind == UnitKind::Bitcrush &&
                chain == focus_chain && slot == focus_slot) {
                const auto* bit = static_cast<const BitUnit*>(unit);
                out.effect_focus_chain = (int8_t)chain;
                out.effect_focus_slot = (int8_t)slot;
                out.effect_focus_kind = (uint8_t)UnitKind::Bitcrush;
                out.effect_focus_valid = true;
                out.effect_focus_audible = !chains[chain].mute && !unit->bypass;
                for (int param = 0; param < bit->num_params; ++param)
                    out.focus_effective[param] = bit->params[param].effective;
                out.bit_depth = bit->visual_bits();
                out.bit_period = bit->visual_period();
                out.bit_held = bit->visual_held();
                out.bit_dropout = bit->visual_dropout();
            }
            if (unit && (unit->kind == UnitKind::Noise || unit->kind == UnitKind::Fm ||
                         unit->kind == UnitKind::Motor || unit->kind == UnitKind::Strain) &&
                chain == focus_chain && slot == focus_slot) {
                out.effect_focus_chain = (int8_t)chain;
                out.effect_focus_slot = (int8_t)slot;
                out.effect_focus_kind = (uint8_t)unit->kind;
                out.effect_focus_valid = true;
                out.effect_focus_audible = !chains[chain].mute && !unit->bypass;
                for (int param = 0; param < unit->num_params; ++param)
                    out.focus_effective[param] = unit->params[param].effective;
                uint16_t phase = 0, energy = 0;
                if (unit->kind == UnitKind::Noise) {
                    const auto* noise = static_cast<const NoiseUnit*>(unit);
                    phase = noise->visual_envelope();
                    energy = noise->visual_activity();
                } else if (unit->kind == UnitKind::Fm) {
                    const auto* fm = static_cast<const FmUnit*>(unit);
                    phase = fm->visual_phase(); energy = fm->visual_envelope();
                    out.fm_index_envelope = (uint8_t)((uint32_t)fm->visual_index_envelope() * 255u / 32767u);
                    out.fm_feedback_envelope = (uint8_t)((uint32_t)fm->visual_feedback_envelope() * 255u / 32767u);
                } else if (unit->kind == UnitKind::Motor) {
                    const auto* motor = static_cast<const MotorUnit*>(unit);
                    phase = motor->visual_phase(); energy = motor->visual_energy();
                } else {
                    const auto* strain = static_cast<const StrainUnit*>(unit);
                    phase = (uint16_t)((int32_t)strain->visual_motion() + 32767);
                    energy = strain->visual_energy();
                }
                out.generator_phase_hi = (uint8_t)(phase >> 8);
                out.generator_phase_lo = (uint8_t)phase;
                out.generator_energy = out.effect_focus_audible ? energy : 0;
            }
            if (unit && unit->kind == UnitKind::Grain &&
                chain == focus_chain && slot == focus_slot) {
                const auto* gran = static_cast<const GrainUnit*>(unit);
                static_assert(EngineUiSnapshot::GRAN_GRAIN_POINTS == GrainUnit::MAX_GRAINS,
                              "GRAN snapshot/grain mailbox size mismatch");
                out.gran_focus_chain = (int8_t)chain;
                out.gran_focus_slot = (int8_t)slot;
                out.gran_focus_valid = true;
                for (int param = 0; param < gran->num_params; ++param)
                    out.focus_effective[param] = gran->params[param].effective;
                std::memcpy(out.gran_positions, gran->visual_positions(),
                            sizeof out.gran_positions);
                std::memcpy(out.gran_phases, gran->visual_phases(),
                            sizeof out.gran_phases);
                out.gran_grains = gran->visual_grains();
                out.gran_active = gran->visual_active();
                out.gran_limit = GrainUnit::voice_limit(
                    gran->params[GrainUnit::P_VOICES].effective);
                out.gran_interval = gran->visual_interval();
            }
            if (unit && unit->kind == UnitKind::Stranular &&
                chain == focus_chain && slot == focus_slot) {
                const auto* strn = static_cast<const StranularUnit*>(unit);
                static_assert(EngineUiSnapshot::STRN_WAVE_POINTS == StranularUnit::VIZ_POINTS,
                              "STRN snapshot/wave mailbox size mismatch");
                static_assert(EngineUiSnapshot::STRN_GRAIN_POINTS == StranularUnit::MAX_GRAINS,
                              "STRN snapshot/grain mailbox size mismatch");
                out.strn_focus_chain = (int8_t)chain;
                out.strn_focus_slot = (int8_t)slot;
                out.strn_focus_valid = true;
                for (int param = 0; param < strn->num_params; ++param)
                    out.focus_effective[param] = strn->params[param].effective;
                std::memcpy(out.strn_wave, strn->visual_wave(), sizeof out.strn_wave);
                std::memcpy(out.strn_positions, strn->visual_positions(), sizeof out.strn_positions);
                out.strn_grains = strn->visual_grains();
                out.strn_fill = strn->visual_fill();
                out.strn_mix = strn->visual_mix();
                out.strn_frozen = strn->visual_frozen();
            }
            if (!unit || unit->kind != UnitKind::Recorder) {
                out.rec_slot[chain][slot] = 0xff;
                out.rec_frames[chain][slot] = out.rec_capacity[chain][slot] = 0;
                continue;
            }
            const auto* rec = static_cast<const RecUnit*>(unit);
            const uint8_t bit = (uint8_t)(1u << chain);
            out.rec_slot[chain][slot] = (uint8_t)rec->sample_slot;
            const uint32_t frames = rec->recorded_frames();
            out.rec_frames[chain][slot] = frames;
            out.rec_capacity[chain][slot] = rec->capacity_frames();
            if (chain == focus_chain && slot == focus_slot) {
                out.rec_waveform_chain = (int8_t)chain;
                out.rec_waveform_slot = (int8_t)slot;
                out.rec_waveform_session = rec->session_token();
                out.rec_waveform_valid = 1u |
                    (rec->is_recording() ? 2u : 0u) |
                    (recorder_record_pending(chain, slot) ? 4u : 0u) |
                    (rec->take_needs_save() ? 8u : 0u) |
                    (rec->is_paused() ? 16u : 0u);
                static_assert(EngineUiSnapshot::REC_WAVEFORM_POINTS == RecUnit::WAVE_POINTS,
                              "REC snapshot/wave cache size mismatch");
                rec->copy_visual_wave(out.rec_waveform);
            }
            if (rec->is_recording()) out.rec_recording |= bit;
            if (rec->recorded_frames()) out.rec_has_take |= bit;
            if (rec->take_needs_save()) out.rec_unsaved_take |= bit;
        }
    }
    std::memcpy(out.chain_peak, chain_peak, sizeof chain_peak);
    for (int chain = 0; chain < NUM_CHAINS; ++chain) {
        const Chain& c = chains[chain];
        out.chain_mute[chain] = c.mute;
        out.chain_volume[chain] = c.volume;
        out.chain_pan[chain] = c.pan;
        uint8_t bypass = 0;
        for (int slot = 0; slot < UNITS_PER_CHAIN; ++slot) {
            const Unit* u = c.units[slot];
            out.unit_kind[chain][slot] = (uint8_t)(u ? u->kind : UnitKind::Empty);
            if (focus_sigil >= 0 && focus_sigil < NUM_SIGILS) {
                out.sigil_patch.route_count[chain][slot] = 0;
                out.sigil_patch.activity[chain][slot] = 0;
                if (u) {
                    const ModSource selected_source = sigil_mod_source(focus_sigil);
                    uint8_t routes = 0;
                    int32_t strongest = 0;
                    const int32_t sigil_value = sigils[focus_sigil].value;
                    for (int param = 0; param < u->num_params; ++param) {
                        const Param& p = u->params[param];
                        if (p.src != selected_source || p.mod_depth == 0) continue;
                        if (routes != 0xffu) ++routes;
                        int32_t contribution = p.mod_enabled ? (sigil_value * p.mod_depth) >> 15 : 0;
                        if (contribution < 0) contribution = -contribution;
                        if (contribution > strongest) strongest = contribution;
                    }
                    out.sigil_patch.route_count[chain][slot] = routes;
                    out.sigil_patch.activity[chain][slot] = (uint8_t)(strongest * 255 / 32767);
                }
            }
            out.vca_envelope[chain][slot] = 0;
            out.vca_state[chain][slot] = 0;
            out.delay_frames[chain][slot] = 0;
            out.delay_wow_offset[chain][slot] = 0;
            out.delay_wet_peak[chain][slot] = 0;
            out.delay_hold[chain][slot] = 0;
            if (u && u->kind == UnitKind::Vca) {
                const auto* vca = static_cast<const VcaUnit*>(u);
                out.vca_envelope[chain][slot] = vca->visual_envelope();
                out.vca_state[chain][slot] = vca->visual_state();
            } else if (u && u->kind == UnitKind::Delay) {
                const auto* delay = static_cast<const DelayUnit*>(u);
                out.delay_frames[chain][slot] = delay->visual_delay_frames();
                out.delay_wow_offset[chain][slot] = delay->visual_wow_offset();
                out.delay_wet_peak[chain][slot] = (c.mute || u->bypass) ? 0 : delay->visual_wet_peak();
                out.delay_hold[chain][slot] =
                    u->params[DelayUnit::P_HOLD].effective > 16384 ? 1 : 0;
            }
            if (focus_sigil < 0 || focus_sigil >= NUM_SIGILS) {
                q15 strongest = 0;
                int32_t strongest_abs = 0;
                if (u) {
                    for (int param = 0; param < u->num_params; ++param) {
                        const Param& p = u->params[param];
                        const int32_t effect = (int32_t)p.target - p.base;
                        const int32_t magnitude = effect < 0 ? -effect : effect;
                        if (magnitude > strongest_abs) { strongest_abs = magnitude; strongest = (q15)effect; }
                    }
                }
                out.unit_mod_effect[chain][slot] = strongest;
            }
            if (u && u->bypass) bypass |= (uint8_t)(1u << slot);
        }
        out.unit_bypass[chain] = bypass;
        out.scene_morph[chain] = scene_morph[chain];
        out.scene_valid[chain] = (uint8_t)((scenes[chain][0].valid ? 1u : 0u) |
                                           (scenes[chain][1].valid ? 2u : 0u));
    }
    capture_sequence_snapshot(out);
    std::memcpy(out.scope, scope, sizeof scope);
    out.scope_pos = scope_pos;
    out.master_peak = master_peak_;
    out.master_serial = (uint32_t)audio_frame_;
    out.render_peak_us = render_peak_us;
    out.dsp_load_permille = dsp_load_permille;
    for (int i = 0; i < NUM_SIGILS; ++i) {
        out.sigil_value[i] = sigils[i].value;
        out.sigil_phase[i] = sigils[i].phase;
    }
}

Pattern& Engine::sequence_pattern(int bank) {
    return bank < 0 || bank >= NUM_PATTERNS || bank == active_pattern
        ? pattern : pattern_banks[bank];
}

const Pattern& Engine::sequence_pattern(int bank) const {
    return bank < 0 || bank >= NUM_PATTERNS || bank == active_pattern
        ? pattern : pattern_banks[bank];
}

// Shared six-slot source authority for UI projection and runtime pitch ownership.
// Never scan pattern cells here. Later overwrite-sources (including MIC/NOISE)
// mask earlier pitch; STRN is a fallback only in the absence of any real source.
Engine::SequenceSource Engine::resolve_sequence_source(int chain) const {
    SequenceSource result;
    if (chain < 0 || chain >= NUM_CHAINS) return result;
    const Chain& c = chains[chain];
    int fallback_slot = -1;
    bool downstream_gate = false;
    for (int slot = 0; slot < UNITS_PER_CHAIN; ++slot) {
        const Unit* unit = c.units[slot];
        if (!unit || unit->bypass) continue;
        SequencerProperties properties = unit->sequencer_properties();
        if (unit->kind == UnitKind::Motor) {
            // MOTOR metadata normally reads the preceding control tick's target.
            // Use the current unslewed layer+mod law instead: an accepted MODE
            // lock may enter/exit note mode on this very event, in any lock order.
            const Param& mode = unit->params[MotorUnit::P_MODE];
            const q31 value = (q31)mode.control_source() + (mode.mod_enabled
                ? ((q31)modulation_value(mode.src) * mode.mod_depth >> 15) : 0);
            properties.pitch_param = static_cast<const MotorUnit*>(unit)->modern() && value >= 16384
                ? MotorUnit::P_NOTE : -1;
        } else if (unit->kind == UnitKind::Scratch &&
                   static_cast<const ScratchUnit*>(unit)->mode != ScratchUnit::Mode::Hold) {
            properties.pitch_param = -1; // pitch is dormant in the scratch reader
        }
        if (properties.pitch_param >= unit->num_params || properties.pitch_param >= PARAM_BANK_SIZE)
            properties.pitch_param = -1;
        if (properties.source) {
            result.slot = (int8_t)slot;
            result.properties = properties;
            downstream_gate = false;
        } else if (result.slot >= 0 && properties.gate_processor) {
            downstream_gate = true;
        }
        if (unit->kind == UnitKind::Stranular) fallback_slot = slot;
    }
    if (result.slot < 0 && fallback_slot >= 0) {
        result.slot = (int8_t)fallback_slot;
        result.properties = c.units[fallback_slot]->sequencer_properties();
        for (int slot = fallback_slot + 1; slot < UNITS_PER_CHAIN; ++slot) {
            const Unit* unit = c.units[slot];
            if (unit && !unit->bypass && unit->sequencer_properties().gate_processor)
                downstream_gate = true;
        }
    }
    if (result.slot >= 0 && c.units[(int)result.slot]->kind == UnitKind::SamplePlayer)
        result.pitch_domain = (uint8_t)static_cast<const PlayUnit*>(c.units[(int)result.slot])->pitch_mode();
    result.lifecycle = result.slot < 0 ? SequenceLifecycle::Strike : result.properties.lifecycle;
    if (result.lifecycle == SequenceLifecycle::Flow && downstream_gate)
        result.lifecycle = SequenceLifecycle::Gate;
    return result;
}

void Engine::capture_sequence_snapshot(EngineUiSnapshot& out, int bank) const {
    out.project_scale = project_scale_;
    const Pattern& selected = sequence_pattern(bank);
    // A transition is an event stream, not one playing bank. Suppress the
    // single-bank head even at an endpoint: old accepted lookahead may remain.
    const bool active = &selected == &pattern && !pattern_transition_.active;
    for (int chain = 0; chain < NUM_CHAINS; ++chain) {
        const Chain& c = chains[chain];
        uint8_t length = selected.length[chain];
        if (length < 1 || length > SEQ_STEPS) length = 16;
        out.sequence_length[chain] = length;
        SequenceDirection direction = selected.direction[chain];
        if (direction >= SequenceDirection::Count) direction = SequenceDirection::Forward;
        out.sequence_direction[chain] = (uint8_t)direction;
        out.sequence_step[chain] = active ? (sequence_tick
            ? sequence_step_at_tick(chain, sequence_tick - 1)
            : sequence_step_at_tick(chain, 0)) : 255;
        const SequenceSource source = resolve_sequence_source(chain);
        const int source_slot = source.slot;
        const SequencerProperties& source_properties = source.properties;
        const UnitKind source_kind = source_slot < 0 ? UnitKind::Empty : c.units[source_slot]->kind;
        const SequenceLifecycle lifecycle = source.lifecycle;
        out.sequence_lifecycle[chain] = (uint8_t)lifecycle;
        // Publish event identity independently of the bank being edited. Reuse
        // the existing topology pass; a continuous flow/empty chain has no cell
        // strike. This is audio time, never inferred from displayed grid motion.
        out.sequence_hit[chain] = {};
        const SequenceHit& hit = sequence_hit_[chain];
        if (transport_playing && sequencer_enabled && source_slot >= 0 &&
            lifecycle != SequenceLifecycle::Flow && hit.step < SEQ_STEPS &&
            hit.bank < NUM_PATTERNS && audio_frame_ >= sequence_hit_audio_frame_[chain]) {
            const uint64_t age = (audio_frame_ - sequence_hit_audio_frame_[chain]) / (ENGINE_SR / 1000);
            if (age < hit.duration_ms) {
                out.sequence_hit[chain] = hit;
                out.sequence_hit[chain].age_ms = (uint16_t)age;
            }
        }

        const int pitch_slot = source_properties.pitch_param >= 0 ? source_slot : -1;
        const int pitch_param = source_properties.pitch_param;
        const UnitKind pitch_kind = pitch_slot < 0 ? UnitKind::Empty : source_kind;
        out.sequence_pitch_slot[chain] = (int8_t)pitch_slot;
        out.sequence_pitch_param[chain] = (int8_t)pitch_param;
        out.sequence_pitch_kind[chain] = (uint8_t)pitch_kind;

        const int value_slot = source_properties.level_param >= 0 ? source_slot : -1;
        const int value_param = lifecycle == SequenceLifecycle::Flow && source_properties.flow_param >= 0
            ? source_properties.flow_param : source_properties.level_param;
        const int duration_slot = source_properties.duration_param >= 0 ? source_slot : -1;
        const int duration_param = source_properties.duration_param;
        out.sequence_value_slot[chain] = (int8_t)value_slot;
        out.sequence_value_param[chain] = (int8_t)value_param;
        out.sequence_duration_slot[chain] = (int8_t)duration_slot;
        out.sequence_duration_param[chain] = (int8_t)duration_param;
        out.sequence_source_kind[chain] = (uint8_t)source_kind;
        uint16_t trig = 0;
        uint64_t pitch_lock_mask = 0;
        uint64_t value_lock_mask = 0;
        for (int step = 0; step < SEQ_STEPS; ++step) {
            const SeqStep& s = selected.steps[chain][step];
            if (step < 16 && (s.flags & SeqStep::Trig))
                trig |= (uint16_t)(1u << step);
            const uint16_t locks = s.lock_count > SEQ_LOCKS ? SEQ_LOCKS : s.lock_count;
            int timing = selected.microtiming[chain][step];
            if (timing < -SEQ_MICROTIMING_MAX) timing = -SEQ_MICROTIMING_MAX;
            if (timing >  SEQ_MICROTIMING_MAX) timing =  SEQ_MICROTIMING_MAX;
            out.sequence_meta[chain][step] = (uint16_t)(
                (s.flags & 0x3fu) | (locks << 6) |
                ((uint16_t)(timing + SEQ_MICROTIMING_MAX) << 9));
            q15 pitch = 0;
            if (pitch_slot >= 0 && pitch_param >= 0) {
                const Unit* pitch_unit = c.units[pitch_slot];
                pitch = pitch_unit->params[pitch_param].base;
                for (int lock = 0; lock < locks; ++lock)
                    if (s.locks[lock].slot == pitch_slot && s.locks[lock].param == pitch_param) {
                        pitch = s.locks[lock].value;
                        pitch_lock_mask |= (uint64_t)1u << step;
                        break;
                    }
            }
            out.sequence_pitch[chain][step] = pitch;
            q15 value = 0;
            if (value_slot >= 0 && value_param >= 0) {
                const Unit* value_unit = c.units[value_slot];
                value = value_unit->params[value_param].base;
                for (int lock = 0; lock < locks; ++lock)
                    if (s.locks[lock].slot == value_slot && s.locks[lock].param == value_param) {
                        value = s.locks[lock].value;
                        value_lock_mask |= (uint64_t)1u << step;
                        break;
                    }
            }
            out.sequence_value[chain][step] = value;
            q15 duration = 0;
            if (duration_slot >= 0 && duration_param >= 0) {
                const Unit* duration_unit = c.units[duration_slot];
                duration = duration_unit->params[duration_param].base;
                for (int lock = 0; lock < locks; ++lock)
                    if (s.locks[lock].slot == duration_slot && s.locks[lock].param == duration_param) {
                        duration = s.locks[lock].value;
                        break;
                    }
            }
            int normalized_duration = duration;
            if (normalized_duration < 0) normalized_duration = -normalized_duration;
            if (normalized_duration > 32767) normalized_duration = 32767;
            out.sequence_duration[chain][step] = (uint8_t)(normalized_duration * 255 / 32767);
        }
        out.step_trig[chain] = trig;
        out.sequence_pitch_lock_mask[chain] = pitch_lock_mask;
        out.sequence_value_lock_mask[chain] = value_lock_mask;
    }
}

void Engine::apply_macro_pad(int pad, q15 value) {
    if (pad < 0 || pad >= 8) return;
    MacroPad& macro = macro_pads[pad];
    int count = macro.route_count > MacroPad::MAX_ROUTES ? MacroPad::MAX_ROUTES : macro.route_count;
    for (int i = 0; i < count; ++i) {
        const MacroPadRoute& r = macro.routes[i];
        if (r.chain >= NUM_CHAINS || r.slot >= UNITS_PER_CHAIN) continue;
        Unit* u = chains[r.chain].units[r.slot];
        if (!u || r.param >= u->num_params) continue;
        u->params[r.param].base = (q15)((int32_t)r.minimum + (((int32_t)r.maximum - r.minimum) * value >> 15));
    }
}

void Engine::apply_global_fx(GlobalFxTarget target, q15 value) {
    if ((uint8_t)target >= (uint8_t)GlobalFxTarget::Count) return;
    if (value < 0) value = 0;

    // Master-bus targets: always audible, no matching unit required.
    switch (target) {
        case GlobalFxTarget::Cutoff:
        case GlobalFxTarget::Resonance:
        case GlobalFxTarget::Crush:
        case GlobalFxTarget::Drive:
            master_fx_target_[(int)target] = value;
            return;
        default: break;
    }

    auto set = [value](Unit* unit, int param, q15 minimum, q15 maximum) {
        if (!unit || param < 0 || param >= unit->num_params) return;
        Param& p = unit->params[param];
        if (value <= 0) {
            p.performance_override = false;
            return;
        }
        p.performance = (q15)((int32_t)minimum +
            (((int32_t)maximum - minimum) * value >> 15));
        p.performance_override = true;
    };
    for (auto& chain : chains) for (Unit* unit : chain.units) {
        if (!unit) continue;
        switch (target) {
            case GlobalFxTarget::Reverb:
                if (unit->kind == UnitKind::Reverb) set(unit, ReverbUnit::P_MIX, 0, 26000);
                break;
            case GlobalFxTarget::Delay:
                if (unit->kind == UnitKind::Delay) set(unit, DelayUnit::P_MIX, 0, 26000);
                break;
            case GlobalFxTarget::Freeze:
                if (unit->kind == UnitKind::Delay) set(unit, DelayUnit::P_HOLD, 0, 32767);
                else if (unit->kind == UnitKind::Ghost) set(unit, GhostUnit::P_FREEZE, 0, 32767);
                else if (unit->kind == UnitKind::Paul) set(unit, PaulUnit::P_FREEZE, 0, 32767);
                break;
            case GlobalFxTarget::Wash:
                if (unit->kind == UnitKind::Reverb) {
                    set(unit, ReverbUnit::P_DECAY, 16000, 30000);
                    set(unit, ReverbUnit::P_MIX, 0, 28000);
                } else if (unit->kind == UnitKind::Delay) {
                    set(unit, DelayUnit::P_FEEDBACK, 10000, 28000);
                    set(unit, DelayUnit::P_MIX, 0, 22000);
                } else if (unit->kind == UnitKind::Ghost) {
                    set(unit, GhostUnit::P_SMEAR, 8000, 30000);
                    set(unit, GhostUnit::P_MIX, 0, 26000);
                }
                break;
            default: break;
        }
    }
}

void Engine::apply_global_fx_mask(uint8_t active_mask) {
    // Clear every runtime FX layer, then reapply active targets in deterministic
    // enum order. This makes overlapping effects (e.g. Reverb + Wash) release
    // without clearing parameters still owned by another active effect.
    for (auto& chain : chains) for (Unit* unit : chain.units) {
        if (!unit) continue;
        for (int param = 0; param < unit->num_params; ++param)
            unit->params[param].performance_override = false;
    }
    for (int target = 0; target < (int)GlobalFxTarget::Count; ++target)
        master_fx_target_[target] = 0;
    for (int target = 0; target < (int)GlobalFxTarget::Count; ++target)
        if (active_mask & (1u << target))
            apply_global_fx((GlobalFxTarget)target, trackr::fx::Q15_ONE);
}

void Engine::set_live_retrigger(int chain, int division, bool active) {
    if (!active) { live_retrigger_active_ = false; return; }
    if (chain < 0 || chain >= NUM_CHAINS) return;
    if (division < 0) division = 0;
    if (division > 3) division = 3;
    const bool changed = !live_retrigger_active_ || live_retrigger_chain_ != chain ||
                         live_retrigger_division_ != division;
    live_retrigger_active_ = true;
    live_retrigger_chain_ = (uint8_t)chain;
    live_retrigger_division_ = (uint8_t)division;
    if (changed) {
        trigger_chain(chain);
        uint32_t interval = frames_per_beat() >> division;
        if (!interval) interval = 1;
        live_retrigger_next_frame_ = audio_frame_ + interval;
        if (clock_runtime_) clock_roll_phase_ = clock_audio_phase_ + (CLOCK_BEAT >> division);
    }
}

void Engine::fire_live_retriggers() {
    if (!live_retrigger_active_) return;
    if (clock_runtime_) {
        while (clock_audio_phase_ >= clock_roll_phase_) {
            trigger_chain(live_retrigger_chain_);
            clock_roll_phase_ += CLOCK_BEAT >> live_retrigger_division_;
        }
        return;
    }
    uint32_t interval = frames_per_beat() >> live_retrigger_division_;
    if (!interval) interval = 1;
    while (audio_frame_ >= live_retrigger_next_frame_) {
        trigger_chain(live_retrigger_chain_);
        live_retrigger_next_frame_ += interval;
    }
}

void Engine::capture_scene(int chain, int which) {
    if (chain < 0 || chain >= NUM_CHAINS || which < 0 || which > 1) return;
    release_sequence_locks();
    PersistentScene& scene = scenes[chain][which];
    scene = {};
    scene.valid = true;
    const Chain& source = chains[chain];
    scene.volume = source.volume;
    scene.pan = source.pan;
    for (int slot = 0; slot < UNITS_PER_CHAIN; ++slot) {
        Unit* unit = source.units[slot];
        scene.kind[slot] = unit ? unit->kind : UnitKind::Empty;
        for (int param = 0; param < MAX_PARAMS; ++param)
            scene.base[slot][param] = unit && param < unit->num_params ? unit->params[param].base : 0;
    }
}

bool Engine::scene_topology_compatible(int which) const {
    if (which < 0 || which > 1) return false;
    for (int chain = 0; chain < NUM_CHAINS; ++chain) {
        const PersistentScene& scene = scenes[chain][which];
        if (!scene.valid) continue;
        for (int slot = 0; slot < UNITS_PER_CHAIN; ++slot) {
            const Unit* unit = chains[chain].units[slot];
            const UnitKind kind = unit ? unit->kind : UnitKind::Empty;
            if (scene.kind[slot] != kind) return false;
        }
    }
    return true;
}

bool Engine::global_scene_topology_compatible() const {
    for (int chain = 0; chain < NUM_CHAINS; ++chain) {
        const auto& a = scenes[chain][0];
        const auto& b = scenes[chain][1];
        if (!a.valid || !b.valid) return false;
        for (int slot = 0; slot < UNITS_PER_CHAIN; ++slot) {
            const Unit* unit = chains[chain].units[slot];
            const UnitKind kind = unit ? unit->kind : UnitKind::Empty;
            if (a.kind[slot] != kind || b.kind[slot] != kind) return false;
        }
    }
    return true;
}

void Engine::apply_scene_morph(int chain, q15 morph) {
    if (chain < 0 || chain >= NUM_CHAINS) return;
    if (morph < 0) morph = 0;
    release_sequence_locks();
    scene_morph[chain] = morph;
    const PersistentScene& a = scenes[chain][0];
    const PersistentScene& b = scenes[chain][1];
    if (!a.valid || !b.valid) return;
    auto blend = [morph](q15 x, q15 y) -> q15 {
        return (q15)((int32_t)x + ((((int32_t)y - x) * morph) >> 15));
    };
    Chain& target = chains[chain];
    target.volume = blend(a.volume, b.volume);
    target.pan = blend(a.pan, b.pan);
    for (int slot = 0; slot < UNITS_PER_CHAIN; ++slot) {
        Unit* unit = target.units[slot];
        if (!unit || a.kind[slot] != unit->kind || b.kind[slot] != unit->kind) continue;
        for (int param = 0; param < unit->num_params; ++param)
            unit->params[param].base = blend(a.base[slot][param], b.base[slot][param]);
    }
}

void Engine::capture_global_scene(int which) {
    if (which < 0 || which > 1) return;
    for (int chain = 0; chain < NUM_CHAINS; ++chain) capture_scene(chain, which);
}

void Engine::apply_global_scene_morph(q15 morph) {
    if (!global_scene_topology_compatible()) return;
    for (int chain = 0; chain < NUM_CHAINS; ++chain) apply_scene_morph(chain, morph);
}

bool Engine::apply_transition_scene_morph(q15 morph) {
    if (!global_scene_topology_compatible()) return false;
    if (morph < 0) morph = 0;
    auto blend = [morph](q15 x, q15 y) -> q15 {
        return (q15)((int32_t)x + ((((int32_t)y - x) * morph) >> 15));
    };
    for (int chain = 0; chain < NUM_CHAINS; ++chain) {
        const PersistentScene& a = scenes[chain][0];
        const PersistentScene& b = scenes[chain][1];
        Chain& target = chains[chain];
        scene_morph[chain] = morph;
        target.pan = blend(a.pan, b.pan);
        for (int slot = 0; slot < UNITS_PER_CHAIN; ++slot) {
            Unit* unit = target.units[slot];
            if (!unit) continue;
            for (int param = 0; param < unit->num_params; ++param)
                unit->params[param].base = blend(a.base[slot][param], b.base[slot][param]);
        }
    }
    return true;
}

// Q32 beats keep queued decisions independent of any sample-rate deadline.
// The increment is rounded once per control tick (worst error <0.000224 bpm).
static uint64_t clock_swing_delay(int32_t swing) {
    return (uint64_t)(swing - 50 * 65536) * 65536u / 200u;
}

ClockMod Engine::clock_mod(ClockTarget target) const {
    return (unsigned)target < 2 ? clock_mods_[(unsigned)target] : ClockMod{};
}
bool Engine::clock_routes_active() const {
    for (const auto& route : clock_mods_)
        if (route.src != ModSource::None && route.depth) return true;
    return false;
}
bool Engine::set_clock_mod(ClockTarget target, ModSource source, int depth) {
    if ((unsigned)target >= 2 || (source != ModSource::None && mod_source_sigil(source) < 0)) return false;
    const int limit = target == ClockTarget::Tempo ? CLOCK_TEMPO_DEPTH_MAX : CLOCK_SWING_DEPTH_MAX;
    if (depth < -limit) depth = -limit;
    if (depth > limit) depth = limit;
    ClockMod& route = clock_mods_[(unsigned)target];
    if (route.src == source && route.depth == depth) return false;
    // Convert existing queues before changing the route. No replan/reroll/lock release.
    if (!clock_runtime_ && source != ModSource::None && depth) activate_clock_runtime();
    route = {source, (int16_t)depth};
    return true;
}
int32_t Engine::effective_bpm_q16() const { return clock_runtime_ ? clock_applied_[0] : bpm * 65536; }
int32_t Engine::effective_swing_q16() const { return clock_runtime_ ? clock_applied_[1] : swing_percent * 65536; }
int Engine::effective_bpm() const { return (effective_bpm_q16() + 32768) / 65536; }
void Engine::capture_clock_inspector(ClockTarget target, ClockInspector& out) const {
    out = {};
    if ((unsigned)target >= 2) return;
    const bool tempo = target == ClockTarget::Tempo;
    out.mod = clock_mod(target);
    out.base = tempo ? bpm : swing_percent;
    out.source = modulation_value(out.mod.src);
    // q15 asymmetric endpoints map exactly to physical +/-depth; no signed shifts.
    const int divisor = out.source < 0 ? 32768 : 32767;
    const int64_t wanted = (int64_t)out.base * 65536 +
        (int64_t)out.source * out.mod.depth * 65536 / divisor;
    const int32_t low = (tempo ? 20 : 50) * 65536, high = (tempo ? 300 : 75) * 65536;
    out.limited = wanted < low || wanted > high;
    out.target_q16 = (int32_t)(wanted < low ? low : wanted > high ? high : wanted);
    out.applied_q16 = tempo ? effective_bpm_q16() : effective_swing_q16();
}
uint64_t Engine::clock_phase_for_frame(uint64_t frame) const {
    const uint64_t fpb = frames_per_beat();
    if (frame >= transport_frame) return clock_phase_ + (frame - transport_frame) * CLOCK_BEAT / fpb;
    const uint64_t back = (transport_frame - frame) * CLOCK_BEAT / fpb;
    return back > clock_phase_ ? 0 : clock_phase_ - back;
}
uint64_t Engine::clock_frame_for_phase(uint64_t phase) const {
    if (phase <= clock_phase_) return transport_frame;
    return transport_frame + (phase - clock_phase_ + clock_increment_ - 1) / clock_increment_;
}
uint64_t Engine::clock_next_boundary(LaunchQuantize quantize) const {
    uint64_t quantum = CLOCK_STEP;
    if (quantize == LaunchQuantize::Beat) quantum = CLOCK_BEAT;
    else if (quantize == LaunchQuantize::Bar) quantum = CLOCK_BEAT * beats_per_bar;
    return (clock_phase_ / quantum + 1) * quantum;
}
void Engine::activate_clock_runtime() {
    if (clock_runtime_) return;
    const uint32_t fpb = frames_per_beat();
    clock_applied_[0] = bpm * 65536;
    clock_applied_[1] = swing_percent * 65536;
    clock_phase_ = (uint64_t)beat_count * CLOCK_BEAT + (uint64_t)frame_in_beat * CLOCK_BEAT / fpb;
    clock_audio_phase_ = 0;
    clock_increment_ = (uint32_t)(((uint64_t)clock_applied_[0] * 65536 + ENGINE_SR * 30u) / (ENGINE_SR * 60u));
    clock_plan_phase_ = clock_phase_for_frame(sequence_plan_grid_frame_);
    for (int c = 0; c < NUM_CHAINS; ++c) {
        clock_event_start_[c] = clock_phase_for_frame(sequence_event_frame_[c]);
        clock_event_end_[c] = clock_phase_for_frame(sequence_event_frame_[c] + sequence_event_span_[c]);
        clock_retrig_next_[c] = 1;
        const uint8_t count = active_sequence_step_[c].retrig_count();
        while (clock_retrig_next_[c] < count && sequence_event_frame_[c] +
               (uint64_t)sequence_event_span_[c] * clock_retrig_next_[c] / count <= transport_frame)
            ++clock_retrig_next_[c];
        for (auto& event : pending_sequence_events_[c]) if (event.valid) {
            event.clock_phase = clock_phase_for_frame(event.frame);
            event.clock_nominal = clock_phase_for_frame(event.nominal_frame);
            const uint64_t delay = (event.tick & 1u) ? clock_swing_delay(clock_applied_[1]) : 0;
            event.clock_grid = event.clock_nominal > delay ? event.clock_nominal - delay : 0;
            event.clock_micro = (int64_t)event.clock_phase - (int64_t)event.clock_nominal;
        }
    }
    if (pending_recorder_command_.valid)
        clock_recorder_phase_ = clock_phase_for_frame(pending_recorder_command_.deadline);
    if (live_retrigger_active_)
        clock_roll_phase_ = (live_retrigger_next_frame_ > audio_frame_
            ? live_retrigger_next_frame_ - audio_frame_ : 0) * CLOCK_BEAT / fpb;
    for (auto& chain : chains) for (Unit* unit : chain.units)
        if (unit && unit->kind == UnitKind::SamplePlayer) {
            auto* play = static_cast<PlayUnit*>(unit);
            if (play->gate_deadline_ != UINT64_MAX && !play->gate_phase_) {
                play->gate_deadline_ = clock_phase_for_frame(play->gate_deadline_);
                play->gate_phase_ = true;
            }
        }
    for (auto& chain : chains) for (Unit* unit : chain.units)
        if (unit && unit->kind == UnitKind::Grain) {
            auto* gran = static_cast<GrainUnit*>(unit);
            if (gran->gate_deadline_ != UINT64_MAX && !gran->gate_phase_) {
                gran->gate_deadline_ = clock_phase_for_frame(gran->gate_deadline_);
                gran->gate_phase_ = true;
            }
        }
    for(auto& chain:chains)for(Unit* unit:chain.units)if(unit && unit->kind==UnitKind::Scratch) {
        auto* scratch=static_cast<ScratchUnit*>(unit);
        if(scratch->gate_deadline_!=UINT64_MAX && !scratch->gate_phase_) {
            scratch->gate_deadline_=clock_phase_for_frame(scratch->gate_deadline_);scratch->gate_phase_=true;
        }
    }
    for(auto& chain:chains)for(Unit* unit:chain.units)if(unit && unit->kind==UnitKind::Paul) {
        auto* p=static_cast<PaulUnit*>(unit);
        if(p->gate_deadline_!=UINT64_MAX && !p->gate_phase_) {
            p->gate_deadline_=clock_phase_for_frame(p->gate_deadline_);p->gate_phase_=true;
        }
    }
    clock_runtime_ = true;
    update_clock_deadlines();
}
void Engine::reset_clock_runtime() {
    const bool was_phase_clock = clock_runtime_;
    const uint64_t roll_remaining = clock_roll_phase_ > clock_audio_phase_
        ? clock_roll_phase_ - clock_audio_phase_ : 0;
    clock_runtime_ = false;
    clock_phase_ = clock_audio_phase_ = 0;
    clock_applied_[0] = bpm * 65536;
    clock_applied_[1] = swing_percent * 65536;
    if (clock_routes_active()) activate_clock_runtime();
    if (was_phase_clock && live_retrigger_active_) {
        if (clock_runtime_) clock_roll_phase_ = clock_audio_phase_ + roll_remaining;
        else live_retrigger_next_frame_ = audio_frame_ +
            (roll_remaining * frames_per_beat() + CLOCK_BEAT - 1) / CLOCK_BEAT;
    }
}
void Engine::update_clock_unit(Unit* unit) {
    if (!unit) return;
    if (unit->kind == UnitKind::Stranular) {
        auto* granular = static_cast<StranularUnit*>(unit);
        if (clock_runtime_) granular->set_transport_bpm_q16(effective_bpm_q16());
        else granular->set_transport_bpm(bpm);
    } else if (unit->kind == UnitKind::Grain) {
        auto* granular = static_cast<GrainUnit*>(unit);
        if (clock_runtime_) granular->set_transport_bpm_q16(effective_bpm_q16());
        else granular->set_transport_bpm(bpm);
    } else if (unit->kind == UnitKind::Delay) {
        // Setter refreshes a sync target immediately after control_tick so no
        // stale-tempo tick enters the next audio block or sequence lock boundary.
        static_cast<DelayUnit*>(unit)->set_clock_bpm_q16(effective_bpm_q16());
    } else if (unit->kind == UnitKind::Vca) {
        static_cast<VcaUnit*>(unit)->set_clock_bpm_q16(effective_bpm_q16());
    } else if (unit->kind == UnitKind::Fract) {
        static_cast<FractUnit*>(unit)->set_transport_bpm_q16(effective_bpm_q16());
    } else if (unit->kind == UnitKind::Comp) {
        static_cast<CompUnit*>(unit)->set_transport_bpm_q16(effective_bpm_q16());
    }
}
void Engine::update_clock_modulation() {
    if (!clock_runtime_) return;
    for (int i = 0; i < 2; ++i) {
        ClockInspector inspector;
        capture_clock_inspector((ClockTarget)i, inspector);
        const int32_t difference = inspector.target_q16 - clock_applied_[i];
        int32_t step = difference / 20; // 20 ms one-pole; finite 1/Q16 tail.
        if (!step && difference) step = difference > 0 ? 1 : -1;
        // At 20 bpm, even the maximum swing motion is <half transport speed.
        // Future odd events cannot escape the advancing clock or cross neighbours.
        if (i == 1) {
            if (step > 2048) step = 2048;
            if (step < -2048) step = -2048; // 1/32 percentage point per ms.
        }
        clock_applied_[i] += step;
    }
    clock_increment_ = (uint32_t)(((uint64_t)clock_applied_[0] * 65536 + ENGINE_SR * 30u) / (ENGINE_SR * 60u));
    // Already-fired starts remain fixed. Only unconsumed events and retrig ends
    // follow swing; their snapshots, RNG decisions and pulse cursor never change.
    const uint64_t delay = clock_swing_delay(clock_applied_[1]);
    for (auto& chain : pending_sequence_events_) for (auto& event : chain) if (event.valid) {
        event.clock_nominal = event.clock_grid + ((event.tick & 1u) ? delay : 0);
        const int64_t due = (int64_t)event.clock_nominal + event.clock_micro;
        event.clock_phase = due < 0 ? 0 : (uint64_t)due;
    }
    update_clock_deadlines();
    if (transport_playing) dispatch_sequence_phase();
}
void Engine::update_clock_deadlines() {
    for (int c = 0; c < NUM_CHAINS; ++c) {
        uint64_t next = UINT64_MAX;
        for (auto& event : pending_sequence_events_[c]) if (event.valid) {
            event.frame = clock_frame_for_phase(event.clock_phase);
            event.nominal_frame = clock_frame_for_phase(event.clock_nominal);
            if (event.clock_phase < next) next = event.clock_phase;
        }
        if (next != UINT64_MAX && next > clock_event_start_[c]) clock_event_end_[c] = next;
    }
    sequence_plan_grid_frame_ = clock_frame_for_phase(clock_plan_phase_);
    if (pending_recorder_command_.valid)
        pending_recorder_command_.deadline = clock_frame_for_phase(clock_recorder_phase_);
    frame_in_beat = (uint32_t)(((clock_phase_ & 0xffffffffu) * frames_per_beat()) >> 32);
}
void Engine::schedule_sequence_phase(uint64_t tick, uint64_t grid) {
    const uint64_t nominal = grid + ((tick & 1u) ? clock_swing_delay(clock_applied_[1]) : 0);
    for (int c = 0; c < NUM_CHAINS; ++c) {
        PendingSequenceEvent* slot = nullptr;
        for (auto& event : pending_sequence_events_[c]) if (!event.valid) { slot = &event; break; }
        if (!slot) continue;
        const int bank = transition_bank_at_tick(c, tick);
        const Pattern& selected = sequence_pattern(bank);
        const int index = sequence_step_at_tick(c, tick, bank);
        int amount = selected.microtiming[c][index];
        if (amount < -SEQ_MICROTIMING_MAX) amount = -SEQ_MICROTIMING_MAX;
        if (amount > SEQ_MICROTIMING_MAX) amount = SEQ_MICROTIMING_MAX;
        slot->valid = true;
        slot->tick = tick;
        slot->clock_grid = grid;
        slot->clock_nominal = nominal;
        slot->clock_micro = (int64_t)CLOCK_STEP * amount / SEQ_MICROTIMING_DIVISIONS;
        const int64_t due = (int64_t)nominal + slot->clock_micro;
        slot->clock_phase = due < 0 ? 0 : (uint64_t)due;
        slot->frame = clock_frame_for_phase(slot->clock_phase);
        slot->nominal_frame = clock_frame_for_phase(nominal);
        slot->source_step = (uint8_t)index;
        slot->source_bank = bank < 0 ? active_pattern : (uint8_t)bank;
        slot->step = selected.steps[c][index];
        slot->condition_pass = step_condition_passes(slot->step, c, tick, bank);
    }
}
uint64_t Engine::clock_next_deadline_phase() const {
    uint64_t next = (clock_phase_ / CLOCK_STEP + 1) * CLOCK_STEP;
    if (pending_recorder_command_.valid && clock_recorder_phase_ < next) next = clock_recorder_phase_;
    if (!sequence_clock_active_) return next;
    for (int c = 0; c < NUM_CHAINS; ++c) {
        for (const auto& event : pending_sequence_events_[c])
            if (event.valid && event.clock_phase < next) next = event.clock_phase;
        const uint8_t count = active_sequence_step_[c].retrig_count(), pulse = clock_retrig_next_[c];
        if (active_sequence_pass_[c] && (active_sequence_step_[c].flags & SeqStep::Trig) &&
            pulse < count && clock_event_end_[c] > clock_event_start_[c]) {
            const uint64_t due = clock_event_start_[c] +
                (clock_event_end_[c] - clock_event_start_[c]) * pulse / count;
            if (due < next) next = due;
        }
    }
    return next;
}
void Engine::fire_clock_retrigs() {
    if (!sequence_clock_active_) return;
    for (int c = 0; c < NUM_CHAINS; ++c) {
        if (!active_sequence_pass_[c] || !(active_sequence_step_[c].flags & SeqStep::Trig)) continue;
        const uint8_t count = active_sequence_step_[c].retrig_count();
        while (clock_retrig_next_[c] < count && clock_event_end_[c] > clock_event_start_[c]) {
            const uint64_t due = clock_event_start_[c] +
                (clock_event_end_[c] - clock_event_start_[c]) * clock_retrig_next_[c] / count;
            if (due > clock_phase_) break;
            ++clock_retrig_next_[c]; // consume before trigger: never resurrect a pulse.
            trigger_sequence_chain(c);
        }
    }
}
void Engine::dispatch_sequence_phase() {
    if (!sequence_clock_active_) return;
    fire_clock_retrigs();
    // Two queue slots per chain bound draining; scheduling can add at most one
    // already-due initial event. No event snapshots or random choices recreated.
    bool again;
    do {
        again = false;
        for (int c = 0; c < NUM_CHAINS; ++c) {
            PendingSequenceEvent* due = nullptr;
            PendingSequenceEvent* next = nullptr;
            for (auto& event : pending_sequence_events_[c]) if (event.valid) {
                if (!due || event.clock_phase < due->clock_phase ||
                    (event.clock_phase == due->clock_phase && event.tick < due->tick)) due = &event;
            }
            if (!due || due->clock_phase > clock_phase_) continue;
            for (auto& event : pending_sequence_events_[c])
                if (event.valid && &event != due && (!next || event.clock_phase < next->clock_phase)) next = &event;
            clock_event_start_[c] = clock_phase_;
            clock_event_end_[c] = next ? next->clock_phase : clock_phase_ + CLOCK_STEP;
            clock_retrig_next_[c] = 1;
            sequence_event_span_[c] = (uint32_t)(clock_frame_for_phase(clock_event_end_[c]) - transport_frame);
            if (!sequence_event_span_[c]) sequence_event_span_[c] = 1;
            fire_sequence_event(c, due->tick, due->nominal_frame);
            due->valid = false;
            again = true;
        }
        advance_sequence_clock();
    } while (again);
}
void Engine::advance_clock_transport(int frames) {
    if (!transport_playing || frames <= 0) return;
    if (sequencer_enabled && !sequence_clock_active_) initialize_sequence_clock(false);
    uint32_t remaining = (uint32_t)frames;
    while (remaining) {
        dispatch_sequence_phase();
        if (pending_recorder_command_.valid && clock_recorder_phase_ <= clock_phase_)
            fire_pending_rec((uint8_t)(1u << pending_recorder_command_.chain));
        const uint64_t boundary = (clock_phase_ / CLOCK_STEP + 1) * CLOCK_STEP;
        const uint64_t deadline = clock_next_deadline_phase();
        uint32_t n = remaining;
        if (deadline > clock_phase_) {
            const uint64_t distance = (deadline - clock_phase_ + clock_increment_ - 1) / clock_increment_;
            if (distance < n) n = (uint32_t)distance;
        }
        clock_phase_ += (uint64_t)clock_increment_ * n;
        transport_frame += n;
        remaining -= n;
        beat_count = (uint32_t)(clock_phase_ >> 32);
        frame_in_beat = (uint32_t)(((clock_phase_ & 0xffffffffu) * frames_per_beat()) >> 32);
        if (clock_phase_ >= boundary) {
            const bool beat = (boundary & 0xffffffffu) == 0;
            const bool bar = beat && beat_in_bar() == 0;
            if (launch_quantize == LaunchQuantize::Sixteenth ||
                (beat && launch_quantize == LaunchQuantize::Beat) ||
                (bar && launch_quantize == LaunchQuantize::Bar)) {
                fire_pending(0x0f);
                fire_pending_rec(0x0f);
            }
            if (pending_pattern < NUM_PATTERNS &&
                (pattern_launch_quantize == LaunchQuantize::Sixteenth ||
                 (beat && pattern_launch_quantize == LaunchQuantize::Beat) ||
                 (bar && pattern_launch_quantize == LaunchQuantize::Bar))) activate_pattern(pending_pattern, true);
            if (bar) advance_pattern_chain_at_bar();
        }
        if (pending_recorder_command_.valid && clock_recorder_phase_ <= clock_phase_)
            fire_pending_rec((uint8_t)(1u << pending_recorder_command_.chain));
        dispatch_sequence_phase();
    }
}
void Engine::set_bpm(int value) {
    if (value < 20) value = 20;
    if (value > 300) value = 300;
    if (clock_runtime_) { bpm = value; return; }
    // Retain remaining musical duration when the legacy scheduler is retimed.
    const uint32_t old_gate_beat = frames_per_beat();
    const uint32_t new_gate_beat = (uint32_t)(((uint64_t)ENGINE_SR * 60u + (uint32_t)value / 2u) / (uint32_t)value);
    if (bpm != value) for (auto& chain : chains) for (Unit* unit : chain.units)
        if (unit && unit->kind == UnitKind::SamplePlayer) {
            auto* play = static_cast<PlayUnit*>(unit);
            if (play->gate_deadline_ != UINT64_MAX && !play->gate_phase_ && play->gate_deadline_ > transport_frame)
                play->gate_deadline_ = transport_frame +
                    ((play->gate_deadline_ - transport_frame) * new_gate_beat + old_gate_beat - 1u) / old_gate_beat;
        }
    if (bpm != value) for (auto& chain : chains) for (Unit* unit : chain.units)
        if (unit && unit->kind == UnitKind::Grain) {
            auto* gran = static_cast<GrainUnit*>(unit);
            if (gran->gate_deadline_ != UINT64_MAX && !gran->gate_phase_ && gran->gate_deadline_ > transport_frame)
                gran->gate_deadline_ = transport_frame +
                    ((gran->gate_deadline_ - transport_frame) * new_gate_beat + old_gate_beat - 1u) / old_gate_beat;
        }
    if(bpm!=value)for(auto& chain:chains)for(Unit* unit:chain.units)if(unit && unit->kind==UnitKind::Scratch) {
        auto* scratch=static_cast<ScratchUnit*>(unit);
        if(scratch->gate_deadline_!=UINT64_MAX && !scratch->gate_phase_ && scratch->gate_deadline_>transport_frame)
            scratch->gate_deadline_=transport_frame+((scratch->gate_deadline_-transport_frame)*new_gate_beat+old_gate_beat-1)/old_gate_beat;
    }
    if(bpm!=value)for(auto& chain:chains)for(Unit* unit:chain.units)if(unit && unit->kind==UnitKind::Paul) {
        auto* p=static_cast<PaulUnit*>(unit);
        if(p->gate_deadline_!=UINT64_MAX && !p->gate_phase_ && p->gate_deadline_>transport_frame)
            p->gate_deadline_=transport_frame+((p->gate_deadline_-transport_frame)*new_gate_beat+old_gate_beat-1)/old_gate_beat;
    }
    if (bpm != value) { cancel_param_temp(); cancel_sequence_audition(); }
    if (bpm != value && sequence_clock_active_) clear_sequence_runtime();
    bpm = value;
    const uint32_t fpb = frames_per_beat();
    if (frame_in_beat >= fpb) frame_in_beat %= fpb;
    for (int i = 0; i < NUM_SIGILS; ++i)
        if (sigil_sync[i]) refresh_sigil_rate(i);
    for (auto& chain : chains)
        for (auto* unit : chain.units) {
            if (unit && unit->kind == UnitKind::Stranular)
                static_cast<StranularUnit*>(unit)->set_transport_bpm(bpm);
            else if (unit && unit->kind == UnitKind::Grain)
                static_cast<GrainUnit*>(unit)->set_transport_bpm(bpm);
            else if (unit && unit->kind == UnitKind::Vca)
                static_cast<VcaUnit*>(unit)->set_clock_bpm_q16(effective_bpm_q16());
        }
}

void Engine::transport_play() {
    stop_bounce_preview();
    if (!transport_playing) for (int i = 0; i < NUM_SIGILS; ++i)
        if (sigil_config[i].restart == SigilRestart::Transport && sigils[i].mode != SigilMode::Follow) reset_sigil(i);
    cancel_param_temp();
    cancel_sequence_audition();
    if (!transport_playing) cancel_rot_auditions();
    transport_playing = true;
}
void Engine::transport_stop() {
    cancel_rot_auditions();
    cancel_relic_gesture();
    stop_bounce(BounceReason::Stopped);
    release_play_gates();
    transition_fx_.stop();
    clear_sequence_runtime(true, true);
    transport_playing = false;
    for (auto& chain : chains) for (Unit* u : chain.units)
        if (u && u->kind==UnitKind::Arp) static_cast<ArpUnit*>(u)->reset();
    update_arps(); // close only owned source gates; preserve downstream tails
    for (auto& chain : chains) for (Unit* u : chain.units)
        if (u && u->kind == UnitKind::Fract) static_cast<FractUnit*>(u)->stop_transport();
        else if (u && u->kind == UnitKind::Glitch) static_cast<GlitchUnit*>(u)->stop_transport();
        else if (u && u->kind == UnitKind::Vca) static_cast<VcaUnit*>(u)->stop_cycle();
        else if (u && u->kind == UnitKind::Rule) static_cast<RuleUnit*>(u)->stop();
        else if (u && u->kind == UnitKind::Rot) static_cast<RotUnit*>(u)->stop();
    stop_pattern_chain();
    sequencer_fill_active = false;
    pending_triggers = pending_rec_start = pending_rec_stop = 0;
    pending_recorder_command_ = {};
    if (pending_pattern < NUM_PATTERNS) activate_pattern(pending_pattern);
}
void Engine::transport_reset() {
    cancel_relic_gesture();relic_epoch_=0;
    // Reset/load must observe LOW before another action edge, even if a sigil
    // restarts high. This is independent of the unit's DSP or undo state.
    for (auto& chain : chains) for (Unit* unit : chain.units)
        if (unit) unit->module_random_.armed = false;
    stop_bounce_preview();
    stop_bounce(BounceReason::Reset);
    if (bounce_.view.slot < 0) ++bounce_.view.generation; // invalidate staged arm across reset/load
    release_play_gates();
    for (int chain = 0; chain < NUM_CHAINS; ++chain) clear_grain_launch_requests(chain);
    transition_fx_.reset();
    clear_pattern_transition();
    clear_sequence_runtime(true, true);
    clear_arp_runtime();
    env_value_ = 0;
    for (auto& chain : chains) for (Unit* u : chain.units)
        if (u && u->kind == UnitKind::Fract) {
            auto* fract = static_cast<FractUnit*>(u);
            fract->reset(); fract->stop_transport(); // invalidate PCM without clearing 128KiB
        } else if (u && u->kind == UnitKind::Glitch) static_cast<GlitchUnit*>(u)->reset();
        else if (u && u->kind == UnitKind::Inst) u->reset(); // reset discards old notes; ordinary stop keeps finite tails
        else if (u && u->kind == UnitKind::Vca && static_cast<VcaUnit*>(u)->modern()) u->reset();
        else if (u && (u->kind == UnitKind::Rule || u->kind == UnitKind::Rot)) u->reset();
    transport_frame = 0;
    beat_count = 0;
    frame_in_beat = 0;
    sequence_tick = 0;
    pending_pattern = 0xff;
    stop_pattern_chain();
    pattern_chain_position = 0;
    sequencer_fill_active = false;
    sequence_rng_ = 0x53455131u;
    pending_triggers = 0;
    pending_rec_start = pending_rec_stop = 0;
    pending_recorder_command_ = {};
    for (auto& s : sigils) { s.trigger(); s.value = 0; }
    reset_clock_runtime();
    for (int i = 0; i < NUM_SIGILS; ++i)
        if (!sigil_config[i].legacy) reset_sigil(i);
}

uint32_t Engine::frames_per_beat() const {
    if (clock_runtime_)
        return (uint32_t)(((uint64_t)ENGINE_SR * 60u * 65536u + (uint32_t)clock_applied_[0] / 2u) / (uint32_t)clock_applied_[0]);
    return (uint32_t)(((uint64_t)ENGINE_SR * 60u + (uint32_t)bpm / 2u) / (uint32_t)bpm);
}

bool Engine::chain_trigger_pending(int chain) const {
    return chain >= 0 && chain < NUM_CHAINS && (pending_triggers & (1u << chain));
}

bool Engine::chain_record_pending(int chain) const {
    return chain >= 0 && chain < NUM_CHAINS &&
           ((pending_rec_start | pending_rec_stop) & (1u << chain));
}

bool Engine::recorder_record_pending(int chain, int slot) const {
    return pending_recorder_command_.valid && pending_recorder_command_.chain == chain &&
           pending_recorder_command_.slot == slot && recorder_command_current(pending_recorder_command_);
}

void Engine::cancel_pending_recorder() {
    if (pending_recorder_command_.valid && pending_recorder_command_.chain >= 0) {
        const uint8_t bit = (uint8_t)(1u << pending_recorder_command_.chain);
        pending_rec_start &= (uint8_t)~bit;
        pending_rec_stop &= (uint8_t)~bit;
    }
    pending_recorder_command_ = {};
}

void Engine::request_chain_trigger(int chain, LaunchQuantize quantize) {
    if (chain < 0 || chain >= NUM_CHAINS) return;
    launch_quantize = quantize;
    if (quantize == LaunchQuantize::Immediate || !transport_playing) {
        trigger_chain(chain);
        return;
    }
    pending_triggers |= (uint8_t)(1u << chain);
}

trackr::synth::SampleEditResult Engine::request_chain_record(int chain, bool start, LaunchQuantize quantize) {
    using trackr::synth::SampleEditResult;
    if (chain < 0 || chain >= NUM_CHAINS || quantize > LaunchQuantize::Bar) return SampleEditResult::InvalidSlot;
    if (pending_recorder_command_.valid) return SampleEditResult::Busy;
    if (start) {
        // This method may run at an audio-guarded controller boundary or from
        // host/offline code, but it must never allocate. UI stages workspaces via
        // plan_chain_record/stage_recorder/commit_chain_record first.
        for (int slot = 0; slot < UNITS_PER_CHAIN; ++slot) {
            auto* unit = chains[chain].units[slot];
            if (!unit || unit->kind != UnitKind::Recorder) continue;
            auto* rec = static_cast<RecUnit*>(unit);
            if (!rec->can_start_fresh()) return SampleEditResult::Busy;
            if (rec->sample_slot < 0 || rec->sample_slot >= trackr::synth::SAMPLE_BANK_SIZE) return SampleEditResult::InvalidSlot;
            const auto& sample = trackr::synth::SampleBank::instance().slot(rec->sample_slot);
            const uint8_t channels = chain_audio_channels(chain, slot);
            if (!channels || !chain_audio_channels(chain)) return SampleEditResult::InvalidSlot;
            if (!recorder_is_private(*rec) || !sample.has_storage() || sample.channels != channels ||
                sample.logical_frames != 0 || sample.num_frames() != rec->required_capacity_frames())
                return SampleEditResult::SharedSlot;
        }
    }
    // All preflights above precede transient ownership changes. An accepted
    // Start reserves recording globally, even when quantized/on another chain.
    if (start) {
        bool recorder = false;
        for (const Unit* u : chains[chain].units)
            if (u && u->kind == UnitKind::Recorder) recorder = true;
        if (recorder) cancel_rot_auditions();
    }
    cancel_param_temp();
    cancel_scratch_gesture();
    launch_quantize = quantize;
    auto apply = [this, chain, start]() {
        for (auto* u : chains[chain].units) {
            if (!u || u->kind != UnitKind::Recorder) continue;
            auto* rec = static_cast<RecUnit*>(u);
            if (start) rec->trigger(); else rec->stop();
        }
    };
    if (quantize == LaunchQuantize::Immediate || !transport_playing) {
        apply();
        return SampleEditResult::Ok;
    }
    const uint8_t bit = (uint8_t)(1u << chain);
    if (start) { pending_rec_start |= bit; pending_rec_stop &= (uint8_t)~bit; }
    else       { pending_rec_stop |= bit; pending_rec_start &= (uint8_t)~bit; }
    for (int slot = 0; slot < UNITS_PER_CHAIN; ++slot) {
        auto& token = pending_chain_recorders_[chain][slot]; token = {};
        Unit* u = chains[chain].units[slot];
        if (!u || u->kind != UnitKind::Recorder) continue;
        auto* rec = static_cast<RecUnit*>(u);
        if (rec->sample_slot < 0 || rec->sample_slot >= trackr::synth::SAMPLE_BANK_SIZE) continue;
        const auto& sample = trackr::synth::SampleBank::instance().slot(rec->sample_slot);
        token = {rec, rec->take_generation(), trackr::synth::SampleBank::instance().generation(rec->sample_slot),
            rec->session_token(), rec->state_token(), 0, (int8_t)chain, (int8_t)slot, (int8_t)rec->sample_slot,
            start ? RecorderCommand::Start : RecorderCommand::Stop, true,
            sample.data.data(), sample.data.size(), sample.channels};
    }
    return SampleEditResult::Ok;
}

trackr::synth::SampleEditResult Engine::request_recorder_record(
        int chain, int slot, bool start, LaunchQuantize quantize) {
    return request_recorder_command(chain, slot, start ? RecorderCommand::Start : RecorderCommand::Stop, quantize);
}

RecorderCommand Engine::recorder_pending_command(int chain, int slot) const {
    return recorder_record_pending(chain, slot) ? pending_recorder_command_.command : RecorderCommand::None;
}

bool Engine::recording_active_or_pending() const {
    if (pending_rec_start || pending_rec_stop || pending_recorder_command_.valid) return true;
    for (const auto& chain : chains) for (const Unit* u : chain.units)
        if (u && u->kind == UnitKind::Recorder && static_cast<const RecUnit*>(u)->writer_open()) return true;
    return false;
}

void Engine::interrupt_recorders() {
    cancel_relic_gesture();
    cancel_pending_recorder();
    pending_rec_start = pending_rec_stop = 0;
    // A queued whole-chain launch must not re-open a take after wake/input loss.
    pending_triggers = 0;
    for (auto& chain : chains) for (Unit* u : chain.units)
        if (u && u->kind == UnitKind::Recorder) static_cast<RecUnit*>(u)->stop();
}

trackr::synth::SampleEditResult Engine::validate_recorder_command(int chain, int slot, RecorderCommand command) const {
    using trackr::synth::SampleEditResult;
    if (chain < 0 || chain >= NUM_CHAINS || slot < 0 || slot >= UNITS_PER_CHAIN ||
        command == RecorderCommand::None || command > RecorderCommand::Resume) return SampleEditResult::InvalidSlot;
    const Unit* u = chains[chain].units[slot];
    if (!u || u->kind != UnitKind::Recorder) return SampleEditResult::InvalidSlot;
    const auto& rec = *static_cast<const RecUnit*>(u);
    if (rec.sample_slot < 0 || rec.sample_slot >= trackr::synth::SAMPLE_BANK_SIZE) return SampleEditResult::InvalidSlot;
    if (command == RecorderCommand::Stop) return SampleEditResult::Ok;
    if (command == RecorderCommand::Pause && !rec.is_recording()) return SampleEditResult::Busy;
    if (command == RecorderCommand::Resume && !rec.is_paused()) return SampleEditResult::Busy;
    if (command == RecorderCommand::Start && !rec.can_start_fresh()) return SampleEditResult::Busy;
    if (!recorder_is_private(rec)) return SampleEditResult::SharedSlot;
    const uint8_t channels = chain_audio_channels(chain, slot);
    if (!channels || !chain_audio_channels(chain)) return SampleEditResult::InvalidSlot;
    if (command != RecorderCommand::Start)
        return rec.writer_storage_matches() && rec.writer_channels_ == channels ? SampleEditResult::Ok : SampleEditResult::SharedSlot;
    const auto& sample = trackr::synth::SampleBank::instance().slot(rec.sample_slot);
    if (!sample.has_storage() || sample.channels != channels || sample.logical_frames != 0 ||
        sample.num_frames() != rec.required_capacity_frames() || !rec.target_frames()) return SampleEditResult::SharedSlot;
    return SampleEditResult::Ok;
}

trackr::synth::SampleEditResult Engine::request_recorder_command(
        int chain, int slot, RecorderCommand command, LaunchQuantize quantize) {
    using trackr::synth::SampleEditResult;
    if (quantize > LaunchQuantize::Bar) return SampleEditResult::InvalidSlot;
    if (pending_recorder_command_.valid && !recorder_command_current(pending_recorder_command_)) cancel_pending_recorder();
    const auto result = validate_recorder_command(chain, slot, command);
    if (result != SampleEditResult::Ok) return result;
    if (pending_recorder_command_.valid) {
        // Stop means cancel a not-yet-started take, never start then stop it.
        const auto& pending = pending_recorder_command_;
        const auto* rec = static_cast<const RecUnit*>(chains[chain].units[slot]);
        if (pending.chain == chain && pending.slot == slot && pending.rec == rec &&
            pending.session_token == rec->session_token() && pending.state_token == rec->state_token() &&
            command == RecorderCommand::Stop && pending.command == RecorderCommand::Start) {
            cancel_pending_recorder();
            return SampleEditResult::Ok;
        }
        return SampleEditResult::Busy; // no repeat or stealing another focused owner
    }
    if (pending_rec_start || pending_rec_stop) return SampleEditResult::Busy;
    accept_recorder_command(chain, slot, command, quantize);
    return SampleEditResult::Ok;
}

void Engine::accept_recorder_command(int chain, int slot, RecorderCommand command, LaunchQuantize quantize) {
    auto* rec = static_cast<RecUnit*>(chains[chain].units[slot]);
    if (command == RecorderCommand::Start || command == RecorderCommand::Resume)
        cancel_rot_auditions();
    cancel_param_temp();
    cancel_scratch_gesture();
    launch_quantize = quantize;
    if (quantize == LaunchQuantize::Immediate || !transport_playing) {
        switch (command) {
            case RecorderCommand::Start: rec->trigger(); break;
            case RecorderCommand::Stop: rec->stop(); break;
            case RecorderCommand::Pause: rec->pause(); break;
            case RecorderCommand::Resume: rec->resume(); break;
            default: break;
        }
        return;
    }
    uint64_t deadline = transport_frame;
    const uint32_t fpb = frames_per_beat();
    if (quantize == LaunchQuantize::Sixteenth) {
        const uint32_t division = fpb > 3 ? fpb / 4 : 1;
        uint32_t remainder = frame_in_beat % division;
        deadline += remainder ? division - remainder : division;
    } else if (quantize == LaunchQuantize::Beat) {
        deadline += frame_in_beat ? fpb - frame_in_beat : fpb;
    } else if (quantize == LaunchQuantize::Bar) {
        const uint32_t beats_left = beats_per_bar - beat_in_bar();
        deadline += (uint64_t)(beats_left ? beats_left : beats_per_bar) * fpb - frame_in_beat;
    }
    pending_recorder_command_ = {rec, rec->take_generation(),
        trackr::synth::SampleBank::instance().generation(rec->sample_slot),
        rec->session_token(), rec->state_token(), deadline, (int8_t)chain,
        (int8_t)slot, (int8_t)rec->sample_slot, command, true};
    const auto& sample = trackr::synth::SampleBank::instance().slot(rec->sample_slot);
    pending_recorder_command_.data = sample.data.data();
    pending_recorder_command_.elements = sample.data.size();
    pending_recorder_command_.channels = sample.channels;
    if (clock_runtime_) {
        clock_recorder_phase_ = clock_next_boundary(quantize);
        pending_recorder_command_.deadline = clock_frame_for_phase(clock_recorder_phase_);
    }
    const uint8_t bit = (uint8_t)(1u << chain);
    if (command == RecorderCommand::Start || command == RecorderCommand::Resume) pending_rec_start |= bit;
    else pending_rec_stop |= bit;
}

bool Engine::recorder_command_current(const PendingRecorderCommand& command) const {
    if (!command.valid || command.chain < 0 || command.slot < 0 || command.sample_slot < 0 ||
        command.chain >= NUM_CHAINS || command.slot >= UNITS_PER_CHAIN || command.sample_slot >= trackr::synth::SAMPLE_BANK_SIZE ||
        chains[(int)command.chain].units[(int)command.slot] != command.rec || !command.rec) return false;
    if (command.rec->kind != UnitKind::Recorder || command.rec->session_token() != command.session_token ||
        command.rec->state_token() != command.state_token || command.rec->take_generation() != command.take_generation ||
        command.rec->sample_slot != command.sample_slot) return false;
    const auto& sample = trackr::synth::SampleBank::instance().slot(command.sample_slot);
    return trackr::synth::SampleBank::instance().generation(command.sample_slot) == command.bank_generation &&
        sample.data.data() == command.data && sample.data.size() == command.elements && sample.channels == command.channels &&
        (command.command == RecorderCommand::Stop ||
         (chain_audio_channels(command.chain) && chain_audio_channels(command.chain, command.slot) == command.channels));
}

void Engine::fire_pending_rec(uint8_t mask) {
    // A focused token owns its original musical endpoint. A later chain launch
    // may change launch_quantize, but must neither fire this token early nor
    // postpone it until that unrelated grid. Legacy untouched patches stay exact.
    if (clock_runtime_ && pending_recorder_command_.valid && clock_recorder_phase_ > clock_phase_) return;
    if (pending_recorder_command_.valid) {
        const PendingRecorderCommand command = pending_recorder_command_;
        const uint8_t bit = command.chain >= 0 ? (uint8_t)(1u << command.chain) : 0;
        if ((mask & bit) != 0) {
            const bool current = recorder_command_current(command);
            cancel_pending_recorder();
            if (current && validate_recorder_command(command.chain, command.slot, command.command) == trackr::synth::SampleEditResult::Ok) {
                // Preserve the global grid selected since this command was queued.
                const auto grid = launch_quantize;
                accept_recorder_command(command.chain, command.slot, command.command, LaunchQuantize::Immediate);
                launch_quantize = grid;
            }
        }
        return;
    }
    const uint8_t starts = pending_rec_start & mask;
    const uint8_t stops = pending_rec_stop & mask;
    for (int chain = 0; chain < NUM_CHAINS; ++chain) {
        const uint8_t bit = (uint8_t)(1u << chain);
        if (!(starts & bit) && !(stops & bit)) continue;
        for (int slot = 0; slot < UNITS_PER_CHAIN; ++slot) {
            auto& token = pending_chain_recorders_[chain][slot];
            if (recorder_command_current(token) &&
                validate_recorder_command(chain, slot, token.command) == trackr::synth::SampleEditResult::Ok) {
                if (starts & bit) { cancel_rot_auditions(); token.rec->trigger(); }
                if (stops & bit) token.rec->stop();
            }
            token = {};
        }
    }
    pending_rec_start &= (uint8_t)~starts;
    pending_rec_stop &= (uint8_t)~stops;
}

void Engine::fire_pending(uint8_t mask) {
    const uint8_t fire = pending_triggers & mask;
    for (int chain = 0; chain < NUM_CHAINS; ++chain)
        if (fire & (1u << chain)) trigger_chain(chain);
    pending_triggers &= (uint8_t)~fire;
}

void Engine::set_sequencer_enabled(bool enabled) {
    if (enabled != sequencer_enabled) { cancel_param_temp(); cancel_sequence_audition(); }
    if (!enabled) {
        for (auto& hit : sequence_hit_) hit = {};
        release_sequence_locks();
        for (auto& pass : active_sequence_pass_) pass = false;
    }
    sequencer_enabled = enabled;
}

void Engine::set_swing(int percent) {
    if (percent < 50) percent = 50;
    if (percent > 75) percent = 75;
    if (!clock_runtime_ && swing_percent != percent && sequence_clock_active_) clear_sequence_runtime();
    swing_percent = (uint8_t)percent;
}

void Engine::activate_pattern(int index, bool start_now) {
    if (index < 0 || index >= NUM_PATTERNS) return;
    cancel_relic_gesture();
    clear_pattern_transition();
    clear_sequence_runtime(true, !transport_playing);
    if (active_pattern < NUM_PATTERNS) pattern_banks[active_pattern] = pattern;
    active_pattern = (uint8_t)index;
    pattern_chain_current_pattern = active_pattern;
    pattern = pattern_banks[index];
    pending_pattern = 0xff;
    sequence_tick = 0;
    sequence_rng_ = 0x53455131u;
    if (start_now && sequencer_enabled && transport_playing) initialize_sequence_clock(true);
}

void Engine::request_pattern(int index, bool quantized) {
    if (index < 0 || index >= NUM_PATTERNS) return;
    cancel_relic_gesture();
    // An explicit launch, including the current bank, ends the override and
    // must not leave transition events queued under a now-single-bank label.
    if (pattern_transition_.active) clear_sequence_runtime();
    clear_pattern_transition();
    take_over_pattern_chain();
    if (index == active_pattern) { pending_pattern = 0xff; return; }
    if (!transport_playing || !quantized || pattern_launch_quantize == LaunchQuantize::Immediate)
        activate_pattern(index, transport_playing);
    else
        pending_pattern = (uint8_t)index;
}

bool Engine::set_pattern_transition(int source, int destination, q15 position) {
    if (source < 0 || source >= NUM_PATTERNS || destination < 0 || destination >= NUM_PATTERNS)
        return false;
    if (pending_pattern < NUM_PATTERNS || pattern_chain_playing || pattern_chain_start_pending_)
        return false;
    if (position < 0) position = 0;
    if (pattern_transition_.active) {
        if (source != pattern_transition_.source &&
            (pattern_transition_.position != 32767 || position != 32767)) return false;
        if (destination != pattern_transition_.destination &&
            (pattern_transition_.position != 0 || position != 0)) return false;
    }
    pattern_transition_ = {true, (uint8_t)source, (uint8_t)destination, position};
    return true;
}

void Engine::clear_pattern_transition() { pattern_transition_ = {}; }

bool Engine::finish_pattern_transition() {
    const PatternTransitionState& state = pattern_transition_;
    if (!state.active || pending_pattern < NUM_PATTERNS || pattern_chain_playing || pattern_chain_start_pending_)
        return false;
    if (state.source != state.destination && state.position != 0 && state.position != 32767)
        return false;
    const uint8_t bank = state.position == 32767 ? state.destination : state.source;
    if (active_pattern < NUM_PATTERNS) pattern_banks[active_pattern] = pattern;
    if (bank != active_pattern) pattern = pattern_banks[bank];
    active_pattern = bank;
    pattern_chain_current_pattern = active_pattern;
    clear_pattern_transition();
    return true;
}

int Engine::transition_bank_at_tick(int chain, uint64_t tick) const {
    const PatternTransitionState& state = pattern_transition_;
    if (!state.active) return -1; // Preserve active alias and old condition RNG.
    if (!state.position || state.source == state.destination) return state.source;
    if (state.position == 32767) return state.destination;
    // Stateless avalanche; no shared RNG draw or decision changes on rerender.
    // Native common tick preserves each endpoint's own length/direction/cycle.
    uint32_t hash = (uint32_t)tick ^ (uint32_t)(tick >> 32) ^
                    (0x9e3779b9u * (uint32_t)(chain + 1));
    hash ^= hash >> 16; hash *= 0x7feb352du;
    hash ^= hash >> 15; hash *= 0x846ca68bu;
    hash ^= hash >> 16;
    return hash % 32767u < (uint32_t)state.position ? state.destination : state.source;
}

bool Engine::set_pattern_chain_step(int position, int pattern_index, int bars) {
    if (position < 0 || position >= PATTERN_CHAIN_STEPS ||
        pattern_index < 0 || pattern_index >= NUM_PATTERNS) return false;
    if (bars < 1) bars = 1;
    if (bars > 16) bars = 16;
    // Running, armed and explicitly queued entries use their latched tuple.
    pattern_chain[position] = {(uint8_t)pattern_index, (uint8_t)bars};
    return true;
}

bool Engine::insert_pattern_chain_step(int position, int pattern_index, int bars) {
    if (position < 0 || position > pattern_chain_length ||
        pattern_chain_length >= PATTERN_CHAIN_STEPS ||
        pattern_index < 0 || pattern_index >= NUM_PATTERNS) return false;
    if (bars < 1) bars = 1;
    if (bars > 16) bars = 16;
    const bool had_cursor = pattern_chain_length != 0;
    for (int i = pattern_chain_length; i > position; --i) pattern_chain[i] = pattern_chain[i - 1];
    pattern_chain[position] = {(uint8_t)pattern_index, (uint8_t)bars};
    ++pattern_chain_length;
    if (had_cursor && pattern_chain_position >= position) ++pattern_chain_position;
    if (pattern_chain_next_pending && pattern_chain_next_position_ != 0xff &&
        pattern_chain_next_position_ >= position) ++pattern_chain_next_position_;
    return true;
}

bool Engine::remove_pattern_chain_step(int position) {
    if (position < 0 || position >= pattern_chain_length) return false;
    if (((pattern_chain_playing || pattern_chain_paused) && position == pattern_chain_position) ||
        (pattern_chain_next_pending && position == pattern_chain_next_position_)) return false;
    for (int i = position + 1; i < pattern_chain_length; ++i) pattern_chain[i - 1] = pattern_chain[i];
    pattern_chain[pattern_chain_length - 1] = {};
    --pattern_chain_length;
    if (pattern_chain_position > position) --pattern_chain_position;
    else if (pattern_chain_position >= pattern_chain_length)
        pattern_chain_position = pattern_chain_length ? (uint8_t)(pattern_chain_length - 1) : 0;
    if (pattern_chain_next_pending && pattern_chain_next_position_ != 0xff &&
        pattern_chain_next_position_ > position) --pattern_chain_next_position_;
    return true;
}

bool Engine::move_pattern_chain_step(int from, int to) {
    if (from < 0 || from >= pattern_chain_length || to < 0 || to >= pattern_chain_length) return false;
    if (from == to) return true;
    const PatternChainStep moving = pattern_chain[from];
    if (from < to) for (int i = from; i < to; ++i) pattern_chain[i] = pattern_chain[i + 1];
    else for (int i = from; i > to; --i) pattern_chain[i] = pattern_chain[i - 1];
    pattern_chain[to] = moving;
    const auto remap = [from, to](uint8_t index) -> uint8_t {
        if (index == from) return (uint8_t)to;
        if (from < to && index > from && index <= to) return (uint8_t)(index - 1);
        if (from > to && index >= to && index < from) return (uint8_t)(index + 1);
        return index;
    };
    pattern_chain_position = remap(pattern_chain_position);
    if (pattern_chain_next_pending && pattern_chain_next_position_ != 0xff)
        pattern_chain_next_position_ = remap(pattern_chain_next_position_);
    return true;
}

void Engine::clear_pattern_chain() {
    stop_pattern_chain();
    for (auto& entry : pattern_chain) entry = {};
    pattern_chain_length = 0;
    pattern_chain_position = 0;
}

bool Engine::set_pattern_chain_length(int length) {
    if (length < 0) length = 0;
    if (length > PATTERN_CHAIN_STEPS) length = PATTERN_CHAIN_STEPS;
    if (((pattern_chain_playing || pattern_chain_paused) && length <= pattern_chain_position) ||
        (pattern_chain_next_pending && pattern_chain_next_position_ != 0xff &&
         length <= pattern_chain_next_position_)) return false;
    pattern_chain_length = (uint8_t)length;
    if (!pattern_chain_length) {
        stop_pattern_chain();
        pattern_chain_position = 0;
    } else if (pattern_chain_position >= pattern_chain_length) {
        pattern_chain_position = (uint8_t)(pattern_chain_length - 1);
    }
    return true;
}

bool Engine::arm_pattern_chain(int position, bool strict_next_bar) {
    if (position < 0 || position >= pattern_chain_length) return false;
    const PatternChainStep entry = pattern_chain[position];
    if (entry.pattern >= NUM_PATTERNS || entry.bars < 1 || entry.bars > 16) return false;
    pattern_chain_position = (uint8_t)position;
    pattern_chain_start_step_ = entry;
    pattern_chain_playing = true;
    pattern_chain_paused = false;
    pattern_chain_held = false;
    pattern_chain_next_pending = false;
    pattern_chain_next_position_ = 0xff;
    pattern_chain_start_pending_ = transport_playing &&
        (strict_next_bar || frame_in_beat != 0 || beat_in_bar() != 0);
    pattern_chain_current_bars = pattern_chain_bars_remaining = entry.bars;
    pattern_chain_current_pattern = active_pattern;
    if (!pattern_chain_start_pending_) begin_pattern_chain_step(entry);
    return true;
}

bool Engine::start_pattern_chain(bool restart) {
    const int position = restart || pattern_chain_position >= pattern_chain_length ? 0 : pattern_chain_position;
    if (position >= pattern_chain_length) return false;
    const PatternChainStep& entry = pattern_chain[position];
    if (entry.pattern >= NUM_PATTERNS || entry.bars < 1 || entry.bars > 16) return false;
    if (pattern_transition_.active) clear_sequence_runtime();
    clear_pattern_transition();
    pending_pattern = 0xff;
    return arm_pattern_chain(position, false);
}

bool Engine::resume_pattern_chain(int position) {
    // Even endpoint blends must be explicitly kept/cleared first. Manual launch
    // must resolve before auto can claim ownership, never silently disappear.
    if (pattern_transition_.active || pending_pattern < NUM_PATTERNS) return false;
    return arm_pattern_chain(position, true);
}

bool Engine::take_over_pattern_chain() {
    const bool owned = pattern_chain_playing || pattern_chain_start_pending_;
    if (owned) pattern_chain_paused = true;
    pattern_chain_playing = false;
    pattern_chain_start_pending_ = false;
    pattern_chain_next_pending = false;
    pattern_chain_next_position_ = 0xff;
    return owned;
}

bool Engine::set_pattern_chain_hold(bool held) {
    if (!pattern_chain_playing) return false;
    pattern_chain_held = held;
    return true;
}

bool Engine::next_pattern_chain() {
    // No ambiguous skip of a card which has not even begun. Repeated next
    // gestures coalesce, retaining the first accepted next identity/tuple.
    if (!pattern_chain_playing || pattern_chain_start_pending_ || !pattern_chain_length) return false;
    if (pattern_chain_next_pending) return true;
    int next = pattern_chain_position + 1;
    if (next >= pattern_chain_length) next = pattern_chain_loop ? 0 : 0xff;
    if (next != 0xff) {
        const PatternChainStep entry = pattern_chain[next];
        if (entry.pattern >= NUM_PATTERNS || entry.bars < 1 || entry.bars > 16) return false;
        pattern_chain_next_step_ = entry;
    }
    pattern_chain_next_position_ = (uint8_t)next;
    pattern_chain_next_pending = true;
    return true;
}

void Engine::stop_pattern_chain() {
    pattern_chain_playing = false;
    pattern_chain_paused = false;
    pattern_chain_held = false;
    pattern_chain_start_pending_ = false;
    pattern_chain_next_pending = false;
    pattern_chain_next_position_ = 0xff;
    pattern_chain_current_bars = pattern_chain_bars_remaining = 0;
    pattern_chain_current_pattern = active_pattern;
    pattern_chain_start_step_ = pattern_chain_next_step_ = {};
}

void Engine::begin_pattern_chain_step(const PatternChainStep& entry) {
    pattern_chain_current_bars = pattern_chain_bars_remaining = entry.bars;
    activate_pattern(entry.pattern, transport_playing);
}

void Engine::advance_pattern_chain_at_bar() {
    if (!pattern_chain_playing || !pattern_chain_length) return;
    if (pattern_chain_start_pending_) {
        pattern_chain_start_pending_ = false;
        begin_pattern_chain_step(pattern_chain_start_step_);
        return;
    }
    if (pattern_chain_next_pending) {
        const uint8_t next = pattern_chain_next_position_;
        pattern_chain_next_pending = false;
        pattern_chain_next_position_ = 0xff;
        if (next == 0xff) { stop_pattern_chain(); return; }
        pattern_chain_position = next;
        pattern_chain_held = false;
        begin_pattern_chain_step(pattern_chain_next_step_);
        return;
    }
    if (pattern_chain_held) return;
    if (pattern_chain_bars_remaining > 1) {
        --pattern_chain_bars_remaining;
        return;
    }
    uint8_t next = (uint8_t)(pattern_chain_position + 1);
    if (next >= pattern_chain_length) {
        if (!pattern_chain_loop) { stop_pattern_chain(); return; }
        next = 0;
    }
    const PatternChainStep entry = pattern_chain[next];
    if (entry.pattern >= NUM_PATTERNS || entry.bars < 1 || entry.bars > 16) {
        stop_pattern_chain();
        return;
    }
    pattern_chain_position = next;
    begin_pattern_chain_step(entry);
}

uint8_t Engine::sequence_step_at_tick(int chain, uint64_t tick, int bank) const {
    if (chain < 0 || chain >= NUM_CHAINS) return 0;
    const Pattern& selected = sequence_pattern(bank);
    uint8_t length = selected.length[chain];
    if (length < 1 || length > SEQ_STEPS) length = 16;
    SequenceDirection direction = selected.direction[chain];
    if (direction >= SequenceDirection::Count) direction = SequenceDirection::Forward;
    if (direction == SequenceDirection::Reverse)
        return (uint8_t)(length - 1u - (tick % length));
    if (direction == SequenceDirection::PingPong && length > 1) {
        const uint64_t period = (uint64_t)(length - 1u) * 2u;
        const uint64_t phase = tick % period;
        return (uint8_t)(phase < length ? phase : period - phase);
    }
    return (uint8_t)(tick % length);
}

uint64_t Engine::sequence_cycle_at_tick(int chain, uint64_t tick, int bank) const {
    if (chain < 0 || chain >= NUM_CHAINS) return 0;
    const Pattern& selected = sequence_pattern(bank);
    uint8_t length = selected.length[chain];
    if (length < 1 || length > SEQ_STEPS) length = 16;
    SequenceDirection direction = selected.direction[chain];
    if (direction >= SequenceDirection::Count) direction = SequenceDirection::Forward;
    const uint64_t period = direction == SequenceDirection::PingPong && length > 1
        ? (uint64_t)(length - 1u) * 2u : length;
    return tick / period;
}

uint8_t Engine::sequence_step(int chain) const {
    return sequence_step_at_tick(chain, sequence_tick);
}

void Engine::set_sequence_length(int chain, int length, int bank) {
    if (chain < 0 || chain >= NUM_CHAINS) return;
    if (length < 1) length = 1;
    if (length > SEQ_STEPS) length = SEQ_STEPS;
    Pattern& selected = sequence_pattern(bank);
    if (selected.length[chain] == length) return;
    selected.length[chain] = (uint8_t)length;
    // Only active edits invalidate live lookahead. Inactive banks are storage,
    // never a launch request or a reason to disturb the playing clock.
    if (&selected == &pattern && sequencer_enabled && transport_playing)
        initialize_sequence_clock(false);
}

void Engine::set_sequence_direction(int chain, SequenceDirection direction, int bank) {
    if (chain < 0 || chain >= NUM_CHAINS) return;
    if (direction >= SequenceDirection::Count) direction = SequenceDirection::Forward;
    Pattern& selected = sequence_pattern(bank);
    if (selected.direction[chain] == direction) return;
    selected.direction[chain] = direction;
    if (&selected == &pattern && sequencer_enabled && transport_playing)
        initialize_sequence_clock(false);
}

bool Engine::set_step_lock(int chain, int step, int slot, int param, q15 value) {
    if (chain < 0 || chain >= NUM_CHAINS || step < 0 || step >= SEQ_STEPS ||
        slot < 0 || slot >= UNITS_PER_CHAIN || param < 0 || param >= MAX_PARAMS) return false;
    SeqStep& seq = pattern.steps[chain][step];
    int count = seq.lock_count > SEQ_LOCKS ? SEQ_LOCKS : seq.lock_count;
    for (int i = 0; i < count; ++i) {
        if (seq.locks[i].slot == slot && seq.locks[i].param == param) {
            seq.locks[i].value = value;
            return true;
        }
    }
    if (count >= SEQ_LOCKS) return false;
    seq.locks[count] = {(uint8_t)slot, (uint8_t)param, value};
    seq.lock_count = (uint8_t)(count + 1);
    return true;
}

bool Engine::remove_step_lock(int chain, int step, int slot, int param, int bank) {
    if (chain < 0 || chain >= NUM_CHAINS || step < 0 || step >= SEQ_STEPS) return false;
    SeqStep& seq = sequence_pattern(bank).steps[chain][step];
    int count = seq.lock_count > SEQ_LOCKS ? SEQ_LOCKS : seq.lock_count;
    for (int i = 0; i < count; ++i) {
        if (seq.locks[i].slot != slot || seq.locks[i].param != param) continue;
        for (int n = i + 1; n < count; ++n) seq.locks[n - 1] = seq.locks[n];
        seq.locks[count - 1] = {};
        seq.lock_count = (uint8_t)(count - 1);
        return true;
    }
    return false;
}

void Engine::clear_step_locks(int chain, int step) {
    if (chain < 0 || chain >= NUM_CHAINS || step < 0 || step >= SEQ_STEPS) return;
    SeqStep& seq = pattern.steps[chain][step];
    seq.lock_count = 0;
    for (auto& lock : seq.locks) lock = {};
}

void Engine::set_step_microtiming(int chain, int step, int amount) {
    if (chain < 0 || chain >= NUM_CHAINS || step < 0 || step >= SEQ_STEPS) return;
    if (amount < -SEQ_MICROTIMING_MAX) amount = -SEQ_MICROTIMING_MAX;
    if (amount > SEQ_MICROTIMING_MAX) amount = SEQ_MICROTIMING_MAX;
    pattern.microtiming[chain][step] = (int8_t)amount;
}

int Engine::step_microtiming(int chain, int step) const {
    if (chain < 0 || chain >= NUM_CHAINS || step < 0 || step >= SEQ_STEPS) return 0;
    return pattern.microtiming[chain][step];
}

bool Engine::copy_step(int source_chain, int source_step, int target_chain, int target_step) {
    if (source_chain < 0 || source_chain >= NUM_CHAINS || target_chain < 0 || target_chain >= NUM_CHAINS ||
        source_step < 0 || source_step >= SEQ_STEPS || target_step < 0 || target_step >= SEQ_STEPS) return false;
    pattern.steps[target_chain][target_step] = pattern.steps[source_chain][source_step];
    pattern.microtiming[target_chain][target_step] = pattern.microtiming[source_chain][source_step];
    return true;
}

bool Engine::copy_track(int source_chain, int target_chain) {
    if (source_chain < 0 || source_chain >= NUM_CHAINS || target_chain < 0 || target_chain >= NUM_CHAINS) return false;
    pattern.length[target_chain] = pattern.length[source_chain];
    pattern.direction[target_chain] = pattern.direction[source_chain];
    std::memcpy(pattern.steps[target_chain], pattern.steps[source_chain], sizeof pattern.steps[target_chain]);
    std::memcpy(pattern.microtiming[target_chain], pattern.microtiming[source_chain], sizeof pattern.microtiming[target_chain]);
    return true;
}

bool Engine::rotate_track(int chain, int amount) {
    if (chain < 0 || chain >= NUM_CHAINS) return false;
    int length = pattern.length[chain];
    if (length < 1 || length > SEQ_STEPS) length = 16;
    amount %= length;
    if (amount < 0) amount += length;
    if (!amount) return true;
    // Rotate in-place by cycles so this UI-thread helper never needs a 1 KiB
    // temporary on the 3DS stack.
    int a = length, b = amount;
    while (b) { const int r = a % b; a = b; b = r; }
    const int cycles = a;
    for (int start = 0; start < cycles; ++start) {
        SeqStep saved_step = pattern.steps[chain][start];
        const int8_t saved_timing = pattern.microtiming[chain][start];
        int current = start;
        while (true) {
            int previous = current - amount;
            if (previous < 0) previous += length;
            if (previous == start) break;
            pattern.steps[chain][current] = pattern.steps[chain][previous];
            pattern.microtiming[chain][current] = pattern.microtiming[chain][previous];
            current = previous;
        }
        pattern.steps[chain][current] = saved_step;
        pattern.microtiming[chain][current] = saved_timing;
    }
    if (sequencer_enabled && transport_playing) initialize_sequence_clock(false);
    return true;
}

bool Engine::restore_track(int chain, const SeqStep (&steps)[SEQ_STEPS],
                           const int8_t (&microtiming)[SEQ_STEPS], int length,
                           SequenceDirection direction) {
    if (chain < 0 || chain >= NUM_CHAINS) return false;
    if (length < 1 || length > SEQ_STEPS) return false;
    if (direction >= SequenceDirection::Count) return false;
    std::memcpy(pattern.steps[chain], steps, sizeof pattern.steps[chain]);
    std::memcpy(pattern.microtiming[chain], microtiming,
                sizeof pattern.microtiming[chain]);
    pattern.length[chain] = (uint8_t)length;
    pattern.direction[chain] = direction;
    if (sequencer_enabled && transport_playing) initialize_sequence_clock(false);
    return true;
}

void Engine::clear_track(int chain) {
    if (chain < 0 || chain >= NUM_CHAINS) return;
    for (int step = 0; step < SEQ_STEPS; ++step) {
        pattern.steps[chain][step] = {};
        pattern.microtiming[chain][step] = 0;
    }
    if (sequencer_enabled && transport_playing) initialize_sequence_clock(false);
}

bool Engine::copy_pattern_bank(int source_bank, int target_bank) {
    if (source_bank < 0 || source_bank >= NUM_PATTERNS || target_bank < 0 || target_bank >= NUM_PATTERNS) return false;
    const Pattern& source = source_bank == active_pattern ? pattern : pattern_banks[source_bank];
    if (target_bank == active_pattern) {
        clear_sequence_runtime();
        pattern = source;
        pattern_banks[target_bank] = pattern;
        sequence_tick = 0;
        sequence_rng_ = 0x53455131u;
    } else {
        pattern_banks[target_bank] = source;
    }
    return true;
}

void Engine::clear_pattern_bank(int bank) {
    if (bank < 0 || bank >= NUM_PATTERNS) return;
    Pattern& target = bank == active_pattern ? pattern : pattern_banks[bank];
    if (bank == active_pattern) clear_sequence_runtime();
    for (int chain = 0; chain < NUM_CHAINS; ++chain) {
        target.length[chain] = 16;
        target.direction[chain] = SequenceDirection::Forward;
        for (int step = 0; step < SEQ_STEPS; ++step) {
            target.steps[chain][step] = {};
            target.microtiming[chain][step] = 0;
        }
    }
    pattern_banks[bank] = target;
    if (bank == active_pattern) {
        sequence_tick = 0;
        sequence_rng_ = 0x53455131u;
    }
}

void Engine::clear_step(int chain, int step, int bank) {
    if (chain < 0 || chain >= NUM_CHAINS || step < 0 || step >= SEQ_STEPS) return;
    Pattern& selected = sequence_pattern(bank);
    selected.steps[chain][step] = {};
    selected.microtiming[chain][step] = 0;
}

bool Engine::set_env_follow_source(int chain) {
    if (chain < 0 || chain >= NUM_CHAINS || chain == env_chain_) return false;
    env_chain_ = (uint8_t)chain;
    env_value_ = 0;
    return true;
}

bool Engine::set_env_follow_response(EnvResponse response) {
    if (response >= EnvResponse::Count || response == env_response_) return false;
    env_response_ = response;
    env_value_ = 0;
    return true;
}

bool Engine::param_temp_allowed(int chain, int slot, int param) const {
    if (chain < 0 || chain >= NUM_CHAINS || slot < 0 || slot >= UNITS_PER_CHAIN ||
        param < 0 || param >= PARAM_BANK_SIZE || chains[chain].mute) return false;
    const Unit* unit = chains[chain].units[slot];
    if (!unit || unit->kind == UnitKind::Empty || unit->kind == UnitKind::Recorder ||
        unit->bypass || param >= unit->num_params) return false;
    // Conservative global exclusion: a focused gesture must never enter a take,
    // including a bypassed recorder or a scheduled command on another chain.
    if (pending_rec_start || pending_rec_stop || pending_recorder_command_.valid) return false;
    for (const auto& c : chains) for (const Unit* u : c.units)
        if (u && u->kind == UnitKind::Recorder && static_cast<const RecUnit*>(u)->writer_open())
            return false;
    return true;
}

bool Engine::param_temp_active() const {
    if (temp_chain_ < 0 || temp_slot_ < 0 || temp_param_ < 0) return false;
    const Unit* unit = chains[(int)temp_chain_].units[(int)temp_slot_];
    return unit && temp_param_ < unit->num_params && unit->params[(int)temp_param_].temp_override;
}

q15 Engine::param_temp_value() const {
    return param_temp_active() ? chains[(int)temp_chain_].units[(int)temp_slot_]->params[(int)temp_param_].temp : 0;
}

void Engine::cancel_param_temp() {
    if (param_temp_active())
        chains[(int)temp_chain_].units[(int)temp_slot_]->params[(int)temp_param_].temp_override = false;
    temp_chain_ = temp_slot_ = temp_param_ = -1;
    // No resolve/control_tick here: the normal tick slews to the current live
    // performance/audition/step/base layer, without a stale saved-value restore.
}

bool Engine::begin_param_temp(int chain, int slot, int param) {
    if (scratch_gesture_.token && chain==scratch_gesture_.chain && slot==scratch_gesture_.slot &&
        (param==scratch_gesture_.x || param==scratch_gesture_.y)) return false;
    if (!param_temp_allowed(chain, slot, param)) { cancel_param_temp(); return false; }
    if (param_temp_active() && temp_chain_ == chain && temp_slot_ == slot && temp_param_ == param)
        return true; // Repeated begin must not reset an in-flight gesture.
    cancel_param_temp();
    Param& value = chains[chain].units[slot]->params[param];
    value.temp = value.control_source();
    value.temp_override = true;
    temp_chain_ = (int8_t)chain; temp_slot_ = (int8_t)slot; temp_param_ = (int8_t)param;
    return true;
}

bool Engine::set_param_temp(q15 value) {
    if (!param_temp_active() || !param_temp_allowed(temp_chain_, temp_slot_, temp_param_)) {
        cancel_param_temp(); return false;
    }
    Param& param = chains[(int)temp_chain_].units[(int)temp_slot_]->params[(int)temp_param_];
    const q15 low = param.bipolar ? -32767 : 0;
    param.temp = value < low ? low : value;
    return true;
}

#include "scratch_engine_impl.inc"

void Engine::capture_param_inspector(int chain, int slot, int param, ParamInspector& out) const {
    out = {};
    out.env_chain = env_chain_; out.env_response = env_response_; out.env_level = env_value_;
    if (chain < 0 || chain >= NUM_CHAINS || slot < 0 || slot >= UNITS_PER_CHAIN ||
        param < 0 || param >= PARAM_BANK_SIZE) return;
    const Unit* unit = chains[chain].units[slot];
    if (!unit || param >= unit->num_params) return;
    const Param& value = unit->params[param];
    out.valid = true;
    out.chain = (int8_t)chain; out.slot = (int8_t)slot; out.param = (int8_t)param;
    out.kind = (uint8_t)unit->kind;
    if (value.name) {
        for (unsigned i = 0; i + 1 < sizeof out.name && value.name[i]; ++i) out.name[i] = value.name[i];
    }
    out.base = value.base; out.control = unit->kind==UnitKind::Scratch?
        static_cast<const ScratchUnit*>(unit)->axis_control_source(param):value.control_source();
    out.effective = value.effective;
    out.source = value.src; out.depth = value.mod_depth; out.bipolar = value.bipolar;
    out.relic_source=value.relic_source;out.relic_port=value.relic_port;
    out.mod_enabled = value.mod_enabled;
    out.temp = value.temp_override;
    bool scratch_layer=false;
    if(unit->kind==UnitKind::Scratch && !value.temp_override && !value.performance_override) {
        const auto* s=static_cast<const ScratchUnit*>(unit);int x,y;s->axis_params(x,y);
        scratch_layer=s->latched() && (param==x || param==y);
    }
    out.layer = value.temp_override ? 4 : (value.performance_override || scratch_layer ? 3 :
                (value.audition_override ? 2 : (value.step_lock_override ? 1 : 0)));
    out.source_value = value.mod_enabled ? parameter_modulation_value(unit,param) : 0;
    q31 source_lo = 0, source_hi = 0;
    if (!value.relic_source && mod_source_sigil(value.src) >= 0 && sigil_config[mod_source_sigil(value.src)].enabled) {
        // Include signed depth and legacy INT16_MIN. This is a conservative rail.
        // Preserve DSP values and include that extra negative LSB in potential.
        source_lo = -32768; source_hi = 32767;
        const int index = mod_source_sigil(value.src);
        const auto& c = sigil_config[index]; const auto& s = sigils[index];
        if (!c.legacy && s.mode != SigilMode::RandomStep && s.mode != SigilMode::RandomSmooth) {
            const int32_t depth = s.depth < -32767 ? -32767 : s.depth;
            source_lo = c.unipolar ? 0 : -depth;
            source_hi = depth;
            if (source_lo > source_hi) { const q31 tmp=source_lo; source_lo=source_hi; source_hi=tmp; }
            if (c.shape == SigilShape::Drawn && !s.drawn) source_lo = source_hi = 0;
        }
    } else if (!value.relic_source && value.src == ModSource::EnvFollow) source_hi = 32767;
    if (!value.mod_enabled) source_lo = source_hi = 0;
    q31 lo = (q31)out.control + ((source_lo * value.mod_depth) >> 15);
    q31 hi = (q31)out.control + ((source_hi * value.mod_depth) >> 15);
    if (lo > hi) { const q31 swap = lo; lo = hi; hi = swap; }
    const q31 floor = value.bipolar ? -32767 : 0;
    out.clip_low = lo < floor; out.clip_high = hi > 32767;
    auto clamp = [floor](q31 v) -> q15 { return (q15)(v < floor ? floor : (v > 32767 ? 32767 : v)); };
    out.range_min = clamp(lo); out.range_max = clamp(hi);
    out.target = clamp((q31)out.control + (((q31)out.source_value * value.mod_depth) >> 15));
}

bool Engine::sequence_audition_allowed(int chain) const {
    if (chain < 0 || chain >= NUM_CHAINS || chains[chain].mute || chain_record_pending(chain))
        return false;
    bool audible = false;
    for (const Unit* unit : chains[chain].units) {
        if (!unit) continue;
        if (unit->kind == UnitKind::Recorder) {
            // Even bypassed REC captures. Never insert an edit preview into a take.
            if (static_cast<const RecUnit*>(unit)->writer_open()) return false;
        } else if (!unit->bypass) {
            // ROT stopped auditions own a private preparing tape. A REC owner
            // anywhere excludes that manual preview; other source policy stays.
            if (unit->kind == UnitKind::Rot && recording_active_or_pending()) return false;
            audible = true;
        }
    }
    return audible;
}

void Engine::refresh_audition_params(int chain, bool release) {
    if (chain < 0 || chain >= NUM_CHAINS) return;
    for (Unit* unit : chains[chain].units) {
        if (!unit) continue;
        bool changed = false;
        for (int p = 0; p < unit->num_params; ++p) {
            Param& param = unit->params[p];
            if (!param.audition_override) continue;
            changed = true;
            if (release) param.audition_override = false;
            if (!param.temp_override) param.slew_ready = false;
            if(unit->kind==UnitKind::Scratch) {
                auto* s=static_cast<ScratchUnit*>(unit);s->synchronize();s->resolve_param(p,parameter_modulation_value(unit,p));
            } else param.resolve(parameter_modulation_value(unit,p));
        }
        if (!changed) continue;
        unit->control_tick();
        update_clock_unit(unit);
    }
}

void Engine::cancel_rot_auditions(int chain) {
    if (chain < -1 || chain >= NUM_CHAINS) return;
    const int first = chain < 0 ? 0 : chain;
    const int last = chain < 0 ? NUM_CHAINS : chain + 1;
    for (int c = first; c < last; ++c) for (Unit* unit : chains[c].units) {
        if (!unit || unit->kind != UnitKind::Rot) continue;
        auto* rot = static_cast<RotUnit*>(unit);
        // audition_ is the actual stopped manual owner, also retained after
        // stop()/release. started_/HOLD alone never grant cancellation authority.
        if (rot->audition_) rot->reset();
    }
}

void Engine::cancel_sequence_audition() {
    const int chain = sequence_audition_chain_;
    sequence_audition_chain_ = -1;
    sequence_audition_end_frame_ = 0;
    // No retained Unit pointers or saved params: current topology and current
    // playback/performance overrides remain authoritative after cancellation.
    // No owner (-1) is a no-op, NOT an all-chain ROT cancellation. Kill the
    // old chain's transient reader before a refresh can leave preparation alive.
    if (chain >= 0) cancel_rot_auditions(chain);
    refresh_audition_params(chain, true);
}

bool Engine::audition_sequence_step(int chain, int step, int bank) {
    cancel_sequence_audition();
    if (step < 0 || step >= SEQ_STEPS || !sequence_audition_allowed(chain)) return false;
    sequence_audition_chain_ = (int8_t)chain;
    sequence_audition_end_frame_ = audio_frame_ + SEQUENCE_AUDITION_FRAMES;
    // Begin from this step's unlocked bases, not locks from whatever event is
    // currently playing. Leave the latter intact underneath the audition layer.
    for (Unit* unit : chains[chain].units) {
        if (!unit || unit->bypass || unit->kind == UnitKind::Recorder) continue;
        for (int p = 0; p < unit->num_params; ++p) {
            unit->params[p].audition = unit->params[p].base;
            unit->params[p].audition_override = true;
        }
    }
    const SeqStep& selected = sequence_pattern(bank).steps[chain][step];
    const int count = selected.lock_count > SEQ_LOCKS ? SEQ_LOCKS : selected.lock_count;
    for (int i = 0; i < count; ++i) {
        const StepLock& lock = selected.locks[i];
        if (lock.slot >= UNITS_PER_CHAIN) continue;
        Unit* unit = chains[chain].units[lock.slot];
        if (!unit || unit->bypass || unit->kind == UnitKind::Recorder ||
            lock.param >= unit->num_params || lock.param >= PARAM_BANK_SIZE) continue;
        unit->params[lock.param].audition = lock.value;
    }
    refresh_audition_params(chain, false);
    // Ignore step trig/condition/retrig intentionally: this is a single explicit
    // listening gesture, never a scheduler event or a random-condition draw.
    trigger_chain_internal(chain, true);
    return true;
}

void Engine::release_sequence_pitch(int chain) {
    for (int c = 0; c < NUM_CHAINS; ++c) {
        if (chain >= 0 && chain != c) continue;
        SequencePitchOwner& owner = sequence_pitch_owner_[c];
        // Check installation before dereferencing a retained identity. Structural
        // mutators release before detaching; direct stale identities are dropped.
        if (owner.unit && owner.slot >= 0 && owner.slot < UNITS_PER_CHAIN &&
            chains[c].units[(int)owner.slot] == owner.unit &&
            owner.param >= 0 && owner.param < PARAM_BANK_SIZE) {
            Param& param = owner.unit->params[(int)owner.param];
            param.step_lock_override = false;
            if (!param.temp_override) param.slew_ready = false;
        }
        owner = {};
    }
}

bool Engine::validate_sequence_pitch(int chain, const SequenceSource& source) {
    const SequencePitchOwner& owner = sequence_pitch_owner_[chain];
    if (!owner.unit) return false;
    const Unit* unit = source.slot < 0 ? nullptr : chains[chain].units[(int)source.slot];
    if (transport_playing && sequencer_enabled && unit == owner.unit &&
        source.slot == owner.slot && source.properties.pitch_param == owner.param &&
        unit->kind == owner.kind && source.pitch_domain == owner.domain &&
        unit->params[(int)owner.param].bipolar == owner.bipolar &&
        unit->params[(int)owner.param].step_lock_override) return false;
    release_sequence_pitch(chain);
    return true;
}

void Engine::release_sequence_locks(int chain) {
    release_sequence_step_locks(chain);
    release_sequence_pitch(chain);
}

void Engine::release_sequence_step_locks(int chain) {
    if (chain < 0 || chain == sequence_audition_chain_) cancel_sequence_audition();
    for (auto& active : active_step_locks_) {
        if (!active.valid || (chain >= 0 && active.chain != chain)) continue;
        if (active.chain < NUM_CHAINS && active.slot < UNITS_PER_CHAIN) {
            Unit* unit = chains[active.chain].units[active.slot];
            // Reader/domain edits can shrink the visible count between events.
            // The fixed bank still exists: never orphan a now-dormant override.
            if (unit && active.param < PARAM_BANK_SIZE) {
                Param& param = unit->params[active.param];
                param.step_lock_override = false;
                // The next resolve must publish the restored base+mod value at
                // the boundary, not slew from the preceding step's lock.
                if (!param.temp_override) param.slew_ready = false;
            }
        }
        active.valid = false;
    }
}

bool Engine::step_condition_passes(const SeqStep& step, int chain, uint64_t tick, int bank) {
    using Condition = SeqStep::Condition;
    Condition condition = step.condition();
    if (condition >= Condition::Count) condition = Condition::Always;
    if (condition == Condition::Always) return true;
    const uint64_t cycle = sequence_cycle_at_tick(chain, tick, bank);
    if (condition == Condition::FirstOfTwo) return (cycle & 1u) == 0;
    if (condition == Condition::SecondOfTwo) return (cycle & 1u) != 0;
    if (condition == Condition::Fill || condition == Condition::NotFill) return true;
    sequence_rng_ = sequence_rng_ * 1664525u + 1013904223u;
    const uint16_t random = (uint16_t)(sequence_rng_ >> 16);
    if (condition == Condition::Quarter) return random < 16384u;
    if (condition == Condition::Half) return random < 32768u;
    if (condition == Condition::ThreeQuarter) return random < 49152u;
    return true;
}

uint64_t Engine::next_sequence_grid_frame(uint64_t frame) const {
    const uint32_t fpb = frames_per_beat();
    const uint32_t sixteenth = fpb > 3 ? fpb / 4 : 1;
    const uint32_t subdivision = sequence_plan_subdivision_ & 3u;
    if (frame != transport_frame)
        return frame + (subdivision ? sixteenth : fpb - sixteenth * 3u);
    if (!subdivision) return frame + (fpb - frame_in_beat);
    const uint32_t target = subdivision * sixteenth;
    return frame + (target > frame_in_beat ? target - frame_in_beat : fpb - frame_in_beat + target);
}

void Engine::clear_sequence_runtime(bool cancel_temp, bool release_pitch) {
    if (cancel_temp) { cancel_param_temp(); cancel_scratch_gesture(); }
    release_sequence_step_locks();
    if (release_pitch) release_sequence_pitch();
    for (auto& pass : active_sequence_pass_) pass = false;
    for (auto& chain : pending_sequence_events_) for (auto& event : chain) event = {};
    for (int chain = 0; chain < NUM_CHAINS; ++chain) {
        active_sequence_tick_[chain] = 0;
        sequence_event_frame_[chain] = 0;
        sequence_event_span_[chain] = 1;
        active_sequence_source_step_[chain] = active_sequence_source_bank_[chain] = 255;
        sequence_hit_[chain] = {};
        sequence_hit_audio_frame_[chain] = 0;
    }
    sequence_clock_active_ = false;
    sequence_plan_tick_ = sequence_tick;
    sequence_plan_grid_frame_ = 0;
    sequence_plan_subdivision_ = (uint8_t)((sequence_tick + 1u) & 3u);
}

void Engine::schedule_sequence_tick(uint64_t tick, uint64_t nominal_frame) {
    const uint32_t sixteenth = frames_per_beat() > 3 ? frames_per_beat() / 4 : 1;
    for (int chain = 0; chain < NUM_CHAINS; ++chain) {
        const int bank = transition_bank_at_tick(chain, tick);
        const Pattern& selected = sequence_pattern(bank);
        const int step_index = (int)sequence_step_at_tick(chain, tick, bank);
        int amount = selected.microtiming[chain][step_index];
        if (amount < -SEQ_MICROTIMING_MAX) amount = -SEQ_MICROTIMING_MAX;
        if (amount > SEQ_MICROTIMING_MAX) amount = SEQ_MICROTIMING_MAX;
        const int64_t product = (int64_t)sixteenth * amount;
        const int64_t offset = product >= 0
            ? (product + SEQ_MICROTIMING_DIVISIONS / 2) / SEQ_MICROTIMING_DIVISIONS
            : -((-product + SEQ_MICROTIMING_DIVISIONS / 2) / SEQ_MICROTIMING_DIVISIONS);
        int64_t due = (int64_t)nominal_frame + offset;
        if (due < (int64_t)transport_frame) due = (int64_t)transport_frame;
        PendingSequenceEvent* slot = nullptr;
        for (auto& event : pending_sequence_events_[chain]) if (!event.valid) { slot = &event; break; }
        if (!slot) continue;
        slot->valid = true;
        slot->tick = tick;
        slot->frame = (uint64_t)due;
        slot->nominal_frame = nominal_frame;
        slot->source_step = (uint8_t)step_index;
        slot->source_bank = bank < 0 ? active_pattern : (uint8_t)bank;
        slot->step = selected.steps[chain][step_index];
        slot->condition_pass = step_condition_passes(slot->step, chain, tick, bank);
    }
}

void Engine::initialize_sequence_clock(bool start_now) {
    // Initial lazy clock setup is not a new user transport action.
    clear_sequence_runtime(false);
    sequence_clock_active_ = true;
    sequence_plan_tick_ = sequence_tick;
    if (clock_runtime_) {
        clock_plan_phase_ = start_now ? clock_phase_ : clock_next_boundary(LaunchQuantize::Sixteenth);
        for (int i = 0; i < 2; ++i) {
            schedule_sequence_phase(sequence_plan_tick_++, clock_plan_phase_);
            clock_plan_phase_ += CLOCK_STEP;
        }
        update_clock_deadlines();
        if (start_now) dispatch_sequence_phase();
        return;
    }
    if (start_now) {
        sequence_plan_subdivision_ = (uint8_t)(sequence_plan_tick_ & 3u);
        sequence_plan_grid_frame_ = transport_frame;
    } else {
        sequence_plan_subdivision_ = (uint8_t)((sequence_plan_tick_ + 1u) & 3u);
        sequence_plan_grid_frame_ = next_sequence_grid_frame(transport_frame);
    }
    for (int planned = 0; planned < 2; ++planned) {
        const uint32_t sixteenth = frames_per_beat() > 3 ? frames_per_beat() / 4 : 1;
        uint8_t swing = swing_percent < 50 ? 50 : swing_percent > 75 ? 75 : swing_percent;
        const uint32_t delay = (uint32_t)(((uint64_t)sixteenth * (2u * swing - 100u)) / 100u);
        const uint64_t nominal = sequence_plan_grid_frame_ + ((sequence_plan_tick_ & 1u) ? delay : 0u);
        schedule_sequence_tick(sequence_plan_tick_, nominal);
        ++sequence_plan_tick_;
        sequence_plan_subdivision_ = (uint8_t)((sequence_plan_subdivision_ + 1u) & 3u);
        sequence_plan_grid_frame_ = next_sequence_grid_frame(sequence_plan_grid_frame_);
    }
    if (start_now) dispatch_sequence_events();
}

void Engine::advance_sequence_clock() {
    if (!sequence_clock_active_) return;
    if (clock_runtime_) {
        while (clock_plan_phase_ <= clock_phase_ + CLOCK_BEAT) {
            bool space = true;
            for (const auto& chain : pending_sequence_events_) {
                bool free = false; for (const auto& event : chain) if (!event.valid) free = true;
                if (!free) { space = false; break; }
            }
            if (!space) break;
            schedule_sequence_phase(sequence_plan_tick_++, clock_plan_phase_);
            clock_plan_phase_ += CLOCK_STEP;
        }
        update_clock_deadlines();
        return;
    }
    while (sequence_plan_grid_frame_ <= transport_frame + frames_per_beat()) {
        bool have_space = true;
        for (const auto& chain : pending_sequence_events_) {
            bool free = false; for (const auto& event : chain) if (!event.valid) free = true;
            if (!free) { have_space = false; break; }
        }
        if (!have_space) break;
        const uint32_t sixteenth = frames_per_beat() > 3 ? frames_per_beat() / 4 : 1;
        uint8_t swing = swing_percent < 50 ? 50 : swing_percent > 75 ? 75 : swing_percent;
        const uint32_t delay = (uint32_t)(((uint64_t)sixteenth * (2u * swing - 100u)) / 100u);
        const uint64_t nominal = sequence_plan_grid_frame_ + ((sequence_plan_tick_ & 1u) ? delay : 0u);
        schedule_sequence_tick(sequence_plan_tick_, nominal);
        ++sequence_plan_tick_;
        sequence_plan_subdivision_ = (uint8_t)((sequence_plan_subdivision_ + 1u) & 3u);
        sequence_plan_grid_frame_ = next_sequence_grid_frame(sequence_plan_grid_frame_);
    }
}

void Engine::fire_sequence_event(int chain, uint64_t tick, uint64_t nominal_frame) {
    bool refresh_param[UNITS_PER_CHAIN][PARAM_BANK_SIZE] = {};
    for (const ActiveStepLock& active : active_step_locks_)
        if (active.valid && active.chain == chain && active.slot < UNITS_PER_CHAIN &&
            active.param < PARAM_BANK_SIZE)
            refresh_param[active.slot][active.param] = true;
    release_sequence_step_locks(chain); // ordinary locks end even on condition failure
    SeqStep seq;
    const PendingSequenceEvent* pending = nullptr;
    for (const auto& event : pending_sequence_events_[chain])
        if (event.valid && event.tick == tick && event.nominal_frame == nominal_frame) { pending = &event; break; }
    if (pending) seq = pending->step;
    const SeqStep::Condition condition = seq.condition();
    bool pass = pending ? pending->condition_pass : true;
    if (condition == SeqStep::Condition::Fill) pass = sequencer_fill_active;
    else if (condition == SeqStep::Condition::NotFill) pass = !sequencer_fill_active;
    pass = pass && sequencer_enabled;
    active_sequence_pass_[chain] = pass;
    active_sequence_step_[chain] = seq;
    active_sequence_source_step_[chain] = pending ? pending->source_step : 255;
    active_sequence_source_bank_[chain] = pending ? pending->source_bank : 255;
    active_sequence_tick_[chain] = tick;
    sequence_event_frame_[chain] = transport_frame;
    const int count = pass ? (seq.lock_count > SEQ_LOCKS ? SEQ_LOCKS : seq.lock_count) : 0;
    // Stage at most four accepted values before choosing the primary domain.
    // MODE and NOTE can appear in either order. No intermediate resolve/tick:
    // selectors see the final accepted layers, unrelated DSP keeps its old clock.
    for (int i = 0; i < count; ++i) {
        const StepLock& lock = seq.locks[i];
        if (lock.slot >= UNITS_PER_CHAIN) continue;
        Unit* unit = chains[chain].units[lock.slot];
        if (!unit || lock.param >= unit->num_params || lock.param >= PARAM_BANK_SIZE) continue;
        unit->params[lock.param].step_lock = lock.value;
        unit->params[lock.param].step_lock_override = true;
    }
    const SequenceSource source = resolve_sequence_source(chain);
    const SequencePitchOwner previous_pitch = sequence_pitch_owner_[chain];
    if (validate_sequence_pitch(chain, source) && previous_pitch.slot >= 0 &&
        previous_pitch.slot < UNITS_PER_CHAIN && previous_pitch.param >= 0 &&
        previous_pitch.param < PARAM_BANK_SIZE &&
        chains[chain].units[(int)previous_pitch.slot] == previous_pitch.unit)
        refresh_param[(int)previous_pitch.slot][(int)previous_pitch.param] = true;

    int active_count = 0;
    for (int i = 0; i < count; ++i) {
        const StepLock& lock = seq.locks[i];
        if (lock.slot >= UNITS_PER_CHAIN) continue;
        Unit* unit = chains[chain].units[lock.slot];
        if (!unit || lock.param >= unit->num_params || lock.param >= PARAM_BANK_SIZE) continue;
        Param& param = unit->params[lock.param];
        if (source.slot == lock.slot && source.properties.pitch_param == lock.param) {
            // Any explicit raw value, including zero/base, is the next written
            // pitch. Note and generic Lock store the same StepLock provenance.
            sequence_pitch_owner_[chain] = {unit, (int8_t)lock.slot, (int8_t)lock.param,
                                            unit->kind, param.bipolar, source.pitch_domain};
        } else {
            ActiveStepLock& active = active_step_locks_[chain * SEQ_LOCKS + active_count++];
            active = {true, (uint8_t)chain, lock.slot, lock.param};
        }
        // Reapply after invalidation: a former pitch target may legitimately be
        // an ordinary upstream/dormant lock in this event; it must stay tracked.
        param.step_lock = lock.value;
        param.step_lock_override = true;
        refresh_param[lock.slot][lock.param] = true;
        if (!param.temp_override) param.slew_ready = false;
    }

    // Only the primary pitch survives unwritten/rejected steps. Ordinary locks
    // still expire every boundary. Refresh changed parameters only, retaining
    // live modulation and trigger-time caches without extra unrelated slew ticks.
    for (int slot = 0; slot < UNITS_PER_CHAIN; ++slot) {
        Unit* unit = chains[chain].units[slot];
        if (!unit) continue;
        bool refresh_unit = false;
        for (int param_index = 0; param_index < unit->num_params; ++param_index) {
            if (!refresh_param[slot][param_index]) continue;
            refresh_unit = true;
            Param& param = unit->params[param_index];
            const q15 modulation = parameter_modulation_value(unit,param_index);
            if(unit->kind==UnitKind::Scratch) {
                auto* s=static_cast<ScratchUnit*>(unit);s->synchronize();s->resolve_param(param_index,modulation);
            } else param.resolve(modulation);
        }
        if (!refresh_unit) continue;
        unit->control_tick();
        update_clock_unit(unit);
    }
    if (pass && (seq.flags & SeqStep::Trig)) trigger_sequence_chain(chain);
    if (tick + 1 > sequence_tick) sequence_tick = tick + 1;
}

void Engine::dispatch_sequence_events() {
    if (clock_runtime_) { dispatch_sequence_phase(); return; }
    if (!sequence_clock_active_) return;
    for (int chain = 0; chain < NUM_CHAINS; ++chain) {
        PendingSequenceEvent* due = nullptr;
        PendingSequenceEvent* next = nullptr;
        for (auto& event : pending_sequence_events_[chain]) {
            if (!event.valid) continue;
            if (event.frame <= transport_frame && (!due || event.frame < due->frame)) due = &event;
            else if (!next || event.frame < next->frame) next = &event;
        }
        if (!due) continue;
        if (next && next->frame > due->frame)
            sequence_event_span_[chain] = (uint32_t)(next->frame - due->frame);
        else sequence_event_span_[chain] = 1;
        const uint64_t tick = due->tick, nominal = due->nominal_frame;
        fire_sequence_event(chain, tick, nominal);
        due->valid = false;
    }
    advance_sequence_clock();
}

uint64_t Engine::next_sequence_deadline() const {
    if (clock_runtime_ && transport_playing) return clock_frame_for_phase(clock_next_deadline_phase());
    uint64_t deadline = UINT64_MAX;
    if (!sequence_clock_active_) return deadline;
    for (const auto& chain : pending_sequence_events_) for (const auto& event : chain)
        if (event.valid && event.frame < deadline) deadline = event.frame;
    for (int chain = 0; chain < NUM_CHAINS; ++chain) {
        if (!active_sequence_pass_[chain]) continue;
        const SeqStep& step = active_sequence_step_[chain];
        const uint8_t count = step.retrig_count();
        for (uint8_t pulse = 1; pulse < count; ++pulse) {
            const uint64_t frame = sequence_event_frame_[chain] +
                ((uint64_t)sequence_event_span_[chain] * pulse) / count;
            if (frame > transport_frame && frame < deadline) deadline = frame;
        }
    }
    return deadline;
}

void Engine::fire_sequence_retrigs() {
    if (!sequence_clock_active_) return;
    for (int chain = 0; chain < NUM_CHAINS; ++chain) {
        if (!active_sequence_pass_[chain]) continue;
        const SeqStep& step = active_sequence_step_[chain];
        if (!(step.flags & SeqStep::Trig)) continue;
        const uint8_t count = step.retrig_count();
        for (uint8_t pulse = 1; pulse < count; ++pulse) {
            const uint64_t frame = sequence_event_frame_[chain] +
                ((uint64_t)sequence_event_span_[chain] * pulse) / count;
            if (frame == transport_frame) { trigger_sequence_chain(chain); break; }
        }
    }
}

void Engine::advance_transport(int frames) {
    if (clock_runtime_) { advance_clock_transport(frames); return; }
    if (!transport_playing || frames <= 0) return;
    if (sequencer_enabled && !sequence_clock_active_) initialize_sequence_clock(false);
    const uint32_t fpb = frames_per_beat();
    const uint32_t sixteenth = fpb > 3 ? fpb / 4 : 1;
    // Jump between interesting frames (beat / sixteenth / sequencer deadline)
    // instead of walking frame by frame. The old per-frame loop performed a
    // modulo, an event scan, and 64-bit retrig divisions on every one of the
    // 32000 frames per second - a large fixed tax on the Old 3DS audio budget.
    uint32_t remaining = (uint32_t)frames;
    while (remaining) {
        uint32_t step = remaining;
        const uint32_t to_beat = fpb - frame_in_beat;
        if (to_beat < step) step = to_beat;
        const uint32_t to_sixteenth = sixteenth - (frame_in_beat % sixteenth);
        if (to_sixteenth < step) step = to_sixteenth;
        if (sequence_clock_active_) {
            const uint64_t deadline = next_sequence_deadline();
            if (deadline > transport_frame && deadline - transport_frame < (uint64_t)step)
                step = (uint32_t)(deadline - transport_frame);
        }
        transport_frame += step;
        frame_in_beat += step;
        remaining -= step;

        bool beat_boundary = false;
        if (frame_in_beat >= fpb) {
            frame_in_beat = 0;
            ++beat_count;
            beat_boundary = true;
        }
        if (beat_boundary) {
            if (launch_quantize == LaunchQuantize::Beat ||
                (launch_quantize == LaunchQuantize::Bar && beat_in_bar() == 0)) {
                fire_pending(0x0f);
                fire_pending_rec(0x0f);
            }
            if (pending_pattern < NUM_PATTERNS &&
                (pattern_launch_quantize == LaunchQuantize::Beat ||
                 (pattern_launch_quantize == LaunchQuantize::Bar && beat_in_bar() == 0)))
                activate_pattern(pending_pattern, true);
            if (beat_in_bar() == 0) advance_pattern_chain_at_bar();
        }
        const bool sixteenth_boundary = beat_boundary || (frame_in_beat % sixteenth == 0);
        if (sixteenth_boundary) {
            if (launch_quantize == LaunchQuantize::Sixteenth) {
                fire_pending(0x0f);
                fire_pending_rec(0x0f);
            }
            if (pending_pattern < NUM_PATTERNS &&
                pattern_launch_quantize == LaunchQuantize::Sixteenth)
                activate_pattern(pending_pattern, true);
        }
        if (sequence_clock_active_) {
            dispatch_sequence_events();
            // dispatch fires one due event per chain per call, and scheduling
            // may clamp a microtimed event onto the current frame. Drain until
            // nothing is due so chunking never delays an event past its frame.
            while (next_sequence_deadline() <= transport_frame)
                dispatch_sequence_events();
            fire_sequence_retrigs();
        }
    }
}
void Engine::set_sigil_sync(int sigil, bool sync) {
    if (sigil < 0 || sigil >= NUM_SIGILS) return;
    sigil_sync[sigil] = sync;
    refresh_sigil_rate(sigil);
}

void Engine::set_sigil_div(int sigil, int div) {
    if (sigil < 0 || sigil >= NUM_SIGILS) return;
    if (div < 0) div = 0;
    if (div > 5) div = 5;
    sigil_div[sigil] = (uint8_t)div;
    if (sigil_sync[sigil]) refresh_sigil_rate(sigil);
}

void Engine::set_step_dest(int sigil, int chain) {
    if (sigil < 0 || sigil >= NUM_SIGILS) return;
    if (chain < 0) chain = 0;
    if (chain >= NUM_CHAINS) chain = NUM_CHAINS - 1;
    step_dest[sigil] = (uint8_t)chain;
}

void Engine::set_sigil_mod(int sigil, ModSource src, q15 depth) {
    if (sigil < 0 || sigil >= NUM_SIGILS || src >= ModSource::Count) return;
    // ENV is delayed post-module history; all sigils must precede the destination.
    const int src_idx = mod_source_sigil(src);
    if (src_idx >= sigil) return;
    sigil_mods[sigil] = { src, depth };
    refresh_sigil_rate(sigil);
}

static bool sigil_legacy_shape(const Sigil& s, const SigilConfig& c) {
    return c.legacy || s.mode == SigilMode::RandomStep || s.mode == SigilMode::RandomSmooth;
}
static q15 sigil_random_next(uint32_t& state) {
    state = state * 1664525u + 1013904223u;
    const int32_t value = (int32_t)(state >> 16) - 32768;
    return (q15)(value == -32768 ? -32767 : value);
}
void Engine::refresh_sigil_rate(int index) {
    auto& s = sigils[index]; const auto& c = sigil_config[index];
    if (!c.enabled || s.mode == SigilMode::Follow) { s.phase_inc = 0; return; }
    if (sigil_sync[index]) {
        s.set_rate_sync_q16(effective_bpm_q16(), sigil_div[index], CONTROL_RATE);
    } else {
        const auto& m = sigil_mods[index];
        const int32_t mod = ((int32_t)modulation_value(m.src) * m.depth) >> 15;
        if (c.legacy || c.legacy_rate) {
            if (m.src == ModSource::None) {
                // Old files could store negative normalized values; the historic
                // squared law is still defined for them and must not be clamped.
                s.set_rate_norm(rate_norm_[index], CONTROL_RATE);
                return;
            }
            const int32_t rate = (int32_t)rate_norm_[index] + mod;
            s.set_rate_norm((q15)(rate < 0 ? 0 : rate > 32767 ? 32767 : rate), CONTROL_RATE);
        } else {
            // Signed modulation spans the entire modern free-rate range.
            int32_t rate = c.rate_millihz + (int32_t)((int64_t)mod * 19950 / 32767);
            if (rate < 50) rate = 50;
            if (rate > 20000) rate = 20000;
            s.phase_inc = (uint32_t)(((uint64_t)rate << 32) / (CONTROL_RATE * 1000u));
        }
    }
}
q15 Engine::sample_sigil_shape(int index, uint32_t phase) const {
    const auto& s = sigils[index]; const auto& c = sigil_config[index];
    const auto& r = sigil_runtime_[index];
    if (s.mode == SigilMode::Steps) phase = (phase & 0xf0000000u) + 0x08000000u;
    if (c.shape == SigilShape::Random) return r.to;
    if (c.shape == SigilShape::Smooth) {
        const uint64_t t = phase >> 16, t2 = t * t >> 16;
        const uint32_t sm = (uint32_t)((t2 * (196608u - 2u * t)) >> 16);
        return trackr::fx::lerp_q15(r.from, r.to, (uint16_t)(sm > 65535u ? 65535u : sm));
    }
    if (c.shape != SigilShape::Drawn) return sigil_wave_sample(c.shape, phase);
    if (!s.drawn) return 0;
    const unsigned pos = phase >> 25;
    if (s.mode == SigilMode::Steps) return s.table[pos];
    const unsigned next = pos == SIGIL_POINTS-1 && s.mode == SigilMode::OneShot ? pos : (pos+1)%SIGIL_POINTS;
    return trackr::fx::lerp_q15(s.table[pos], s.table[next], (uint16_t)(phase >> 9));
}
bool Engine::capture_sigil_wave(int index, q15* out, int count) const {
    if (!out || count < 1 || count > SIGIL_POINTS) return false;
    if ((unsigned)index >= NUM_SIGILS) {
        for (int p=0; p<count; ++p) out[p]=0;
        return false;
    }
    const auto& s=sigils[index]; const auto& c=sigil_config[index];
    const bool silent = !c.enabled || !sigil_config_valid(c) || s.mode >= SigilMode::Count ||
        (s.mode == SigilMode::Follow && !s.follow_active) ||
        (s.mode == SigilMode::OneShot && !s.running);
    const bool stochastic = s.mode == SigilMode::RandomStep || s.mode == SigilMode::RandomSmooth ||
        c.shape == SigilShape::Random || c.shape == SigilShape::Smooth;
    const bool empty = !stochastic && c.shape == SigilShape::Drawn && !s.drawn;
    if (silent || stochastic || empty) {
        const q15 scalar = silent || empty ? 0 : s.value;
        for (int p=0; p<count; ++p) out[p]=scalar;
        return false;
    }
    for (int p=0; p<count; ++p) {
        const uint32_t phase = count == 1 ? 0 : (uint32_t)((uint64_t)p * UINT32_MAX / (count-1));
        int32_t raw = sample_sigil_shape(index,phase);
        if (c.legacy) out[p]=(q15)((raw * s.depth) >> 15);
        else {
            if (raw < -32767) raw=-32767;
            if (c.unipolar) raw=(raw+32767)/2;
            const int32_t value=raw * s.depth / 32767;
            out[p]=(q15)(value < -32767 ? -32767 : value > 32767 ? 32767 : value);
        }
    }
    return true;
}
void Engine::step_sigil(int index, bool advance) {
    auto& s = sigils[index]; const auto& c = sigil_config[index]; auto& r = sigil_runtime_[index];
    if (!c.enabled) { s.value = 0; return; }
    if (sigil_legacy_shape(s, c)) {
        if (advance) s.step();
        return;
    }
    if ((c.shape == SigilShape::Drawn && !s.drawn) ||
        (s.mode == SigilMode::Follow && !s.follow_active) ||
        (s.mode == SigilMode::OneShot && !s.running)) { s.value = 0; return; }
    const uint32_t before = s.mode == SigilMode::Follow ? r.previous_phase : s.phase;
    if (advance && s.mode != SigilMode::Follow) s.phase += s.phase_inc;
    const bool wrapped = advance && s.phase < before;
    if (wrapped && s.mode == SigilMode::OneShot) { s.running = false; s.phase = UINT32_MAX; }
    else if (wrapped) {
        r.from = r.to;
        r.to = sigil_random_next(r.random_state);
    }
    r.previous_phase = s.phase;
    int32_t raw = sample_sigil_shape(index, s.phase);
    if (raw < -32767) raw = -32767;
    if (c.unipolar) raw = (raw + 32767) / 2;
    int32_t value = raw * s.depth / 32767; // modern unity is exact (PBAY migration)
    s.value = (q15)(value < -32767 ? -32767 : value > 32767 ? 32767 : value);
}
void Engine::reset_sigil(int index) {
    if ((unsigned)index >= NUM_SIGILS) return;
    auto& s = sigils[index]; auto& r = sigil_runtime_[index]; const auto& c = sigil_config[index];
    s.trigger(); s.value = 0; s.follow_active = false;
    s.random_state = 0x53494731u; s.random_from = s.random_to = 0;
    r = {};
    // e..h deliberately reproduce historical PBAY1 slot seeds.
    r.random_state = 0x4c464f31u + (uint32_t)(index >= 4 ? index-4 : index+4) * 0x9e3779b9u;
    r.to = sigil_random_next(r.random_state);
    r.from = 0;
    if (!sigil_legacy_shape(s,c) && s.mode != SigilMode::Follow) s.phase = sigil_phase_offset(c.phase_degrees);
    r.previous_phase = s.phase;
    refresh_sigil_rate(index);
    step_sigil(index, false);
}
bool Engine::set_sigil_config(int index, const SigilConfig& c) {
    if ((unsigned)index >= NUM_SIGILS || !sigil_config_valid(c)) return false;
    const SigilConfig old = sigil_config[index];
    if (sigil_config_equal(old,c)) return false;
    sigil_config[index] = c;
    if ((!old.enabled && c.enabled) || old.legacy != c.legacy) reset_sigil(index);
    else {
        if (!sigil_legacy_shape(sigils[index],c) && sigils[index].mode != SigilMode::Follow)
            sigils[index].phase += sigil_phase_offset(c.phase_degrees) - sigil_phase_offset(old.phase_degrees);
        refresh_sigil_rate(index);
        step_sigil(index, false);
    }
    return true;
}

void Engine::set_sigil_rate(int sigil, q15 norm) {
    if (sigil < 0 || sigil >= NUM_SIGILS) return;
    rate_norm_[sigil] = norm;
    if (!sigil_sync[sigil]) refresh_sigil_rate(sigil);
}

void Engine::swap_patch_state(Engine& other) {
    stop_bounce_preview(); other.stop_bounce_preview();
    stop_play_preview(); other.stop_play_preview();
    // A pending live fade must never leak into a newly loaded/swapped patch.
    for (int c=0;c<NUM_CHAINS;++c) { volume_ramp_[c]={}; other.volume_ramp_[c]={}; }
    cancel_param_temp();
    other.cancel_param_temp();
    cancel_scratch_gesture();other.cancel_scratch_gesture();
    retire_relic_sources_all();other.retire_relic_sources_all();
    for (int i = 0; i < NUM_SIGILS; ++i) {
        const SigilConfig config = sigil_config[i];
        sigil_config[i] = other.sigil_config[i]; other.sigil_config[i] = config;
    }
    const uint8_t ec = env_chain_; env_chain_ = other.env_chain_; other.env_chain_ = ec;
    const EnvResponse er = env_response_; env_response_ = other.env_response_; other.env_response_ = er;
    invalidate_structural_undo();
    other.invalidate_structural_undo();
    // Parked resources depart WITH their patch. No guarded destruction.
    {Unit* retired=structural_undo_retired_;structural_undo_retired_=other.structural_undo_retired_;other.structural_undo_retired_=retired;}
    release_sequence_locks();
    other.release_sequence_locks();
    for (int c = 0; c < NUM_CHAINS; ++c) {
        for (int s = 0; s < UNITS_PER_CHAIN; ++s) {
            Unit* t = chains[c].units[s];
            chains[c].units[s] = other.chains[c].units[s];
            other.chains[c].units[s] = t;
        }
        q15 v = chains[c].volume; chains[c].volume = other.chains[c].volume; other.chains[c].volume = v;
        q15 p = chains[c].pan; chains[c].pan = other.chains[c].pan; other.chains[c].pan = p;
        bool m = chains[c].mute; chains[c].mute = other.chains[c].mute; other.chains[c].mute = m;
        PersistentScene sa = scenes[c][0]; scenes[c][0] = other.scenes[c][0]; other.scenes[c][0] = sa;
        PersistentScene sb = scenes[c][1]; scenes[c][1] = other.scenes[c][1]; other.scenes[c][1] = sb;
        q15 morph = scene_morph[c]; scene_morph[c] = other.scene_morph[c]; other.scene_morph[c] = morph;
    }
    for (int p = 0; p < 8; ++p) { MacroPad x = macro_pads[p]; macro_pads[p] = other.macro_pads[p]; other.macro_pads[p] = x; }
    for (int i = 0; i < NUM_PERFORMANCE_BINDINGS; ++i) {
        PerformanceBinding x = performance_bindings[i];
        performance_bindings[i] = other.performance_bindings[i];
        other.performance_bindings[i] = x;
    }
    swap_pattern(pattern, other.pattern);
    for (int bank = 0; bank < NUM_PATTERNS; ++bank)
        swap_pattern(pattern_banks[bank], other.pattern_banks[bank]);
    uint8_t ap = active_pattern; active_pattern = other.active_pattern; other.active_pattern = ap;
    uint8_t pp = pending_pattern; pending_pattern = other.pending_pattern; other.pending_pattern = pp;
    LaunchQuantize pq = pattern_launch_quantize; pattern_launch_quantize = other.pattern_launch_quantize; other.pattern_launch_quantize = pq;
    MusicalScale scale = project_scale_; project_scale_ = other.project_scale_; other.project_scale_ = scale;
    for (auto& chain : chains) for (auto* unit : chain.units) bind_pitch_scale(unit);
    for (auto& chain : other.chains) for (auto* unit : chain.units) other.bind_pitch_scale(unit);
    bind_pitch_scale(&play_preview_); other.bind_pitch_scale(&other.play_preview_);
    uint8_t sw = swing_percent; swing_percent = other.swing_percent; other.swing_percent = sw;
    bool seq_on = sequencer_enabled; sequencer_enabled = other.sequencer_enabled; other.sequencer_enabled = seq_on;
    for (int i = 0; i < PATTERN_CHAIN_STEPS; ++i) {
        PatternChainStep entry = pattern_chain[i]; pattern_chain[i] = other.pattern_chain[i]; other.pattern_chain[i] = entry;
    }
    uint8_t pcl = pattern_chain_length; pattern_chain_length = other.pattern_chain_length; other.pattern_chain_length = pcl;
    bool pcloop = pattern_chain_loop; pattern_chain_loop = other.pattern_chain_loop; other.pattern_chain_loop = pcloop;
    pattern_chain_position = other.pattern_chain_position = 0;
    stop_pattern_chain();
    other.stop_pattern_chain();
    for (int i = 0; i < NUM_SIGILS; ++i) {
        Sigil sg = sigils[i]; sigils[i] = other.sigils[i]; other.sigils[i] = sg;
        SigilMod sm = sigil_mods[i]; sigil_mods[i] = other.sigil_mods[i]; other.sigil_mods[i] = sm;
        q15 rn = rate_norm_[i]; rate_norm_[i] = other.rate_norm_[i]; other.rate_norm_[i] = rn;
        bool sy = sigil_sync[i]; sigil_sync[i] = other.sigil_sync[i]; other.sigil_sync[i] = sy;
        uint8_t dv = sigil_div[i]; sigil_div[i] = other.sigil_div[i]; other.sigil_div[i] = dv;
        uint8_t dt = step_dest[i]; step_dest[i] = other.step_dest[i]; other.step_dest[i] = dt;
    }
    for (int i = 0; i < 2; ++i) {
        ClockMod route = clock_mods_[i]; clock_mods_[i] = other.clock_mods_[i]; other.clock_mods_[i] = route;
    }
    int b = bpm; bpm = other.bpm; other.bpm = b;
    bool tp = transport_playing; transport_playing = other.transport_playing; other.transport_playing = tp;
    uint8_t ts = beats_per_bar; beats_per_bar = other.beats_per_bar; other.beats_per_bar = ts;
    LaunchQuantize lq = launch_quantize; launch_quantize = other.launch_quantize; other.launch_quantize = lq;
    // A held performance gesture belongs to the departing patch, not its units.
    live_retrigger_active_ = other.live_retrigger_active_ = false;
    for(int c=0;c<NUM_CHAINS;++c){rebind_rules(c);other.rebind_rules(c);rebind_rots(c);other.rebind_rots(c);}
    transport_reset();
    other.transport_reset();
    env_value_ = other.env_value_ = 0;
    control_countdown_ = other.control_countdown_ = 0;
}

void Engine::set_external_sample_slot(int slot) {
    external_sample_slot_ = slot >= 0 && slot < trackr::synth::SAMPLE_BANK_SIZE ? slot : -1;
}

SampleSlotRefs Engine::sample_slot_refs(int sample_slot, const Unit* ignore) const {
    SampleSlotRefs refs;
    if (sample_slot < 0 || sample_slot >= trackr::synth::SAMPLE_BANK_SIZE) return refs;
    if (external_sample_slot_ == sample_slot) ++refs.external;
    if (bounce_.view.slot == sample_slot && bounce_storage_valid()) ++refs.bounce;
    for (int chain = 0; chain < NUM_CHAINS; ++chain) {
        for (const Unit* unit : chains[chain].units) {
            if (!unit || unit == ignore) continue;
            switch (unit->kind) {
                case UnitKind::SamplePlayer:
                    if (static_cast<const PlayUnit*>(unit)->sample_slot == sample_slot) ++refs.play;
                    break;
                case UnitKind::Grain:
                    if (static_cast<const GrainUnit*>(unit)->sample_slot == sample_slot) ++refs.grain;
                    break;
                case UnitKind::Scratch:
                    if (static_cast<const ScratchUnit*>(unit)->sample_slot == sample_slot) ++refs.scratch;
                    break;
                case UnitKind::Paul:
                    if (static_cast<const PaulUnit*>(unit)->sample_slot == sample_slot) ++refs.paul;
                    break;
                case UnitKind::Rot:
                    if (static_cast<const RotUnit*>(unit)->sample_slot == sample_slot) ++refs.rot;
                    break;
                case UnitKind::Rule:
                    if (static_cast<const RuleUnit*>(unit)->sample_slot == sample_slot) ++refs.rule;
                    break;
                case UnitKind::Recorder:
                    if (static_cast<const RecUnit*>(unit)->sample_slot == sample_slot) ++refs.rec;
                    break;
                default: break;
            }
        }
    }
    // A deleted SCRT retained for structural undo still owns its source alias.
    const Unit* retained=structural_undo_ && structural_undo_->valid?structural_undo_->retained:nullptr;
    if(retained && retained!=ignore && retained->kind==UnitKind::Scratch &&
        static_cast<const ScratchUnit*>(retained)->sample_slot==sample_slot)++refs.scratch;
    if(retained && retained!=ignore && retained->kind==UnitKind::Paul &&
        static_cast<const PaulUnit*>(retained)->sample_slot==sample_slot)++refs.paul;
    if(retained && retained!=ignore && retained->kind==UnitKind::Rule &&
        static_cast<const RuleUnit*>(retained)->sample_slot==sample_slot)++refs.rule;
    if(retained && retained!=ignore && retained->kind==UnitKind::Rot &&
        static_cast<const RotUnit*>(retained)->sample_slot==sample_slot)++refs.rot;
    return refs;
}

bool Engine::recorder_is_private(const RecUnit& rec) const {
    return rec.sample_slot >= 0 && rec.sample_slot < trackr::synth::SAMPLE_BANK_SIZE &&
           sample_slot_refs(rec.sample_slot, &rec).total() == 0;
}

static bool recorder_workspace_busy(const Engine& engine, const RecUnit& rec) {
    if (rec.writer_open()) return true;
    for (int chain = 0; chain < NUM_CHAINS; ++chain)
        for (int slot = 0; slot < UNITS_PER_CHAIN; ++slot)
            if (engine.chains[chain].units[slot] == &rec)
                return engine.recorder_record_pending(chain, slot) ||
                    engine.chain_record_pending_start(chain) || engine.chain_record_pending_stop(chain);
    return false;
}

trackr::synth::SampleEditResult Engine::plan_recorder(RecUnit& rec, bool auto_detach, RecWorkspacePlan& out,
                                                       int requested_slot) const {
    using namespace trackr::synth;
    out = {};
    if (recorder_workspace_busy(*this, rec)) return SampleEditResult::Busy;
    SampleBank& bank = SampleBank::instance();
    int target = rec.sample_slot;
    bool installed = false;
    for (int chain = 0; chain < NUM_CHAINS && !installed; ++chain)
        for (int slot = 0; slot < UNITS_PER_CHAIN; ++slot) if (chains[chain].units[slot] == &rec) {
            installed = true; out.chain = (int8_t)chain; out.unit_slot = (int8_t)slot; break;
        }
    const bool valid_source = rec.sample_slot >= 0 && rec.sample_slot < SAMPLE_BANK_SIZE;
    const bool owns_source = valid_source && (bank.slot(rec.sample_slot).empty() ||
        (rec.recorded_frames() && rec.recorded_frames() == bank.slot(rec.sample_slot).playable_frames()));
    const bool private_slot = installed && owns_source && recorder_is_private(rec);
    if (requested_slot >= 0) {
        if (requested_slot >= SAMPLE_BANK_SIZE) return SampleEditResult::InvalidSlot;
        if (sample_slot_refs(requested_slot, &rec).total() != 0 ||
            (!bank.slot(requested_slot).empty() && !(requested_slot == rec.sample_slot && owns_source)))
            return SampleEditResult::SharedSlot;
        target = requested_slot;
    } else if (!private_slot) {
        if (!auto_detach) return SampleEditResult::SharedSlot;
        target = -1;
        for (int pass = 0; pass < 2 && target < 0; ++pass) {
            const int begin = pass == 0 ? 16 : 0;
            const int end = pass == 0 ? SAMPLE_BANK_SIZE : 16;
            for (int slot = begin; slot < end; ++slot) {
                if (sample_slot_refs(slot).total() == 0 && bank.slot(slot).empty()) { target = slot; break; }
            }
        }
        if (target < 0) return SampleEditResult::NoFreeSlot;
    }
    const uint32_t frames = rec.required_capacity_frames();
    const Sample& current = bank.slot(target);
    const uint8_t channels = installed ? chain_audio_channels(out.chain, out.unit_slot) : 1;
    if (!channels || (installed && !chain_audio_channels(out.chain))) return SampleEditResult::InvalidSlot;
    const bool needs_commit = target != rec.sample_slot || current.channels != channels ||
                              current.logical_frames == UINT32_MAX || current.num_frames() != frames;
    if (needs_commit && !bank.can_replace(target, (size_t)frames * channels)) return SampleEditResult::MemoryBudget;
    out.channels = channels;
    out.rec = &rec;
    out.original_slot = rec.sample_slot;
    out.target_slot = target;
    out.target_generation = bank.generation(target);
    out.original_generation = bank.generation(rec.sample_slot);
    out.take_generation = rec.take_generation();
    out.session_token = rec.session_token();
    out.state_token = rec.state_token();
    if (valid_source) {
        const auto& original = bank.slot(rec.sample_slot);
        out.original_data = original.data.data(); out.original_elements = original.data.size();
        out.original_logical_frames = original.logical_frames;
        out.original_channels = original.channels;
    }
    out.target_channels = current.channels;
    out.target_data = current.data.data(); out.target_elements = current.data.size();
    out.target_logical_frames = current.logical_frames;
    out.recorded_frames = rec.recorded_frames();
    out.frames = frames;
    out.needs_commit = needs_commit;
    return SampleEditResult::Ok;
}

trackr::synth::SampleEditResult Engine::plan_chain_record(int chain, RecWorkspacePlan* out, int& count) const {
    using trackr::synth::SampleEditResult;
    count = 0;
    if (chain < 0 || chain >= NUM_CHAINS || !out) return SampleEditResult::InvalidSlot;
    bool claimed[trackr::synth::SAMPLE_BANK_SIZE] = {};
    for (Unit* unit : chains[chain].units) {
        if (!unit || unit->kind != UnitKind::Recorder) continue;
        auto* rec = static_cast<RecUnit*>(unit);
        auto result = plan_recorder(*rec, true, out[count]);
        if (result != SampleEditResult::Ok) return result;
        if (claimed[out[count].target_slot]) {
            // Two aliased REC cards planned before either commit need separate targets.
            out[count] = {};
            int target = -1;
            auto& bank = trackr::synth::SampleBank::instance();
            for (int pass = 0; pass < 2 && target < 0; ++pass) {
                const int begin = pass == 0 ? 16 : 0;
                const int end = pass == 0 ? trackr::synth::SAMPLE_BANK_SIZE : 16;
                for (int slot = begin; slot < end; ++slot)
                    if (!claimed[slot] && sample_slot_refs(slot).total() == 0 && bank.slot(slot).empty()) { target = slot; break; }
            }
            if (target < 0) return SampleEditResult::NoFreeSlot;
            result = plan_recorder(*rec, false, out[count], target);
            if (result != SampleEditResult::Ok) return result;
        }
        claimed[out[count].target_slot] = true;
        ++count;
    }
    return SampleEditResult::Ok;
}

bool Engine::recorder_plan_current(const RecWorkspacePlan& plan) const {
    if (!plan.rec) return false;
    // An installed unit must still be installed before dereferencing its address.
    // Uninstalled plans are for controller-owned new units; caller owns lifetime.
    if (plan.chain >= 0 && (plan.chain >= NUM_CHAINS || plan.unit_slot < 0 || plan.unit_slot >= UNITS_PER_CHAIN ||
        chains[(int)plan.chain].units[(int)plan.unit_slot] != plan.rec)) return false;
    return plan.rec->kind == UnitKind::Recorder && plan.rec->session_token() == plan.session_token &&
        plan.rec->state_token() == plan.state_token && plan.rec->take_generation() == plan.take_generation &&
        plan.rec->recorded_frames() == plan.recorded_frames &&
        plan.channels == (plan.chain >= 0 ? chain_audio_channels(plan.chain, plan.unit_slot) : 1) &&
        (plan.chain < 0 || chain_audio_channels(plan.chain));
}

trackr::synth::SampleEditResult Engine::validate_recorder_plan(const RecWorkspacePlan& plan) const {
    using namespace trackr::synth;
    if (!recorder_plan_current(plan) || recorder_workspace_busy(*this, *plan.rec) ||
        (plan.needs_commit && plan.rec->take_needs_save() && !plan.discard_unsaved)) return SampleEditResult::Busy;
    const SampleBank& bank = SampleBank::instance();
    if (plan.target_slot < 0 || plan.target_slot >= SAMPLE_BANK_SIZE ||
        plan.rec->sample_slot != plan.original_slot || plan.rec->required_capacity_frames() != plan.frames ||
        bank.generation(plan.original_slot) != plan.original_generation ||
        bank.generation(plan.target_slot) != plan.target_generation ||
        sample_slot_refs(plan.target_slot, plan.rec).total() != 0) return SampleEditResult::SharedSlot;
    if (plan.original_slot >= 0 && plan.original_slot < SAMPLE_BANK_SIZE) {
        const auto& original = bank.slot(plan.original_slot);
        if (original.data.data() != plan.original_data || original.data.size() != plan.original_elements ||
            original.logical_frames != plan.original_logical_frames || original.channels != plan.original_channels) return SampleEditResult::SharedSlot;
    }
    const auto& target = bank.slot(plan.target_slot);
    if (target.data.data() != plan.target_data || target.data.size() != plan.target_elements ||
        target.logical_frames != plan.target_logical_frames || target.channels != plan.target_channels) return SampleEditResult::SharedSlot;
    if (plan.target_slot != plan.original_slot && !target.empty()) return SampleEditResult::SharedSlot;
    if (plan.needs_commit && (plan.staged.channels != plan.channels || plan.staged.num_frames() != plan.frames ||
        plan.staged.logical_frames != 0)) return SampleEditResult::InvalidSlot;
    return SampleEditResult::Ok;
}

trackr::synth::SampleEditResult Engine::commit_chain_record(RecWorkspacePlan* plans, int count) {
    using namespace trackr::synth;
    if (!plans || count < 0 || count > UNITS_PER_CHAIN) return SampleEditResult::InvalidSlot;
    SampleBank& bank = SampleBank::instance();
    int target_slots[UNITS_PER_CHAIN];
    Sample staged[UNITS_PER_CHAIN];
    int replacement_count = 0;
    // Full token/ref validation and one aggregate memory projection happen before
    // the first live swap, so a chain never lands in a half-retargeted state.
    for (int i = 0; i < count; ++i) {
        auto& plan = plans[i];
        const auto result = validate_recorder_plan(plan);
        if (result != SampleEditResult::Ok) return result;
        for (int j = 0; j < i; ++j)
            if (plans[j].target_slot == plan.target_slot) return SampleEditResult::SharedSlot;
    }
    for (int i = 0; i < count; ++i) {
        auto& plan = plans[i];
        if (plan.needs_commit) {
            target_slots[replacement_count] = plan.target_slot;
            staged[replacement_count] = std::move(plan.staged);
            ++replacement_count;
        }
    }
    if (!bank.can_replace_many(target_slots, staged, replacement_count)) {
        // Restore staged ownership because callers may inspect/retry the plans.
        int replacement = 0;
        for (int i = 0; i < count; ++i)
            if (plans[i].needs_commit) plans[i].staged = std::move(staged[replacement++]);
        return SampleEditResult::MemoryBudget;
    }
    // Restore each staged vector and use the already validated single-plan commit.
    // can_replace_many guarantees no later MemoryBudget failure.
    int replacement = 0;
    for (int i = 0; i < count; ++i)
        if (plans[i].needs_commit) plans[i].staged = std::move(staged[replacement++]);
    // Commit is structurally no-fail after aggregate preflight: swap every PCM
    // first, then publish all REC ownership changes. Retired vectors stay in the
    // plans and are destroyed by the controller after releasing its guard.
    for (int i = 0; i < count; ++i) {
        auto& plan = plans[i];
        if (plan.needs_commit) {
            bank.swap_sample_unchecked(plan.target_slot, plan.staged, SampleOrigin::Runtime, "");
            plan.rec->adopt_workspace();
        }
    }
    for (int i = 0; i < count; ++i) plans[i].rec->sample_slot = plans[i].target_slot;
    return SampleEditResult::Ok;
}

trackr::synth::SampleEditResult Engine::stage_recorder(RecWorkspacePlan& plan) {
    if (!plan.rec || plan.target_slot < 0) return trackr::synth::SampleEditResult::InvalidSlot;
    if (!plan.needs_commit) return trackr::synth::SampleEditResult::Ok;
    return RecUnit::stage_workspace(plan.frames, plan.staged, plan.channels);
}

trackr::synth::SampleEditResult Engine::commit_recorder(RecWorkspacePlan& plan) {
    using namespace trackr::synth;
    const auto result = validate_recorder_plan(plan);
    if (result != SampleEditResult::Ok) return result;
    SampleBank& bank = SampleBank::instance();
    if (plan.needs_commit) {
        if (!bank.can_replace(plan.target_slot, plan.staged.data.capacity())) return SampleEditResult::MemoryBudget;
        bank.swap_sample_unchecked(plan.target_slot, plan.staged, SampleOrigin::Runtime, "");
        plan.rec->adopt_workspace();
    }
    plan.rec->sample_slot = plan.target_slot;
    return SampleEditResult::Ok;
}

trackr::synth::SampleEditResult Engine::commit_recorder_restart(RecWorkspacePlan& plan, int chain, int slot,
                                                               LaunchQuantize quantize) {
    using namespace trackr::synth;
    if (quantize > LaunchQuantize::Bar || chain < 0 || chain >= NUM_CHAINS || slot < 0 || slot >= UNITS_PER_CHAIN ||
        plan.chain != chain || plan.unit_slot != slot) return SampleEditResult::InvalidSlot;
    // No data is changed until ownership, consent, staged shape, memory projection
    // and command acceptance are all known to succeed under this same guard.
    if (pending_recorder_command_.valid || pending_rec_start || pending_rec_stop) return SampleEditResult::Busy;
    const auto valid = validate_recorder_plan(plan);
    if (valid != SampleEditResult::Ok) return valid;
    if (plan.rec->take_needs_save() && !plan.discard_unsaved) return SampleEditResult::Busy;
    SampleBank& bank = SampleBank::instance();
    const Sample& target = plan.needs_commit ? plan.staged : bank.slot(plan.target_slot);
    if (!target.has_storage() || target.channels != plan.channels || target.num_frames() != plan.frames ||
        target.logical_frames == UINT32_MAX || !plan.frames) return SampleEditResult::InvalidSlot;
    if (plan.needs_commit && !bank.can_replace(plan.target_slot, plan.staged.data.capacity())) return SampleEditResult::MemoryBudget;
    // After this point only non-failing swaps/scalars. Retired PCM lives in plan.
    if (plan.needs_commit) bank.swap_sample_unchecked(plan.target_slot, plan.staged, SampleOrigin::Runtime, "");
    plan.rec->sample_slot = plan.target_slot;
    plan.rec->clear_take();
    accept_recorder_command(chain, slot, RecorderCommand::Start, quantize);
    return SampleEditResult::Ok;
}

trackr::synth::SampleEditResult Engine::prepare_recorder(RecUnit& rec, bool auto_detach) {
    RecWorkspacePlan plan;
    auto result = plan_recorder(rec, auto_detach, plan);
    if (result != trackr::synth::SampleEditResult::Ok) return result;
    result = stage_recorder(plan);
    if (result != trackr::synth::SampleEditResult::Ok) return result;
    return commit_recorder(plan);
}

trackr::synth::SampleEditResult Engine::retarget_recorder(RecUnit& rec, int sample_slot) {
    RecWorkspacePlan plan;
    auto result = plan_recorder(rec, false, plan, sample_slot);
    if (result != trackr::synth::SampleEditResult::Ok) return result;
    result = stage_recorder(plan);
    if (result != trackr::synth::SampleEditResult::Ok) return result;
    return commit_recorder(plan);
}

trackr::synth::SampleEditResult Engine::clear_recorder(RecUnit& rec) {
    using trackr::synth::SampleEditResult;
    RecWorkspacePlan plan;
    auto result = plan_recorder(rec, true, plan);
    if (result == SampleEditResult::Ok) result = stage_recorder(plan);
    if (result != SampleEditResult::Ok) return result;
    plan.discard_unsaved = true; // explicit clear, not an implicit retarget/prepare
    result = commit_recorder(plan);
    if (result != SampleEditResult::Ok) return result;
    rec.clear_take();
    return SampleEditResult::Ok;
}

Unit* Engine::replace_unit(int chain, int slot, Unit* u) {
    if (chain < 0 || chain >= NUM_CHAINS || slot < 0 || slot >= UNITS_PER_CHAIN)
        { delete u; return nullptr; }
    if(!pent_install_valid(u) || !can_install_unit(u,chains[chain].units[slot]))return nullptr; // caller retains refused candidate
    pent_render_source_[chain] = nullptr;
    cancel_relic_gesture();
    cancel_param_temp();
    cancel_scratch_gesture();
    invalidate_structural_undo();
    release_sequence_step_locks();
    release_sequence_pitch(chain);
    sequence_hit_[chain] = {};
    clear_arp_runtime(chain);
    clear_grain_launch_requests(chain);
    if (u && u->kind == UnitKind::Grain) static_cast<GrainUnit*>(u)->clear_launch_requests();
    Unit* old = chains[chain].units[slot];
    // Detach the momentary UI request while the old module is still owned and
    // valid. A later editor capture cannot safely reach a parked/deleted unit.
    // Independent HOLD, saved DAMAGE and audio history remain untouched.
    if(old && old!=u && old->kind==UnitKind::Fract)
        static_cast<FractUnit*>(old)->set_manual_grab(false);
    if(old && old!=u && old->kind==UnitKind::Scratch) {
        auto* scratch=static_cast<ScratchUnit*>(old);scratch->reset();scratch->engine_=nullptr;scratch->chain_=scratch->slot_=-1;
    }
    if(old && old!=u && old->kind==UnitKind::Paul) {
        auto* p=static_cast<PaulUnit*>(old);p->reset();p->engine_=nullptr;p->chain_=p->slot_=-1;
    }
    if(old && old!=u && old->kind==UnitKind::Rule){auto* r=static_cast<RuleUnit*>(old);r->reset();r->engine_=nullptr;r->chain_=r->slot_=-1;}
    if(old && old!=u && old->kind==UnitKind::Rot){auto* r=static_cast<RotUnit*>(old);r->reset();r->engine_=nullptr;r->chain_=r->slot_=-1;}
    chains[chain].units[slot] = u;
    bind_pitch_scale(u);
    if(auto* r=bind_rot(chain,slot))r->reset();
    if(auto* r=bind_rule(chain,slot))r->reset();
    if(auto* p=bind_paul(chain,slot))p->reset();
    if(u && u->kind==UnitKind::Scratch) {
        auto* scratch=static_cast<ScratchUnit*>(u);scratch->reset();
        scratch->engine_=this;scratch->chain_=int8_t(chain);scratch->slot_=int8_t(slot);
    }
    if (u && u != old) initialize_module_random(*u);
    return old;
}

namespace {
struct SlotReferenceMap {
    int8_t mapped[UNITS_PER_CHAIN] = {0, 1, 2, 3, 4, 5};
    int8_t dropped = -1;
};

SlotReferenceMap insertion_slot_map(int slot, int empty) {
    SlotReferenceMap map;
    for (int index = slot; index < empty; ++index) map.mapped[index] = (int8_t)(index + 1);
    return map;
}

SlotReferenceMap removal_slot_map(int slot) {
    SlotReferenceMap map;
    map.dropped = (int8_t)slot;
    for (int index = slot + 1; index < UNITS_PER_CHAIN; ++index)
        map.mapped[index] = (int8_t)(index - 1);
    return map;
}

int moved_slot_index(int slot, int from, int to) {
    if (slot == from) return to;
    if (to < from && slot >= to && slot < from) return slot + 1;
    if (to > from && slot > from && slot <= to) return slot - 1;
    return slot;
}

SlotReferenceMap movement_slot_map(int from, int to) {
    SlotReferenceMap map;
    for (int slot = 0; slot < UNITS_PER_CHAIN; ++slot)
        map.mapped[slot] = (int8_t)moved_slot_index(slot, from, to);
    return map;
}

void remap_pattern_slots(Pattern& pattern, int chain, const SlotReferenceMap& map) {
    for (int step = 0; step < SEQ_STEPS; ++step) {
        SeqStep& sequence = pattern.steps[chain][step];
        int out = 0;
        const int count = sequence.lock_count > SEQ_LOCKS ? SEQ_LOCKS : sequence.lock_count;
        for (int lock = 0; lock < count; ++lock) {
            StepLock value = sequence.locks[lock];
            if (value.slot == map.dropped) continue;
            if (value.slot < UNITS_PER_CHAIN) value.slot = (uint8_t)map.mapped[value.slot];
            sequence.locks[out++] = value;
        }
        sequence.lock_count = (uint8_t)out;
        while (out < SEQ_LOCKS) sequence.locks[out++] = {};
    }
}

void remap_macro_routes(MacroPad* pads, int chain, const SlotReferenceMap& map) {
    for (int pad_index = 0; pad_index < 8; ++pad_index) {
        MacroPad& pad = pads[pad_index];
        int out = 0;
        const int count = pad.route_count > MacroPad::MAX_ROUTES
            ? MacroPad::MAX_ROUTES : pad.route_count;
        for (int route = 0; route < count; ++route) {
            MacroPadRoute value = pad.routes[route];
            if (value.chain == chain && value.slot == map.dropped) continue;
            if (value.chain == chain && value.slot < UNITS_PER_CHAIN)
                value.slot = (uint8_t)map.mapped[value.slot];
            pad.routes[out++] = value;
        }
        pad.route_count = (uint8_t)out;
        while (out < MacroPad::MAX_ROUTES) pad.routes[out++] = {};
    }
}
} // namespace

int Engine::insert_unit(int chain, int slot, Unit* unit) {
    if (!unit || chain < 0 || chain >= NUM_CHAINS || slot < 0 ||
        slot >= UNITS_PER_CHAIN) return -1;
    int empty = -1;
    for (int index = slot; index < UNITS_PER_CHAIN; ++index) {
        if (!chains[chain].units[index]) { empty = index; break; }
    }
    if (empty < 0 || !pent_install_valid(unit) || !can_install_unit(unit)) return -1;
    pent_render_source_[chain] = nullptr;
    cancel_relic_gesture();
    invalidate_structural_undo();
    release_sequence_pitch(chain);
    clear_sequence_runtime();
    clear_arp_runtime(chain);
    clear_grain_launch_requests(chain);
    if (unit->kind == UnitKind::Grain) static_cast<GrainUnit*>(unit)->clear_launch_requests();
    for (int index = empty; index > slot; --index)
        chains[chain].units[index] = chains[chain].units[index - 1];
    chains[chain].units[slot] = unit;
    bind_pitch_scale(unit);
    rebind_rules(chain);rebind_rots(chain);
    if(unit->kind==UnitKind::Rule || unit->kind==UnitKind::Rot)unit->reset();
    initialize_module_random(*unit);

    const SlotReferenceMap reference_map = insertion_slot_map(slot, empty);
    remap_pattern_slots(pattern, chain, reference_map);
    for (int bank = 0; bank < NUM_PATTERNS; ++bank)
        remap_pattern_slots(pattern_banks[bank], chain, reference_map);

    for (int which = 0; which < 2; ++which) {
        PersistentScene& scene = scenes[chain][which];
        for (int index = empty; index > slot; --index) {
            scene.kind[index] = scene.kind[index - 1];
            for (int param = 0; param < MAX_PARAMS; ++param)
                scene.base[index][param] = scene.base[index - 1][param];
        }
        scene.kind[slot] = unit->kind;
        for (int param = 0; param < MAX_PARAMS; ++param)
            scene.base[slot][param] = param < unit->num_params ? unit->params[param].base : 0;
    }
    remap_macro_routes(macro_pads, chain, reference_map);
    return empty;
}

int Engine::move_unit_before(int chain, int from_slot, int before_slot) {
    if (chain < 0 || chain >= NUM_CHAINS || from_slot < 0 ||
        from_slot >= UNITS_PER_CHAIN || before_slot < 0 ||
        before_slot > UNITS_PER_CHAIN || from_slot == before_slot ||
        !chains[chain].units[from_slot] ||
        (before_slot < UNITS_PER_CHAIN && !chains[chain].units[before_slot])) return -1;

    // UNITS_PER_CHAIN is the trailing insertion boundary, including sparse
    // legacy chains. No new Unit is constructed; all references follow below.
    int to_slot = before_slot;
    if (before_slot == UNITS_PER_CHAIN) {
        to_slot = UNITS_PER_CHAIN - 1;
        while (to_slot > from_slot && !chains[chain].units[to_slot]) --to_slot;
    } else if (from_slot < before_slot) --to_slot;
    if (to_slot == from_slot) return from_slot;
    cancel_relic_gesture();
    if (!structural_edit_in_progress_) invalidate_structural_undo();

    release_sequence_pitch(chain);
    clear_sequence_runtime();
    clear_arp_runtime(chain);
    clear_grain_launch_requests(chain);
    Unit* moved = chains[chain].units[from_slot];
    if (to_slot < from_slot) {
        for (int slot = from_slot; slot > to_slot; --slot)
            chains[chain].units[slot] = chains[chain].units[slot - 1];
    } else {
        for (int slot = from_slot; slot < to_slot; ++slot)
            chains[chain].units[slot] = chains[chain].units[slot + 1];
    }
    chains[chain].units[to_slot] = moved;
    rebind_rules(chain);rebind_rots(chain);

    const SlotReferenceMap reference_map = movement_slot_map(from_slot, to_slot);
    remap_pattern_slots(pattern, chain, reference_map);
    for (int bank = 0; bank < NUM_PATTERNS; ++bank)
        remap_pattern_slots(pattern_banks[bank], chain, reference_map);

    for (int which = 0; which < 2; ++which) {
        PersistentScene& scene = scenes[chain][which];
        const UnitKind moved_kind = scene.kind[from_slot];
        q15 moved_base[MAX_PARAMS];
        for (int param = 0; param < MAX_PARAMS; ++param)
            moved_base[param] = scene.base[from_slot][param];
        if (to_slot < from_slot) {
            for (int slot = from_slot; slot > to_slot; --slot) {
                scene.kind[slot] = scene.kind[slot - 1];
                for (int param = 0; param < MAX_PARAMS; ++param)
                    scene.base[slot][param] = scene.base[slot - 1][param];
            }
        } else {
            for (int slot = from_slot; slot < to_slot; ++slot) {
                scene.kind[slot] = scene.kind[slot + 1];
                for (int param = 0; param < MAX_PARAMS; ++param)
                    scene.base[slot][param] = scene.base[slot + 1][param];
            }
        }
        scene.kind[to_slot] = moved_kind;
        for (int param = 0; param < MAX_PARAMS; ++param)
            scene.base[to_slot][param] = moved_base[param];
    }

    remap_macro_routes(macro_pads, chain, reference_map);
    return to_slot;
}

Unit* Engine::remove_unit(int chain, int slot) {
    if (chain < 0 || chain >= NUM_CHAINS || slot < 0 ||
        slot >= UNITS_PER_CHAIN || !chains[chain].units[slot]) return nullptr;
    cancel_relic_gesture();
    if (!structural_edit_in_progress_) invalidate_structural_undo();
    release_sequence_pitch(chain);
    clear_sequence_runtime();
    clear_arp_runtime(chain);
    clear_grain_launch_requests(chain);
    Unit* removed = chains[chain].units[slot];
    if(removed->kind==UnitKind::Scratch) {
        auto* scratch=static_cast<ScratchUnit*>(removed);scratch->reset();scratch->engine_=nullptr;scratch->chain_=scratch->slot_=-1;
    }
    if(removed->kind==UnitKind::Paul) {
        auto* p=static_cast<PaulUnit*>(removed);p->reset();p->engine_=nullptr;p->chain_=p->slot_=-1;
    }
    for (int index = slot; index + 1 < UNITS_PER_CHAIN; ++index)
        chains[chain].units[index] = chains[chain].units[index + 1];
    if(removed->kind==UnitKind::Rot){auto* r=static_cast<RotUnit*>(removed);r->reset();r->engine_=nullptr;r->chain_=r->slot_=-1;}
    if(removed->kind==UnitKind::Rule){auto* r=static_cast<RuleUnit*>(removed);r->reset();r->engine_=nullptr;r->chain_=r->slot_=-1;}
    chains[chain].units[UNITS_PER_CHAIN - 1] = nullptr;
    rebind_rules(chain);rebind_rots(chain);

    const SlotReferenceMap reference_map = removal_slot_map(slot);
    remap_pattern_slots(pattern, chain, reference_map);
    for (int bank = 0; bank < NUM_PATTERNS; ++bank)
        remap_pattern_slots(pattern_banks[bank], chain, reference_map);

    for (int which = 0; which < 2; ++which) {
        PersistentScene& scene = scenes[chain][which];
        for (int index = slot; index + 1 < UNITS_PER_CHAIN; ++index) {
            scene.kind[index] = scene.kind[index + 1];
            for (int param = 0; param < MAX_PARAMS; ++param)
                scene.base[index][param] = scene.base[index + 1][param];
        }
        scene.kind[UNITS_PER_CHAIN - 1] = UnitKind::Empty;
        for (int param = 0; param < MAX_PARAMS; ++param)
            scene.base[UNITS_PER_CHAIN - 1][param] = 0;
    }

    remap_macro_routes(macro_pads, chain, reference_map);
    return removed;
}

void Engine::invalidate_structural_undo() {
    if (!structural_undo_) return;
    structural_undo_->valid = false;
    structural_undo_->chain = -1;
    structural_undo_->selected_slot = -1;
    if (structural_undo_->retained && !structural_undo_retired_) {
        structural_undo_retired_ = structural_undo_->retained;
        structural_undo_->retained = nullptr;
    }
}

void Engine::reclaim_structural_undo() {
    delete structural_undo_retired_;
    structural_undo_retired_ = nullptr;
}

void Engine::discard_structural_undo() {
    invalidate_structural_undo();
    reclaim_structural_undo();
}

bool Engine::structural_undo_available() const {
    return structural_undo_ && structural_undo_->valid;
}

void Engine::capture_structural_undo(int chain, int selected_slot) {
    invalidate_structural_undo();
    if (!structural_undo_ || structural_undo_->retained) return;
    StructuralUndo& undo = *structural_undo_;
    undo.valid = true;
    undo.chain = (int8_t)chain;
    undo.selected_slot = (int8_t)selected_slot;
    for (int slot = 0; slot < UNITS_PER_CHAIN; ++slot) undo.units[slot] = chains[chain].units[slot];
    for (int step = 0; step < SEQ_STEPS; ++step) {
        undo.steps[step] = pattern.steps[chain][step];
        undo.microtiming[step] = pattern.microtiming[chain][step];
        for (int bank = 0; bank < NUM_PATTERNS; ++bank) {
            undo.bank_steps[bank][step] = pattern_banks[bank].steps[chain][step];
            undo.bank_microtiming[bank][step] = pattern_banks[bank].microtiming[chain][step];
        }
    }
    undo.scenes[0] = scenes[chain][0];
    undo.scenes[1] = scenes[chain][1];
    for (int pad = 0; pad < 8; ++pad) undo.macro_pads[pad] = macro_pads[pad];
}

int Engine::move_unit_before_with_undo(int chain, int from_slot, int before_slot) {
    if (chain < 0 || chain >= NUM_CHAINS || from_slot < 0 || from_slot >= UNITS_PER_CHAIN ||
        before_slot < 0 || before_slot > UNITS_PER_CHAIN || from_slot == before_slot ||
        !chains[chain].units[from_slot] ||
        (before_slot < UNITS_PER_CHAIN && !chains[chain].units[before_slot])) return -1;
    int final_slot = before_slot - (from_slot < before_slot ? 1 : 0);
    if (before_slot == UNITS_PER_CHAIN)
        while (final_slot > from_slot && !chains[chain].units[final_slot]) --final_slot;
    if (final_slot == from_slot) return from_slot;
    capture_structural_undo(chain, from_slot);
    if (!structural_undo_available()) return -1;
    structural_edit_in_progress_ = true;
    final_slot = move_unit_before(chain, from_slot, before_slot);
    structural_edit_in_progress_ = false;
    if (final_slot < 0) invalidate_structural_undo();
    return final_slot;
}

bool Engine::remove_unit_with_undo(int chain, int slot) {
    if (chain < 0 || chain >= NUM_CHAINS || slot < 0 || slot >= UNITS_PER_CHAIN ||
        !chains[chain].units[slot]) return false;
    Unit* unit = chains[chain].units[slot];
    if (unit->kind == UnitKind::Recorder) static_cast<RecUnit*>(unit)->stop();
    capture_structural_undo(chain, slot);
    if (!structural_undo_available()) return false;
    structural_edit_in_progress_ = true;
    structural_undo_->retained = remove_unit(chain, slot);
    structural_edit_in_progress_ = false;
    if (!structural_undo_->retained) { invalidate_structural_undo(); return false; }
    return true;
}

bool Engine::undo_structural_edit(int* restored_chain, int* restored_slot) {
    if (!structural_undo_available() || structural_undo_->chain < 0 || structural_undo_->chain >= NUM_CHAINS) return false;
    StructuralUndo& undo = *structural_undo_;
    const int chain = undo.chain;

    // Refuse to overwrite a unit installed after the structural edit. Normal UI
    // replace/paste already invalidates history; this guard covers direct callers.
    for (Unit* current : chains[chain].units) {
        if (!current) continue;
        bool known = false;
        for (Unit* previous : undo.units) if (current == previous) { known = true; break; }
        if (!known) { invalidate_structural_undo(); return false; }
    }

    release_sequence_pitch(chain);
    clear_sequence_runtime();
    clear_arp_runtime(chain);
    clear_grain_launch_requests(chain);
    for (int slot = 0; slot < UNITS_PER_CHAIN; ++slot) {
        chains[chain].units[slot] = undo.units[slot];
        bind_pitch_scale(undo.units[slot]);
        if (undo.units[slot] && undo.units[slot]->kind == UnitKind::Grain)
            static_cast<GrainUnit*>(undo.units[slot])->clear_launch_requests();
    }
    for (int step = 0; step < SEQ_STEPS; ++step) {
        pattern.steps[chain][step] = undo.steps[step];
        pattern.microtiming[chain][step] = undo.microtiming[step];
        for (int bank = 0; bank < NUM_PATTERNS; ++bank) {
            pattern_banks[bank].steps[chain][step] = undo.bank_steps[bank][step];
            pattern_banks[bank].microtiming[chain][step] = undo.bank_microtiming[bank][step];
        }
    }
    scenes[chain][0] = undo.scenes[0];
    scenes[chain][1] = undo.scenes[1];
    for (int pad = 0; pad < 8; ++pad) macro_pads[pad] = undo.macro_pads[pad];
    rebind_rules(chain);rebind_rots(chain);
    if (restored_chain) *restored_chain = chain;
    if (restored_slot) *restored_slot = undo.selected_slot;
    // A deleted unit could not observe sigils while detached. Restoring its
    // identity/route/undo must not fire a stale previously armed edge.
    if (undo.retained) undo.retained->module_random_.armed = false;
    cancel_relic_gesture();
    // The retained deleted unit is installed again and ownership returns to Chain.
    undo.retained = nullptr;
    undo.valid = false;
    undo.chain = -1;
    undo.selected_slot = -1;
    return true;
}

void Engine::trigger_chain(int chain) {
    if (chain == sequence_audition_chain_) cancel_sequence_audition();
    trigger_chain_internal(chain, false);
}

void Engine::trigger_sequence_chain(int chain) {
    trigger_chain(chain);
    if (chain < 0 || chain >= NUM_CHAINS) return;
    // Latch the accepted native interval, not rounded UI BPM or sample LEN.
    // Retriggers renew from their actual timestamp; manual triggers clear it.
    const uint64_t now = clock_runtime_ ? clock_phase_ : transport_frame;
    uint64_t span = clock_runtime_ ? (clock_event_end_[chain] > clock_event_start_[chain]
        ? clock_event_end_[chain] - clock_event_start_[chain] : 1u) : sequence_event_span_[chain];
    if (!span) span = 1;
    // Observation only: accepted native sequence trigs/retrigs enter here;
    // manual pads, live rolls and editor auditions do not. Latch source identity
    // from the accepted queue tuple so transition lookahead cannot change banks.
    if (transport_playing && sequencer_enabled && active_sequence_pass_[chain] &&
        (active_sequence_step_[chain].flags & SeqStep::Trig)) {
        const uint64_t frames = clock_runtime_
            ? (span + clock_increment_ - 1u) / clock_increment_ : span;
        const uint8_t count = active_sequence_step_[chain].retrig_count();
        uint64_t duration = frames * 85u / ((ENGINE_SR / 1000) * 100u * count);
        if (duration < 1) duration = 1;
        if (duration > 140) duration = 140;
        sequence_hit_[chain] = {0, active_sequence_source_step_[chain],
            active_sequence_source_bank_[chain], (uint16_t)duration};
        sequence_hit_audio_frame_[chain] = audio_frame_;
    }
    for (int slot = 0; slot < UNITS_PER_CHAIN; ++slot) {
        Unit* unit = chains[chain].units[slot];
        if(auto* p=bind_paul(chain,slot)) {
            if(p->voice_ && p->mode==PaulUnit::Mode::Gate && !p->bypass && !chains[chain].mute && arp_owner_for(chain,slot)<0) {
                p->sequence_gate_=true;p->manual_remaining_=0;p->gate_phase_=clock_runtime_;
                p->gate_deadline_=now+std::max<uint64_t>(1,(span*PaulUnit::gate_quarters(p->params[PaulUnit::P_GATE].target)+3)/4);
            }
            continue;
        }
        if(unit && unit->kind==UnitKind::Scratch && !unit->bypass && !chains[chain].mute) {
            auto* scratch=static_cast<ScratchUnit*>(unit);
            if(scratch->voice_) {
                scratch->sequence_gate_=true;scratch->manual_remaining_=0;
                scratch->gate_deadline_=now+std::max<uint64_t>(1,(span*ScratchUnit::gate_quarters(scratch->params[ScratchUnit::P_GATE].target)+3)/4);
                scratch->gate_phase_=clock_runtime_;
            }
            continue;
        }
        if (unit && unit->kind == UnitKind::Grain) {
            static_cast<GrainUnit*>(unit)->begin_sequence_gate(now + span, clock_runtime_);
            continue;
        }
        if (!unit || unit->kind != UnitKind::SamplePlayer || arp_owner_for(chain,slot)>=0) continue;
        auto* play = static_cast<PlayUnit*>(unit);
        if (!play->modern_ || !play->playing_ || !play->latched_gate_) continue;
        uint64_t duration = (span * play->latched_gate_quarters_ + 3u) / 4u;
        if (!duration) duration = 1;
        play->gate_deadline_ = now + duration;
        play->gate_phase_ = clock_runtime_;
    }
}

void Engine::release_play_gates() {
    for (auto& chain : chains) for (Unit* unit : chain.units)
        if (unit && unit->kind == UnitKind::SamplePlayer)
            static_cast<PlayUnit*>(unit)->release_sequence_gate();
        else if (unit && unit->kind == UnitKind::Grain)
            static_cast<GrainUnit*>(unit)->release_sequence_gate();
        else if(unit && unit->kind==UnitKind::Scratch)
            static_cast<ScratchUnit*>(unit)->release_sequence_gate();
        else if(unit && unit->kind==UnitKind::Paul)
            static_cast<PaulUnit*>(unit)->release_sequence_gate();
}

void Engine::clear_grain_launch_requests(int chain) {
    for(int slot=0;slot<UNITS_PER_CHAIN;++slot) {
        Unit* unit=chains[chain].units[slot];
        if(auto* p=bind_paul(chain,slot))p->reset();
        else if (unit && unit->kind == UnitKind::Grain)
            static_cast<GrainUnit*>(unit)->clear_launch_requests();
        else if(unit && unit->kind==UnitKind::Scratch) {
            auto* scratch=static_cast<ScratchUnit*>(unit);scratch->reset();
            scratch->engine_=this;scratch->chain_=int8_t(chain);scratch->slot_=int8_t(slot);
        }
    }
}

uint64_t Engine::next_play_gate_deadline() {
    uint64_t next = UINT64_MAX;
    for (auto& chain : chains) for (Unit* unit : chain.units) {
        if(unit && unit->kind==UnitKind::Paul) {
            auto* p=static_cast<PaulUnit*>(unit);p->synchronize();
            if(p->gate_deadline_!=UINT64_MAX && (!transport_playing || !sequencer_enabled))p->release_sequence_gate();
            if(p->gate_deadline_!=UINT64_MAX) {
                const uint64_t now=p->gate_phase_?clock_phase_:transport_frame;
                if(p->gate_deadline_<=now)p->release_sequence_gate();
                else next=std::min(next,p->gate_phase_?clock_frame_for_phase(p->gate_deadline_):p->gate_deadline_);
            }
            continue;
        }
        if(unit && unit->kind==UnitKind::Scratch) {
            auto* scratch=static_cast<ScratchUnit*>(unit);scratch->synchronize();
            if(scratch->gate_deadline_!=UINT64_MAX && (!transport_playing || !sequencer_enabled))scratch->release_sequence_gate();
            if(scratch->gate_deadline_!=UINT64_MAX) {
                const uint64_t now=scratch->gate_phase_?clock_phase_:transport_frame;
                if(scratch->gate_deadline_<=now)scratch->release_sequence_gate();
                else {
                    const uint64_t frame=scratch->gate_phase_?clock_frame_for_phase(scratch->gate_deadline_):scratch->gate_deadline_;
                    if(frame<next)next=frame;
                }
            }
            continue;
        }
        if (unit && unit->kind == UnitKind::Grain) {
            auto* gran = static_cast<GrainUnit*>(unit);
            gran->synchronize_performance();
            if (unit->bypass || (gran->clean && !gran->scan_source_matches()))
                gran->clear_launch_requests();
            if (!transport_playing || !sequencer_enabled) gran->release_sequence_gate();
            if (gran->gate_deadline_ != UINT64_MAX) {
                const uint64_t now = gran->gate_phase_ ? clock_phase_ : transport_frame;
                if (gran->gate_deadline_ <= now) gran->release_sequence_gate();
                else {
                    const uint64_t frame = gran->gate_phase_
                        ? clock_frame_for_phase(gran->gate_deadline_) : gran->gate_deadline_;
                    if (frame < next) next = frame;
                }
            }
            continue;
        }
        if (!unit || unit->kind != UnitKind::SamplePlayer) continue;
        auto* play = static_cast<PlayUnit*>(unit);
        if (play->gate_deadline_ == UINT64_MAX) continue;
        const uint64_t now = play->gate_phase_ ? clock_phase_ : transport_frame;
        if (!transport_playing || play->gate_deadline_ <= now) {
            play->release_sequence_gate();
            continue;
        }
        const uint64_t frame = play->gate_phase_ ? clock_frame_for_phase(play->gate_deadline_) : play->gate_deadline_;
        if (frame < next) next = frame;
    }
    return next;
}

void Engine::trigger_chain_internal(int chain, bool audible_only) {
    if (chain < 0 || chain >= NUM_CHAINS) return;
    // FOLLOW is bound to the same destination chain used by STEP. Resetting here
    // gives a deterministic zero-phase edge before PLAY publishes its live cursor.
    for (int sigil = 0; sigil < NUM_SIGILS; ++sigil)
        if (sigils[sigil].mode == SigilMode::Follow && step_dest[sigil] == chain)
            sigils[sigil].follow(0, false);
    bool rot_recording_checked = false, rot_recording = false;
    for (int slot=0; slot<UNITS_PER_CHAIN; ++slot) {
        Unit* unit=chains[chain].units[slot];
        if (unit && unit->kind == UnitKind::Rot && !transport_playing) {
            if (!rot_recording_checked) {
                rot_recording = recording_active_or_pending();
                rot_recording_checked = true;
            }
            if (rot_recording) continue; // refuse only ROT stopped manual preview
        }
        // Recording is an explicit REC/STOP command. A musical chain trigger must
        // never erase a finalized take just because the chain still contains REC.
        if (!unit || unit->kind == UnitKind::Relic || unit->kind == UnitKind::Recorder || (audible_only && unit->bypass)) continue;
        if (unit->kind==UnitKind::Arp && (unit->bypass || chains[chain].mute)) continue;
        if(auto* r=bind_rule(chain,slot))for(int p=0;p<r->num_params;++p)r->params[p].resolve(parameter_modulation_value(r,p));
        if(auto* r=bind_rot(chain,slot))for(int p=0;p<r->num_params;++p)r->params[p].resolve(parameter_modulation_value(r,p));
        if(auto* p=bind_paul(chain,slot)) {
            for(int i=0;i<p->num_params;++i)p->params[i].resolve(parameter_modulation_value(p,i));
        }
        if (arp_trigger_owner_for(chain,slot)>=0) continue;
        if(unit->kind==UnitKind::Scratch) {
            auto* scratch=static_cast<ScratchUnit*>(unit);
            scratch->engine_=this;scratch->chain_=int8_t(chain);scratch->slot_=int8_t(slot);
            scratch->synchronize();
            for(int p=0;p<unit->num_params;++p)scratch->resolve_param(p,parameter_modulation_value(unit,p));
        }
        if (unit->kind==UnitKind::Vca) update_clock_unit(unit);
        unit->trigger();
    }
}

void Engine::control_step() {
    // 1) sigils step in order; earlier sigils may bend later sigils' rates.
    // FOLLOW takes its phase from the first audible PLAY in its destination chain.
    for (int i = 0; i < NUM_SIGILS; ++i) {
        if (sigils[i].mode == SigilMode::Follow) {
            bool active = false;
            uint32_t phase = 0;
            const int chain = step_dest[i];
            if (chain >= 0 && chain < NUM_CHAINS && !chains[chain].mute) {
                for (Unit* unit : chains[chain].units) {
                    if (!unit || unit->kind != UnitKind::SamplePlayer || unit->bypass) continue;
                    const auto* play = static_cast<const PlayUnit*>(unit);
                    if (play->playback_active()) {
                        active = true;
                        phase = play->playback_phase();
                    }
                    break;
                }
            }
            sigils[i].follow(phase, active);
        }
        // Free, unmodulated increments are cached by setters/reset. Sync and
        // live rate routes alone need control-rate division; leaving FOLLOW
        // reactivates its zero increment on the next tick.
        if (sigil_sync[i] || sigil_mods[i].src != ModSource::None ||
            !sigils[i].phase_inc || sigils[i].mode == SigilMode::Follow)
            refresh_sigil_rate(i);
        int prev_step = sigils[i].current_step();
        step_sigil(i, true);
        // Steps mode: crossing into a new step fires triggers into its destination chain
        if (sigils[i].mode == SigilMode::Steps && sigil_config[i].enabled &&
            (sigils[i].drawn || (!sigil_config[i].legacy && sigil_config[i].shape != SigilShape::Drawn))) {
            int now_step = sigils[i].current_step();
            const q15 gate = sigil_config[i].legacy ? sigils[i].table[now_step * 8 + 4] : sample_sigil_shape(i,sigils[i].phase);
            if (now_step != prev_step && gate > 4096)
                trigger_chain(step_dest[i]);
        }
    }

    // All eight sources consumed the coherent previous tempo above. Publish only
    // now: a synced sigil feeding tempo has one control-tick causal feedback.
    update_clock_modulation();

    // Action routes consume edges even when muted/bypassed/recording-blocked.
    // Only persistent bases change; ordinary resolve below retains every override.
    step_module_random();

    // Validate only the four bounded pitch records before normal resolution.
    // Bypass/source precedence and live domain edits must not revive a stale note.
    for (int chain = 0; chain < NUM_CHAINS; ++chain)
        if (sequence_pitch_owner_[chain].unit)
            validate_sequence_pitch(chain, resolve_sequence_source(chain));

    // 2) resolve all unit params + let units recompute coefficients
    for (auto& c : chains) {
        for (auto* u : c.units) {
            if (!u || u->kind == UnitKind::Relic) continue;
            bind_pitch_scale(u);
            ScratchUnit* scratch=u->kind==UnitKind::Scratch?static_cast<ScratchUnit*>(u):nullptr;
            if(scratch)scratch->synchronize(); // invalid latch never resolves one stale control tick
            for (int p = 0; p < u->num_params; ++p) {
                Param& pr = u->params[p];
                q15 mv = parameter_modulation_value(u,p);
                if(scratch)scratch->resolve_param(p,mv);else pr.resolve(mv);
            }
            u->control_tick();
            update_clock_unit(u);
        }
    }
}

bool Engine::play_editor_busy() const {
    return recording_active_or_pending();
}
uint32_t Engine::start_play_preview(int chain, int slot, uint32_t first, uint32_t end,
                                    bool repeat, bool reverse, uint16_t speed_q8, bool scrub, int8_t part_pitch) {
    if (chain < 0 || chain >= NUM_CHAINS || slot < 0 || slot >= UNITS_PER_CHAIN || play_editor_busy() ||
        !play_part_pitch::valid(part_pitch) || speed_q8 < trackr::synth::Sample::CUT_SPEED_MIN ||
        speed_q8 > trackr::synth::Sample::CUT_SPEED_MAX) return 0;
    const Unit* u = chains[chain].units[slot];
    if (!u || u->kind != UnitKind::SamplePlayer || u->bypass || chains[chain].mute) return 0;
    const auto& source = *static_cast<const PlayUnit*>(u);
    if (source.sample_slot < 0 || source.sample_slot >= trackr::synth::SAMPLE_BANK_SIZE || !source.edit_source_matches()) return 0;
    stop_bounce_preview(); stop_sample_preview(); stop_play_preview();
    play_preview_.set_modern(source.modern());
    play_preview_.sample_slot = source.sample_slot;
    play_preview_.pitch_quantize = source.pitch_quantize;
    bind_pitch_scale(&play_preview_);
    play_preview_.set_pitch_mode(source.pitch_mode());
    play_preview_.restore_edit_state({});
    if (source.edit_source_matches())
        play_preview_.set_edit_state(source.edit_state(), trackr::synth::SampleBank::instance().generation(source.sample_slot));
    for (int p = 0; p < PlayUnit::P_COUNT; ++p) {
        play_preview_.params[p].base = source.modern() || p < PlayUnit::P_POSITION ?
            (p == PlayUnit::P_NOTE ? source.params[p].target : source.params[p].effective) : 0;
        play_preview_.params[p].snap();
    }
    play_preview_generation_ = trackr::synth::SampleBank::instance().generation(source.sample_slot);
    if (!play_preview_.preview_frames(first, end, repeat, play_preview_generation_, reverse, speed_q8, scrub, part_pitch)) return 0;
    play_preview_owner_ = u; play_preview_chain_ = chain; play_preview_slot_ = slot;
    if (!++play_preview_token_) ++play_preview_token_;
    return play_preview_token_;
}
void Engine::stop_play_preview(uint32_t token) {
    if (token && token != play_preview_token_) return;
    if (token && play_preview_owner_) { play_preview_.release(); return; }
    play_preview_.reset(); play_preview_owner_ = nullptr;
    play_preview_chain_ = play_preview_slot_ = -1;
}
bool Engine::play_preview_valid() const {
    if (!play_preview_owner_ || play_preview_chain_ < 0 || play_preview_chain_ >= NUM_CHAINS ||
        play_preview_slot_ < 0 || play_preview_slot_ >= UNITS_PER_CHAIN || play_editor_busy()) return false;
    const Unit* unit = chains[play_preview_chain_].units[play_preview_slot_];
    if (unit != play_preview_owner_ || !unit || unit->kind != UnitKind::SamplePlayer ||
        unit->bypass || chains[play_preview_chain_].mute) return false;
    const auto& source = *static_cast<const PlayUnit*>(unit);
    return source.sample_slot == play_preview_.sample_slot && source.modern() == play_preview_.modern() &&
        source.pitch_mode() == play_preview_.pitch_mode() && source.edit_source_matches() &&
        trackr::synth::SampleBank::instance().generation(source.sample_slot) == play_preview_generation_ &&
        play_preview_.source_matches(play_preview_.voice_source_);
}
bool Engine::play_preview_active(uint32_t token) const {
    return token && token == play_preview_token_ && play_preview_valid() && play_preview_.playback_active();
}
bool Engine::update_play_preview(uint32_t token, uint32_t first, uint32_t end,
                                 bool reverse, uint16_t rate, int8_t part_pitch) {
    return play_preview_active(token) && play_preview_.update_preview_frames(first, end, reverse, rate, part_pitch);
}
uint32_t Engine::play_preview_position(uint32_t token) const {
    return play_preview_active(token) ? play_preview_.visual_source_frame() : 0;
}
void Engine::process_play_preview(q15* out, int frames) {
    if (!play_preview_owner_) return;
    if (!play_preview_valid()) { stop_play_preview(); return; }
    const Unit* u = chains[play_preview_chain_].units[play_preview_slot_];
    q15 mono[CONTROL_INTERVAL];
    for (int done = 0; done < frames;) {
        const int n = std::min(frames - done, CONTROL_INTERVAL);
        const auto& source = *static_cast<const PlayUnit*>(u);
        for (int p : {PlayUnit::P_SPEED, PlayUnit::P_GAIN, PlayUnit::P_POSITION, PlayUnit::P_SCRUB, PlayUnit::P_HOLD})
            play_preview_.params[p].effective = source.modern() || p < PlayUnit::P_POSITION ? source.params[p].effective : 0;
        play_preview_.params[PlayUnit::P_NOTE].target = source.params[PlayUnit::P_NOTE].target;
        play_preview_.params[PlayUnit::P_NOTE].effective = source.params[PlayUnit::P_NOTE].effective;
        play_preview_.pitch_quantize = source.pitch_quantize;
        play_preview_.control_tick();
        play_preview_.process(mono, n);
        for (int i = 0; i < n; ++i) {
            const q31 v = (q31)mono[i] * 24576 >> 15;
            out[(done+i)*2] = trackr::fx::sat_q15((q31)out[(done+i)*2] + v);
            out[(done+i)*2+1] = trackr::fx::sat_q15((q31)out[(done+i)*2+1] + v);
        }
        done += n;
    }
}
void Engine::swap_sample_preview(trackr::synth::Sample& staged) {
    if (recording_active_or_pending()) return;
    stop_bounce_preview();
    stop_play_preview();
    using std::swap;
    swap(sample_preview_, staged);
    sample_preview_frame_ = 0;
    sample_preview_amp_ = 0;
    sample_preview_active_ = sample_preview_.playable_frames() >= 2 &&
        (sample_preview_.channels == 1 || sample_preview_.channels == 2);
}

void Engine::stop_sample_preview() {
    sample_preview_active_ = false;
    sample_preview_frame_ = 0;
    sample_preview_amp_ = 0;
}

void Engine::process_sample_preview(q15* out, int frames) {
    if (!sample_preview_active_ || !out || frames <= 0) return;
    if (recording_active_or_pending()) { stop_sample_preview(); return; }
    const uint32_t total = sample_preview_.playable_frames();
    const int channels = sample_preview_.channels;
    if (total < 2 || (channels != 1 && channels != 2)) {
        stop_sample_preview();
        return;
    }
    constexpr uint32_t FADE = 96;
    for (int i = 0; i < frames; ++i) {
        if (sample_preview_frame_ >= total) {
            stop_sample_preview();
            return;
        }
        if (sample_preview_amp_ < trackr::fx::Q15_ONE) {
            int next = sample_preview_amp_ + 768;
            sample_preview_amp_ = (q15)(next > trackr::fx::Q15_ONE ? trackr::fx::Q15_ONE : next);
        }
        q15 sample;
        const size_t index = (size_t)sample_preview_frame_ * channels;
        if (channels == 2)
            sample = (q15)(((int32_t)sample_preview_.data[index] + sample_preview_.data[index + 1]) >> 1);
        else sample = sample_preview_.data[index];
        q15 edge = sample_preview_amp_;
        const uint32_t remain = total - sample_preview_frame_;
        if (remain < FADE) {
            const q15 tail = (q15)((remain * trackr::fx::Q15_ONE) / FADE);
            if (tail < edge) edge = tail;
        }
        // Audition sits below full project level and enters the normal master FX /
        // limiter path. Saturation here keeps concurrent project playback bounded.
        const q31 audition = (q31)(((int64_t)sample * edge * 24576) >> 30);
        const int stereo = i * 2;
        out[stereo] = trackr::fx::sat_q15((q31)out[stereo] + audition);
        out[stereo + 1] = trackr::fx::sat_q15((q31)out[stereo + 1] + audition);
        ++sample_preview_frame_;
    }
}

bool Engine::set_transition_fx(TransitionFxKind kind, int amount) {
    return transition_fx_.set(kind, amount, effective_bpm_q16());
}
void Engine::release_transition_fx() { transition_fx_.release(); }

// Master-bus performance FX: post-mix, pre-limiter, allocation-free.
// Retain the original 32-frame law: at audio samples 0,32,64,... take
// floor((target-current)/8), with a signed one-count minimum. At 32kHz
// the large-signal e-fold time is -1ms/log(7/8) = 7.49ms (not 4ms).
// This private clock advances even at idle, independent of render/event splits.
void Engine::process_master_fx(q15* out, int frames) {
    using trackr::fx::q31;
    while (frames > 0) {
        const bool tick = master_fx_countdown_ == 0;
        if (tick) master_fx_countdown_ = CONTROL_INTERVAL;
        const int chunk = std::min(frames, master_fx_countdown_);
        bool any = false;
        for (int t = 0; t < (int)GlobalFxTarget::Count; ++t) {
            const q31 target = master_fx_target_[t];
            q31 current = master_fx_current_[t];
            if (tick && current != target) {
                q31 step = (target - current) >> 3;
                if (step == 0) step = target > current ? 1 : -1;
                current += step;
                master_fx_current_[t] = (q15)current;
            }
            if (master_fx_current_[t]) any = true;
        }
        const q15 cutoff_amount = master_fx_current_[(int)GlobalFxTarget::Cutoff];
        const q15 res_amount    = master_fx_current_[(int)GlobalFxTarget::Resonance];
        const q15 crush_amount  = master_fx_current_[(int)GlobalFxTarget::Crush];
        const q15 drive_amount  = master_fx_current_[(int)GlobalFxTarget::Drive];
        if (!any) {
            master_svf_l_.reset();
            master_svf_r_.reset();
            master_crush_hold_ = 0;
            out += chunk * 2;
            frames -= chunk;
            master_fx_countdown_ -= chunk;
            continue;
        }

        const bool filter_on = cutoff_amount > 0 || res_amount > 0;
        if (filter_on) {
            // Cutoff sweeps DOWN as the gesture deepens (classic performance filter):
            // 0 -> open (bypass-ish), full -> ~500. Resonance rides on top.
            const q15 cutoff = (q15)(30000 - ((q31)(30000 - 1500) * cutoff_amount >> 15));
            const q15 resonance = (q15)((q31)26000 * res_amount >> 15);
            master_svf_l_.set_params(cutoff, resonance);
            master_svf_r_.set_params(cutoff, resonance);
            master_svf_l_.type = trackr::dsp::FilterType::LPF;
            master_svf_r_.type = trackr::dsp::FilterType::LPF;
        }

        // Crush: sample-rate reduction 1..32x hold. Depth curve favors the
        // audible low end of the sweep.
        const int crush_step = crush_amount > 0 ? 1 + ((q31)crush_amount * 31 >> 15) : 0;
        // Drive: pre-gain 1x..4x into the soft clipper. The largest gain is
        // 131065; both gain and signed PCM products are widened BEFORE multiply.
        const q31 drive_gain = 32767 + q31((int64_t(drive_amount) * 3 * 32767) >> 15);

        for (int i = 0; i < chunk; ++i) {
            q31 l = out[i * 2];
            q31 r = out[i * 2 + 1];
            if (crush_step > 1) {
                if (master_crush_hold_ <= 0) {
                    master_crush_l_ = (q15)l;
                    master_crush_r_ = (q15)r;
                    master_crush_hold_ = crush_step;
                }
                --master_crush_hold_;
                l = master_crush_l_;
                r = master_crush_r_;
            }
            if (filter_on) {
                l = master_svf_l_.step((q15)l);
                r = master_svf_r_.step((q15)r);
            }
            if (drive_amount > 0) {
                l = trackr::fx::soft_clip_q31(q31((int64_t(l) * drive_gain) >> 15));
                r = trackr::fx::soft_clip_q31(q31((int64_t(r) * drive_gain) >> 15));
            }
            out[i * 2]     = trackr::fx::sat_q15(l);
            out[i * 2 + 1] = trackr::fx::sat_q15(r);
        }
        out += chunk * 2;
        frames -= chunk;
        master_fx_countdown_ -= chunk;
    }
}

void Engine::set_chain_volume_smooth(int chain, q15 volume) {
    if (chain<0 || chain>=NUM_CHAINS) return;
    if (volume<0) volume=0;
    Chain& c=chains[chain]; VolumeRamp& ramp=volume_ramp_[chain];
    if (volume==c.volume) return;
    q15 current=c.volume;
    if (ramp.remaining && ramp.target==c.volume)
        current=(q15)(ramp.target+((int32_t)ramp.start-ramp.target)*ramp.remaining/128);
    ramp.start=current; ramp.target=volume; ramp.remaining=128;
    c.volume=volume;
}

uint8_t Engine::chain_audio_channels(int chain, int before_slot) const {
    if (chain < 0 || chain >= NUM_CHAINS || before_slot < 0 || before_slot > UNITS_PER_CHAIN) return 0;
    uint8_t channels = 1;
    for (int slot = 0; slot < before_slot; ++slot) {
        const Unit* u = chains[chain].units[slot];
        if (!u) continue;
        if (u->kind == UnitKind::Relic ||
            (u->kind == UnitKind::Pent && uint8_t(static_cast<const PentUnit*>(u)->width) > 1)) return 0;
        // These three processors own their bypass crossfade; REC captures even
        // bypassed. All other bypassed cards really leave the pipe untouched.
        if (u->bypass && u->kind != UnitKind::Recorder && u->kind != UnitKind::Comp &&
            u->kind != UnitKind::Mouth && u->kind != UnitKind::Glitch) continue;
        const uint8_t next = u->output_channels(channels);
        if ((next != 1 && next != 2) || (next == 2 && !u->supports_stereo()) ||
            (channels == 2 && next == 1 && !u->sequencer_properties().source)) return 0;
        channels = next;
    }
    return channels;
}

void Engine::render(q15* out, int frames) {
    using trackr::fx::q31;
    const bool bounce_audition = bounce_preview_active();
    if (bounce_preview_active_ && !bounce_audition) stop_bounce_preview();
    int done = 0;
    while (done < frames) {
        if (pending_recorder_command_.valid && !recorder_command_current(pending_recorder_command_)) cancel_pending_recorder();
        // Chain audition is explicitly stopped-transport only. A transport or
        // recording transition relinquishes it before chain/REC processing;
        // ordinary sequencer triggers (preview_=false) are never released here.
        const bool recording_preview_guard = play_editor_busy();
        if (transport_playing || recording_preview_guard) {
            for (auto& chain : chains) for (Unit* unit : chain.units)
                if (unit && unit->kind == UnitKind::SamplePlayer) {
                    auto* play = static_cast<PlayUnit*>(unit);
                    if (play->preview_active()) {
                        if (recording_preview_guard) play->reset(); else play->release();
                    }
                } else if (recording_preview_guard && unit && unit->kind == UnitKind::Rot) {
                    // Reuse this existing bounded fragment scan. Covers public
                    // REC writers and bypassed/unsupported ROT pipes too, before
                    // any reader/capture. Ordinary musical tape stays untouched.
                    auto* rot = static_cast<RotUnit*>(unit);
                    if (rot->audition_) rot->reset();
                }
        }
        if(scratch_gesture_.token && !scratch_gesture_active(scratch_gesture_.token))cancel_scratch_gesture();
        if (temp_chain_ >= 0 && (!param_temp_active() ||
            !param_temp_allowed(temp_chain_, temp_slot_, temp_param_))) cancel_param_temp();
        if (sequence_audition_chain_ >= 0 &&
            (audio_frame_ >= sequence_audition_end_frame_ ||
             !sequence_audition_allowed(sequence_audition_chain_)))
            cancel_sequence_audition();
        if (pending_recorder_command_.valid && transport_playing &&
            transport_frame >= pending_recorder_command_.deadline)
            fire_pending_rec((uint8_t)(1u << pending_recorder_command_.chain));
        if (control_countdown_ <= 0) {
            control_step();
            control_countdown_ = CONTROL_INTERVAL;
        }
        update_arps(); // after resolved locks/mods, before the first sample of a note
        int n = frames - done;
        if (n > control_countdown_) n = control_countdown_;
        n = arp_render_limit(n);
        if (sequence_audition_chain_ >= 0 &&
            sequence_audition_end_frame_ - audio_frame_ < (uint64_t)n)
            n = (int)(sequence_audition_end_frame_ - audio_frame_);
        const uint64_t gate_deadline = next_play_gate_deadline();
        if (gate_deadline > transport_frame && gate_deadline - transport_frame < (uint64_t)n)
            n = (int)(gate_deadline - transport_frame);
        const uint64_t deadline = next_sequence_deadline();
        if (deadline > transport_frame && deadline - transport_frame < (uint64_t)n)
            n = (int)(deadline - transport_frame);
        if (pending_recorder_command_.valid && pending_recorder_command_.deadline > transport_frame &&
            pending_recorder_command_.deadline - transport_frame < (uint64_t)n)
            n = (int)(pending_recorder_command_.deadline - transport_frame);

        if (clock_runtime_ && live_retrigger_active_ && clock_roll_phase_ > clock_audio_phase_) {
            const uint64_t roll_frames = (clock_roll_phase_ - clock_audio_phase_ + clock_increment_ - 1) / clock_increment_;
            if (roll_frames < (uint64_t)n) n = (int)roll_frames;
        } else if (!clock_runtime_ && live_retrigger_active_) {
            // Legacy audio-frame deadlines have their own truncated-fpb law.
            // Fire overdue work before samples, then stop exactly at the next
            // deadline. The phase/q32 scheduler and its event order stay intact.
            fire_live_retriggers();
            const uint64_t roll_frames = live_retrigger_next_frame_ - audio_frame_;
            if (roll_frames < (uint64_t)n) n = (int)roll_frames;
        }

        // clear the stereo block
        std::memset(out + done * 2, 0, (size_t)n * 2 * sizeof(q15));

        for (int ci = 0; ci < NUM_CHAINS; ++ci) {
            Chain& c = chains[ci];
            // meter decay (~1ms per block): time constant ~64ms, drains to zero
            { q15 d = (q15)(chain_peak[ci] >> 6); chain_peak[ci] -= d ? d : (chain_peak[ci] ? 1 : 0); }
            const bool muted = c.mute;
            bool any = false;
            uint8_t channels = 1;
            std::memset(c.scratch, 0, sizeof(c.scratch));
            std::memset(c.stereo, 0, sizeof(c.stereo));
            pent_render_source_[ci] = nullptr;
            c.rendered_channels = chain_audio_channels(ci);
            // Refuse before advancing ANY audio processor/writer. Control-rate
            // modulation remains alive; no unprocessed stereo can escape to mix.
            if (!c.rendered_channels) {
                for (auto* u : c.units) if (u && u->kind == UnitKind::Recorder) static_cast<RecUnit*>(u)->stop();
                if (ci == env_chain_) env_value_ = q15(env_value_ - (env_value_ >> 6));
                chain_peak[ci] = 0;
                continue;
            }
            auto process = [&](Unit* u) {
                const uint8_t next = u->output_channels(channels);
                if (next == 2) {
                    if (channels == 1) for (int i = 0; i < n; ++i) c.stereo[i*2] = c.stereo[i*2+1] = c.scratch[i];
                    if (!u->process_stereo(c.stereo, n)) return false;
                } else u->process(c.scratch, n); // explicit mono source OVERWRITE, not folding
                channels = next;
                if (u->kind == UnitKind::Pent) {
                    pent_render_source_[ci] = u;
                    pent_render_width_[ci] = uint8_t(static_cast<const PentUnit*>(u)->width);
                } else if (pent_render_source_[ci] && u->sequencer_properties().source) {
                    pent_render_source_[ci] = nullptr; // a later overwrite source wins
                }
                return true;
            };
            for (auto* u : c.units) {
                if (!u) continue;
                if (u->kind == UnitKind::Recorder) {
                    auto* recorder = static_cast<RecUnit*>(u);
                    // Capture follows transport even when the card is bypassed;
                    // bypass only skips REC's output monitor behavior.
                    if (u->bypass) recorder->capture_input_channels(channels == 2 ? c.stereo : c.scratch, n, channels);
                    else if (!process(u)) { c.rendered_channels = 0; break; }
                    any = true;
                    continue;
                }
                if(u->kind==UnitKind::Rot){for(int s=0;s<UNITS_PER_CHAIN;++s)if(c.units[s]==u)bind_rot(ci,s)->synchronize();}
                if(u->kind==UnitKind::Rule){for(int s=0;s<UNITS_PER_CHAIN;++s)if(c.units[s]==u)bind_rule(ci,s)->synchronize();}
                if(u->kind==UnitKind::Paul) {
                    for(int s=0;s<UNITS_PER_CHAIN;++s)if(c.units[s]==u)bind_paul(ci,s)->synchronize();
                }
                if(u->kind==UnitKind::Scratch) {
                    auto* scratch=static_cast<ScratchUnit*>(u);
                    scratch->engine_=this;scratch->chain_=int8_t(ci);
                    for(int s=0;s<UNITS_PER_CHAIN;++s)if(c.units[s]==u)scratch->slot_=int8_t(s);
                    scratch->synchronize();
                }
                if (u->kind == UnitKind::Fract) {
                    auto* fract = static_cast<FractUnit*>(u);
                    fract->set_enabled(!u->bypass && !muted);
                    if (u->bypass || muted) continue;
                    // Supply the exact applied transport phase at this block's
                    // first sample. FRACT decides internally at sample edges,
                    // without extra engine splits or sequencer trigger ownership.
                    if (clock_runtime_)
                        fract->set_transport_clock(transport_playing, clock_phase_, clock_increment_);
                    else {
                        const uint32_t fpb = frames_per_beat();
                        const uint64_t scaled = uint64_t(frame_in_beat) * CLOCK_BEAT;
                        fract->set_transport_clock(transport_playing,
                            uint64_t(beat_count) * CLOCK_BEAT + scaled / fpb,
                            uint32_t(CLOCK_BEAT / fpb), uint32_t(scaled % fpb), fpb);
                    }
                }
                if (u->kind == UnitKind::Glitch) {
                    auto* glitch = static_cast<GlitchUnit*>(u);
                    glitch->set_enabled(!u->bypass && !muted);
                    if (muted) continue;
                    // Same applied phase and legacy carry as FRACT; source triggers,
                    // global clock and engine block splits remain untouched.
                    if (clock_runtime_)
                        glitch->set_transport_clock(transport_playing, clock_phase_, clock_increment_);
                    else {
                        const uint32_t fpb = frames_per_beat();
                        const uint64_t scaled = uint64_t(frame_in_beat) * CLOCK_BEAT;
                        glitch->set_transport_clock(transport_playing,
                            uint64_t(beat_count) * CLOCK_BEAT + scaled / fpb,
                            uint32_t(CLOCK_BEAT / fpb), uint32_t(scaled % fpb), fpb);
                    }
                }
                // COMP/MOUTH keep warm state; GLITCH invalidates PCM and releases
                // only its last scalar sample. All own their bypass dry crossfade.
                if (u->bypass && u->kind != UnitKind::Comp && u->kind != UnitKind::Mouth && u->kind != UnitKind::Glitch) {
                    if (u->kind == UnitKind::Osc) static_cast<OscUnit*>(u)->set_visual_capture(false);
                    continue;
                }
                if (!process(u)) { c.rendered_channels = 0; break; }
                if (u->arp_voice.owner>=0) {
                    // One millisecond source edge, before any downstream effect.
                    // Q15 unity is 32768, so an open gate is bit-identical.
                    for (int i=0;i<n;++i) {
                        int gain=u->arp_voice.gain;
                        gain=u->arp_voice.gate ? std::min(32768,gain+1024) : std::max(0,gain-1024);
                        u->arp_voice.gain=uint16_t(gain);
                        if (channels == 2) for (int ch = 0; ch < 2; ++ch)
                            c.stereo[i*2+ch]=q15((int32_t(c.stereo[i*2+ch])*gain)>>15);
                        else c.scratch[i]=q15((int32_t(c.scratch[i])*gain)>>15);
                    }
                }
                any = true;
            }
            if (!c.rendered_channels) {
                std::memset(c.scratch, 0, sizeof c.scratch); std::memset(c.stereo, 0, sizeof c.stereo);
                for (auto* u : c.units) if (u && u->kind == UnitKind::Recorder) static_cast<RecUnit*>(u)->stop();
                chain_peak[ci] = 0; continue;
            }
            c.rendered_channels = channels;
            const q15* tap = channels == 2 ? c.stereo : c.scratch;
            // Selected post-module/pre-volume tap; mute/empty releases to zero.
            if (ci == env_chain_) {
                q31 peak = 0;
                if (any && !muted) for (int i = 0; i < n * channels; ++i) {
                    q31 a = tap[i] < 0 ? -tap[i] : tap[i];
                    if (a > peak) peak = a;
                }
                const q31 delta = peak - env_value_;
                if (env_response_ == EnvResponse::Fast) {
                    if (delta > 0) env_value_ = (q15)(env_value_ + ((delta * 3) >> 2));
                    else           env_value_ = (q15)(env_value_ + (delta >> 6));
                } else {
                    // Body 1/8 attack, 1/128 release; Slow 1/32 and 1/256.
                    // Fixed shifts only: no audio-rate divisions or allocations.
                    const int shift = delta > 0 ? (env_response_ == EnvResponse::Body ? 3 : 5)
                                                : (env_response_ == EnvResponse::Body ? 7 : 8);
                    env_value_ = (q15)(env_value_ + (delta >> shift));
                }
            }

            // Advance an explicitly requested fader ramp even while muted/empty.
            // No UI tick dependence, allocations, or change to the idle gain path.
            VolumeRamp& ramp=volume_ramp_[ci];
            if (ramp.remaining && ramp.target!=c.volume) ramp.remaining=0;
            const bool ramping=ramp.remaining!=0;
            q15 gains[CONTROL_INTERVAL];
            if (ramping) for(int i=0;i<n;++i) {
                if(ramp.remaining)--ramp.remaining;
                gains[i]=(q15)(ramp.target+((int32_t)ramp.start-ramp.target)*ramp.remaining/128);
            }
            if (!any || muted) continue;

            // per-chain peak meter (post-volume, pre-master-sum)
            {
                q31 pk = 0;
                for (int i = 0; i < n; ++i) {
                    for (int ch = 0; ch < channels; ++ch) {
                        q31 s = ((q31)tap[i * channels + ch] * (ramping ? gains[i] : c.volume)) >> 15;
                        if (s < 0) s = -s;
                        if (s > pk) pk = s;
                    }
                }
                if (pk > chain_peak[ci]) chain_peak[ci] = (q15)(pk > 32767 ? 32767 : pk);
            }

            q31 lgain = c.volume;
            q31 rgain = c.volume;
            if (c.pan > 0)      lgain = (lgain * (32767 - c.pan)) / 32767;
            else if (c.pan < 0) rgain = (rgain * (32767 + c.pan)) / 32767;

            for (int i = 0; i < n; ++i) {
                if (ramping) {
                    lgain=rgain=gains[i];
                    if (c.pan>0) lgain=(lgain*(32767-c.pan))/32767;
                    else if(c.pan<0) rgain=(rgain*(32767+c.pan))/32767;
                }
                q31 l_s = ((q31)tap[i * channels] * lgain) >> 15;
                q31 r_s = ((q31)tap[i * channels + (channels == 2 ? 1 : 0)] * rgain) >> 15;
                q31 l = (q31)out[(done + i) * 2]     + l_s;
                q31 r = (q31)out[(done + i) * 2 + 1] + r_s;
                out[(done + i) * 2]     = trackr::fx::soft_clip_q31(l);
                out[(done + i) * 2 + 1] = trackr::fx::soft_clip_q31(r);
            }
        }

        // Browser audition is deliberately independent from project topology and
        // is mixed only after all chains, before the shared master FX/limiter.
        process_sample_preview(out + done * 2, n);
        process_play_preview(out + done * 2, n);

        // Existing master FX, independent temporary transition bus, then limiter.
        // The subsequent published scope therefore hears the actual gestures.
        process_master_fx(out + done * 2, n);
        transition_fx_.process(out + done * 2, n);

        // Master peak limiter. It shares one gain envelope across stereo,
        // attacks immediately, releases slowly, and performs no allocation.
        q31 block_peak = 1;
        for (int i = 0; i < n * 2; ++i) {
            q31 a = out[done * 2 + i]; if (a < 0) a = -a;
            if (a > block_peak) block_peak = a;
        }
        constexpr q31 LIMIT = 30000;
        q15 target_gain = block_peak > LIMIT ? (q15)((LIMIT << 15) / block_peak) : trackr::fx::Q15_ONE;
        if (target_gain < master_gain_) master_gain_ = target_gain;
        uint16_t visual_peak = 0;
        for (int i = 0; i < n; ++i) {
            if (master_gain_ < target_gain) {
                int32_t step = ((int32_t)target_gain - master_gain_) >> 9;
                master_gain_ = (q15)(master_gain_ + (step ? step : 1));
            }
            out[(done + i) * 2] = trackr::fx::sat_q15(((q31)out[(done + i) * 2] * master_gain_) >> 15);
            out[(done + i) * 2 + 1] = trackr::fx::sat_q15(((q31)out[(done + i) * 2 + 1] * master_gain_) >> 15);
            // Piggyback on the existing limiter loop: no extra PCM traversal,
            // allocation, lock or change to the audio samples/control boundaries.
            const int l = out[(done + i) * 2], r = out[(done + i) * 2 + 1];
            visual_peak = (uint16_t)std::max<int>(visual_peak,
                std::min(32767, std::max(l < 0 ? -l : l, r < 0 ? -r : r)));
        }
        // Keep ordinary clocks/DSP advancing, but a stopped take audition owns
        // the final output exclusively: raw stereo (or duplicated mono), with
        // no second pass through project chains, master FX or the limiter.
        if (bounce_audition) {
            process_bounce_preview(out + done * 2, n);
            visual_peak = 0;
            for (int i = 0; i < n * 2; ++i) {
                const int value = out[done * 2 + i];
                visual_peak = (uint16_t)std::max<int>(visual_peak,
                    std::min(32767, value < 0 ? -value : value));
            }
        }
        // ~46ms release at 44.1kHz keeps short attacks visible between UI reads.
        const unsigned decay = (unsigned(master_peak_) * unsigned(n) + 2047) / 2048;
        master_peak_ = std::max<uint16_t>(visual_peak,
            decay >= master_peak_ ? 0 : uint16_t(master_peak_ - decay));

        // Final master stereo or selected chain's still-intact post-module scratch.
        // Preview/master processing uses separate buffers, never chain scratch.
        // Overlap-copy only: no new render boundaries or control/DSP timing changes.
        capture_bounce(out + done * 2, n);

        // master scope: every 8th frame of the summed output (left channel)
        for (int i = 0; i < n; ++i) {
            if (++scope_decim_ >= 8) {
                scope_decim_ = 0;
                scope[scope_pos] = out[(done + i) * 2];
                scope_pos = (scope_pos + 1) % SCOPE_LEN;
            }
        }
        done += n;
        audio_frame_ += n;
        if (clock_runtime_) clock_audio_phase_ += (uint64_t)clock_increment_ * n;
        if (sequence_audition_chain_ >= 0 && audio_frame_ >= sequence_audition_end_frame_)
            cancel_sequence_audition();
        control_countdown_ -= n;
        advance_transport(n);
        fire_live_retriggers();
    }
}

#include "bounce_impl.inc"

} // namespace seance

#include "arp_engine_impl.inc"
