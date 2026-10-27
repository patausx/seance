#include "ui_module_focus_internal.h"
#include "ui_state.h"
#include "ui_theme.h"
#include "ui_param_lens.h"
#include "module_inspector.h"
#include "editor_style.h"
#include "module_heading.h"
#include "audio_out.h"
#include "../../core/engine/chorus_motion.h"
#include "../../core/ui/draw.h"
#include <algorithm>
#include <cstdio>
#include <type_traits>

namespace seance::platform::ui3ds {
namespace {
namespace m=editor_style;
using C=ChorusUnit;
constexpr const char* NAMES[]={"rate","depth","mix","spread"};
constexpr int RAIL_X=100, RAIL_END=303;
struct Frame {
    ParamInspector detail={};
    q15 base[C::P_COUNT]={},edit[C::P_COUNT]={},live[C::P_COUNT]={};
    uint32_t delay[2]={};
    uint16_t mod_links=0,compare_gain=0;
    uint8_t source[C::P_COUNT]={},kinds[UNITS_PER_CHAIN]={};
    bool crossed[UNITS_PER_CHAIN]={};
    bool valid=false,bypass=false,mute=false,lock=false,full=false,ready=false,lens=false;
    bool compare=false,compare_allowed=false;
    int8_t chain=0,slot=0,selected=C::P_SPREAD;
    int16_t center[2]={200,200};
} frame;
static_assert(sizeof(Frame)<=256 && std::is_trivially_copyable<Frame>::value,
              "bounded pointer-free shared chorus frame");
int selected=C::P_SPREAD;
struct Gesture {
    Engine* engine=nullptr;
    uintptr_t unit=0;
    View view=View::Chains;
    int chain=-1,slot=-1,bank=-1,step=-1,lock_chain=-1,param=-1,selection=-1;
    int x=0,last_x=0;
    q15 value=0;
    bool locked=false,compare=false;
} gesture;
// All chorus gestures share an existing UI-only token; no persisted enum changes.
constexpr Drag TOKEN=Drag::ChorusRate;
bool context(bool body=true) {
    return g_eng && g_audio && (!body || !param_lens_is_open()) && g_overlay==Overlay::Editor &&
        (g_view==View::Chains || (g_view==View::Sequencer && g_seq_lock_armed)) &&
        g_edit_chain>=0 && g_edit_chain<NUM_CHAINS && g_edit_slot>=0 && g_edit_slot<UNITS_PER_CHAIN;
}
bool lock_context() {
    return g_seq_lock_armed && g_seq_lock_chain==g_edit_chain &&
        g_seq_lock_step>=0 && g_seq_lock_step<SEQ_STEPS;
}
q15 edit_value(const C& u,int p) {
    return lock_context()?seq_display_value(g_edit_slot,p,u.params[p].base):u.params[p].base;
}
bool can_compare(const C& u) {
    return !g_seq_lock_armed && !u.bypass && !g_eng->chains[g_edit_chain].mute &&
        u.ready() && u.visual_ready() && !g_eng->recording_active_or_pending();
}
bool owns(const C& u) {
    return context() && gesture.engine==g_eng && gesture.unit==uintptr_t(&u) && gesture.view==g_view &&
        gesture.chain==g_edit_chain && gesture.slot==g_edit_slot && gesture.bank==g_eng->active_pattern &&
        g_eng->chains[g_edit_chain].units[g_edit_slot]==&u && gesture.locked==g_seq_lock_armed &&
        gesture.selection==selected && (!gesture.locked || (lock_context() &&
        gesture.step==g_seq_lock_step && gesture.lock_chain==g_seq_lock_chain));
}
void start(C& u,int p,int x,bool compare=false) {
    gesture={};gesture.engine=g_eng;gesture.unit=uintptr_t(&u);gesture.view=g_view;
    gesture.chain=g_edit_chain;gesture.slot=g_edit_slot;gesture.bank=g_eng->active_pattern;
    gesture.step=g_seq_lock_step;gesture.lock_chain=g_seq_lock_chain;gesture.locked=g_seq_lock_armed;
    gesture.param=p;gesture.selection=selected;gesture.x=gesture.last_x=x;
    gesture.value=p>=0?edit_value(u,p):0;gesture.compare=compare;g_drag=TOKEN;
}
int position(int p,q15 v) {
    const int lo=p==C::P_SPREAD?-32767:0;
    return RAIL_X+(std::max(lo,int(v))-lo)*(RAIL_END-RAIL_X)/(32767-lo);
}
void right(int x,int y,const char* text,u32 color) {ui::text(x-ui::text_w(text),y,text,color);}
void value_text(int p,q15 value,char* out,size_t size) {
    const unsigned raw=chorus_motion::unipolar_raw(value);
    if(p==C::P_SPREAD)std::snprintf(out,size,"%u deg",unsigned(chorus_motion::spread_degrees(value)));
    else if(p==C::P_RATE) {
        const unsigned cent=(uint64_t(chorus_motion::rate_increment(raw))*ENGINE_SR*100+(1ull<<31))>>32;
        std::snprintf(out,size,"%u.%02uhz",cent/100,cent%100);
    } else if(p==C::P_DEPTH) {
        const unsigned cent=(chorus_motion::depth_samples(raw)*100000+ENGINE_SR/2)/ENGINE_SR;
        std::snprintf(out,size,"%u.%02ums",cent/100,cent%100);
    } else std::snprintf(out,size,"%u%%",(raw*100+16383)/32767);
}
void delay_text(char* out,size_t size,uint32_t delay) {
    const unsigned cent=(uint64_t(delay)*100000+(uint64_t(ENGINE_SR)<<15))/(uint64_t(ENGINE_SR)<<16);
    std::snprintf(out,size,"%u.%02ums",cent/100,cent%100);
}
void source_text(char* out,size_t size,ModSource source) {
    const int s=mod_source_sigil(source);
    if(s>=0)std::snprintf(out,size,"%c",'a'+s);
    else std::snprintf(out,size,"env");
}
const char* status() {
    return frame.bypass?"bypassed":frame.mute?"chain muted":!frame.ready?"await audio":
        frame.compare?"dry compare":frame.compare_gain?"returning":frame.live[C::P_MIX]==0?"dry only":"live";
}
// One compile-time engraved template, shared by both copies. Seven ribs are
// graphical texture, NOT extra voices, PCM, a spectrum or stereo channels.
struct Point {int16_t x,y;};
struct Lens {Point outline[65]={};Point ribs[7][33]={};};
constexpr int rounded(int n,int d) {return n<0?-((-n+d/2)/d):(n+d/2)/d;}
constexpr int sine(int i) {return int(chorus_motion::SINE_Q16[i&255])-32768;}
constexpr Lens lens_template() {
    Lens out;
    for(int j=0;j<=64;++j)out.outline[j]={int16_t(rounded(86*sine(j*4+64),32768)),int16_t(rounded(46*sine(j*4),32768))};
    for(int r=-3;r<=3;++r)for(int j=0;j<=32;++j)
        out.ribs[r+3][j]={int16_t(rounded(86*r*sine(j*4),4*32768)),int16_t(-rounded(46*sine(j*4+64),32768))};
    return out;
}
constexpr Lens LENS=lens_template();
u32 blend(u32 a,u32 b,unsigned amount) {
    u32 out=0xff000000u;
    for(int shift=0;shift<24;shift+=8)out|=((((a>>shift)&255)*(255-amount)+((b>>shift)&255)*amount+127)/255)<<shift;
    return out;
}
// palette::branch is a white surface role in legacy graphite, not quiet line ink.
u32 quiet_branch() {return blend(m::paper_color(109),m::ghost(),100);}
unsigned root(unsigned n) {unsigned r=0;for(unsigned bit=1u<<14;bit;bit>>=2){if(n>=r+bit){n-=r+bit;r=(r>>1)+bit;}else r>>=1;}return r;}
void lens(int cx,u32 ink) {
    const auto path=[&](const Point* p,int count) {for(int j=1;j<count;++j)
        ui::line(cx+p[j-1].x,109+p[j-1].y,cx+p[j].x,109+p[j].y,ink);};
    path(LENS.outline,65);for(const auto& rib:LENS.ribs)path(rib,33);
}
} // namespace

int chorus_focus_selected() {return selected;}
void chorus_focus_select(int p) {
    if(p<0 || p>=C::P_COUNT)return;
    if(p!=selected)chorus_focus_release();
    selected=p;
}
void chorus_focus_release() {
    // Never dereference a retained pointer: deletion/replacement or a whole
    // engine switch may already have happened. Locate only a currently owned unit.
    if(gesture.compare && g_eng && g_audio && gesture.engine==g_eng) {
        platform::AudioOut::Guard guard(*g_audio);
        for(auto& chain:g_eng->chains)for(Unit* unit:chain.units)
            if(uintptr_t(unit)==gesture.unit && unit && unit->kind==UnitKind::Chorus)
                static_cast<C*>(unit)->set_dry_compare(false);
    }
    if(gesture.unit && g_drag==TOKEN)g_drag=Drag::None;
    gesture={};
}
bool chorus_focus_touch_body(C& u,int tx,int ty) {
    chorus_focus_release();
    if(!context())return false;
    platform::AudioOut::Guard guard(*g_audio);
    if(g_eng->chains[g_edit_chain].units[g_edit_slot]!=&u)return false;
    if(ty<28) {
        if(!g_seq_lock_armed && hit(tx,ty,244,0,37,24)){u.bypass=!u.bypass;mark_project_mutated();}
        return true;
    }
    if(hit(tx,ty,16,194,100,24)) {
        if(can_compare(u)){start(u,-1,tx,true);u.set_dry_compare(true);}
        return true;
    }
    if(!hit(tx,ty,8,39,304,148))return true;
    const int p=(ty-39)/37;selected=p;
    if(g_seq_lock_armed && !lock_context())return true;
    if(lock_context() && hit(tx,ty,12,43+37*p,52,14)) {
        if(g_eng->remove_step_lock(g_seq_lock_chain,g_seq_lock_step,g_edit_slot,p))mark_project_mutated();
        return true;
    }
    start(u,p,tx);return true;
}
void chorus_focus_drag(C& u,int tx,int ty) {
    if(!gesture.unit)return;
    if(!context() || g_drag!=TOKEN){chorus_focus_release();return;}
    platform::AudioOut::Guard guard(*g_audio);
    if(!owns(u)){chorus_focus_release();return;}
    if(gesture.compare) {
        if(!hit(tx,ty,16,194,100,24) || !can_compare(u) || !u.dry_compare_requested())chorus_focus_release();
        else u.set_dry_compare(true); // refresh the bounded audio lease only on owned touch
        return;
    }
    if(tx==gesture.last_x)return;
    gesture.last_x=tx;
    const int lo=gesture.param==C::P_SPREAD?-32767:0;
    const q15 next=q15(std::max(lo,std::min(32767,int(gesture.value)+(tx-gesture.x)*(32767-lo)/(RAIL_END-RAIL_X))));
    if(next==edit_value(u,gesture.param))return;
    if(g_seq_lock_armed)seq_record_lock(g_edit_slot,gesture.param,next);
    else {u.params[gesture.param].base=next;mark_project_mutated();}
}
void chorus_focus_capture() {
    frame={};
    if(!context(false)){chorus_focus_release();return;}
    {
        platform::AudioOut::Guard guard(*g_audio);
        const auto& chain=g_eng->chains[g_edit_chain];const Unit* unit=chain.units[g_edit_slot];
        if(!unit || unit->kind!=UnitKind::Chorus){chorus_focus_release();return;}
        const auto& u=*static_cast<const C*>(unit);
        if(gesture.unit && (!owns(u) || g_drag!=TOKEN ||
            (gesture.compare && (!can_compare(u) || !u.dry_compare_requested()))))chorus_focus_release();
        frame.bypass=u.bypass;frame.mute=chain.mute;frame.ready=u.visual_ready();
        frame.lock=lock_context();frame.lens=param_lens_is_open();
        frame.full=frame.lock && (g_seq_lock_flash>0 || g_eng->pattern.steps[g_edit_chain][g_seq_lock_step].lock_count>=SEQ_LOCKS);
        frame.chain=g_edit_chain;frame.slot=g_edit_slot;frame.selected=selected;
        frame.compare=gesture.compare && u.dry_compare_requested();frame.compare_gain=u.visual_compare_q15();
        frame.compare_allowed=can_compare(u);frame.mod_links=focus_mod_links(u);
        for(int p=0;p<C::P_COUNT;++p) {
            frame.base[p]=u.params[p].base;frame.edit[p]=edit_value(u,p);
            frame.live[p]=frame.ready?u.visual_effective(p):u.params[p].base;
            frame.source[p]=uint8_t(u.params[p].src);
        }
        for(int v=0;v<2;++v)frame.delay[v]=u.visual_delay_q16(v);
        for(int s=0;s<UNITS_PER_CHAIN;++s) {
            const Unit* at=chain.units[s];frame.kinds[s]=uint8_t(at?at->kind:UnitKind::Empty);
            frame.crossed[s]=at && at->bypass;
        }
        g_eng->capture_param_inspector(g_edit_chain,g_edit_slot,selected,frame.detail);frame.valid=frame.detail.valid;
    }
    // 16px/ms signed separation of two real last-processed read delays. No UI
    // clock/free-running animation; the same frozen pair feeds both LCDs.
    const int delta=rounded(int(frame.delay[0])-int(frame.delay[1]),262144);
    frame.center[0]=200+delta;frame.center[1]=200-delta;
}
void chorus_focus_draw(const C&) {
    m::paper(320);if(!frame.valid)return;
    char text[48],value[24];
    module_heading::header(UnitKind::Chorus,m::spot());
    std::snprintf(text,sizeof text,"ch%02d / %02d",frame.chain+1,frame.slot+1);ui::text(70,m::kHeaderTextY,text,m::ghost());
    ui::text(251,m::kHeaderTextY,"byp",frame.bypass?m::spot():m::ghost());right(m::right_edge(320),m::kHeaderTextY,"x del",m::ghost());
    if(frame.bypass)ui::line(251,m::kHeaderMarkY,268,m::kHeaderMarkY,m::spot());
    if(frame.lens) { // no competing text underneath the shared translucent MOD sheet
        ui::text(16,32,NAMES[frame.selected],m::spot());ui::text(m::kLeft,m::kTextY,"b / return",m::ghost());return;
    }
    const bool quiet=frame.bypass || frame.mute || !frame.ready;
    ui::text(16,32,"parameter",m::ghost());right(304,32,frame.lock?"lock draft":!frame.ready?"base":quiet?"last":"live",m::ghost());
    for(int p=0;p<C::P_COUNT;++p) {
        const int y=47+37*p;
        const u32 ink=quiet?m::ghost():frame.selected==p?m::spot():m::ink();
        ui::text(16,y,NAMES[p],ink);
        if(frame.source[p]) {
            source_text(text,sizeof text,ModSource(frame.source[p]));right(218,y,text,frame.mod_links&(1u<<p)?m::spot():m::ghost());
            if(!(frame.mod_links&(1u<<p)))ui::line(218-ui::text_w(text),y+4,217,y+4,m::ghost());
        }
        value_text(p,frame.lock?frame.edit[p]:frame.live[p],value,sizeof value);right(304,y,value,ink);
        value_text(p,frame.lock?frame.live[p]:frame.base[p],value,sizeof value);
        std::snprintf(text,sizeof text,"%s %s",frame.lock?"live":"base",value);ui::text(16,y+16,text,m::ghost());
        module_fader::horizontal(RAIL_X,RAIL_END,y+19,position(p,frame.edit[p]),position(p,frame.live[p]),
            {bool(frame.mod_links&(1u<<p)),false,quiet});
        if(frame.selected==p)ui::line(8,y,8,y+7,m::spot());
    }
    ui::text(20,202,frame.compare?"dry held":"hold dry",frame.compare?m::spot():frame.compare_allowed?m::ghost():quiet_branch());
    ui::text(134,202,frame.compare?"release to return":frame.full?"lock full":frame.lock?"tap name: clear":
        frame.compare_allowed?"compare":"unavailable",m::ghost());
    if(frame.compare)ui::line(20,215,67,215,m::spot());
    ui::text(m::kLeft,m::kTextY,"b back",m::ghost());if(!frame.lock)right(m::right_edge(320),m::kTextY,"l: mod >",m::ghost());
}
bool chorus_focus_draw_top() {
    if(!frame.valid)return false;
    m::paper(400);char text[96],a[24],b[24];
    module_heading::header(UnitKind::Chorus,m::spot());
    std::snprintf(text,sizeof text,"ch%02d / %02d",frame.chain+1,frame.slot+1);right(m::right_edge(400),m::kHeaderTextY,text,m::ghost());
    int x=14;
    for(int s=0;s<UNITS_PER_CHAIN;++s) {
        const auto kind=UnitKind(frame.kinds[s]);if(kind==UnitKind::Empty)continue;
        if(x>14)ui::text(x-9,module_heading::kRouteY,"/",m::ghost());
        const char* name=m::module_name(kind);const int w=ui::text_w(name);
        ui::text(x,module_heading::kRouteY,name,s==frame.slot?m::spot():m::ghost());
        if(s==frame.slot)ui::line(x,module_heading::kRouteUnderlineY,x+w-2,module_heading::kRouteUnderlineY,m::spot());
        if(frame.crossed[s])ui::line(x,module_heading::kRouteCrossY,x+w-2,module_heading::kRouteCrossY,m::ghost());
        x+=w+12;
    }
    if(!frame.ready || (!frame.live[C::P_MIX] && !frame.compare)) {
        ui::line(frame.ready?155:188,110,frame.ready?245:212,110,quiet_branch());
        const char* word=frame.ready?"dry only":"await audio";ui::text(200-ui::text_w(word)/2,135,word,m::ghost());
    } else {
        const unsigned emphasis=root(unsigned(frame.live[C::P_MIX])*65025u/32767u);
        const bool quiet=frame.bypass || frame.mute || frame.compare || frame.compare_gain;
        const u32 branch=quiet_branch();
        const u32 colors[]={quiet?branch:blend(branch,m::ink(),87+61*emphasis/255),
            quiet?blend(branch,m::ghost(),178):blend(branch,m::spot(),128+64*emphasis/255)};
        for(int i=0;i<2;++i) {
            lens(frame.center[i],colors[i]);const int y=i?159:59;
            if(i) {ui::line(frame.center[i]-1,y-1,frame.center[i]+1,y-1,colors[i]);
                ui::line(frame.center[i]-1,y+1,frame.center[i]+1,y+1,colors[i]);
                ui::rect(frame.center[i]-1,y,1,1,colors[i]);ui::rect(frame.center[i]+1,y,1,1,colors[i]);}
            else ui::rect(frame.center[i]-1,y-1,3,3,colors[i]);
        }
    }
    for(int i=0;i<2;++i) {
        if(frame.ready)delay_text(a,sizeof a,frame.delay[i]);else std::snprintf(a,sizeof a,"-- ms");
        std::snprintf(text,sizeof text,"%d / %s",i+1,a);
        if(i)right(384,175,text,m::ghost());else ui::text(16,175,text,m::ghost());
    }
    const char* caption=frame.bypass||frame.mute?"last read":!frame.ready?"no read":"read delays";
    ui::text(200-ui::text_w(caption)/2,175,caption,m::ghost());ui::line(16,194,384,194,quiet_branch());
    const int p=frame.selected;ui::text(16,205,NAMES[p],m::spot());
    value_text(p,frame.lock?frame.edit[p]:frame.live[p],a,sizeof a);
    std::snprintf(text,sizeof text,"%s%s",frame.lock?"draft ":"",a);right(222,205,text,m::ink());
    value_text(p,frame.lock?frame.live[p]:frame.base[p],a,sizeof a);
    std::snprintf(text,sizeof text,"%s %s",frame.lock?"live":"base",a);right(384,205,text,m::ghost());
    const auto& d=frame.detail;
    if(d.source==ModSource::None)std::snprintf(text,sizeof text,"delay model / mod off%s",d.layer?" / override":"");
    else {
        char src[8];source_text(src,sizeof src,d.source);
        value_text(p,d.range_min,a,sizeof a);value_text(p,d.range_max,b,sizeof b);
        std::snprintf(text,sizeof text,"%s %s..%s%s",src,a,b,!d.mod_enabled?" / off":d.clip_low||d.clip_high?" !":"");
    }
    ui::text(m::kLeft,m::kTextY,text,m::ghost());right(m::right_edge(400),m::kTextY,status(),m::ghost());
    return true;
}
} // namespace seance::platform::ui3ds
