// SNCL21 admits RULE33 actual/retained scene kinds, RULEv1 per actual card.
// RULE faults are refused in a full read-only preflight before ANY factory.
// No-RULE save version and all former chunks retain their exact bytes.
// SNCK20 admits PENT32 actual/retained scene kinds. PENTv1 contains only
// {chain,slot,width}; required for every real card, never empty for scene-only.
// CORE.extra/UNIT.flags stay zero; all twelve unipolar bases use CORE/USIG.
// SNCI version18 is required only for clean authored GRAN reading nondefaults.
// GRNX v1: u8 version1/count1..24, then {chain,slot,reading,reverse_percent}.
// UNIT bit4 exactly matches sparse records. Runtime mixed phase is never stored.
// Default/dormant50 old/off saves retain their exact prior container and bytes.
// SNCH version17 carries append-only PAUL30. All-forward PAUL v1 stays exact:
// {chain,slot,mode}. Any nonforward card requires PAUL v2 {chain,slot,mode,direction}
// for EVERY card (Forward=0,Reverse=1,PingPong=2). Source=CORE.extra (<64), flags=0.
// DSP/FFT/RNG/gates/scan/loop leg/focus are runtime-only. Non-PAUL saves stay exact.
// Pre17 CORE and SCNE PAUL kinds reject, including incomplete containers.
// SNCG version16 also carries append-only SCRT29: pre-SCRT readers reject CORE's
// unknown nonempty kind. SCRT v1 keeps {chain,slot,mode,map}; custom axes require
// SCRT v2 {chain,slot,mode,map,axis_x,axis_y} for every card (-1 encoded as255).
// No latch/gesture/gate/read-head state is serialized. Non-SCRT saves stay unchanged.
// SNCG version16 marks seven-family INST/modal; pre16 INST is rejected, not migrated.
// SNCE version14 marks reciprocal TILT; Clear6 and both Legacy5 laws keep old flags.
// SNCF version15 marks extended VCA; old readers must reject new launch/timing modes.
// Relic31 is reserved but retired: actual/scene/typed data refuses transactionally.
// patch_io.cpp - transactional SNC7..20 chunks + SNC1-SNC6 migration.
// SNCD version13 marks extended FOLD; older readers reject instead of losing sound.
// SNCC version12 marks extended RING; older readers reject instead of losing sound.
// SNCB version11 marks extended STRAIN; older readers reject it transactionally.
// SNCA version10 marks extended BIT; all older readers reject it transactionally.
// SNC9 marks extended NOISE; old readers reject rather than silently use old DSP.
// SNC8 uses the same chunks; an explicitly extended MOTOR requires version8
// because old readers do not validate MOTOR UNIT flags and would silently lose it.
//
// SNC7 header: "SNC7", u8 version=7. Chunks are {char id[4], u32 length,
// payload}. Unknown chunks are skipped. Required chunks are CORE, SIGL, ROUT,
// UNIT, SAMP, TRNS, STRN. Optional PTRN stores the 1/16 sequencer pattern.
// CORE stores all 12 ParamBank entries per slot. Legacy SNC1-SNC6 retain their
// historical fixed six-param layout. Optional SDIR stores per-bank, per-chain
// Forward/Reverse/PingPong traversal without changing fixed PTRN/PBNK records.
// Optional ENVR v1 stores global follower chain + response (3 bytes); absent
// chunks migrate to chain 0 / Fast, and malformed/duplicate ENVR rejects load.
// Optional CLCK v1: two Sigil clock routes in physical integer units; no live
// clock state is serialized. Absent means None/0; malformed/duplicate rejects.
// Optional MRND v1: u8 version/count, then count {u8 chain,slot,source}.
// Only assigned A-H routes; max24, unique occupied slots. No RNG/edge/undo state.
// Absent means None; malformed/duplicate chunks or entries reject transactionally.
// Optional SCAL v1: {u8 version=1,tonic0..11,mode0..9}, exactly3 bytes.
// Omit only tonic0/Chromatic; absent restores that default. No format bump,
// UNIT runtime scale or stored-note rewrites. Invalid/duplicate refuses preflight.
#include "patch_io.h"
#include "engine.h"
#include "units.h"
#include "tilt_edit.h"
#include "../synth/wav_loader.h"
#include "../io/file_publication.h"
#include <cstdio>
#include <cstring>
#include <utility>
#include <new>
#include <memory>

namespace seance::patchio {

static constexpr char MAGIC7[4] = { 'S', 'N', 'C', '7' };
static constexpr uint8_t VERSION7 = 7;
static constexpr char MAGIC8[4] = { 'S', 'N', 'C', '8' };
static constexpr char MAGIC9[4] = { 'S', 'N', 'C', '9' };
static constexpr char MAGIC10[4] = { 'S', 'N', 'C', 'A' };
static constexpr char MAGIC11[4] = { 'S', 'N', 'C', 'B' };
static constexpr char MAGIC12[4] = { 'S', 'N', 'C', 'C' };
static constexpr char MAGIC14[4] = { 'S', 'N', 'C', 'E' };
static constexpr char MAGIC13[4] = { 'S', 'N', 'C', 'D' };
static constexpr char MAGIC15[4] = { 'S', 'N', 'C', 'F' };
static constexpr char MAGIC16[4] = { 'S', 'N', 'C', 'G' };
static constexpr char MAGIC17[4] = { 'S', 'N', 'C', 'H' };
static constexpr char MAGIC18[4] = { 'S', 'N', 'C', 'I' };
static constexpr char MAGIC20[4] = { 'S', 'N', 'C', 'K' };
static constexpr char MAGIC21[4] = { 'S', 'N', 'C', 'L' };
static constexpr char MAGIC22[4] = { 'S', 'N', 'C', 'M' };
static constexpr int LEGACY_PARAM_COUNT = 6;
static constexpr int LEGACY_SIGIL_COUNT = 4;
static constexpr uint32_t UNIVERSAL_SIGIL_BYTES = 5 + 8 * 274 + NUM_CHAINS * UNITS_PER_CHAIN * 2;
// File-only PBAY1 migration: no separate LFO runtime/config exists in Engine.
struct LegacyLfo {
    bool active=false, sync=false, unipolar=false;
    uint8_t shape=0, restart=0, division=4;
    uint16_t rate_millihz=1000, phase_degrees=0;
};
static bool legacy_lfo_valid(const LegacyLfo& c) {
    return c.shape < 5 && c.restart < 2 && c.division < 6 && c.rate_millihz >= 50 &&
        c.rate_millihz <= 20000 && c.phase_degrees < 360;
}
static constexpr uint32_t MAX_CHUNK_SIZE = 64u * 1024u;

static uint8_t unit_extra(const Unit* u);
static uint8_t unit_flags(const Unit* u);

struct PatchDocument::Impl {
    struct ParamData { int16_t base = 0; uint8_t src = 0; int16_t depth = 0; bool enabled = true; uint32_t relic_source=0;uint8_t relic_port=0; };
    struct UnitData {
        uint8_t kind = (uint8_t)UnitKind::Empty;
        uint8_t bypass = 0;
        uint8_t random_source = 0;
        uint8_t extra = 0;
        uint8_t flags = 0;
        uint8_t strn_window = 0;
        uint8_t strn_division = 0;
        uint8_t strn_long = 0;
        uint8_t scratch_mode = 1, scratch_map = 2;
        int8_t scratch_axis_x = -1, scratch_axis_y = -1;
        uint8_t paul_mode = 0, paul_direction = 0;
        uint8_t pent_width = 0;
        RuleUnit::Config rule_config;
        RotUnit::Config rot_config;
        bool gran_perform = false;
        uint8_t gran_mode = 0, gran_window = 0, gran_count = 4;
        uint8_t gran_reading = 0, gran_reverse_percent = 50;
        PlayEditState play_edit;
        bool play_position = false, play_hold = false, play_note = false;
        uint8_t play_pitch_mode = 0;
        GlitchUnit::PatternState glitch_pattern;
        ArpUnit::NoteRecall arp_recall;
        ParamData params[PARAM_BANK_SIZE];
    };
    struct ChainData {
        int16_t volume = trackr::fx::Q15_ONE;
        int16_t pan = 0;
        uint8_t mute = 0;
        UnitData units[UNITS_PER_CHAIN];
    };
    struct SigilData {
        SigilConfig config;
        bool drawn = false;
        uint8_t mode = 0;
        int16_t table[SIGIL_POINTS] = {};
        int16_t rate = 0;
        int16_t depth = 0;
        uint8_t mod_src = 0;
        int16_t mod_depth = 0;
        uint8_t sync = 0;
        uint8_t division = 0;
        uint8_t step_dest = 0;
    };

    ChainData chains[NUM_CHAINS];
    SigilData sigils[NUM_SIGILS];
    uint16_t bpm = 120;
    MusicalScale scale{};
    ClockMod clock_mods[2];
    uint8_t env_chain = 0;
    uint8_t env_response = (uint8_t)EnvResponse::Fast;
    SampleManifest::Entry samples[SampleManifest::SLOT_COUNT];
    uint32_t sample_chops[SampleManifest::SLOT_COUNT][trackr::synth::Sample::MAX_CHOPS] = {};
    uint16_t sample_slice_reverse[SampleManifest::SLOT_COUNT] = {};
    uint16_t sample_slice_speed[SampleManifest::SLOT_COUNT][trackr::synth::Sample::MAX_CHOPS] = {};
    int8_t sample_slice_pitch[SampleManifest::SLOT_COUNT][trackr::synth::Sample::MAX_CHOPS] = {};
    bool sample_exact_native[SampleManifest::SLOT_COUNT] = {};
    uint8_t transport_playing = 0;
    uint8_t beats_per_bar = 4;
    uint8_t launch_quantize = 0;
    uint8_t swing_percent = 50;
    uint8_t pattern_launch_quantize = (uint8_t)LaunchQuantize::Bar;
    uint8_t sequencer_enabled = 0;
    Pattern pattern;
    Pattern pattern_banks[NUM_PATTERNS];
    uint8_t active_pattern = 0;
    PatternChainStep pattern_chain[PATTERN_CHAIN_STEPS];
    uint8_t pattern_chain_length = 0;
    uint8_t pattern_chain_loop = 1;
    PersistentScene scenes[NUM_CHAINS][2];
    int16_t scene_morph[NUM_CHAINS] = {};
    MacroPad macro_pads[8];
    PerformanceBinding performance_bindings[NUM_PERFORMANCE_BINDINGS];
    WorkspaceLayout workspace;
};

PatchDocument::PatchDocument() : impl_(new(std::nothrow) Impl()) {
    if (impl_) workspace_layout_defaults(impl_->workspace);
}
PatchDocument::~PatchDocument() { delete impl_; }

// Each optional PLAY extension is required even for scene/lock/macro-only use.
static bool authored_play_params(const Engine& eng, int chain, int slot, const PlayUnit& play,
                                int first, int end, q15 default_base = 0) {
    if (!play.modern()) return false;
    for (int p = first; p < end; ++p) {
        const auto& v = play.params[p];
        if (v.base != default_base || v.src != ModSource::None || v.mod_depth || v.relic_source) return true;
        for (const auto& scene : eng.scenes[chain])
            if (scene.kind[slot] == UnitKind::SamplePlayer && scene.base[slot][p]) return true;
    }
    for (const auto& pad : eng.macro_pads) for (int i = 0; i < pad.route_count; ++i) {
        const auto& r = pad.routes[i];
        if (r.chain == chain && r.slot == slot && r.param >= first && r.param < end) return true;
    }
    auto has_lock = [=](const Pattern& pattern) {
        for (const auto& step : pattern.steps[chain]) for (int i = 0; i < step.lock_count; ++i)
            if (step.locks[i].slot == slot && step.locks[i].param >= first && step.locks[i].param < end) return true;
        return false;
    };
    if (has_lock(eng.pattern)) return true;
    for (const auto& pattern : eng.pattern_banks) if (has_lock(pattern)) return true;
    return false;
}

// REV bit1 is an authored-utilities marker, not merely a nonzero live value.
// Include muted cables, scene-only, macro-only and locks in ALL eight banks.
static bool authored_reverb_utilities(const Engine& eng, int chain, int slot, const ReverbUnit& rev) {
    if (!rev.modern()) return false;
    for (int p = ReverbUnit::P_PREDELAY; p < ReverbUnit::P_COUNT; ++p) {
        const auto& value = rev.params[p];
        if (value.base || value.src != ModSource::None || value.mod_depth || !value.mod_enabled || value.relic_source) return true;
        for (const auto& scene : eng.scenes[chain])
            if (scene.kind[slot] == UnitKind::Reverb && scene.base[slot][p]) return true;
    }
    for (const auto& pad : eng.macro_pads) for (int i = 0; i < pad.route_count; ++i) {
        const auto& route = pad.routes[i];
        if (route.chain == chain && route.slot == slot && route.param >= ReverbUnit::P_PREDELAY &&
            route.param < ReverbUnit::P_COUNT) return true;
    }
    auto has_lock = [=](const Pattern& pattern) {
        for (const auto& step : pattern.steps[chain]) for (int i = 0; i < step.lock_count; ++i) {
            const auto& lock = step.locks[i];
            if (lock.slot == slot && lock.param >= ReverbUnit::P_PREDELAY && lock.param < ReverbUnit::P_COUNT) return true;
        }
        return false;
    };
    if (has_lock(eng.pattern)) return true;
    for (const auto& pattern : eng.pattern_banks) if (has_lock(pattern)) return true;
    return false;
}

// Validate raw public config without constructing a unit, normalizing invalid
// values or resolving against live params. Defaults come from the stable map;
// axis_params() is deliberately not used because it may mask invalid overrides.
static bool scratch_axes_valid(uint8_t mode, uint8_t map, int x, int y) {
    if (mode > 1 || map > 2 || x < -1 || y < -1 ||
        x >= ScratchUnit::P_COUNT || y >= ScratchUnit::P_COUNT) return false;
    int default_x, default_y;
    ScratchUnit::map_params(ScratchUnit::Mode(mode), ScratchUnit::Map(map), default_x, default_y);
    const int resolved_x = x == -1 ? default_x : x;
    const int resolved_y = y == -1 ? default_y : y;
    return ScratchUnit::axis_parameter_available(ScratchUnit::Mode(mode), resolved_x) &&
        ScratchUnit::axis_parameter_available(ScratchUnit::Mode(mode), resolved_y) && resolved_x != resolved_y;
}

static bool rot_bases_valid(const Engine& eng) {
    for(const auto& chain:eng.chains)for(const Unit* u:chain.units)if(u && u->kind==UnitKind::Rot) {
        const auto& r=*static_cast<const RotUnit*>(u);
        if(!RotUnit::valid_config(r.config()) || r.sample_slot<0 || r.sample_slot>=64)return false;
        for(int p=0;p<12;++p)if(r.params[p].base<(p==RotUnit::P_PITCH?-32767:0))return false;
    }
    for(const auto& chain:eng.scenes)for(const auto& sc:chain)for(int s=0;s<UNITS_PER_CHAIN;++s)if(sc.kind[s]==UnitKind::Rot)
        for(int p=0;p<12;++p)if(sc.base[s][p]<(p==RotUnit::P_PITCH?-32767:0))return false;
    return true;
}
static bool rule_bases_valid(const Engine& eng) {
    for(const auto& chain:eng.chains)for(const Unit* u:chain.units)if(u && u->kind==UnitKind::Rule) {
        const auto& r=*static_cast<const RuleUnit*>(u);
        if(!RuleUnit::valid_config(r.config()) || r.sample_slot<0 || r.sample_slot>=64)return false;
        for(int p=0;p<12;++p)if(r.params[p].base<(p==RuleUnit::P_ROOT?-32767:0))return false;
    }
    for(const auto& chain:eng.scenes)for(const auto& sc:chain)for(int s=0;s<UNITS_PER_CHAIN;++s)if(sc.kind[s]==UnitKind::Rule)
        for(int p=0;p<12;++p)if(sc.base[s][p]<(p==RuleUnit::P_ROOT?-32767:0))return false;
    return true;
}
static bool pent_bases_valid(const Engine& eng) {
    for (const auto& chain : eng.chains) for (const Unit* unit : chain.units)
        if (unit && unit->kind == UnitKind::Pent)
            for (const auto& param : unit->params) if (param.base < 0) return false;
    for (const auto& chain : eng.scenes) for (const auto& scene : chain)
        for (int slot = 0; slot < UNITS_PER_CHAIN; ++slot) if (scene.kind[slot] == UnitKind::Pent)
            for (q15 base : scene.base[slot]) if (base < 0) return false;
    return true;
}

bool capture(const Engine& eng, PatchDocument& out, const WorkspaceLayout* workspace) {
    using Impl = PatchDocument::Impl;
    if (!out.impl_ || eng.recording_active_or_pending() || !eng.relic_storage_valid() || !pent_bases_valid(eng) || !rule_bases_valid(eng) || !rot_bases_valid(eng)) return false;
    Impl& document = *out.impl_;
    if (!musical_scale::valid(eng.project_scale())) return false;
    document.scale = eng.project_scale();
    if (workspace && !workspace_layout_valid(*workspace)) return false;
    if (workspace) document.workspace = *workspace;
    else workspace_layout_defaults(document.workspace);
    for (int ch = 0; ch < NUM_CHAINS; ++ch) {
        const Chain& source_chain = eng.chains[ch];
        Impl::ChainData& target_chain = document.chains[ch];
        target_chain.volume = source_chain.volume;
        target_chain.pan = source_chain.pan;
        target_chain.mute = source_chain.mute ? 1 : 0;
        for (int slot = 0; slot < UNITS_PER_CHAIN; ++slot) {
            const Unit* unit = source_chain.units[slot];
            Impl::UnitData& target = target_chain.units[slot];
            target = {};
            target.kind = (uint8_t)(unit ? unit->kind : UnitKind::Empty);
            target.bypass = unit && unit->bypass ? 1 : 0;
            if (unit && unit->random_source != ModSource::None && mod_source_sigil(unit->random_source) < 0) return false;
            target.random_source = unit ? uint8_t(unit->random_source) : 0;
            target.extra = unit_extra(unit);
            target.flags = unit_flags(unit);
            if (unit && unit->kind == UnitKind::Reverb &&
                authored_reverb_utilities(eng, ch, slot, *static_cast<const ReverbUnit*>(unit)))
                target.flags |= 2u; // old readers reject safely; neutral saves stay exact
            if (unit && unit->kind == UnitKind::SamplePlayer) {
                const auto* play = static_cast<const PlayUnit*>(unit);
                // Saving stale bindings would associate exact frames with a
                // different source in SAMP. Refuse rather than silently rebind.
                if (play->edit_state().enabled && !play->edit_source_matches() &&
                    !play->pending_edit_source()) return false;
                target.play_edit = play->edit_state();
                target.play_position = authored_play_params(eng, ch, slot, *play, PlayUnit::P_POSITION, PlayUnit::P_SCRUB + 1);
                target.play_hold = authored_play_params(eng, ch, slot, *play, PlayUnit::P_HOLD, PlayUnit::P_HOLD + 1);
                target.play_pitch_mode = uint8_t(play->pitch_mode());
                target.play_note = play->modern() && (target.play_pitch_mode || authored_play_params(eng, ch, slot,
                    *play, PlayUnit::P_NOTE, PlayUnit::P_COUNT, musical_note::encode(musical_note::ROOT)));
                if (target.play_note) {
                    const auto& v = play->params[PlayUnit::P_NOTE];
                    target.params[PlayUnit::P_NOTE] = {v.base, uint8_t(v.src), v.mod_depth, v.mod_enabled};
                }
            }
            if (unit && unit->kind == UnitKind::Arp) {
                const auto& recall = static_cast<const ArpUnit*>(unit)->note_recall;
                if (recall.mask & ~15u) return false;
                for (int i = 0; i < 4; ++i) if ((recall.mask & (1u << i)) &&
                    ArpUnit::note_semitones(unit->params[ArpUnit::P_NOTE1+i].base) == ArpUnit::NOTE_OFF) {
                    if (ArpUnit::note_semitones(recall.values[i]) == ArpUnit::NOTE_OFF) return false;
                    target.arp_recall.mask |= uint8_t(1u << i);
                    target.arp_recall.values[i] = recall.values[i];
                }
            }
            if (unit && unit->kind == UnitKind::Grain) {
                const auto* gran = static_cast<const GrainUnit*>(unit);
                // RAW validation, not runtime normalization. Dormant authored
                // percentage also requires GRNX; legacy authorship cannot save.
                target.gran_reading = uint8_t(gran->reading);
                target.gran_reverse_percent = gran->reverse_percent;
                if (target.gran_reading > 3 || target.gran_reverse_percent > 100 ||
                    (!gran->clean && (target.gran_reading || target.gran_reverse_percent != 50))) return false;
                if (gran->clean) {
                    target.gran_mode = (uint8_t)gran->launch_mode;
                    target.gran_window = (uint8_t)gran->window;
                    target.gran_count = gran->burst_count;
                    if (target.gran_mode > 2 || target.gran_window > 1 ||
                        !target.gran_count || target.gran_count > 16) return false;
                    target.gran_perform = target.gran_mode || target.gran_window || target.gran_count != 4;
                }
            }
            if (unit && unit->kind == UnitKind::Scratch) {
                const auto* scratch = static_cast<const ScratchUnit*>(unit);
                target.scratch_mode = uint8_t(scratch->mode);
                target.scratch_map = uint8_t(scratch->map);
                target.scratch_axis_x = scratch->axis_x;
                target.scratch_axis_y = scratch->axis_y;
                if (!scratch_axes_valid(target.scratch_mode, target.scratch_map,
                        target.scratch_axis_x, target.scratch_axis_y) ||
                    scratch->sample_slot < 0 || scratch->sample_slot >= SampleManifest::SLOT_COUNT) return false;
            }
            if(unit && unit->kind==UnitKind::Rule)target.rule_config=static_cast<const RuleUnit*>(unit)->config();
            if(unit && unit->kind==UnitKind::Rot)target.rot_config=static_cast<const RotUnit*>(unit)->config();
            if (unit && unit->kind == UnitKind::Pent) {
                target.pent_width = uint8_t(static_cast<const PentUnit*>(unit)->width);
                if (target.pent_width > 1 || target.extra || target.flags) return false;
            }
            if (unit && unit->kind == UnitKind::Paul) {
                const auto* paul = static_cast<const PaulUnit*>(unit);
                target.paul_mode = uint8_t(paul->mode);
                target.paul_direction = uint8_t(paul->direction);
                if (target.paul_mode > 1 || target.paul_direction > 2 || paul->sample_slot < 0 ||
                    paul->sample_slot >= SampleManifest::SLOT_COUNT) return false;
            }
            if (unit && unit->kind == UnitKind::Glitch) {
                target.glitch_pattern = static_cast<const GlitchUnit*>(unit)->pattern_state();
                if (!GlitchUnit::valid_pattern(target.glitch_pattern)) return false;
            }
            if (unit) for (int param = 0; param < PARAM_BANK_SIZE; ++param) {
                if (param >= unit->num_params) break;
                target.params[param].base = unit->params[param].base;
                target.params[param].src = (uint8_t)unit->params[param].src;
                target.params[param].depth = unit->params[param].mod_depth;
                target.params[param].enabled = unit->params[param].mod_enabled;
                target.params[param].relic_source=unit->params[param].relic_source;
                target.params[param].relic_port=unit->params[param].relic_port;
            }
            if (unit && unit->kind == UnitKind::Stranular) {
                const auto* stranular = static_cast<const StranularUnit*>(unit);
                target.strn_window = stranular->window_mode;
                target.strn_division = stranular->sync_division;
                target.strn_long = stranular->long_mode ? 1 : 0;
            }
        }
    }
    for (int i = 0; i < NUM_SIGILS; ++i) {
        const Sigil& source = eng.sigils[i];
        Impl::SigilData& target = document.sigils[i];
        target.config = eng.sigil_config[i];
        if (!sigil_config_valid(target.config)) return false;
        target.drawn = source.drawn;
        target.mode = (uint8_t)source.mode;
        std::memcpy(target.table, source.table, sizeof target.table);
        target.rate = eng.sigil_rate(i);
        target.depth = source.depth;
        target.mod_src = (uint8_t)eng.sigil_mods[i].src;
        target.mod_depth = eng.sigil_mods[i].depth;
        target.sync = eng.sigil_sync[i] ? 1 : 0;
        target.division = eng.sigil_div[i];
        target.step_dest = eng.step_dest[i];
        if (target.mode >= (uint8_t)SigilMode::Count || target.mod_src >= (uint8_t)ModSource::Count ||
            mod_source_sigil((ModSource)target.mod_src) >= i || target.division >= 6 ||
            target.step_dest >= NUM_CHAINS) return false;
    }
    document.bpm = (uint16_t)eng.bpm;
    for (int i = 0; i < 2; ++i) document.clock_mods[i] = eng.clock_mod((ClockTarget)i);
    document.env_chain = (uint8_t)eng.env_follow_source();
    document.env_response = (uint8_t)eng.env_follow_response();
    const auto& bank = trackr::synth::SampleBank::instance();
    for (int slot = 0; slot < SampleManifest::SLOT_COUNT; ++slot) {
        const auto& source = bank.provenance(slot);
        auto& target = document.samples[slot];
        target.origin = (uint8_t)source.origin;
        target.missing = source.missing;
        std::memcpy(target.path, source.path, sizeof target.path);
        target.path[sizeof target.path - 1] = 0;
        std::memcpy(document.sample_chops[slot], bank.slot(slot).chops,
                    sizeof document.sample_chops[slot]);
        document.sample_slice_reverse[slot] = bank.slot(slot).slice_reverse_mask;
        for (int cut = 0; cut < trackr::synth::Sample::MAX_CHOPS; ++cut) {
            const uint16_t speed = bank.slot(slot).slice_speed_q8[cut];
            if (trackr::synth::Sample::cut_speed(speed) != speed) return false;
            document.sample_slice_speed[slot][cut] = bank.slot(slot).chops[cut] == UINT32_MAX ? 256 : speed;
            const int8_t pitch = bank.slot(slot).slice_pitch_semitones[cut];
            if (!play_part_pitch::valid(pitch) || (bank.slot(slot).chops[cut] == UINT32_MAX && pitch)) return false;
            document.sample_slice_pitch[slot][cut] = pitch;
        }
        document.sample_exact_native[slot] = bank.slot(slot).exact_native_endpoint;
    }
    // A staged/missing PLAY source owns its serialized metadata even though the
    // process-wide bank may still contain another project's PCM. Conflicting
    // pending aliases fail rather than silently pick one file/cut list.
    bool pending_slots[SampleManifest::SLOT_COUNT] = {};
    uint32_t pending_frames[SampleManifest::SLOT_COUNT] = {};
    for (const auto& chain : eng.chains) for (const Unit* unit : chain.units) {
        if (!unit || unit->kind != UnitKind::SamplePlayer) continue;
        const auto* play = static_cast<const PlayUnit*>(unit);
        const auto* pending = play->pending_edit_source();
        if (play->edit_state().enabled && play->sample_slot >= 0 && play->sample_slot < SampleManifest::SLOT_COUNT)
            document.sample_exact_native[play->sample_slot] = true;
        if (!pending) continue;
        const int slot = pending->slot;
        auto& entry = document.samples[slot];
        if (pending_slots[slot] && (entry.origin != (uint8_t)pending->provenance.origin ||
            entry.missing != pending->provenance.missing || std::strcmp(entry.path, pending->provenance.path) ||
            std::memcmp(document.sample_chops[slot], pending->chops, sizeof pending->chops) ||
            document.sample_slice_reverse[slot] != pending->reverse ||
            std::memcmp(document.sample_slice_speed[slot], pending->speed_q8, sizeof pending->speed_q8) ||
            std::memcmp(document.sample_slice_pitch[slot], pending->pitch_semitones, sizeof pending->pitch_semitones) ||
            document.sample_exact_native[slot] != pending->exact_native_endpoint ||
            (pending_frames[slot] && play->edit_state().source_frames && pending_frames[slot] != play->edit_state().source_frames))) return false;
        pending_slots[slot] = true; pending_frames[slot] = play->edit_state().source_frames;
        entry.origin = (uint8_t)pending->provenance.origin;
        entry.missing = pending->provenance.missing;
        std::memcpy(entry.path, pending->provenance.path, sizeof entry.path);
        entry.path[sizeof entry.path - 1] = 0;
        std::memcpy(document.sample_chops[slot], pending->chops, sizeof pending->chops);
        document.sample_slice_reverse[slot] = pending->reverse;
        for (int cut = 0; cut < trackr::synth::Sample::MAX_CHOPS; ++cut)
            if (!play_part_pitch::valid(pending->pitch_semitones[cut]) ||
                (pending->chops[cut] == UINT32_MAX && pending->pitch_semitones[cut])) return false;
        std::memcpy(document.sample_slice_speed[slot], pending->speed_q8, sizeof pending->speed_q8);
        std::memcpy(document.sample_slice_pitch[slot], pending->pitch_semitones, sizeof pending->pitch_semitones);
        document.sample_exact_native[slot] = pending->exact_native_endpoint;
    }
    document.transport_playing = eng.transport_playing ? 1 : 0;
    document.beats_per_bar = eng.beats_per_bar;
    document.launch_quantize = (uint8_t)eng.launch_quantize;
    document.swing_percent = eng.swing_percent;
    document.pattern_launch_quantize = (uint8_t)eng.pattern_launch_quantize;
    document.sequencer_enabled = eng.sequencer_enabled ? 1 : 0;
    document.pattern = eng.pattern;
    std::memcpy(document.pattern_banks, eng.pattern_banks, sizeof document.pattern_banks);
    document.active_pattern = eng.active_pattern;
    std::memcpy(document.pattern_chain, eng.pattern_chain, sizeof document.pattern_chain);
    document.pattern_chain_length = eng.pattern_chain_length;
    document.pattern_chain_loop = eng.pattern_chain_loop ? 1 : 0;
    std::memcpy(document.scenes, eng.scenes, sizeof document.scenes);
    std::memcpy(document.scene_morph, eng.scene_morph, sizeof document.scene_morph);
    std::memcpy(document.macro_pads, eng.macro_pads, sizeof document.macro_pads);
    std::memcpy(document.performance_bindings, eng.performance_bindings, sizeof document.performance_bindings);
    return true;
}

static bool w8(FILE* f, uint8_t v) { return fwrite(&v, 1, 1, f) == 1; }
static bool w16(FILE* f, int16_t v) {
    uint8_t b[2] = { (uint8_t)(v & 0xff), (uint8_t)((uint16_t)v >> 8) };
    return fwrite(b, 1, 2, f) == 2;
}
static bool wu16(FILE* f, uint16_t v) { return w16(f, (int16_t)v); }
static bool wu32(FILE* f, uint32_t v) {
    uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    return fwrite(b, 1, 4, f) == 4;
}
static bool wbytes(FILE* f, const void* p, size_t n) { return !n || fwrite(p, 1, n, f) == n; }
static bool r8(FILE* f, uint8_t& v) { return fread(&v, 1, 1, f) == 1; }
static bool r16(FILE* f, int16_t& v) {
    uint8_t b[2]; if (fread(b, 1, 2, f) != 2) return false;
    v = (int16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8)); return true;
}
static bool ru16(FILE* f, uint16_t& v) { int16_t x; if (!r16(f, x)) return false; v = (uint16_t)x; return true; }
static bool ru32(FILE* f, uint32_t& v) {
    uint8_t b[4]; if (fread(b, 1, 4, f) != 4) return false;
    v = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24); return true;
}
static int16_t clamp_pan(int32_t v) { return v < -32767 ? -32767 : v > 32767 ? 32767 : (int16_t)v; }

static uint8_t unit_extra(const Unit* u) {
    if (!u) return 0;
    switch (u->kind) {
        case UnitKind::SamplePlayer: return (uint8_t)static_cast<const PlayUnit*>(u)->sample_slot;
        case UnitKind::Grain: return (uint8_t)static_cast<const GrainUnit*>(u)->sample_slot;
        case UnitKind::Scratch: return (uint8_t)static_cast<const ScratchUnit*>(u)->sample_slot;
        case UnitKind::Paul: return (uint8_t)static_cast<const PaulUnit*>(u)->sample_slot;
        case UnitKind::Rule: return (uint8_t)static_cast<const RuleUnit*>(u)->sample_slot;
        case UnitKind::Rot: return (uint8_t)static_cast<const RotUnit*>(u)->sample_slot;
        case UnitKind::Recorder: return (uint8_t)static_cast<const RecUnit*>(u)->sample_slot;
        case UnitKind::Osc: {
            const auto* osc = static_cast<const OscUnit*>(u);
            return (uint8_t)(osc->shape_morph ? OscUnit::nearest_shape_anchor(osc->params[OscUnit::P_SHAPE].base)
                                              : osc->wave);
        }
        case UnitKind::Filter: return (uint8_t)static_cast<const FilterUnit*>(u)->type;
        default: return 0;
    }
}
static void apply_extra(Unit* u, uint8_t x) {
    if (!u) return;
    switch (u->kind) {
        case UnitKind::SamplePlayer: static_cast<PlayUnit*>(u)->sample_slot = x % trackr::synth::SAMPLE_BANK_SIZE; break;
        case UnitKind::Grain: static_cast<GrainUnit*>(u)->sample_slot = x % trackr::synth::SAMPLE_BANK_SIZE; break;
        case UnitKind::Scratch: static_cast<ScratchUnit*>(u)->sample_slot = x; break;
        case UnitKind::Paul: static_cast<PaulUnit*>(u)->sample_slot = x; break;
        case UnitKind::Rule: static_cast<RuleUnit*>(u)->sample_slot = x; break;
        case UnitKind::Rot: static_cast<RotUnit*>(u)->sample_slot = x; break;
        case UnitKind::Recorder: static_cast<RecUnit*>(u)->sample_slot = x % trackr::synth::SAMPLE_BANK_SIZE; break;
        case UnitKind::Osc: if (x < (uint8_t)OscUnit::Wave::Count) static_cast<OscUnit*>(u)->wave = (OscUnit::Wave)x; break;
        case UnitKind::Filter: if (x <= (uint8_t)trackr::dsp::FilterType::Off) static_cast<FilterUnit*>(u)->type = (trackr::dsp::FilterType)x; break;
        default: break;
    }
}
static uint8_t unit_flags(const Unit* u) {
    if (!u) return 0;
    if (u->kind == UnitKind::SamplePlayer) {
        const auto* play = static_cast<const PlayUnit*>(u);
        return (play->pitch_quantize ? 1u : 0u) | (play->modern() ? 2u : 0u);
    }
    if (u->kind == UnitKind::Osc) {
        const auto* osc = static_cast<const OscUnit*>(u);
        return (uint8_t)((osc->pitch_quantize ? 1u : 0u) | (osc->shape_morph ? 2u : 0u) |
                         (osc->clean ? 4u : 0u));
    }
    if (u->kind == UnitKind::Fm) {
        const auto* fm = static_cast<const FmUnit*>(u);
        return (uint8_t)((fm->phase_reset ? 1u : 0u) | (fm->alias_safe ? 2u : 0u) |
                         (fm->drone ? 4u : 0u) |
                         (fm->extended ? (0x80u | (((uint8_t)fm->carrier_wave & 3u) << 3) |
                                                  (((uint8_t)fm->mod_wave & 3u) << 5)) : 0u));
    }
    // Bit7 marks authored BODY/DECAY; bit0 marks authored ATTACK/MODE. Migrated
    // follow cards can be re-saved as eight params without changing their sound.
    if (u->kind == UnitKind::Motor) return static_cast<const MotorUnit*>(u)->modern() ? 1u : 0u;
    if (u->kind == UnitKind::Fold) return static_cast<const FoldUnit*>(u)->modern() ? 1u : 0u;
    if (u->kind == UnitKind::Ring) return static_cast<const RingUnit*>(u)->modern() ? 1u : 0u;
    if (u->kind == UnitKind::Strain) return static_cast<const StrainUnit*>(u)->modern() ? 1u : 0u;
    if (u->kind == UnitKind::Bitcrush) return static_cast<const BitUnit*>(u)->modern() ? 1u : 0u;
    if (u->kind == UnitKind::Noise) return static_cast<const NoiseUnit*>(u)->modern() ? 0x83u : 0x81u;
    if (u->kind == UnitKind::Ghost) return static_cast<const GhostUnit*>(u)->long_tail ? 1u : 0u;
    if (u->kind == UnitKind::Vca) {
        const auto* vca=static_cast<const VcaUnit*>(u);
        return vca->modern()?3u:vca->flexible?1u:0u;
    }
    if (u->kind == UnitKind::Delay) return static_cast<const DelayUnit*>(u)->modern() ? 1u : 0u;
    if (u->kind == UnitKind::Reverb) return static_cast<const ReverbUnit*>(u)->modern() ? 1u : 0u;
    if (u->kind == UnitKind::Tilt) return uint8_t(static_cast<const TiltUnit*>(u)->revision());
    // MIC bit0 distinguishes real low-cut data from the old unused third slot.
    // All in-memory MIC cards expose three params; migration clears old tails.
    if (u->kind == UnitKind::MicIn) return 1u;
    // GRAN: bit0 sync, bits1-2 division, bit3 clean; bit4 required GRNX marker.
    // Absence of bit3 always means frozen legacy DSP. Reading defaults add no bit.
    if (u->kind == UnitKind::Grain) {
        const auto* g = static_cast<const GrainUnit*>(u);
        return (uint8_t)((g->clock_sync ? 1u : 0u) |
                         (uint32_t)((g->sync_division & 3u) << 1) | (g->clean ? 8u : 0u) |
                         (g->clean && (g->reading != GrainUnit::Reading::SignedPitch || g->reverse_percent != 50) ? 16u : 0u));
    }
    return 0;
}
static void apply_unit_flags(Unit* u, uint8_t flags) {
    if (!u) return;
    if (u->kind == UnitKind::SamplePlayer) static_cast<PlayUnit*>(u)->pitch_quantize = (flags & 1) != 0;
    else if (u->kind == UnitKind::Osc) {
        auto* osc = static_cast<OscUnit*>(u);
        osc->pitch_quantize = (flags & 1u) != 0;
        osc->shape_morph = (flags & 2u) != 0;
        osc->clean = (flags & 4u) != 0;
    }
    else if (u->kind == UnitKind::Fm) {
        auto* fm = static_cast<FmUnit*>(u);
        fm->phase_reset = (flags & 1u) != 0;
        fm->alias_safe = (flags & 2u) != 0;
        fm->drone = (flags & 4u) != 0;
        fm->set_extended((flags & 0x80u) != 0);
        fm->carrier_wave = fm->extended ? (FmUnit::Wave)((flags >> 3) & 3u) : FmUnit::Wave::Sine;
        fm->mod_wave = fm->extended ? (FmUnit::Wave)((flags >> 5) & 3u) : FmUnit::Wave::Sine;
    }
    else if (u->kind == UnitKind::Ghost) static_cast<GhostUnit*>(u)->long_tail = (flags & 1u) != 0;
    else if (u->kind == UnitKind::Vca) static_cast<VcaUnit*>(u)->set_flexible((flags & 1u) != 0);
    else if (u->kind == UnitKind::Noise) {
        auto* noise = static_cast<NoiseUnit*>(u);
        if (!(flags & 0x80u)) {
            noise->params[NoiseUnit::P_BODY].base = 0;
            noise->params[NoiseUnit::P_BODY].snap();
            noise->params[NoiseUnit::P_DECAY].base = 32767;
            noise->params[NoiseUnit::P_DECAY].snap();
        }
    }
    else if (u->kind == UnitKind::Grain) {
        auto* g = static_cast<GrainUnit*>(u);
        g->clock_sync = (flags & 1) != 0;
        g->sync_division = (uint8_t)((flags >> 1) & 3u);
        g->clean = (flags & 8u) != 0;
    }
}
// Loading is deliberately legacy-first, even for SNC1-3 without UNIT flags.
static Unit* make_loaded_unit(UnitKind kind) {
    if (!unit_kind_available(kind)) return nullptr;
    // Never allocate a modern 256kb ring just to load an old 64kb delay.
    if (kind == UnitKind::Delay) {
        Unit* unit = new(std::nothrow) DelayUnit(false);
        if (unit && !unit->ready()) { delete unit; return nullptr; }
        return unit;
    }
    if (kind == UnitKind::Reverb) {
        // Old projects never pay for a modern tank, and never borrow new sonics.
        Unit* unit = new(std::nothrow) ReverbUnit(false);
        if (unit && !unit->ready()) { delete unit; return nullptr; }
        return unit;
    }
    if (kind == UnitKind::Noise) {
        auto* unit = new(std::nothrow) NoiseUnit(false);
        if (unit) { unit->params[NoiseUnit::P_MODE].base=0; unit->params[NoiseUnit::P_MODE].snap(); }
        return unit;
    }
    if (kind == UnitKind::Motor) return new(std::nothrow) MotorUnit(false);
    if (kind == UnitKind::Fold) return new(std::nothrow) FoldUnit(false);
    if (kind == UnitKind::Ring) return new(std::nothrow) RingUnit(false);
    if (kind == UnitKind::Strain) return new(std::nothrow) StrainUnit(false);
    if (kind == UnitKind::Bitcrush) return new(std::nothrow) BitUnit(false);
    if (kind == UnitKind::Tilt) return new(std::nothrow) TiltUnit(false);
    Unit* unit = make_unit(kind);
    if (unit && kind == UnitKind::SamplePlayer) {
        auto* play = static_cast<PlayUnit*>(unit);
        play->set_modern(false);
        play->params[PlayUnit::P_LOOP].base = 32767; // historical constructor default
        play->params[PlayUnit::P_LOOP].snap();
    }
    if (unit && kind == UnitKind::Osc) {
        auto* osc = static_cast<OscUnit*>(unit);
        osc->shape_morph = false;
        osc->clean = false;
    }
    if (unit && kind == UnitKind::Grain) static_cast<GrainUnit*>(unit)->clean = false;
    if (unit && kind == UnitKind::Fm) static_cast<FmUnit*>(unit)->set_extended(false);
    if (unit && kind == UnitKind::Ghost) static_cast<GhostUnit*>(unit)->long_tail = false;
    if (unit && kind == UnitKind::Vca) static_cast<VcaUnit*>(unit)->set_flexible(false);
    return unit;
}
static void finish_play_mode(Unit* unit, bool modern) {
    auto* play = static_cast<PlayUnit*>(unit);
    play->set_modern(modern);
    if (!modern) {
        // Hidden legacy tails have no routes/locks/PCM effects. Keep useful
        // defaults in memory for an explicit UI conversion, never in old saves.
        for (int p=PlayUnit::P_MODE;p<PARAM_BANK_SIZE;++p) play->params[p]=Param{};
        play->params[PlayUnit::P_MODE].name="MODE";
        play->params[PlayUnit::P_MODE].base=32767;
        play->params[PlayUnit::P_MODE].snap();
        play->params[PlayUnit::P_GATE].name="GATE";
        play->params[PlayUnit::P_GATE].base=play_mode::gate_value(play_mode::DEFAULT_GATE);
        play->params[PlayUnit::P_GATE].snap();
    }
}
static void apply_param(Unit* u, int p, int16_t base, uint8_t src, int16_t depth) {
    if (!u || p < 0 || p >= PARAM_BANK_SIZE) return;
    // TILT RANGE and RING FOLLOW were appended after their first SNC7 implementations.
    // Historical files wrote zero into unused ParamBank slots, so zero preserves the
    // old transfer while new units can opt into the expanded controls.
    Param& v = u->params[p]; v.base = base;
    v.src = src < (uint8_t)ModSource::Count ? (ModSource)src : ModSource::None;
    v.mod_depth = depth; v.effective = v.target = base; v.slew_ready = false;
}

static void canonicalize_play_range(Engine& target, int chain, int slot, bool authored, int first, int end) {
    auto* play = static_cast<PlayUnit*>(target.chains[chain].units[slot]);
    for (int p = first; p < end; ++p) play->params[p] = Param{};
    if (authored) return;
    for (auto& scene : target.scenes[chain]) if (scene.kind[slot] == UnitKind::SamplePlayer)
        for (int p = first; p < end; ++p) scene.base[slot][p] = 0;
    for (auto& pad : target.macro_pads) {
        int kept = 0;
        for (int r = 0; r < pad.route_count; ++r) {
            const auto v = pad.routes[r];
            if (v.chain == chain && v.slot == slot && v.param >= first && v.param < end) continue;
            pad.routes[kept++] = v;
        }
        for (int r = kept; r < pad.route_count; ++r) pad.routes[r] = MacroPadRoute{};
        pad.route_count = (uint8_t)kept;
    }
    auto strip = [=](Pattern& pattern) {
        for (auto& step : pattern.steps[chain]) {
            int kept = 0;
            for (int i = 0; i < step.lock_count; ++i) {
                const auto v = step.locks[i];
                if (v.slot == slot && v.param >= first && v.param < end) continue;
                step.locks[kept++] = v;
            }
            for (int i = kept; i < step.lock_count; ++i) step.locks[i] = StepLock{};
            step.lock_count = (uint8_t)kept;
        }
    };
    strip(target.pattern); for (auto& pattern : target.pattern_banks) strip(pattern);
}

static void canonicalize_play_position(Engine& target, int chain, int slot, bool position, bool hold = false, bool note = false) {
    canonicalize_play_range(target, chain, slot, position, PlayUnit::P_POSITION, PlayUnit::P_SCRUB + 1);
    canonicalize_play_range(target, chain, slot, hold, PlayUnit::P_HOLD, PlayUnit::P_HOLD + 1);
    canonicalize_play_range(target, chain, slot, note, PlayUnit::P_NOTE, PARAM_BANK_SIZE);
    auto* play = static_cast<PlayUnit*>(target.chains[chain].units[slot]);
    play->params[PlayUnit::P_POSITION].name = "POS";
    play->params[PlayUnit::P_SCRUB].name = "SCRUB";
    play->params[PlayUnit::P_HOLD].name = "HOLD";
    play->set_pitch_mode(PlayUnit::PitchMode::Legacy);
    play->params[PlayUnit::P_NOTE].name = "NOTE";
    play->params[PlayUnit::P_NOTE].base = musical_note::encode(musical_note::ROOT);
    play->params[PlayUnit::P_NOTE].snap();
}

// Historical GRAN padding is not authored SCAN, even after a later opt-in.
// Run after all chunks so hidden routes/locks/scenes cannot reappear by ordering.
static void canonicalize_grain_scan(Engine& target, int chain, int slot) {
    auto* grain = static_cast<GrainUnit*>(target.chains[chain].units[slot]);
    if (grain->clean) return;
    constexpr int scan = GrainUnit::P_SCAN;
    grain->params[scan] = Param{};
    grain->params[scan].name = "SCAN";
    grain->params[scan].bipolar = true;
    for (auto& scene : target.scenes[chain]) if (scene.kind[slot] == UnitKind::Grain)
        scene.base[slot][scan] = 0;
    for (auto& pad : target.macro_pads) {
        int kept = 0;
        for (int r = 0; r < pad.route_count; ++r) {
            const auto v = pad.routes[r];
            if (v.chain == chain && v.slot == slot && v.param == scan) continue;
            pad.routes[kept++] = v;
        }
        for (int r = kept; r < pad.route_count; ++r) pad.routes[r] = MacroPadRoute{};
        pad.route_count = (uint8_t)kept;
    }
    auto strip = [=](Pattern& pattern) {
        for (auto& step : pattern.steps[chain]) {
            int kept = 0;
            for (int i = 0; i < step.lock_count; ++i) {
                const auto v = step.locks[i];
                if (v.slot == slot && v.param == scan) continue;
                step.locks[kept++] = v;
            }
            for (int i = kept; i < step.lock_count; ++i) step.locks[i] = StepLock{};
            step.lock_count = (uint8_t)kept;
        }
    };
    strip(target.pattern); for (auto& pattern : target.pattern_banks) strip(pattern);
}

static void canonicalize_osc(Engine& target, int chain, int slot, bool morph) {
    auto* osc = static_cast<OscUnit*>(target.chains[chain].units[slot]);
    osc->shape_morph = morph;
    if (morph) return;
    const char* name = osc->params[OscUnit::P_SHAPE].name;
    osc->params[OscUnit::P_SHAPE] = Param{};
    osc->params[OscUnit::P_SHAPE].name = name;
    osc->params[OscUnit::P_SHAPE].base = OscUnit::shape_anchor(osc->wave);
    osc->params[OscUnit::P_SHAPE].snap();
    for (auto& scene : target.scenes[chain]) if (scene.kind[slot] == UnitKind::Osc)
        scene.base[slot][OscUnit::P_SHAPE] = osc->params[OscUnit::P_SHAPE].base;
    for (auto& pad : target.macro_pads) {
        int kept = 0;
        for (int route = 0; route < pad.route_count; ++route) {
            const auto& value = pad.routes[route];
            if (value.chain == chain && value.slot == slot && value.param == OscUnit::P_SHAPE) continue;
            pad.routes[kept++] = value;
        }
        for (int route = kept; route < pad.route_count; ++route) pad.routes[route] = MacroPadRoute{};
        pad.route_count = (uint8_t)kept;
    }
    auto strip_locks = [=](Pattern& pattern) {
        for (auto& step : pattern.steps[chain]) {
            int kept = 0;
            for (int lock = 0; lock < step.lock_count; ++lock) {
                const auto& value = step.locks[lock];
                if (value.slot == slot && value.param == OscUnit::P_SHAPE) continue;
                step.locks[kept++] = value;
            }
            for (int lock = kept; lock < step.lock_count; ++lock) step.locks[lock] = StepLock{};
            step.lock_count = (uint8_t)kept;
        }
    };
    strip_locks(target.pattern);
    for (auto& pattern : target.pattern_banks) strip_locks(pattern);
}

// Legacy FM padding must remain dormant even after an explicit later opt-in.
// This runs only after all chunks, so reversed CORE/UNIT and scene/route order
// cannot accidentally restore authored-looking historical hidden entries.
static void canonicalize_fm(Engine& target, int chain, int slot, bool extended) {
    auto* fm = static_cast<FmUnit*>(target.chains[chain].units[slot]);
    fm->set_extended(extended);
    if (extended) return;
    fm->carrier_wave = fm->mod_wave = FmUnit::Wave::Sine;
    for (int p = FmUnit::P_PITCH_ENV; p < FmUnit::P_COUNT; ++p) {
        const char* name = fm->params[p].name;
        fm->params[p] = Param{};
        fm->params[p].name = name;
        fm->params[p].bipolar = p == FmUnit::P_PITCH_ENV;
        fm->params[p].base = p == FmUnit::P_PITCH_TIME ? FmUnit::DEFAULT_PITCH_TIME : 0;
        fm->params[p].snap();
    }
    for (auto& scene : target.scenes[chain]) if (scene.kind[slot] == UnitKind::Fm)
        for (int p = FmUnit::P_PITCH_ENV; p < FmUnit::P_COUNT; ++p)
            scene.base[slot][p] = fm->params[p].base;
    for (auto& pad : target.macro_pads) {
        int kept = 0;
        for (int r = 0; r < pad.route_count; ++r) {
            const auto& route = pad.routes[r];
            if (route.chain == chain && route.slot == slot && route.param >= FmUnit::P_PITCH_ENV) continue;
            pad.routes[kept++] = route;
        }
        for (int r = kept; r < pad.route_count; ++r) pad.routes[r] = MacroPadRoute{};
        pad.route_count = (uint8_t)kept;
    }
    auto strip_locks = [=](Pattern& pattern) {
        for (auto& step : pattern.steps[chain]) {
            int kept = 0;
            for (int i = 0; i < step.lock_count; ++i) {
                const auto& lock = step.locks[i];
                if (lock.slot == slot && lock.param >= FmUnit::P_PITCH_ENV) continue;
                step.locks[kept++] = lock;
            }
            for (int i = kept; i < step.lock_count; ++i) step.locks[i] = StepLock{};
            step.lock_count = (uint8_t)kept;
        }
    };
    strip_locks(target.pattern);
    for (auto& pattern : target.pattern_banks) strip_locks(pattern);
}

// MIC's appended low-cut must never inherit old hidden parameters or routes.
// Run after ALL chunks: the third slot becomes exposed even on a migrated card.
static void canonicalize_mic(Engine& target, int chain, int slot, bool modern) {
    Unit* unit = target.chains[chain].units[slot];
    const int first = modern ? MicUnit::P_COUNT : MicUnit::P_LOW_CUT;
    for (int p = first; p < PARAM_BANK_SIZE; ++p) {
        const char* name = unit->params[p].name;
        unit->params[p] = Param{};
        unit->params[p].name = name;
    }
    for (auto& scene : target.scenes[chain]) if (scene.kind[slot] == UnitKind::MicIn)
        for (int p = first; p < MAX_PARAMS; ++p) scene.base[slot][p] = 0;
    for (auto& pad : target.macro_pads) {
        int kept = 0;
        for (int r = 0; r < pad.route_count; ++r) {
            const auto& route = pad.routes[r];
            if (route.chain == chain && route.slot == slot && route.param >= first) continue;
            pad.routes[kept++] = route;
        }
        // Don't rewrite unrelated inactive route padding.
        for (int r = kept; r < pad.route_count; ++r) pad.routes[r] = MacroPadRoute{};
        pad.route_count = (uint8_t)kept;
    }
    auto strip_locks = [=](Pattern& pattern) {
        for (auto& step : pattern.steps[chain]) {
            int kept = 0;
            for (int i = 0; i < step.lock_count; ++i) {
                const auto& lock = step.locks[i];
                if (lock.slot == slot && lock.param >= first) continue;
                step.locks[kept++] = lock;
            }
            for (int i = kept; i < step.lock_count; ++i) step.locks[i] = StepLock{};
            step.lock_count = (uint8_t)kept;
        }
    };
    strip_locks(target.pattern);
    for (auto& pattern : target.pattern_banks) strip_locks(pattern);
}

// NOISE's attack/mode become visible on migrated cards: discard historical
// padding and every route into it only after all chunks have been consumed.
static void canonicalize_noise(Engine& target, int chain, int slot, bool authored, bool modern=false) {
    auto* unit = static_cast<NoiseUnit*>(target.chains[chain].units[slot]);
    unit->set_modern(modern);
    const int first = modern ? NoiseUnit::P_COUNT : authored ? NoiseUnit::LEGACY_COUNT : NoiseUnit::P_ATTACK;
    for (int p = first; p < PARAM_BANK_SIZE; ++p) {
        const char* name = unit->params[p].name;
        unit->params[p] = Param{};
        unit->params[p].name = name;
    }
    for (auto& scene : target.scenes[chain]) if (scene.kind[slot] == UnitKind::Noise)
        for (int p = first; p < MAX_PARAMS; ++p) scene.base[slot][p] = 0;
    for (auto& pad : target.macro_pads) {
        int kept = 0;
        for (int r = 0; r < pad.route_count; ++r) {
            const auto& route = pad.routes[r];
            if (route.chain == chain && route.slot == slot && route.param >= first) continue;
            pad.routes[kept++] = route;
        }
        for (int r = kept; r < pad.route_count; ++r) pad.routes[r] = MacroPadRoute{};
        pad.route_count = (uint8_t)kept;
    }
    auto strip_locks = [=](Pattern& pattern) {
        for (auto& step : pattern.steps[chain]) {
            int kept = 0;
            for (int i = 0; i < step.lock_count; ++i) {
                const auto& lock = step.locks[i];
                if (lock.slot == slot && lock.param >= first) continue;
                step.locks[kept++] = lock;
            }
            for (int i = kept; i < step.lock_count; ++i) step.locks[i] = StepLock{};
            step.lock_count = (uint8_t)kept;
        }
    };
    strip_locks(target.pattern);
    for (auto& pattern : target.pattern_banks) strip_locks(pattern);
}

// MOTOR extension is authored only in the SNC8 container. Historical padding
// and every stale scene/macro/lock into it are discarded after ALL chunks.
static void canonicalize_motor(Engine& target,int chain,int slot,bool modern) {
    auto* unit=static_cast<MotorUnit*>(target.chains[chain].units[slot]);
    unit->set_modern(modern);
    const int first=modern ? MotorUnit::P_COUNT : 6;
    for(int p=first;p<PARAM_BANK_SIZE;++p){
        const char* name=unit->params[p].name;unit->params[p]=Param{};unit->params[p].name=name;
    }
    for(auto& scene:target.scenes[chain])if(scene.kind[slot]==UnitKind::Motor)
        for(int p=first;p<MAX_PARAMS;++p)scene.base[slot][p]=0;
    for(auto& pad:target.macro_pads){
        int kept=0;
        for(int i=0;i<pad.route_count;++i){
            const auto route=pad.routes[i];
            if(route.chain==chain&&route.slot==slot&&route.param>=first)continue;
            pad.routes[kept++]=route;
        }
        for(int i=kept;i<pad.route_count;++i)pad.routes[i]=MacroPadRoute{};
        pad.route_count=uint8_t(kept);
    }
    auto strip=[=](Pattern& pattern){for(auto& step:pattern.steps[chain]){
        int kept=0;
        for(int i=0;i<step.lock_count;++i){const auto lock=step.locks[i];
            if(lock.slot==slot&&lock.param>=first)continue;
            step.locks[kept++]=lock;
        }
        for(int i=kept;i<step.lock_count;++i)step.locks[i]=StepLock{};
        step.lock_count=uint8_t(kept);
    }};
    strip(target.pattern);for(auto& pattern:target.pattern_banks)strip(pattern);
    unit->control_tick();
}

// Old BIT padding must never become an audible new control or hidden route.
// Run after ALL chunks, including scenes/macros and every pattern bank.
static void canonicalize_bit(Engine& target,int chain,int slot,bool modern) {
    auto* unit=static_cast<BitUnit*>(target.chains[chain].units[slot]);
    unit->set_modern(modern);
    const int first=modern?BitUnit::P_COUNT:BitUnit::LEGACY_COUNT;
    for(int p=first;p<PARAM_BANK_SIZE;++p){
        const char* name=unit->params[p].name;unit->params[p]=Param{};unit->params[p].name=name;
    }
    for(auto& scene:target.scenes[chain])if(scene.kind[slot]==UnitKind::Bitcrush)
        for(int p=first;p<MAX_PARAMS;++p)scene.base[slot][p]=0;
    for(auto& pad:target.macro_pads){
        int kept=0;
        for(int i=0;i<pad.route_count;++i){const auto route=pad.routes[i];
            if(route.chain==chain&&route.slot==slot&&route.param>=first)continue;
            pad.routes[kept++]=route;
        }
        for(int i=kept;i<pad.route_count;++i)pad.routes[i]=MacroPadRoute{};
        pad.route_count=uint8_t(kept);
    }
    auto strip=[=](Pattern& pattern){for(auto& step:pattern.steps[chain]){
        int kept=0;
        for(int i=0;i<step.lock_count;++i){const auto lock=step.locks[i];
            if(lock.slot==slot&&lock.param>=first)continue;
            step.locks[kept++]=lock;
        }
        for(int i=kept;i<step.lock_count;++i)step.locks[i]=StepLock{};
        step.lock_count=uint8_t(kept);
    }};
    strip(target.pattern);for(auto& pattern:target.pattern_banks)strip(pattern);
    unit->control_tick();
}

#include "vca_patch.inc"

// Fold appended controls and attenuation metadata require explicit SNCD13.
// Always run after every chunk, so hidden historical padding/routes stay inert.
static void canonicalize_fold(Engine& target,int chain,int slot,bool modern) {
    auto* unit=static_cast<FoldUnit*>(target.chains[chain].units[slot]);
    unit->set_modern(modern);
    const int first=modern?FoldUnit::P_COUNT:FoldUnit::LEGACY_COUNT;
    for(int p=first;p<PARAM_BANK_SIZE;++p){
        const char* name=unit->params[p].name;unit->params[p]=Param{};unit->params[p].name=name;
    }
    for(auto& scene:target.scenes[chain])if(scene.kind[slot]==UnitKind::Fold)
        for(int p=first;p<MAX_PARAMS;++p)scene.base[slot][p]=0;
    for(auto& pad:target.macro_pads){
        int kept=0;
        for(int i=0;i<pad.route_count;++i){const auto route=pad.routes[i];
            if(route.chain==chain&&route.slot==slot&&route.param>=first)continue;
            pad.routes[kept++]=route;
        }
        for(int i=kept;i<pad.route_count;++i)pad.routes[i]=MacroPadRoute{};
        pad.route_count=uint8_t(kept);
    }
    auto strip=[=](Pattern& pattern){for(auto& step:pattern.steps[chain]){
        int kept=0;
        for(int i=0;i<step.lock_count;++i){const auto lock=step.locks[i];
            if(lock.slot==slot&&lock.param>=first)continue;
            step.locks[kept++]=lock;
        }
        for(int i=kept;i<step.lock_count;++i)step.locks[i]=StepLock{};
        step.lock_count=uint8_t(kept);
    }};
    strip(target.pattern);for(auto& pattern:target.pattern_banks)strip(pattern);
    unit->control_tick();
}

// Ring's appended six controls require its explicit SNCC revision marker.
static void canonicalize_ring(Engine& target,int chain,int slot,bool modern) {
    auto* unit=static_cast<RingUnit*>(target.chains[chain].units[slot]);
    unit->set_modern(modern);
    const int first=modern?RingUnit::P_COUNT:RingUnit::LEGACY_COUNT;
    for(int p=first;p<PARAM_BANK_SIZE;++p){
        const char* name=unit->params[p].name;unit->params[p]=Param{};unit->params[p].name=name;
    }
    for(auto& scene:target.scenes[chain])if(scene.kind[slot]==UnitKind::Ring)
        for(int p=first;p<MAX_PARAMS;++p)scene.base[slot][p]=0;
    for(auto& pad:target.macro_pads){
        int kept=0;
        for(int i=0;i<pad.route_count;++i){const auto route=pad.routes[i];
            if(route.chain==chain&&route.slot==slot&&route.param>=first)continue;
            pad.routes[kept++]=route;
        }
        for(int i=kept;i<pad.route_count;++i)pad.routes[i]=MacroPadRoute{};
        pad.route_count=uint8_t(kept);
    }
    auto strip=[=](Pattern& pattern){for(auto& step:pattern.steps[chain]){
        int kept=0;
        for(int i=0;i<step.lock_count;++i){const auto lock=step.locks[i];
            if(lock.slot==slot&&lock.param>=first)continue;
            step.locks[kept++]=lock;
        }
        for(int i=kept;i<step.lock_count;++i)step.locks[i]=StepLock{};
        step.lock_count=uint8_t(kept);
    }};
    strip(target.pattern);for(auto& pattern:target.pattern_banks)strip(pattern);
    unit->control_tick();
}

// Strain's three additions are valid only in an explicitly marked SNCB card.
// Canonicalize after all chunks, never interpreting old padding as new sound.
static void canonicalize_strain(Engine& target,int chain,int slot,bool modern) {
    auto* unit=static_cast<StrainUnit*>(target.chains[chain].units[slot]);
    unit->set_modern(modern);
    const int first=modern?StrainUnit::P_COUNT:StrainUnit::LEGACY_COUNT;
    for(int p=first;p<PARAM_BANK_SIZE;++p){
        const char* name=unit->params[p].name;unit->params[p]=Param{};unit->params[p].name=name;
    }
    for(auto& scene:target.scenes[chain])if(scene.kind[slot]==UnitKind::Strain)
        for(int p=first;p<MAX_PARAMS;++p)scene.base[slot][p]=0;
    for(auto& pad:target.macro_pads){
        int kept=0;
        for(int i=0;i<pad.route_count;++i){const auto route=pad.routes[i];
            if(route.chain==chain&&route.slot==slot&&route.param>=first)continue;
            pad.routes[kept++]=route;
        }
        for(int i=kept;i<pad.route_count;++i)pad.routes[i]=MacroPadRoute{};
        pad.route_count=uint8_t(kept);
    }
    auto strip=[=](Pattern& pattern){for(auto& step:pattern.steps[chain]){
        int kept=0;
        for(int i=0;i<step.lock_count;++i){const auto lock=step.locks[i];
            if(lock.slot==slot&&lock.param>=first)continue;
            step.locks[kept++]=lock;
        }
        for(int i=kept;i<step.lock_count;++i)step.locks[i]=StepLock{};
        step.lock_count=uint8_t(kept);
    }};
    strip(target.pattern);for(auto& pattern:target.pattern_banks)strip(pattern);
}

// Only a marked modern REV may activate slots5..7. Run AFTER every chunk and
// cable-enabled mask, not at UNIT parsing time (which may precede CORE).
static void canonicalize_reverb(Engine& target, int chain, int slot, bool authored) {
    Unit* unit = target.chains[chain].units[slot];
    const int first = authored ? ReverbUnit::P_COUNT : ReverbUnit::P_PREDELAY;
    for (int p = first; p < PARAM_BANK_SIZE; ++p) {
        const char* name = unit->params[p].name;
        unit->params[p] = Param{};
        unit->params[p].name = name;
    }
    for (auto& scene : target.scenes[chain]) if (scene.kind[slot] == UnitKind::Reverb)
        for (int p = first; p < MAX_PARAMS; ++p) scene.base[slot][p] = 0;
    for (auto& pad : target.macro_pads) {
        int kept = 0;
        for (int i = 0; i < pad.route_count; ++i) {
            const auto& route = pad.routes[i];
            if (route.chain == chain && route.slot == slot && route.param >= first) continue;
            pad.routes[kept++] = route;
        }
        for (int i = kept; i < pad.route_count; ++i) pad.routes[i] = MacroPadRoute{};
        pad.route_count = uint8_t(kept);
    }
    auto strip = [=](Pattern& pattern) {
        for (auto& step : pattern.steps[chain]) {
            int kept = 0;
            for (int i = 0; i < step.lock_count; ++i) {
                const auto& lock = step.locks[i];
                if (lock.slot == slot && lock.param >= first) continue;
                step.locks[kept++] = lock;
            }
            for (int i = kept; i < step.lock_count; ++i) step.locks[i] = StepLock{};
            step.lock_count = uint8_t(kept);
        }
    };
    strip(target.pattern);
    for (auto& pattern : target.pattern_banks) strip(pattern);
}

struct ChunkWriter {
    FILE* f; long size_pos; long payload_pos; bool ok;
    ChunkWriter(FILE* file, const char id[4]) : f(file), size_pos(0), payload_pos(0), ok(true) {
        ok = wbytes(f, id, 4); size_pos = ftell(f); ok = ok && size_pos >= 0 && wu32(f, 0);
        payload_pos = ftell(f); ok = ok && payload_pos >= 0;
    }
    bool finish() {
        long end = ftell(f); if (!ok || end < payload_pos || (uint64_t)(end - payload_pos) > 0xffffffffu) return false;
        if (fseek(f, size_pos, SEEK_SET) != 0 || !wu32(f, (uint32_t)(end - payload_pos)) || fseek(f, end, SEEK_SET) != 0) return false;
        return true;
    }
};

static bool write_param(FILE* f, const PatchDocument::Impl::UnitData& unit, int p) {
    // Keep modern CORE's original eight controls byte-identical. Appended params
    // live only in PPOS/PHLD; an old reader cannot mistake them for legacy padding.
    if (unit.kind == (uint8_t)UnitKind::SamplePlayer && p >= PlayUnit::P_POSITION)
        return w16(f, 0) && w8(f, 0) && w16(f, 0);
    const auto& param = unit.params[p];
    return w16(f, param.base) && w8(f, param.src) && w16(f, param.depth);
}

static bool write_pattern_data(FILE* f, const Pattern& pattern) {
    bool ok = true;
    for (int ch = 0; ch < NUM_CHAINS && ok; ++ch) {
        uint8_t length = pattern.length[ch];
        if (length < 1 || length > SEQ_STEPS) length = 16;
        ok = ok && w8(f, length);
        for (int st = 0; st < SEQ_STEPS && ok; ++st) {
            const SeqStep& step = pattern.steps[ch][st];
            const uint8_t count = step.lock_count <= SEQ_LOCKS ? step.lock_count : SEQ_LOCKS;
            ok = ok && w8(f, step.flags) && w8(f, count);
            for (int i = 0; i < SEQ_LOCKS && ok; ++i) {
                const StepLock& lock = step.locks[i];
                ok = ok && w8(f, lock.slot) && w8(f, lock.param) && w16(f, lock.value);
            }
        }
    }
    return ok;
}

static bool write_pattern(FILE* f, const PatchDocument::Impl& document) {
    return w8(f, document.sequencer_enabled) && write_pattern_data(f, document.pattern);
}

static bool write_microtiming_data(FILE* f, const Pattern& pattern) {
    for (int ch = 0; ch < NUM_CHAINS; ++ch) for (int st = 0; st < SEQ_STEPS; ++st) {
        int value = pattern.microtiming[ch][st];
        if (value < -SEQ_MICROTIMING_MAX) value = -SEQ_MICROTIMING_MAX;
        if (value > SEQ_MICROTIMING_MAX) value = SEQ_MICROTIMING_MAX;
        if (!w8(f, (uint8_t)(int8_t)value)) return false;
    }
    return true;
}

static bool document_admitted(const PatchDocument::Impl& document, uint8_t& gran_reading_count) {
    if (!musical_scale::valid(document.scale)) return false;
    gran_reading_count = 0;
    for (const auto& chain : document.chains) for (const auto& unit : chain.units) {
        if (unit.kind != uint8_t(UnitKind::Grain)) continue;
        const bool authored = unit.gran_reading || unit.gran_reverse_percent != 50;
        if (unit.gran_reading > 3 || unit.gran_reverse_percent > 100 ||
            (authored && !(unit.flags & 8u)) || bool(unit.flags & 16u) != authored) return false;
        gran_reading_count += authored;
    }
    return true;
}

bool write_document(const PatchDocument& captured, FILE* f) {
    if (!f || !captured.impl_ || std::ftell(f) != 0) return false;
    const PatchDocument::Impl& document = *captured.impl_;
    uint8_t gran_reading_count = 0;
    if (!document_admitted(document,gran_reading_count)) return false;
    bool has_rot=false, has_rule=false, has_pent=false, has_paul=false, has_scratch=false, has_inst=false, extended_motor=false, extended_noise=false, extended_bit=false, extended_strain=false, extended_ring=false, extended_fold=false, extended_tilt=false, extended_vca=false;
    for(const auto& chain:document.chains)for(const auto& unit:chain.units) {
        has_rule |= unit.kind==uint8_t(UnitKind::Rule);
        has_rot |= unit.kind==uint8_t(UnitKind::Rot);
        has_pent |= unit.kind==uint8_t(UnitKind::Pent);
        has_paul |= unit.kind==uint8_t(UnitKind::Paul);
        has_scratch |= unit.kind==uint8_t(UnitKind::Scratch);
        has_inst |= unit.kind==uint8_t(UnitKind::Inst);
        extended_motor |= unit.kind==uint8_t(UnitKind::Motor) && (unit.flags&1u);
        extended_noise |= unit.kind==uint8_t(UnitKind::Noise) && (unit.flags&2u);
        extended_bit |= unit.kind==uint8_t(UnitKind::Bitcrush) && (unit.flags&1u);
        extended_strain |= unit.kind==uint8_t(UnitKind::Strain) && (unit.flags&1u);
        extended_ring |= unit.kind==uint8_t(UnitKind::Ring) && (unit.flags&1u);
        extended_vca |= unit.kind==uint8_t(UnitKind::Vca) && (unit.flags&2u);
        extended_tilt |= unit.kind==uint8_t(UnitKind::Tilt) && unit.flags==2u;
        extended_fold |= unit.kind==uint8_t(UnitKind::Fold) && (unit.flags&1u);
    }
    // A scene-only PAUL kind must not be written into a pre17 container either.
    for (const auto& chain : document.scenes) for (const auto& scene : chain)
        for (UnitKind kind : scene.kind) {
            has_rule |= kind == UnitKind::Rule;
            has_rot |= kind == UnitKind::Rot;
            has_paul |= kind == UnitKind::Paul;
            has_pent |= kind == UnitKind::Pent; // invalid scene retains authored kind too
        }
    const uint8_t version=has_rot ? 22 : has_rule ? 21 : has_pent ? 20 : gran_reading_count ? 18 : has_paul ? 17 : (has_scratch || has_inst) ? 16 : extended_vca ? 15 : extended_tilt ? 14 : extended_fold ? 13 : extended_ring ? 12 : extended_strain ? 11 : extended_bit ? 10 : extended_noise ? 9 : extended_motor ? 8 : VERSION7;
    bool ok = wbytes(f, has_rot ? MAGIC22 : has_rule ? MAGIC21 : has_pent ? MAGIC20 : gran_reading_count ? MAGIC18 : has_paul ? MAGIC17 : (has_scratch || has_inst) ? MAGIC16 : extended_vca ? MAGIC15 : extended_tilt ? MAGIC14 : extended_fold ? MAGIC13 : extended_ring ? MAGIC12 : extended_strain ? MAGIC11 : extended_bit ? MAGIC10 : extended_noise ? MAGIC9 : extended_motor ? MAGIC8 : MAGIC7, 4) && w8(f, version);

    if (ok) {
        ChunkWriter c(f, "CORE");
        for (int ch = 0; ch < NUM_CHAINS; ++ch) {
            const auto& chain = document.chains[ch]; c.ok = c.ok && w16(f, chain.volume) && w16(f, chain.pan) && w8(f, chain.mute);
            for (int s = 0; s < UNITS_PER_CHAIN; ++s) {
                const auto& unit = chain.units[s];
                c.ok = c.ok && w8(f, unit.kind) && w8(f, unit.bypass) && w8(f, unit.extra);
                for (int p = 0; p < PARAM_BANK_SIZE; ++p) c.ok = c.ok && write_param(f, unit, p);
            }
        }
        ok = c.finish();
    }
    if (ok) {
        ChunkWriter c(f, "SIGL");
        for (int i = 0; i < LEGACY_SIGIL_COUNT; ++i) {
            const auto& sigil = document.sigils[i]; c.ok = c.ok && w8(f, sigil.mode);
            for (int p = 0; p < SIGIL_POINTS; ++p) c.ok = c.ok && w16(f, sigil.table[p]);
            c.ok = c.ok && w16(f, sigil.rate) && w16(f, sigil.depth);
        }
        ok = c.finish();
    }
    if (ok) {
        ChunkWriter c(f, "ROUT");
        c.ok = c.ok && wu16(f, document.bpm);
        for (int i = 0; i < LEGACY_SIGIL_COUNT; ++i) c.ok = c.ok && w8(f, document.sigils[i].mod_src) && w16(f, document.sigils[i].mod_depth);
        for (int i = 0; i < LEGACY_SIGIL_COUNT; ++i) c.ok = c.ok && w8(f, document.sigils[i].sync) && w8(f, document.sigils[i].division) && w8(f, document.sigils[i].step_dest);
        ok = c.finish();
    }
    if (ok) {
        uint8_t count = 0;
        for (const auto& chain : document.chains) for (const auto& unit : chain.units)
            if (unit.random_source) ++count;
        if (count) {
            ChunkWriter c(f, "MRND");
            c.ok = c.ok && w8(f, 1) && w8(f, count);
            for (int ch=0; ch<NUM_CHAINS; ++ch) for (int slot=0; slot<UNITS_PER_CHAIN; ++slot) {
                const auto& unit=document.chains[ch].units[slot];
                if (unit.random_source) c.ok = c.ok && w8(f, uint8_t(ch)) && w8(f, uint8_t(slot)) && w8(f, unit.random_source);
            }
            ok = c.finish();
        }
    }
    if (ok && (document.clock_mods[0].src != ModSource::None || document.clock_mods[0].depth ||
               document.clock_mods[1].src != ModSource::None || document.clock_mods[1].depth)) {
        ChunkWriter c(f, "CLCK");
        c.ok = c.ok && w8(f, 1) && w8(f, 2);
        for (const auto& mod : document.clock_mods)
            c.ok = c.ok && w8(f, (uint8_t)mod.src) && w16(f, mod.depth);
        ok = c.finish();
    }
    if (ok) {
        ChunkWriter c(f, "UNIT");
        for (int ch = 0; ch < NUM_CHAINS; ++ch) for (int s = 0; s < UNITS_PER_CHAIN; ++s) c.ok = c.ok && w8(f, document.chains[ch].units[s].flags);
        ok = c.finish();
    }
    if(ok && has_rot) {
        uint8_t count=0,law=0;for(const auto& c:document.chains)for(const auto& u:c.units)if(u.kind==uint8_t(UnitKind::Rot)){++count;law|=u.rot_config.law;}
        // ROTSv1 (9-byte entries, bytes as before the law field) while every entry is law 0;
        // any other law writes ROTSv2: every entry gains a trailing u8 law (10 bytes).
        const bool wide=law!=0;
        if(count) {
            ChunkWriter c(f,"ROTS");c.ok=w8(f,wide?2:1)&&w8(f,count);
            for(int ch=0;ch<NUM_CHAINS;++ch)for(int s=0;s<UNITS_PER_CHAIN;++s) {
                const auto& u=document.chains[ch].units[s];if(u.kind!=uint8_t(UnitKind::Rot))continue;
                const auto& cfg=u.rot_config;
                c.ok=c.ok && RotUnit::valid_config(cfg) && u.extra<64 && !u.flags && w8(f,ch)&&w8(f,s)&&
                    wu32(f,cfg.seed)&&wu16(f,cfg.passes)&&w8(f,uint8_t(cfg.mode))&&(!wide || w8(f,cfg.law));
            }
            ok=c.finish();
        }
    }
    if(ok && has_rule) {
        uint8_t count=0;for(const auto& c:document.chains)for(const auto& u:c.units)count+=u.kind==uint8_t(UnitKind::Rule);
        if(count) {
            ChunkWriter c(f,"RULE");c.ok=w8(f,1)&&w8(f,count);
            for(int ch=0;ch<NUM_CHAINS;++ch)for(int s=0;s<UNITS_PER_CHAIN;++s) {
                const auto& u=document.chains[ch].units[s];if(u.kind!=uint8_t(UnitKind::Rule))continue;
                const auto& cfg=u.rule_config;
                c.ok=c.ok && RuleUnit::valid_config(cfg) && u.extra<64 && !u.flags && w8(f,ch)&&w8(f,s)&&
                    wu32(f,cfg.seed)&&wu16(f,cfg.seed_row)&&w8(f,cfg.phrase)&&w8(f,cfg.source_root);
            }
            ok=c.finish();
        }
    }
    if (ok && has_pent) {
        uint8_t count = 0;
        for (const auto& chain : document.chains) for (const auto& unit : chain.units)
            count += unit.kind == uint8_t(UnitKind::Pent);
        if (count) {
            ChunkWriter c(f, "PENT");
            c.ok = w8(f, 1) && w8(f, count);
            for (int ch = 0; ch < NUM_CHAINS && c.ok; ++ch) for (int s = 0; s < UNITS_PER_CHAIN && c.ok; ++s) {
                const auto& unit = document.chains[ch].units[s];
                if (unit.kind != uint8_t(UnitKind::Pent)) continue;
                c.ok = unit.pent_width <= 1 && !unit.extra && !unit.flags &&
                    w8(f, ch) && w8(f, s) && w8(f, unit.pent_width);
            }
            ok = c.finish();
        }
    }
    if (ok && has_paul) {
        uint8_t count = 0;
        bool directed = false;
        for (const auto& chain : document.chains) for (const auto& unit : chain.units)
            if (unit.kind == uint8_t(UnitKind::Paul)) {
                ++count;
                directed |= unit.paul_direction != 0;
            }
        // No empty PAUL chunk for scene-only state; every actual card is required.
        // Keep v1 byte-exact for all-forward cards. Old v1 readers reject v2.
        if (count) {
            ChunkWriter c(f, "PAUL");
            c.ok = c.ok && w8(f, directed ? 2 : 1) && w8(f, count);
            for (int ch=0; ch<NUM_CHAINS && c.ok; ++ch) for (int s=0; s<UNITS_PER_CHAIN && c.ok; ++s) {
                const auto& unit = document.chains[ch].units[s];
                if (unit.kind != uint8_t(UnitKind::Paul)) continue;
                c.ok = unit.paul_mode <= 1 && unit.paul_direction <= 2 && !unit.flags &&
                    unit.extra < SampleManifest::SLOT_COUNT &&
                    w8(f, ch) && w8(f, s) && w8(f, unit.paul_mode);
                if (directed) c.ok = c.ok && w8(f, unit.paul_direction);
            }
            ok = c.finish();
        }
    }
    if (ok && has_scratch) {
        uint8_t count = 0;
        for (const auto& chain : document.chains) for (const auto& unit : chain.units)
            count += unit.kind == uint8_t(UnitKind::Scratch);
        bool custom_axes = false;
        for (const auto& chain : document.chains) for (const auto& unit : chain.units)
            if (unit.kind == uint8_t(UnitKind::Scratch))
                custom_axes |= unit.scratch_axis_x != -1 || unit.scratch_axis_y != -1;
        ChunkWriter c(f, "SCRT");
        c.ok = w8(f, custom_axes ? 2 : 1) && w8(f, count);
        for (int ch=0; ch<NUM_CHAINS && c.ok; ++ch) for (int s=0; s<UNITS_PER_CHAIN && c.ok; ++s) {
            const auto& unit = document.chains[ch].units[s];
            if (unit.kind != uint8_t(UnitKind::Scratch)) continue;
            c.ok = scratch_axes_valid(unit.scratch_mode, unit.scratch_map,
                    unit.scratch_axis_x, unit.scratch_axis_y) &&
                unit.extra < SampleManifest::SLOT_COUNT && w8(f, ch) && w8(f, s) &&
                w8(f, unit.scratch_mode) && w8(f, unit.scratch_map);
            if (custom_axes) c.ok = c.ok && w8(f, uint8_t(unit.scratch_axis_x)) &&
                w8(f, uint8_t(unit.scratch_axis_y));
        }
        ok = c.finish();
    }
    if (ok) {
        uint8_t count = 0;
        for (const auto& chain : document.chains) for (const auto& unit : chain.units)
            if (unit.kind == (uint8_t)UnitKind::Glitch) ++count;
        if (count) {
            // GLCH v1: count bounded by 24 slots, exact cells + independent RNG,
            // never live PCM, transport/read heads, or a queued reroll command.
            ChunkWriter c(f, "GLCH");
            c.ok = w8(f, 1) && w8(f, count);
            for (int ch=0; ch<NUM_CHAINS && c.ok; ++ch) for (int s=0; s<UNITS_PER_CHAIN && c.ok; ++s) {
                const auto& unit = document.chains[ch].units[s];
                if (unit.kind != (uint8_t)UnitKind::Glitch) continue;
                const auto& state = unit.glitch_pattern;
                c.ok = GlitchUnit::valid_pattern(state) && w8(f, ch) && w8(f, s) &&
                    wu32(f, state.rng) && wu32(f, state.revision);
                for (const auto& cell : state.cells) c.ok = c.ok && w8(f, (uint8_t)cell.op) &&
                    w8(f, cell.source) && w8(f, (uint8_t)cell.semitones) && w8(f, cell.repeats);
            }
            ok = c.finish();
        }
    }
    if (ok) {
        uint8_t count = 0;
        for (const auto& chain : document.chains) for (const auto& unit : chain.units)
            if (unit.play_edit.enabled) ++count;
        if (count) {
            ChunkWriter c(f, "PEDT");
            c.ok = w8(f, 1) && w8(f, count);
            for (int ch = 0; ch < NUM_CHAINS && c.ok; ++ch)
                for (int s = 0; s < UNITS_PER_CHAIN && c.ok; ++s) {
                    const auto& u = document.chains[ch].units[s];
                    const auto& e = u.play_edit;
                    if (!e.enabled) continue;
                    c.ok = u.kind == (uint8_t)UnitKind::SamplePlayer && (u.flags & 2u) &&
                        play_edit_valid(e) && w8(f, (uint8_t)ch) && w8(f, (uint8_t)s) && w8(f, u.extra) &&
                        wu32(f, e.source_frames) && wu32(f, e.start) && wu32(f, e.end) &&
                        wu32(f, e.fade_in) && wu32(f, e.fade_out) && wu32(f, e.loop_start) &&
                        wu32(f, e.loop_end) && wu32(f, e.crossfade) &&
                        w16(f, e.anchor_start) && w16(f, e.anchor_length);
                }
            ok = c.finish();
        }
    }
    if (ok) {
        uint8_t count = 0;
        for (const auto& chain : document.chains) for (const auto& unit : chain.units) count += unit.play_position;
        if (count) {
            ChunkWriter c(f, "PPOS");
            c.ok = w8(f, 1) && w8(f, count);
            for (int ch = 0; ch < NUM_CHAINS && c.ok; ++ch) for (int slot = 0; slot < UNITS_PER_CHAIN && c.ok; ++slot) {
                const auto& u = document.chains[ch].units[slot];
                if (!u.play_position) continue;
                c.ok = w8(f, ch) && w8(f, slot);
                for (int p = PlayUnit::P_POSITION; p <= PlayUnit::P_SCRUB && c.ok; ++p) {
                    const auto& v = u.params[p];
                    c.ok = v.base >= 0 && v.src < (uint8_t)ModSource::Count && w16(f, v.base) && w8(f, v.src) && w16(f, v.depth);
                }
            }
            ok = c.finish();
        }
    }
    if (ok) {
        uint8_t count = 0;
        for (const auto& chain : document.chains) for (const auto& unit : chain.units) count += unit.play_hold;
        if (count) {
            ChunkWriter c(f, "PHLD");
            c.ok = w8(f, 1) && w8(f, count);
            for (int ch = 0; ch < NUM_CHAINS && c.ok; ++ch) for (int slot = 0; slot < UNITS_PER_CHAIN && c.ok; ++slot) {
                const auto& u = document.chains[ch].units[slot];
                if (!u.play_hold) continue;
                const auto& v = u.params[PlayUnit::P_HOLD];
                c.ok = w8(f, ch) && w8(f, slot) && v.base >= 0 && v.src < (uint8_t)ModSource::Count &&
                    w16(f, v.base) && w8(f, v.src) && w16(f, v.depth);
            }
            ok = c.finish();
        }
    }
    // PNOT does not reuse CORE padding or the existing PPOS/PHLD layouts.
    if (ok) {
        uint8_t count = 0;
        for (const auto& chain : document.chains) for (const auto& unit : chain.units) count += unit.play_note;
        if (count) {
            ChunkWriter c(f, "PNOT");
            c.ok = w8(f, 1) && w8(f, count);
            for (int ch = 0; ch < NUM_CHAINS && c.ok; ++ch) for (int slot = 0; slot < UNITS_PER_CHAIN && c.ok; ++slot) {
                const auto& u = document.chains[ch].units[slot];
                if (!u.play_note) continue;
                const auto& v = u.params[PlayUnit::P_NOTE];
                c.ok = u.kind == uint8_t(UnitKind::SamplePlayer) && (u.flags & 2u) && u.play_pitch_mode <= 2 &&
                    v.base >= 0 && v.src < uint8_t(ModSource::Count) && w8(f, ch) && w8(f, slot) &&
                    w8(f, u.play_pitch_mode) && w16(f, v.base) && w8(f, v.src) && w16(f, v.depth);
            }
            ok = c.finish();
        }
    }
    // Optional editor metadata; original ARP controls/OFF values stay byte-exact.
    // Older SNC7 readers skip ARPM and still hear precisely the same note pool.
    if (ok) {
        uint8_t count = 0;
        for (const auto& chain : document.chains) for (const auto& unit : chain.units)
            count += unit.arp_recall.mask != 0;
        if (count) {
            ChunkWriter c(f, "ARPM");
            c.ok = w8(f, 1) && w8(f, count);
            for (int ch = 0; ch < NUM_CHAINS && c.ok; ++ch) for (int slot = 0; slot < UNITS_PER_CHAIN && c.ok; ++slot) {
                const auto& u = document.chains[ch].units[slot];
                if (!u.arp_recall.mask) continue;
                c.ok = u.kind == uint8_t(UnitKind::Arp) && w8(f, ch) && w8(f, slot) && w8(f, u.arp_recall.mask);
                for (q15 value : u.arp_recall.values) c.ok = c.ok && w16(f, value);
            }
            ok = c.finish();
        }
    }
    bool any_exact_native = false;
    for (bool value : document.sample_exact_native) any_exact_native = any_exact_native || value;
    if (ok && any_exact_native) {
        ChunkWriter c(f, "SEXT");
        c.ok = w8(f, 1) && w8(f, SampleManifest::SLOT_COUNT);
        for (bool value : document.sample_exact_native) c.ok = c.ok && w8(f, value ? 1 : 0);
        ok = c.finish();
    }
    if (ok) {
        ChunkWriter c(f, "SAMP");
        for (int slot = 0; slot < SampleManifest::SLOT_COUNT; ++slot) {
            const auto& p = document.samples[slot]; size_t raw = std::strlen(p.path);
            uint16_t len = (uint16_t)(raw > sizeof(p.path) - 1 ? sizeof(p.path) - 1 : raw);
            c.ok = c.ok && w8(f, (uint8_t)p.origin) && w8(f, p.missing ? 1 : 0) && wu16(f, len) && wbytes(f, p.path, len);
        }
        ok = c.finish();
    }
    if (ok) {
        // Optional slot-shared PLAY cut metadata. This deliberately does not
        // become UNIT state because every PLAY/GRAN alias sees the same sample.
        ChunkWriter c(f, "CHOP");
        c.ok = c.ok && w8(f, 1) && w8(f, SampleManifest::SLOT_COUNT) &&
               w8(f, trackr::synth::Sample::MAX_CHOPS);
        for (int slot = 0; slot < SampleManifest::SLOT_COUNT && c.ok; ++slot)
            for (int chop = 0; chop < trackr::synth::Sample::MAX_CHOPS && c.ok; ++chop)
                c.ok = wu32(f, document.sample_chops[slot][chop]);
        ok = c.finish();
    }
    if (ok) {
        // Optional per-slice direction. It is slot-shared for the same reason as
        // CHOP and stays a separate chunk so old SNC7 readers skip it safely.
        ChunkWriter c(f, "SREV");
        c.ok = c.ok && w8(f, 1) && w8(f, SampleManifest::SLOT_COUNT);
        for (int slot = 0; slot < SampleManifest::SLOT_COUNT && c.ok; ++slot)
            c.ok = wu16(f, document.sample_slice_reverse[slot]);
        ok = c.finish();
    }
    bool any_cut_speed = false;
    for (const auto& speeds : document.sample_slice_speed) for (auto speed : speeds) any_cut_speed |= speed != 256;
    if (ok && any_cut_speed) {
        ChunkWriter c(f, "PSPD");
        c.ok = w8(f, 1) && w8(f, SampleManifest::SLOT_COUNT) && w8(f, trackr::synth::Sample::MAX_CHOPS);
        for (const auto& speeds : document.sample_slice_speed) for (auto speed : speeds)
            c.ok = c.ok && trackr::synth::Sample::cut_speed(speed) == speed && wu16(f, speed);
        ok = c.finish();
    }
    bool any_cut_pitch = false;
    for (const auto& pitches : document.sample_slice_pitch) for (auto pitch : pitches) any_cut_pitch |= pitch != 0;
    if (ok && any_cut_pitch) {
        // Independent optional SPITv1; old readers skip, no container bump.
        ChunkWriter c(f, "SPIT");
        c.ok = w8(f, 1) && w8(f, SampleManifest::SLOT_COUNT) && w8(f, trackr::synth::Sample::MAX_CHOPS);
        for (const auto& pitches : document.sample_slice_pitch) for (auto pitch : pitches)
            c.ok = c.ok && play_part_pitch::valid(pitch) && w8(f, (uint8_t)pitch);
        ok = c.finish();
    }
    if (ok) {
        ChunkWriter c(f, "TRNS");
        c.ok = c.ok && w8(f, document.transport_playing) && w8(f, document.beats_per_bar) &&
               w8(f, document.launch_quantize) && w8(f, document.swing_percent) &&
               w8(f, document.pattern_launch_quantize);
        ok = c.finish();
    }
    if (ok) {
        ChunkWriter c(f, "PTRN");
        c.ok = c.ok && write_pattern(f, document);
        ok = c.finish();
    }
    if (ok) {
        ChunkWriter c(f, "PBNK");
        c.ok = c.ok && w8(f, document.active_pattern);
        for (int bank = 0; bank < NUM_PATTERNS && c.ok; ++bank) {
            const Pattern& saved = bank == document.active_pattern ? document.pattern : document.pattern_banks[bank];
            c.ok = write_pattern_data(f, saved);
        }
        ok = c.finish();
    }
    if (ok) {
        // Optional per-chain traversal mode for every pattern bank. Keeping this
        // outside PTRN/PBNK lets older SNC7 readers skip it without misaligning
        // their fixed pattern records; absence means Forward for every chain.
        ChunkWriter c(f, "SDIR");
        c.ok = c.ok && w8(f, 1) && w8(f, NUM_PATTERNS) && w8(f, NUM_CHAINS);
        for (int bank = 0; bank < NUM_PATTERNS && c.ok; ++bank) {
            const Pattern& saved = bank == document.active_pattern ? document.pattern : document.pattern_banks[bank];
            for (int chain = 0; chain < NUM_CHAINS && c.ok; ++chain) {
                SequenceDirection direction = saved.direction[chain];
                if (direction >= SequenceDirection::Count) direction = SequenceDirection::Forward;
                c.ok = w8(f, (uint8_t)direction);
            }
        }
        ok = c.finish();
    }
    if (ok) {
        ChunkWriter c(f, "MTIM");
        c.ok = c.ok && w8(f, 1) && w8(f, SEQ_MICROTIMING_DIVISIONS) &&
               w8(f, NUM_PATTERNS) && w8(f, NUM_CHAINS) && w8(f, SEQ_STEPS);
        for (int bank = 0; bank < NUM_PATTERNS && c.ok; ++bank) {
            const Pattern& saved = bank == document.active_pattern ? document.pattern : document.pattern_banks[bank];
            c.ok = write_microtiming_data(f, saved);
        }
        ok = c.finish();
    }
    if (ok) {
        ChunkWriter c(f, "PCHN");
        c.ok = c.ok && w8(f, 1) && w8(f, document.pattern_chain_length) &&
               w8(f, document.pattern_chain_loop) && w8(f, PATTERN_CHAIN_STEPS);
        for (int i = 0; i < PATTERN_CHAIN_STEPS && c.ok; ++i) {
            const PatternChainStep& entry = document.pattern_chain[i];
            const uint8_t pattern = entry.pattern < NUM_PATTERNS ? entry.pattern : 0;
            const uint8_t bars = entry.bars >= 1 && entry.bars <= 16 ? entry.bars : 1;
            c.ok = w8(f, pattern) && w8(f, bars);
        }
        ok = c.finish();
    }
    if (ok) {
        // STRN is a legacy-migration chunk in SNC7. CORE is authoritative for
        // the twelve ParamBank records, so only non-Param mode bytes live here.
        ChunkWriter c(f, "STRN");
        for (int ch = 0; ch < NUM_CHAINS; ++ch) for (int s = 0; s < UNITS_PER_CHAIN; ++s) {
            const auto& unit = document.chains[ch].units[s];
            c.ok = c.ok && w8(f, unit.strn_window) && w8(f, unit.strn_division) && w8(f, unit.strn_long);
        }
        ok = c.finish();
    }

    if (ok) {
        ChunkWriter c(f, "SCNE");
        for (int ch=0;ch<NUM_CHAINS;++ch) {
            c.ok=c.ok&&w16(f,document.scene_morph[ch]);
            for(int n=0;n<2;++n){const PersistentScene& sc=document.scenes[ch][n];c.ok=c.ok&&w8(f,sc.valid?1:0)&&w16(f,sc.volume)&&w16(f,sc.pan);for(int s=0;s<UNITS_PER_CHAIN;++s){c.ok=c.ok&&w8(f,(uint8_t)sc.kind[s]);for(int p=0;p<MAX_PARAMS;++p)c.ok=c.ok&&w16(f,sc.base[s][p]);}}
        }
        ok=c.finish();
    }
    if (ok) {
        ChunkWriter c(f, "MPAD");
        for(int p=0;p<8;++p){const MacroPad& pad=document.macro_pads[p];c.ok=c.ok&&wbytes(f,pad.name,sizeof pad.name)&&w8(f,pad.route_count);for(int r=0;r<MacroPad::MAX_ROUTES;++r){const MacroPadRoute& x=pad.routes[r];c.ok=c.ok&&w8(f,x.chain)&&w8(f,x.slot)&&w8(f,x.param)&&w16(f,x.minimum)&&w16(f,x.maximum);}}
        ok=c.finish();
    }

    // GRAN performance is separate from UNIT's frozen/rejected flag namespace.
    // Emit only authored nondefaults: default flow/linear saves remain byte-identical.
    if (ok) {
        uint8_t count = 0;
        for (const auto& chain : document.chains) for (const auto& unit : chain.units)
            if (unit.gran_perform) ++count;
        if (count) {
            ChunkWriter c(f, "GRNP");
            c.ok = c.ok && w8(f, 1) && w8(f, count);
            for (int ch = 0; ch < NUM_CHAINS; ++ch) for (int slot = 0; slot < UNITS_PER_CHAIN; ++slot) {
                const auto& unit = document.chains[ch].units[slot];
                if (!unit.gran_perform) continue;
                c.ok = c.ok && w8(f, ch) && w8(f, slot) && w8(f, unit.gran_mode) &&
                    w8(f, unit.gran_window) && w8(f, unit.gran_count);
            }
            ok = c.finish();
        }
    }

    if (ok && gran_reading_count) {
        ChunkWriter c(f, "GRNX");
        c.ok = w8(f, 1) && w8(f, gran_reading_count);
        for (int ch=0; ch<NUM_CHAINS; ++ch) for (int slot=0; slot<UNITS_PER_CHAIN; ++slot) {
            const auto& unit = document.chains[ch].units[slot];
            if (unit.kind != uint8_t(UnitKind::Grain) || !(unit.flags & 16u)) continue;
            c.ok = c.ok && w8(f, ch) && w8(f, slot) && w8(f, unit.gran_reading) && w8(f, unit.gran_reverse_percent);
        }
        ok = c.finish();
    }

    // Physical bindings are optional patch state. Older SNC7 readers skip this
    // chunk; newer readers retain constructor defaults when it is absent.
    if (ok) {
        ChunkWriter c(f, "PBND");
        c.ok = c.ok && w8(f, 1) && w8(f, NUM_PERFORMANCE_BINDINGS);
        for (int i = 0; i < NUM_PERFORMANCE_BINDINGS && c.ok; ++i) {
            const PerformanceBinding& binding = document.performance_bindings[i];
            c.ok = w8(f, (uint8_t)binding.action) && w8(f, binding.target) &&
                   w8(f, (uint8_t)binding.mode);
        }
        ok = c.finish();
    }

    // USIG v1: complete eight-sigil state plus cable mute. SIGL/ROUT stay four-wide.
    if (ok) {
        ChunkWriter c(f, "USIG");
        c.ok = c.ok && w8(f, 1) && w8(f, NUM_SIGILS) && w8(f, NUM_CHAINS) &&
            w8(f, UNITS_PER_CHAIN) && w8(f, PARAM_BANK_SIZE);
        for (const auto& s : document.sigils) {
            const auto& cfg = s.config;
            const uint8_t flags = (cfg.enabled ? 1 : 0) | (cfg.legacy ? 2 : 0) |
                (cfg.unipolar ? 4 : 0) | (s.drawn ? 8 : 0) | (cfg.legacy_rate ? 16 : 0);
            c.ok = c.ok && w8(f,flags) && w8(f,(uint8_t)cfg.shape) && w8(f,(uint8_t)cfg.restart) &&
                wu16(f,cfg.rate_millihz) && wu16(f,cfg.phase_degrees) && w8(f,s.mode) &&
                w16(f,s.rate) && w16(f,s.depth) && w8(f,s.mod_src) && w16(f,s.mod_depth) &&
                w8(f,s.sync) && w8(f,s.division) && w8(f,s.step_dest);
            for (q15 value : s.table) c.ok = c.ok && w16(f,value);
        }
        for (const auto& chain : document.chains) for (const auto& unit : chain.units) {
            uint16_t disabled = 0;
            for (int p = 0; p < PARAM_BANK_SIZE; ++p) if (!unit.params[p].enabled) disabled |= 1u << p;
            c.ok = c.ok && wu16(f, disabled);
        }
        ok = c.finish();
    }

    // Global follower configuration, independent of stable ModSource route ids.
    if (ok) {
        ChunkWriter c(f, "ENVR");
        c.ok = c.ok && w8(f, 1) && w8(f, document.env_chain) && w8(f, document.env_response);
        ok = c.finish();
    }

    // Optional UI metadata. SNC7 readers that predate the patch table skip WSPC;
    // files without it receive deterministic defaults on load.
    if (ok) {
        ChunkWriter c(f, "WSPC");
        c.ok = c.ok && w8(f, 1) && w8(f, NUM_CHAINS) && w8(f, UNITS_PER_CHAIN);
        for (int chain = 0; chain < NUM_CHAINS && c.ok; ++chain)
            for (int slot = 0; slot < UNITS_PER_CHAIN && c.ok; ++slot) {
                const WorkspaceNodePosition& point = document.workspace.node[chain][slot];
                c.ok = w16(f, point.x) && w16(f, point.y);
            }
        ok = c.finish();
    }

    // Append without moving/changing any former chunk, including default saves.
    if (ok && !musical_scale::equal(document.scale, MusicalScale{})) {
        ChunkWriter c(f, "SCAL");
        c.ok = c.ok && w8(f, 1) && w8(f, document.scale.tonic) && w8(f, uint8_t(document.scale.mode));
        ok = c.finish();
    }
    return ok && !std::ferror(f);
}

bool save(const PatchDocument& captured, const char* path) {
    return save(captured, path, nullptr);
}

bool save(const PatchDocument& captured, const char* path, SaveFailure* failure) {
    SaveFailure ignored = SaveFailure::None;
    SaveFailure& why = failure ? *failure : ignored;
    why = SaveFailure::None;
    uint8_t gran_reading_count = 0;
    if (!path || !captured.impl_ || !document_admitted(*captured.impl_,gran_reading_count)) {
        why = SaveFailure::Invalid; return false;
    }
    char previous[256];
    const int n = std::snprintf(previous, sizeof previous, "%s.bak", path);
    if (n < 0 || size_t(n) >= sizeof previous) { why = SaveFailure::Invalid; return false; }
    fileio::OwnedFile temporary;
    if (!fileio::create_temporary(temporary, path)) { why = SaveFailure::Create; return false; }
    const bool ok = write_document(captured, temporary.stream());
    const bool closed = temporary.close(ok);
    if (!ok || !closed) { why = SaveFailure::Write; return false; }
    // Never rename onto an existing name: the previous version becomes .bak.
    if (!temporary.publish_replacing(path, previous)) { why = SaveFailure::Publish; return false; }
    return true;
}

bool save(const Engine& eng, const char* path) {
    PatchDocument document;
    return capture(eng, document) && save(document, path);
}

static bool read_core(FILE* f, Engine& target, int param_count,
                      uint8_t (&raw_extra)[NUM_CHAINS][UNITS_PER_CHAIN], uint8_t format) {
    bool ok = param_count >= 0 && param_count <= PARAM_BANK_SIZE;
    for (int c = 0; c < NUM_CHAINS && ok; ++c) {
        int16_t vol, pan; uint8_t mute; ok = r16(f, vol) && r16(f, pan) && r8(f, mute);
        if (!ok) break;
        target.chains[c].volume = vol;
        target.chains[c].pan = clamp_pan(pan);
        target.chains[c].mute = mute != 0;
        for (int s = 0; s < UNITS_PER_CHAIN && ok; ++s) {
            uint8_t kind, bypass, extra; ok = r8(f, kind) && r8(f, bypass) && r8(f, extra);
            // Validate before construction, not behind the required-chunks gate.
            if(ok && kind==uint8_t(UnitKind::Rule) && (format<21 || extra>=64))return false;
            if(ok && kind==uint8_t(UnitKind::Rot) && (format<22 || extra>=64))return false;
            if (ok && kind == uint8_t(UnitKind::Pent) && (format < 20 || extra)) return false;
            if (ok && kind == uint8_t(UnitKind::Paul) && format < 17) return false;
            if(ok && kind==uint8_t(UnitKind::Relic))return false;
            if (ok) raw_extra[c][s] = extra;
            Unit* u = ok && kind < (uint8_t)UnitKind::Count ? make_loaded_unit((UnitKind)kind) : nullptr;
            if (ok && kind != (uint8_t)UnitKind::Empty && !u) ok = false;
            if (u) { u->bypass = bypass != 0; apply_extra(u, extra); }
            for (int p = 0; p < param_count && ok; ++p) { int16_t base, depth; uint8_t src; ok = r16(f, base) && r8(f, src) && r16(f, depth) && src < (uint8_t)ModSource::Count && (kind != uint8_t(UnitKind::Pent) || base >= 0); if (ok) apply_param(u, p, base, src, depth); }
            if (!ok || !target.can_install_unit(u,target.chains[c].units[s])) { delete u;ok=false;break; } delete target.replace_unit(c, s, u);
        }
    }
    return ok;
}
static bool read_sigils(FILE* f, Engine& target) {
    bool ok = true;
    for (int i = 0; i < LEGACY_SIGIL_COUNT && ok; ++i) {
        Sigil& sg = target.sigils[i]; uint8_t mode; ok = r8(f, mode) && mode < (uint8_t)SigilMode::Count; if (ok) sg.mode = (SigilMode)mode;
        for (int p = 0; p < SIGIL_POINTS && ok; ++p) { int16_t v; ok = r16(f, v); if (ok) sg.table[p] = v; }
        int16_t rate, depth; ok = ok && r16(f, rate) && r16(f, depth);
        if (ok) { sg.drawn = false; for (int p = 0; p < SIGIL_POINTS; ++p) if (sg.table[p]) { sg.drawn = true; break; } target.set_sigil_rate(i, rate); sg.depth = depth; sg.trigger(); sg.value = 0; }
    }
    return ok;
}
static bool read_strn_modes(FILE* f, Engine& target) {
    bool ok = true;
    for (int c = 0; c < NUM_CHAINS && ok; ++c) for (int s = 0; s < UNITS_PER_CHAIN && ok; ++s) {
        uint8_t window, div, long_mode; ok = r8(f, window) && r8(f, div) && r8(f, long_mode);
        Unit* u = target.chains[c].units[s];
        if (ok && u && u->kind == UnitKind::Stranular) {
            auto* st = static_cast<StranularUnit*>(u);
            st->window_mode = window < 3 ? window : 0;
            st->sync_division = div < 4 ? div : 0;
            st->long_mode = long_mode != 0;
            st->set_transport_bpm(target.bpm);
        }
    }
    return ok;
}

static bool read_pattern_data(FILE* f, Pattern& parsed) {
    bool ok = true;
    for (int ch = 0; ch < NUM_CHAINS && ok; ++ch) {
        uint8_t length = 16;
        ok = r8(f, length);
        parsed.length[ch] = (length >= 1 && length <= SEQ_STEPS) ? length : 16;
        for (int st = 0; st < SEQ_STEPS && ok; ++st) {
            SeqStep& step = parsed.steps[ch][st];
            uint8_t flags = 0, count = 0;
            ok = r8(f, flags) && r8(f, count);
            if (!ok || count > SEQ_LOCKS) { ok = false; break; }
            step.flags = flags & (SeqStep::Trig | SeqStep::ConditionMask | SeqStep::RetrigMask);
            if (step.condition() >= SeqStep::Condition::Count)
                step.set_condition(SeqStep::Condition::Always);
            step.lock_count = count;
            for (int i = 0; i < SEQ_LOCKS && ok; ++i) {
                uint8_t slot = 0, param = 0; int16_t value = 0;
                ok = r8(f, slot) && r8(f, param) && r16(f, value);
                if (!ok) break;
                if (i < count) {
                    if (slot >= UNITS_PER_CHAIN || param >= MAX_PARAMS) { ok = false; break; }
                    for (int previous = 0; previous < i; ++previous) {
                        if (step.locks[previous].slot == slot && step.locks[previous].param == param) {
                            ok = false;
                            break;
                        }
                    }
                    if (!ok) break;
                    step.locks[i] = {slot, param, value};
                }
            }
        }
    }
    return ok;
}

static bool read_pattern(FILE* f, Engine& target) {
    uint8_t enabled = 0;
    const bool ok = r8(f, enabled) && read_pattern_data(f, target.pattern);
    if (ok) {
        std::memcpy(target.pattern.microtiming, target.pattern_banks[0].microtiming,
                    sizeof target.pattern.microtiming);
        target.pattern_banks[0] = target.pattern;
        target.active_pattern = 0;
        target.pending_pattern = 0xff;
        target.sequencer_enabled = enabled != 0;
        target.sequence_tick = 0;
    }
    return ok;
}

static bool read_pattern_banks(FILE* f, Engine& target) {
    uint8_t active = 0;
    bool ok = r8(f, active) && active < NUM_PATTERNS;
    for (int bank = 0; bank < NUM_PATTERNS && ok; ++bank)
        ok = read_pattern_data(f, target.pattern_banks[bank]);
    if (ok) {
        target.active_pattern = active;
        target.pattern = target.pattern_banks[active];
        target.pending_pattern = 0xff;
        target.sequence_tick = 0;
    }
    return ok;
}

static bool read_sequence_directions(FILE* f, uint8_t* parsed) {
    uint8_t version = 0, banks = 0, chains = 0;
    bool ok = r8(f, version) && r8(f, banks) && r8(f, chains) &&
              version == 1 && banks == NUM_PATTERNS && chains == NUM_CHAINS;
    for (int bank = 0; bank < NUM_PATTERNS && ok; ++bank)
        for (int chain = 0; chain < NUM_CHAINS && ok; ++chain) {
            uint8_t raw = 0;
            ok = r8(f, raw) && raw < (uint8_t)SequenceDirection::Count;
            if (ok) parsed[bank * NUM_CHAINS + chain] = raw;
        }
    return ok;
}

static bool read_microtiming(FILE* f, int8_t* parsed) {
    uint8_t version = 0, divisions = 0, banks = 0, chains = 0, steps = 0;
    bool ok = r8(f, version) && r8(f, divisions) && r8(f, banks) && r8(f, chains) && r8(f, steps) &&
              version == 1 && divisions == SEQ_MICROTIMING_DIVISIONS &&
              banks == NUM_PATTERNS && chains == NUM_CHAINS && steps == SEQ_STEPS;
    for (int bank = 0; bank < NUM_PATTERNS && ok; ++bank) {
        for (int ch = 0; ch < NUM_CHAINS && ok; ++ch) for (int st = 0; st < SEQ_STEPS; ++st) {
            uint8_t raw = 0; ok = r8(f, raw);
            const int value = (int)(int8_t)raw;
            if (!ok || value < -SEQ_MICROTIMING_MAX || value > SEQ_MICROTIMING_MAX) { ok = false; break; }
            parsed[(bank * NUM_CHAINS + ch) * SEQ_STEPS + st] = (int8_t)value;
        }
    }
    return ok;
}

static bool read_pattern_chain(FILE* f, Engine& target) {
    uint8_t version = 0, length = 0, loop = 0, capacity = 0;
    bool ok = r8(f, version) && r8(f, length) && r8(f, loop) && r8(f, capacity) &&
              version == 1 && length <= PATTERN_CHAIN_STEPS && loop <= 1 &&
              capacity == PATTERN_CHAIN_STEPS;
    PatternChainStep parsed[PATTERN_CHAIN_STEPS];
    for (int i = 0; i < PATTERN_CHAIN_STEPS && ok; ++i) {
        uint8_t pattern = 0, bars = 0;
        ok = r8(f, pattern) && r8(f, bars) && pattern < NUM_PATTERNS && bars >= 1 && bars <= 16;
        if (ok) parsed[i] = {pattern, bars};
    }
    if (ok) {
        std::memcpy(target.pattern_chain, parsed, sizeof parsed);
        target.pattern_chain_length = length;
        target.pattern_chain_loop = loop != 0;
        target.pattern_chain_position = 0;
        target.pattern_chain_bars_remaining = 0;
        target.pattern_chain_playing = false;
    }
    return ok;
}

static bool read_legacy_strn(FILE* f, Engine& target) {
    bool ok = true;
    for (int c = 0; c < NUM_CHAINS && ok; ++c) for (int s = 0; s < UNITS_PER_CHAIN && ok; ++s) {
        int16_t spray, reverse, pitch; uint8_t hold, window, sync, cluster, div;
        ok = r16(f, spray) && r16(f, reverse) && r16(f, pitch) && r8(f, hold) && r8(f, window) && r8(f, sync) && r8(f, cluster) && r8(f, div);
        Unit* u = target.chains[c].units[s]; if (ok && u && u->kind == UnitKind::Stranular) {
            auto* st = static_cast<StranularUnit*>(u);
            st->params[StranularUnit::P_SPRAY].base = spray < 0 ? 0 : spray;
            st->params[StranularUnit::P_REVERSE].base = reverse < 0 ? 0 : reverse;
            st->params[StranularUnit::P_PITCH_ENV].base = pitch;
            st->params[StranularUnit::P_HOLD].base = hold ? trackr::fx::Q15_ONE : 0;
            st->params[StranularUnit::P_SYNC].base = sync ? trackr::fx::Q15_ONE : 0;
            st->params[StranularUnit::P_CLUSTER].base = cluster <= 1 ? 0 : (q15)(((cluster > 4 ? 4 : cluster) - 1) * 32767 / 3);
            for (int p = StranularUnit::P_SPRAY; p <= StranularUnit::P_CLUSTER; ++p) st->params[p].snap();
            st->window_mode = window < 3 ? window : 0;
            st->sync_division = div < 4 ? div : 0;
            st->set_transport_bpm(target.bpm);
        }
    }
    return ok;
}

// Optional PLAY cut persistence follows the sample slot, not a card alias. The
// file format remains SNC7; older readers skip this chunk and newer readers
// retain WAV cue/default cuts when it is absent.
static bool read_sample_chops(FILE* f, SampleManifest& manifest) {
    uint8_t version = 0, slots = 0, capacity = 0;
    bool ok = r8(f, version) && r8(f, slots) && r8(f, capacity) &&
              version == 1 && slots == SampleManifest::SLOT_COUNT &&
              capacity == trackr::synth::Sample::MAX_CHOPS;
    for (int slot = 0; slot < SampleManifest::SLOT_COUNT && ok; ++slot)
        for (int chop = 0; chop < trackr::synth::Sample::MAX_CHOPS && ok; ++chop)
            ok = ru32(f, manifest.chops[slot][chop]);
    if (ok) manifest.has_chops = true;
    return ok;
}

static bool read_sample_slice_reverse(FILE* f, SampleManifest& manifest) {
    uint8_t version = 0, slots = 0;
    bool ok = r8(f, version) && r8(f, slots) &&
              version == 1 && slots == SampleManifest::SLOT_COUNT;
    for (int slot = 0; slot < SampleManifest::SLOT_COUNT && ok; ++slot)
        ok = ru16(f, manifest.slice_reverse[slot]);
    if (ok) manifest.has_slice_reverse = true;
    return ok;
}

static bool read_workspace(FILE* f, WorkspaceLayout& layout, uint32_t len) {
    if (len != 3u + (uint32_t)(NUM_CHAINS * UNITS_PER_CHAIN * 4)) return false;
    uint8_t version = 0, chains = 0, slots = 0;
    if (!r8(f, version) || !r8(f, chains) || !r8(f, slots) ||
        version != 1 || chains != NUM_CHAINS || slots != UNITS_PER_CHAIN) return false;
    WorkspaceLayout parsed;
    for (int chain = 0; chain < NUM_CHAINS; ++chain)
        for (int slot = 0; slot < UNITS_PER_CHAIN; ++slot) {
            int16_t x = 0, y = 0;
            if (!r16(f, x) || !r16(f, y)) return false;
            parsed.node[chain][slot] = {x, y};
        }
    if (!workspace_layout_valid(parsed)) return false;
    layout = parsed;
    return true;
}

static PlayUnit::EditSourceMetadata play_source_metadata(const SampleManifest& manifest, int slot) {
    PlayUnit::EditSourceMetadata source;
    source.slot = slot;
    source.provenance.origin = (trackr::synth::SampleOrigin)manifest.slots[slot].origin;
    source.provenance.missing = manifest.slots[slot].missing;
    std::memcpy(source.provenance.path, manifest.slots[slot].path, sizeof source.provenance.path);
    for (int i = 0; i < trackr::synth::Sample::MAX_CHOPS; ++i)
        source.chops[i] = manifest.has_chops ? manifest.chops[slot][i] : UINT32_MAX;
    source.reverse = manifest.has_slice_reverse ? manifest.slice_reverse[slot] : 0;
    for (int i = 0; i < trackr::synth::Sample::MAX_CHOPS; ++i)
        source.speed_q8[i] = manifest.has_slice_speed ? manifest.slice_speed_q8[slot][i] : 256;
    for (int i = 0; i < trackr::synth::Sample::MAX_CHOPS; ++i)
        source.pitch_semitones[i] = manifest.has_slice_pitch ? manifest.slice_pitch_semitones[slot][i] : 0;
    source.exact_native_endpoint = manifest.exact_native[slot];
    return source;
}

static bool read_scale(FILE* f, uint32_t len, MusicalScale& parsed) {
    uint8_t version = 0, tonic = 0, mode = 0;
    if (len != 3 || !r8(f, version) || !r8(f, tonic) || !r8(f, mode) || version != 1) return false;
    const MusicalScale scale{tonic, ScaleMode(mode)};
    if (!musical_scale::valid(scale)) return false;
    parsed = scale;
    return true;
}

static bool load_v7(FILE* f, Engine& target, SampleManifest& manifest,
                    WorkspaceLayout& workspace, uint8_t format=7) {
    bool seen_scale = false;
    MusicalScale parsed_scale{};
    bool seen_envr = false, seen_clock = false, seen_play_edit = false, seen_sext = false;
    bool seen_ppos = false, seen_pspd = false, seen_spit = false, seen_phld = false, seen_pnot = false;
    bool seen_relc=false,seen_rlmo=false;
    bool seen_rot=false;
    struct RotConfig {bool present=false;RotUnit::Config config;};
    RotConfig parsed_rot[NUM_CHAINS][UNITS_PER_CHAIN];
    bool seen_rule=false;
    struct RuleConfig {bool present=false;RuleUnit::Config config;};
    RuleConfig parsed_rule[NUM_CHAINS][UNITS_PER_CHAIN];
    bool seen_pent = false;
    struct PentConfig { bool present = false; uint8_t width = 0; };
    PentConfig parsed_pent[NUM_CHAINS][UNITS_PER_CHAIN];
    bool seen_paul = false;
    struct PaulConfig { bool present = false; uint8_t mode = 0, direction = 0; };
    PaulConfig parsed_paul[NUM_CHAINS][UNITS_PER_CHAIN];
    bool seen_scratch = false;
    struct ScratchConfig {
        bool present = false;
        uint8_t mode = 1, map = 2;
        int8_t axis_x = -1, axis_y = -1;
    };
    ScratchConfig parsed_scratch[NUM_CHAINS][UNITS_PER_CHAIN];
    bool seen_arpm = false;
    bool seen_mrnd = false;
    ModSource parsed_random[NUM_CHAINS][UNITS_PER_CHAIN] = {};
    bool seen_grnp = false;
    struct GrainPerform { bool present = false; uint8_t mode = 0, window = 0, count = 4; };
    GrainPerform parsed_gran[NUM_CHAINS][UNITS_PER_CHAIN];
    bool seen_grnx = false;
    struct GrainReading { bool present = false; uint8_t reading = 0, percent = 50; };
    GrainReading parsed_reading[NUM_CHAINS][UNITS_PER_CHAIN];
    ArpUnit::NoteRecall parsed_arp_recall[NUM_CHAINS][UNITS_PER_CHAIN];
    bool parsed_note[NUM_CHAINS][UNITS_PER_CHAIN] = {};
    uint8_t parsed_pitch_mode[NUM_CHAINS][UNITS_PER_CHAIN] = {};
    PatchDocument::Impl::ParamData note_params[NUM_CHAINS][UNITS_PER_CHAIN];
    bool seen_pbay = false, seen_usig = false, seen_glitch = false;
    bool parsed_glitch[NUM_CHAINS][UNITS_PER_CHAIN] = {};
    std::unique_ptr<GlitchUnit::PatternState[]> glitch_patterns;
    LegacyLfo parsed_lfos[LEGACY_SIGIL_COUNT];
    PatchDocument::Impl::SigilData parsed_sigils[NUM_SIGILS];
    uint16_t parsed_disabled[NUM_CHAINS][UNITS_PER_CHAIN] = {};
    bool parsed_hold[NUM_CHAINS][UNITS_PER_CHAIN] = {};
    PatchDocument::Impl::ParamData hold_params[NUM_CHAINS][UNITS_PER_CHAIN];
    bool parsed_position[NUM_CHAINS][UNITS_PER_CHAIN] = {};
    PatchDocument::Impl::ParamData position_params[NUM_CHAINS][UNITS_PER_CHAIN][2];
    PlayEditState parsed_play_edit[NUM_CHAINS][UNITS_PER_CHAIN];
    uint8_t parsed_play_source[NUM_CHAINS][UNITS_PER_CHAIN] = {};
    ClockMod parsed_clock[2];
    bool seen_core=false, seen_sigl=false, seen_rout=false, seen_unit=false, seen_samp=false, seen_trns=false, seen_strn=false;
    bool seen_scne=false, seen_mpad=false, seen_pbnd=false, seen_ptrn=false, seen_pbnk=false, seen_sdir=false, seen_mtim=false, seen_pchn=false, seen_wspc=false, seen_chop=false, seen_srev=false;
    std::unique_ptr<int8_t[]> parsed_timing(new(std::nothrow) int8_t[NUM_PATTERNS * NUM_CHAINS * SEQ_STEPS]());
    uint8_t parsed_directions[NUM_PATTERNS * NUM_CHAINS] = {};
    uint8_t parsed_unit_flags[NUM_CHAINS][UNITS_PER_CHAIN] = {};
    uint8_t parsed_core_extra[NUM_CHAINS][UNITS_PER_CHAIN] = {};
    if (!parsed_timing) return false;
    long file_end=0;
    const long at=ftell(f);
    if(at<0 || fseek(f,0,SEEK_END)!=0)return false;
    file_end=ftell(f);
    if(file_end<at || fseek(f,at,SEEK_SET)!=0)return false;
    while (true) {
        char id[4]; size_t got = fread(id, 1, 4, f);
        if (got == 0 && feof(f) && !ferror(f)) {
            if (ftell(f) != file_end) return false; // premature EOF is not a complete container
            // SEEK_SET may skip beyond a file truncated after the initial size
            // capture (and stdio may still hold its last header in a buffer).
            // Recheck physical EOF, not merely the logical seek position.
            if (fseek(f, 0, SEEK_END) != 0 || ftell(f) != file_end) return false;
            break;
        }
        if (got != 4) return false;
        uint32_t len; if (!ru32(f, len) || len > MAX_CHUNK_SIZE) return false;
        long start = ftell(f); if (start < 0) return false; bool ok = true;
        // Every SNC7..19 chunk container must contain its complete payload.
        // The subtraction also proves start+len fits long before seeking.
        if(start>file_end || uint64_t(len)>uint64_t(file_end-start))return false;
        if(!std::memcmp(id,"RELC",4) || !std::memcmp(id,"RLMO",4)) {
            // Recognize old wire chunks; never skip typed/orphan data as unknown.
            bool& seen=!std::memcmp(id,"RELC",4)?seen_relc:seen_rlmo;
            if(format<19 || seen || len!=4)return false;
            seen=true;
            uint8_t version=0,flags=0;uint16_t count=0;
            ok=r8(f,version)&&r8(f,flags)&&ru16(f,count)&&version==1&&!flags&&!count;
        }
        else if(!std::memcmp(id,"ROTS",4)) {
            if(format<22 || seen_rot || len<2)return false;
            seen_rot=true;
            uint8_t version=0,count=0;ok=r8(f,version)&&r8(f,count)&&(version==1||version==2)&&count>0&&count<=24&&len==2u+(version==2?10u:9u)*count;
            for(int i=0;i<count && ok;++i) {
                uint8_t ch=0,s=0,mode=0,law=0;RotUnit::Config cfg;
                ok=r8(f,ch)&&r8(f,s)&&ru32(f,cfg.seed)&&ru16(f,cfg.passes)&&r8(f,mode)&&(version==1||r8(f,law))&&ch<4&&s<6;
                cfg.mode=RotUnit::Mode(mode);cfg.law=law;
                if(!ok || !RotUnit::valid_config(cfg) || parsed_rot[ch][s].present)return false;
                parsed_rot[ch][s]={true,cfg};
            }
        }
        else if(!std::memcmp(id,"RULE",4)) {
            if(format<21 || seen_rule || len<2)return false;
            seen_rule=true;
            uint8_t version=0,count=0;ok=r8(f,version)&&r8(f,count)&&version==1&&count>0&&count<=24&&len==2u+10u*count;
            for(int i=0;i<count && ok;++i) {
                uint8_t ch=0,s=0;RuleUnit::Config cfg;
                ok=r8(f,ch)&&r8(f,s)&&ru32(f,cfg.seed)&&ru16(f,cfg.seed_row)&&r8(f,cfg.phrase)&&r8(f,cfg.source_root)&&ch<4&&s<6&&RuleUnit::valid_config(cfg);
                if(!ok || parsed_rule[ch][s].present)return false;
                parsed_rule[ch][s]={true,cfg};
            }
        }
        else if (!std::memcmp(id,"PENT",4)) {
            if (format < 20 || seen_pent || len < 2) return false;
            seen_pent = true;
            uint8_t version = 0, count = 0;
            ok = r8(f, version) && r8(f, count) && version == 1 && count > 0 &&
                count <= NUM_CHAINS * UNITS_PER_CHAIN && len == 2u + 3u * count;
            for (int i = 0; i < count && ok; ++i) {
                uint8_t ch = 0, slot = 0, width = 0;
                ok = r8(f, ch) && r8(f, slot) && r8(f, width) &&
                    ch < NUM_CHAINS && slot < UNITS_PER_CHAIN && width <= 1;
                if (!ok || parsed_pent[ch][slot].present) return false;
                parsed_pent[ch][slot] = {true, width};
            }
        }
        else if (!std::memcmp(id,"PAUL",4)) {
            if (format < 17 || seen_paul || len < 2) return false;
            seen_paul = true;
            uint8_t version=0, count=0;
            ok = r8(f,version) && r8(f,count) && (version==1 || version==2) && count>0 &&
                count<=NUM_CHAINS*UNITS_PER_CHAIN && len==2u+(version==1?3u:4u)*count;
            for (int i=0; i<count && ok; ++i) {
                uint8_t ch=0, slot=0, mode=0, direction=0; // v1 defaults Forward
                ok = r8(f,ch) && r8(f,slot) && r8(f,mode) &&
                    ch<NUM_CHAINS && slot<UNITS_PER_CHAIN && mode<=1;
                if (version==2) ok = ok && r8(f,direction) && direction<=2;
                if (!ok || parsed_paul[ch][slot].present) return false;
                parsed_paul[ch][slot] = {true, mode, direction};
            }
        }
        else if (!std::memcmp(id,"SCRT",4)) {
            if (format < 16 || seen_scratch || len < 2) return false;
            seen_scratch = true;
            uint8_t version=0, count=0;
            ok = r8(f,version) && r8(f,count) && (version==1 || version==2) && count>0 &&
                count<=NUM_CHAINS*UNITS_PER_CHAIN && len==2u+(version==1?4u:6u)*count;
            for (int i=0; i<count && ok; ++i) {
                uint8_t ch=0, slot=0, mode=0, map=0, raw_x=255, raw_y=255;
                ok = r8(f,ch) && r8(f,slot) && r8(f,mode) && r8(f,map) &&
                    ch<NUM_CHAINS && slot<UNITS_PER_CHAIN;
                if (version==2) ok = ok && r8(f,raw_x) && r8(f,raw_y);
                const int x = raw_x==255 ? -1 : int(raw_x);
                const int y = raw_y==255 ? -1 : int(raw_y);
                if (!ok || !scratch_axes_valid(mode,map,x,y) || parsed_scratch[ch][slot].present) return false;
                parsed_scratch[ch][slot] = {true, mode, map, int8_t(x), int8_t(y)};
            }
        }
        else if (!std::memcmp(id,"MRND",4)) {
            if (seen_mrnd || len < 2) return false;
            seen_mrnd = true;
            uint8_t version=0, count=0;
            ok = r8(f,version) && r8(f,count) && version==1 && count>0 &&
                count<=NUM_CHAINS*UNITS_PER_CHAIN && len==2u+3u*count;
            for (int i=0; i<count && ok; ++i) {
                uint8_t ch=0, slot=0, source=0;
                ok = r8(f,ch) && r8(f,slot) && r8(f,source) && ch<NUM_CHAINS && slot<UNITS_PER_CHAIN &&
                    mod_source_sigil(ModSource(source))>=0;
                if (!ok || parsed_random[ch][slot]!=ModSource::None) return false;
                parsed_random[ch][slot]=ModSource(source);
            }
        }
        else if (!std::memcmp(id,"GLCH",4)) {
            if (seen_glitch || len < 2) return false;
            seen_glitch = true;
            uint8_t version=0, count=0;
            ok = r8(f,version) && r8(f,count) && version == 1 && count > 0 &&
                count <= NUM_CHAINS*UNITS_PER_CHAIN && len == 2u + count*74u;
            if (!ok) return false;
            glitch_patterns.reset(new(std::nothrow) GlitchUnit::PatternState[NUM_CHAINS*UNITS_PER_CHAIN]);
            if (!glitch_patterns) return false;
            for (int i=0; i<count && ok; ++i) {
                uint8_t ch=0, slot=0;
                ok = r8(f,ch) && r8(f,slot) && ch < NUM_CHAINS && slot < UNITS_PER_CHAIN;
                if (!ok || parsed_glitch[ch][slot]) return false;
                parsed_glitch[ch][slot] = true;
                auto& state = glitch_patterns[ch*UNITS_PER_CHAIN+slot];
                ok = ru32(f,state.rng) && ru32(f,state.revision);
                for (auto& cell : state.cells) {
                    uint8_t op=0, semitones=0;
                    ok = ok && r8(f,op) && r8(f,cell.source) && r8(f,semitones) && r8(f,cell.repeats);
                    cell.op = (GlitchUnit::Op)op;
                    cell.semitones = semitones < 128 ? int8_t(semitones) : int8_t(int(semitones)-256);
                }
                ok = ok && GlitchUnit::valid_pattern(state);
            }
        }
        else if (!std::memcmp(id,"USIG",4)) {
            if (seen_usig || seen_pbay || len != UNIVERSAL_SIGIL_BYTES) return false;
            seen_usig = true;
            uint8_t version=0,count=0,chains=0,slots=0,params=0;
            ok = r8(f,version) && r8(f,count) && r8(f,chains) && r8(f,slots) && r8(f,params) &&
                version == 1 && count == NUM_SIGILS && chains == NUM_CHAINS &&
                slots == UNITS_PER_CHAIN && params == PARAM_BANK_SIZE;
            for (int i=0; i<NUM_SIGILS && ok; ++i) {
                auto& s = parsed_sigils[i]; auto& cfg = s.config;
                uint8_t flags=0,shape=0,restart=0;
                ok = r8(f,flags) && r8(f,shape) && r8(f,restart) &&
                    ru16(f,cfg.rate_millihz) && ru16(f,cfg.phase_degrees) && r8(f,s.mode) &&
                    r16(f,s.rate) && r16(f,s.depth) && r8(f,s.mod_src) && r16(f,s.mod_depth) &&
                    r8(f,s.sync) && r8(f,s.division) && r8(f,s.step_dest) && !(flags & ~31u);
                cfg.enabled = flags & 1; cfg.legacy = flags & 2; cfg.unipolar = flags & 4; s.drawn = flags & 8;
                cfg.legacy_rate = flags & 16;
                cfg.shape = (SigilShape)shape; cfg.restart = (SigilRestart)restart;
                ok = ok && sigil_config_valid(cfg) && s.mode < (uint8_t)SigilMode::Count &&
                    s.sync < 2 && s.division < 6 && s.step_dest < NUM_CHAINS &&
                    s.mod_src < (uint8_t)ModSource::Count && mod_source_sigil((ModSource)s.mod_src) < i;
                for (auto& value : s.table) ok = ok && r16(f,value);
            }
            for (auto& chain : parsed_disabled) for (auto& disabled : chain)
                ok = ok && ru16(f,disabled) && !(disabled & ~((1u << PARAM_BANK_SIZE)-1u));
        }
        else if (!std::memcmp(id,"PBAY",4)) {
            constexpr uint32_t bytes = 5 + LEGACY_SIGIL_COUNT * 8 + NUM_CHAINS * UNITS_PER_CHAIN * 2;
            if (seen_pbay || seen_usig || len != bytes) return false;
            seen_pbay = true;
            uint8_t version=0, count=0, chains=0, slots=0, params=0;
            ok = r8(f,version) && r8(f,count) && r8(f,chains) && r8(f,slots) && r8(f,params) &&
                version == 1 && count == LEGACY_SIGIL_COUNT && chains == NUM_CHAINS &&
                slots == UNITS_PER_CHAIN && params == PARAM_BANK_SIZE;
            for (auto& lfo : parsed_lfos) {
                uint8_t flags=0, shape=0, restart=0;
                ok = ok && r8(f,flags) && r8(f,shape) && r8(f,restart) && r8(f,lfo.division) &&
                    ru16(f,lfo.rate_millihz) && ru16(f,lfo.phase_degrees) && !(flags & ~7u);
                lfo.active = (flags & 1) != 0; lfo.sync = (flags & 2) != 0; lfo.unipolar = (flags & 4) != 0;
                lfo.shape = shape; lfo.restart = restart;
                ok = ok && legacy_lfo_valid(lfo);
            }
            for (auto& chain : parsed_disabled) for (auto& disabled : chain)
                ok = ok && ru16(f,disabled) && !(disabled & ~((1u << PARAM_BANK_SIZE) - 1u));
        }
        else if (!std::memcmp(id,"PPOS",4)) {
            if (seen_ppos || len < 2) return false;
            seen_ppos = true;
            uint8_t version = 0, count = 0;
            ok = r8(f, version) && r8(f, count) && version == 1 && count && count <= NUM_CHAINS * UNITS_PER_CHAIN && len == 2u + count * 12u;
            for (int i = 0; i < count && ok; ++i) {
                uint8_t ch = 0, slot = 0;
                ok = r8(f, ch) && r8(f, slot) && ch < NUM_CHAINS && slot < UNITS_PER_CHAIN;
                if (ok) {
                    if (parsed_position[ch][slot]) return false;
                    parsed_position[ch][slot] = true;
                    for (auto& v : position_params[ch][slot])
                        ok = ok && r16(f, v.base) && r8(f, v.src) && r16(f, v.depth) && v.base >= 0 && v.src < (uint8_t)ModSource::Count;
                }
            }
        }
        else if (!std::memcmp(id,"PHLD",4)) {
            if (seen_phld || len < 2) return false;
            seen_phld = true;
            uint8_t version = 0, count = 0;
            ok = r8(f, version) && r8(f, count) && version == 1 && count && count <= NUM_CHAINS * UNITS_PER_CHAIN && len == 2u + count * 7u;
            for (int i = 0; i < count && ok; ++i) {
                uint8_t ch = 0, slot = 0;
                ok = r8(f, ch) && r8(f, slot) && ch < NUM_CHAINS && slot < UNITS_PER_CHAIN;
                if (ok) {
                    if (parsed_hold[ch][slot]) return false;
                    parsed_hold[ch][slot] = true;
                    auto& v = hold_params[ch][slot];
                    ok = r16(f, v.base) && r8(f, v.src) && r16(f, v.depth) && v.base >= 0 && v.src < (uint8_t)ModSource::Count;
                }
            }
        }
        else if (!std::memcmp(id,"PNOT",4)) {
            if (seen_pnot || len < 2) return false;
            seen_pnot = true;
            uint8_t version = 0, count = 0;
            ok = r8(f, version) && r8(f, count) && version == 1 && count && count <= NUM_CHAINS * UNITS_PER_CHAIN && len == 2u + count * 8u;
            for (int i = 0; i < count && ok; ++i) {
                uint8_t ch = 0, slot = 0;
                ok = r8(f, ch) && r8(f, slot) && ch < NUM_CHAINS && slot < UNITS_PER_CHAIN;
                if (ok) {
                    if (parsed_note[ch][slot]) return false;
                    parsed_note[ch][slot] = true;
                    auto& v = note_params[ch][slot];
                    ok = r8(f, parsed_pitch_mode[ch][slot]) && parsed_pitch_mode[ch][slot] <= 2 &&
                        r16(f, v.base) && r8(f, v.src) && r16(f, v.depth) && v.base >= 0 && v.src < uint8_t(ModSource::Count);
                }
            }
        }
        else if (!std::memcmp(id,"ARPM",4)) {
            if (seen_arpm || len < 2) return false;
            seen_arpm = true;
            uint8_t version = 0, count = 0;
            ok = r8(f, version) && r8(f, count) && version == 1 && count &&
                count <= NUM_CHAINS * UNITS_PER_CHAIN && len == 2u + count * 11u;
            for (int i = 0; i < count && ok; ++i) {
                uint8_t ch = 0, slot = 0;
                ok = r8(f, ch) && r8(f, slot) && ch < NUM_CHAINS && slot < UNITS_PER_CHAIN;
                if (!ok) return false;
                auto& recall = parsed_arp_recall[ch][slot];
                if (recall.mask) return false;
                ok = r8(f, recall.mask) && recall.mask && !(recall.mask & ~15u);
                for (int n = 0; n < 4 && ok; ++n) {
                    ok = r16(f, recall.values[n]);
                    if (recall.mask & (1u << n))
                        ok = ok && ArpUnit::note_semitones(recall.values[n]) != ArpUnit::NOTE_OFF;
                    else ok = ok && recall.values[n] == 0;
                }
            }
        }
        else if (!std::memcmp(id,"PSPD",4)) {
            if (seen_pspd || len != 3u + SampleManifest::SLOT_COUNT * trackr::synth::Sample::MAX_CHOPS * 2u) return false;
            seen_pspd = true;
            uint8_t version = 0, count = 0, cuts = 0;
            ok = r8(f, version) && r8(f, count) && r8(f, cuts) && version == 1 && count == SampleManifest::SLOT_COUNT && cuts == trackr::synth::Sample::MAX_CHOPS;
            for (auto& speeds : manifest.slice_speed_q8) for (auto& speed : speeds)
                ok = ok && ru16(f, speed) && trackr::synth::Sample::cut_speed(speed) == speed;
            manifest.has_slice_speed = ok;
        }
        else if (!std::memcmp(id,"SPIT",4)) {
            if (seen_spit || len != 3u + SampleManifest::SLOT_COUNT * trackr::synth::Sample::MAX_CHOPS) return false;
            seen_spit = true;
            uint8_t version = 0, slots = 0, cuts = 0;
            ok = r8(f, version) && r8(f, slots) && r8(f, cuts) && version == 1 &&
                slots == SampleManifest::SLOT_COUNT && cuts == trackr::synth::Sample::MAX_CHOPS;
            for (auto& pitches : manifest.slice_pitch_semitones) for (auto& pitch : pitches) {
                uint8_t raw = 0;
                ok = ok && r8(f, raw);
                const int value = raw < 128 ? raw : int(raw) - 256;
                ok = ok && play_part_pitch::valid(value);
                if (ok) pitch = (int8_t)value;
            }
            manifest.has_slice_pitch = ok;
        }
        else if (!std::memcmp(id,"SEXT",4)) {
            if (seen_sext || len != 2u + SampleManifest::SLOT_COUNT) return false;
            seen_sext = true;
            uint8_t version = 0, count = 0;
            ok = r8(f, version) && r8(f, count) && version == 1 && count == SampleManifest::SLOT_COUNT;
            for (int slot = 0; slot < SampleManifest::SLOT_COUNT && ok; ++slot) {
                uint8_t flags = 0; ok = r8(f, flags) && flags <= 1;
                if (ok) manifest.exact_native[slot] = (flags & 1u) != 0;
            }
        }
        else if (!std::memcmp(id,"PEDT",4)) {
            if (seen_play_edit || len < 2) return false;
            seen_play_edit = true;
            uint8_t version = 0, count = 0;
            ok = r8(f, version) && r8(f, count) && version == 1 && count > 0 &&
                count <= NUM_CHAINS * UNITS_PER_CHAIN && len == 2u + (uint32_t)count * 39u;
            for (int i = 0; i < count && ok; ++i) {
                uint8_t ch = 0, slot = 0, source = 0;
                PlayEditState e; e.enabled = true;
                ok = r8(f, ch) && r8(f, slot) && r8(f, source) &&
                    ch < NUM_CHAINS && slot < UNITS_PER_CHAIN && source < SampleManifest::SLOT_COUNT &&
                    ru32(f, e.source_frames) && ru32(f, e.start) && ru32(f, e.end) &&
                    ru32(f, e.fade_in) && ru32(f, e.fade_out) && ru32(f, e.loop_start) &&
                    ru32(f, e.loop_end) && ru32(f, e.crossfade) &&
                    r16(f, e.anchor_start) && r16(f, e.anchor_length) && play_edit_valid(e);
                if (ok) {
                    if (parsed_play_edit[ch][slot].enabled) return false;
                    parsed_play_edit[ch][slot] = e;
                    parsed_play_source[ch][slot] = source;
                }
            }
        }
        else if (!std::memcmp(id,"SCAL",4)) {
            if (seen_scale) return false;
            seen_scale = true;
            ok = read_scale(f, len, parsed_scale);
        }
        else if (!std::memcmp(id,"ENVR",4)) {
            if (seen_envr || len != 3) return false;
            seen_envr = true;
            uint8_t version = 0, chain = 0, response = 0;
            ok = r8(f, version) && r8(f, chain) && r8(f, response) && version == 1 &&
                 chain < NUM_CHAINS && response < (uint8_t)EnvResponse::Count;
            if (ok) {
                target.set_env_follow_source(chain);
                target.set_env_follow_response((EnvResponse)response);
            }
        }
        else if (!std::memcmp(id,"CLCK",4)) {
            if (seen_clock || len != 8) return false;
            seen_clock = true;
            uint8_t version = 0, count = 0;
            ok = r8(f, version) && r8(f, count) && version == 1 && count == 2;
            for (int i = 0; i < 2 && ok; ++i) {
                uint8_t source = 0; int16_t depth = 0;
                ok = r8(f, source) && r16(f, depth) && (source == 0 || mod_source_sigil((ModSource)source) >= 0) &&
                    depth >= (i == 0 ? -280 : -25) && depth <= (i == 0 ? 280 : 25);
                if (ok) parsed_clock[i] = {(ModSource)source, depth};
            }
        }
        else if (!std::memcmp(id,"GRNX",4)) {
            if (format < 18 || seen_grnx || len < 2) return false;
            seen_grnx = true;
            uint8_t version=0, count=0;
            ok = r8(f,version) && r8(f,count) && version==1 && count>0 &&
                count<=NUM_CHAINS*UNITS_PER_CHAIN && len==2u+4u*count;
            for (int i=0; i<count && ok; ++i) {
                uint8_t ch=0, slot=0, reading=0, percent=50;
                ok = r8(f,ch) && r8(f,slot) && r8(f,reading) && r8(f,percent) &&
                    ch<NUM_CHAINS && slot<UNITS_PER_CHAIN && reading<=3 && percent<=100 &&
                    (reading || percent!=50);
                if (!ok || parsed_reading[ch][slot].present) return false;
                parsed_reading[ch][slot] = {true, reading, percent};
            }
        }
        else if (!std::memcmp(id,"GRNP",4)) {
            if (seen_grnp || len < 2) return false;
            seen_grnp = true;
            uint8_t version = 0, count = 0;
            ok = r8(f, version) && r8(f, count) && version == 1 &&
                count <= NUM_CHAINS * UNITS_PER_CHAIN && len == 2u + 5u * count;
            for (int record = 0; record < count && ok; ++record) {
                uint8_t chain = 0, slot = 0, mode = 0, window = 0, burst = 0;
                ok = r8(f, chain) && r8(f, slot) && r8(f, mode) && r8(f, window) && r8(f, burst) &&
                    chain < NUM_CHAINS && slot < UNITS_PER_CHAIN && mode <= 2 && window <= 1 && burst >= 1 && burst <= 16;
                if (!ok || parsed_gran[chain][slot].present) return false;
                parsed_gran[chain][slot] = {true, mode, window, burst};
            }
        }
        else if (!std::memcmp(id,"CORE",4)) { if (seen_core || len != NUM_CHAINS * (5 + UNITS_PER_CHAIN * (3 + PARAM_BANK_SIZE * 5))) return false; seen_core=true; ok=read_core(f,target,PARAM_BANK_SIZE,parsed_core_extra,format); }
        else if (!std::memcmp(id,"SIGL",4)) { if (seen_sigl || len != LEGACY_SIGIL_COUNT * (1+2*SIGIL_POINTS+4)) return false; seen_sigl=true; ok=read_sigils(f,target); }
        else if (!std::memcmp(id,"ROUT",4)) {
            if (seen_rout || len != 2 + LEGACY_SIGIL_COUNT * 6) return false;
            seen_rout=true; uint16_t bpm; ok=ru16(f,bpm) && bpm>=20 && bpm<=300; if(ok) target.set_bpm((int)bpm);
            for(int i=0;i<LEGACY_SIGIL_COUNT&&ok;++i){uint8_t src;int16_t depth;ok=r8(f,src)&&r16(f,depth)&&src<(uint8_t)ModSource::Count&&mod_source_sigil((ModSource)src)<i;if(ok)target.set_sigil_mod(i,(ModSource)src,depth);}
            for(int i=0;i<LEGACY_SIGIL_COUNT&&ok;++i){uint8_t sync,div,dest;ok=r8(f,sync)&&r8(f,div)&&r8(f,dest)&&sync<2&&div<6&&dest<NUM_CHAINS;if(ok){target.set_sigil_div(i,div);target.set_step_dest(i,dest);target.set_sigil_sync(i,sync!=0);}}
        } else if (!std::memcmp(id,"UNIT",4)) {
            if (seen_unit || len != NUM_CHAINS * UNITS_PER_CHAIN) return false;
            seen_unit=true;
            for(int c=0;c<NUM_CHAINS&&ok;++c)for(int s=0;s<UNITS_PER_CHAIN&&ok;++s) {
                uint8_t x; ok=r8(f,x);
                if(ok) { parsed_unit_flags[c][s]=x; apply_unit_flags(target.chains[c].units[s],x); }
            }
        }
        else if (!std::memcmp(id,"SAMP",4) ) { if(seen_samp)return false; seen_samp=true; manifest.has_sample_manifest=true; for(int slot=0;slot<SampleManifest::SLOT_COUNT&&ok;++slot){uint8_t origin,missing;uint16_t n;ok=r8(f,origin)&&r8(f,missing)&&ru16(f,n);if(!ok||n>=sizeof(manifest.slots[slot].path)){ok=false;break;}auto&e=manifest.slots[slot];if(n&&fread(e.path,1,n,f)!=n){ok=false;break;}e.path[n]=0;e.origin=origin<=(uint8_t)trackr::synth::SampleOrigin::Recording?origin:(uint8_t)trackr::synth::SampleOrigin::Runtime;e.missing=missing!=0;} }
        else if (!std::memcmp(id,"CHOP",4) ) { if(seen_chop)return false; seen_chop=true; ok=read_sample_chops(f,manifest); }
        else if (!std::memcmp(id,"SREV",4) ) { if(seen_srev)return false; seen_srev=true; ok=read_sample_slice_reverse(f,manifest); }
        else if (!std::memcmp(id,"TRNS",4) ) { if(seen_trns)return false; seen_trns=true;uint8_t play,beats,q,swing=50,pq=(uint8_t)LaunchQuantize::Bar;ok=r8(f,play)&&r8(f,beats)&&r8(f,q);if(ok&&len>=4)ok=r8(f,swing);if(ok&&len>=5)ok=r8(f,pq);if(ok){target.transport_playing=play!=0;target.beats_per_bar=beats>=2&&beats<=8?beats:4;target.launch_quantize=q<=(uint8_t)LaunchQuantize::Bar?(LaunchQuantize)q:LaunchQuantize::Immediate;target.pattern_launch_quantize=pq<=(uint8_t)LaunchQuantize::Bar?(LaunchQuantize)pq:LaunchQuantize::Bar;target.set_swing(swing);} }
        else if (!std::memcmp(id,"STRN",4) ) { if(seen_strn)return false; seen_strn=true;ok=read_strn_modes(f,target); }
        else if (!std::memcmp(id,"PTRN",4) ) { if(seen_ptrn)return false; seen_ptrn=true;if(!seen_pbnk)ok=read_pattern(f,target); }
        else if (!std::memcmp(id,"PBNK",4) ) { if(seen_pbnk)return false; seen_pbnk=true;ok=read_pattern_banks(f,target); }
        else if (!std::memcmp(id,"SDIR",4) ) { if(seen_sdir)return false; seen_sdir=true;ok=read_sequence_directions(f,parsed_directions); }
        else if (!std::memcmp(id,"MTIM",4) ) { if(seen_mtim)return false; seen_mtim=true;ok=read_microtiming(f,parsed_timing.get()); }
        else if (!std::memcmp(id,"PCHN",4) ) { if(seen_pchn)return false; seen_pchn=true;ok=read_pattern_chain(f,target); }
        else if (!std::memcmp(id,"SCNE",4)) {
            if (seen_scne || len != NUM_CHAINS * (2u + 2u * (5u + UNITS_PER_CHAIN * (1u + MAX_PARAMS * 2u)))) return false;
            seen_scne = true;
            for (int c=0; c<NUM_CHAINS && ok; ++c) {
                ok = r16(f,target.scene_morph[c]);
                for (int n=0; n<2 && ok; ++n) {
                    auto& scene = target.scenes[c][n]; uint8_t valid=0;
                    ok = r8(f,valid) && r16(f,scene.volume) && r16(f,scene.pan);
                    scene.valid = valid != 0;
                    for (int s=0; s<UNITS_PER_CHAIN && ok; ++s) {
                        uint8_t kind=0; ok = r8(f,kind);
                        if(ok && kind==uint8_t(UnitKind::Rule) && format<21)return false;
                        if(ok && kind==uint8_t(UnitKind::Rot) && format<22)return false;
                        if (ok && kind == uint8_t(UnitKind::Pent) && format < 20) return false;
                        if (ok && kind == uint8_t(UnitKind::Paul) && format < 17) return false;
                        if(ok && kind==uint8_t(UnitKind::Relic))return false;
                        if(ok && format>=19 && kind>=uint8_t(UnitKind::Count))return false;
                        scene.kind[s] = kind < uint8_t(UnitKind::Count) ? UnitKind(kind) : UnitKind::Empty;
                        for (int p=0; p<MAX_PARAMS && ok; ++p) ok = r16(f,scene.base[s][p]);
                    }
                }
            }
        }
        else if (!std::memcmp(id,"MPAD",4) ) { if(seen_mpad)return false; seen_mpad=true;for(int p=0;p<8&&ok;++p){MacroPad&pad=target.macro_pads[p];ok=fread(pad.name,1,sizeof pad.name,f)==sizeof pad.name&&r8(f,pad.route_count);pad.name[sizeof pad.name-1]=0;if(pad.route_count>MacroPad::MAX_ROUTES)pad.route_count=MacroPad::MAX_ROUTES;for(int r=0;r<MacroPad::MAX_ROUTES&&ok;++r){MacroPadRoute&x=pad.routes[r];int16_t lo=0,hi=0;ok=r8(f,x.chain)&&r8(f,x.slot)&&r8(f,x.param)&&r16(f,lo)&&r16(f,hi);if(ok){x.minimum=lo;x.maximum=hi;}}} }
        else if (!std::memcmp(id,"WSPC",4) ) {
            if(seen_wspc)return false;
            seen_wspc=true; ok=read_workspace(f,workspace,len);
        }
        else if (!std::memcmp(id,"PBND",4) ) {
            if(seen_pbnd)return false;
            seen_pbnd=true; uint8_t version=0,count=0; ok=r8(f,version)&&r8(f,count)&&version==1&&count==NUM_PERFORMANCE_BINDINGS;
            PerformanceBinding parsed[NUM_PERFORMANCE_BINDINGS];
            for(int i=0;i<NUM_PERFORMANCE_BINDINGS&&ok;++i){uint8_t action,target_value,mode;ok=r8(f,action)&&r8(f,target_value)&&r8(f,mode);if(ok){
                if(action>=(uint8_t)PerformanceAction::Count||mode>=(uint8_t)PerformanceMode::Count){ok=false;break;}
                PerformanceAction a=(PerformanceAction)action; bool target_ok=true;
                if(a==PerformanceAction::ChainTrigger||a==PerformanceAction::ChainMute)target_ok=target_value<NUM_CHAINS;
                else if(a==PerformanceAction::PatternLaunch)target_ok=target_value<NUM_PATTERNS;
                else if(a==PerformanceAction::MacroPad)target_ok=target_value<8;
                else if(a==PerformanceAction::GlobalFx)target_ok=target_value<(uint8_t)GlobalFxTarget::Count;
                else if(a==PerformanceAction::Retrigger)target_ok=target_value<4;
                if(!target_ok){ok=false;break;} parsed[i]={a,target_value,(PerformanceMode)mode};
            }}
            if(ok)std::memcpy(target.performance_bindings,parsed,sizeof parsed);
        }
        long used = ftell(f) - start; if (!ok || used < 0 || (uint32_t)used > len) return false;
        if (fseek(f, start + (long)len, SEEK_SET) != 0) return false;
    }
    if (seen_sdir) {
        for (int bank = 0; bank < NUM_PATTERNS; ++bank)
            for (int chain = 0; chain < NUM_CHAINS; ++chain)
                target.pattern_banks[bank].direction[chain] =
                    (SequenceDirection)parsed_directions[bank * NUM_CHAINS + chain];
        if (target.active_pattern < NUM_PATTERNS)
            std::memcpy(target.pattern.direction, target.pattern_banks[target.active_pattern].direction,
                        sizeof target.pattern.direction);
    }
    if (seen_mtim) {
        for (int bank = 0; bank < NUM_PATTERNS; ++bank)
            std::memcpy(target.pattern_banks[bank].microtiming,
                        parsed_timing.get() + bank * NUM_CHAINS * SEQ_STEPS,
                        sizeof target.pattern_banks[bank].microtiming);
        if (target.active_pattern < NUM_PATTERNS)
            std::memcpy(target.pattern.microtiming,
                        parsed_timing.get() + target.active_pattern * NUM_CHAINS * SEQ_STEPS,
                        sizeof target.pattern.microtiming);
    }
    if (seen_pspd) {
        if (!manifest.has_chops) return false;
        for (int slot = 0; slot < SampleManifest::SLOT_COUNT; ++slot)
            for (int cut = 0; cut < trackr::synth::Sample::MAX_CHOPS; ++cut)
                if (manifest.chops[slot][cut] == UINT32_MAX && manifest.slice_speed_q8[slot][cut] != 256) return false;
    }
    if (seen_spit) {
        if (!manifest.has_chops) return false;
        for (int slot = 0; slot < SampleManifest::SLOT_COUNT; ++slot)
            for (int cut = 0; cut < trackr::synth::Sample::MAX_CHOPS; ++cut)
                if (manifest.chops[slot][cut] == UINT32_MAX && manifest.slice_pitch_semitones[slot][cut]) return false;
    }
    const bool required = seen_core && seen_sigl && seen_rout && seen_unit && seen_samp && seen_trns && seen_strn;
    if (required && seen_glitch) for (int c=0; c<NUM_CHAINS; ++c) for (int s=0; s<UNITS_PER_CHAIN; ++s) {
        if (!parsed_glitch[c][s]) continue;
        Unit* unit = target.chains[c].units[s];
        if (!unit || unit->kind != UnitKind::Glitch || parsed_unit_flags[c][s] || parsed_core_extra[c][s] ||
            !static_cast<GlitchUnit*>(unit)->restore_pattern(glitch_patterns[c*UNITS_PER_CHAIN+s])) return false;
    }
    // CORE stages all 12 entries even in legacy-first units. Commit Delay/REV
    // allocation only after UNIT is known, independent of chunk order.
    if (required) for (int c=0;c<NUM_CHAINS;++c) for (int s=0;s<UNITS_PER_CHAIN;++s) {
        Unit* unit = target.chains[c].units[s];
        const PlayEditState& edit = parsed_play_edit[c][s];
        const auto& performance = parsed_gran[c][s];
        const auto& reading_config = parsed_reading[c][s];
        if (reading_config.present && (!unit || unit->kind != UnitKind::Grain ||
            (parsed_unit_flags[c][s] & 24u) != 24u)) return false;
        if (performance.present && (!unit || unit->kind != UnitKind::Grain ||
            !(parsed_unit_flags[c][s] & 8u))) return false;
        if (edit.enabled && (!unit || unit->kind != UnitKind::SamplePlayer ||
            !(parsed_unit_flags[c][s] & 2u) || parsed_core_extra[c][s] != parsed_play_source[c][s])) return false;
        if ((parsed_position[c][s] || parsed_hold[c][s] || parsed_note[c][s]) && (!unit || unit->kind != UnitKind::SamplePlayer || !(parsed_unit_flags[c][s] & 2u))) return false;
        const auto& rot_config=parsed_rot[c][s];
        if(rot_config.present && (!unit || unit->kind!=UnitKind::Rot))return false;
        const auto& rule_config=parsed_rule[c][s];
        if(rule_config.present && (!unit || unit->kind!=UnitKind::Rule))return false;
        const auto& pent_config = parsed_pent[c][s];
        if (pent_config.present && (!unit || unit->kind != UnitKind::Pent)) return false;
        const auto& paul_config = parsed_paul[c][s];
        if (paul_config.present && (!unit || unit->kind != UnitKind::Paul)) return false;
        const auto& scratch_config = parsed_scratch[c][s];
        if (scratch_config.present && (!unit || unit->kind != UnitKind::Scratch)) return false;
        if (!unit) continue;
        const uint8_t flags = parsed_unit_flags[c][s];
        if(unit->kind==UnitKind::Rot) {
            if(format<22 || !rot_config.present || flags || parsed_core_extra[c][s]>=64)return false;
            if(!static_cast<RotUnit*>(unit)->set_config(rot_config.config))return false;
            continue;
        }
        if(unit->kind==UnitKind::Rule) {
            if(format<21 || !rule_config.present || flags || parsed_core_extra[c][s]>=64)return false;
            if(!static_cast<RuleUnit*>(unit)->set_config(rule_config.config))return false;
            continue;
        }
        if (unit->kind == UnitKind::Pent) {
            if (format < 20 || !pent_config.present || flags || parsed_core_extra[c][s]) return false;
            if (!static_cast<PentUnit*>(unit)->set_width(PentUnit::Width(pent_config.width))) return false;
            continue;
        }
        if (unit->kind == UnitKind::Paul) {
            if (format < 17 || !paul_config.present || flags ||
                parsed_core_extra[c][s] >= SampleManifest::SLOT_COUNT) return false;
            auto* paul = static_cast<PaulUnit*>(unit);
            paul->set_mode(PaulUnit::Mode(paul_config.mode));
            paul->set_direction(PaulUnit::Direction(paul_config.direction));
            continue;
        }
        if (unit->kind == UnitKind::Scratch) {
            if (format < 16 || !scratch_config.present || flags ||
                parsed_core_extra[c][s] >= SampleManifest::SLOT_COUNT) return false;
            auto* scratch = static_cast<ScratchUnit*>(unit);
            scratch->set_mode(ScratchUnit::Mode(scratch_config.mode));
            scratch->set_map(ScratchUnit::Map(scratch_config.map));
            // The complete pair has already validated in the parser. Assign it
            // together on this fresh staged unit: calling set_axis one at a time
            // would incorrectly reject swaps against the other preset default.
            scratch->axis_x = scratch_config.axis_x;
            scratch->axis_y = scratch_config.axis_y;
            continue;
        }
        if (unit->kind == UnitKind::Inst) {
            if (format<16 || flags || parsed_core_extra[c][s]) return false;
            continue;
        }
        if (unit->kind == UnitKind::Vca) {
            if ((flags & ~3u) || flags==2u || ((flags & 2u) && format<15)) return false;
            canonicalize_vca(target,c,s,flags);
            continue;
        }
        if (unit->kind == UnitKind::Fold) {
            if ((flags & ~1u) || ((flags & 1u) && format<13)) return false;
            canonicalize_fold(target,c,s,(flags & 1u)!=0);
            continue;
        }
        if (unit->kind == UnitKind::Ring) {
            if ((flags & ~1u) || ((flags & 1u) && format<12)) return false;
            canonicalize_ring(target,c,s,(flags & 1u)!=0);
            continue;
        }
        if (unit->kind == UnitKind::Strain) {
            if ((flags & ~1u) || ((flags & 1u) && format<11)) return false;
            canonicalize_strain(target,c,s,(flags & 1u)!=0);
            continue;
        }
        if (unit->kind == UnitKind::Bitcrush) {
            if ((flags & ~1u) || ((flags & 1u) && format<10)) return false;
            canonicalize_bit(target,c,s,(flags & 1u)!=0);
            continue;
        }
        if (unit->kind == UnitKind::Motor) {
            if (format>=8 && (flags & ~1u)) return false;
            canonicalize_motor(target,c,s,format>=8 && (flags & 1u));
            continue;
        }
        if (unit->kind == UnitKind::Grain) {
            if ((flags & ~31u) || ((flags & 16u) && (format < 18 || !(flags & 8u))) ||
                bool(flags & 16u) != reading_config.present) return false;
            apply_unit_flags(unit, flags); // UNIT may precede CORE; bit3 may be absent
            canonicalize_grain_scan(target, c, s);
            if (reading_config.present) {
                auto* gran = static_cast<GrainUnit*>(unit);
                gran->reading = GrainUnit::Reading(reading_config.reading);
                gran->reverse_percent = reading_config.percent;
            }
            if (performance.present) {
                auto* gran = static_cast<GrainUnit*>(unit);
                gran->set_launch_mode((GrainUnit::LaunchMode)performance.mode);
                gran->set_window((GrainUnit::Window)performance.window);
                gran->set_burst_count(performance.count);
            }
            continue;
        }
        if (unit->kind == UnitKind::Fm) {
            apply_unit_flags(unit, flags);
            canonicalize_fm(target, c, s, (flags & 0x80u) != 0);
            continue;
        }
        if (unit->kind == UnitKind::Osc) {
            if (flags & ~7u) return false;
            apply_unit_flags(unit, flags);
            canonicalize_osc(target, c, s, (flags & 2u) != 0);
            continue;
        }
        if (unit->kind == UnitKind::SamplePlayer) {
            if (flags & ~3u) return false;
            apply_unit_flags(unit, flags); // UNIT may precede CORE
            finish_play_mode(unit, (flags & 2u) != 0);
            canonicalize_play_position(target, c, s, parsed_position[c][s], parsed_hold[c][s], parsed_note[c][s]);
            if (parsed_note[c][s]) {
                static_cast<PlayUnit*>(unit)->set_pitch_mode(PlayUnit::PitchMode(parsed_pitch_mode[c][s]));
                const auto& v = note_params[c][s];
                apply_param(unit, PlayUnit::P_NOTE, v.base, v.src, v.depth);
            }
            if (parsed_position[c][s]) for (int p = 0; p < 2; ++p) {
                const auto& v = position_params[c][s][p];
                apply_param(unit, PlayUnit::P_POSITION + p, v.base, v.src, v.depth);
            }
            if (parsed_hold[c][s]) {
                const auto& v = hold_params[c][s];
                apply_param(unit, PlayUnit::P_HOLD, v.base, v.src, v.depth);
            }
            if (edit.enabled) {
                auto* play = static_cast<PlayUnit*>(unit);
                if (!play->restore_edit_state(edit)) return false;
                manifest.exact_native[play->sample_slot] = true;
            }
            continue;
        }
        if (unit->kind == UnitKind::Noise) {
            if ((flags & ~0x83u) || ((flags & 1u) && !(flags & 0x80u)) ||
                ((flags & 2u) && (format<9 || (flags & 0x81u)!=0x81u))) return false;
            apply_unit_flags(unit, flags); // migrations independent of chunk order
            canonicalize_noise(target, c, s, (flags & 1u) != 0, (flags & 2u) != 0);
            continue;
        }
        if (unit->kind == UnitKind::MicIn) {
            if (flags & ~1u) return false;
            canonicalize_mic(target, c, s, (flags & 1u) != 0);
            continue;
        }
        if (unit->kind == UnitKind::Tilt) {
            if (flags>2u || (flags==2u && format<14)) return false;
            auto* tilt = static_cast<TiltUnit*>(unit);
            tilt->set_revision(TiltUnit::Revision(flags));
            tilt_edit::clear_inactive(target,c,s,tilt->num_params);
            continue;
        }
        if (unit->kind == UnitKind::Reverb) {
            if ((flags & ~3u) || ((flags & 2u) && !(flags & 1u))) return false;
            if (!static_cast<ReverbUnit*>(unit)->set_modern((flags & 1u) != 0)) return false;
            continue;
        }
        if (unit->kind != UnitKind::Delay) continue;
        if (flags & ~1u) return false; // future sound modes must never silently downgrade
        auto* delay = static_cast<DelayUnit*>(unit);
        if (!delay->set_modern((flags & 1u) != 0)) return false;
        if (!delay->modern()) for (int p=6;p<PARAM_BANK_SIZE;++p) {
            auto& param = delay->params[p];
            param.base=param.mod_depth=0; param.src=ModSource::None; param.snap();
        }
        delay->set_clock_bpm_q16(target.effective_bpm_q16());
    }
    if (required && seen_arpm) {
        // Apply only after CORE, independent of chunk order. The outer loader
        // stages an Engine, so a malformed recall cannot partially load a patch.
        for (int c = 0; c < NUM_CHAINS; ++c) for (int s = 0; s < UNITS_PER_CHAIN; ++s) {
            const auto& recall = parsed_arp_recall[c][s];
            if (!recall.mask) continue;
            Unit* unit = target.chains[c].units[s];
            if (!unit || unit->kind != UnitKind::Arp) return false;
            for (int n = 0; n < 4; ++n) if ((recall.mask & (1u << n)) &&
                ArpUnit::note_semitones(unit->params[ArpUnit::P_NOTE1+n].base) != ArpUnit::NOTE_OFF) return false;
            static_cast<ArpUnit*>(unit)->note_recall = recall;
        }
    }
    if (required && seen_usig) {
        // The four-wide compatibility mirror must agree, regardless of chunk order.
        for (int i=0;i<LEGACY_SIGIL_COUNT;++i) {
            const auto& s=parsed_sigils[i]; const auto& old=target.sigils[i];
            if (s.mode != (uint8_t)old.mode || s.rate != target.sigil_rate(i) || s.depth != old.depth ||
                std::memcmp(s.table,old.table,sizeof s.table) || s.mod_src != (uint8_t)target.sigil_mods[i].src ||
                s.mod_depth != target.sigil_mods[i].depth || bool(s.sync) != target.sigil_sync[i] ||
                s.division != target.sigil_div[i] || s.step_dest != target.step_dest[i]) return false;
        }
        for (int i=0;i<NUM_SIGILS;++i) {
            const auto& p=parsed_sigils[i]; auto& s=target.sigils[i];
            s.mode=(SigilMode)p.mode; s.drawn=p.drawn; s.depth=p.depth;
            std::memcpy(s.table,p.table,sizeof s.table);
            target.sigil_config[i]=p.config;
            target.set_sigil_rate(i,p.rate); target.set_sigil_mod(i,(ModSource)p.mod_src,p.mod_depth);
            target.set_sigil_div(i,p.division); target.set_step_dest(i,p.step_dest); target.set_sigil_sync(i,p.sync);
            target.reset_sigil(i);
        }
    }
    if (required && seen_pbay) {
        for (int i=0;i<LEGACY_SIGIL_COUNT;++i) {
            const auto& p=parsed_lfos[i]; SigilConfig config;
            config.legacy=false; config.enabled=p.active; config.unipolar=p.unipolar;
            config.shape=(SigilShape)(p.shape+1); config.restart=(SigilRestart)p.restart;
            config.rate_millihz=p.rate_millihz; config.phase_degrees=p.phase_degrees;
            target.sigil_config[i+4]=config;
            target.set_sigil_div(i+4,p.division); target.set_sigil_sync(i+4,p.sync);
            target.reset_sigil(i+4);
        }
    }
    // Apply after all legacy unit canonicalization, independent of chunk order.
    if (required && (seen_pbay || seen_usig)) {
        for (int c=0;c<NUM_CHAINS;++c) for (int s=0;s<UNITS_PER_CHAIN;++s) {
            Unit* unit = target.chains[c].units[s];
            if (!unit) continue;
            for (int p=0;p<unit->num_params;++p)
                unit->params[p].mod_enabled = !(parsed_disabled[c][s] & (1u << p));
            // A PNOT record may deliberately retain the inactive note cable in
            // tape mode, where it is outside the eleven exposed parameters.
            if (parsed_note[c][s])
                unit->params[PlayUnit::P_NOTE].mod_enabled = !(parsed_disabled[c][s] & (1u << PlayUnit::P_NOTE));
        }
    }
    if (required) for (int c = 0; c < NUM_CHAINS; ++c) for (int s = 0; s < UNITS_PER_CHAIN; ++s) {
        Unit* unit = target.chains[c].units[s];
        if (unit && unit->kind == UnitKind::Reverb)
            canonicalize_reverb(target, c, s, (parsed_unit_flags[c][s] & 2u) != 0);
    }
    // Stage only after every PEDT has contributed its source-wide endpoint flag:
    // a plain alias preceding an exact alias must receive identical metadata.
    if (required && manifest.has_sample_manifest)
        for (auto& chain : target.chains) for (Unit* unit : chain.units)
            if (unit && unit->kind == UnitKind::SamplePlayer) {
                auto* play = static_cast<PlayUnit*>(unit);
                play->stage_edit_source(play_source_metadata(manifest, play->sample_slot));
            }
    if (required && seen_clock) for (int i = 0; i < 2; ++i)
        target.set_clock_mod((ClockTarget)i, parsed_clock[i].src, parsed_clock[i].depth);
    if (required && seen_mrnd) for (int ch=0; ch<NUM_CHAINS; ++ch) for (int slot=0; slot<UNITS_PER_CHAIN; ++slot) {
        if (parsed_random[ch][slot]==ModSource::None) continue;
        Unit* unit=target.chains[ch].units[slot];
        if (!unit) return false;
        // Unit was freshly constructed; swap_patch_state/transport_reset disarm
        // edges again. No live source sampling or reroll occurs while loading.
        unit->random_source=parsed_random[ch][slot];
    }
    if(format==19) {
        if(!required || !seen_relc || !seen_rlmo || !seen_usig)return false;
        if(!target.relic_storage_valid())return false;
    }
    if (format >= 20 && (!seen_usig || !pent_bases_valid(target) || !rule_bases_valid(target) || !rot_bases_valid(target))) return false;
    // Setter reports valid+changed, not admission: a valid explicit default is
    // allowed too. Fresh staging also gives every optional-less legacy its default.
    if (required) target.set_project_scale(parsed_scale);
    return required;
}

static bool load_legacy(FILE* f, Engine& target, SampleManifest& manifest, int ver) {
    bool ok = true;
    for (int c=0;c<NUM_CHAINS&&ok;++c) {
        int16_t vol,pan=0;uint8_t mute;ok=r16(f,vol);if(ok&&ver>=2)ok=r16(f,pan);if(ok)ok=r8(f,mute);if(!ok)break;
        target.chains[c].volume=vol;target.chains[c].pan=clamp_pan(pan);target.chains[c].mute=mute!=0;
        for(int s=0;s<UNITS_PER_CHAIN&&ok;++s){uint8_t kind,bypass,extra;ok=r8(f,kind)&&r8(f,bypass)&&r8(f,extra);if(ok&&(kind==uint8_t(UnitKind::Paul)||kind==uint8_t(UnitKind::Relic)||kind==uint8_t(UnitKind::Pent)||kind==uint8_t(UnitKind::Rule)||kind==uint8_t(UnitKind::Rot)))return false;Unit*u=ok&&kind<(uint8_t)UnitKind::Count?make_loaded_unit((UnitKind)kind):nullptr;if(ok&&kind!=(uint8_t)UnitKind::Empty&&!u)ok=false;if(u){u->bypass=bypass!=0;apply_extra(u,extra);}for(int p=0;p<LEGACY_PARAM_COUNT&&ok;++p){int16_t base,depth;uint8_t src;ok=r16(f,base)&&r8(f,src)&&r16(f,depth);if(ok)apply_param(u,p,base,src,depth);}if(!ok){delete u;break;}delete target.replace_unit(c,s,u);}
    }
    ok=ok&&read_sigils(f,target);
    for(int i=0;i<LEGACY_SIGIL_COUNT&&ok;++i){uint8_t src;int16_t depth;ok=r8(f,src)&&r16(f,depth);if(ok)target.set_sigil_mod(i,src<(uint8_t)ModSource::Count?(ModSource)src:ModSource::None,depth);}
    if(ok&&ver>=3){uint16_t bpm;ok=ru16(f,bpm);if(ok)target.set_bpm((int)bpm);for(int i=0;i<LEGACY_SIGIL_COUNT&&ok;++i){uint8_t sync,div,dest;ok=r8(f,sync)&&r8(f,div)&&r8(f,dest);if(ok){target.set_sigil_div(i,div);target.set_step_dest(i,dest);target.set_sigil_sync(i,sync!=0);}}}
    if(ok&&ver>=4)for(int c=0;c<NUM_CHAINS&&ok;++c)for(int s=0;s<UNITS_PER_CHAIN&&ok;++s){uint8_t flags;ok=r8(f,flags);if(ok)apply_unit_flags(target.chains[c].units[s],flags);}
    if(ok&&ver>=5){manifest.has_sample_manifest=true;for(int slot=0;slot<SampleManifest::SLOT_COUNT&&ok;++slot){uint8_t origin,missing;uint16_t n;ok=r8(f,origin)&&r8(f,missing)&&ru16(f,n);if(!ok||n>=sizeof(manifest.slots[slot].path)){ok=false;break;}auto&e=manifest.slots[slot];if(n&&fread(e.path,1,n,f)!=n){ok=false;break;}e.path[n]=0;e.origin=origin<=(uint8_t)trackr::synth::SampleOrigin::Recording?origin:(uint8_t)trackr::synth::SampleOrigin::Runtime;e.missing=missing!=0;}uint8_t play,beats,q;ok=ok&&r8(f,play)&&r8(f,beats)&&r8(f,q);if(ok){target.transport_playing=play!=0;target.beats_per_bar=beats>=2&&beats<=8?beats:4;target.launch_quantize=q<=(uint8_t)LaunchQuantize::Bar?(LaunchQuantize)q:LaunchQuantize::Immediate;}bool ext=true;if(ver==5){int x=fgetc(f);if(x==EOF){clearerr(f);ext=false;}else ungetc(x,f);}if(ext)ok=ok&&read_legacy_strn(f,target);}
    // Pre-SNC7 UNIT bits and the sixth historical padding slot never opt in to
    // modern Tilt or create a route to its newly appended wet trim.
    if (ok) for (auto& chain : target.chains) for (Unit* unit : chain.units) {
        if (!unit || unit->kind != UnitKind::Tilt) continue;
        static_cast<TiltUnit*>(unit)->set_modern(false);
        for (int p=TiltUnit::P_COUNT;p<PARAM_BANK_SIZE;++p) unit->params[p]=Param{};
    }
    // Historical UNIT bits never opt in to MIC low-cut, including SNC4..6.
    if (ok) for (int c = 0; c < NUM_CHAINS; ++c) for (int s = 0; s < UNITS_PER_CHAIN; ++s) {
        Unit* unit = target.chains[c].units[s];
        if (unit && unit->kind == UnitKind::Osc) {
            static_cast<OscUnit*>(unit)->shape_morph = false;
            static_cast<OscUnit*>(unit)->clean = false;
            canonicalize_osc(target, c, s, false);
        }
        if (unit && unit->kind == UnitKind::Grain) {
            static_cast<GrainUnit*>(unit)->clean = false;
            canonicalize_grain_scan(target, c, s);
        }
        if (unit && unit->kind == UnitKind::Fm) canonicalize_fm(target, c, s, false);
        if (unit && unit->kind == UnitKind::SamplePlayer) {
            finish_play_mode(unit, false);
            canonicalize_play_position(target, c, s, false);
        }
        if (unit && unit->kind == UnitKind::Vca) canonicalize_vca(target,c,s,0);
        if (unit && unit->kind == UnitKind::Motor) canonicalize_motor(target, c, s, false);
        if (unit && unit->kind == UnitKind::Tilt) tilt_edit::clear_inactive(target,c,s,TiltUnit::P_TRIM);
        if (unit && unit->kind == UnitKind::Fold) canonicalize_fold(target, c, s, false);
        if (unit && unit->kind == UnitKind::Ring) canonicalize_ring(target, c, s, false);
        if (unit && unit->kind == UnitKind::Strain) canonicalize_strain(target, c, s, false);
        if (unit && unit->kind == UnitKind::Bitcrush) canonicalize_bit(target, c, s, false);
        if (unit && unit->kind == UnitKind::MicIn) canonicalize_mic(target, c, s, false);
        if (unit && unit->kind == UnitKind::Reverb) canonicalize_reverb(target, c, s, false);
        // Even historical UNIT bit0 cannot opt into attack/mode in SNC1..6.
        if (unit && unit->kind == UnitKind::Noise) {
            if (ver < 4) apply_unit_flags(unit, 0);
            canonicalize_noise(target, c, s, false);
        }
    }
    return ok;
}

// RULE config admission is a read-only full-container pass before ANY factory.
// Even reordered/missing/duplicate/future RULE records cannot allocate a staging
// engine or touch the bank. Other historical chunk policy remains in the parser.
static bool rule_preflight(FILE* f,uint8_t version) {
    if(version<7)return true; // legacy kind admission belongs to retirement scan
    const long begin=ftell(f);if(begin<0 || fseek(f,0,SEEK_END))return false;
    const long end=ftell(f);if(end<begin)return false;
    uint32_t actual=0,configured=0;uint8_t flags[24]={};bool seen_rule=false,seen_core=false,seen_unit=false;
    for(long pos=begin;pos<end;) {
        if(end-pos<8 || fseek(f,pos,SEEK_SET))return false;
        char id[4];uint32_t len=0;if(fread(id,1,4,f)!=4 || !ru32(f,len) || len>MAX_CHUNK_SIZE || uint64_t(len)>uint64_t(end-pos-8))return false;
        const long payload=pos+8;
        if(!std::memcmp(id,"CORE",4)) {
            constexpr uint32_t size=NUM_CHAINS*(5+UNITS_PER_CHAIN*(3+12*5));
            if(seen_core || len!=size)return false;
            seen_core=true;
            for(int c=0;c<4;++c) {
                int16_t vol=0,pan=0;uint8_t mute=0;if(!r16(f,vol)||!r16(f,pan)||!r8(f,mute))return false;
                for(int s=0;s<6;++s) {
                    uint8_t kind=0,bypass=0,extra=0;if(!r8(f,kind)||!r8(f,bypass)||!r8(f,extra))return false;
                    const bool rule=kind==uint8_t(UnitKind::Rule);
                    if(rule){if(version<21 || extra>=64)return false;actual|=1u<<(c*6+s);}
                    for(int p=0;p<12;++p){int16_t base=0,depth=0;uint8_t src=0;if(!r16(f,base)||!r8(f,src)||!r16(f,depth))return false;
                        if(rule && (src>=uint8_t(ModSource::Count) || base<(p==RuleUnit::P_ROOT?-32767:0)))return false;}
                }
            }
        } else if(!std::memcmp(id,"UNIT",4)) {
            if(seen_unit || len!=24)return false;
            seen_unit=true;
            for(auto& v:flags)if(!r8(f,v))return false;
        } else if(!std::memcmp(id,"RULE",4)) {
            if(version<21 || seen_rule || len<2)return false;
            seen_rule=true;
            uint8_t v=0,count=0;if(!r8(f,v)||!r8(f,count)||v!=1||!count||count>24||len!=2u+10u*count)return false;
            for(int i=0;i<count;++i) {
                uint8_t c=0,s=0;RuleUnit::Config cfg;
                if(!r8(f,c)||!r8(f,s)||!ru32(f,cfg.seed)||!ru16(f,cfg.seed_row)||!r8(f,cfg.phrase)||!r8(f,cfg.source_root)||c>=4||s>=6||!RuleUnit::valid_config(cfg))return false;
                const uint32_t bit=1u<<(c*6+s);if(configured&bit)return false;configured|=bit;
            }
        } else if(!std::memcmp(id,"SCNE",4)) {
            constexpr uint32_t size=4*(2+2*(5+6*25));if(len!=size)return false;
            for(int c=0;c<4;++c){int16_t morph=0;if(!r16(f,morph))return false;
                for(int n=0;n<2;++n){uint8_t valid=0;int16_t volume=0,pan=0;if(!r8(f,valid)||!r16(f,volume)||!r16(f,pan))return false;
                    for(int s=0;s<6;++s){uint8_t kind=0;if(!r8(f,kind))return false;
                        if(kind==uint8_t(UnitKind::Rule) && version<21)return false;
                        for(int p=0;p<12;++p){int16_t base=0;if(!r16(f,base))return false;if(kind==uint8_t(UnitKind::Rule) && base<(p==RuleUnit::P_ROOT?-32767:0))return false;}
                    }
                }
            }
        }
        pos=payload+long(len);
    }
    if(actual!=configured || (actual && (!seen_core || !seen_unit)))return false;
    for(int i=0;i<24;++i)if((actual&(1u<<i)) && flags[i])return false;
    return fseek(f,begin,SEEK_SET)==0;
}

// ROT read-only admission across the complete physical container, before factories.
// Format22 checks duplicate IDs recognized by load_v7 and exclusive USIG/PBAY.
// This does NOT validate every historical payload semantic before staging.
static bool rot_preflight(FILE* f,uint8_t version) {
    if(version<7)return true;
    const long begin=ftell(f);if(begin<0 || fseek(f,0,SEEK_END))return false;
    const long end=ftell(f);if(end<begin)return false;
    uint32_t actual=0,configured=0;uint8_t flags[24]={};bool seen_rot=false,seen_core=false,seen_unit=false,seen_scene=false;
    uint32_t required=0;
    // Complete recognized-ID set from load_v7; first8 remain required in22.
    static constexpr const char* ids[]={"CORE","SIGL","ROUT","UNIT","SAMP","TRNS","STRN","USIG","ROTS","RULE","PENT","SCNE","MRND","CLCK","PAUL","SCRT","GRNX","ENVR","WSPC","PBNK","PTRN","SDIR","MTIM","PCHN","CHOP","SREV","PSPD","SPIT","SEXT","RELC","RLMO","ARPM","GLCH","GRNP","MPAD","PBAY","PBND","PEDT","PHLD","PNOT","PPOS","SCAL"};
    constexpr size_t id_count=sizeof ids/sizeof ids[0];
    static_assert(id_count<=64,"ROT recognized duplicate mask capacity");
    uint64_t seen_ids=0;
    bool seen_usig=false,seen_pbay=false,seen_scale=false;
    for(long pos=begin;pos<end;) {
        if(end-pos<8 || fseek(f,pos,SEEK_SET))return false;
        char id[4];uint32_t len=0;if(fread(id,1,4,f)!=4 || !ru32(f,len) || len>MAX_CHUNK_SIZE || uint64_t(len)>uint64_t(end-pos-8))return false;
        const long payload=pos+8;
        if(version==22) {
            for(size_t n=0;n<id_count;++n)if(!std::memcmp(id,ids[n],4)) {
                const uint64_t bit=uint64_t(1)<<n;
                if(seen_ids&bit)return false;
                seen_ids|=bit;if(n<8)required|=uint32_t(bit);
                break;
            }
            if(!std::memcmp(id,"USIG",4)){if(seen_pbay)return false;seen_usig=true;}
            if(!std::memcmp(id,"PBAY",4)){if(seen_usig)return false;seen_pbay=true;}
        }
        // SCAL is strict across every admitted SNC7+ container, not only22.
        // Sharing the literal parser prevents preflight/staging policy drift.
        if(!std::memcmp(id,"SCAL",4)) {
            MusicalScale scale{};
            if(seen_scale || !read_scale(f,len,scale))return false;
            seen_scale=true;
        } else if(!std::memcmp(id,"CORE",4)) {
            constexpr uint32_t size=NUM_CHAINS*(5+UNITS_PER_CHAIN*(3+12*5));
            if(seen_core || len!=size){return false;}seen_core=true;
            for(int c=0;c<4;++c) {
                int16_t vol=0,pan=0;uint8_t mute=0;if(!r16(f,vol)||!r16(f,pan)||!r8(f,mute))return false;
                for(int s=0;s<6;++s) {
                    uint8_t kind=0,bypass=0,extra=0;if(!r8(f,kind)||!r8(f,bypass)||!r8(f,extra))return false;
                    const bool rot=kind==uint8_t(UnitKind::Rot);
                    if(rot){if(version<22 || extra>=64 || bypass>1 || mute>1)return false;actual|=1u<<(c*6+s);}
                    for(int p=0;p<12;++p){int16_t base=0,depth=0;uint8_t src=0;if(!r16(f,base)||!r8(f,src)||!r16(f,depth))return false;
                        if(rot && (src>=uint8_t(ModSource::Count) || base<(p==RotUnit::P_PITCH?-32767:0)))return false;}
                }
            }
        } else if(!std::memcmp(id,"UNIT",4)) {
            if(seen_unit || len!=24){return false;}seen_unit=true;for(auto& v:flags)if(!r8(f,v))return false;
        } else if(!std::memcmp(id,"ROTS",4)) {
            if(version<22 || seen_rot || len<2){return false;}seen_rot=true;
            uint8_t v=0,count=0;if(!r8(f,v)||!r8(f,count)||(v!=1&&v!=2)||!count||count>24||len!=2u+(v==2?10u:9u)*count)return false;
            for(int i=0;i<count;++i) {
                uint8_t c=0,s=0,mode=0,law=0;RotUnit::Config cfg;
                if(!r8(f,c)||!r8(f,s)||!ru32(f,cfg.seed)||!ru16(f,cfg.passes)||!r8(f,mode)||(v==2&&!r8(f,law))||c>=4||s>=6)return false;
                cfg.mode=RotUnit::Mode(mode);cfg.law=law;if(!RotUnit::valid_config(cfg))return false;
                const uint32_t bit=1u<<(c*6+s);if(configured&bit)return false;configured|=bit;
            }
        } else if(!std::memcmp(id,"SCNE",4)) {
            constexpr uint32_t size=4*(2+2*(5+6*25));if(seen_scene || len!=size)return false;seen_scene=true;
            for(int c=0;c<4;++c){int16_t morph=0;if(!r16(f,morph))return false;
                for(int n=0;n<2;++n){uint8_t valid=0;int16_t volume=0,pan=0;if(!r8(f,valid)||!r16(f,volume)||!r16(f,pan))return false;
                    for(int s=0;s<6;++s){uint8_t kind=0;if(!r8(f,kind))return false;
                        if(kind==uint8_t(UnitKind::Rot) && (version<22 || valid>1))return false;
                        for(int p=0;p<12;++p){int16_t base=0;if(!r16(f,base))return false;if(kind==uint8_t(UnitKind::Rot) && base<(p==RotUnit::P_PITCH?-32767:0))return false;}
                    }
                }
            }
        }
        pos=payload+long(len);
    }
    if(actual!=configured || (actual && (!seen_core || !seen_unit)) || (version==22 && required!=255))return false;
    for(int i=0;i<24;++i)if((actual&(1u<<i)) && flags[i])return false;
    return fseek(f,begin,SEEK_SET)==0;
}

// Read-only admission BEFORE constructing a staging Engine or any factory.
// Scan the entire physical container so scene-only/reordered retired data cannot
// pay for factories preceding its chunk. Ordinary parser bounds still apply.
static bool retirement_preflight(FILE* f,uint8_t version) {
    const long begin=ftell(f);
    if(begin<0 || fseek(f,0,SEEK_END)!=0)return false;
    const long end=ftell(f);
    if(end<begin || fseek(f,begin,SEEK_SET)!=0)return false;
    auto kind_at=[&](long at) {
        if(at<begin || at>=end || fseek(f,at,SEEK_SET)!=0)return false;
        uint8_t kind=0;
        return r8(f,kind) && kind!=uint8_t(UnitKind::Relic) &&
            kind<uint8_t(UnitKind::Count) && !(version<22 && kind==uint8_t(UnitKind::Rot)) && !(version<21 && kind==uint8_t(UnitKind::Rule)) &&
            !(version < 20 && kind == uint8_t(UnitKind::Pent));
    };
    if(version<7) {
        const long chain_size=(version>=2?5:3)+UNITS_PER_CHAIN*(3+LEGACY_PARAM_COUNT*5);
        for(int c=0;c<NUM_CHAINS;++c)for(int s=0;s<UNITS_PER_CHAIN;++s)
            if(!kind_at(begin+c*chain_size+(version>=2?5:3)+s*(3+LEGACY_PARAM_COUNT*5)))return false;
    } else {
        for(long pos=begin;pos<end;) {
            if(end-pos<8 || fseek(f,pos,SEEK_SET)!=0)return false;
            char id[4];uint32_t len=0;
            if(fread(id,1,4,f)!=4 || !ru32(f,len) || len>MAX_CHUNK_SIZE || uint64_t(len)>uint64_t(end-pos-8))return false;
            const long payload=pos+8;
            if(!std::memcmp(id,"CORE",4)) {
                constexpr long chain_size=5+UNITS_PER_CHAIN*(3+PARAM_BANK_SIZE*5);
                if(len!=NUM_CHAINS*chain_size)return false;
                for(int c=0;c<NUM_CHAINS;++c)for(int s=0;s<UNITS_PER_CHAIN;++s)
                    if(!kind_at(payload+c*chain_size+5+s*(3+PARAM_BANK_SIZE*5)))return false;
            } else if(!std::memcmp(id,"SCNE",4)) {
                constexpr long scene_size=5+UNITS_PER_CHAIN*(1+MAX_PARAMS*2);
                constexpr long chain_size=2+2*scene_size;
                if(len!=NUM_CHAINS*chain_size)return false;
                for(int c=0;c<NUM_CHAINS;++c)for(int n=0;n<2;++n)for(int s=0;s<UNITS_PER_CHAIN;++s)
                    if(!kind_at(payload+c*chain_size+2+n*scene_size+5+s*(1+MAX_PARAMS*2)))return false;
            } else if(!std::memcmp(id,"PENT",4) && version < 20) {
                return false;
            } else if(!std::memcmp(id,"RELC",4) || !std::memcmp(id,"RLMO",4)) {
                uint8_t v=0,flags=0;uint16_t count=0;
                if(version<19 || len!=4 || !r8(f,v) || !r8(f,flags) || !ru16(f,count) || v!=1 || flags || count)return false;
            }
            pos=payload+long(len);
        }
    }
    return fseek(f,begin,SEEK_SET)==0;
}

bool load(Engine& eng, const char* path, SampleManifest* out_manifest,
          WorkspaceLayout* out_workspace) {
    if (eng.recording_active_or_pending()) return false;
    FILE* f=fopen(path,"rb");if(!f)return false;
    char magic[4];uint8_t ver=0;
    bool ok=fread(magic,1,4,f)==4&&magic[0]=='S'&&magic[1]=='N'&&magic[2]=='C'&&r8(f,ver);
    // Validate the exact magic/version envelope before any staging allocation.
    const char suffix = ver < 10 ? char('0' + ver) : char('A' + ver - 10);
    if(!ok || ver<1 || ver>22 || magic[3]!=suffix || !rot_preflight(f,ver) || !rule_preflight(f,ver) || !retirement_preflight(f,ver)){fclose(f);return false;}
    Engine* parsed=new(std::nothrow) Engine();
    SampleManifest* manifest=new(std::nothrow) SampleManifest();
    WorkspaceLayout* workspace=new(std::nothrow) WorkspaceLayout();
    if(!parsed||!manifest||!workspace){delete parsed;delete manifest;delete workspace;fclose(f);return false;}
    workspace_layout_defaults(*workspace);
    if(ok&&((magic[3]>='7'&&magic[3]<='9'&&ver==(uint8_t)(magic[3]-'0')) || (magic[3]=='A'&&ver==10) || (magic[3]=='B'&&ver==11) || (magic[3]=='C'&&ver==12) || (magic[3]=='M'&&ver==22) || (magic[3]=='L'&&ver==21) || (magic[3]=='K'&&ver==20) || (magic[3]=='J'&&ver==19) || (magic[3]=='I'&&ver==18) || (magic[3]=='H'&&ver==17) || (magic[3]=='G'&&ver==16) || (magic[3]=='F'&&ver==15) || (magic[3]=='E'&&ver==14) || (magic[3]=='D'&&ver==13)))ok=load_v7(f,*parsed,*manifest,*workspace,ver);
    else if(ok&&magic[3]>='1'&&magic[3]<='6'&&ver==(uint8_t)(magic[3]-'0'))ok=load_legacy(f,*parsed,*manifest,ver);
    else ok=false;
    if (ferror(f)) ok=false;
    if (fclose(f)!=0) ok=false;
    // The instrument q15 source map now has seven bins instead of six. No
    // heuristic migration: user explicitly chose the new source contract.
    if(ok && ver<16)for(const auto& chain:parsed->chains)for(const Unit* unit:chain.units)
        if(unit && (unit->kind==UnitKind::Inst || unit->kind==UnitKind::Scratch))ok=false;
    if(ok && ver<17)for(const auto& chain:parsed->chains)for(const Unit* unit:chain.units)
        if(unit && unit->kind==UnitKind::Paul)ok=false;
    if(!ok || eng.recording_active_or_pending()){delete parsed;delete manifest;delete workspace;return false;}
    eng.swap_patch_state(*parsed);
    // SCRT is installed into the parser first, so its runtime context must not
    // point at that soon-to-be-retired staging Engine. Reinstall the SAME unit
    // through the normal ownership hook; no allocation, clone or config change.
    for (int ch=0; ch<NUM_CHAINS; ++ch) for (int slot=0; slot<UNITS_PER_CHAIN; ++slot) {
        Unit* unit = eng.chains[ch].units[slot];
        if (unit && unit->kind == UnitKind::Scratch) eng.replace_unit(ch, slot, unit);
    }
    if(out_manifest)*out_manifest=*manifest;
    else if(ver>=5){auto&bank=trackr::synth::SampleBank::instance();for(int slot=0;slot<SampleManifest::SLOT_COUNT;++slot){const auto&e=manifest->slots[slot];bank.set_provenance(slot,(trackr::synth::SampleOrigin)e.origin,e.path,e.missing);}}
    if(out_workspace)*out_workspace=*workspace;
    delete parsed;
    delete manifest;
    delete workspace;
    return true;
}

int decode_samples(SampleManifest& manifest, int target_sr, int max_frames, RestoredSamples& out) {
    if (target_sr <= 0 || max_frames <= 0) return 0;
    int restored = 0;
    auto& bank = trackr::synth::SampleBank::instance();
    trackr::synth::WavDecodeLimits limits;
    limits.workspace_bytes = trackr::synth::WAV_DECODE_WORKSPACE_BYTES;
    for (int slot = 0; slot < trackr::synth::SAMPLE_BANK_SIZE; ++slot) {
        auto& e = manifest.slots[slot];
        e.missing = false;
        out.ready[slot] = false;
        out.decoded[slot] = {};
        if (e.origin == (uint8_t)trackr::synth::SampleOrigin::Runtime || !e.path[0]) continue;
        limits.preserve_native_endpoint = manifest.exact_native[slot];
        auto r = trackr::synth::load_wav_to_sample(e.path, out.decoded[slot], target_sr, max_frames, limits);
        if (r == trackr::synth::WavLoadResult::MemoryBudget) return -1;
        out.ready[slot] = r == trackr::synth::WavLoadResult::Ok || r == trackr::synth::WavLoadResult::Truncated;
        e.missing = !out.ready[slot];
        if (out.ready[slot]) {
            if (!bank.can_replace_all(out.decoded, out.ready, SampleManifest::SLOT_COUNT))
                return -1;
            ++restored;
        }
    }
    return restored;
}

bool commit_restored_samples(const SampleManifest& manifest, RestoredSamples& decoded, Engine* hydrated, Engine* live_before_swap) {
    auto& bank = trackr::synth::SampleBank::instance();
    if (!bank.can_replace_all(decoded.decoded, decoded.ready, SampleManifest::SLOT_COUNT)) return false;
    // Normalize absent slots to empty before the no-fail swap. After this point
    // decoded owns the complete old bank, so its multi-megabyte vectors are
    // destroyed by the controller only after AudioOut::Guard is released.
    // decode_samples already reset every absent staged slot before this call.
    // Disarm PAUL before replacing the bank behind its latched FFT source.
    // Budget rejection above leaves both bank and live runtime untouched.
    if((hydrated && !hydrated->relic_storage_valid()) || (live_before_swap && !live_before_swap->relic_storage_valid()))return false;
    if(live_before_swap)live_before_swap->retire_relic_sources_all();
    if(hydrated)hydrated->retire_relic_sources_all();
    if (hydrated) for (auto& chain : hydrated->chains) for (Unit* unit : chain.units)
        if (unit && (unit->kind == UnitKind::Paul || unit->kind == UnitKind::Rule || unit->kind == UnitKind::Rot)) unit->reset();
    if(live_before_swap)for(auto& chain:live_before_swap->chains)for(Unit* unit:chain.units)if(unit && (unit->kind==UnitKind::Rule || unit->kind==UnitKind::Rot))unit->reset();
    bank.swap_all(decoded.decoded, SampleManifest::SLOT_COUNT);
    for (int slot = 0; slot < trackr::synth::SAMPLE_BANK_SIZE; ++slot) {
        bank.slot(slot).exact_native_endpoint = manifest.exact_native[slot];
        if (manifest.has_chops) {
            const uint32_t frames = bank.slot(slot).playable_frames();
            for (int chop = 0; chop < trackr::synth::Sample::MAX_CHOPS; ++chop) {
                const uint32_t value = manifest.chops[slot][chop];
                bank.slot(slot).chops[chop] = value != 0xffffffffu && value + 1 < frames
                    ? value : 0xffffffffu;
            }
        }
        if (manifest.has_slice_reverse) {
            uint16_t valid_mask = 0;
            for (int chop = 0; chop < trackr::synth::Sample::MAX_CHOPS; ++chop)
                if (bank.slot(slot).chops[chop] != 0xffffffffu)
                    valid_mask |= (uint16_t)(1u << chop);
            bank.slot(slot).slice_reverse_mask = manifest.slice_reverse[slot] & valid_mask;
        }
        bank.slot(slot).reset_cut_speeds();
        if (manifest.has_slice_speed) for (int cut = 0; cut < trackr::synth::Sample::MAX_CHOPS; ++cut)
            if (bank.slot(slot).chops[cut] != UINT32_MAX)
                bank.slot(slot).slice_speed_q8[cut] = manifest.slice_speed_q8[slot][cut];
        bank.slot(slot).reset_cut_pitches();
        if (manifest.has_slice_pitch) for (int cut = 0; cut < trackr::synth::Sample::MAX_CHOPS; ++cut)
            if (bank.slot(slot).chops[cut] != UINT32_MAX)
                bank.slot(slot).slice_pitch_semitones[cut] = manifest.slice_pitch_semitones[slot][cut];
        const auto& entry = manifest.slots[slot];
        bank.set_provenance(slot, (trackr::synth::SampleOrigin)entry.origin,
                            entry.path, entry.missing);
    }
    if (hydrated) for (auto& chain : hydrated->chains) for (Unit* unit : chain.units) {
        if (unit && unit->kind == UnitKind::Scratch) {
            // SCRT reads the same committed slot as PLAY/GRAN. Never keep an old
            // gate/gesture/source pointer across hydration (including missing
            // sources); the next explicit trigger latches this bank generation.
            static_cast<ScratchUnit*>(unit)->reset();
            continue;
        }
        if (!unit || unit->kind != UnitKind::SamplePlayer) continue;
        auto* play = static_cast<PlayUnit*>(unit);
        const int slot = play->sample_slot;
        if (slot < 0 || slot >= SampleManifest::SLOT_COUNT) continue;
        if (decoded.ready[slot] && !manifest.slots[slot].missing && play->bind_edit_source()) continue;
        // Missing or changed-length sources keep exact metadata, not playback.
        const PlayEditState edit = play->edit_state();
        play->restore_edit_state(edit);
        play->stage_edit_source(play_source_metadata(manifest, slot), true);
    }
    if(hydrated)hydrated->hydrate_relic_sources();
    return true;
}

int restore_samples(SampleManifest& manifest, int target_sr, int max_frames, Engine* hydrated) {
    RestoredSamples decoded;
    const int restored = decode_samples(manifest, target_sr, max_frames, decoded);
    if (restored < 0 || !commit_restored_samples(manifest, decoded, hydrated)) return -1;
    return restored;
}

} // namespace seance::patchio
