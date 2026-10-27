#include "ui_module_focus_internal.h"
#include "ui_state.h"
#include "ui_sequencer.h"
#include "ui_theme.h"
#include "ui_param_lens.h"
#include "module_inspector.h"
#include "editor_style.h"
#include "module_heading.h"
#include "inspector_gothic.h"
#include "audio_out.h"
#include "../../core/engine/vca_envelope.h"
#include "../../core/engine/vca_timing.h"
#include "../../core/ui/draw.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <type_traits>

namespace seance::platform::ui3ds {
namespace {
namespace m = editor_style;
namespace ve = seance::vca_envelope;
namespace vt = seance::vca_timing;
using V = VcaUnit;
constexpr int X0=40, X1=259, Y0=76, Y1=150;
struct Frame {
    uint16_t mod_links=0;
    ParamInspector detail;
    q15 edit[V::P_COUNT]={}, sounding[V::P_COUNT]={};
    uint32_t progress=0;
    uint16_t opening=0, gain=0;
    uint8_t stage=0;
    int8_t chain=0, slot=0, selected=V::P_DECAY;
    bool valid=false, flexible=true, ready=false, bypass=false, mute=false;
    bool lock=false, lock_full=false, lens=false, gain_only=false;
    bool modern=false, setup=false, lock_armed=false, cycling=false;
    int32_t bpm=120*65536;
    uint32_t latched[3]={}, times[3]={}, total=2;
    uint8_t plot[353]={}, kinds[UNITS_PER_CHAIN]={};
    bool bypassed[UNITS_PER_CHAIN]={};
    int16_t dot_x=-1, dot_y=0;
    int8_t route_first=0,route_last=UNITS_PER_CHAIN-1;
    char values[V::P_COUNT][20]={}, summary[40]={}, multiplier[48]={}, ticks[5][16]={};
} frame;
static_assert(sizeof(Frame)<=1280 && std::is_trivially_copyable<Frame>::value,
              "bounded pointer-free dual VCA frame");
int selected=V::P_DECAY, drag_param=-1, origin_x=0, origin_y=0, drag_extent=160, drag_sign=1;
int origin_chain=-1, origin_slot=-1, origin_step=-1, origin_lock_chain=-1;
bool origin_locked=false, drag_horizontal=false, drag_time=false, origin_flexible=false;
uintptr_t origin_unit=0;
q15 origin_value=0;
uint32_t origin_time=0, origin_total=0;
Engine* origin_engine=nullptr;View origin_view=View::Chains;
int origin_bank=-1;
bool origin_setup=false,origin_modern=false,origin_sync=false,origin_fine=false;
bool setup=false,fine_held=false,blocked_frame=false;
Engine* bound_engine=nullptr;uintptr_t bound_unit=0;
int bound_chain=-1,bound_slot=-1,bound_count=-1;
constexpr int main_params[4][2]={{V::P_ATTACK,V::P_HOLD},{V::P_DECAY,V::P_GAIN},
    {V::P_A_CURVE,V::P_D_CURVE},{V::P_FLOOR,V::P_DUCK}};
constexpr const char* main_names[4][2]={{"attack","hold"},{"decay","gain"},
    {"a curve","d curve"},{"floor","action"}};
constexpr int rows[4]={44,90,136,182};
constexpr int lefts[2]={14,172},rights[2]={148,306};
int clamp(int v,int lo=0) { return std::max(lo,std::min(32767,v)); }
bool time_param(int p) { return p==V::P_ATTACK || p==V::P_HOLD || p==V::P_DECAY; }
bool curve_param(int p) { return p==V::P_A_CURVE || p==V::P_D_CURVE; }
bool context(bool drawing=false) {
    return g_eng && g_audio && (drawing || (!blocked_frame && !param_lens_is_open())) && g_overlay==Overlay::Editor &&
        (g_view==View::Chains || (g_view==View::Sequencer && g_seq_lock_armed)) &&
        g_edit_chain>=0 && g_edit_chain<NUM_CHAINS && g_edit_slot>=0 && g_edit_slot<UNITS_PER_CHAIN;
}
bool lock_context() {
    return g_seq_lock_armed && g_seq_lock_chain==g_edit_chain && g_seq_lock_step>=0 && g_seq_lock_step<SEQ_STEPS;
}
int bank() { const int b=sequencer_edit_bank();return b>=0&&b<NUM_PATTERNS?b:g_eng->active_pattern; }
int gesture_bank() { return g_seq_lock_armed?bank():g_eng->active_pattern; }
q15 edited(const V& u,int p) {
    if(lock_context()) {
        const auto& step=g_eng->sequence_pattern(bank()).steps[g_edit_chain][g_seq_lock_step];
        for(int i=0;i<std::min(int(step.lock_count),SEQ_LOCKS);++i)
            if(step.locks[i].slot==g_edit_slot&&step.locks[i].param==p)return step.locks[i].value;
    }
    return u.params[p].base;
}
Drag drag_kind(int p) {
    return p==V::P_GAIN?Drag::VcaGain:p==V::P_ATTACK?Drag::VcaAttack:p==V::P_DECAY?Drag::VcaDecay:Drag::ParamBase;
}
void assign(V& u,int p,q15 value) {
    if((g_seq_lock_armed && !lock_context()) || p<0 || p>=u.num_params)return;
    if(lock_context()) {
        auto& step=g_eng->sequence_pattern(bank()).steps[g_edit_chain][g_seq_lock_step];
        const int count=std::min(int(step.lock_count),SEQ_LOCKS);int found=-1;
        for(int i=0;i<count;++i)if(step.locks[i].slot==g_edit_slot&&step.locks[i].param==p){found=i;break;}
        if(found>=0){if(step.locks[found].value==value)return;step.locks[found].value=value;}
        else {if(count>=SEQ_LOCKS){g_seq_lock_flash=90;return;}
            step.locks[count]={uint8_t(g_edit_slot),uint8_t(p),value};step.lock_count=uint8_t(count+1);}
    } else {if(value==u.params[p].base)return;u.params[p].base=value;}
    mark_project_mutated();
}
uint32_t duration(q15 value,int p,bool flexible) {
    if(flexible)return ve::time_frames(value,p==V::P_HOLD);
    if(p==V::P_HOLD)return 0;
    const uint64_t v=clamp(value);
    return uint32_t(((((v*v)>>15)*32000)>>15)+1);
}
struct Shape {
    q15 p[V::P_COUNT]={};
    uint32_t a=1,h=0,d=1,total=2;
    bool flexible=true, flat=false;
    Shape(const q15* values,bool modern):flexible(modern) {
        for(int i=0;i<V::P_COUNT;++i)p[i]=values[i];
        if(!modern) {p[V::P_HOLD]=p[V::P_FLOOR]=p[V::P_A_CURVE]=p[V::P_D_CURVE]=p[V::P_DUCK]=0;}
        const bool sync=modern&&frame.modern&&vt::enabled(p[V::P_SYNC]);
        a=sync?vt::sync_frames(p[V::P_ATTACK],frame.bpm):duration(p[V::P_ATTACK],V::P_ATTACK,modern);
        h=sync?vt::sync_frames(p[V::P_HOLD],frame.bpm,true):duration(p[V::P_HOLD],V::P_HOLD,modern);
        d=sync?vt::sync_frames(p[V::P_DECAY],frame.bpm):duration(p[V::P_DECAY],V::P_DECAY,modern);total=a+h+d;
        flat=!modern && p[V::P_ATTACK]<128 && p[V::P_DECAY]<128;
    }
    bool duck() const { return p[V::P_DUCK]>=16384; }
    q15 at(uint32_t t) const {
        if(flat)return 32767;
        q15 e=0;
        if(t<a) e=ve::curve(q15(uint64_t(t)*32767/a),p[V::P_A_CURVE]);
        else if(t<a+h)e=32767;
        else if(t<total)e=ve::curve(q15(uint64_t(total-t)*32767/d),q15(-clamp(p[V::P_D_CURVE],-32767)));
        return ve::opening(e,p[V::P_FLOOR],duck());
    }
    int x(uint32_t t,int left=X0,int right=X1) const { return left+int(uint64_t(right-left)*t/total); }
    int y(q15 v,int high=Y0,int low=Y1) const { return low-(clamp(v)*(low-high)+16383)/32767; }
};
void bead(int x,int y,int r,u32 color,bool filled=false) {
    for(int dy=-r;dy<=r;++dy) {
        int w=0;while((w+1)*(w+1)+dy*dy<=r*r)++w;
        int hole=-1;
        if(!filled && r>1 && std::abs(dy)<=r-2) {
            hole=0;while((hole+1)*(hole+1)+dy*dy<=(r-2)*(r-2))++hole;
        }
        if(hole<0)ui::rect(x-w,y+dy,2*w+1,1,color);
        else {
            if(w>hole) {ui::rect(x-w,y+dy,w-hole,1,color);ui::rect(x+hole+1,y+dy,w-hole,1,color);}
            m::paper_rect(x-hole,y+dy,2*hole+1,1);
        }
    }
}
void diamond(int x,int y,u32 color) {
    ui::line(x,y-3,x+3,y,color);ui::line(x+3,y,x,y+3,color);
    ui::line(x,y+3,x-3,y,color);ui::line(x-3,y,x,y-3,color);
}
void dotted(int left,int right,int y,u32 color) { for(int x=left;x<=right;x+=4)ui::rect(x,y,1,1,color); }
u32 chosen(int p) { return frame.selected==p?m::spot():m::ink(); }
void value_text(char* out,size_t size,int p,q15 value,bool) {
    vca_focus_value_text(out,size,p,value);
}
void compact(char* out,size_t size,int p,q15 value) {
    if(time_param(p)) {
        const uint32_t f=duration(value,p,frame.flexible);
        const unsigned ms=(f+16)/32;
        if(ms>=1000) {const unsigned cent=(ms+5)/10;std::snprintf(out,size,"%u.%02us",cent/100,cent%100);}
        else std::snprintf(out,size,"%ums",ms);
    } else if(curve_param(p))std::snprintf(out,size,"%+d",int(value)*100/32767);
    else std::snprintf(out,size,"%d%%",(clamp(value)*100+16383)/32767);
}
void right(int x,int y,const char* text,u32 color) { ui::text(x-ui::text_w(text),y,text,color); }
void draw_shape(const Shape& s,int left,int high,int right_x,int low,bool thick) {
    int py=s.y(s.at(0),high,low);
    for(int x=left+1;x<=right_x;++x) {
        const uint32_t t=uint64_t(x-left)*s.total/(right_x-left);
        const int y=s.y(s.at(t),high,low);
        const int p=t<s.a?V::P_ATTACK:t<s.a+s.h?V::P_HOLD:V::P_DECAY;
        const bool curve_selected=(p==V::P_ATTACK && frame.selected==V::P_A_CURVE) ||
                                  (p==V::P_DECAY && frame.selected==V::P_D_CURVE);
        ui::line(x-1,py,x,y,curve_selected?m::spot():chosen(p),thick?2:1);py=y;
    }
}
void live_dot(int left,int high,int right_x,int low,int radius) {
    if(!frame.ready || !frame.stage || frame.bypass || frame.mute || frame.gain_only)return;
    // Stage position and vertical opening are independent REAL DSP scalars.
    // The drawn shape is a from-rest parameter diagram, not a retrigger history.
    const Shape live(frame.sounding,frame.flexible);
    uint32_t t=0;
    if(frame.stage==1)t=uint64_t(live.a)*frame.progress/65536;
    else if(frame.stage==2)t=live.a+uint64_t(live.h)*frame.progress/65536;
    else t=live.a+live.h+uint64_t(live.d)*frame.progress/65536;
    if(!frame.flexible) {
        // Legacy x is a projection onto its nominal linear diagram, not a timer.
        const uint32_t p=frame.stage==1?frame.opening:32767-frame.opening;
        t=(frame.stage==1?0:live.a)+uint64_t(frame.stage==1?live.a:live.d)*p/32767;
    }
    t=std::min(t,live.total);
    const int x=live.x(t,left,right_x),y=live.y(frame.opening,high,low);
    bead(x,y,radius,m::spot());if(radius>2)bead(x,y,1,m::spot(),true);
}
const char* stage_name() {
    if(frame.bypass)return "bypassed";
    if(frame.mute)return "chain muted";
    if(!frame.ready)return "await audio";
    if(frame.gain_only)return "gain only";
    if(frame.modern&&vt::enabled(frame.edit[V::P_RUN])&&!frame.stage)return "wait trigger";
    static const char* names[]={"idle","attack","hold","decay"};
    return names[frame.stage<4?frame.stage:0];
}
const char* title() {
    static const char* names[]={"gain / output ceiling","attack / rise time","decay / tail time",
        "hold / plateau time","floor / minimum opening","attack / curve bend","decay / curve bend","polarity / trigger action",
        "time / free or stage-entry sync","retrigger / while envelope active","run / trigger-armed cycle"};
    if(!frame.flexible && frame.selected==V::P_ATTACK)return "attack / legacy nominal time";
    if(!frame.flexible && frame.selected==V::P_DECAY)return "decay / legacy nominal time";
    return names[frame.selected];
}
void hero(const char* value) {
    int width=0;
    for(const char* p=value;*p;++p) {
        if(const auto* glyph=inspector_gothic::digit(*p))width+=glyph->width+2;
        else if(*p=='.')width+=6;
        else if(*p=='+' || *p=='-')width+=18;
        else break;
    }
    const int scale=frame.flexible?1:(width*2<=210?2:1);
    const auto result=inspector_gothic::draw_numeric(14,frame.flexible?130:134,value,m::ink(),m::spot(),scale);
    if(!result.drawn)ui::text(14,112,value,m::ink(),2);
    else if(frame.flexible)ui::text(result.end_x+7,124,result.suffix,m::ghost());
    else ui::text(14,150,result.suffix,m::ghost());
}
void bind(const V& u) {
    if(bound_engine==g_eng&&bound_unit==uintptr_t(&u)&&bound_chain==g_edit_chain&&bound_slot==g_edit_slot&&bound_count==u.num_params)return;
    vca_focus_release();setup=false;selected=V::P_DECAY;
    bound_engine=g_eng;bound_unit=uintptr_t(&u);bound_chain=g_edit_chain;bound_slot=g_edit_slot;bound_count=u.num_params;
}
void begin(V& u,int p,int tx,int ty) {
    selected=drag_param=p;origin_x=tx;origin_y=ty;origin_value=edited(u,p);
    origin_chain=g_edit_chain;origin_slot=g_edit_slot;origin_unit=uintptr_t(&u);origin_flexible=u.flexible;
    origin_locked=g_seq_lock_armed;origin_step=g_seq_lock_step;origin_lock_chain=g_seq_lock_chain;
    origin_engine=g_eng;origin_view=g_view;origin_bank=gesture_bank();origin_setup=setup;
    origin_modern=u.modern();origin_sync=u.modern()&&vt::enabled(edited(u,V::P_SYNC));origin_fine=fine_held;
    g_drag=drag_kind(p);g_drag_param=p;
}
bool owns(const V& u) {
    return origin_engine==g_eng&&origin_view==g_view&&origin_bank==gesture_bank()&&origin_unit==uintptr_t(&u)&&
        origin_chain==g_edit_chain&&origin_slot==g_edit_slot&&origin_flexible==u.flexible&&origin_modern==u.modern()&&
        origin_locked==g_seq_lock_armed&&origin_setup==setup&&origin_fine==fine_held&&drag_param==selected&&
        g_drag_param==drag_param&&origin_sync==(u.modern()&&vt::enabled(edited(u,V::P_SYNC)))&&
        g_eng->chains[g_edit_chain].units[g_edit_slot]==&u&&drag_param<u.num_params&&
        (!origin_locked||(lock_context()&&origin_step==g_seq_lock_step&&origin_lock_chain==g_seq_lock_chain));
}
bool remove_name(int p,int x,int y,const char* name,int tx,int ty) {
    if(!lock_context()||!hit(tx,ty,x-3,y-4,ui::text_w(name)+6,16))return false;
    selected=p;if(g_eng->remove_step_lock(g_seq_lock_chain,g_seq_lock_step,g_edit_slot,p,bank()))mark_project_mutated();return true;
}
void elapsed(char* out,size_t size,uint32_t samples,bool units=true) {
    const uint32_t ms=(samples+16)/32;
    if(ms<1000)std::snprintf(out,size,"%u%s",unsigned(ms),units?"ms":"");
    else {const unsigned cent=(ms+5)/10;std::snprintf(out,size,"%u.%02u%s",cent/100,cent%100,units?"s":"");}
}
void format(char* out,size_t size,int p,q15 value,bool concise) {
    if(frame.flexible&&time_param(p)) {
        if(frame.modern&&vt::enabled(frame.edit[V::P_SYNC])) {
            const int i=vt::division(value,p==V::P_HOLD);
            std::snprintf(out,size,"%s",i<0?"off":vt::division_name(i));
        } else elapsed(out,size,ve::time_frames(value,p==V::P_HOLD));
    } else if(concise&&curve_param(p))std::snprintf(out,size,"%+d",clamp(value,-32767)*100/32767);
    else if(concise&&(p==V::P_GAIN||p==V::P_FLOOR))std::snprintf(out,size,"%d%%",(clamp(value)*100+16383)/32767);
    else module_inspector_value_text(out,size,UnitKind::Vca,p,value,frame.flexible);
}
void capture_diagram() {
    const Shape shape(frame.edit,true);
    frame.times[0]=shape.a;frame.times[1]=shape.h;frame.times[2]=shape.d;frame.total=shape.total;
    struct Cache {q15 key[V::P_COUNT]={};int32_t bpm=0;bool modern=false,valid=false;uint8_t plot[353]={};};
    static Cache cache;
    if(!cache.valid||cache.bpm!=frame.bpm||cache.modern!=frame.modern||!std::equal(frame.edit,frame.edit+V::P_COUNT,cache.key)) {
        for(int i=0;i<353;++i)cache.plot[i]=uint8_t(shape.y(shape.at(uint64_t(i)*shape.total/352),88,178));
        std::copy(frame.edit,frame.edit+V::P_COUNT,cache.key);cache.bpm=frame.bpm;cache.modern=frame.modern;cache.valid=true;
    }
    std::copy(std::begin(cache.plot),std::end(cache.plot),frame.plot);
    char span[18];elapsed(span,sizeof span,shape.total);
    std::snprintf(frame.summary,sizeof frame.summary,"%s / %s",frame.modern&&vt::enabled(frame.edit[V::P_SYNC])?"sync":"free",span);
    for(int i=0;i<5;++i)elapsed(frame.ticks[i],sizeof frame.ticks[i],uint64_t(shape.total)*i/4);
    frame.dot_x=-1;
    if(frame.ready&&frame.stage&&!frame.bypass&&!frame.mute&&!frame.gain_only) {
        // Stage progress uses the actual latched sample lengths, not new MOD
        // targets. Project time onto this from-rest edit axis; do not claim the
        // vertical opening lies on its curve (soft retrigger can start nonzero).
        uint64_t t=0;for(int i=0;i<int(frame.stage)-1&&i<3;++i)t+=frame.latched[i];
        t+=uint64_t(frame.latched[std::min(2,int(frame.stage)-1)])*frame.progress/65536;
        frame.dot_x=34+int(std::min<uint64_t>(shape.total,t)*352/shape.total);
        frame.dot_y=shape.y(frame.opening,88,178);
    }
    if(!frame.ready||frame.bypass||frame.mute)std::snprintf(frame.multiplier,sizeof frame.multiplier,"multiplier --");
    else if(!frame.gain)std::snprintf(frame.multiplier,sizeof frame.multiplier,"multiplier 0%% / -inf db");
    else std::snprintf(frame.multiplier,sizeof frame.multiplier,"multiplier %u%% / %.1fdb",(frame.gain*100u+16383)/32767,
        20.0*std::log10(double(frame.gain)/32767.0));
    auto width=[](int first,int last){int w=0;for(int i=first;i<=last;++i)if(UnitKind(frame.kinds[i])!=UnitKind::Empty){if(w)w+=22;w+=ui::text_w(m::module_name(UnitKind(frame.kinds[i])));}return w;};
    // The route and selected readout share a row, but never overlap. Keep the
    // selected slot and complete module labels if a long route needs a window.
    while(width(frame.route_first,frame.route_last)>216) {
        if(frame.slot-frame.route_first>=frame.route_last-frame.slot&&frame.route_first<frame.slot)++frame.route_first;
        else if(frame.route_last>frame.slot)--frame.route_last;else break;
    }
}
int position(q15 value,int p,int left,int end) {
    const bool bi=curve_param(p);return left+(clamp(value,bi?-32767:0)+(bi?32767:0))*(end-left)/(bi?65534:32767);
}
void option(int x,int y,const char* text,bool on) {
    const auto c=on?m::spot():m::ghost();ui::text(x,y,text,c);
    if(on)ui::line(x,y+12,x+ui::text_w(text)-2,y+12,c);
}
void draw_flexible() {
    if(frame.setup) {
        if(!frame.modern) {
            ui::text(14,45,"legacy ahd / eight parameters",m::ink());
            ui::text(14,73,"original sound and free stage times",m::ghost());
            ui::text(14,106,"time / retrigger / cycle",m::ghost());
            option(222,133,"extend",false);
            ui::text(14,173,frame.lock_armed?"exit p-lock to extend":"explicit upgrade / keeps current ahd",m::ghost());
            ui::text(14,199,"new saved copy needs current reader",m::ghost());
        } else {
            ui::text(14,45,"time",chosen(V::P_SYNC));
            const bool sync=vt::enabled(frame.edit[V::P_SYNC]);
            option(174,45,"free",!sync);option(268,45,"sync",sync);
            ui::text(14,69,sync?"stage-entry tempo / not phase locked":"milliseconds / independent stage times",m::ghost());
            ui::line(14,89,306,89,m::branch());
            ui::text(14,105,"retrigger",chosen(V::P_RETRIGGER));
            const int rt=vt::retrigger(frame.edit[V::P_RETRIGGER]);
            option(108,105,"soft",rt==0);option(180,105,"reset",rt==1);option(258,105,"ignore",rt==2);
            const char* help[]={"restarts from current envelope","from zero / 64-sample output crossfade","ignores triggers while envelope active"};
            ui::text(14,129,help[rt],m::ghost());ui::line(14,149,306,149,m::branch());
            ui::text(14,165,"run",chosen(V::P_RUN));const bool cycle=vt::enabled(frame.edit[V::P_RUN]);
            option(150,165,"one shot",!cycle);option(270,165,"cycle",cycle);
            ui::text(14,189,cycle?(frame.cycling?"trigger-armed cycle / active":frame.stage?"finishing / trigger starts cycle":"cycle / waiting for trigger"):"one trigger / one envelope",m::ghost());
            ui::text(14,213,"time mode remaps raw stage values",m::ghost());
        }
    } else for(int row=0;row<4;++row)for(int col=0;col<2;++col) {
        const int p=main_params[row][col],x=lefts[col],end=rights[col],y=rows[row];
        ui::text(x,y,main_names[row][col],frame.selected==p?m::spot():m::ghost());
        if(p==V::P_DUCK){option(x+2,y+23,"open",frame.edit[p]<16384);option(x+96,y+23,"duck",frame.edit[p]>=16384);continue;}
        right(end,y,frame.values[p],chosen(p));
        module_fader::horizontal(x+1,end-1,y+23,
            position(frame.edit[p],p,x+1,end-1),position(frame.sounding[p],p,x+1,end-1),
            {bool(frame.mod_links&(1u<<p)),false,frame.bypass||frame.mute},curve_param(p)?position(0,p,x+1,end-1):-1);
    }
    if(frame.lock)m::footer(320,frame.lock_full?"lock full / name: remove":"p-lock / name: remove","b back");
    else {ui::text(m::kLeft,m::kTextY,frame.setup?"b main":"b back",m::ghost());
        ui::text(112,m::kTextY,frame.setup?"a trig":"a trig/r fine",m::ghost());right(m::right_edge(320),m::kTextY,"l: mod >",m::ghost());}
}
bool draw_flexible_top() {
    m::paper(400);module_heading::header(UnitKind::Vca,m::spot());char text[80];
    std::snprintf(text,sizeof text,"ch%02d / %02d",frame.chain+1,frame.slot+1);right(386,m::kHeaderTextY,text,m::ghost());
    int rx=14;for(int i=frame.route_first;i<=frame.route_last;++i){const auto kind=UnitKind(frame.kinds[i]);if(kind==UnitKind::Empty)continue;
        if(rx>14){ui::text(rx,35,"/",m::ghost());rx+=14;}
        const char* name=m::module_name(kind);const int w=ui::text_w(name);ui::text(rx,35,name,i==frame.slot?m::spot():m::ghost());
        if(i==frame.slot)ui::line(rx,module_frame_layout::kRouteUnderlineY,rx+w-2,module_frame_layout::kRouteUnderlineY,m::spot());
        if(frame.bypassed[i])ui::line(rx,39,rx+w-2,39,m::ghost());
        rx+=w+8;}
    static const char* names[]={"gain","attack","decay","hold","floor","a curve","d curve","action","time","retrigger","run"};
    std::snprintf(text,sizeof text,"%s %s",names[frame.selected],frame.values[frame.selected]);right(386,35,text,m::ink());
    ui::text(14,55,"opening",m::ghost());right(386,55,frame.summary,m::ghost());
    const int stage_params[]={V::P_ATTACK,V::P_HOLD,V::P_DECAY};const char* stage_names[]={"attack","hold","decay"};
    uint32_t time=0;for(int stage=0;stage<3;++stage){const int x0=34+int(uint64_t(time)*352/frame.total);time+=frame.times[stage];
        const int x1=34+int(uint64_t(time)*352/frame.total),w=ui::text_w(stage_names[stage]);
        if(x1-x0>w+6)ui::text((x0+x1-w)/2,70,stage_names[stage],chosen(stage_params[stage]));
        if(stage<2&&x1>34&&x1<386)for(int y=84;y<178;y+=4)ui::rect(x1,y,1,1,palette::shadow());}
    ui::text(14,84,"100",m::ghost());ui::text(14,174,"0",m::ghost());
    dotted(34,386,88,palette::shadow());dotted(34,386,178,palette::shadow());
    const int fy=178-(clamp(frame.edit[V::P_FLOOR])*90+16383)/32767;
    if(fy!=88&&fy!=178)dotted(34,386,fy,frame.selected==V::P_FLOOR?m::spot():palette::shadow());
    for(int i=1;i<353;++i){const uint32_t t=uint64_t(i)*frame.total/352;
        const int p=t<frame.times[0]?V::P_ATTACK:t<frame.times[0]+frame.times[1]?V::P_HOLD:V::P_DECAY;
        const bool accent=frame.selected==p||(p==V::P_ATTACK&&frame.selected==V::P_A_CURVE)||(p==V::P_DECAY&&frame.selected==V::P_D_CURVE);
        ui::line(33+i,frame.plot[i-1],34+i,frame.plot[i],accent?m::spot():m::ink(),2);}
    if(frame.dot_x>=0){for(int y=frame.dot_y+6;y<179;y+=4)ui::rect(frame.dot_x,y,1,1,m::spot());
        bead(frame.dot_x,frame.dot_y,4,m::spot());}
    ui::line(34,179,386,179,palette::shadow());
    for(int i=0;i<5;++i){const int x=34+i*88;ui::line(x,181,x,183,palette::shadow());
        ui::text(i==4?386-ui::text_w(frame.ticks[i]):x,188,frame.ticks[i],m::ghost());}
    if(frame.ready&&!frame.bypass&&!frame.mute)std::snprintf(text,sizeof text,"opening %u%%",(frame.opening*100u+16383)/32767);
    else std::snprintf(text,sizeof text,"opening --");
    ui::text(14,208,text,m::ink());right(386,208,frame.multiplier,m::spot());
    const char* caption=frame.lock_full?"lock full / name: remove":frame.lock?"lock shape / dot: actual":frame.bypass?"bypassed / shape not output":frame.mute?"muted / shape not output":"shape from rest / dot: actual";
    m::footer(400,caption,stage_name());return true;
}
} // namespace

int vca_focus_selected() { return selected; }
void vca_focus_select(int p) { if(p>=0 && p<V::P_COUNT){if(p!=selected)vca_focus_release();selected=p;} }
bool vca_focus_flexible() { return frame.flexible; }
void vca_focus_value_text(char* out,size_t size,int p,q15 value){format(out,size,p,value,false);}
void vca_focus_release() { if(drag_param>=0&&g_drag==drag_kind(drag_param))g_drag=Drag::None;drag_param=-1;origin_unit=0; }
bool vca_focus_back(){if(!context()||!setup)return false;vca_focus_release();setup=false;return true;}
void vca_focus_input(u32 down,u32 held,bool blocked){const bool fine=(held&KEY_R)!=0;
    if(blocked||fine!=fine_held||(down&~u32(KEY_TOUCH|KEY_R)))vca_focus_release();
    fine_held=fine;blocked_frame=blocked;}

bool vca_focus_touch_body(V& u,int tx,int ty) {
    if(!context())return false;
    platform::AudioOut::Guard guard(*g_audio);
    if(g_eng->chains[g_edit_chain].units[g_edit_slot]!=&u)return false;
    bind(u);vca_focus_release();g_drag=Drag::None;
    if(u.flexible&&hit(tx,ty,198,0,48,26)){setup=!setup;return true;}
    if(ty<26) {
        if(!g_seq_lock_armed && hit(tx,ty,244,0,39,26)) {u.bypass=!u.bypass;mark_project_mutated();}
        return true;
    }
    if(u.flexible) {
        if(g_seq_lock_armed&&!lock_context())return true;
        if(setup) {
            if(!u.modern()) {
                if(!g_seq_lock_armed&&hit(tx,ty,212,124,82,32)){V defaults;
                    for(int p=V::AHD_COUNT;p<V::P_COUNT;++p)u.params[p]=defaults.params[p];
                    for(auto& scene:g_eng->scenes[g_edit_chain])if(scene.kind[g_edit_slot]==UnitKind::Vca)
                        for(int p=V::AHD_COUNT;p<V::P_COUNT;++p)scene.base[g_edit_slot][p]=defaults.params[p].base;
                    u.set_modern(true);mark_project_mutated();bind(u);setup=true;}
                return true;
            }
            const int ps[]={V::P_SYNC,V::P_RETRIGGER,V::P_RUN};const char* ns[]={"time","retrigger","run"};
            for(int row=0;row<3;++row)if(hit(tx,ty,8,37+row*60,304,28)) {
                const int p=ps[row];selected=p;if(remove_name(p,14,45+row*60,ns[row],tx,ty))return true;
                if(row==0){if(tx>=160)assign(u,p,tx<240?0:32767);}
                else if(row==1){if(tx>=102)assign(u,p,vt::retrigger_value(tx<164?0:tx<245?1:2));}
                else if(tx>=140)assign(u,p,tx<247?0:32767);
                return true;
            }
            return true;
        }
        for(int row=0;row<4;++row)for(int col=0;col<2;++col){const int p=main_params[row][col],x=lefts[col],y=rows[row];
            if(!hit(tx,ty,x-6,y-8,146,42))continue;
            selected=p;
            if(remove_name(p,x,y,main_names[row][col],tx,ty))return true;
            if(p==V::P_DUCK){if(ty>=y+12)assign(u,p,tx<x+75?0:32767);return true;}
            begin(u,p,tx,ty);return true;
        }
        return true;
    }
    q15 values[V::P_COUNT]={};for(int p=0;p<u.num_params;++p)values[p]=edited(u,p);
    const Shape shape(values,u.flexible);
    int p=-1;bool name=false,node=false;
    if(hit(tx,ty,277,86,37,73) || hit(tx,ty,273,52,41,32))p=V::P_GAIN;
    else if(ty>=161 && ty<195 && tx>=8 && tx<300) {
        const int col=(tx-8)/74;
        static const int params[]={V::P_ATTACK,V::P_HOLD,V::P_DECAY,V::P_FLOOR};
        p=params[col];name=ty<177;
    } else if(u.flexible && ty>=198 && ty<222) {
        if(tx>=8 && tx<156)p=V::P_A_CURVE;
        else if(tx>=164 && tx<312)p=V::P_D_CURVE;
        name=tx<(p==V::P_A_CURVE?58:218);
    } else if(ty>=64 && ty<160 && tx<275) {
        int best=0x7fffffff;
        auto candidate=[&](int param,int x,int y,int radius) {
            if(param>=u.num_params)return;
            const int dx=tx-x,dy=ty-y,d=dx*dx+dy*dy;
            if(d<=radius*radius && d<best) {best=d;p=param;node=true;}
        };
        // Nearest visible handle wins. At coincident/collapsed stages use the
        // stable labelled values below; no invented minimum-width time axis.
        candidate(V::P_ATTACK,shape.x(shape.a),shape.y(shape.at(shape.a)),11);
        if(u.flexible)candidate(V::P_HOLD,shape.x(shape.a+shape.h),shape.y(shape.at(shape.a+shape.h)),11);
        candidate(V::P_DECAY,X1,shape.y(shape.at(shape.total)),11);
        if(u.flexible) {
            candidate(V::P_FLOOR,22,shape.y(values[V::P_FLOOR]),11);
            for(int i=0;i<2;++i) {
                const uint32_t t=i?shape.a+shape.h+shape.d/2:shape.a/2;
                candidate(i?V::P_D_CURVE:V::P_A_CURVE,shape.x(t),shape.y(shape.at(t)),8);
            }
        }
    }
    if(p<0 || p>=u.num_params)return true;
    selected=p;
    if(g_seq_lock_armed && !lock_context())return true;
    // Parameter-name tap clears only a stored lock; values and nodes edit it.
    if(lock_context() && (name || (p==V::P_GAIN && ty>=52 && ty<66))) {
        if(g_eng->remove_step_lock(g_seq_lock_chain,g_seq_lock_step,g_edit_slot,p,bank()))mark_project_mutated();
        return true;
    }
    begin(u,p,tx,ty);
    drag_horizontal=time_param(p);drag_time=node && time_param(p) && u.flexible;
    origin_time=duration(origin_value,p,u.flexible);origin_total=shape.total;
    drag_extent=p==V::P_GAIN?56:p==V::P_FLOOR?Y1-Y0:curve_param(p)?64:160;
    drag_sign=drag_horizontal?1:-1;
    if(curve_param(p))drag_sign=(p==V::P_A_CURVE?1:-1)*(shape.duck()?-1:1);
    g_drag=drag_kind(p);
    return true;
}

void vca_focus_drag(V& u,int tx,int ty) {
    if(drag_param<0)return;
    if(!context() || g_drag!=drag_kind(drag_param)) {vca_focus_release();return;}
    platform::AudioOut::Guard guard(*g_audio);
    if(!owns(u)) {vca_focus_release();return;}
    if(u.flexible) {
        const int range=curve_param(drag_param)?65534:32767,dx=tx-origin_x;
        const int movement=fine_held?dx:dx*range/296;
        int next=clamp(int(origin_value)+movement,curve_param(drag_param)?-32767:0);
        if(curve_param(drag_param)&&((origin_value<0&&next>0)||(origin_value>0&&next<0))&&std::abs(next)<(fine_held?1:range/296))next=0;
        assign(u,drag_param,q15(next));return;
    }
    const int delta=(drag_horizontal?tx-origin_x:ty-origin_y)*drag_sign;
    q15 next;
    if(drag_time) {
        const int64_t frames=int64_t(origin_time)+int64_t(delta)*origin_total/(X1-X0);
        const uint32_t bounded=uint32_t(std::max<int64_t>(drag_param==V::P_HOLD?0:32,std::min<int64_t>(256000,frames)));
        next=ve::time_value(bounded,drag_param==V::P_HOLD);
    } else next=q15(clamp(origin_value+delta*32767/drag_extent,curve_param(drag_param)?-32767:0));
    assign(u,drag_param,next);
}

void vca_focus_capture() {
    frame={};
    if(!context(true)) {vca_focus_release();bound_engine=nullptr;bound_unit=0;setup=false;return;}
    {
        platform::AudioOut::Guard guard(*g_audio);
        const auto& chain=g_eng->chains[g_edit_chain];const Unit* unit=chain.units[g_edit_slot];
        if(!unit || unit->kind!=UnitKind::Vca) {vca_focus_release();bound_engine=nullptr;bound_unit=0;return;}
        const auto& u=*static_cast<const V*>(unit);bind(u);
        if(drag_param>=0&&!owns(u))vca_focus_release();
        if(selected>=u.num_params){selected=V::P_DECAY;vca_focus_release();}
        frame.valid=true;frame.flexible=u.flexible;frame.modern=u.modern();frame.setup=setup;frame.selected=selected;
        frame.chain=g_edit_chain;frame.slot=g_edit_slot;frame.bypass=u.bypass;frame.mute=chain.mute;
        frame.lock=lock_context();frame.lock_armed=g_seq_lock_armed;frame.lens=param_lens_is_open();
        frame.lock_full=frame.lock&&g_eng->sequence_pattern(bank()).steps[g_edit_chain][g_seq_lock_step].lock_count>=SEQ_LOCKS;
        if(frame.lens)vca_focus_release();
        g_eng->capture_param_inspector(g_edit_chain,g_edit_slot,selected,frame.detail);
        frame.ready=u.visual_ready();frame.stage=u.visual_stage();frame.progress=u.visual_progress_q16();
        frame.mod_links=focus_mod_links(u);frame.bpm=u.clock_bpm_q16();frame.cycling=u.visual_cycling();
        frame.opening=u.visual_envelope();frame.gain=u.visual_gain();frame.gain_only=(u.visual_state()&V::VisualGainOnly)!=0;
        const int pp[]={V::P_ATTACK,V::P_HOLD,V::P_DECAY};
        for(int i=0;i<3;++i)frame.latched[i]=u.visual_stage_frames(pp[i]);
        for(int p=0;p<u.num_params;++p){frame.edit[p]=edited(u,p);frame.sounding[p]=frame.ready?u.visual_effective(p):u.params[p].effective;}
        for(int i=0;i<UNITS_PER_CHAIN;++i){frame.kinds[i]=chain.units[i]?uint8_t(chain.units[i]->kind):uint8_t(UnitKind::Empty);frame.bypassed[i]=chain.units[i]&&chain.units[i]->bypass;}
    }
    for(int p=0;p<(frame.modern?V::P_COUNT:frame.flexible?V::AHD_COUNT:V::LEGACY_COUNT);++p)format(frame.values[p],sizeof frame.values[p],p,frame.edit[p],true);
    if(frame.flexible&&!frame.lens)capture_diagram(); // pure model outside the audio guard; never in draw/MOD
}

void vca_focus_draw(const V&) {
    m::paper(320);if(!frame.valid)return;
    char text[64],value[24];
    module_heading::header(UnitKind::Vca,m::spot());
    std::snprintf(text,sizeof text,"ch%02d / %02d",frame.chain+1,frame.slot+1);ui::text(49,m::kHeaderTextY,text,m::ghost());
    ui::text(258,m::kHeaderTextY,"byp",frame.bypass?m::spot():m::ghost());right(m::right_edge(320),m::kHeaderTextY,"x",m::ghost());
    if(frame.bypass)ui::line(258,m::kHeaderMarkY,275,m::kHeaderMarkY,m::spot());
    if(frame.flexible)ui::text(206,m::kHeaderTextY,frame.setup?"main":"setup",m::spot());
    if(frame.lens){m::footer(320,"b back","l: mod >");return;}
    if(frame.flexible){draw_flexible();return;}
    const Shape shape(frame.edit,false);
    if(frame.flexible) {
        ui::text(14,35,"open",shape.duck()?m::ghost():m::spot());
        ui::text(72,35,"duck",shape.duck()?m::spot():m::ghost());
        const int x=shape.duck()?72:14;ui::line(x,46,x+23,46,m::spot());
    } else ui::text(14,35,"legacy ad",m::ghost());
    right(306,35,frame.bypass?"bypassed":frame.mute?"chain muted":shape.flat?"gain only":"one shot",m::ghost());
    ui::text(14,57,"opening",m::ghost());
    const unsigned total_ms=(shape.total+16)/32;
    if(shape.flat)std::snprintf(text,sizeof text,"steady");
    else if(shape.total<32)std::snprintf(text,sizeof text,"0.%02ums",unsigned((uint64_t(shape.total)*100000+ENGINE_SR/2)/ENGINE_SR));
    else if(total_ms<1000)std::snprintf(text,sizeof text,"%ums",total_ms);
    else {const unsigned cent=(total_ms+5)/10;std::snprintf(text,sizeof text,"%u.%02us",cent/100,cent%100);}
    ui::text(153,57,text,m::ghost());
    ui::text(270,57,"gain",chosen(V::P_GAIN));compact(value,sizeof value,V::P_GAIN,frame.edit[V::P_GAIN]);ui::text(270,70,value,chosen(V::P_GAIN));
    const int floor_y=shape.y(shape.p[V::P_FLOOR]);
    dotted(X0,X1,floor_y,frame.selected==V::P_FLOOR?m::spot():m::branch());dotted(X0,X1,Y0,m::branch());
    ui::line(X0,Y1,X1,Y1,m::branch());
    for(int x=X0+5;x<X1;x+=7) {
        const int y=shape.y(shape.at(uint64_t(x-X0)*shape.total/(X1-X0)));
        if(y+4<floor_y-2)ui::line(x,y+4,x,floor_y-2,m::branch());
    }
    draw_shape(shape,X0,Y0,X1,Y1,true);
    if(!shape.flat) {
        const int params[]={V::P_ATTACK,V::P_HOLD,V::P_DECAY};
        const uint32_t times[]={shape.a,shape.a+shape.h,shape.total};
        for(int i=0;i<3;++i)if(frame.flexible || i!=1)
            bead(shape.x(times[i]),shape.y(shape.at(times[i])),frame.selected==params[i]?6:4,chosen(params[i]));
    }
    if(frame.flexible) {
        for(int i=0;i<2;++i) {
            const uint32_t t=i?shape.a+shape.h+shape.d/2:shape.a/2;
            diamond(shape.x(t),shape.y(shape.at(t)),chosen(i?V::P_D_CURVE:V::P_A_CURVE));
        }
        ui::line(22,floor_y,X0-4,floor_y,chosen(V::P_FLOOR));bead(22,floor_y,4,chosen(V::P_FLOOR));
    }
    module_fader::vertical(295,92,148,148-clamp(frame.edit[V::P_GAIN])*56/32767,
        148-clamp(frame.sounding[V::P_GAIN])*56/32767,
        {bool(frame.mod_links&(1u<<V::P_GAIN)),false,frame.bypass||frame.mute});
    live_dot(X0,Y0,X1,Y1,5);
    static const int params[]={V::P_ATTACK,V::P_HOLD,V::P_DECAY,V::P_FLOOR};
    static const char* names[]={"attack","hold","decay","floor"};
    for(int col=0;col<4;++col) {
        const int p=params[col],x=14+col*74;const bool enabled=frame.flexible || p<3;
        ui::text(x,166,names[col],enabled && frame.selected==p?m::spot():m::ghost());
        if(enabled)compact(value,sizeof value,p,frame.edit[p]);else std::snprintf(value,sizeof value,"--");
        ui::text(x,179,value,enabled?chosen(p):m::ghost());
        if(frame.selected==p)ui::line(x,191,x+ui::text_w(value)-1,191,m::spot());
    }
    for(int i=0;i<2;++i) {
        const int p=i?V::P_D_CURVE:V::P_A_CURVE;
        if(frame.flexible)compact(value,sizeof value,p,frame.edit[p]);else std::snprintf(value,sizeof value,"--");
        std::snprintf(text,sizeof text,"%c curve %s",i?'d':'a',value);ui::text(i?174:14,204,text,frame.flexible?chosen(p):m::ghost());
    }
    ui::line(14,223,306,223,m::branch());
    if(frame.lock)ui::text(m::kLeft,m::kTextY,frame.lock_full?"lock full / tap name to clear / b back":"p-lock / tap name to clear / b back",m::ghost());
    else {ui::text(m::kLeft,m::kTextY,"b back",m::ghost());ui::text(118,m::kTextY,"a trig",m::ghost());right(m::right_edge(320),m::kTextY,"l: mod >",m::ghost());}
}

bool vca_focus_draw_top() {
    if(!frame.valid || !frame.detail.valid)return false;
    if(frame.flexible&&!frame.lens)return draw_flexible_top();
    const auto& d=frame.detail;char text[80],value[24];m::paper(400);
    module_heading::header(UnitKind::Vca,m::spot());
    std::snprintf(text,sizeof text,"ch%02d / %02d",frame.chain+1,frame.slot+1);right(m::right_edge(400),m::kHeaderTextY,text,m::ghost());
    ui::text(14,42,title(),m::spot());ui::text(14,58,frame.lock?"edit lock":d.temp?"temporary":"base",m::ghost());
    auto format=[&](q15 v) {value_text(value,sizeof value,d.param,v,frame.flexible);};
    format(d.temp?d.control:frame.edit[frame.selected]);hero(value);
    const bool compact_top=frame.flexible && !frame.lens && !frame.lock;
    format(d.base);std::snprintf(text,sizeof text,"stored base %s",value);ui::text(14,compact_top?149:166,text,m::ghost());
    auto px=[&](q15 v) {return 14+(clamp(v,d.bipolar?-32767:0)+(d.bipolar?32767:0))*210/(d.bipolar?65534:32767);};
    const int sy=compact_top?171:184;
    module_fader::horizontal(14,224,sy,px(frame.edit[frame.selected]),px(d.effective),
        {d.source!=ModSource::None && d.mod_enabled,false,frame.bypass||frame.mute},
        d.bipolar?px(0):-1);
    format(d.bipolar?-32767:0);ui::text(14,compact_top?185:199,value,m::ghost());format(32767);right(224,compact_top?185:199,value,m::ghost());
    ui::line(240,46,240,211,m::branch());
    if(frame.lens || frame.lock || !frame.flexible) {
        ui::text(254,46,"live / resolved",m::ghost());format(d.effective);ui::text(254,60,value,m::spot());
        format(d.control);std::snprintf(text,sizeof text,"control %s",value);ui::text(254,78,text,m::ink());
        format(d.target);std::snprintf(text,sizeof text,"target %s",value);ui::text(254,92,text,m::ghost());
    } else {
        std::snprintf(text,sizeof text,"stage %s",stage_name());ui::text(254,46,text,m::ghost());
        if(frame.ready && !frame.bypass && !frame.mute)std::snprintf(text,sizeof text,"opening %u%%",(frame.opening*100u+16383)/32767);
        else std::snprintf(text,sizeof text,"opening --");
        ui::text(254,66,text,m::ink());
        if(frame.ready && !frame.bypass && !frame.mute)std::snprintf(text,sizeof text,"vca gain %u%%",(frame.gain*100u+16383)/32767);
        else std::snprintf(text,sizeof text,"vca gain --");
        ui::text(254,82,text,m::spot());
    }
    if(d.source==ModSource::None)std::snprintf(text,sizeof text,"mod none");
    else if(d.source==ModSource::EnvFollow)std::snprintf(text,sizeof text,"env ch%d / %s%+d%%",d.env_chain+1,d.mod_enabled?"":"muted ",int(d.depth)*100/32767);
    else std::snprintf(text,sizeof text,"sg.%c / %s%+d%%",'a'+mod_source_sigil(d.source),d.mod_enabled?"":"muted ",int(d.depth)*100/32767);
    ui::text(254,111,text,d.source!=ModSource::None && !d.mod_enabled?m::ghost():m::ink());
    static const char* layers[]={"base","step lock","audition","performance","temp"};
    std::snprintf(text,sizeof text,"via %s",layers[d.layer<5?d.layer:0]);ui::text(254,127,text,m::ghost());
    if(frame.lens || frame.lock || !frame.flexible) {
        ui::text(254,149,d.clip_low||d.clip_high?"range / clips":"potential range",m::ghost());
        format(d.range_min);ui::text(254,164,value,m::ink());format(d.range_max);ui::text(254,178,value,m::ink());
        value_text(value,sizeof value,d.param,frame.sounding[d.param],frame.flexible);
        std::snprintf(text,sizeof text,"sounding %s",frame.ready?value:"--");ui::text(254,199,text,m::ghost());
    } else {
        const Shape shape(frame.edit,frame.flexible);dotted(254,384,202,m::branch());
        draw_shape(shape,254,170,384,202,false);live_dot(254,170,384,202,2);
    }
    const char* explanation=frame.flexible?"shape from rest / dot is actual opening":"legacy ad / original sound and nominal times";
    if(frame.lock_full)explanation="lock full / tap parameter name to clear";
    else if(frame.bypass)explanation="bypassed / diagram is not audible output";
    else if(frame.mute)explanation="chain muted / diagram is not audible output";
    else if(d.temp)explanation="temporary / release l restores stored base";
    else if(frame.flexible && (time_param(d.param)||curve_param(d.param)))explanation="time + curve latch at each stage entry";
    else if(frame.flexible && d.param==V::P_FLOOR) {
        std::snprintf(text,sizeof text,"floor %d%% of gain / minimum %d%% input",(clamp(frame.edit[V::P_FLOOR])*100+16383)/32767,
                      int((int64_t(clamp(frame.edit[V::P_FLOOR]))*clamp(frame.edit[V::P_GAIN])*100+(32767LL*32767)/2)/(32767LL*32767)));
        explanation=text;
    } else if(frame.flexible && d.param==V::P_DUCK)explanation="open rises / duck dips / trigger, not sidechain";
    ui::text(14,217,frame.lens?"mod / b returns to same parameter":"select below / hold l or tap mod",m::ghost());
    ui::text(m::kLeft,m::kTextY,explanation,m::ghost());return true;
}
} // namespace seance::platform::ui3ds
