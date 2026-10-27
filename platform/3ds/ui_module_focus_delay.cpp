#include "ui_module_focus_internal.h"
#include "ui_state.h"
#include "ui_theme.h"
#include "ui_param_lens.h"
#include "module_inspector.h"
#include "editor_style.h"
#include "module_heading.h"
#include "inspector_gothic.h"
#include "delay_readout.h"
#include "delay_iris.h"
#include "../../core/engine/delay_time.h"
#include "../../core/ui/draw.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <type_traits>

namespace seance::platform::ui3ds {
namespace {
namespace m=editor_style;

using D=DelayUnit;
constexpr const char* NAMES[]={"time","feedback","mix","wow","sat","hold","tone","clock","division","feel","mode"};
struct Frame {
    ParamInspector detail={};
    uint16_t mod_links=0;
    q15 value[D::P_COUNT]={}, base[D::P_COUNT]={}, edit[D::P_COUNT]={};
    uint8_t source[D::P_COUNT]={}, kinds[UNITS_PER_CHAIN]={};
    bool bypassed[UNITS_PER_CHAIN]={};
    D::VisualState live={};
    uint32_t interval=32, base_interval=32, edit_interval=32;
    bool modern=false, valid=false, bypass=false, mute=false, lock=false, full=false, ready=false;
    bool clear_armed=false, limited=false, edit_limited=false, lens=false;
    uint8_t clear_progress=0;
    int8_t chain=0, slot=0, selected=0;
} frame;
static_assert(sizeof(Frame)<=256 && std::is_trivially_copyable<Frame>::value,
              "bounded shared pointer-free delay capture");
delay_iris::Frame iris; // <=2100B, prepared after releasing the audio guard
int selected=D::P_TIME;
struct Box {
    int x=0,y=0,w=0,h=0;
    bool contains(int tx,int ty) const {return hit(tx,ty,x,y,w,h);}
};
constexpr Box CLEAR{258,204,55,24};
enum class Action : uint8_t { Drag,Tap,Clear };
struct Gesture {
    Engine* engine=nullptr;
    uintptr_t unit=0;
    View view=View::Chains;
    int chain=-1,slot=-1,bank=-1,step=-1,lock_chain=-1,param=-1,selection=-1;
    int x=0,y=0,last_x=0;
    q15 value=0,next=0;
    bool locked=false,modern=false;
    uint32_t started=0;
    Action action=Action::Drag;
    Box box{};
} gesture;
constexpr int CLEAR_FRAMES=45; // main's 60Hz UI clock: deliberate 750ms hold
int count() { return frame.modern?D::P_COUNT:6; }
int clamp(int v) { return std::max(0,std::min(32767,v)); }
bool context(bool body=true) {
    return g_eng && g_audio && (!body || !param_lens_is_open()) && g_overlay==Overlay::Editor &&
        (g_view==View::Chains || (g_view==View::Sequencer && g_seq_lock_armed)) &&
        g_edit_chain>=0 && g_edit_chain<NUM_CHAINS && g_edit_slot>=0 && g_edit_slot<UNITS_PER_CHAIN;
}
bool lock_context() {
    return g_seq_lock_armed && g_seq_lock_chain==g_edit_chain &&
        g_seq_lock_step>=0 && g_seq_lock_step<SEQ_STEPS;
}
q15 edit_value(const D& u,int p) {
    return lock_context()?seq_display_value(g_edit_slot,p,u.params[p].base):u.params[p].base;
}
bool sync_control(const D& u) {
    return u.modern() && (lock_context()?edit_value(u,D::P_SYNC):delay_readout::resolved(u,D::P_SYNC))>16384;
}
bool owns(const D& u) {
    return context() && gesture.engine==g_eng && gesture.unit==uintptr_t(&u) && gesture.view==g_view &&
        gesture.chain==g_edit_chain && gesture.slot==g_edit_slot && gesture.bank==g_eng->active_pattern &&
        g_eng->chains[g_edit_chain].units[g_edit_slot]==&u && gesture.locked==g_seq_lock_armed &&
        gesture.modern==u.modern() && gesture.selection==selected &&
        // A sigil can switch free/sync while a finger is down. Never silently
        // continue editing the other, now invisible, time control.
        (gesture.param!=D::P_TIME || !sync_control(u)) &&
        ((gesture.param!=D::P_DIV && gesture.param!=D::P_FEEL) || sync_control(u)) &&
        (!gesture.locked || (lock_context() && gesture.step==g_seq_lock_step && gesture.lock_chain==g_seq_lock_chain));
}
void start(D& u,int p,int tx,int ty,Action action=Action::Drag,Box box={},q15 next=0) {
    gesture={};gesture.engine=g_eng;gesture.unit=uintptr_t(&u);gesture.view=g_view;
    gesture.chain=g_edit_chain;gesture.slot=g_edit_slot;gesture.bank=g_eng->active_pattern;
    gesture.step=g_seq_lock_step;gesture.lock_chain=g_seq_lock_chain;gesture.locked=g_seq_lock_armed;
    gesture.modern=u.modern();gesture.param=p;gesture.selection=selected;
    gesture.x=gesture.last_x=tx;gesture.y=ty;gesture.value=p>=0?edit_value(u,p):0;
    gesture.action=action;gesture.box=box;gesture.next=next;gesture.started=g_ui_frame;
    g_drag=Drag::ParamBase;
}
void assign(D& u,int p,q15 value) {
    if(value==edit_value(u,p))return;
    if(g_seq_lock_armed){if(lock_context())seq_record_lock(g_edit_slot,p,value);}
    else {u.params[p].base=value;mark_project_mutated();}
}
u32 color(int p) {
    return frame.bypass||frame.mute?m::ghost():
        (frame.selected==p || (!frame.lock && (frame.mod_links&(1u<<p))))?m::spot():m::ink();
}
void right(int x,int y,const char* text,u32 ink) { ui::text(x-ui::text_w(text),y,text,ink); }
q15 shown(int p) {return frame.lock?frame.edit[p]:frame.value[p];}
void value_text(int p,q15 value,char* text,size_t size) {
    module_inspector_value_text(text,size,UnitKind::Delay,p,value,true,frame.modern);
}
void badge(int p,int end,int y) {
    const auto src=ModSource(frame.source[p]);if(src==ModSource::None)return;
    char text[8];const int sigil=mod_source_sigil(src);
    if(sigil>=0)std::snprintf(text,sizeof text,"%c",'a'+sigil);
    else std::snprintf(text,sizeof text,"env");
    right(end,y,text,frame.mod_links&(1u<<p)?m::spot():m::ghost());
    if(!(frame.mod_links&(1u<<p)))ui::line(end-ui::text_w(text),y+4,end-1,y+4,m::ghost());
}
void rail(int p,int x,int end,int y) {
    const int at=x+clamp(frame.edit[p])*(end-x)/32767;
    const int live=x+clamp(frame.value[p])*(end-x)/32767;
    module_fader::horizontal(x,end,y,at,live,
        {bool(frame.mod_links&(1u<<p)),false,frame.bypass||frame.mute});
}
void primary(int p,int x,int end,const char* name) {
    char text[24],caption[32];value_text(p,shown(p),text,sizeof text);
    ui::text(x,35,name,frame.selected==p?m::spot():m::ink());
    badge(p,p==D::P_DIV?132:end,35);
    if(p==D::P_DIV)ui::text(x,65,text,color(p),2);
    else {
        const auto number=inspector_gothic::draw_numeric(x,85,text,color(p),m::spot());
        ui::text(number.end_x+4,79,number.suffix,m::ghost());
    }
    value_text(p,frame.lock?frame.value[p]:frame.base[p],text,sizeof text);
    std::snprintf(caption,sizeof caption,"%s %s",frame.lock?"live":"base",text);
    ui::text(x,100,caption,m::ghost());
    rail(p,x,end,118);
}
void secondary(int p,int x,const char* name,bool enabled=true) {
    ui::text(x,140,name,frame.selected==p?m::spot():m::ink());
    char text[24],caption[32];
    if(enabled) {
        badge(p,x+57,140);value_text(p,shown(p),text,sizeof text);
        ui::text(x,158,text,color(p));
        value_text(p,frame.lock?frame.value[p]:frame.base[p],text,sizeof text);
        std::snprintf(caption,sizeof caption,"%s %s",frame.lock?"l":"b",text);
        ui::text(x,173,caption,m::ghost());
        rail(p,x+2,x+56,192);
    } else {
        ui::text(x,158,"fixed",m::ghost());
        module_fader::horizontal_track(x+2,x+56,192);
    }
    if(enabled && frame.selected==p)ui::line(x,201,x+56,201,m::spot());
}
} // namespace

int delay_focus_selected() { return selected; }
void delay_focus_select(int p) {
    if(p<0 || p>=count())return;
    if(p!=selected)delay_focus_release();
    selected=p;
}
bool delay_focus_modern() { return frame.modern; }
void delay_focus_release() {
    if(gesture.unit && g_drag==Drag::ParamBase)g_drag=Drag::None;
    gesture={};
}
void delay_focus_touch_release() {
    if(gesture.unit && gesture.action==Action::Tap && context() && g_drag==Drag::ParamBase) {
        platform::AudioOut::Guard guard(*g_audio);
        Unit* at=g_eng->chains[g_edit_chain].units[g_edit_slot];
        if(at && at->kind==UnitKind::Delay) {
            auto& u=*static_cast<D*>(at);
            // A tap is a conditional commit, never a stale overwrite after another edit.
            if(owns(u) && edit_value(u,gesture.param)==gesture.value)assign(u,gesture.param,gesture.next);
        }
    }
    delay_focus_release();
}

bool delay_focus_touch_body(D& u,int tx,int ty) {
    if(!context())return false;
    platform::AudioOut::Guard guard(*g_audio);
    if(g_eng->chains[g_edit_chain].units[g_edit_slot]!=&u)return false;
    delay_focus_release();g_drag=Drag::None;
    if(!g_seq_lock_armed && hit(tx,ty,246,0,36,25)) {
        u.bypass=!u.bypass;mark_project_mutated();return true;
    }
    if(lock_context() && hit(tx,ty,8,u.modern()?204:209,100,u.modern()?24:15)) {
        if(g_eng->remove_step_lock(g_seq_lock_chain,g_seq_lock_step,g_edit_slot,selected))mark_project_mutated();
        return true;
    }
    if(g_seq_lock_armed && !lock_context())return true;
    if(u.modern() && !g_seq_lock_armed && CLEAR.contains(tx,ty)) {
        start(u,-1,tx,ty,Action::Clear,CLEAR);return true;
    }
    const auto tap=[&](int p,Box box,q15 next) {
        if(!box.contains(tx,ty))return false;
        selected=p;start(u,p,tx,ty,Action::Tap,box,next);return true;
    };
    if(tap(D::P_HOLD,{184,0,56,25},edit_value(u,D::P_HOLD)>16384?0:32767))return true;
    if(u.modern()) {
        if(g_seq_lock_armed) {
            if(tap(D::P_SYNC,{108,204,40,24},edit_value(u,D::P_SYNC)>16384?0:32767))return true;
        } else {
            if(tap(D::P_SYNC,{8,204,46,24},0) || tap(D::P_SYNC,{54,204,54,24},32767))return true;
        }
        if(tap(D::P_MODE,{148,204,52,24},0) || tap(D::P_MODE,{200,204,50,24},32767))return true;
    }
    int p=-1;
    const bool synced=sync_control(u);
    if(synced && hit(tx,ty,142,28,58,24))p=D::P_FEEL;
    else if(hit(tx,ty,8,28,192,97))p=synced?D::P_DIV:D::P_TIME;
    else if(hit(tx,ty,208,28,105,97))p=D::P_FEEDBACK;
    else if(hit(tx,ty,8,132,70,69))p=D::P_MIX;
    else if(hit(tx,ty,160,132,70,69))p=D::P_WOW;
    else if(hit(tx,ty,236,132,70,69))p=D::P_SAT;
    else if(u.modern() && hit(tx,ty,84,132,70,69))p=D::P_TONE;
    if(p>=0) {selected=p;start(u,p,tx,ty);}
    return true; // View-only diagram/gaps never expose hidden legacy hit zones.
}

void delay_focus_drag(D& u,int tx,int ty) {
    if(!gesture.unit)return;
    if(!context() || g_drag!=Drag::ParamBase){delay_focus_release();return;}
    platform::AudioOut::Guard guard(*g_audio);
    if(!owns(u)){delay_focus_release();return;}
    if(gesture.action==Action::Clear) {
        if(!gesture.box.contains(tx,ty)){delay_focus_release();return;}
        if(uint32_t(g_ui_frame-gesture.started)>=CLEAR_FRAMES){u.request_clear();delay_focus_release();}
        return;
    }
    if(gesture.action==Action::Tap) {
        if(!gesture.box.contains(tx,ty) || std::abs(tx-gesture.x)>8 || std::abs(ty-gesture.y)>8 ||
           edit_value(u,gesture.param)!=gesture.value)delay_focus_release();
        return;
    }
    if(tx==gesture.last_x)return; // Stationary reports cannot restore an externally edited value.
    gesture.last_x=tx;
    const q15 next=q15(clamp(gesture.value+(tx-gesture.x)*32767/(gesture.param==D::P_FEEL?48:144)));
    assign(u,gesture.param,next);
}

void delay_focus_capture() {
    frame={};iris={};
    if(!context(false)){delay_focus_release();return;}
    {
        platform::AudioOut::Guard guard(*g_audio);
        const auto& chain=g_eng->chains[g_edit_chain];const Unit* unit=chain.units[g_edit_slot];
        if(!unit || unit->kind!=UnitKind::Delay){delay_focus_release();return;}
        const auto& u=*static_cast<const D*>(unit);
        if(gesture.unit && (!owns(u) || g_drag!=Drag::ParamBase ||
           (gesture.action==Action::Tap && edit_value(u,gesture.param)!=gesture.value)))delay_focus_release();
        frame.modern=u.modern();frame.live=u.visual_state();
        if(selected>=u.num_params)selected=0;
        frame.chain=g_edit_chain;frame.slot=g_edit_slot;frame.selected=selected;
        frame.bypass=u.bypass;frame.mute=chain.mute;frame.ready=u.ready();
        frame.lock=lock_context();frame.lens=param_lens_is_open();
        frame.full=frame.lock &&
            (g_seq_lock_flash>0 || g_eng->pattern.steps[g_edit_chain][g_seq_lock_step].lock_count>=SEQ_LOCKS);
        frame.clear_armed=gesture.unit && gesture.action==Action::Clear;frame.clear_progress=frame.clear_armed?
            std::min<uint32_t>(100,uint32_t(g_ui_frame-gesture.started)*100/CLEAR_FRAMES):0;
        frame.mod_links=focus_mod_links(u);
        for(int p=0;p<u.num_params;++p) {
            frame.base[p]=u.params[p].base;frame.edit[p]=edit_value(u,p);
            frame.value[p]=delay_readout::resolved(u,p);frame.source[p]=uint8_t(u.params[p].src);
        }
        for(int s=0;s<UNITS_PER_CHAIN;++s) {
            const Unit* at=chain.units[s];frame.kinds[s]=uint8_t(at?at->kind:UnitKind::Empty);
            frame.bypassed[s]=at && at->bypass;
        }
        g_eng->capture_param_inspector(g_edit_chain,g_edit_slot,selected,frame.detail);
        frame.valid=frame.detail.valid;
    }
    if(!frame.valid)return;
    // Musical mappings + bounded iris geometry use ONLY the copied scalars, with
    // audio unlocked. Both LCDs will read this same immutable frame until capture.
    frame.interval=delay_readout::interval(frame.modern,frame.value,frame.live.bpm_q16,&frame.limited);
    frame.base_interval=delay_readout::interval(frame.modern,frame.base,frame.live.bpm_q16);
    frame.edit_interval=delay_readout::interval(frame.modern,frame.edit,frame.live.bpm_q16,&frame.edit_limited);
    delay_iris::build(iris,delay_time::free_value(frame.interval),delay_time::free_value(frame.base_interval),
                      frame.value[D::P_FEEDBACK],frame.value[D::P_MIX]);
}

void delay_focus_draw(const D&) {
    m::paper(320);
    if(!frame.valid)return;
    char text[40];
    module_heading::header(UnitKind::Delay,m::spot());
    std::snprintf(text,sizeof text,"ch%02d / %02d",frame.chain+1,frame.slot+1);
    ui::text(50,m::kHeaderTextY,text,m::ghost());
    ui::text(193,m::kHeaderTextY,shown(D::P_HOLD)>16384?"held":"hold",color(D::P_HOLD));
    if(shown(D::P_HOLD)>16384 || frame.selected==D::P_HOLD)ui::line(190,m::kHeaderMarkY,234,m::kHeaderMarkY,m::spot());
    ui::text(258,m::kHeaderTextY,"byp",frame.bypass?m::spot():m::ghost());right(m::right_edge(320),m::kHeaderTextY,"x",m::ghost());
    if(frame.lens) {
        // MOD owns the body. Keep identity/selection, not large inactive digits
        // shining through its translucent material and behind its own readouts.
        ui::text(14,35,NAMES[frame.selected],m::spot());
        ui::text(m::kLeft,m::kTextY,"b / return",m::ghost());
        return;
    }
    const bool synced=frame.modern && shown(D::P_SYNC)>16384;
    primary(synced?D::P_DIV:D::P_TIME,14,190,synced?"division":"time");
    primary(D::P_FEEDBACK,218,306,"feedback");
    if(synced) {
        static const char* feels[]={"str","dot","tri"};
        right(192,35,feels[delay_time::feel(shown(D::P_FEEL))],color(D::P_FEEL));
        badge(D::P_FEEL,196,48);
        if(frame.selected==D::P_FEEL)ui::line(166,45,192,45,m::spot());
        const uint32_t frames=frame.lock?frame.edit_interval:frame.interval;
        std::snprintf(text,sizeof text,"%ums%s",unsigned((frames+16)/32),(frame.lock?frame.edit_limited:frame.limited)?"!":"");
        right(190,80,text,m::ghost());
    }
    // The echo picture lives upstairs. Every lower area is now a generous,
    // visible control; its complete cell shares the same relative drag origin.
    secondary(D::P_MIX,14,"mix");
    secondary(D::P_TONE,90,"tone",frame.modern);
    secondary(D::P_WOW,166,"wow");
    secondary(D::P_SAT,242,"sat");
    if(frame.modern) {
        if(frame.lock) {
            ui::text(14,213,"clear lock",m::ghost());
            ui::text(114,213,synced?"sync":"free",color(D::P_SYNC));
        } else {
            ui::text(14,213,"free",synced?m::ghost():m::spot());
            ui::text(62,213,"sync",synced?m::spot():m::ghost());
            const int x=synced?62:14;ui::line(x,223,x+23,223,m::spot());
        }
        const bool clean=shown(D::P_MODE)>16384;
        ui::text(154,213,"tape",clean?m::ghost():m::spot());
        ui::text(206,213,"clean",clean?m::spot():m::ghost());
        const int x=clean?206:154;ui::line(x,223,x+23,223,m::spot());
        if(!frame.lock) {
            ui::text(266,213,frame.live.clearing?"wait":frame.clear_armed?"hold":"clear",frame.clear_armed?m::spot():m::ghost());
            if(frame.clear_armed)ui::line(260,223,260+frame.clear_progress*48/100,223,m::spot());
        }
    } else {
        ui::text(14,213,frame.lock?"clear lock":"free / tape",m::ghost());
        right(306,213,"legacy",m::ghost());
    }
    ui::text(m::kLeft,m::kTextY,"b back",m::ghost());
    ui::text(86,m::kTextY,frame.full?"lock full":frame.lock?"edit lock":!frame.ready?"no buffer":
        frame.bypass?"bypassed":frame.mute?"chain muted":frame.live.clearing?"clearing":
        frame.clear_armed?"hold to clear":"values / live",m::ghost());
    if(!frame.lock)right(m::right_edge(320),m::kTextY,"l: mod >",m::ghost());
}
bool delay_focus_draw_top() {
    if(!frame.valid)return false;
    m::paper(400);
    char text[96],a[24],b[24];
    module_heading::header(UnitKind::Delay,m::spot());
    std::snprintf(text,sizeof text,"ch%02d / %02d",frame.chain+1,frame.slot+1);
    right(m::right_edge(400),m::kHeaderTextY,text,m::ghost());
    int x=14;
    for(int s=0;s<UNITS_PER_CHAIN;++s) {
        const auto kind=UnitKind(frame.kinds[s]);if(kind==UnitKind::Empty)continue;
        if(x>14)ui::text(x-9,module_heading::kRouteY,"/",m::ghost());
        const char* name=m::module_name(kind);const int w=ui::text_w(name);
        ui::text(x,module_heading::kRouteY,name,s==frame.slot?m::spot():m::ghost());
        if(s==frame.slot)ui::line(x,module_heading::kRouteUnderlineY,x+w-2,module_heading::kRouteUnderlineY,m::spot());
        if(frame.bypassed[s])ui::line(x,module_heading::kRouteCrossY,x+w-2,module_heading::kRouteCrossY,m::ghost());
        x+=w+12;
    }
    const bool paused=frame.bypass || frame.mute;
    ui::text(14,46,"return iris / model",m::ghost());
    std::snprintf(text,sizeof text,"%s / %s",frame.modern?(frame.value[D::P_SYNC]>16384?"sync":"free"):"legacy",
                  frame.modern && frame.value[D::P_MODE]>16384?"clean":"tape");
    right(386,46,text,m::ghost());
    const auto ink=[&](unsigned amount) {
        const u32 lo=palette::shadow(),hi=paused?m::ghost():m::spot();u32 out=0xff000000u;
        for(int shift=0;shift<24;shift+=8)out|=((((lo>>shift)&255)*(255-amount)+((hi>>shift)&255)*amount+127)/255)<<shift;
        return out;
    };
    for(int n=0;n<iris.count;++n) {
        const u32 col=ink(iris.emphasis[n]);
        for(int j=1;j<=delay_iris::SEGMENTS;++j) {
            const auto& a=iris.points[n][j-1];const auto& b=iris.points[n][j];
            if(a.x!=b.x || a.y!=b.y)ui::line(a.x,a.y,b.x,b.y,col);
        }
        if(n<3) {
            const auto& end=iris.points[n][delay_iris::SEGMENTS];
            ui::line(end.x-2,end.y-2,end.x+2,end.y+2,paused?m::ghost():m::spot());
        }
    }
    ui::circle(iris.base.x,iris.base.y,2,m::ghost(),false);
    ui::text(173,99,"live time",m::ghost());
    std::snprintf(text,sizeof text,"%u",unsigned((frame.interval+16)/32));
    int width=0;for(const char* p=text;*p;++p)if(const auto* glyph=inspector_gothic::digit(*p))width+=glyph->width+2;
    inspector_gothic::draw_numeric(200-width/2,140,text,paused?m::ghost():m::ink(),m::spot());
    ui::text(194,153,"ms",m::ghost());
    value_text(D::P_FEEDBACK,frame.value[D::P_FEEDBACK],a,sizeof a);
    std::snprintf(text,sizeof text,"return %s",a);ui::text(14,207,text,paused?m::ghost():m::spot());
    value_text(D::P_MIX,frame.value[D::P_MIX],a,sizeof a);
    std::snprintf(text,sizeof text,"wet %s",a);right(386,207,text,paused?m::ghost():m::spot());
    const auto read_ms=[](char* out,size_t size,uint32_t q8) {
        const uint64_t cent=(uint64_t(q8)*100+4096)/8192;
        if(cent<1000)std::snprintf(out,size,"%u.%02ums",unsigned(cent/100),unsigned(cent%100));
        else std::snprintf(out,size,"%ums",unsigned((cent+50)/100));
    };
    if(!frame.ready || !frame.live.ready)std::snprintf(text,sizeof text,"read --");
    else {
        read_ms(a,sizeof a,frame.live.read_a_q8);
        if(frame.live.crossfading) {
            read_ms(b,sizeof b,frame.live.read_b_q8);
            std::snprintf(text,sizeof text,"%s>%s %d%%",a,b,int(frame.live.crossfade_q15)*100/32767);
        } else std::snprintf(text,sizeof text,"%s %s",paused?"last":"read",a);
    }
    ui::text(200-ui::text_w(text)/2,207,text,m::ghost());

    const auto& d=frame.detail;
    if(frame.lock) {
        value_text(frame.selected,frame.edit[frame.selected],a,sizeof a);
        value_text(frame.selected,frame.base[frame.selected],b,sizeof b);
        std::snprintf(text,sizeof text,"draft %s %s / base %s",NAMES[frame.selected],a,b);
    } else if(d.source==ModSource::None)std::snprintf(text,sizeof text,"%s / mod off%s",NAMES[frame.selected],d.layer?" / live override":"");
    else {
        char src[16];
        if(d.source==ModSource::EnvFollow)std::snprintf(src,sizeof src,"env ch%d",d.env_chain+1);
        else std::snprintf(src,sizeof src,"sg.%c",'a'+mod_source_sigil(d.source));
        value_text(frame.selected,d.range_min,a,sizeof a);value_text(frame.selected,d.range_max,b,sizeof b);
        std::snprintf(text,sizeof text,"%s / %s %+d%% / %s..%s%s",NAMES[frame.selected],src,int(d.depth)*100/32767,a,b,
                      !d.mod_enabled?" / muted":d.clip_low||d.clip_high?" !":"");
    }
    ui::text(14,219,text,m::ghost());
    const char* status="parameter model / not audio";
    if(!frame.ready)status="buffer unavailable";
    else if(frame.full)status="lock bank full";
    else if(frame.live.clearing)status=paused?"clear pending / paused":"clearing history";
    else if(frame.bypass || frame.mute)status=frame.mute?"chain muted / last read":"bypassed / last read";
    else if(!frame.live.ready)status="await processing";
    else if(frame.limited)status="clock limited to 4s";
    else if(frame.live.hold_amount) {
        std::snprintf(text,sizeof text,"hold %d%% / not freeze",int(frame.live.hold_amount)*100/32767);status=text;
    } else if(frame.live.crossfading)status="20ms clean handoff";
    ui::text(m::kLeft,m::kTextY,status,m::ghost());
    if(frame.lock)right(m::right_edge(400),m::kTextY,"live / draft below",m::ghost());
    else if(d.temp)right(m::right_edge(400),m::kTextY,"temp override",m::spot());
    else {
        int links=0;for(int p=0;p<count();++p)if(frame.mod_links&(1u<<p))++links;
        if(links){std::snprintf(text,sizeof text,"%d live links",links);right(m::right_edge(400),m::kTextY,text,m::spot());}
        else if(frame.modern && frame.value[D::P_SYNC]>16384) {
            std::snprintf(text,sizeof text,"%.1f bpm",double(frame.live.bpm_q16)/65536.0);right(m::right_edge(400),m::kTextY,text,m::ghost());
        }
    }
    return true;
}
} // namespace seance::platform::ui3ds
