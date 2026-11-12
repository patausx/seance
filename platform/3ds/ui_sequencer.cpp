#include "ui_play_editor.h"
#include "ui_sequencer.h"
#include "ui_state.h"
#include "ui_theme.h"
#include "ui_palette.h"
#include "audio_out.h"
#include "sequencer_layout.h"
#include "sequencer_mode_ring.h"
#include "step_focus_ink.h"
#include "step_feedback.h"
#include "sequencer_inspector.h"
#include "sequencer_step_edit.h"
#include "sequencer_lock_format.h"
#include "sequence_note.h"
#include "sequencer_scale_menu.h"
#include "sequencer_slices.h"
#include "sequencer_cuts.h"
#include "pcm_envelope.h"
#include "slice_boundaries.h"
#include "menu_style.h"
#include "module_fader.h"
#include "inspector_gothic.h"
#include "sequencer_wave.h"
#include "sequencer_bank_panel.h"
#include "../../core/engine/play_window.h"
#include "../../core/ui/draw.h"
#include "../../core/ui/font.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace seance;
namespace seance::platform::ui3ds {
namespace {
using namespace seq_layout;
namespace m = menu_style;
namespace ui = sequencer_scale::drawing;
constexpr int META_LOCK_SHIFT = 6;
constexpr int META_TIMING_SHIFT = 9;
constexpr int PATTERN_X0 = X0;
constexpr int PATTERN_X1 = X0 + (PAGE_STEPS - 1) * PITCH + CELL_W - 1;
constexpr const int *PATTERN_ROWS = ROWS;

enum class SeqMode : uint8_t { Trig, Note, Level, Retrig, When, Time, Lock, Slice, Count };
static_assert((int)SeqMode::Count == MODE_COUNT && (int)SeqMode::Slice == seq_slice::MODE,
              "mode ordering agrees with shared layout");

int g_seq_chain = 0;
int g_seq_col = 0;
int g_seq_page = 0;
SeqMode g_seq_mode = SeqMode::Trig;
SeqMode g_ring_mode = SeqMode::Trig;
seq_ring::State g_ring_pointer;
step_focus::State g_step_focus;
step_focus::Box g_focus_box{};
bool g_focus_inverse = false;
step_feedback::Press g_step_press;
uint64_t g_feedback_now = 0;
bool g_feedback_visible = false;
bool g_mode_ring = false;
bool g_r_pressed = false;
bool g_r_choice = false;
bool g_r_cancel = false;
bool g_l_held = false;
bool g_touch_blocked_frame = false;
u32 g_r_start = 0;
int g_pending_chain = -1;
int g_pending_step = -1;
bool g_value_drag = false;
bool g_paint_active = false;
bool g_paint_value = false;
int g_paint_last_step = -1;
int g_touch_origin_x = 0;
int g_lock_slot = 0;
int g_lock_param = 0;
SequencerInspector g_inspector;
seq_slice::Frame g_slices;
seq_cuts::Frame g_cuts;
bool g_cuts_open=false, g_cut_add=false;
seq_edit::Selection g_selection;
seq_edit::Clipboard g_clipboard;
seq_edit::History g_history;
seq_edit::Row g_edit_before, g_edit_draft;
seq_edit::Scope g_copy_scope = seq_edit::Scope::Steps;
int g_edit_bank=-1, g_history_chain=-1;
uint64_t g_edit_signature=0, g_target_signature[NUM_CHAINS]={};
int g_param_memory[NUM_CHAINS][UNITS_PER_CHAIN]={};
bool g_offset=false, g_tools=false, g_l_drain=false;
bool g_ring_dirty=false, g_freeze_without_address=false; // frozen disclosure backgrounds
bool g_pad_center=false;
int g_pad_direction=0, g_select_chain=-1, g_drag_origin_x=0;
u32 g_pad_start=0;
char g_edit_status[48]={};
void finish_edit();
void sync_edit_context();
void nudge_step_values(int direction, bool group);
int g_seq_bank = -1;
EngineUiSnapshot g_score;
uint8_t g_wave_y[Engine::SCOPE_LEN]={}; // 200 frozen screen coordinates, not an audio buffer.
uint8_t g_wave_accent=0; // frozen observed peak, shared by the entire trace
static_assert(sizeof(g_score) <= 8192, "bounded static edit projection");
bool g_b_pressed=false, g_b_open=false, g_b_drain=false;
u32 g_b_start=0, g_b_repeat_start=0, g_b_repeat_key=0;
int g_b_bank=0, g_b_track=0, g_b_play=0;
// Staged per bank and track; committed when B is released, discarded by navigation.
uint8_t g_b_length[NUM_PATTERNS][NUM_CHAINS]={};
uint8_t g_b_direction[NUM_PATTERNS][NUM_CHAINS]={};
uint8_t g_b_length_edits[NUM_CHAINS]={}, g_b_direction_edits[NUM_CHAINS]={}; // bit per bank
int g_b_copy=-1, g_b_confirm=0, g_b_rail=-1; // copied bank / armed 1 paste 2 clear / dragged length rail
bool g_b_dirty=false;
char g_b_status[48]={};
SequencerBankPanel g_bank_panel;
static_assert(NUM_PATTERNS <= 8, "held-bank edit masks cover every bank");
constexpr u32 B_HOLD_FRAMES=15; // ~250 ms at the existing 60 Hz UI cadence.
constexpr u32 B_OWNED_KEYS=KEY_B|KEY_A|KEY_X|KEY_Y|KEY_L|KEY_R|
    KEY_LEFT|KEY_RIGHT|KEY_UP|KEY_DOWN|KEY_TOUCH;

int feedback_context() {
    return g_seq_page + (SEQ_STEPS / PAGE_STEPS) * (g_seq_bank + NUM_PATTERNS * int(g_seq_mode));
}
void note_value_changed() {
    g_step_press.fire(osGetTime(), feedback_context(), g_seq_chain, g_seq_col);
}

// All references are resolved under Guard; no pointer survives a bank launch.
Pattern& edit_pattern() { return g_eng->sequence_pattern(g_seq_bank); }
void capture_score_locked() {
    if (g_seq_bank < 0 || g_seq_bank >= NUM_PATTERNS) g_seq_bank=g_eng->active_pattern;
    g_score=g_ui_snapshot;
    g_eng->capture_sequence_snapshot(g_score,g_seq_bank);
}

int g_cut_selected=0, g_cut_drag=-1, g_cut_confirm=0;
int g_cut_drag_x=0;
uint32_t g_cut_drag_origin=0;
bool g_cut_drag_dirty=false;
u32 g_cut_audition_frame=0;
// Gesture identity only, never dereferenced after a guard or used by renderers.
const Unit* g_cut_unit=nullptr;
const q15* g_cut_pcm=nullptr;
int g_cut_slot=-1, g_cut_sample=-1, g_cut_chain=-1;
uint32_t g_cut_generation=0, g_cut_frames=0;
uint32_t g_cut_chops[16]={};
uint16_t g_cut_speeds[16]={};
uint16_t g_cut_reverse=0;
bool g_cut_context=false;


uint8_t meta_flags(uint16_t meta) {
    return (uint8_t)(meta & 0x3fu);
}
int meta_locks(uint16_t meta) {
    return (meta >> META_LOCK_SHIFT) & 7;
}
int meta_timing(uint16_t meta) {
    return (int)((meta >> META_TIMING_SHIFT) & 0x3f) - SEQ_MICROTIMING_MAX;
}
SeqStep::Condition meta_condition(uint16_t meta) {
    return (SeqStep::Condition)((meta_flags(meta) & SeqStep::ConditionMask) >> SeqStep::ConditionShift);
}
int meta_retrig(uint16_t meta) {
    return ((meta_flags(meta) & SeqStep::RetrigMask) >> SeqStep::RetrigShift) + 1;
}
bool meta_trig(uint16_t meta) {
    return (meta_flags(meta) & SeqStep::Trig) != 0;
}

const char *mode_name(SeqMode mode) {
    static const char *names[] = {"trig", "note", "level", "retrig", "when", "time", "lock", "slice"};
    return names[(int)mode];
}
void show_mode_ring(uint64_t now) {
    if (!g_mode_ring) g_ring_dirty = true; // freeze the score behind the chooser once per opening
    g_mode_ring = true;
    g_ring_pointer.show(now);
}
void sync_ring_choice() {
    g_r_choice = g_ring_pointer.candidate >= 0;
    if (g_r_choice) g_ring_mode = (SeqMode)g_ring_pointer.candidate;
}
void select_ring_mode(int mode) {
    const uint64_t now = osGetTime();
    g_ring_pointer.select(mode, now);
    sync_ring_choice();
    show_mode_ring(now);
}
const char *condition_name(SeqStep::Condition condition) {
    static const char *names[] = {"all", "50", "25", "75", "1:2", "2:2", "fill", "!fill"};
    const int index = (int)condition;
    return index >= 0 && index < (int)SeqStep::Condition::Count ? names[index] : "?";
}
bool flow_chain(int chain) {
    return (SequenceLifecycle)g_score.sequence_lifecycle[chain] == SequenceLifecycle::Flow;
}

// Trig layer on a constant source that has a level target: the row is its value curve.
bool flow_row(int chain) {
    return g_seq_mode == SeqMode::Trig && flow_chain(chain) &&
           g_score.sequence_value_slot[chain] >= 0 && g_score.sequence_value_param[chain] >= 0;
}
bool flow_row() { return flow_row(g_seq_chain); }

int pattern_x(int local) {
    return PATTERN_X0 + local * PITCH;
}
int local_from_touch(int tx) {
    int local = (tx - PATTERN_X0) / PITCH;
    if (local < 0)
        local = 0;
    if (local > 15)
        local = 15;
    return local;
}
int chain_from_touch(int ty) {
    for (int chain = 0; chain < NUM_CHAINS; ++chain)
        if (ty >= PATTERN_ROWS[chain] && ty < PATTERN_ROWS[chain] + CELL_H)
            return chain;
    return -1;
}

void bead(int x, int y, int radius, u32 color, bool filled = true) {
    ui::circle((float)x, (float)y, (float)radius, color, filled);
    if (!filled && radius > 2) {
        const int r = radius - 2;
        for (int dy = -r; dy <= r; ++dy) {
            int dx = 0;
            while ((dx + 1) * (dx + 1) + dy * dy <= r * r) ++dx;
            if (dx > 0) ui::paper_rect(x - dx, y + dy, 2 * dx + 1, 1);
        }
    }
}

int pitch_display_semitones(int chain, q15 value) {
    const UnitKind kind = (UnitKind)g_score.sequence_pitch_kind[chain];
    if (kind == UnitKind::Grain || kind == UnitKind::SamplePlayer) {
        int32_t increment = (int32_t)value * 8;
        if (increment < 0)
            increment = -increment;
        if (increment < 1024)
            increment = 1024;
        static const uint32_t ratios[12] = {65536, 69433, 73562,  77936,  82570,  87480,
                                            92682, 98199, 104032, 110218, 116773, 123715};
        int best = -72;
        uint32_t best_diff = 0xffffffffu;
        for (int semi = -72; semi <= 24; ++semi) {
            int oct = semi / 12, rem = semi % 12;
            if (rem < 0) {
                rem += 12;
                --oct;
            }
            uint32_t ratio = ratios[rem];
            if (oct >= 0)
                ratio <<= oct;
            else
                ratio >>= -oct;
            const uint32_t diff =
                increment > (int32_t)ratio ? (uint32_t)(increment - ratio) : (uint32_t)(ratio - increment);
            if (diff < best_diff) {
                best = semi;
                best_diff = diff;
            }
        }
        return best;
    }
    const int scaled = (int)value * 24;
    return (scaled + (scaled < 0 ? -16383 : 16383)) / 32767;
}
void pitch_text(int chain, int step, char *out, int capacity) {
    if (!out || capacity < 1)
        return;
    if (chain < 0 || chain >= NUM_CHAINS || step < 0 || step >= SEQ_STEPS ||
        g_score.sequence_pitch_slot[chain] < 0) {
        std::snprintf(out, (size_t)capacity, "--");
        return;
    }
    const q15 value = g_score.sequence_pitch[chain][step];
    const UnitKind kind = (UnitKind)g_score.sequence_pitch_kind[chain];
    if (sequence_note::absolute(kind, g_score.sequence_pitch_param[chain])) {
        sequence_note::name(out, size_t(capacity), sequence_note::midi(kind, value));
    } else {
        const int semi = pitch_display_semitones(chain, value);
        const bool reverse = value < 0 && (kind == UnitKind::Grain || kind == UnitKind::SamplePlayer);
        std::snprintf(out, (size_t)capacity, "%s%+d", reverse ? "r" : "", semi);
    }
}
const char *source_name(int chain) {
    switch ((UnitKind)g_score.sequence_source_kind[chain]) {
    case UnitKind::Inst:
        return "inst";
    case UnitKind::Bird:
        return "bird";
    case UnitKind::Osc:
        return "osc";
    case UnitKind::Fm:
        return "fm";
    case UnitKind::SamplePlayer:
        return "play";
    case UnitKind::Grain:
        return "gran";
    case UnitKind::Noise:
        return "noise";
    case UnitKind::Motor:
        return "motor";
    case UnitKind::Stranular:
        return m::module_name(UnitKind::Stranular);
    case UnitKind::MicIn:
        return "mic";
    case UnitKind::Empty:
        return "chain";
    default: // newer sources (pent, rule, rot, scrt, paul) by their picker names
        return m::module_name((UnitKind)g_score.sequence_source_kind[chain]);
    }
}
int value_percent(int chain, int step) {
    int value = g_score.sequence_value[chain][step];
    if (value < 0)
        value = -value;
    if (value > 32767)
        value = 32767;
    return (value * 100 + 16383) / 32767;
}

#include "sequencer_step_edit.inc"

bool set_lock_value(int slot, int param, q15 value) {
    if (slot < 0 || slot >= UNITS_PER_CHAIN || param < 0)
        return false;
    platform::AudioOut::Guard guard(*g_audio);
    Unit *unit = g_eng->chains[g_seq_chain].units[slot];
    if (!unit || param >= unit->num_params)
        return false;
    SeqStep &step = edit_pattern().steps[g_seq_chain][g_seq_col];
    const int count = step.lock_count > SEQ_LOCKS ? SEQ_LOCKS : step.lock_count;
    int found = -1;
    for (int i = 0; i < count; ++i)
        if (step.locks[i].slot == slot && step.locks[i].param == param) {
            found = i;
            break;
        }
    int next = value;
    if (!unit->params[param].bipolar && next < 0)
        next = 0;
    if (next < -32767)
        next = -32767;
    if (next > 32767)
        next = 32767;
    // A stationary stylus must not keep retriggering or dirtying the project.
    if (found >= 0 && step.locks[found].value == next)
        return false;
    if (found >= 0)
        step.locks[found].value = (q15)next;
    else {
        if (count >= SEQ_LOCKS) {
            g_seq_lock_flash = 90;
            return false;
        }
        step.locks[count] = {(uint8_t)slot, (uint8_t)param, (q15)next};
        step.lock_count = (uint8_t)(count + 1);
    }
    mark_project_mutated();
    note_value_changed();
    if (g_seq_mode == SeqMode::Note || g_seq_mode == SeqMode::Slice)
        g_eng->audition_sequence_step(g_seq_chain, g_seq_col, g_seq_bank);
    return true;
}
q15 edit_value(int slot, int param) {
    platform::AudioOut::Guard guard(*g_audio);
    if (slot < 0 || slot >= UNITS_PER_CHAIN || param < 0)
        return 0;
    const Unit* unit = g_eng->chains[g_seq_chain].units[slot];
    if (!unit || param >= unit->num_params)
        return 0;
    const SeqStep& step = edit_pattern().steps[g_seq_chain][g_seq_col];
    for (int i = 0; i < step.lock_count && i < SEQ_LOCKS; ++i)
        if (step.locks[i].slot == slot && step.locks[i].param == param)
            return step.locks[i].value;
    return unit->params[param].base;
}
// Call only under Guard: find the actual last overwrite-source, not a stale
// worker pitch slot, and never edit a PLAY that a later source masks.
int slice_source_slot(int chain) {
    int slot = -1;
    for (int s = 0; s < UNITS_PER_CHAIN; ++s) {
        const Unit* unit = g_eng->chains[chain].units[s];
        if (unit && !unit->bypass && unit->sequencer_properties().source) slot = s;
    }
    if (slot < 0) return -1;
    const Unit* unit = g_eng->chains[chain].units[slot];
    return unit->kind == UnitKind::SamplePlayer && unit->num_params > PlayUnit::P_SLICE ? slot : -1;
}
uint16_t slice_source_mask(int slot) {
    const auto& play = *static_cast<const PlayUnit*>(g_eng->chains[g_seq_chain].units[slot]);
    if (play.sample_slot < 0 || play.sample_slot >= trackr::synth::SAMPLE_BANK_SIZE) return 0;
    return seq_slice::valid_mask(trackr::synth::SampleBank::instance().slot(play.sample_slot));
}
bool cut_busy_locked() {
    if (g_eng->pending_rec_start || g_eng->pending_rec_stop || g_eng->next_recorder_deadline()!=UINT64_MAX) return true;
    for (int c=0;c<NUM_CHAINS;++c) for (int s=0;s<UNITS_PER_CHAIN;++s) {
        const Unit* u=g_eng->chains[c].units[s];
        if (u && u->kind==UnitKind::Recorder && static_cast<const RecUnit*>(u)->writer_open()) return true;
    }
    return false;
}
void cut_remember_locked(const PlayUnit& unit, int slot) {
    const auto& bank=trackr::synth::SampleBank::instance();
    const auto& sample=bank.slot(unit.sample_slot);
    g_cut_unit=&unit; g_cut_slot=slot; g_cut_sample=unit.sample_slot; g_cut_chain=g_seq_chain;
    g_cut_pcm=sample.data.data();g_cut_frames=sample.playable_frames();g_cut_generation=bank.generation(unit.sample_slot);
    std::memcpy(g_cut_chops,sample.chops,sizeof g_cut_chops);g_cut_reverse=sample.slice_reverse_mask;
    std::memcpy(g_cut_speeds,sample.slice_speed_q8,sizeof g_cut_speeds);
    g_cut_context=true;
}
PlayUnit* cut_source_locked(bool* unchanged=nullptr) {
    if(unchanged)*unchanged=false;
    const int slot=slice_source_slot(g_seq_chain);
    if(slot<0){g_eng->cancel_sequence_audition();g_cut_context=false;g_cut_add=false;g_cut_drag=-1;g_cut_confirm=0;g_cut_selected=0;return nullptr;}
    auto& unit=*static_cast<PlayUnit*>(g_eng->chains[g_seq_chain].units[slot]);
    if(unit.sample_slot<0 || unit.sample_slot>=trackr::synth::SAMPLE_BANK_SIZE){g_eng->cancel_sequence_audition();g_cut_context=false;g_cut_add=false;g_cut_drag=-1;g_cut_confirm=0;g_cut_selected=0;return nullptr;}
    const auto& bank=trackr::synth::SampleBank::instance(); const auto& sample=bank.slot(unit.sample_slot);
    const bool same=g_cut_context && g_cut_chain==g_seq_chain && g_cut_slot==slot && g_cut_unit==&unit &&
        g_cut_sample==unit.sample_slot && g_cut_pcm==sample.data.data() && g_cut_frames==sample.playable_frames() &&
        g_cut_generation==bank.generation(unit.sample_slot) && g_cut_reverse==sample.slice_reverse_mask &&
        std::memcmp(g_cut_chops,sample.chops,sizeof g_cut_chops)==0 &&
        std::memcmp(g_cut_speeds,sample.slice_speed_q8,sizeof g_cut_speeds)==0;
    if(!same){
        g_cut_add=false;g_cut_drag=-1;g_cut_confirm=0;
        g_cut_selected=seq_slice::decode(edit_value(slot,PlayUnit::P_SLICE));
        cut_remember_locked(unit,slot);
        g_eng->cancel_sequence_audition();
    }
    if(unchanged)*unchanged=same;
    return &unit;
}
void open_cuts() {
    platform::AudioOut::Guard guard(*g_audio);
    const int slot=slice_source_slot(g_seq_chain);
    if(slot>=0){g_eng->cancel_sequence_audition();play_editor_open_cuts(g_seq_chain,slot,true);}
}
void close_cuts() {
    g_cuts_open=false;g_cut_context=false;g_cut_drag=-1;g_cut_add=false;g_cut_confirm=0;
    platform::AudioOut::Guard guard(*g_audio);g_eng->cancel_sequence_audition();
}
uint32_t cut_end(const trackr::synth::Sample& sample,uint32_t begin) {
    uint32_t end=sample.playable_frames();
    for(uint32_t at:sample.chops)if(at>begin && at<end)end=at;
    return end;
}
void audition_cut_locked(PlayUnit& unit) {
    if(g_cut_selected<1 || g_cut_selected>16 ||
       !(seq_slice::valid_mask(trackr::synth::SampleBank::instance().slot(unit.sample_slot))&(1u<<(g_cut_selected-1))))return;
    // The existing engine owns duration/refusal/cancellation. Override only its
    // runtime audition layer, never sneak a temporary lock into the pattern/base.
    if(!g_eng->audition_sequence_step(g_seq_chain,g_seq_col,g_seq_bank))return;
    Param& p=unit.params[PlayUnit::P_SLICE];p.audition=seq_slice::encode(g_cut_selected);p.slew_ready=false;
    ParamInspector context;
    g_eng->capture_param_inspector(g_seq_chain,g_cut_slot,PlayUnit::P_SLICE,context);
    p.resolve(context.source_value);unit.control_tick();unit.trigger();g_cut_audition_frame=g_ui_frame;
}
void cuts_changed_locked(PlayUnit& unit, bool dirty=true) {
    cut_remember_locked(unit,g_cut_slot);if(dirty)mark_project_mutated();
    g_eng->cancel_sequence_audition();
    unit.control_tick();
}
void cut_action(int action) {
    platform::AudioOut::Guard guard(*g_audio);
    PlayUnit* unit=cut_source_locked();if(!unit||cut_busy_locked())return;
    auto& sample=trackr::synth::SampleBank::instance().slot(unit->sample_slot);
    const uint32_t frames=sample.playable_frames();
    if((sample.channels!=1&&sample.channels!=2)||frames<2)return;
    if(action==1){g_cut_add=!g_cut_add;g_cut_confirm=0;return;}
    g_cut_add=false;
    if(action==4||action==8||action==16||action==255){
        bool exists=sample.slice_reverse_mask!=0;
        for(uint32_t at:sample.chops)exists=exists||at!=UINT32_MAX;
        if(action!=255 && frames<(uint32_t)action*2)return;
        if(exists && g_cut_confirm!=action){g_cut_confirm=action;return;}
        g_cut_confirm=0;
        uint32_t next[16];bool changed=sample.slice_reverse_mask!=0;
        for(int i=0;i<16;++i){next[i]=action!=255&&i<action?(uint32_t)((uint64_t)i*frames/action):UINT32_MAX;changed=changed||sample.chops[i]!=next[i]||sample.slice_speed_q8[i]!=256;}
        if(!changed)return;
        std::memcpy(sample.chops,next,sizeof next);sample.slice_reverse_mask=0;sample.reset_cut_speeds();g_cut_selected=action==255?0:1;
        cuts_changed_locked(*unit);return;
    }
    g_cut_confirm=0;
    const int id=g_cut_selected-1;if(id<0||id>=16)return;
    const uint32_t begin=sample.chops[id];if(begin==UINT32_MAX||begin>=frames)return;
    if(action==3){
        if(!(seq_slice::valid_mask(sample)&(1u<<id)))return;
        sample.slice_reverse_mask^=(uint16_t)(1u<<id);cuts_changed_locked(*unit);audition_cut_locked(*unit);return;
    }
    if(action==2){
        int left=-1;for(int i=0;i<16;++i)if(sample.chops[i]<begin&&(left<0||sample.chops[i]>sample.chops[left]))left=i;
        if(left<0)return; // The earliest cut stays anchored; merging absorbs right into left.
        sample.chops[id]=UINT32_MAX;sample.slice_speed_q8[id]=256;sample.slice_reverse_mask&=(uint16_t)~(1u<<id);g_cut_selected=left+1;
        cuts_changed_locked(*unit);return;
    }
}
void cut_wave_touch(int tx,int ty,bool dragging) {
    platform::AudioOut::Guard guard(*g_audio);
    bool same=false;PlayUnit* unit=cut_source_locked(&same);if(!unit||cut_busy_locked())return;
    auto& sample=trackr::synth::SampleBank::instance().slot(unit->sample_slot);
    const uint32_t frames=sample.playable_frames();if(frames<2||(sample.channels!=1&&sample.channels!=2))return;
    if(tx<seq_cuts::X0)tx=seq_cuts::X0;
    if(tx>seq_cuts::X1)tx=seq_cuts::X1;
    uint32_t at=(uint32_t)((uint64_t)(tx-seq_cuts::X0)*frames/(seq_cuts::X1-seq_cuts::X0));
    if(at>frames-2)at=frames-2;
    if(dragging){
        if(!same||g_cut_drag<0)return;
        const int id=g_cut_drag;const uint32_t original=sample.chops[id];
        uint32_t lower=0,upper=frames-2;bool has_left=false;
        for(int i=0;i<16;++i)if(i!=id&&sample.chops[i]!=UINT32_MAX){
            const uint32_t p=sample.chops[i];
            if(p<original){has_left=true;if(p+2>lower)lower=p+2;}
            if(p>original && p>=2 && p-2<upper)upper=p-2;
        }
        if(!has_left||upper<lower)return;
        int64_t requested=(int64_t)g_cut_drag_origin+(int64_t)(tx-g_cut_drag_x)*frames/(seq_cuts::X1-seq_cuts::X0);
        if(tx==seq_cuts::X0)requested=lower;
        if(tx==seq_cuts::X1)requested=upper;
        at=(uint32_t)(requested<(int64_t)lower?lower:requested>(int64_t)upper?upper:requested);
        if(at==original)return;
        sample.chops[id]=at;cuts_changed_locked(*unit,!g_cut_drag_dirty);g_cut_drag_dirty=true;
        if(g_ui_frame-g_cut_audition_frame>=3)audition_cut_locked(*unit);
        return;
    }
    g_cut_confirm=0;
    if(g_cut_add){
        g_cut_add=false;
        int first=-1,second=-1,count=0;
        for(int i=0;i<16;++i){
            const uint32_t p=sample.chops[i];
            if(p==UINT32_MAX){if(first<0)first=i;else if(second<0)second=i;continue;}
            ++count;
            if(p==at){g_cut_selected=i+1;return;}
            const uint32_t gap=p>at?p-at:at-p;if(gap<2)return;
        }
        if(first<0){g_seq_lock_flash=90;return;}
        if(count==0&&at>0){
            if(at<2||second<0)return;
            sample.chops[first]=0;sample.slice_speed_q8[first]=256;sample.slice_reverse_mask&=(uint16_t)~(1u<<first);first=second;
        }
        sample.chops[first]=at;sample.slice_speed_q8[first]=256;sample.slice_reverse_mask&=(uint16_t)~(1u<<first);g_cut_selected=first+1;
        cuts_changed_locked(*unit);return;
    }
    int nearest=-1,distance=8;
    if(ty<125)for(int i=0;i<16;++i){
        const uint32_t p=sample.chops[i];if(p==UINT32_MAX||p>=frames)continue;
        const int x=seq_cuts::X0+(int)((uint64_t)p*(seq_cuts::X1-seq_cuts::X0)/frames);
        int d=tx-x;if(d<0)d=-d;if(d<distance){nearest=i;distance=d;}
    }
    int selected=nearest;
    if(selected<0)for(int i=0;i<16;++i){const uint32_t p=sample.chops[i];if(p<=at&&(selected<0||p>sample.chops[selected]))selected=i;}
    if(selected<0)return;
    g_cut_selected=selected+1;g_cut_drag=nearest;
    g_cut_drag_x=tx;g_cut_drag_origin=sample.chops[selected];g_cut_drag_dirty=false;
    audition_cut_locked(*unit);
}
void cuts_touch(int tx,int ty) {
    if(hit(tx,ty,260,0,60,25)){close_cuts();return;}
    if(hit(tx,ty,16,174,92,24)){cut_action(1);return;}
    if(hit(tx,ty,112,174,90,24)){cut_action(2);return;}
    if(hit(tx,ty,206,174,98,24)){cut_action(3);return;}
    if(hit(tx,ty,16,200,62,24)){cut_action(4);return;}
    if(hit(tx,ty,82,200,62,24)){cut_action(8);return;}
    if(hit(tx,ty,148,200,62,24)){cut_action(16);return;}
    if(hit(tx,ty,214,200,90,24)){cut_action(255);return;}
    if(hit(tx,ty,seq_cuts::X0,38,seq_cuts::X1-seq_cuts::X0+1,110))cut_wave_touch(tx,ty,false);
    else {g_cut_confirm=0;g_cut_add=false;}
}

// No-op at a boundary must not create a redundant lock, consume capacity or hear
// again. Compare discrete meaning even for noncanonical legacy raw q15 locks.
bool write_slice(int slot, int next) {
    if (seq_slice::decode(edit_value(slot, PlayUnit::P_SLICE)) == next) return false;
    return set_lock_value(slot, PlayUnit::P_SLICE, seq_slice::encode(next));
}
int nudged_slice(uint16_t mask, int current, int delta) {
    int next = current;
    if (delta > 0) {
        for (int i = current + 1; i <= 16; ++i)
            if (mask & (1u << (i - 1))) { next = i; break; }
    } else if (delta < 0) {
        next = 0;
        for (int i = current - 1; i >= 1; --i)
            if (mask & (1u << (i - 1))) { next = i; break; }
    }
    return next;
}
void set_slice_delta(int delta) {
    platform::AudioOut::Guard guard(*g_audio);
    const int slot = slice_source_slot(g_seq_chain);
    if (slot < 0) return;
    const uint16_t mask = slice_source_mask(slot);
    if (!mask) return;
    write_slice(slot, nudged_slice(mask, seq_slice::decode(edit_value(slot, PlayUnit::P_SLICE)), delta));
}
void set_slice_absolute(int tx) {
    platform::AudioOut::Guard guard(*g_audio);
    const int slot = slice_source_slot(g_seq_chain);
    if (slot < 0) return;
    const uint16_t mask = slice_source_mask(slot);
    const int count = seq_slice::count(mask);
    if (!count) return;
    if (tx < 102) tx = 102;
    if (tx > 267) tx = 267;
    const int ordinal = 1 + (tx - 102) * count / 166;
    write_slice(slot, seq_slice::at(mask, ordinal));
}
void set_slice_window() {
    platform::AudioOut::Guard guard(*g_audio);
    const int slot = slice_source_slot(g_seq_chain);
    if (slot >= 0 && slice_source_mask(slot)) write_slice(slot, 0);
}
void remove_slice_lock() {
    platform::AudioOut::Guard guard(*g_audio);
    const int slot = slice_source_slot(g_seq_chain);
    if (slot >= 0 && g_eng->remove_step_lock(g_seq_chain, g_seq_col, slot, PlayUnit::P_SLICE, g_seq_bank)) {
        note_value_changed();
        g_eng->cancel_sequence_audition();
        mark_project_mutated();
    }
}
// Compute without writing: single and group buttons share the musical mapping.
q15 nudged_pitch(q15 current, int direction) {
    const int param = g_score.sequence_pitch_param[g_seq_chain];
    if (!direction) return current;
    const UnitKind kind = (UnitKind)g_score.sequence_pitch_kind[g_seq_chain];
    int next = current;
    if (sequence_note::absolute(kind, param)) {
        const int from = sequence_note::midi(kind, current);
        const int low=sequence_note::minimum(kind),high=sequence_note::maximum(kind);
        const int to = musical_scale::active(g_score.project_scale)
            ? (from<low||from>high
                ? musical_scale::snap_note(g_score.project_scale,std::max(low,std::min(high,from)),low,high)
                : musical_scale::next_note(g_score.project_scale,from,direction,low,high))
            : std::max(low,std::min(high,from+direction));
        if (to == from) return current;
        return sequence_note::value(kind, to, current);
    }
    if (kind == UnitKind::Stranular || kind == UnitKind::Scratch || kind == UnitKind::Paul) {
        int semi = pitch_display_semitones(g_seq_chain, current) + direction;
        if (semi < -24)
            semi = -24;
        if (semi > 24)
            semi = 24;
        if (semi == pitch_display_semitones(g_seq_chain, current)) return current;
        next = semi * 32767 / 24;
    } else {
        int semi = pitch_display_semitones(g_seq_chain, current) + direction;
        if (semi < -72)
            semi = -72;
        if (semi > 24)
            semi = 24;
        if (semi == pitch_display_semitones(g_seq_chain, current)) return current;
        static const uint32_t ratios[12] = {65536, 69433, 73562,  77936,  82570,  87480,
                                            92682, 98199, 104032, 110218, 116773, 123715};
        int oct = semi / 12, rem = semi % 12;
        if (rem < 0) {
            rem += 12;
            --oct;
        }
        uint32_t ratio = ratios[rem];
        if (oct < 0)
            ratio >>= -oct;
        else
            ratio <<= oct;
        next = (int)(ratio / 8u);
        if (current < 0)
            next = -next;
    }
    if (next < -32767)
        next = -32767;
    if (next > 32767)
        next = 32767;
    return (q15)next;
}
void set_pitch_absolute(int tx) {
    platform::AudioOut::Guard guard(*g_audio);
    const auto signature=g_edit_signature;
    capture_score_locked();sync_edit_context();
    if(signature!=g_edit_signature)return; // a held stroke cannot retarget a replaced source
    const int slot = g_score.sequence_pitch_slot[g_seq_chain];
    const int param = g_score.sequence_pitch_param[g_seq_chain];
    if (slot < 0 || param < 0)
        return;
    if (tx < VALUE_X0)
        tx = VALUE_X0;
    if (tx > VALUE_X1)
        tx = VALUE_X1;
    const UnitKind pitch_kind = (UnitKind)g_score.sequence_pitch_kind[g_seq_chain];
    if (sequence_note::absolute(pitch_kind, param)) {
        const int low=sequence_note::minimum(pitch_kind), high=sequence_note::maximum(pitch_kind);
        int note=low+((tx-VALUE_X0)*(high-low)+VALUE_TRAVEL/2)/VALUE_TRAVEL;
        if(musical_scale::active(g_score.project_scale))
            note=musical_scale::snap_note(g_score.project_scale,note,low,high);
        set_lock_value(slot,param,sequence_note::value(pitch_kind,note,edit_value(slot,param)));
        return;
    }
    const bool bipolar = pitch_kind == UnitKind::SamplePlayer || pitch_kind == UnitKind::Grain ||
                         pitch_kind == UnitKind::Stranular || pitch_kind == UnitKind::Scratch || pitch_kind == UnitKind::Paul;
    int value = bipolar ? -32767 + (tx - VALUE_X0) * 65534 / VALUE_TRAVEL : (tx - VALUE_X0) * 32767 / VALUE_TRAVEL;
    if (pitch_kind == UnitKind::Osc || pitch_kind == UnitKind::Fm) {
        const int semitone = ((tx - VALUE_X0) * 72 + VALUE_TRAVEL / 2) / VALUE_TRAVEL;
        value = semitone * 32767 / 72;
    } else if (pitch_kind == UnitKind::Stranular || pitch_kind == UnitKind::Scratch || pitch_kind == UnitKind::Paul) {
        const int semitone = -24 + ((tx - VALUE_X0) * 48 + VALUE_TRAVEL / 2) / VALUE_TRAVEL;
        value = semitone * 32767 / 24;
    }
    set_lock_value(slot, param, (q15)value);
}
void set_level_absolute(int tx) {
    if (tx < VALUE_X0)
        tx = VALUE_X0;
    if (tx > VALUE_X1)
        tx = VALUE_X1;
    set_lock_value(g_score.sequence_value_slot[g_seq_chain],
                   g_score.sequence_value_param[g_seq_chain], (q15)((tx - VALUE_X0) * 32767 / VALUE_TRAVEL));
}
void toggle_trig() {
    platform::AudioOut::Guard guard(*g_audio);
    SeqStep &step = edit_pattern().steps[g_seq_chain][g_seq_col];
    step.flags ^= SeqStep::Trig;
    mark_project_mutated();
    note_value_changed();
}
void set_retrig(int count) {
    if (count < 1)
        count = 1;
    if (count > 4)
        count = 4;
    platform::AudioOut::Guard guard(*g_audio);
    auto& step = edit_pattern().steps[g_seq_chain][g_seq_col];
    if (step.retrig_count() != count) note_value_changed();
    step.set_retrig_count((uint8_t)count);
    mark_project_mutated();
}
void set_condition(SeqStep::Condition condition) {
    platform::AudioOut::Guard guard(*g_audio);
    auto& step = edit_pattern().steps[g_seq_chain][g_seq_col];
    if (step.condition() != condition) note_value_changed();
    step.set_condition(condition);
    mark_project_mutated();
}
void set_timing(int timing) {
    if (timing < -SEQ_MICROTIMING_MAX)
        timing = -SEQ_MICROTIMING_MAX;
    if (timing > SEQ_MICROTIMING_MAX)
        timing = SEQ_MICROTIMING_MAX;
    platform::AudioOut::Guard guard(*g_audio);
    if (edit_pattern().microtiming[g_seq_chain][g_seq_col] != timing) note_value_changed();
    edit_pattern().microtiming[g_seq_chain][g_seq_col] = (int8_t)timing;
    mark_project_mutated();
}
bool valid_lock_target(int slot, int param) {
    if (slot < 0 || slot >= UNITS_PER_CHAIN || param < 0)
        return false;
    const Unit *unit = g_eng->chains[g_seq_chain].units[slot];
    return unit && param < unit->num_params;
}
void normalize_lock_target() {
    if (valid_lock_target(g_lock_slot, g_lock_param))
        return;
    g_lock_slot = g_lock_param = 0;
    while (g_lock_slot < UNITS_PER_CHAIN - 1 && !valid_lock_target(g_lock_slot, 0))
        ++g_lock_slot;
}
void browse_lock_param(int delta);
void cycle_lock_target() { browse_lock_param(1); }
void browse_lock_module(int delta) {
    platform::AudioOut::Guard guard(*g_audio);
    normalize_lock_target();
    for (int i = 0; i < UNITS_PER_CHAIN; ++i) {
        g_lock_slot = (g_lock_slot + UNITS_PER_CHAIN + delta) % UNITS_PER_CHAIN;
        if (valid_lock_target(g_lock_slot, 0)) {
            g_lock_param=g_param_memory[g_seq_chain][g_lock_slot];
            if(!valid_lock_target(g_lock_slot,g_lock_param)) g_lock_param=0;
            g_offset=false; g_edit_status[0]=0; return;
        }
    }
}
void browse_lock_param(int delta) {
    platform::AudioOut::Guard guard(*g_audio);
    normalize_lock_target();
    if (!valid_lock_target(g_lock_slot, g_lock_param)) return;
    const int count = g_eng->chains[g_seq_chain].units[g_lock_slot]->num_params;
    g_lock_param = (g_lock_param + count + delta) % count;
    g_param_memory[g_seq_chain][g_lock_slot]=g_lock_param;
    g_offset=false; g_edit_status[0]=0;
}
void remove_selected_lock() { edit_group_lock(0,false,true); finish_edit(); }
// Bare X in the LOCK layer: only the selected target's lock, on the focused step
// or on every step of the group; one undo entry. Nothing to remove writes nothing.
void remove_target_lock() {
    bool any = false;
    {
        platform::AudioOut::Guard guard(*g_audio);
        sync_edit_context();
        normalize_lock_target();
        if (valid_lock_target(g_lock_slot, g_lock_param)) {
            const auto targets = g_selection.targets(g_seq_col);
            for (int s = 0; s < SEQ_STEPS && !any; ++s)
                any = (targets & seq_edit::bit(s)) &&
                      seq_edit::find(edit_pattern().steps[g_seq_chain][s], g_lock_slot, g_lock_param) >= 0;
        }
    }
    if (!any) { edit_status("no lock to remove"); return; }
    remove_selected_lock();
}
void set_lock_absolute(int tx) {
    platform::AudioOut::Guard guard(*g_audio);
    normalize_lock_target();
    if (tx < 160)
        tx = 160;
    if (tx > 315)
        tx = 315;
    Unit *unit = g_eng->chains[g_seq_chain].units[g_lock_slot];
    if (!unit || g_lock_param >= unit->num_params) {
        cycle_lock_target();
        return;
    }
    const bool bipolar=unit->params[g_lock_param].bipolar;
    const int value=g_offset ? (tx-g_drag_origin_x)*32767/155 :
        bipolar ? -32767+(tx-160)*65534/155 : (tx-160)*32767/155;
    edit_group_lock(value);
}
#include "sequencer_value_buttons.inc"
// Sequencer drawing uses the post-splash menu palette locally.
// Inset score hits match the badges; held B owns bank/length/direction.
inline u32 seq_ink() { return m::ink(); }
inline u32 seq_spot() { return m::spot(); }
inline u32 seq_ghost() { return m::ghost(); }
inline u32 seq_branch() { return m::branch(); }
// Inactive / empty score marks: steps beyond the track length, the empty part of
// the level meter, empty lock slots. Graphite branch (255) is brighter than ink
// (244), so there they take the dark subdued role. Pink keeps branch: its
// subdued sits almost on the pink paper and would read worse.
inline u32 seq_inactive() { return palette::visual_graphite() ? palette::subdued() : seq_branch(); }

// Only score foreground uses this sink. The ordinary pass still calls the real
// UI primitives; the small second pass restores covered native ink, not a bitmap.
namespace score_ink {
void rect(int x, int y, int w, int h, u32 color) {
    if (!g_focus_inverse) ui::rect(x, y, w, h, color);
    else ui::clipped_rect(g_focus_box, {x, y, w, h}, col_text_i());
}
void text(int x, int y, const char* label, u32 color) {
    if (!g_focus_inverse) ui::text(x, y, label, color);
    else ui::clipped_text(g_focus_box, x, y, label, col_text_i());
}
void line(int x0, int y0, int x1, int y1, u32 color, int thickness) {
    if (!g_focus_inverse) ui::line(x0, y0, x1, y1, color, thickness);
    else ui::clipped_line(g_focus_box, x0, y0, x1, y1, col_text_i(), thickness);
}
void frame(int x, int y, int w, int h, u32 color) {
    if (!g_focus_inverse) { ui::frame(x, y, w, h, color); return; }
    ui::pixel::frame({x, y, w, h}, [color](ui::pixel::Box b) {
        rect(b.x, b.y, b.w, b.h, color);
    });
}
void point(int x, int y, int r, u32 color) {
    if (!g_focus_inverse) { bead(x, y, r, color, true); return; }
    // Same fixed integer circle spans as core/ui/draw.cpp.
    for (int dy = -r; dy <= r; ++dy) {
        int dx = 0;
        while ((dx + 1) * (dx + 1) + dy * dy <= r * r) ++dx;
        if (dx) rect(x - dx, y + dy, dx * 2 + 1, 1, color);
    }
}
} // namespace score_ink

// Shared cell/selector glyph: 2x2 square pips, compact 2/4, original diagonal 3.
// cx/cy are the integer anchors; the even-sized ink centers at cx-.5/cy-.5.
void draw_retrig_dots(int cx, int cy, int count, u32 color) {
    static const int8_t xy[4][8] = {
        {0, 0}, {-3, -3, 3, 3}, {-4, -4, 0, 0, 4, 4}, {-3, -3, 3, -3, -3, 3, 3, 3}};
    for (int i = 0; i < count; ++i)
        score_ink::rect(cx - 1 + xy[count - 1][i * 2], cy - 1 + xy[count - 1][i * 2 + 1], 2, 2, color);
}
// Center actual native ink, including narrow digits and multi-character signs.
void cell_label(int x, int y, const char *text, u32 color, int height=BADGE_H) {
    namespace ui = score_ink;
    int left=999,right=-1,top=8,bottom=-1;
    for(int i=0;text[i];++i)for(int yy=0;yy<8;++yy) {
        const auto bits=trackr::ui::glyph_row(text[i],yy);
        for(int xx=0;xx<6;++xx)if(bits&(1u<<(5-xx))) {
            const int at=i*6+xx;
            if(at<left)left=at;
            if(at>right)right=at;
            if(yy<top)top=yy;
            if(yy>bottom)bottom=yy;
        }
    }
    if(right<left || right-left+1>CELL_W)return;
    ui::text(x+(CELL_W-(right-left+1))/2-left,
             y+(height-(bottom-top+1))/2-top,text,color);
}
float playback_power(int chain, int step) {
    if (!g_feedback_visible || !g_score.transport_playing || !g_score.sequencer_enabled || flow_chain(chain)) return 0;
    const auto& hit = g_score.sequence_hit[chain];
    if (hit.bank != g_seq_bank || hit.step != step || step >= g_score.sequence_length[chain]) return 0;
    return step_feedback::hit_power(hit.age_ms, hit.duration_ms);
}
float edit_power(int chain, int step) {
    return g_feedback_visible ? g_step_press.power(g_feedback_now, chain, step) : 0;
}
u32 feedback_color(u32 base, float hit, float press) {
    return step_feedback::tint(step_feedback::tint(base, palette::playhead(), hit), seq_ink(), press * .35f);
}
void draw_cell_backing(int chain, int step, int x, int y) {
    const float hit = playback_power(chain, step), press = edit_power(chain, step);
    ui::fill(step_feedback::face({x, y, CELL_W, BADGE_H}, hit, press), feedback_color(col_cell(), hit, press));
}
void draw_event_content(int chain, int step, int x, int y, u32 color, u32 detail) {
    namespace ui = score_ink;
    char label[12] = {};
    const uint16_t meta = g_score.sequence_meta[chain][step];
    switch (g_seq_mode) {
    case SeqMode::Trig:
        ui::rect(x + (CELL_W-7)/2, y + (BADGE_H-7)/2, 7, 7, color);
        break;
    case SeqMode::Note: {
        pitch_text(chain, step, label, sizeof label);
        const UnitKind kind = (UnitKind)g_score.sequence_pitch_kind[chain];
        if (g_score.sequence_pitch_slot[chain] < 0) {
            cell_label(x, y, "--", color);
        } else if (sequence_note::absolute(kind, g_score.sequence_pitch_param[chain])) {
            const int split = label[1] == '#' ? 2 : 1;
            const char note[3] = {label[0], split == 2 ? '#' : '\0', '\0'};
            cell_label(x, y, note, color);
            cell_label(x, y + 23, label + split, detail, 8);
        } else {
            const int semi = pitch_display_semitones(chain, g_score.sequence_pitch[chain][step]);
            const bool reverse = g_score.sequence_pitch[chain][step] < 0 &&
                                 (kind == UnitKind::Grain || kind == UnitKind::SamplePlayer);
            const char direction[3] = {reverse ? 'r' : (semi < 0 ? '-' : '+'),
                                      reverse ? (semi < 0 ? '-' : '+') : '\0', '\0'};
            cell_label(x, y, direction, color);
            std::snprintf(label, sizeof label, "%d", semi < 0 ? -semi : semi);
            cell_label(x, y + 23, label, detail, 8);
        }
        break;
    }
    case SeqMode::Level: {
        const int percent = value_percent(chain, step);
        std::snprintf(label, sizeof label, "%02d", percent);
        cell_label(x, y, label, color);
        ui::rect(x + 2, y + 21, 13, 4, seq_inactive());
        const int width = percent * 13 / 100;
        if (width) ui::rect(x + 2, y + 21, width, 4, detail);
        break;
    }
    case SeqMode::Retrig:
        // 18x18 badge => actual ink center 8.5,8.5, including selected cells.
        draw_retrig_dots(x + CELL_W / 2, y + BADGE_H / 2, meta_retrig(meta), color);
        break;
    case SeqMode::When: {
        const auto condition = meta_condition(meta);
        if (condition == SeqStep::Condition::Always || condition == SeqStep::Condition::FirstOfTwo ||
            condition == SeqStep::Condition::SecondOfTwo) {
            // Diagonal fraction: two ordinary 5x7 ink boxes, a separate slash.
            // Each has its own native pixels; never squeeze text into the badge.
            ui::text(x + 2, y + 1, condition == SeqStep::Condition::SecondOfTwo ? "2" : "1", color);
            ui::line(x + 6, y + 11, x + 10, y + 5, color, 1);
            ui::text(x + 10, y + 9, condition == SeqStep::Condition::Always ? "1" : "2", color);
        } else if (condition == SeqStep::Condition::Fill || condition == SeqStep::Condition::NotFill) {
            cell_label(x, y, condition == SeqStep::Condition::NotFill ? "!f" : "f", color);
        } else {
            cell_label(x, y, condition_name(condition), color);
            cell_label(x, y + 20, "%", detail, 9);
        }
        break;
    }
    case SeqMode::Time: {
        const int timing = meta_timing(meta);
        cell_label(x, y + 1, timing < 0 ? "-" : timing > 0 ? "+" : "=", color, 8);
        std::snprintf(label, sizeof label, "%02d", timing < 0 ? -timing : timing);
        cell_label(x, y + 9, label, color, 8);
        ui::rect(x + 2, y + 24, 13, 1, seq_ghost());
        ui::rect(x + 8, y + 22, 1, 5, seq_ghost());
        const int offset = timing * 6 / SEQ_MICROTIMING_MAX;
        ui::rect(x + 7 + offset, y + 23, 3, 3, detail);
        break;
    }
    case SeqMode::Lock:
        std::snprintf(label, sizeof label, "%d", meta_locks(meta));
        cell_label(x, y, label, color);
        for (int i = 0; i < SEQ_LOCKS; ++i)
            ui::rect(x + 2 + i * 3, y + 21, 2, 5, i < meta_locks(meta) ? detail : seq_inactive());
        break;
    default:
        break;
    }
}
void draw_event_cell(int chain, int step, int x, int y, bool selected) {
    namespace ui = score_ink;
    const uint16_t meta = g_score.sequence_meta[chain][step];
    if (g_seq_mode == SeqMode::Slice) {
        const auto& row = g_slices.rows[chain];
        const auto& cell = row.cells[step % PAGE_STEPS];
        const bool available = row.slot >= 0 && row.loaded && row.valid;
        const bool valid = cell.slice == 0 || (row.valid & (1u << (cell.slice - 1)));
        const bool active = step < g_score.sequence_length[chain];
        const u32 color = !active || !available ? seq_inactive() : cell.locked ? seq_ink() : seq_ghost();
        if (!g_focus_inverse && (selected || meta_trig(meta) || cell.locked || playback_power(chain, step) > 0 || edit_power(chain, step) > 0))
            draw_cell_backing(chain, step, x, y);
        char label[4];
        if (!available || !cell.slice) std::snprintf(label, sizeof label, "--");
        else if (!valid) std::snprintf(label, sizeof label, "??");
        else std::snprintf(label, sizeof label, "%02u", cell.slice);
        const u32 face = color;
        cell_label(x, y, label, face);
        if (cell.locked) ui::rect(x + 11, y + 2, 3, 1, face);
        // Tiny attack below the number: a slice lock is not itself a trigger.
        if (meta_trig(meta)) ui::rect(x + 7, y + 21, 3, 2, active ? seq_ink() : seq_inactive());
        return;
    }
    const bool automation = flow_chain(chain) &&
                            ((g_seq_mode == SeqMode::Note && g_score.sequence_pitch_slot[chain] >= 0) ||
                             (g_seq_mode == SeqMode::Level && g_score.sequence_value_slot[chain] >= 0));
    const bool mode_data = (g_seq_mode == SeqMode::Retrig && meta_retrig(meta) > 1) ||
                           (g_seq_mode == SeqMode::When && meta_condition(meta) != SeqStep::Condition::Always) ||
                           (g_seq_mode == SeqMode::Time && meta_timing(meta) != 0);
    const bool active = meta_trig(meta) || meta_locks(meta) || automation || mode_data ||
                        (selected && g_seq_mode != SeqMode::Trig);
    const bool in_length = step < g_score.sequence_length[chain];
    const u32 ink = !in_length ? seq_inactive() : seq_ink();
    if (active) {
        if (!g_focus_inverse) draw_cell_backing(chain, step, x, y);
        const u32 face = ink;
        if (g_seq_mode == SeqMode::Trig && !meta_trig(meta))
            ui::frame(x + (CELL_W-9)/2, y + (BADGE_H-9)/2, 9, 9, face); // lock-only event, not a trigger
        else
            draw_event_content(chain, step, x, y, face, ink);
        if (automation) {
            const uint64_t mask = g_seq_mode == SeqMode::Note ? g_score.sequence_pitch_lock_mask[chain]
                                                              : g_score.sequence_value_lock_mask[chain];
            // Tiny written-value mark; base values remain unmarked. This does
            // not steal the label/octave pixels or the original touch band.
            if (mask & (uint64_t(1) << step)) ui::rect(x + 11, y + 2, 3, 1, face);
        }
    } else {
        if (!g_focus_inverse && (playback_power(chain, step) > 0 || edit_power(chain, step) > 0))
            draw_cell_backing(chain, step, x, y);
        ui::rect(x + 7, y + 7, 3, 3, in_length ? seq_ghost() : seq_inactive());
    }
    if (g_seq_mode == SeqMode::Trig &&
        (SequenceLifecycle)g_score.sequence_lifecycle[chain] == SequenceLifecycle::Loop &&
        meta_trig(meta)) {
        // A local normalized LEN bracket, never a musical-time tail spilling
        // underneath the next trigger. Details belong to the top PLAY window.
        const int end = x + 4 + g_score.sequence_duration[chain][step] * 10 / 255;
        ui::line(x + 2, y + 23, end, y + 23, ink, 1);
        ui::line(x + 2, y + 21, x + 2, y + 25, ink, 1);
        ui::line(end, y + 21, end, y + 25, ink, 1);
    }
}
#include "sequencer_flow.inc"
void draw_playhead() {
    if (!g_score.sequencer_enabled || !g_score.transport_playing)
        return;
    const u32 lichen = palette::playhead();
    for (int chain = 0; chain < NUM_CHAINS; ++chain) {
        const int step = g_score.sequence_step[chain];
        if (step / PAGE_STEPS != g_seq_page)
            continue;
        // Leave one paper row after a group outline: the 2px transport marker
        // must not merge with its 1px bottom edge into an apparent 3px border.
        const int x = pattern_x(step % PAGE_STEPS), y = PATTERN_ROWS[chain] + CELL_H + 1;
        ui::rect(x + 3, y, CELL_W - 6, 2, lichen);
    }
}

void draw_cuts() {
    const auto& c=g_cuts;const auto& d=g_slices;
    ui::paper(320);
    char text[64];
    ui::text(20,9,"slice / cuts",seq_spot());ui::text(274,9,"done",seq_spot());
    std::snprintf(text,sizeof text,"chain %02d / s%02d",d.chain+1,c.sample);
    ui::text(138,9,text,seq_ghost());
    const bool loaded=d.loaded && d.chain>=0 && d.chain<NUM_CHAINS;
    if(!loaded){
        ui::text(24,70,c.slot<0?"no play source":"no sample",seq_ink(),2);
        ui::text(24,108,c.slot<0?"select a play chain":"load a sample in play",seq_ghost());
    }else{
        constexpr int left=seq_cuts::X0,right=seq_cuts::X1,center=84;
        auto x_at=[](uint16_t v){return seq_cuts::X0+(int)v*(seq_cuts::X1-seq_cuts::X0)/32767;};
        const auto& row=d.rows[d.chain];
        ui::rect(left,center,right-left+1,1,seq_branch());
        for(int x=left;x<=right;++x){
            const int at=(x-left)*(seq_slice::WAVE_COLUMNS-1)/(right-left);
            const int hi=center-(int)d.high[at]*30/128,lo=center-(int)d.low[at]*30/128;
            ui::rect(x,hi,1,lo-hi+1,d.applied && x>=x_at(d.window_begin)&&x<x_at(d.window_end)?seq_spot():seq_ink());
        }
        // Draw real seams over PCM so even a quiet / dense edge remains visible.
        for(int i=0;i<16;++i)if(c.present&(1u<<i)){
            const uint16_t boundary=(uint16_t)((uint64_t)c.chops[i]*32767/c.frames);
            const int x=x_at(boundary);
            ui::rect(x,48,1,72,seq_branch());ui::rect(x-1,46,3,2,seq_ghost());
        }
        int taken_left[16],taken_right[16],taken=0;
        for(int i=0;i<16;++i)if(row.valid&(1u<<i)){
            const int a=x_at(d.begin[i]),b=x_at(d.end[i]);const bool active=c.selected==i+1;
            ui::rect(a,123,b-a+1,1,active?seq_spot():seq_branch());
            const int x=(a+b)/2-6;bool fits=b-a>=15;
            for(int n=0;n<taken;++n)if(x<taken_right[n]+2&&x+12+2>taken_left[n])fits=false;
            if(fits){
                taken_left[taken]=x;taken_right[taken++]=x+12;
                std::snprintf(text,sizeof text,"%02d",i+1);ui::text(x,133,text,active?seq_spot():seq_ghost());
                if(d.reversed&(1u<<i))ui::rect(x+2,144,8,1,seq_spot());
            }
        }
        if(d.applied)slice_boundaries::selected(x_at(d.window_begin),x_at(d.window_end),48,119,seq_spot());
        std::snprintf(text,sizeof text,"wave x%u.%u / visual",d.visual_gain_q8/256,d.visual_gain_q8%256*10/256);
        ui::text(24,29,text,seq_ghost());
    }
    const bool valid=loaded && c.selected>0 && c.selected<=16 && (d.rows[d.chain].valid&(1u<<(c.selected-1)));
    if(c.busy)ui::text(24,156,"recording / cuts protected",seq_spot());
    else if(c.confirm)ui::text(24,156,c.confirm==255?"tap clear again / all cuts":"tap auto again / replace cuts",seq_spot());
    else if(c.add_armed)ui::text(24,156,"tap wave / place a seam",seq_spot());
    else if(valid){
        const uint32_t a=(uint32_t)((uint64_t)c.selected_begin*1000/ENGINE_SR),b=(uint32_t)((uint64_t)c.selected_end*1000/ENGINE_SR);
        std::snprintf(text,sizeof text,"%02u / %lu.%03lu-%lu.%03lus / %s",c.selected,(unsigned long)(a/1000),(unsigned long)(a%1000),
            (unsigned long)(b/1000),(unsigned long)(b%1000),(d.reversed&(1u<<(c.selected-1)))?"rev":"fwd");
        ui::text(24,156,text,seq_ink());
    }else ui::text(24,156,"add a seam or cut evenly",seq_ghost());
    const u32 enabled=loaded&&!c.busy?seq_ink():seq_branch();
    ui::text(28,181,c.add_armed?"tap wave":"+ seam",c.add_armed?seq_spot():enabled);
    ui::text(132,181,"merge",valid?enabled:seq_branch());ui::text(226,181,"reverse",valid?enabled:seq_branch());
    ui::text(25,207,"auto 4",c.confirm==4?seq_spot():enabled);
    ui::text(91,207,"auto 8",c.confirm==8?seq_spot():enabled);
    ui::text(154,207,"auto 16",c.confirm==16?seq_spot():enabled);
    ui::text(238,207,"clear",c.confirm==255?seq_spot():enabled);
    ui::footer(320,"shared cuts / drag seam","b score");
}
void pad_lock_navigation() {
    circlePosition pos; hidCircleRead(&pos);
    const int ax=pos.dx<0?-pos.dx:pos.dx, ay=pos.dy<0?-pos.dy:pos.dy;
    if(ax<35 && ay<35) { g_pad_center=true; g_pad_direction=0; return; }
    if(!g_pad_center || g_seq_mode!=SeqMode::Lock || g_l_held) return;
    int direction=0;
    if(ax>60 || ay>60) direction=ax>=ay ? (pos.dx<0?1:2) : (pos.dy>0?3:4);
    if(!direction) return;
    const bool changed=direction!=g_pad_direction;
    if(changed) { g_pad_direction=direction; g_pad_start=g_ui_frame; }
    if(changed || (g_ui_frame-g_pad_start>=18 && (g_ui_frame-g_pad_start-18)%5==0)) {
        finish_edit();
        if(direction<=2) browse_lock_module(direction==1?-1:1);
        else browse_lock_param(direction==3?-1:1);
    }
}
bool tools_touch(int tx,int ty) {
    if(!(g_l_held||g_tools) || ty<194 || ty>=240) return false;
    finish_edit();
    if(ty<211) {
        if(tx<110) g_copy_scope=(seq_edit::Scope)(((int)g_copy_scope+1)%3);
        else if(tx<205) {
            if(g_seq_mode==SeqMode::Lock && g_inspector.slot>=0) {
                const auto& mod=g_inspector.modules[g_inspector.slot];
                if(seq_lock::supports_offset(mod.kind,mod.variant,g_inspector.param)) g_offset=!g_offset;
                else edit_status("set only: discrete parameter");
            }
        } else if(tx<258) history_step(true);
        else if(g_seq_mode==SeqMode::Lock) remove_selected_lock();
        else if(g_seq_mode==SeqMode::Slice) { begin_edit(); remove_slice_lock(); finish_edit(); }
    } else if(ty<229) {
        if(tx<80) copy_steps(); else if(tx<160) paste_steps();
        else if(tx<240) history_step(false); else { g_selection.clear(); g_tools=false; }
    } else if(tx>=240) {
        g_clipboard.clear(); edit_status("clipboard cleared");
    } else g_tools=false;
    return true;
}
void lock_address(char* out,size_t size) {
    const auto& d=g_inspector;
    if(d.slot<0) std::snprintf(out,size,"no parameter");
    else std::snprintf(out,size,"%02d %.6s / %.8s",d.slot+1,m::module_name(d.modules[d.slot].kind),d.modules[d.slot].params[d.param].name);
}
void draw_lock_editor() {
    const auto& d=g_inspector;
    char label[64];
    if(g_l_held||g_tools) {
        std::snprintf(label,sizeof label,"copy:%s",seq_edit::scope_name(g_copy_scope));
        ui::text(8,198,label,seq_spot());
        ui::text(116,198,g_offset?"offset":"set",seq_spot());
        ui::text(211,198,"redo",d.can_redo?seq_ink():seq_ghost());
        ui::text(265,198,"remove",seq_ink());
        ui::text(8,216,"x copy",seq_ink()); ui::text(84,216,"a paste",seq_ink());
        ui::text(166,216,"y undo",seq_ink()); ui::text(246,216,"b deselect",seq_ink());
        ui::footer(320,"l+^v group / <> range","clear clip");
        return;
    }
    lock_address(label,sizeof label);
    ui::text(267,198,"tools",seq_spot());
    // The address opens the module / parameter lists. Their frozen background
    // leaves out the address and its value row: the sharp address follows the
    // selection and no stale value stays blurred underneath.
    if(g_freeze_without_address) return;
    ui::text(8,198,label,seq_spot());
    if(d.slot>=0) {
        const auto& mod=d.modules[d.slot]; const auto& p=mod.params[d.param];
        seq_lock::format_value(mod.kind,mod.variant,d.param,d.rows[d.param].focused,label,sizeof label,g_score.project_scale);
        ui::text(8,216,label,seq_ink());
        if(seq_lock::supports_offset(mod.kind,mod.variant,d.param)) {
            const int normalized=p.bipolar?(d.rows[d.param].focused+32767)/2:d.rows[d.param].focused;
            const int x=160+normalized*155/32767;
            ui::horizontal(160,315,218,x,x,{},p.bipolar?237:-1);
        } else { ui::text(172,216,"<",seq_spot()); ui::text(296,216,">",seq_spot()); }
    }
    if(d.status[0]) ui::footer(320,d.status,"");
    else {
        std::snprintf(label,sizeof label,"%d / touch %s",d.selected_count,g_offset?"offset":"set");
        ui::footer(320,label,"a+ b- / x remove / y listen");
    }
}
void draw_editor() {
    if(g_seq_mode==SeqMode::Lock || g_l_held || g_tools) { draw_lock_editor(); return; }
    const uint16_t meta = g_score.sequence_meta[g_seq_chain][g_seq_col];
    const bool flow = flow_row();
    ui::text(8, 199, flow ? "flow" : mode_name(g_seq_mode), seq_spot());
    char context[64];
    if (flow) {
        // What the curve controls stands where the step address is elsewhere.
        const int slot = g_score.sequence_value_slot[g_seq_chain], param = g_score.sequence_value_param[g_seq_chain];
        const bool known = slot < UNITS_PER_CHAIN && param < g_inspector.modules[slot].count;
        std::snprintf(context, sizeof context, "%.6s %.8s", known ? m::module_name(g_inspector.modules[slot].kind) : "level",
                      known ? g_inspector.modules[slot].params[param].name : "");
        ui::text(44, 199, context, seq_ghost());
    } else {
        std::snprintf(context, sizeof context, "%d:%02d", g_seq_chain + 1, g_seq_col + 1);
        ui::text(56, 199, context, seq_ghost());
    }
    if(g_seq_mode==SeqMode::Note){
        char scale[32];sequencer_scale::label(scale,sizeof scale,g_score.project_scale);
        ui::text(sequencer_scale::kX,sequencer_scale::kY,scale,seq_spot());
    }
    char value[32] = {};
    if (g_seq_mode == SeqMode::Trig && !flow) {
        std::snprintf(value, sizeof value, "%s", meta_trig(meta) ? "event on" : "event off");
        ui::text(118, 216, "a on / b off", seq_ink());
    } else if (flow || g_seq_mode == SeqMode::Note || g_seq_mode == SeqMode::Level) {
        const bool pitch_mode = g_seq_mode == SeqMode::Note;
        const int slot = pitch_mode ? g_score.sequence_pitch_slot[g_seq_chain]
                                    : g_score.sequence_value_slot[g_seq_chain];
        if (slot < 0) {
            std::snprintf(value, sizeof value, "no %s target", pitch_mode ? "pitch" : "level");
        } else {
            int normalized = g_score.sequence_value[g_seq_chain][g_seq_col];
            if (pitch_mode) {
                pitch_text(g_seq_chain, g_seq_col, value, sizeof value);
                const int pitch = g_score.sequence_pitch[g_seq_chain][g_seq_col];
                const UnitKind kind = (UnitKind)g_score.sequence_pitch_kind[g_seq_chain];
                const bool bipolar =
                    kind == UnitKind::SamplePlayer || kind == UnitKind::Grain || kind == UnitKind::Stranular ||
                    kind == UnitKind::Scratch || kind == UnitKind::Paul;
                normalized = bipolar ? (pitch + 32767) / 2 : pitch;
                if (sequence_note::absolute(kind, g_score.sequence_pitch_param[g_seq_chain])) {
                    const int low=sequence_note::minimum(kind),high=sequence_note::maximum(kind);
                    normalized=std::max(0,std::min(32767,(sequence_note::midi(kind,q15(pitch))-low)*32767/(high-low)));
                }
            } else
                std::snprintf(value, sizeof value, "%d%%", value_percent(g_seq_chain, g_seq_col));
            const int x=VALUE_X0 + (normalized * VALUE_TRAVEL + 16383) / 32767;
            ui::horizontal(VALUE_X0,VALUE_X1,VALUE_Y,x,x);
        }
    } else if (g_seq_mode == SeqMode::Retrig) {
        std::snprintf(value, sizeof value, "%d hits", meta_retrig(meta));
        for (int count = 1; count <= 4; ++count) {
            const int x = 100 + (count - 1) * 60;
            draw_retrig_dots(x, 218, count, count == meta_retrig(meta) ? seq_spot() : seq_ghost());
        }
    } else if (g_seq_mode == SeqMode::When) {
        std::snprintf(value, sizeof value, "%s", condition_name(meta_condition(meta)));
        static const char *labels[] = {"all", "50", "25", "75", "1:2", "2:2", "f", "!f"};
        for (int i = 0; i < (int)SeqStep::Condition::Count; ++i) {
            const int x = 70 + i * 30;
            if (i == (int)meta_condition(meta))
                ui::line(x + 3, 224, x + 25, 224, seq_spot(), 1);
            ui::text(x + (29 - ui::text_w(labels[i])) / 2, 214, labels[i],
                     i == (int)meta_condition(meta) ? seq_spot() : seq_ink());
        }
    } else if (g_seq_mode == SeqMode::Slice) {
        ui::text(122,199,"cuts",seq_spot());
        const auto& row = g_slices.rows[g_seq_chain];
        const int count = seq_slice::count(row.valid);
        const auto& cell = row.cells[g_seq_col % PAGE_STEPS];
        if (row.slot < 0) std::snprintf(value, sizeof value, "no play source");
        else if (!row.loaded) std::snprintf(value, sizeof value, "no sample");
        else if (!count) std::snprintf(value, sizeof value, "create cuts");
        else if (!cell.slice) std::snprintf(value, sizeof value, "%s / window", cell.locked ? "lock" : "base");
        else std::snprintf(value, sizeof value, "%s %02u / %d cuts", cell.locked ? "lock" : "base", cell.slice, count);
        const u32 color = count ? seq_ink() : seq_branch();
        ui::text(8, 214, "window", count && !cell.slice ? seq_spot() : color);
        ui::text(70, 214, "<", color); ui::text(292, 214, ">", color);
        ui::line(102, 222, 267, 222, seq_branch(), 1);
        for (int i = 1; i <= count; ++i) {
            const int id = seq_slice::at(row.valid, i);
            const int x = 102 + (2 * i - 1) * 166 / (2 * count);
            const bool selected = cell.slice == id;
            ui::line(x, 215, x, 222, selected ? seq_spot() : seq_ghost(), 1);
            if (selected) bead(x, 215, 3, seq_spot());
        }
    } else if (g_seq_mode == SeqMode::Time) {
        std::snprintf(value, sizeof value, "%+d / 96", meta_timing(meta));
        const int x = VALUE_X0 + (meta_timing(meta) + SEQ_MICROTIMING_MAX) * VALUE_TRAVEL / (SEQ_MICROTIMING_MAX * 2);
        ui::horizontal(VALUE_X0,VALUE_X1,VALUE_Y,x,x,{},160);
    } else {
        const auto& detail = g_inspector;
        if (detail.slot < 0 || detail.param < 0) {
            std::snprintf(value, sizeof value, "no parameter");
        } else {
            const auto& param = detail.modules[detail.slot].params[detail.param];
            std::snprintf(context, sizeof context, "< %d %.6s >", detail.slot + 1, param.name);
            ui::text(70, 215, context, seq_ink());
            q15 current = param.base;
            bool locked = false;
            for (int i = 0; i < detail.lock_count; ++i)
                if (detail.locks[i].slot == detail.slot && detail.locks[i].param == detail.param) {
                    current = detail.locks[i].value; locked = true;
                }
            const int normalized = param.bipolar ? (current + 32767) / 2 : current;
            const int x=160 + normalized * 155 / 32767;
            ui::horizontal(160,315,218,x,x,{},param.bipolar?237:-1);
            std::snprintf(value, sizeof value, "%s %d / %d:%d", locked ? "lock" : "base",
                          (int)current * 100 / 32767, detail.lock_count, SEQ_LOCKS);
        }
    }
    if (g_seq_lock_flash > 0)
        std::snprintf(value, sizeof value, "lock slots full");
    ui::text(311 - ui::text_w(value), 199, value, seq_ink());
    if(g_inspector.status[0]) ui::footer(320,g_inspector.status,"");
    else ui::footer(320,"a+ b- / y listen","l tools / r mode");
}

// Chooser-only layer marks: what each layer already draws in a cell (trig square,
// level meter, retrig pips, time rail, four lock slots) or on the upper profile.
void ring_icon(int mode, int cx, int cy, u32 c) {
    switch ((SeqMode)mode) {
    case SeqMode::Trig: ui::rect(cx - 3, cy - 3, 7, 7, c); break;
    case SeqMode::Note:
        ui::rect(cx - 4, cy + 2, 3, 2, c); ui::rect(cx - 1, cy - 1, 3, 2, c); ui::rect(cx + 2, cy - 4, 3, 2, c); break;
    case SeqMode::Level:
        ui::rect(cx - 4, cy - 2, 6, 5, c); ui::rect(cx + 2, cy - 2, 3, 1, c);
        ui::rect(cx + 2, cy + 2, 3, 1, c); ui::rect(cx + 4, cy - 1, 1, 3, c); break;
    case SeqMode::Retrig:
        for (int i = -1; i <= 1; ++i) ui::rect(cx - 1 + i * 4, cy - 1 + i * 4, 2, 2, c);
        break;
    case SeqMode::When:
        ui::rect(cx - 4, cy - 4, 3, 3, c); ui::rect(cx + 2, cy + 2, 3, 3, c);
        for (int i = 0; i < 7; ++i) ui::rect(cx + 3 - i, cy - 3 + i, 1, 1, c);
        break;
    case SeqMode::Time:
        ui::rect(cx - 4, cy, 9, 1, c); ui::rect(cx, cy - 3, 1, 7, c); ui::rect(cx + 2, cy - 1, 3, 3, c); break;
    case SeqMode::Lock:
        for (int i = 0; i < 4; ++i) ui::rect(cx - 5 + i * 3, cy - 2, 2, 5, c);
        break;
    case SeqMode::Slice:
        ui::rect(cx - 4, cy - 1, 1, 3, c); ui::rect(cx - 2, cy - 3, 1, 7, c); ui::rect(cx, cy - 5, 1, 11, c);
        ui::rect(cx + 2, cy - 2, 1, 5, c); ui::rect(cx + 4, cy - 1, 1, 3, c); break;
    default: break;
    }
}
void draw_mode_ring() {
    // Geometry and words stay still over the frozen blurred score. Only the
    // UI-captured pointer eases; no engine access, animation advance or input
    // decisions in this drawing pass.
    const auto& point = g_ring_pointer;
    auto fade = [&](u32 color, float strength = 1.f) {
        return (color & 0x00ffffffu) | (u32(255 * point.opacity * strength) << 24);
    };
    ui::circle(RING_X, RING_Y, RING_RADIUS, fade(seq_ghost(), .38f), false);
    for (int i = 0; i < MODE_COUNT; ++i) {
        const bool active = point.candidate == i;
        const char* name = mode_name((SeqMode)i);
        const int w = ui::text_w(name), left = SPOKE_X[i] - w / 2, label_y = mode_label_y(i);
        // Candidate: spot and the shared word underline of the exact text width.
        // The still-committed layer keeps ink; browsing never disguises it.
        const u32 color = fade(active ? seq_spot() : g_seq_mode == (SeqMode)i ? seq_ink() : seq_ghost());
        ring_icon(i, SPOKE_X[i], SPOKE_Y[i], color);
        ui::text(left, label_y, name, color);
        if (active) ui::rect(left, label_y + 12, w - 1, 1, color);
    }
    // The centre names what releasing R gives.
    const char* name = mode_name(point.candidate >= 0 ? (SeqMode)point.candidate : g_seq_mode);
    ui::text(RING_X - ui::text_w(name, 2) / 2, RING_Y - 8, name,
             fade(point.candidate >= 0 ? seq_spot() : seq_ghost()), 2);
    if (point.candidate >= 0) {
        if (point.pulse > 0) ui::circle(SPOKE_X[point.candidate], SPOKE_Y[point.candidate],
            9 + 3 * (1-point.pulse), fade(seq_spot(), point.pulse * .65f), false);
    } else ui::circle(point.x, point.y, 3, fade(seq_spot()), true); // free pointer; a captured one is its icon
}
void draw_wave(bool dim=false) {
    u32 color=seq_wave::ink(seq_ink(),seq_spot(),g_wave_accent);
    if(dim)color=(color&0xffffffu)|0x18000000u;
    int previous_x=0;
    for(int i=1;i<Engine::SCOPE_LEN;++i) {
        const int x=i*319/(Engine::SCOPE_LEN-1);
        ui::line(previous_x,g_wave_y[i-1],x,g_wave_y[i],color,1);
        previous_x=x;
    }
}
#include "sequencer_bank_hold.inc"
void draw_extent_marks(int chain,int first,int y) {
    namespace ui = score_ink;
    const int length=g_score.sequence_length[chain];
    for (int local=0;local<PAGE_STEPS;++local) {
        if (first+local>=length) {
            const int x=pattern_x(local);
            // Slash is an inactivity mark, not a deleted event: all data remains editable.
            ui::line(x+6,y+21,x+10,y+19,seq_inactive(),1);
        }
    }
    // No end post at 16-step page edges (or inside a page). Length remains in
    // held B / upper inspector; inactive cells keep their dim ink and slash.
}
// The real score stream: wave, cells, focus, selection, playhead and editor.
// Drawn natively, or into the shared cache as a disclosure's frozen background.
void draw_score_surface() {
    const int first = g_seq_page * PAGE_STEPS;
    if(!ui::pen.cache)g_feedback_now = osGetTime();
    g_feedback_visible = g_overlay == Overlay::None && g_view == View::Sequencer;
    if(!ui::pen.cache){
        if (g_feedback_visible) g_step_press.update(g_feedback_now, feedback_context());
        else g_step_press.reset();
    }
    draw_wave();
    for (int chain = 0; chain < NUM_CHAINS; ++chain) {
        const int y = PATTERN_ROWS[chain];
        if (flow_chain(chain) && g_seq_mode == SeqMode::Trig)
            draw_flow_lane(chain, first, y);
        else
            for (int local = 0; local < PAGE_STEPS; ++local)
                draw_event_cell(chain, first + local, pattern_x(local), y,
                                chain == g_seq_chain && first + local == g_seq_col);
        draw_extent_marks(chain,first,y);
    }
    // Compact selection fill follows the actual flow value, not row baseline.
    if (g_overlay == Overlay::None && g_view == View::Sequencer &&
        flow_chain(g_seq_chain) && g_seq_mode == SeqMode::Trig) {
        g_step_focus.reset();
        const int y = PATTERN_ROWS[g_seq_chain];
        // The plate travels along the stored curve in the event plate's 100ms; a
        // stylus stroke owns it directly. The selection itself is already live.
        const float target = float(pattern_x(g_seq_col % PAGE_STEPS) + 8);
        const float xc = ui::pen.cache ? target :
            g_flow_focus.update(g_feedback_now, feedback_context() * NUM_CHAINS + g_seq_chain, target, g_paint_active);
        g_focus_box = {int(std::floor(xc + .5f)) - 8,
                       int(std::floor(flow_curve_y(g_seq_chain, first, xc, y) + .5f)) - 5, CELL_W, 10};
        ui::fill(g_focus_box, seq_spot());
        // Original native curve, clipped to the fill; never a synthetic contour.
        g_focus_inverse = true;
        draw_flow_lane(g_seq_chain, first, y);
        g_focus_inverse = false;
    }
    // One moving fill, then inverse-only foreground from the live score. Row /
    // column rejection keeps this bounded to at most a handful of native cells.
    // No editor, wave, playhead or score state is advanced by the animation.
    else if (g_overlay == Overlay::None && g_view == View::Sequencer) {
        g_flow_focus.reset();
        if(!ui::pen.cache)g_focus_box = g_step_focus.update(g_feedback_now, feedback_context(), g_seq_chain, g_seq_col % PAGE_STEPS);
        else if(!g_focus_box.w)g_focus_box=step_focus::raster(step_focus::target(g_seq_chain,g_seq_col%PAGE_STEPS));
        const auto target = step_focus::raster(step_focus::target(g_seq_chain, g_seq_col % PAGE_STEPS));
        const bool arrived = g_focus_box.x == target.x && g_focus_box.y == target.y &&
            g_focus_box.w == target.w && g_focus_box.h == target.h;
        // A hit at the logical destination must never squash a travelling plate
        // at its old location. Focus motion wins until it actually arrives.
        const float hit = arrived ? playback_power(g_seq_chain, g_seq_col) : 0;
        const float press = arrived ? edit_power(g_seq_chain, g_seq_col) : 0;
        ui::fill(step_feedback::face(g_focus_box, hit, press), feedback_color(seq_spot(), hit, press));
        // Keep the original inverse mask, NOT the smaller backing. Thus every
        // native glyph retains its exact coordinates and selected ink color.
        g_focus_inverse = true;
        for (int chain = 0; chain < NUM_CHAINS; ++chain) {
            const int y = PATTERN_ROWS[chain];
            if (!step_focus::overlaps(g_focus_box, {X0, y - 1, PITCH * PAGE_STEPS, CELL_H + 2})) continue;
            if (flow_chain(chain) && g_seq_mode == SeqMode::Trig) draw_flow_lane(chain, first, y);
            else for (int local = 0; local < PAGE_STEPS; ++local)
                if (step_focus::overlaps(g_focus_box, {pattern_x(local), y, CELL_W, CELL_H}))
                    draw_event_cell(chain, first + local, pattern_x(local), y,
                                    chain == g_seq_chain && first + local == g_seq_col);
            draw_extent_marks(chain, first, y);
        }
        g_focus_inverse = false;
    } else { g_step_focus.reset(); g_flow_focus.reset(); }
    for(int local=0;local<PAGE_STEPS;++local) {
        const int x=pattern_x(local), y=PATTERN_ROWS[g_seq_chain], step=first+local;
        if(g_inspector.selection&seq_edit::bit(step))
            ui::frame(x,y-2,CELL_W,CELL_H+2,seq_spot());
        // Paste destination: the free row just under the 18px plate, clear of the
        // octave digit (y+23..30), the level meter and the lock slots.
        if((g_l_held||g_tools) && (g_inspector.paste_mask&seq_edit::bit(step)))
            ui::line(x+4,y+19,x+13,y+19,seq_ghost(),1);
    }
    draw_playhead();
    draw_flow_live(first);
    draw_editor();
}
void draw_score() {
    // The chooser owns this surface over the frozen blurred score: no live
    // translucent glyphs under its words (dense slice numbers showed through).
    if (g_mode_ring && !ui::pen.cache) {
        if (!sigil_surface::bottom_cache.draw()) ui::paper(320);
        draw_mode_ring();
        return;
    }
    ui::paper(320);
    draw_score_surface();
}
// One CPU pass of the real score into the shared half-resolution cache, outside
// the GPU frame and any audio guard. The existing sigils_gpu_frame uploads it.
void freeze_score() {
    auto& cache = sigil_surface::bottom_cache;
    cache.clear();
    ui::pen = {&cache}; ui::paper(320); draw_score_surface(); ui::pen = {};
    cache.finish();
}
void ring_capture() {
    if (!g_mode_ring || !g_ring_dirty) return;
    g_ring_dirty = false;
    freeze_score();
}

#include "sequencer_scale_menu.inc"
#include "sequencer_lock_menu.inc"

void clear_step() {
    platform::AudioOut::Guard guard(*g_audio);
    const auto& before = edit_pattern().steps[g_seq_chain][g_seq_col];
    if (before.flags || before.lock_count || edit_pattern().microtiming[g_seq_chain][g_seq_col]) note_value_changed();
    g_eng->cancel_sequence_audition();
    g_eng->clear_step(g_seq_chain, g_seq_col, g_seq_bank);
    mark_project_mutated();
}
void choose_ring_from_circle() {
    circlePosition pos;
    hidCircleRead(&pos);
    const uint64_t now = osGetTime();
    g_ring_pointer.sample(pos.dx, pos.dy, now);
    sync_ring_choice();
    if (g_ring_pointer.pad_active) show_mode_ring(now);
}
// Called once inside the existing pre-GPU inspector guard. Only the selected
// sample's bounded, decimated envelope is read (<=5632 scalar PCM reads), never
// a duration-dependent scan/cache or a pointer retained across sample changes.
void capture_slices_locked() {
    auto& out = g_slices;
    g_cuts.active=g_cuts_open;
    if(g_cuts_open){
        cut_source_locked();
        g_cuts.selected=(uint8_t)g_cut_selected;g_cuts.add_armed=g_cut_add;
        g_cuts.confirm=(uint8_t)g_cut_confirm;g_cuts.busy=cut_busy_locked();
        if(g_cuts.busy){g_eng->cancel_sequence_audition();g_cut_drag=-1;g_cut_add=false;g_cut_confirm=0;g_cuts.add_armed=false;g_cuts.confirm=0;}
    }
    out.chain = g_seq_chain; out.step = g_seq_col; out.first = g_seq_page * PAGE_STEPS;
    const auto& bank = trackr::synth::SampleBank::instance();
    for (int c = 0; c < NUM_CHAINS; ++c) {
        auto& row = out.rows[c];
        row.slot = (int8_t)slice_source_slot(c);
        if (row.slot < 0) continue;
        const auto& play = *static_cast<const PlayUnit*>(g_eng->chains[c].units[row.slot]);
        if (play.sample_slot >= 0 && play.sample_slot < trackr::synth::SAMPLE_BANK_SIZE) {
            const auto& sample = bank.slot(play.sample_slot);
            row.loaded = (sample.channels == 1 || sample.channels == 2) && sample.playable_frames() >= 2;
            row.valid = seq_slice::valid_mask(sample);
        }
        for (int i = 0; i < PAGE_STEPS; ++i) {
            const auto& step = edit_pattern().steps[c][out.first + i];
            q15 value = play.params[PlayUnit::P_SLICE].base;
            for (int n = 0; n < step.lock_count && n < SEQ_LOCKS; ++n)
                if (step.locks[n].slot == row.slot && step.locks[n].param == PlayUnit::P_SLICE) {
                    value = step.locks[n].value; row.cells[i].locked = true; break;
                }
            row.cells[i].slice = (uint8_t)seq_slice::decode(value);
        }
    }
    const auto& row = out.rows[g_seq_chain];
    g_cuts.slot=row.slot;
    if (row.slot < 0) return;
    const auto& play = *static_cast<const PlayUnit*>(g_eng->chains[g_seq_chain].units[row.slot]);
    const auto& cell = row.cells[g_seq_col % PAGE_STEPS];
    out.sample = play.sample_slot; out.requested = cell.slice; out.locked = cell.locked;
    g_cuts.sample=play.sample_slot;
    if(g_cuts_open){out.requested=(uint8_t)g_cut_selected;out.locked=false;}
    if (!row.loaded) return;
    const auto& sample = bank.slot(play.sample_slot);
    out.loaded = true; out.frames = sample.playable_frames(); out.reversed = sample.slice_reverse_mask;
    if(g_cuts_open){
        g_cuts.frames=out.frames;
        std::memcpy(g_cuts.chops,sample.chops,sizeof g_cuts.chops);
        for(int i=0;i<16;++i)if(sample.chops[i]!=UINT32_MAX&&sample.chops[i]<out.frames)g_cuts.present|=(uint16_t)(1u<<i);
        if(g_cut_selected>0&&g_cut_selected<=16&&(row.valid&(1u<<(g_cut_selected-1)))){
            g_cuts.selected_begin=sample.chops[g_cut_selected-1];g_cuts.selected_end=cut_end(sample,g_cuts.selected_begin);
        }
    }
    for (int i = 0; i < 16; ++i) {
        if (!(row.valid & (1u << i))) continue;
        const uint32_t a = sample.chops[i];
        uint32_t b = out.frames;
        for (uint32_t c : sample.chops) if (c > a && c < b) b = c;
        out.begin[i] = (uint16_t)((uint64_t)a * 32767 / out.frames);
        out.end[i] = (uint16_t)((uint64_t)b * 32767 / out.frames);
    }
    q15 values[PlayUnit::P_COUNT];
    const auto& step = edit_pattern().steps[g_seq_chain][g_seq_col];
    for (int p = 0; p < PlayUnit::P_COUNT; ++p) {
        values[p] = play.params[p].base;
        if (p != PlayUnit::P_GAIN && play.params[p].src != ModSource::None && play.params[p].mod_depth)
            out.modulated = true;
        for (int n = 0; n < step.lock_count && n < SEQ_LOCKS; ++n)
            if (step.locks[n].slot == row.slot && step.locks[n].param == p) { values[p] = step.locks[n].value; break; }
        const int low = play.params[p].bipolar ? -32767 : 0;
        if (values[p] < low) values[p] = (q15)low;
    }
    if(g_cuts_open)values[PlayUnit::P_SLICE]=seq_slice::encode(g_cut_selected);
    const auto window = play.edit_source_matches() ? sequence_note::play_window(sample, values,
        play.pitch_quantize, play.modern(), play.edit_state(), play.pitch_mode(), g_score.project_scale) : PlayWindow{};
    out.window_valid = window.valid; out.applied = window.slice;
    out.window_begin = (uint16_t)((uint64_t)window.start*32767/out.frames);
    out.window_end = (uint16_t)((uint64_t)window.end*32767/out.frames);
    out.reverse = window.increment < 0; out.loop = values[PlayUnit::P_LOOP] > (32767 / 2);
    out.milliseconds = window.traversal_ms();
    out.visual_gain_q8=pcm_envelope::capture(sample,out.low,out.high,out.pcm_reads);
}
} // namespace

static void capture_inspector_detail() {
    if(g_b_pressed)return; // freeze committed context and real trace throughout the hold
    // Existing worker-published, post-limiter master LEFT samples (every 8th
    // frame, ~50ms total). Bounded projection outside Guard and before GPU;
    // never read the running engine's ring or scan sample/recording PCM here.
    const int pos=(g_ui_snapshot.scope_pos>=0 && g_ui_snapshot.scope_pos<Engine::SCOPE_LEN)
        ?g_ui_snapshot.scope_pos:0;
    uint32_t peak=0;
    for(int i=0;i<Engine::SCOPE_LEN;++i) {
        const int32_t sample=g_ui_snapshot.scope[(pos+i)%Engine::SCOPE_LEN];
        g_wave_y[i]=seq_wave::project(sample);
        const uint32_t magnitude=sample<0?uint32_t(-sample):uint32_t(sample);
        if(magnitude>peak)peak=magnitude;
    }
    g_wave_accent=seq_wave::accent_amount(peak);
    g_inspector = {};
    g_slices = {};
    g_cuts = {};
    if (!g_eng || !g_audio) return;
    platform::AudioOut::Guard guard(*g_audio);
    capture_score_locked();
    sync_edit_context();
    if (g_seq_mode == SeqMode::Slice) capture_slices_locked();
    normalize_lock_target();
    g_inspector.chain = g_seq_chain;
    g_inspector.step = g_seq_col;
    g_inspector.show_l_help = g_l_held && !g_r_pressed && !g_r_cancel;
    g_inspector.selection=g_selection.mask;
    g_inspector.selected_count=seq_edit::count(g_selection.targets(g_seq_col));
    g_inspector.clipboard_count=seq_edit::count(g_clipboard.mask);
    g_inspector.copy_scope=(int)g_copy_scope;
    g_inspector.clipboard_scope=(int)g_clipboard.scope;
    g_inspector.offset=g_offset;
    g_inspector.can_undo=g_history.cursor>0;
    g_inspector.can_redo=g_history.cursor<g_history.size;
    std::snprintf(g_inspector.status,sizeof g_inspector.status,"%s",g_edit_status);
    if(g_clipboard.chain==g_seq_chain && g_clipboard.signature==g_edit_signature &&
        (g_clipboard.scope!=seq_edit::Scope::Parameter || (g_clipboard.slot==g_lock_slot && g_clipboard.param==g_lock_param)))
        g_clipboard.destinations(g_selection.mask,g_seq_col,g_inspector.paste_mask);
    if (valid_lock_target(g_lock_slot, g_lock_param)) {
        g_inspector.slot = g_lock_slot;
        g_inspector.param = g_lock_param;
    }
    const SeqStep& step = edit_pattern().steps[g_seq_chain][g_seq_col];
    g_inspector.lock_count = step.lock_count < SEQ_LOCKS ? step.lock_count : SEQ_LOCKS;
    for (int i = 0; i < g_inspector.lock_count; ++i) g_inspector.locks[i] = step.locks[i];
    for (int s = 0; s < UNITS_PER_CHAIN; ++s) {
        const Unit* unit = g_eng->chains[g_seq_chain].units[s];
        if (!unit) continue;
        auto& module = g_inspector.modules[s];
        module.kind = unit->kind;
        module.variant=seq_lock::variant(*unit);
        if(unit->kind==UnitKind::SamplePlayer) module.sample=static_cast<const PlayUnit*>(unit)->sample_slot;
        else if(unit->kind==UnitKind::Grain) module.sample=static_cast<const GrainUnit*>(unit)->sample_slot;
        module.bypass = unit->bypass;
        module.count = unit->num_params < PARAM_BANK_SIZE ? unit->num_params : PARAM_BANK_SIZE;
        for (int p = 0; p < module.count; ++p) {
            const Param& source = unit->params[p];
            auto& dest = module.params[p];
            for (int i = 0; i < 8 && source.name[i]; ++i) {
                char c = source.name[i];
                dest.name[i] = c >= 'A' && c <= 'Z' ? (char)(c + 'a' - 'A') : c;
            }
            dest.base = source.base; dest.live = source.effective;
            dest.depth = source.mod_depth; dest.source = source.src; dest.bipolar = source.bipolar;
        }
    }
    if(g_inspector.slot>=0) {
        const auto targets=g_selection.targets(g_seq_col);
        const auto& module=g_inspector.modules[g_inspector.slot];
        for(int p=0;p<module.count;++p) {
            auto& row=g_inspector.rows[p]; row.focused=module.params[p].base;
            const int f=seq_edit::find(step,g_inspector.slot,p);
            if(f>=0) { row.focused_locked=true; row.focused=step.locks[f].value; }
            for(int s=0;s<SEQ_STEPS;++s) if(targets&seq_edit::bit(s)) {
                const auto& source=edit_pattern().steps[g_seq_chain][s];
                const int i=seq_edit::find(source,g_inspector.slot,p);
                if(i<0) continue;
                const q15 v=source.locks[i].value;
                if(!row.covered || v<row.minimum) row.minimum=v;
                if(!row.covered || v>row.maximum) row.maximum=v;
                ++row.covered;
            }
        }
    }
    const int source_slot = g_score.sequence_pitch_slot[g_seq_chain];
    if (source_slot >= 0 && source_slot < UNITS_PER_CHAIN) {
        const Unit* source = g_eng->chains[g_seq_chain].units[source_slot];
        if (source && source->kind == UnitKind::SamplePlayer && !source->bypass) {
            const auto& play = *static_cast<const PlayUnit*>(source);
            auto& out = g_inspector.play;
            out.present = true;
            out.sample = play.sample_slot;
            if (play.sample_slot < 0 || play.sample_slot >= trackr::synth::SAMPLE_BANK_SIZE) return;
            q15 values[PlayUnit::P_COUNT];
            for (int p = 0; p < PlayUnit::P_COUNT; ++p) {
                values[p] = play.params[p].base;
                if (p != PlayUnit::P_GAIN && play.params[p].src != ModSource::None && play.params[p].mod_depth)
                    out.modulated = true;
                for (int i = 0; i < g_inspector.lock_count; ++i)
                    if (step.locks[i].slot == source_slot && step.locks[i].param == p) {
                        values[p] = step.locks[i].value; break;
                    }
                // Stored projection has no live modulation, but uses the same
                // domain clamp as Param::resolve (legacy locks may be negative).
                const int low = play.params[p].bipolar ? -32767 : 0;
                if (values[p] < low) values[p] = (q15)low;
            }
            const auto window = play.edit_source_matches() ? sequence_note::play_window(trackr::synth::SampleBank::instance().slot(play.sample_slot),
                values, play.pitch_quantize, play.modern(), play.edit_state(), play.pitch_mode(), g_score.project_scale) : PlayWindow{};
            out.valid = window.valid;
            out.loop = values[PlayUnit::P_LOOP] > (32767 / 2);
            out.start = window.normalized_start; out.end = window.normalized_end;
            out.increment = window.increment; out.slice = window.slice;
            out.milliseconds = window.traversal_ms();
        }
    }
}
void sequencer_capture_inspector() {
    capture_inspector_detail();scale_capture();lock_menu_capture();ring_capture();bank_capture();
}
const SequencerBankPanel& sequencer_bank_panel() { return g_bank_panel; }
const SequencerInspector& sequencer_inspector() { return g_inspector; }
const seq_slice::Frame& sequencer_slices() { return g_slices; }
const seq_cuts::Frame& sequencer_cuts() { return g_cuts; }
const EngineUiSnapshot& sequencer_snapshot() { return g_score; }
word_menu::Frame sequencer_scale::motion(int group) { return g_scale_menu.frame[group==0?0:1]; }
int sequencer_edit_bank() { return g_seq_bank; }
int sequencer_selected_chain() {
    return g_seq_chain;
}
int sequencer_selected_step() {
    return g_seq_col;
}
int sequencer_selected_layer() {
    return (int)g_seq_mode;
}
const char *sequencer_selected_layer_name() {
    return flow_row() ? "flow" : mode_name(g_seq_mode); // both screens name the row the same way
}
int sequencer_current_page() {
    return g_seq_page;
}
void sequencer_selected_pitch_text(char *out, int capacity) {
    pitch_text(g_seq_chain, g_seq_col, out, capacity);
}
void sequencer_forget_pins() {
    scale_cancel();
    g_ring_pointer.reset();
    g_mode_ring = g_r_pressed = g_r_choice = false;
    g_step_focus.reset();
    g_step_press.reset();
    g_clipboard.clear(); g_selection.clear(); g_history.clear();
    g_edit_bank=g_history_chain=-1; g_edit_signature=0;
    for(auto& signature:g_target_signature) signature=0;
    lock_menu_cancel(); g_tools=g_l_drain=false; g_pad_center=false;
    g_flow_focus.reset(); g_flow_live.reset(); g_flow_touch=false;
    g_b_copy=-1; g_b_rail=-1; g_b_confirm=0; g_bank_panel.open=false;
    g_seq_bank=-1;
    // A loaded or new project starts on trig, step 1, page 1 and the first target.
    g_seq_mode=g_ring_mode=SeqMode::Trig; g_seq_col=g_seq_page=0;
    g_lock_slot=g_lock_param=0; g_offset=false; g_edit_status[0]=0;
    g_b_drain=g_b_drain||g_b_pressed;
    g_b_pressed=g_b_open=false;
    g_cut_context=false;g_cuts_open=false;g_cut_add=false;g_cut_drag=-1;g_cut_confirm=0;
}

void sequencer_view_cancel_input() {
    scale_cancel();
    if(g_seq_mode==SeqMode::Lock) cancel_edit(); else finish_edit();
    g_pad_center=false; g_pad_direction=0;
    g_l_drain=g_l_drain||g_l_held;
    lock_menu_cancel(); g_tools=false;
    g_step_focus.reset();
    g_step_press.reset();
    g_b_drain=g_b_drain||g_b_pressed;
    g_b_pressed=g_b_open=false; // navigation cancels staged values
    g_b_rail=-1; g_b_confirm=0; g_bank_panel.open=false;

    g_cuts_open=false;g_cut_context=false;g_cut_add=false;g_cut_drag=-1;g_cut_confirm=0;
    if (g_audio && g_eng) {
        platform::AudioOut::Guard guard(*g_audio);
        g_eng->cancel_sequence_audition();
    }
    sequencer_view_touch_release(false);
    g_mode_ring = g_r_pressed = g_r_choice = false;
    g_ring_pointer.reset();
    g_r_cancel = true;
    g_touch_blocked_frame = true;
    g_l_held = false;
}
void sequencer_view_enter() {
    sequencer_view_cancel_input();
    // Leaving the score can replace a unit at the same address: no stale history
    // or clipboard may survive that unobserved structural edit.
    g_clipboard.clear(); g_history.clear(); g_selection.clear();
    g_edit_bank=g_history_chain=-1;
    g_l_drain=false;
    g_seq_chain = g_sel_chain;
    if (g_seq_chain < 0 || g_seq_chain >= NUM_CHAINS)
        g_seq_chain = 0;
    // Layer, step, page and lock target stay as they were left in this project
    // (project load / new resets them in sequencer_forget_pins). The capture below
    // validates the lock target against the chain as it is now.
    if (g_seq_col < 0 || g_seq_col >= SEQ_STEPS) g_seq_col = 0;
    g_seq_page = g_seq_col / PAGE_STEPS;
    if ((int)g_seq_mode >= (int)SeqMode::Count) g_seq_mode = SeqMode::Trig;
    g_ring_mode = g_seq_mode;
    g_r_cancel = false;
    g_touch_blocked_frame = false;
    sequencer_capture_inspector();
}
void sequencer_open_slices(int chain) {
    if(!g_eng||!g_audio)return;
    // Navigation only: preserve selected step/edit bank when returning to this chain.
    if(g_seq_chain!=chain){g_sel_chain=chain;sequencer_view_enter();}
    g_seq_chain=chain;g_seq_mode=g_ring_mode=SeqMode::Slice;
    g_view=View::Sequencer;g_overlay=Overlay::None;g_drag=Drag::None;
    g_cuts_open=false;g_touch_blocked_frame=true;
}
bool sequencer_view_back() {
    if(g_scale_menu.open||g_scale_menu.press){scale_cancel();g_touch_blocked_frame=true;return true;}
    if(g_b_pressed){sequencer_view_cancel_input();return true;}
    if(!g_cuts_open)return false;
    close_cuts();g_touch_blocked_frame=true;return true;
}
void sequencer_view_touch_down(int tx, int ty) {
    if(g_scale_menu.open||g_scale_menu.drain){scale_touch(tx,ty);return;}
    sequencer_view_touch_release(false);
    if(lock_menu_touch(tx,ty))return;
    if (g_r_pressed) {
        if (g_r_cancel)
            return;
        show_mode_ring(osGetTime());
        for (int i = 0; i < MODE_COUNT; ++i) {
            if (hit(tx, ty, MODE_X[i], MODE_Y[i], MODE_W, MODE_H)) {
                select_ring_mode(i);
                return;
            }
        }
        // Empty-space taps deliberately clear a candidate, without touching
        // the obscured score or changing the committed mode.
        g_ring_pointer.pinned = false;
        g_ring_pointer.aim(-1, RING_X, RING_Y, osGetTime());
        sync_ring_choice();
        return;
    }
    if(g_b_pressed && g_b_open && !g_b_drain){bank_touch_down(tx,ty);return;} // the held panel is touchable
    if (g_b_pressed || g_b_drain || g_r_cancel || g_touch_blocked_frame)
        return;
    if(g_cuts_open){cuts_touch(tx,ty);return;}
    { platform::AudioOut::Guard guard(*g_audio);capture_score_locked();sync_edit_context(); }
    if(scale_touch(tx,ty))return;
    if(tools_touch(tx,ty)) return;
    if(g_seq_mode==SeqMode::Lock && ty>=194 && ty<211) {
        if(tx>=258) g_tools=true;
        else g_lock_menu.press=g_lock_menu.armed=true; // the address opens module / parameter lists on lift
        return;
    }
    if(g_seq_mode==SeqMode::Slice && hit(tx,ty,118,194,44,16)){open_cuts();return;}
    g_touch_origin_x = tx;
    const int chain = chain_from_touch(ty);
    if (chain >= 0 && tx >= PATTERN_X0 && tx <= PATTERN_X1 + 1) {
        const int next_step = g_seq_page * PAGE_STEPS + local_from_touch(tx);
        if (g_seq_mode == SeqMode::Slice && (g_seq_chain != chain || g_seq_col != next_step)) {
            platform::AudioOut::Guard guard(*g_audio);
            g_eng->cancel_sequence_audition();
        }
        if(g_l_held) {
            if(g_seq_chain!=chain) {
                g_seq_chain=g_sel_chain=chain;
                platform::AudioOut::Guard guard(*g_audio); sync_edit_context();
            }
            g_seq_col=next_step; g_select_chain=chain;
            g_selection.begin_touch(next_step);
            return;
        }
        g_seq_chain = g_sel_chain = chain;
        g_seq_col = next_step;
        { platform::AudioOut::Guard guard(*g_audio); sync_edit_context(); }
        g_selection.clear(); g_edit_status[0]=0;
        // Selecting a pitch/condition must never increment it as a side effect.
        if (g_seq_mode == SeqMode::Trig) {
            g_pending_chain = chain;
            g_pending_step = g_seq_col;
            g_paint_value = !meta_trig(g_score.sequence_meta[chain][g_seq_col]);
            // On a flow row a stroke draws the value curve; the tap still toggles the trig.
            g_flow_touch = flow_row(chain);
            g_touch_origin_y = ty;
        }
        return;
    }
    if (g_seq_mode == SeqMode::Slice) {
        if(g_selection.mask) { edit_status("group values: l+up/down"); return; }
        if (ty < 210 || ty >= 227) return;
        begin_edit();
        if (tx >= 8 && tx < 52) set_slice_window();
        else if (tx >= 58 && tx < 96) set_slice_delta(-1);
        else if (tx >= 102 && tx < 268) { set_slice_absolute(tx); g_value_drag = true; }
        else if (tx >= 278 && tx < 316) set_slice_delta(1);
        return;
    }
    // Labels/help are not invisible sliders.
    if(g_selection.mask && g_seq_mode!=SeqMode::Lock) { edit_status("group values: l+up/down"); return; }
    const bool value_rail=g_seq_mode==SeqMode::Note || g_seq_mode==SeqMode::Level || g_seq_mode==SeqMode::Time || flow_row();
    if (ty < 210 || ty >= 227 || tx < (value_rail?VALUE_X0:70) || tx > (value_rail?VALUE_X1:315))
        return;
    begin_edit();
    switch (g_seq_mode) {
    case SeqMode::Trig:
        if (flow_row()) { set_level_absolute(tx); g_value_drag = true; } // the selected point of the curve
        else toggle_trig();
        break;
    case SeqMode::Note:
        set_pitch_absolute(tx);
        g_value_drag = true;
        break;
    case SeqMode::Level:
        set_level_absolute(tx);
        g_value_drag = true;
        break;
    case SeqMode::Retrig:
        set_retrig(1 + (tx - 70) / 60);
        break;
    case SeqMode::When: {
        const int i = (tx - 70) / 30;
        if (i < (int)SeqStep::Condition::Count)
            set_condition((SeqStep::Condition)i);
        break;
    }
    case SeqMode::Time:
        set_timing(-SEQ_MICROTIMING_MAX + (tx - VALUE_X0) * SEQ_MICROTIMING_MAX * 2 / VALUE_TRAVEL);
        g_value_drag = true;
        break;
    case SeqMode::Lock:
        if(tx>=160 && g_inspector.slot>=0) {
            const auto& mod=g_inspector.modules[g_inspector.slot];
            if(seq_lock::supports_offset(mod.kind,mod.variant,g_inspector.param)) {
                g_drag_origin_x=tx; g_value_drag=true; set_lock_absolute(tx);
            } else edit_group_lock(tx<238?-1:1,true);
        }
        break;
    default:
        break;
    }
}
void sequencer_view_touch_hold(int tx, int ty) {
    if(g_scale_menu.open||g_scale_menu.press||g_scale_menu.drain){scale_hold(tx,ty);return;}
    if(g_b_pressed && g_b_open && !g_b_drain){bank_touch_hold(tx,ty);return;}
    if(g_lock_menu.open){lock_menu_hold(tx,ty);return;}
    if(g_lock_menu.press){if(!lock_address_hit(tx,ty))g_lock_menu.armed=false;return;} // dragged off: no menu
    if (g_b_pressed || g_b_drain || g_r_pressed || g_r_cancel || g_touch_blocked_frame)
        return;
    if(g_cuts_open){if(g_cut_drag>=0)cut_wave_touch(tx,ty,true);return;}
    if(g_selection.touch) {
        if(!g_l_held) return;
        if(chain_from_touch(ty)!=g_select_chain || tx<PATTERN_X0 || tx>PATTERN_X1+1) return;
        const int movement=tx>g_touch_origin_x?tx-g_touch_origin_x:g_touch_origin_x-tx;
        if(movement<9) return;
        g_seq_col=g_seq_page*PAGE_STEPS+local_from_touch(tx);
        g_selection.drag(g_seq_col);
        return;
    }
    if (g_value_drag) {
        if (g_seq_mode == SeqMode::Slice)
            set_slice_absolute(tx);
        else if (g_seq_mode == SeqMode::Note)
            set_pitch_absolute(tx);
        else if (g_seq_mode == SeqMode::Level)
            set_level_absolute(tx);
        else if (g_seq_mode == SeqMode::Time)
            set_timing(-SEQ_MICROTIMING_MAX + (tx - VALUE_X0) * SEQ_MICROTIMING_MAX * 2 / VALUE_TRAVEL);
        else if (g_seq_mode == SeqMode::Lock)
            set_lock_absolute(tx);
        else if (flow_row())
            set_level_absolute(tx);
        return;
    }
    if (g_pending_step < 0 || g_seq_mode != SeqMode::Trig)
        return;
    if (g_flow_touch) { flow_draw_hold(tx, ty); return; }
    const int movement = tx > g_touch_origin_x ? tx - g_touch_origin_x : g_touch_origin_x - tx;
    if (chain_from_touch(ty) != g_pending_chain || tx < PATTERN_X0 || tx > PATTERN_X1 + 1)
        return;
    if (!g_paint_active && movement < 9)
        return;
    begin_edit();
    platform::AudioOut::Guard guard(*g_audio);
    const int step = g_seq_page * PAGE_STEPS + local_from_touch(tx);
    if (!g_paint_active) {
        g_paint_active = true;
        g_paint_last_step = g_pending_step;
    }
    // Fill skipped cells too, including when a fast stylus jumps several columns.
    const int direction = step >= g_paint_last_step ? 1 : -1;
    for (int s = g_paint_last_step;; s += direction) {
        SeqStep &target = edit_pattern().steps[g_pending_chain][s];
        if (bool(target.flags & SeqStep::Trig) != g_paint_value)
            g_step_press.fire(osGetTime(), feedback_context(), g_pending_chain, s);
        if (g_paint_value)
            target.flags |= SeqStep::Trig;
        else
            target.flags &= ~SeqStep::Trig;
        if (s == step)
            break;
    }
    g_paint_last_step = g_seq_col = step;
    mark_project_mutated();
}
void sequencer_view_touch_release(bool commit_taps) {
    if(scale_release(commit_taps))return;
    if(g_lock_menu.close_on_lift) {
        g_lock_menu.close_on_lift=false;
        if(commit_taps)lock_menu_cancel(); // lifted on a parameter word: chosen
    }
    if(g_lock_menu.press) {
        const bool open=commit_taps && g_lock_menu.armed && !g_touch_blocked_frame;
        g_lock_menu.press=g_lock_menu.armed=false;
        if(open && g_seq_mode==SeqMode::Lock && !g_l_held && !g_tools)lock_menu_open();
    }
    g_b_rail=-1;
    if(!commit_taps && g_history.active) {
        if(g_seq_mode==SeqMode::Lock) cancel_edit(); else finish_edit();
    }
    g_selection.touch=false; g_select_chain=-1;
    g_cut_drag=-1;g_cut_drag_dirty=false;
    if (commit_taps && !g_b_pressed && !g_b_drain && !g_r_pressed && !g_r_cancel && !g_touch_blocked_frame && g_pending_step >= 0 &&
        !g_paint_active) {
        g_seq_chain = g_sel_chain = g_pending_chain;
        g_seq_col = g_pending_step;
        begin_edit(); toggle_trig();
    }
    g_pending_chain = g_pending_step = -1;
    g_value_drag = g_paint_active = g_flow_touch = false;
    g_paint_last_step = -1;
    finish_edit();
}
void sequencer_view_buttons(unsigned int down, unsigned int held, unsigned int up) {
    g_touch_blocked_frame = false;
    const bool had_l=g_l_held;
    g_l_held = (held & KEY_L) != 0;
    if(had_l && !g_l_held) {
        if(g_selection.touch) sequencer_view_touch_release(false);
        finish_edit(); g_l_drain=true; g_pad_center=false;
    }
    if (down & KEY_SELECT) {
        sequencer_view_cancel_input();
        return;
    }
    if(scale_buttons(down,held,up))return;
    if(lock_menu_buttons(down,held))return;
    if(bank_hold_buttons(down,held,up))return;
    { platform::AudioOut::Guard guard(*g_audio);capture_score_locked();sync_edit_context(); }
    if (down & KEY_R) {
        g_pad_center=false; g_pad_direction=0;
        g_tools=false;
        if(g_cuts_open)close_cuts();
        sequencer_view_touch_release(false);
        g_r_pressed = true;
        g_r_cancel = false;
        g_r_choice = false;
        g_r_start = g_ui_frame;
        g_ring_mode = g_seq_mode;
        g_ring_pointer.reset();
    }
    if (g_r_pressed) {
        g_touch_blocked_frame = true;
        if (down & KEY_B) {
            g_r_cancel = true;
            g_mode_ring = false;
        }
        if (!g_r_cancel) {
            if (g_ui_frame - g_r_start >= 12) show_mode_ring(osGetTime());
            // Include the release sample for a final capture. Free space or a
            // centered pad now preserves the last choice regardless of R timing.
            choose_ring_from_circle();
            static const unsigned int keys[] = {KEY_X, KEY_DUP,   KEY_DRIGHT, KEY_DDOWN,
                                                KEY_Y, KEY_DLEFT, KEY_A, KEY_L};
            if (held & KEY_R) for (int i = 0; i < MODE_COUNT; ++i)
                if (down & keys[i]) select_ring_mode(i);
        }
        if ((up & KEY_R) || !(held & KEY_R)) {
            if (!g_r_cancel) {
                if (g_r_choice)
                    g_seq_mode = g_ring_mode;
                else if (!g_mode_ring && g_ui_frame - g_r_start < 12)
                    g_seq_mode = (SeqMode)(((int)g_seq_mode + 1) % (int)SeqMode::Count);
            }
            {
                platform::AudioOut::Guard guard(*g_audio);
                g_eng->cancel_sequence_audition();
            }
            g_r_pressed = g_mode_ring = false;
            g_r_choice = false;
            g_r_cancel = false;
            g_ring_pointer.reset();
            sequencer_view_touch_release(false);
        }
        return; // the entire chord, including release, belongs to mode selection
    }
    // A held R returning from an overlay must not become a new tap/gesture.
    if (held & KEY_R) {
        g_touch_blocked_frame = true;
        return;
    }
    g_r_cancel = false;
    if(g_cuts_open){
        if(down&KEY_B){sequencer_view_back();return;}
        if(g_l_held&&(down&KEY_X)){remove_slice_lock();g_touch_blocked_frame=true;return;}
        if(down&(KEY_LEFT|KEY_RIGHT|KEY_A)){
            g_cut_add=false;g_cut_confirm=0;g_cut_drag=-1;
            platform::AudioOut::Guard guard(*g_audio);
            PlayUnit* unit=cut_source_locked();
            if(unit&&!cut_busy_locked()){
                const uint16_t mask=seq_slice::valid_mask(trackr::synth::SampleBank::instance().slot(unit->sample_slot));
                if(down&KEY_RIGHT)for(int i=g_cut_selected+1;i<=16;++i)if(mask&(1u<<(i-1))){g_cut_selected=i;break;}
                if(down&KEY_LEFT)for(int i=g_cut_selected-1;i>=1;--i)if(mask&(1u<<(i-1))){g_cut_selected=i;break;}
                audition_cut_locked(*unit);
            }
        }
        if(down&KEY_START){
            platform::AudioOut::Guard guard(*g_audio);
            if(!(held&KEY_L)){
                if(held&KEY_R){g_eng->transport_stop();g_eng->transport_reset();}
                else if(g_eng->transport_playing)g_eng->transport_stop();
                else {g_eng->set_sequencer_enabled(true);g_eng->transport_play();}
            }
        }
        return; // X/Y never delete/copy score cells while the cuts panel owns input.
    }
    if(g_l_drain) {
        g_touch_blocked_frame=true;
        if(!(held&(KEY_L|KEY_A|KEY_B|KEY_X|KEY_Y|KEY_DUP|KEY_DDOWN|KEY_DLEFT|KEY_DRIGHT|KEY_TOUCH))) g_l_drain=false;
        return;
    }
    if (g_l_held) {
        if(!had_l) { sequencer_view_touch_release(false); g_pad_center=false; }
        if (down & (KEY_A | KEY_B | KEY_X | KEY_Y)) {
            sequencer_view_touch_release(false);
            g_touch_blocked_frame=true;
            if(down&KEY_X) copy_steps();
            else if(down&KEY_A) paste_steps();
            else if(down&KEY_Y) history_step(false);
            else g_selection.clear();
            return;
        }
        if(down&(KEY_DLEFT|KEY_DRIGHT)) {
            sequencer_view_touch_release(false);
            const int old=g_seq_col;
            if((down&KEY_DLEFT) && g_seq_col>0) --g_seq_col;
            else if((down&KEY_DRIGHT) && g_seq_col<SEQ_STEPS-1) ++g_seq_col;
            g_selection.extend(old,g_seq_col); g_seq_page=g_seq_col/PAGE_STEPS;
            g_touch_blocked_frame=true;
        }
        if(down&(KEY_DUP|KEY_DDOWN)) {
            sequencer_view_touch_release(false); g_touch_blocked_frame=true;
            if((down&(KEY_DUP|KEY_DDOWN))!= (KEY_DUP|KEY_DDOWN))
                nudge_step_values((down&KEY_DUP)?1:-1, true);
        }
        return;
    }
    // Existing cuts/R/L ownership wins; only bare-score B starts setup.
    if(down&KEY_B) { begin_bank_hold();g_touch_blocked_frame=true;return; }
    if(held&KEY_B) { g_touch_blocked_frame=true;return; }
    if(down&KEY_Y) {
        sequencer_view_touch_release(false); g_touch_blocked_frame=true;
        audition_focused_step(); return;
    }
    if(!(held&KEY_TOUCH) && !g_value_drag && !g_selection.touch) pad_lock_navigation();
    const u32 arrows=KEY_DLEFT|KEY_DRIGHT|KEY_DUP|KEY_DDOWN;
    if(down&arrows) { sequencer_view_touch_release(false); g_selection.clear(); g_edit_status[0]=0; }
    if (g_seq_mode == SeqMode::Slice && (down & arrows)) {
        platform::AudioOut::Guard guard(*g_audio);
        g_eng->cancel_sequence_audition();
    }
    if (down & KEY_DLEFT)
        g_seq_col = (g_seq_col + SEQ_STEPS - 1) % SEQ_STEPS;
    if (down & KEY_DRIGHT)
        g_seq_col = (g_seq_col + 1) % SEQ_STEPS;
    g_seq_page = g_seq_col / PAGE_STEPS;
    if (down & KEY_DUP)
        g_seq_chain = (g_seq_chain + NUM_CHAINS - 1) % NUM_CHAINS;
    if (down & KEY_DDOWN)
        g_seq_chain = (g_seq_chain + 1) % NUM_CHAINS;
    g_sel_chain = g_seq_chain;
    { platform::AudioOut::Guard guard(*g_audio); sync_edit_context(); }
    if (down & KEY_A) {
        sequencer_view_touch_release(false); g_touch_blocked_frame=true;
        nudge_step_values(1, false);
    }
    if (down & KEY_X) {
        sequencer_view_touch_release(false);
        if (g_seq_mode == SeqMode::Lock) remove_target_lock(); // the selected target only, step or group
        else {
            g_selection.clear(); // bare X elsewhere is always just the focused step
            begin_edit(); clear_step(); finish_edit();
        }
    }
    if (down & KEY_START) {
        platform::AudioOut::Guard guard(*g_audio);
        if (!(held & KEY_L)) {
            if (held & KEY_R) {
                g_eng->transport_stop();
                g_eng->transport_reset();
            } else if (g_eng->transport_playing)
                g_eng->transport_stop();
            else {
                g_eng->set_sequencer_enabled(true);
                g_eng->transport_play();
            }
        }
    }
}
void sequencer_view_draw() {
    if (g_b_open || g_mode_ring || g_cuts.active) {
        g_step_focus.reset();
        g_step_press.reset();
    }
    if(g_scale_menu.open)scale_draw();
    else if(g_lock_menu.open)lock_menu_draw();
    else if(g_b_open)draw_bank_hold();
    else if(g_cuts.active&&!g_mode_ring)draw_cuts();else draw_score();
}
} // namespace seance::platform::ui3ds
