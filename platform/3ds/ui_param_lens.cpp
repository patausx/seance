#include "ui_param_lens.h"
#include "ui_module_rand.h"
#include "sigil_surface.h"
#include "strn_focus.h"
#include "ui_module_focus_internal.h"
#include "ui_state.h"
#include "ui_theme.h"
#include "menu_style.h"
#include "editor_style.h"
#include "fm_context.h"
#include "module_inspector.h"
#include "play_readout.h"
#include "../../core/engine/tilt_eq.h"
#include "../../core/ui/draw.h"
#include <cstdio>
#include <cstring>
#include <algorithm>

namespace seance::platform::ui3ds {
namespace {
bool opened = false, held_entry = false, quarantine = false;
int chain = -1, slot = -1, selected = 0;
uintptr_t opened_unit = 0;
enum class Touch { None, Base, Depth };
Touch touch = Touch::None;
ParamInspector detail;
int count = 0;
constexpr int X = 24, W = 272;
// Every module shares a no-jump lens gesture. Musical units/neutral values stay
// parameter-specific; the individual module compositions/drag laws are untouched.
bool drawn_quiet=false; // frozen bypass/chain mute, never read the engine in draw
bool fine_held = false, drawn_fine = false, base_busy = false;
struct EditLaw { int fine_divisor=8; q15 neutral=0; const char* label="min"; } drawn_law;
bool modern_tilt = true, legacy_tilt_zero = false;
bool modern_play = true, quantize_play = false;
PlayUnit::PitchMode play_pitch_mode=PlayUnit::PitchMode::Legacy;
bool modern_reverb = true; // frozen with detail; old projects keep feedback readouts
int relative_x = 0, relative_last_x = 0, relative_param = -1, relative_divisor = 1;
q15 relative_value = 0;
uintptr_t relative_unit = 0;

bool context() {
    return g_eng && g_audio && g_view == View::Chains &&
           g_overlay == Overlay::Editor && !g_seq_lock_armed &&
           g_edit_chain >= 0 && g_edit_chain < NUM_CHAINS &&
           g_edit_slot >= 0 && g_edit_slot < UNITS_PER_CHAIN;
}
Unit* unit() { return context() ? g_eng->chains[g_edit_chain].units[g_edit_slot] : nullptr; }
#include "module_rand_controller.inc"
int visible_count(const Unit& u) {
    // The twelfth historical GRAN slot was padding, not an editable legacy SCAN.
    return u.kind==UnitKind::Grain && !static_cast<const GrainUnit&>(u).clean ? GrainUnit::P_SCAN : u.num_params;
}
bool lens_param_available(const Unit& u,int p) {
    return u.kind!=UnitKind::Scratch || ScratchUnit::parameter_available(static_cast<const ScratchUnit&>(u).mode,p);
}
int next_param(const Unit& u,int delta) {
    // REC/STOP are commands, not the dormant legacy P_REC field. Only the two
    // parameters actually consumed by REC's DSP belong in this modulation panel.
    if(u.kind==UnitKind::Recorder)return selected==RecUnit::P_LEN?RecUnit::P_MON:RecUnit::P_LEN;
    const int n=visible_count(u);int p=selected;
    for(int i=0;i<n;++i){p=(p+n+delta)%n;if(lens_param_available(u,p))return p;}
    return selected;
}
bool recorder_busy(const Unit& u) {
    if(u.kind==UnitKind::Paul)return g_eng->recording_active_or_pending();
    return u.kind==UnitKind::Recorder && (static_cast<const RecUnit&>(u).writer_open() ||
        g_eng->recorder_record_pending(g_edit_chain,g_edit_slot) ||
        g_eng->chain_record_pending_start(g_edit_chain) || g_eng->chain_record_pending_stop(g_edit_chain));
}
EditLaw edit_law(const Unit& u,int p) {
    EditLaw law;
    if(u.params[p].bipolar)law.label="zero";
    // Pitch/rate rails need more travel than mix/amount; SHIFT spans +/-1600 Hz.
    if((u.kind==UnitKind::Osc && p==OscUnit::P_PITCH) ||
       (u.kind==UnitKind::Fm && (p==FmUnit::P_PITCH || p==FmUnit::P_RATIO)) ||
       (u.kind==UnitKind::Bird && p==BirdUnit::P_PITCH) ||
       (u.kind==UnitKind::Inst && p==InstUnit::P_NOTE) ||
       (u.kind==UnitKind::SamplePlayer && p==PlayUnit::P_NOTE) ||
       (u.kind==UnitKind::Ghost && p==GhostUnit::P_TONE) ||
       (u.kind==UnitKind::Filter && p==FilterUnit::P_CUTOFF) ||
       (u.kind==UnitKind::Ring && p==RingUnit::P_FREQ) ||
       (u.kind==UnitKind::Tilt && p==TiltUnit::P_FREQ) ||
       (u.kind==UnitKind::Motor && p==MotorUnit::P_RPM))law.fine_divisor=16;
    if(u.kind==UnitKind::Tilt){
        law.fine_divisor=16;
        if(p==TiltUnit::P_TRIM){law.neutral=tilt_eq::TRIM_UNITY;law.label="unity";}
    }
    if(u.kind==UnitKind::Bitcrush && static_cast<const BitUnit&>(u).modern()){
        if(p==BitUnit::P_RATE||p==BitUnit::P_TONE||p==BitUnit::P_DRIFT)law.fine_divisor=16;
        if(p==BitUnit::P_GAIN){law.neutral=26214;law.label="unity";}
        if(p==BitUnit::P_TONE){law.neutral=32767;law.label="off";}
    }
    if(u.kind==UnitKind::Strain && static_cast<const StrainUnit&>(u).modern()){
        if(p==StrainUnit::P_RATE||p==StrainUnit::P_BIAS)law.fine_divisor=16;
        if(p==StrainUnit::P_TRIM){law.neutral=StrainUnit::DEFAULT_TRIM;law.label="unity";}
    }
    if(u.kind==UnitKind::Fold){
        if(p==FoldUnit::P_DRIVE){law.neutral=0;law.label="unity";law.fine_divisor=16;}
        if(p==FoldUnit::P_TRIM){law.neutral=32767;law.label="unity";}
        if(p==FoldUnit::P_BIAS)law.fine_divisor=16;
        if(static_cast<const FoldUnit&>(u).modern()&&p==FoldUnit::P_TONE){law.neutral=32767;law.label="off";law.fine_divisor=16;}
    }
    if(u.kind==UnitKind::Ring&&static_cast<const RingUnit&>(u).modern()&&
       (p==RingUnit::P_NOTE||p==RingUnit::P_FINE))law.fine_divisor=16;
    if(u.kind==UnitKind::Paul){if(p==PaulUnit::P_PITCH)law.fine_divisor=16;
        if(p==PaulUnit::P_GAIN){law.neutral=32767;law.label="unity";}}
    if(u.kind==UnitKind::Scratch && p==ScratchUnit::P_GAIN){law.neutral=32767;law.label="unity";}
    if(u.kind==UnitKind::Shift && p==ShiftUnit::P_SHIFT)law.fine_divisor=32;
    if((u.kind==UnitKind::SamplePlayer && p==PlayUnit::P_SPEED) ||
       (u.kind==UnitKind::Grain && p==GrainUnit::P_PITCH) ||
       (u.kind==UnitKind::Fract && p==FractUnit::P_SPEED)) {
        law.neutral=8192;law.label="unity";law.fine_divisor=16;
    } else if(u.kind==UnitKind::MicIn && p==MicUnit::P_GAIN) {
        law.neutral=8192;law.label="unity";
    } else if((u.kind==UnitKind::Vca && p==VcaUnit::P_GAIN) ||
              (u.kind==UnitKind::SamplePlayer && p==PlayUnit::P_GAIN) ||
              (u.kind==UnitKind::Grain && p==GrainUnit::P_GAIN)) {
        law.neutral=32767;law.label="unity";
    }
    return law;
}
int percent(q15 value) { return (int32_t)value * 100 / 32767; }
int position(q15 value, bool bipolar, int x, int width) {
    int32_t v = value;
    if (v < (bipolar ? -32767 : 0)) v = bipolar ? -32767 : 0;
    if (v > 32767) v = 32767;
    return x + (v + (bipolar ? 32767 : 0)) * (width - 1) / (bipolar ? 65534 : 32767);
}
const char* response_name(EnvResponse response) {
    switch (response) {
        case EnvResponse::Body: return "body";
        case EnvResponse::Slow: return "slow";
        default: return "fast";
    }
}
ModSource lens_source(int index){return index==0?ModSource::None:index==9?ModSource::EnvFollow:sigil_mod_source(index-1);}
void label(int x, int y, const char* text, u32 color) { ui::text(x, y, text, color); }
void change_value(int x) {
    if (!context() || !opened || quarantine || selected < 0 || touch==Touch::None) return;
    platform::AudioOut::Guard guard(*g_audio);
    Unit* u = unit();
    if (!u || (uintptr_t)u != opened_unit || selected >= visible_count(*u) ||
        (u->kind==UnitKind::Recorder && selected==RecUnit::P_REC)) return;
    if(touch==Touch::Base && recorder_busy(*u)){touch=Touch::None;return;}
    Param& p = u->params[selected];
    if ((uintptr_t)u != relative_unit || selected != relative_param) return;
    const int divisor=fine_held?(touch==Touch::Depth?8:edit_law(*u,selected).fine_divisor):1;
    if(divisor!=relative_divisor) {
        relative_x=relative_last_x;
        relative_value=touch==Touch::Depth?p.mod_depth:p.base;
        relative_divisor=divisor; // switching fine at a stationary stylus is inert
    }
    if(x==relative_last_x)return;
    relative_last_x=x;
    auto value_at = [&](bool bipolar) -> q15 {
        int v = relative_value + (x - relative_x) * (bipolar ? 65534 : 32767) / ((W - 1)*relative_divisor);
        if (v < (bipolar ? -32767 : 0)) v = bipolar ? -32767 : 0;
        if (v > 32767) v = 32767;
        return (q15)v;
    };
    if (touch == Touch::Base) {
        const q15 raw = value_at(p.bipolar);
        const q15 next = u->kind==UnitKind::SamplePlayer&&selected==PlayUnit::P_NOTE && raw!=relative_value ?
            musical_note::encode(musical_note::decode(raw)) : raw;
        if (p.base != next) {
            p.base = next;
            mark_project_mutated();
        }
    } else if (touch == Touch::Depth) {
        const q15 next = value_at(true);
        if (p.mod_depth != next) { p.mod_depth = next; mark_project_mutated(); }
    }
}
void select_module(const Unit& u,int param) {
    if(u.kind==UnitKind::Fm)fm_focus_select(param);
    else if(u.kind==UnitKind::Reverb)reverb_focus_select(param);
    else if(u.kind==UnitKind::Ghost)ghost_focus_select(param);
    else if(u.kind==UnitKind::Stranular)stranular_focus_select(param);
    else module_inspector_select(u.kind,param);
}
bool open_selected(bool held) {
    if (!context() || recorder_focus_confirming()) return false;
    platform::AudioOut::Guard guard(*g_audio);
    Unit* u = unit();
    if (!u || !u->num_params) return false;
    random_target = random_focus.matches(u);
    random_clear_press();
    opened = true; held_entry = held; quarantine = true;
    fine_held=false;
    opened_unit = (uintptr_t)u;
    chain = g_edit_chain; slot = g_edit_slot;
    selected = u->kind == UnitKind::Fm ? fm_focus_selected() :
               u->kind == UnitKind::Stranular ? stranular_focus_selected() :
               u->kind == UnitKind::Reverb ? reverb_focus_selected() :
               u->kind == UnitKind::Ghost ? ghost_focus_selected() :
               module_inspector_supports(u->kind) ? module_inspector_selected(u->kind) : 0;
    if(selected<0 || selected>=visible_count(*u) || !lens_param_available(*u,selected)) selected=0;
    if(u->kind==UnitKind::Recorder && selected!=RecUnit::P_LEN && selected!=RecUnit::P_MON) selected=RecUnit::P_LEN;
    select_module(*u,selected);
    // Opening never edits a parameter or converts a legacy module.
    paul_focus_release();
    scratch_focus_release();
    play_focus_release(); osc_focus_release(); fm_focus_release(); vca_focus_release();
    stranular_focus_release(); reverb_focus_release(); ghost_focus_release();
    chorus_focus_release(); delay_focus_release(); filter_focus_release(); fold_focus_release();
    bit_focus_release(); tilt_focus_release(); ring_focus_release(); strain_focus_release();
    bird_focus_release();
    shift_focus_release();
    inst_focus_release();
    glitch_focus_release();
    arp_focus_release();
    mouth_focus_release();
    comp_focus_release();
    fract_focus_release();
    grain_focus_release(); mic_focus_release(); recorder_focus_release();
    noise_focus_release(); motor_focus_release();
    touch = Touch::None; g_drag = Drag::None;
    return true;
}
}

bool module_rand_touch_down(int x,int y) {
    if(opened || quarantine || !context() || recorder_focus_confirming())return false;
    platform::AudioOut::Guard guard(*g_audio);
    Unit* u=unit();
    random_clear_press();
    if(!u || !random_hit(x,y,random_anchor(u->kind))) {random_focus={};return false;}
    random_focus=random_press=random_owner(*u);
    random_touch=true;random_armed=random_available(*u);
    random_press_x=random_anchor(u->kind);
    return true;
}
bool module_rand_touch_drag(int x,int y) {
    if(!random_touch)return false;
    if(!random_hit(x,y,random_press_x))random_armed=false; // leaving cancels, no rearm
    return true;
}
bool module_rand_touch_release() {
    if(!random_touch)return false;
    if(context() && !opened && !quarantine) {
        platform::AudioOut::Guard guard(*g_audio);
        Unit* u=unit();
        if(random_armed && random_press.matches(u) && g_eng->randomize_module(g_edit_chain,g_edit_slot))
            random_acknowledge_change();
    }
    random_clear_press();return true;
}
void module_rand_cancel() {random_clear_press();random_focus={};}
void module_rand_poll() {
    if(!g_eng || !g_audio){random_seen_engine=nullptr;random_seen_revision=0;return;}
    platform::AudioOut::Guard guard(*g_audio);
    const uint64_t revision=g_eng->module_random_revision();
    if(random_seen_engine==g_eng && revision>random_seen_revision)mark_project_mutated();
    random_seen_engine=g_eng;random_seen_revision=revision;
}
void module_rand_rebase() {
    module_rand_cancel();param_lens_cancel();random_frame={};
    random_seen_engine=g_eng;
    random_seen_revision=g_eng?g_eng->module_random_revision():0;
}
void module_rand_draw(const sigil_surface::Painter& painter) {
    if(!random_frame.valid || random_frame.x<0)return;
    namespace m=menu_style;
    const bool active=random_frame.available && (random_frame.focused || random_frame.pressed || random_frame.source!=ModSource::None);
    painter.text(random_frame.x,editor_style::kHeaderTextY,"rand",active?m::spot():m::ghost());
}
void module_rand_draw() {module_rand_draw(sigil_surface::Painter{});}

bool module_focus_open_mod() {return open_selected(false);}
bool param_lens_is_open() { return opened; }
void param_lens_cancel() {
    opened = held_entry = quarantine = random_target = false;
    random_clear_press();
    fine_held=false;
    opened_unit = 0;
    touch = Touch::None;
    relative_unit = 0;
    detail = {};
    count = 0;
}

bool param_lens_input(u32& down, u32 held, u32 up, bool blocked) {
    // Navigation/key edges cancel an owned module gesture, including a pending
    // release-only button. Returning with the stylus held must never re-arm it.
    if (blocked || (down & ~(KEY_TOUCH | KEY_ZL))) {
        delay_focus_release(); chorus_focus_release(); ghost_focus_release();
    }
    constexpr u32 directions = KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT;
    if(blocked || !context())module_rand_cancel();
    else if(down & ~(KEY_TOUCH|KEY_ZL)) {
        random_clear_press();
        if(!(down&KEY_L))random_focus={};
    }
    if (!(held & KEY_TOUCH)) quarantine = false;
    if (!opened) {
        // A fresh L edge is required: returning from navigation while still
        // holding L must not open a second panel on a different module.
        if (!blocked && (down & KEY_L) && (held & KEY_L) &&
            !(held & (KEY_R | directions)) && !(down & (KEY_B | KEY_SELECT | KEY_START | KEY_Y)) &&
            open_selected(true)) {
            quarantine = (held & KEY_TOUCH) != 0;
            down = 0; // screenshot edge was already handled by main
            return true; // even a simultaneous touch must lift first
        }
        if (quarantine) down &= ~KEY_TOUCH;
        return quarantine;
    }
    auto close = [&]() {
        param_lens_cancel();
        quarantine = (held & KEY_TOUCH) != 0;
        g_drag = Drag::None;
    };
    bool same_unit = false;
    if (context()) {
        platform::AudioOut::Guard guard(*g_audio);
        same_unit = (uintptr_t)unit() == opened_unit;
    }
    if (!same_unit || blocked || chain != g_edit_chain || slot != g_edit_slot ||
        (down & (KEY_SELECT | KEY_START | KEY_Y))) {
        close();
        down &= KEY_SELECT | KEY_START | KEY_Y;
        return true;
    }
    if (down & KEY_B) {
        close(); down = 0; // close panel, never the card too
        return true;
    }
    if (held_entry && (!(held & KEY_L) || (up & KEY_L))) {
        close(); down = 0;
        return true; // completed edits persist; remaining touch stays quarantined
    }
    if (held_entry && !(held & KEY_R) && (down & directions)) {
        close(); down &= directions;
        return true; // let the existing main L+arrows navigation own this chord
    }
    // Latched entry retains hardware parameter navigation; L is inert, not TEMP.
    const u32 pressed = down;
    fine_held=(held&KEY_R)!=0;
    down &= ~(KEY_L | KEY_R | KEY_X | KEY_A | directions);
    if (!held_entry && !random_target && (pressed & (KEY_LEFT | KEY_RIGHT))) {
        platform::AudioOut::Guard guard(*g_audio);
        Unit* u = unit();
        if (u && u->num_params) {
            selected = next_param(*u,(pressed & KEY_LEFT)?-1:1);
            select_module(*u,selected);
        }
        touch = Touch::None;
        quarantine = (held & KEY_TOUCH) != 0;
        return true;
    }
    return quarantine;
}

void param_lens_capture() {
    // Validate/synchronize selection before ANY ordinary module capture. The
    // layer no longer substitutes a diagnostic page for those frozen surfaces.
    detail = {}; count = 0;
    if (opened) {
        if (!context() || chain != g_edit_chain || slot != g_edit_slot) {
            param_lens_cancel();
        } else {
            bool invalid=false;
            {
                platform::AudioOut::Guard guard(*g_audio);
                Unit* u = unit();
                invalid=!u || (uintptr_t)u != opened_unit || !u->num_params;
                if (!invalid) {
                    count = visible_count(*u);
                    drawn_fine=fine_held; base_busy=recorder_busy(*u);
                    drawn_quiet=u->bypass || g_eng->chains[chain].mute;
                    modern_tilt=u->kind!=UnitKind::Tilt || static_cast<const TiltUnit*>(u)->modern();
                    legacy_tilt_zero=!modern_tilt && u->params[TiltUnit::P_RANGE].base==0;
                    modern_play=u->kind!=UnitKind::SamplePlayer || static_cast<const PlayUnit*>(u)->modern();
                    play_pitch_mode=u->kind==UnitKind::SamplePlayer?static_cast<const PlayUnit*>(u)->pitch_mode():PlayUnit::PitchMode::Legacy;
                    modern_reverb=u->kind!=UnitKind::Reverb || static_cast<const ReverbUnit*>(u)->modern();
                    quantize_play=u->kind==UnitKind::SamplePlayer && static_cast<const PlayUnit*>(u)->pitch_quantize;
                    if (selected < 0 || selected >= count || !lens_param_available(*u,selected)) selected = 0;
                    if(u->kind==UnitKind::Recorder && selected!=RecUnit::P_LEN && selected!=RecUnit::P_MON)selected=RecUnit::P_LEN;
                    drawn_law=edit_law(*u,selected);
                    select_module(*u,selected);
                    g_eng->capture_param_inspector(chain,slot,selected,detail);
                }
            }
            if(invalid)param_lens_cancel();
        }
    }
    // Each display capture owns its existing guard; response/model work stays
    // outside the lens guard. These are the same ordinary module DTOs, not an
    // extra screenshot/cache/texture or a second engine snapshot.
    random_capture();
    reverb_focus_capture();
    ghost_focus_capture();
    chorus_focus_capture();
    delay_focus_capture();
    module_inspector_capture();
}

bool param_lens_touch_down(int x, int y) {
    if (quarantine) return true;
    if (!opened) {
        if (y < 228 || y >= 240 || x < 214 || x >= 320) return false;
        // PLAY/COMP own their footers; held-L still opens this shared layer.
        if (context()) {
            platform::AudioOut::Guard guard(*g_audio);
            const Unit* u = unit();
            if (u && (u->kind == UnitKind::SamplePlayer || u->kind == UnitKind::Comp || u->kind == UnitKind::Paul || u->kind == UnitKind::Scratch)) return false;
        }
        return open_selected(false);
    }
    touch = Touch::None;
    if (!context()) return true;
    platform::AudioOut::Guard guard(*g_audio);
    Unit* u = unit();
    if (!u || (uintptr_t)u != opened_unit || selected < 0 || selected >= visible_count(*u)) return true;
    if(random_hit(x,y,random_anchor(u->kind)) || (!random_target && hit(x,y,210,46,48,28))) {
        random_target=true;random_focus=random_owner(*u);return true;
    }
    if(random_target) {
        if(hit(x,y,240,46,66,28)) {random_target=false;random_focus={};return true;}
        if(x>=8 && x<311 && y>=177 && y<201) {
            const int i=(x-8)/33;
            if(i<9 && g_eng->set_module_random_source(g_edit_chain,g_edit_slot,i?sigil_mod_source(i-1):ModSource::None))
                mark_project_mutated();
        } else if(hit(x,y,16,134,120,32)) {
            if(g_eng->randomize_module(g_edit_chain,g_edit_slot))random_acknowledge_change();
        } else if(hit(x,y,236,134,68,32)) {
            if(g_eng->undo_module_randomize(g_edit_chain,g_edit_slot))random_acknowledge_change();
        }
        return true;
    }
    Param& p = u->params[selected];
    if (hit(x,y,260,46,46,28)) {
        if(p.src!=ModSource::None){p.mod_enabled=!p.mod_enabled;mark_project_mutated();}
        return true;
    }
    auto start = [&](Touch which) {
        if(which==Touch::Base && recorder_busy(*u))return;
        touch = which;
        relative_x = relative_last_x = x; relative_unit = (uintptr_t)u; relative_param = selected;
        relative_divisor=fine_held?(which==Touch::Depth?8:edit_law(*u,selected).fine_divisor):1;
        relative_value = which == Touch::Depth ? p.mod_depth : p.base;
    };
    if (hit(x,y,14,74,292,43)) { start(Touch::Base); return true; }
    if (hit(x,y,14,119,292,47)) { start(Touch::Depth); return true; }
    if (x >= 8 && x < 318 && y >= 177 && y < 201) {
        const ModSource next = lens_source((x - 8) / 31);
        if (p.src != next || !p.mod_enabled) { p.src = next; p.mod_enabled=true; mark_project_mutated(); }
    } else if (p.src == ModSource::EnvFollow && x >= 84 && x < 212 && y >= 201 && y < 226) {
        if (g_eng->set_env_follow_source((x - 84) / 32)) mark_project_mutated();
    } else if (p.src == ModSource::EnvFollow && x >= 224 && x < 314 && y >= 201 && y < 226) {
        const EnvResponse next = (EnvResponse)(((int)g_eng->env_follow_response() + 1) % 3);
        if (g_eng->set_env_follow_response(next)) mark_project_mutated();
    }
    // The visible module is only context. Empty space and all retired touch
    // arrows/reset/fine/footer zones consume input without a hidden action.
    return true;
}
bool param_lens_touch_drag(int x, int) {
    if (!opened) return false;
    change_value(x);
    return true;
}
void param_lens_touch_release() { touch = Touch::None; quarantine = false; }

void param_lens_draw_entry() {
    if (context()) {
        const Unit* u = unit();
        if (u && (u->kind == UnitKind::Fm || u->kind == UnitKind::Reverb || u->kind == UnitKind::Ghost)) {
            menu_style::paper(320, 228, 240);
            ui::text(12, 231, "b back", menu_style::ghost());
            ui::text(242, 231, "l: mod >", menu_style::spot());
            return;
        }
    }
    ui::rect(0, 228, 320, 12, col_text());
    label(4, 230, "b back", col_bg());
    label(220, 230, "l: mod >", col_bg());
}

void param_lens_draw_bottom() {
    if (!opened || !detail.valid) return;
    if(random_target && random_frame.valid){random_draw_panel();return;}
    namespace m = menu_style;
    const UnitKind kind=UnitKind(detail.kind);
    const bool route_on=detail.source!=ModSource::None && detail.mod_enabled;
    const bool env=detail.source==ModSource::EnvFollow;
    const int bottom=env?226:205;
    // Select the lower paper coordinate space without painting it. The editor
    // has already drawn its ordinary frozen body. Disjoint veils avoid applying
    // both alphas to the panel; native paper/gradient and theme fades stay exact.
    m::paper(320,0,0);
    m::paper_rect(0,0,320,44,170);
    m::paper_rect(0,44,8,bottom-44,170);
    m::paper_rect(312,44,8,bottom-44,170);
    m::paper_rect(0,bottom,320,240-bottom,170);
    m::paper_rect(8,44,304,bottom-44,234);
    const u32 link_ink=route_on?m::spot():m::ghost();
    label(218,55,"rand",m::ghost());
    label(272,55,"link",link_ink);
    if(detail.source!=ModSource::None)ui::rect(272,66,22,1,link_ink);

    char value[24];
    if(kind==UnitKind::Osc)osc_focus_value_text(value,sizeof value,selected,detail.base);
    else if(kind==UnitKind::Fm) {
        char number[20];fm_context_value_text(number,sizeof number,selected,detail.base);
        std::snprintf(value,sizeof value,"%s%s",number,
            selected==FmUnit::P_RATIO||selected==FmUnit::P_RATIO_FINE?"x":"");
    } else if(kind==UnitKind::Reverb)reverb_focus_value_text(value,sizeof value,selected,detail.base,modern_reverb);
    else if(kind==UnitKind::Ghost)ghost_focus_value_text(value,sizeof value,selected,detail.base);
    else if(kind==UnitKind::Vca)vca_focus_value_text(value,sizeof value,selected,detail.base);
    else if(kind==UnitKind::Stranular)stranular_focus_value_text(value,sizeof value,selected,detail.base);
    else if(kind==UnitKind::SamplePlayer)play_readout::value_text(value,sizeof value,selected,detail.base,modern_play,quantize_play,play_pitch_mode);
    else if(kind==UnitKind::Tilt)tilt_focus_value_text(value,sizeof value,selected,detail.base,2);
    else module_inspector_value_text(value,sizeof value,kind,selected,detail.base,
        kind!=UnitKind::Vca||vca_focus_flexible(),kind==UnitKind::Delay&&delay_focus_modern(),
        modern_tilt,legacy_tilt_zero,modern_play,quantize_play);
    label(X,80,base_busy?"base / busy":"base",!base_busy&&drawn_fine?m::spot():m::ghost());
    label(296-ui::text_w(value),80,value,base_busy?m::ghost():m::ink());
    module_fader::horizontal(X,X+W-1,102,position(detail.base,detail.bipolar,X,W),
        position(detail.effective,detail.bipolar,X,W),{route_on,base_busy,drawn_quiet},
        position(drawn_law.neutral,detail.bipolar,X,W));
    label(X,125,"depth",drawn_fine?m::spot():m::ghost());
    std::snprintf(value,sizeof value,"%+d%%",percent(detail.depth));
    label(296-ui::text_w(value),125,value,link_ink);
    const int depth=position(detail.depth,true,X,W);
    module_fader::horizontal(X,X+W-1,147,depth,depth,{},position(0,true,X,W));
    auto word=[&](int x,int y,int w,int h,const char* text,bool on=false) {
        const u32 color=on && detail.mod_enabled?m::spot():m::ghost();
        const int left=x+(w-ui::text_w(text))/2;
        label(left,y+(h-8)/2,text,color);
        if(on)ui::rect(left,y+h-2,std::max(1,ui::text_w(text)-2),1,color);
    };
    static const char* sources[]={"off","a","b","c","d","e","f","g","h","env"};
    for(int i=0;i<10;++i)word(8+i*31,177,29,23,sources[i],detail.source==lens_source(i));
    if(env) {
        label(24,208,"env all",m::ghost()); // the existing global follower, not per-route chains
        for(int i=0;i<4;++i){char n[2]={(char)('1'+i),0};word(84+i*32,201,30,24,n,detail.env_chain==i);}
        word(224,201,90,24,response_name(detail.env_response),true);
    }
}

bool param_lens_draw_top() {
    if (!opened || !detail.valid) return false;
    // Compatibility entry for existing callers/captures. The ordinary selected
    // module owns the upper surface; there is no second modulation dashboard.
    return fm_context_draw_top() || module_inspector_draw_top() ||
        stranular_focus_draw_top() || reverb_focus_draw_top() || ghost_focus_draw_top();
}
}
