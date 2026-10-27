#include "ui_module_focus_internal.h"
#include "ui_state.h"
#include "ui_theme.h"
#include "ui_param_lens.h"
#include "editor_style.h"
#include "module_heading.h"
#include "inspector_gothic.h"
#include "audio_out.h"
#include "../../core/ui/draw.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <type_traits>

namespace seance::platform::ui3ds {
namespace {
namespace m = editor_style;
using F = FractUnit;
constexpr const char* NAMES[F::P_COUNT] = {
    "size", "length", "chance", "damage", "speed", "mix", "clock", "hold"
};
struct Frame {
    uint16_t mod_links=0;
    bool valid=false, bypass=false, mute=false, lock=false, lock_full=false, lens=false;
    int8_t chain=0, slot=0, selected=0;
    int32_t bpm=120*65536;
    q15 edit[F::P_COUNT]={}, live[F::P_COUNT]={};
    char values[F::P_COUNT][20]={};
    F::Visual visual{};
    ParamInspector detail{};
} frame;
static_assert(sizeof(Frame)<=512 && std::is_trivially_copyable<Frame>::value,
              "FRACT frozen bounded frame, no live engine/PCM in draw");
int selected=0, drag_param=-1, origin_x=0;
int origin_chain=-1, origin_slot=-1, origin_step=-1, origin_lock_chain=-1;
bool origin_locked=false;
uintptr_t origin_unit=0, manual_unit=0;
Engine* origin_engine=nullptr;
Engine* manual_engine=nullptr;
q15 origin_value=0;

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
    if(manual_unit && g_eng==manual_engine) {
        for(auto& chain:g_eng->chains)for(auto* u:chain.units)
            if(uintptr_t(u)==manual_unit && u->kind==UnitKind::Fract)
                static_cast<F*>(u)->set_manual_grab(false);
    }
    manual_unit=origin_unit=0;manual_engine=origin_engine=nullptr;drag_param=-1;
}
bool owns(const F& u) {
    return g_eng==origin_engine && origin_unit==uintptr_t(&u) &&
        origin_chain==g_edit_chain && origin_slot==g_edit_slot &&
        g_eng->chains[g_edit_chain].units[g_edit_slot]==&u && origin_locked==g_seq_lock_armed &&
        (!origin_locked || (lock_context() && origin_step==g_seq_lock_step && origin_lock_chain==g_seq_lock_chain));
}
void remember(F& u,int tx,int p) {
    drag_param=p;origin_x=tx;origin_value=edit_value(u,p);origin_unit=uintptr_t(&u);origin_engine=g_eng;
    origin_chain=g_edit_chain;origin_slot=g_edit_slot;origin_locked=g_seq_lock_armed;
    origin_step=g_seq_lock_step;origin_lock_chain=g_seq_lock_chain;
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
void outline(int x,int y,int w,int h,u32 color) {
    ui::line(x,y,x+w,y,color);ui::line(x,y+h,x+w,y+h,color);
    ui::line(x,y,x,y+h,color);ui::line(x+w,y,x+w,y+h,color);
}
void tape(int x,int y,int width,int height,bool compact) {
    const auto& v=frame.visual;
    const bool sounding=v.active && !frame.bypass && !frame.mute;
    // Each tile is the SAME actual loop trace, not invented grains or output FFT.
    // Tile count expresses the shrink ratio, saturated at eight for a fixed GPU budget.
    const int tiles=sounding && v.loop_frames?std::min(8,int(std::max<uint32_t>(1u,v.capture_frames/v.loop_frames))):1;
    const int active=int(v.repeats)%tiles;
    const int gap=tiles>1?(compact?3:5):0;
    for(int t=0;t<tiles;++t) {
        const int left=x+t*width/tiles+gap/2;
        const int right_edge=x+(t+1)*width/tiles-gap/2;
        const int w=right_edge-left;
        const int offset=tiles>1?((t&1)?2:-2):0;
        const int mid=y+height/2+offset;
        const auto color=sounding && t==active?m::spot():m::ghost();
        ui::line(left,y+offset,right_edge,y+offset,m::branch());
        ui::line(left,y+height+offset,right_edge,y+height+offset,m::branch());
        const int stride=compact?2:1;
        for(int n=stride;n<48;n+=stride) {
            const int x0=left+(n-stride)*(w-1)/47,x1=left+n*(w-1)/47;
            const int y0=mid-int(v.wave[n-stride])*(height/2-4)/32768;
            const int y1=mid-int(v.wave[n])*(height/2-4)/32768;
            ui::line(x0,y0,x1,y1,color);
        }
        // Diagonal torn edges remain geometrically tied to the current fragmentation.
        if(tiles>1) {
            ui::line(left,y+offset,left+2,y+4+offset,m::branch());
            ui::line(right_edge-2,y+height-4+offset,right_edge,y+height+offset,m::branch());
        }
        if(sounding && t==active) {
            const int px=left+int(uint32_t(v.phase)*unsigned(w-1)/32767u);
            ui::line(px,y-3+offset,px,y+height+3+offset,m::spot());
            ui::rect(px-2,y-4+offset,5,2,m::spot());
        }
    }
}
const char* status() {
    if(frame.bypass)return "bypassed / dry";
    if(frame.mute)return "chain muted";
    if(frame.visual.held)return "held fragment";
    if(frame.visual.manual)return "manual fracture";
    if(frame.visual.active)return "automatic fracture";
    return frame.visual.fill?"listening / live through":"waiting for audio";
}
} // namespace

int fract_focus_selected() {return selected;}
void fract_focus_select(int p) {if(p>=0 && p<F::P_COUNT)selected=p;}
void fract_focus_release() {
    if(g_audio && g_eng && (manual_unit || origin_unit)) {
        platform::AudioOut::Guard guard(*g_audio);release_locked();
    } else {manual_unit=origin_unit=0;manual_engine=origin_engine=nullptr;drag_param=-1;}
}
void fract_focus_value_text(char* out,size_t size,int p,q15 value) {
    format(out,size,p,value,frame.edit[F::P_SYNC]>16383,frame.bpm);
}
bool fract_focus_touch_body(F& u,int tx,int ty) {
    if(!context() || param_lens_is_open())return false;
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
    if(hit(tx,ty,236,88,72,40)) {
        selected=F::P_HOLD;change(u,selected,edit_value(u,selected)>16383?0:32767);return true;
    }
    if(hit(tx,ty,12,88,212,40)) {
        if(!g_seq_lock_armed && !u.bypass && !g_eng->chains[g_edit_chain].mute) {
            selected=F::P_DAMAGE;remember(u,tx,F::P_DAMAGE);
            u.set_manual_grab(true);manual_unit=uintptr_t(&u);manual_engine=g_eng;g_drag=Drag::FractGrab;
        }
        return true;
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
    if(!context() || param_lens_is_open()){fract_focus_release();return;}
    platform::AudioOut::Guard guard(*g_audio);
    if(drag_param<0 || !owns(u)){release_locked();return;}
    if(g_drag==Drag::FractGrab) {
        if(u.bypass || g_eng->chains[g_edit_chain].mute){release_locked();return;}
        // Hold + pull left tightens future loops. A stationary touch never changes the patch.
        const int dx=origin_x-tx;
        if(std::abs(dx)>4)change(u,F::P_DAMAGE,q15(clamp(origin_value+dx*32767/180,F::P_DAMAGE)));
    } else if(g_drag==Drag::FractParam) {
        const int range=drag_param==F::P_SPEED?65534:32767;
        change(u,drag_param,q15(clamp(origin_value+(tx-origin_x)*range/132,drag_param)));
    } else release_locked();
}
void fract_focus_capture() {
    frame={};
    if(!context()){fract_focus_release();return;}
    {
        platform::AudioOut::Guard guard(*g_audio);
        const auto& chain=g_eng->chains[g_edit_chain];const Unit* unit=chain.units[g_edit_slot];
        if(!unit || unit->kind!=UnitKind::Fract){release_locked();return;}
        const auto& u=*static_cast<const F*>(unit);
        if(origin_unit && (!owns(u) || param_lens_is_open() ||
           (g_drag!=Drag::FractParam && g_drag!=Drag::FractGrab) ||
           (g_drag==Drag::FractGrab && (u.bypass || chain.mute))))release_locked();
        frame.valid=true;frame.bypass=u.bypass;frame.mute=chain.mute;frame.lock=lock_context();
        frame.lock_full=frame.lock && g_seq_lock_flash>0;frame.lens=false;
        frame.chain=g_edit_chain;frame.slot=g_edit_slot;frame.selected=selected;
        frame.mod_links=focus_mod_links(u);
        frame.bpm=u.transport_bpm_q16();frame.visual=u.visual();
        for(int p=0;p<F::P_COUNT;++p){frame.edit[p]=edit_value(u,p);frame.live[p]=u.params[p].effective;}
        g_eng->capture_param_inspector(g_edit_chain,g_edit_slot,selected,frame.detail);
    }
    for(int p=0;p<F::P_COUNT;++p)fract_focus_value_text(frame.values[p],sizeof frame.values[p],p,frame.edit[p]);
}
void fract_focus_draw(const F&) {
    m::paper(320);if(!frame.valid)return;
    module_heading::header(UnitKind::Fract,m::spot());
    char text[48];std::snprintf(text,sizeof text,"ch%02d / %02d",frame.chain+1,frame.slot+1);
    ui::text(67,m::kHeaderTextY,text,m::ghost());
    ui::text(188,m::kHeaderTextY,frame.values[F::P_SYNC],frame.selected==F::P_SYNC?m::spot():m::ink());
    ui::text(276,m::kHeaderTextY,"byp",frame.bypass?m::spot():m::ghost());
    if(frame.bypass)ui::line(276,m::kHeaderMarkY,293,m::kHeaderMarkY,m::spot());
    tape(14,34,292,34,true);
    ui::text(14,77,status(),m::ghost());
    const bool grab_disabled=frame.lock || frame.bypass || frame.mute;
    const bool pressed=!grab_disabled && (frame.visual.manual || frame.visual.held);
    outline(12,88,212,40,pressed?m::spot():m::branch());
    if(pressed)ui::rect(13,89,3,38,m::spot());
    ui::text(26,98,"grab",grab_disabled?m::ghost():m::spot(),2);
    ui::text(100,97,frame.lock?"locked step":frame.bypass?"bypassed":frame.mute?"chain muted":"hold + pull left",m::ghost());
    ui::text(100,112,grab_disabled?"edit below":"to squeeze",m::ghost());
    outline(236,88,72,40,frame.edit[F::P_HOLD]>16383?m::spot():m::branch());
    ui::text(248,98,"hold",frame.selected==F::P_HOLD?m::spot():m::ink());
    ui::text(248,113,frame.values[F::P_HOLD],m::ghost());
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
    ui::text(76,m::kHeaderTextY,"buffer fracture",m::ghost());
    char text[64];std::snprintf(text,sizeof text,"ch%02d / %02d",frame.chain+1,frame.slot+1);
    right(m::right_edge(400),m::kHeaderTextY,text,m::ghost());ui::line(14,29,386,29,m::branch());
    const int p=frame.selected;
    ui::text(14,36,NAMES[p],m::spot());
    ui::text(14,50,frame.lock?"edit lock":frame.detail.temp?"temporary":"base",m::ghost());
    char value[24];format(value,sizeof value,p,frame.detail.temp?frame.detail.control:frame.edit[p],
                         frame.edit[F::P_SYNC]>16383,frame.bpm);
    const auto hero=inspector_gothic::draw_numeric(14,94,value,m::ink(),m::spot());
    if(hero.drawn)ui::text(hero.end_x+4,84,hero.suffix,m::spot());else ui::text(14,66,value,m::ink(),3);
    format(value,sizeof value,p,frame.live[p],frame.live[F::P_SYNC]>16383,frame.bpm);
    right(386,42,"effective",m::ghost());right(386,57,value,m::spot());
    const auto& d=frame.detail;
    if(d.source==ModSource::None)std::snprintf(text,sizeof text,"mod off");
    else if(d.source==ModSource::EnvFollow)std::snprintf(text,sizeof text,"env ch%d / %s%+d%%",d.env_chain+1,d.mod_enabled?"":"muted ",int(d.depth)*100/32767);
    else std::snprintf(text,sizeof text,"sg.%c / %s%+d%%",'a'+mod_source_sigil(d.source),d.mod_enabled?"":"muted ",int(d.depth)*100/32767);
    right(386,77,text,d.source==ModSource::None || !d.mod_enabled?m::ghost():m::spot());
    tape(14,110,372,56,false);
    std::snprintf(text,sizeof text,"loop %lums",static_cast<unsigned long>((frame.visual.loop_frames+16)/32));
    ui::text(14,178,text,m::spot());
    std::snprintf(text,sizeof text,"repeat %u",unsigned(frame.visual.repeats));right(386,178,text,m::ghost());
    const int span=frame.visual.capture_frames?int(uint64_t(frame.visual.loop_frames)*372/frame.visual.capture_frames):0;
    ui::line(14,197,386,197,m::branch());
    if(span>0)ui::rect(14,195,std::min(372,span),3,m::spot());
    ui::text(14,207,"retained loop / captured span",m::ghost());
    ui::line(14,222,386,222,m::branch());
    m::footer(400,status(),frame.lens?"mod / b return":"repeat engine");
    return true;
}
} // namespace seance::platform::ui3ds
