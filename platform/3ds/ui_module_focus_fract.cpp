#include "ui_module_focus_internal.h"
#include "ui_state.h"
#include "ui_theme.h"
#include "ui_param_lens.h"
#include "ui_nav.h"
#include "fract_window_ui.h"
#include "editor_style.h"
#include "module_heading.h"
#include "audio_out.h"
#include "../../core/ui/draw.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <type_traits>

namespace seance::platform::ui3ds {
namespace {
namespace m = editor_style;
namespace w = fract_window;
using F = FractUnit;
constexpr const char* NAMES[F::P_COUNT] = {
    "size", "length", "chance", "damage", "speed", "mix", "clock", "hold"
};
struct Frame {
    uint16_t mod_links=0;
    bool valid=false, bypass=false, mute=false, lock=false, lock_full=false, lens=false, manual_owned=false;
    int8_t chain=0, slot=0, selected=0;
    int32_t bpm=120*65536;
    q15 edit[F::P_COUNT]={}, live[F::P_COUNT]={};
    char values[F::P_COUNT][20]={};
    F::Visual visual{};
    ParamInspector detail{};
    q15 stored_damage=0;
    char status[32]={};
} frame;
static_assert(sizeof(Frame)<=1024 && std::is_trivially_copyable<Frame>::value,
              "FRACT frozen bounded frame, no live engine/PCM in draw");
int selected=0, drag_param=-1, origin_x=0;
int origin_chain=-1, origin_slot=-1, origin_step=-1, origin_lock_chain=-1;
bool origin_locked=false;
int origin_view=-1, origin_bank=-1;
platform::AudioOut* origin_audio=nullptr;
uintptr_t origin_unit=0, manual_unit=0;
Engine* origin_engine=nullptr;
Engine* manual_engine=nullptr;
q15 origin_value=0;
bool window_moved=false;

bool context() {
    return g_eng && g_audio && g_overlay==Overlay::Editor &&
        (g_view==View::Chains || (g_view==View::Sequencer && g_seq_lock_armed)) &&
        g_edit_chain>=0 && g_edit_chain<NUM_CHAINS && g_edit_slot>=0 && g_edit_slot<UNITS_PER_CHAIN;
}
bool lock_context() {
    return g_seq_lock_armed && g_seq_lock_chain==g_edit_chain &&
        g_seq_lock_step>=0 && g_seq_lock_step<SEQ_STEPS;
}
q15 edit_value(const F& u,int p) {
    return lock_context()?seq_display_value(g_edit_slot,p,u.params[p].base):u.params[p].base;
}
int clamp(int v,int p) { return std::max(p==F::P_SPEED?-32767:0,std::min(32767,v)); }
void release_locked() {
    // Do not dereference a retained pointer. The module can have moved or been deleted.
    // A moved module may now belong to a different live Engine. Scan ONLY that
    // guarded engine; never follow the old engine or an unowned retained unit.
    if(manual_unit && g_eng) {
        for(auto& chain:g_eng->chains)for(auto* u:chain.units)
            if(uintptr_t(u)==manual_unit && u->kind==UnitKind::Fract)
                static_cast<F*>(u)->set_manual_grab(false);
    }
    manual_unit=origin_unit=0;manual_engine=origin_engine=nullptr;drag_param=-1;window_moved=false;
}
void cancel_locked() {
    release_locked();
    if(g_drag==Drag::FractGrab || g_drag==Drag::FractParam)g_drag=Drag::None;
}
bool owns(const F& u) {
    return g_eng==origin_engine && g_audio==origin_audio && origin_unit==uintptr_t(&u) &&
        origin_view==int(g_view) && origin_bank==g_eng->active_pattern &&
        origin_chain==g_edit_chain && origin_slot==g_edit_slot &&
        g_eng->chains[g_edit_chain].units[g_edit_slot]==&u && origin_locked==g_seq_lock_armed &&
        (!origin_locked || (lock_context() && origin_step==g_seq_lock_step && origin_lock_chain==g_seq_lock_chain));
}
void remember(F& u,int tx,int p) {
    drag_param=p;origin_x=tx;origin_value=edit_value(u,p);origin_unit=uintptr_t(&u);origin_engine=g_eng;
    origin_chain=g_edit_chain;origin_slot=g_edit_slot;origin_locked=g_seq_lock_armed;
    origin_step=g_seq_lock_step;origin_lock_chain=g_seq_lock_chain;
    origin_view=int(g_view);origin_bank=g_eng->active_pattern;origin_audio=g_audio;
}
void change(F& u,int p,q15 value) {
    if(value==edit_value(u,p))return;
    if(g_seq_lock_armed) { if(lock_context())seq_record_lock(g_edit_slot,p,value); }
    else {u.params[p].base=value;mark_project_mutated();}
}
void format(char* out,size_t size,int p,q15 value,bool sync,int32_t bpm) {
    if(!out || !size)return;
    if(p<0 || p>=F::P_COUNT){std::snprintf(out,size,"--");return;}
    const q15 v=q15(clamp(value,p));
    if(p==F::P_SIZE && sync) {
        // A trailing * identifies a nominal division limited by the one-second bank.
        const uint64_t beat=(uint64_t(ENGINE_SR)*60*65536+uint32_t(bpm)/2)/uint32_t(bpm);
        const unsigned division=uint32_t(v)*7u/32768u;
        const bool capped=((beat<<division)/32)>F::MAX_CAPTURE;
        std::snprintf(out,size,"%s%s",F::size_label(v),capped?"*":"");
    } else if(p==F::P_SIZE || p==F::P_LENGTH) {
        const uint32_t frames=p==F::P_SIZE?F::size_frames(v,sync,bpm):F::length_frames(v,bpm);
        std::snprintf(out,size,"%lums",static_cast<unsigned long>((frames+16)/32));
    } else if(p==F::P_SPEED) {
        const int speed=F::speed_q16(v);
        const unsigned v100=unsigned((int64_t(std::abs(speed))*100+32768)/65536);
        std::snprintf(out,size,"%s%u.%02ux",speed<0?"-":"",v100/100,v100%100);
    } else if(p==F::P_SYNC)std::snprintf(out,size,"%s",v>16383?"sync":"free");
    else if(p==F::P_HOLD)std::snprintf(out,size,"%s",v>16383?"on":"off");
    else std::snprintf(out,size,"%u%%",unsigned((uint32_t(v)*100+16383)/32767));
}
void right(int x,int y,const char* text,u32 color) {ui::text(x-ui::text_w(text),y,text,color);}
int rail_x(q15 v,int p,int x,int width) {
    const bool bi=p==F::P_SPEED;
    return x+(clamp(v,p)+(bi?32767:0))*(width-1)/(bi?65534:32767);
}
void bracket(int x,int y0,int y1,bool left,u32 color) {
    ui::line(x,y0,x,y1,color);
    ui::line(x,y0,x+(left?6:-6),y0,color);
    ui::line(x,y1,x+(left?6:-6),y1,color);
}
void envelope(const q15 (&low)[F::ENVELOPE_BINS],const q15 (&high)[F::ENVELOPE_BINS],
              const w::Rect& box,int chosen_right,bool active) {
    const int mid=box.y+box.h/2,scale=box.h/2-2;
    for(int col=0;col<box.w;++col) {
        const unsigned bin=unsigned(col)*F::ENVELOPE_BINS/unsigned(box.w);
        const int x=box.x+col,top=mid-int(high[bin])*scale/32768,bottom=mid-int(low[bin])*scale/32768;
        ui::line(x,top,x,bottom,active && x<=chosen_right?m::spot():m::ghost());
    }
}
void wave(const q15 (&points)[48],const w::Rect& box,int chosen_right,bool active) {
    // Only the 48 published PCM points. No tiling, invented peaks, oscillator,
    // time-derived phase or resampling of a different source in the painter.
    const int mid=box.y+box.h/2,scale=box.h/2-2;
    for(int n=1;n<48;++n) {
        const int x0=box.x+(n-1)*(box.w-1)/47,x1=box.x+n*(box.w-1)/47;
        const int y0=mid-int(points[n-1])*scale/32768,y1=mid-int(points[n])*scale/32768;
        if(active && x0<chosen_right && x1>chosen_right) {
            const int cut_y=y0+(y1-y0)*(chosen_right-x0)/(x1-x0);
            ui::line(x0,y0,chosen_right,cut_y,m::spot());
            ui::line(chosen_right,cut_y,x1,y1,m::ghost());
        } else ui::line(x0,y0,x1,y1,active && x1<=chosen_right?m::spot():m::ghost());
    }
}
bool reader_active() {
    return frame.visual.active && frame.visual.reader_frames && !frame.bypass && !frame.mute;
}
const char* status() {
    const auto& v=frame.visual;
    if(frame.bypass)return "bypassed / dry";
    if(frame.mute)return "chain muted";
    if(frame.lock)return "p-lock / grab disabled";
    if(!v.reader_frames && (v.manual || v.held))return "waiting for capture";
    if(!v.history_frames)return "waiting for audio";
    if(!reader_active())return "live / dry";
    if(!v.wet)return "reader / dry";
    if(v.manual) {
        const bool squeezed=v.window_active && v.reader_frames<v.loop_frames;
        if(frame.manual_owned)return squeezed?"touch / temporary":"touch / full window";
        return squeezed?"manual / temporary":"manual / full window";
    }
    if(v.held)return "held / no squeeze";
    return "reader / repeat"; // Visual does not distinguish automatic ownership from a wet release tail
}
struct Labels {
    char history_label[24]={}, reader_label[32]={}, damage_label[24]={};
    char selected_info[64]={}, mod_info[40]={};
};
void format_labels(Labels& labels) {
    const auto& v=frame.visual;
    if(v.history_frames)std::snprintf(labels.history_label,sizeof labels.history_label,"%s %lums",
        v.capture_frames?"captured":"recent",static_cast<unsigned long>((v.history_frames+16)/32));
    else std::snprintf(labels.history_label,sizeof labels.history_label,"no history");
    if(v.reader_frames && v.active)std::snprintf(labels.reader_label,sizeof labels.reader_label,"reader %lums @ 0ms",
        static_cast<unsigned long>((v.reader_frames+16)/32));
    else std::snprintf(labels.reader_label,sizeof labels.reader_label,"reader off");
    char damage[8];format(damage,sizeof damage,F::P_DAMAGE,frame.stored_damage,false,frame.bpm);
    std::snprintf(labels.damage_label,sizeof labels.damage_label,"saved damage %s",damage);
    const int p=frame.selected;
    char control[20],live[20];
    format(control,sizeof control,p,frame.detail.temp?frame.detail.control:frame.edit[p],frame.edit[F::P_SYNC]>16383,frame.bpm);
    format(live,sizeof live,p,frame.live[p],frame.live[F::P_SYNC]>16383,frame.bpm);
    std::snprintf(labels.selected_info,sizeof labels.selected_info,"%s %s %s / live %s",NAMES[p],
        frame.lock?"lock":frame.detail.temp?"temp":"base",control,live);
    const auto& d=frame.detail;
    if(d.source==ModSource::None)std::snprintf(labels.mod_info,sizeof labels.mod_info,"mod off");
    else if(d.source==ModSource::EnvFollow)std::snprintf(labels.mod_info,sizeof labels.mod_info,"env ch%d / %s%+d%%",
        d.env_chain+1,d.mod_enabled?"":"muted ",int(d.depth)*100/32767);
    else std::snprintf(labels.mod_info,sizeof labels.mod_info,"sg.%c / %s%+d%%",
        'a'+mod_source_sigil(d.source),d.mod_enabled?"":"muted ",int(d.depth)*100/32767);
}
void window_wave() {
    const auto& v=frame.visual;
    const bool active=reader_active() && v.loop_frames;
    const uint32_t span=v.loop_frames?v.loop_frames:v.history_frames;
    if(span) {
        const int end=active?w::right(v.reader_frames,v.loop_frames,w::kWave.x,w::kWave.w):w::kFullRight;
        envelope(v.loop_frames?v.loop_low:v.history_low,v.loop_frames?v.loop_high:v.history_high,w::kWave,end,active);
        bracket(w::kWave.x,active?w::kBracketTop:43,active?w::kBracketBottom:103,true,active?m::spot():m::branch());
        bracket(end,active?w::kBracketTop:43,active?w::kBracketBottom:103,false,active?m::spot():m::branch());
        // This is the actual physically-owned waveform edge, not a parameter
        // handle or a post-lift pose. HOLD never keeps the grip/squeeze alive.
        if(active && frame.manual_owned && v.manual) {
            ui::rect(end-2,72,5,11,m::spot());ui::rect(end-1,74,3,7,palette::inverse());
        }
    } else ui::text(160-ui::text_w(frame.status)/2,71,frame.status,m::ghost());
    const char* label=frame.lock?"p-lock / grab off":frame.bypass?"bypassed / dry":frame.mute?"chain muted":
        !v.reader_frames && (v.manual || v.held)?"waiting for capture":"window";
    ui::text(14,w::kLabelY,label,m::ghost());
    char hold[32];std::snprintf(hold,sizeof hold,"hold %s",frame.values[F::P_HOLD]);
    right(304,w::kLabelY,hold,frame.selected==F::P_HOLD || frame.edit[F::P_HOLD]>16383?m::spot():m::ink());
    if(frame.edit[F::P_HOLD]>16383)ui::line(262,129,303,129,m::spot());
}
} // namespace

int fract_focus_selected() {return selected;}
void fract_focus_select(int p) {if(p>=0 && p<F::P_COUNT)selected=p;}
void fract_focus_release() {
    // Every cancellation/lift releases the runtime window through the current
    // guarded Engine only. Never dereference a parked unit or retired engine.
    if(g_audio && g_eng && (manual_unit || origin_unit)) {
        platform::AudioOut::Guard guard(*g_audio);release_locked();
    } else {manual_unit=origin_unit=0;manual_engine=origin_engine=nullptr;drag_param=-1;window_moved=false;}
    if(g_drag==Drag::FractGrab || g_drag==Drag::FractParam)g_drag=Drag::None;
}
void fract_focus_touch_release() {
    // No visual tail, no synthetic DOWN and no timer: core clears the temporary
    // window immediately while an independently-held underlying fragment stays.
    fract_focus_release();
}
void fract_focus_value_text(char* out,size_t size,int p,q15 value) {
    format(out,size,p,value,frame.edit[F::P_SYNC]>16383,frame.bpm);
}
bool fract_focus_touch_body(F& u,int tx,int ty) {
    if(!context() || param_lens_is_open() || nav_is_open()){fract_focus_release();return false;}
    platform::AudioOut::Guard guard(*g_audio);
    release_locked();
    if(g_eng->chains[g_edit_chain].units[g_edit_slot]!=&u)return false;
    if(g_seq_lock_armed && !lock_context())return true;
    if(hit(tx,ty,254,0,60,25)) {
        if(!g_seq_lock_armed){u.bypass=!u.bypass;mark_project_mutated();}
        return true;
    }
    if(hit(tx,ty,178,0,64,25)) {
        selected=F::P_SYNC;change(u,selected,edit_value(u,selected)>16383?0:32767);return true;
    }
    if(hit(tx,ty,w::kHoldTouch.x,w::kHoldTouch.y,w::kHoldTouch.w,w::kHoldTouch.h)) {
        selected=F::P_HOLD;change(u,selected,edit_value(u,selected)>16383?0:32767);return true;
    }
    if(hit(tx,ty,w::kTouch.x,w::kTouch.y,w::kTouch.w,w::kTouch.h)) {
        if(!g_seq_lock_armed && !u.bypass && !g_eng->chains[g_edit_chain].mute) {
            selected=F::P_SIZE;remember(u,tx,F::P_SIZE);
            u.set_manual_grab(true);u.set_grab_window(32767);
            manual_unit=uintptr_t(&u);manual_engine=g_eng;g_drag=Drag::FractGrab;
        }
        return true; // waveform is inert while p-lock is armed
    }
    for(int p=0;p<6;++p) {
        const int x=(p&1)?166:14,y=139+(p/2)*27;
        if(!hit(tx,ty,x-5,y-3,146,27))continue;
        selected=p;
        if(lock_context() && hit(tx,ty,x-4,y-3,ui::text_w(NAMES[p])+8,13)) {
            if(g_eng->remove_step_lock(g_seq_lock_chain,g_seq_lock_step,g_edit_slot,p))mark_project_mutated();
            return true;
        }
        remember(u,tx,p);g_drag=Drag::FractParam;return true;
    }
    return true;
}
void fract_focus_drag(F& u,int tx,int) {
    if(!context() || param_lens_is_open() || nav_is_open()){fract_focus_release();return;}
    platform::AudioOut::Guard guard(*g_audio);
    if(drag_param<0 || !owns(u)){cancel_locked();return;}
    if(g_drag==Drag::FractGrab) {
        if(manual_unit!=uintptr_t(&u) || manual_engine!=g_eng || g_seq_lock_armed ||
           u.bypass || g_eng->chains[g_edit_chain].mute){cancel_locked();return;}
        if(std::abs(tx-origin_x)>w::kDeadzone)window_moved=true;
        const uint32_t span=u.underlying_loop_frames();
        u.set_grab_window(q15(w::fraction(tx,origin_x,span,F::MIN_GRAB_WINDOW,window_moved)));
    } else if(g_drag==Drag::FractParam) {
        const int range=drag_param==F::P_SPEED?65534:32767;
        change(u,drag_param,q15(clamp(origin_value+(tx-origin_x)*range/132,drag_param)));
    } else cancel_locked();
}
void fract_focus_capture() {
    frame={};
    if(!context())fract_focus_release();
    else {
        platform::AudioOut::Guard guard(*g_audio);
        const auto& chain=g_eng->chains[g_edit_chain];const Unit* unit=chain.units[g_edit_slot];
        if(!unit || unit->kind!=UnitKind::Fract)cancel_locked();
        else {
            const auto& u=*static_cast<const F*>(unit);
            // A key change cannot just rebind presentation while the old manual
            // audio owner keeps grabbing. Cancel both before publishing this frame.
            if(origin_unit && (!owns(u) || param_lens_is_open() || nav_is_open() ||
               (g_drag!=Drag::FractParam && g_drag!=Drag::FractGrab) ||
               (g_drag==Drag::FractGrab && (u.bypass || chain.mute))))cancel_locked();
            frame.valid=true;frame.bypass=u.bypass;frame.mute=chain.mute;frame.lock=lock_context();
            frame.lock_full=frame.lock && g_seq_lock_flash>0;frame.lens=param_lens_is_open();
            frame.chain=g_edit_chain;frame.slot=g_edit_slot;frame.selected=selected;
            frame.mod_links=focus_mod_links(u);
            frame.bpm=u.transport_bpm_q16();frame.visual=u.visual();frame.stored_damage=u.params[F::P_DAMAGE].base;
            frame.manual_owned=g_drag==Drag::FractGrab && manual_unit==uintptr_t(&u) && manual_engine==g_eng && owns(u) &&
                frame.visual.manual && !g_seq_lock_armed && !u.bypass && !chain.mute && !frame.lens && !nav_is_open();
            for(int p=0;p<F::P_COUNT;++p){frame.edit[p]=edit_value(u,p);frame.live[p]=u.params[p].effective;}
            g_eng->capture_param_inspector(g_edit_chain,g_edit_slot,selected,frame.detail);
        }
    }
    if(!frame.valid)return;
    for(int p=0;p<F::P_COUNT;++p)fract_focus_value_text(frame.values[p],sizeof frame.values[p],p,frame.edit[p]);
    std::snprintf(frame.status,sizeof frame.status,"%s",status());
}
void fract_focus_draw(const F&) {
    m::paper(320);if(!frame.valid)return;
    module_heading::header(UnitKind::Fract,m::spot());
    char text[48];std::snprintf(text,sizeof text,"ch%02d / %02d",frame.chain+1,frame.slot+1);
    ui::text(67,m::kHeaderTextY,text,m::ghost());
    ui::text(188,m::kHeaderTextY,frame.values[F::P_SYNC],frame.selected==F::P_SYNC?m::spot():m::ink());
    ui::text(276,m::kHeaderTextY,"byp",frame.bypass?m::spot():m::ghost());
    if(frame.bypass)ui::line(276,m::kHeaderMarkY,293,m::kHeaderMarkY,m::spot());
    window_wave();
    for(int p=0;p<6;++p) {
        const int x=(p&1)?166:14,y=139+(p/2)*27;
        const auto color=frame.selected==p?m::spot():m::ink();
        ui::text(x,y,NAMES[p],color);right(x+132,y,frame.values[p],color);
        const int base=rail_x(frame.edit[p],p,x,133),live=rail_x(frame.live[p],p,x,133);
        module_fader::horizontal(x,x+132,y+16,base,live,
            {bool(frame.mod_links&(1u<<p)),false,frame.bypass||frame.mute},
            p==F::P_SPEED?rail_x(8192,p,x,133):-1);
    }
    ui::line(14,221,306,221,m::branch());
    m::footer(320,frame.lock?(frame.lock_full?"lock full":"p-lock / name: remove"):"b back",frame.lock?"b back":"l: mod >");
}
bool fract_focus_draw_top() {
    if(!frame.valid)return false;
    m::paper(400);
    module_heading::header(UnitKind::Fract,m::spot());
    ui::text(76,m::kHeaderTextY,"waveform window",m::ghost());
    char text[48];std::snprintf(text,sizeof text,"ch%02d / %02d",frame.chain+1,frame.slot+1);
    right(m::right_edge(400),m::kHeaderTextY,text,m::ghost());
    const auto& v=frame.visual;
    const bool active=reader_active();
    Labels labels;format_labels(labels);
    ui::text(14,40,labels.history_label,m::ghost());
    right(386,40,frame.status,active && v.wet?m::spot():m::ghost());
    if(v.history_frames) {
        const int end=w::right(v.reader_frames,v.history_frames,w::kHistory.x,w::kHistory.w);
        envelope(v.history_low,v.history_high,w::kHistory,end,active);
        bracket(20,56,121,true,m::branch());bracket(379,56,121,false,m::branch());
        if(active) {
            ui::line(20,127,std::max(21,end),127,m::spot());
            ui::line(20,124,20,129,m::spot());ui::line(end,124,end,129,m::spot());
        }
    }
    ui::text(14,142,labels.reader_label,active?m::spot():m::ghost());
    if(v.reader_frames && v.active) {
        wave(v.reader_wave,w::kReader,379,active);
        if(active && v.wet) {
            const int phase=std::min(32767,int(v.reader_phase));
            const int x=w::kReader.x+phase*(w::kReader.w-1)/32767;
            ui::line(x,w::kReader.y,x,w::kReader.y+w::kReader.h-1,m::spot());
        }
    } else ui::line(20,176,379,176,m::branch());
    ui::text(14,205,labels.damage_label,m::ink());
    std::snprintf(text,sizeof text,"hold %s",frame.values[F::P_HOLD]);
    right(386,205,text,frame.edit[F::P_HOLD]>16383?m::spot():m::ghost());
    ui::line(14,221,386,221,m::branch());
    m::footer(400,labels.selected_info,labels.mod_info);
    return true;
}
} // namespace seance::platform::ui3ds
