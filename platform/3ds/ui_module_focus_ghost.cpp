#include "ui_module_focus_internal.h"
#include "ui_param_lens.h"
#include "ui_state.h"
#include "editor_style.h"
#include "module_heading.h"
#include "inspector_gothic.h"
#include "ghost_memory_visual.h"
#include "audio_out.h"
#include "../../core/ui/draw.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace seance::platform::ui3ds {
namespace {
namespace m = editor_style;
using G = GhostUnit;

// Drawing geometry stays unchanged; fixed disjoint touch corridors cover each
// rail rather than chasing a moving stored-value marker.
constexpr int TONE_X0=82,TONE_X1=232,TONE_Y=54;
constexpr int DECAY_X=61,DECAY_Y0=92,DECAY_Y1=174;
constexpr int DRIFT_X=254,DRIFT_Y0=59,DRIFT_Y1=111;
constexpr int SMEAR_X0=111,SMEAR_X1=225,SMEAR_Y=183;
constexpr int MIX_X=294,MIX_Y0=116,MIX_Y1=181;
constexpr int FREEZE_X=160,FREEZE_Y=117;
constexpr const char* names[8]={"root","decay","smear","drift","freeze","mix","fine","cycle"};
constexpr const char* descriptions[8]={"nominal first well","memory feedback","circulation damping",
    "pitch movement","sealed memory","wet level","shared tuning","drift period"};

int selected=G::P_TONE;
bool note_steps=true; // input preference only; toggle never retunes
int origin_x=0,origin_y=0,last_x=0,last_y=0,bound_chain=-1,bound_slot=-1;
int origin_bank=-1,origin_param=-1;
View origin_view=View::Chains;
Drag origin_drag=Drag::None;
Engine* bound_engine=nullptr;
Engine* origin_engine=nullptr;
uintptr_t identity=0,origin_unit=0; // comparison tokens only; never dereferenced
q15 origin_value=0;

struct Frame {
    bool mute=false;
    bool valid=false,bypass=false,long_tail=false,note_steps=true,held=false;
    int8_t chain=-1,slot=-1,selected=0;
    q15 base[8]={},edit[8]={},live[8]={};
    ModSource source[8]={};
    uint16_t energy=0,freeze=0;
    q15 drift=0;
    ParamInspector param;
    uint8_t kinds[UNITS_PER_CHAIN]={}; // frozen IDs; draw full UI names, never sliced core aliases
    bool bypassed[UNITS_PER_CHAIN]={};
    uint8_t mod_enabled_mask=0;
    ghost_memory_visual::Geometry figure;
} frame;
static_assert(sizeof(Frame)<=2048,"Ghost pre-GPU DTO must remain bounded");

q15 clamp_uni(int32_t v){return(q15)(v<0?0:v>32767?32767:v);}
q15 clamp_bi(int32_t v){return(q15)(v<-32767?-32767:v>32767?32767:v);}
bool hit(int x,int y,int rx,int ry,int rw,int rh){return x>=rx&&x<rx+rw&&y>=ry&&y<ry+rh;}
int ux(q15 v,int a,int b){return a+(uint32_t)(uint16_t)v*(b-a)/32767;}
int uy(q15 v,int a,int b){return b-(uint32_t)(uint16_t)v*(b-a)/32767;}
bool context(const G& u) {
    return g_eng&&g_audio&&!param_lens_is_open()&&g_overlay==Overlay::Editor&&
        g_view==View::Chains&&
        g_edit_chain>=0&&g_edit_chain<NUM_CHAINS&&g_edit_slot>=0&&g_edit_slot<UNITS_PER_CHAIN&&
        g_eng->chains[g_edit_chain].units[g_edit_slot]==&u;
}
bool capture_context() {
    return g_eng&&g_audio&&g_overlay==Overlay::Editor&&
        g_view==View::Chains&&
        g_edit_chain>=0&&g_edit_chain<NUM_CHAINS&&g_edit_slot>=0&&g_edit_slot<UNITS_PER_CHAIN;
}
bool ours(){return g_drag>=Drag::GhostTone&&g_drag<=Drag::GhostRate;}
void release(){
    if(origin_unit && ours())g_drag=Drag::None;
    origin_unit=0;origin_engine=nullptr;origin_param=-1;origin_drag=Drag::None;last_x=last_y=0;
}
void bind(const G& u){
    if(bound_engine!=g_eng || identity!=(uintptr_t)&u || bound_chain!=g_edit_chain || bound_slot!=g_edit_slot){
        release();selected=G::P_TONE;identity=(uintptr_t)&u;bound_engine=g_eng;
        bound_chain=g_edit_chain;bound_slot=g_edit_slot;
    }
}
q15 edit_value(const G&u,int p){return u.params[p].base;}
void set_param(G&u,int p,q15 v){
    if(edit_value(u,p)==v)return;
    u.params[p].base=v;mark_project_mutated();
}
bool frozen(const G&u){
    // Current guarded owner, never another unit's stale slot-only UI snapshot.
    return u.params[G::P_FREEZE].effective>16384;
}
bool owns(const G&u){
    return context(u) && origin_engine==g_eng && origin_unit==uintptr_t(&u) && origin_view==g_view &&
        bound_chain==g_edit_chain && bound_slot==g_edit_slot && origin_bank==g_eng->active_pattern &&
        selected==origin_param && g_drag==origin_drag;
}
bool dormant(const G&u,int p){return frozen(u)&&p!=G::P_FREEZE&&p!=G::P_MIX;}
int root_note(q15 tone){return(int)std::lround(69+12*std::log2(G::root_millihz(tone,0)/440000.0));}
q15 note_tone(int note){
    note=std::max(33,std::min(56,note));const double hz=440*std::pow(2.0,(note-69)/12.0);
    return clamp_uni((int32_t)std::lround((4.0-32000.0/(149*hz))*32767/3.0));
}
int sigil_index(ModSource s){return mod_source_sigil(s);}
const char* route_name(ModSource s){static const char* names[]={"a","b","c","d","e","f","g","h"};const int i=sigil_index(s);return i>=0?names[i]:s==ModSource::EnvFollow?"env":"off";}
void target_value(int p,q15 v,int&x,int&y){switch(p){case G::P_TONE:x=ux(v,TONE_X0,TONE_X1);y=TONE_Y;break;case G::P_DECAY:x=DECAY_X;y=uy(v,DECAY_Y0,DECAY_Y1);break;case G::P_SMEAR:x=ux(v,SMEAR_X0,SMEAR_X1);y=SMEAR_Y;break;case G::P_DRIFT:x=DRIFT_X;y=uy(v,DRIFT_Y0,DRIFT_Y1);break;case G::P_FREEZE:x=FREEZE_X;y=FREEZE_Y;break;case G::P_MIX:x=MIX_X;y=uy(v,MIX_Y0,MIX_Y1);break;case G::P_FINE:x=150+(int32_t)v*60/32767;y=76;break;case G::P_RATE:x=ux(v,64,250);y=220;break;default:x=160;y=117;}}
void begin_drag(G&u,int p,Drag d,int x,int y){
    origin_x=last_x=x;origin_y=last_y=y;origin_value=edit_value(u,p);
    origin_engine=g_eng;origin_unit=uintptr_t(&u);origin_view=g_view;origin_bank=g_eng->active_pattern;
    origin_param=p;origin_drag=g_drag=d;
}
struct Corridor {int param,x,y,w,h;};
constexpr Corridor CORRIDORS[]={
    {G::P_TONE,82,42,158,20}, {G::P_FINE,82,62,131,22},
    {G::P_DECAY,20,86,62,98}, {G::P_DRIFT,242,43,26,70},
    {G::P_FREEZE,130,96,62,46}, {G::P_MIX,270,112,50,89},
    {G::P_SMEAR,92,169,148,34}, {G::P_RATE,54,203,202,25}
};
constexpr Drag DRAGS[8]={Drag::GhostTone,Drag::GhostDecay,Drag::GhostSmear,Drag::GhostDrift,
                        Drag::GhostFreeze,Drag::GhostMix,Drag::GhostFine,Drag::GhostRate};

void memory_traces(const Frame& f,int x,int y,int w,int h) {
    namespace shape=ghost_memory_visual;
    const auto& figure=f.figure;
    for(int well=0;well<shape::WELLS;++well)for(int echo=shape::ECHOES-1;echo>=0;--echo) {
        const auto& points=figure.points[well*shape::ECHOES+echo];
        const u32 ink=figure.quiet||echo==2?m::branch():echo?m::ghost():m::ink();
        for(int j=1;j<=shape::SEGMENTS;++j) {
            const auto& a=points[j-1];const auto& b=points[j];
            const int ax=x+a.x*w/shape::EXTENT,ay=y+a.y*h/shape::EXTENT;
            const int bx=x+b.x*w/shape::EXTENT,by=y+b.y*h/shape::EXTENT;
            const int start=figure.sealed?10:16-well;
            const bool accent=!figure.quiet && !echo && j>=start && j<start+6;
            if(ax!=bx || ay!=by)ui::line(ax,ay,bx,by,accent?m::spot():ink);
        }
        if(!echo && figure.sealed && !figure.quiet) {
            const int sx=x+points[0].x*w/shape::EXTENT,sy=y+points[0].y*h/shape::EXTENT;
            ui::line(sx-2,sy,sx+2,sy,m::spot());
        }
    }
}
void right(int x,int y,const char*s,u32 c){ui::text(x-ui::text_w(s),y,s,c);}
void hero_value(const char* value,bool dormant){
    const u32 ink=dormant?m::ghost():m::ink();
    const auto r=inspector_gothic::draw_numeric(14,130,value,ink,dormant?m::ghost():m::spot());
    if(!r.drawn)ui::text(14,98,value,ink,4);
    else if(*r.suffix)ui::text(r.end_x+3,118,r.suffix,ink,r.end_x+3+ui::text_w(r.suffix)*2<=174?2:1);
}
void underline(int x,int y,const char*s,bool on){if(on)ui::line(x,y,x+ui::text_w(s)-2,y,m::spot(),1);}
void value_text(char*out,size_t n,int p,q15 value,q15 fine){
    if(p==G::P_TONE){const uint32_t hz=G::root_millihz(value,fine);std::snprintf(out,n,"%lu.%02luhz",(unsigned long)(hz/1000),(unsigned long)((hz%1000)/10));}
    else if(p==G::P_FINE){const int c=(int32_t)value*5000/32767;std::snprintf(out,n,"%c%d.%02dc",c<0?'-':'+',std::abs(c)/100,std::abs(c)%100);}
    else if(p==G::P_RATE){const uint32_t ms=G::drift_period_ms(value);std::snprintf(out,n,"%lu.%lus",(unsigned long)(ms/1000),(unsigned long)((ms%1000)/100));}
    else if(p==G::P_FREEZE)std::snprintf(out,n,"%s",value>16384?"on":"off");
    else std::snprintf(out,n,"%d%%",(int)((uint32_t)(uint16_t)value*100/32767));
}
void frame_value_text(char*out,size_t n,const Frame&f,q15 value,q15 fine){value_text(out,n,f.selected,value,fine);}
void draw_parameter_fader(int p,const Frame& f){
    int x,y,lx,ly;target_value(p,f.edit[p],x,y);target_value(p,f.live[p],lx,ly);
    const module_fader::State state{
        f.source[p]!=ModSource::None && bool(f.mod_enabled_mask&(1u<<p)),
        f.held && p!=G::P_MIX && p!=G::P_FREEZE,f.bypass||f.mute};
    switch(p){
        case G::P_TONE:module_fader::horizontal(TONE_X0,TONE_X1,TONE_Y,x,lx,state,-1,true);break;
        case G::P_DECAY:module_fader::vertical(DECAY_X,DECAY_Y0,DECAY_Y1,y,ly,state);break;
        case G::P_SMEAR:module_fader::horizontal(SMEAR_X0,SMEAR_X1,SMEAR_Y,x,lx,state);break;
        case G::P_DRIFT:module_fader::vertical(DRIFT_X,DRIFT_Y0,DRIFT_Y1,y,ly,state);break;
        case G::P_MIX:module_fader::vertical(MIX_X,MIX_Y0,MIX_Y1,y,ly,state);break;
        case G::P_FINE:module_fader::horizontal(90,210,76,x,lx,state,150,true);break;
        case G::P_RATE:module_fader::horizontal(64,250,220,x,lx,state);break;
        default:break;
    }
}
const char* memory_state(const Frame& f) {
    if(f.bypass)return "bypassed";
    if(f.mute)return "chain muted";
    if(f.live[G::P_MIX]==0)return "dry mix";
    if(f.freeze==32767 && f.held)return "memory held";
    if(f.freeze>0)return f.held?"sealing memory":"releasing memory";
    if(f.held)return "freeze entering";
    if(f.edit[G::P_FREEZE]>16384)return "freeze queued";
    return f.energy?"tail active":"memory quiet";
}
void draw_bottom_frame(const Frame&f){
    m::paper(320);char label[64];
    module_heading::header(UnitKind::Ghost,m::spot());std::snprintf(label,sizeof label,"ch%02d / %02d",f.chain+1,f.slot+1);ui::text(72,m::kHeaderTextY,label,m::ghost());
    ui::text(132,m::kHeaderTextY,f.note_steps?"note":"free",m::spot());underline(132,m::kHeaderMarkY,f.note_steps?"note":"free",f.selected==G::P_TONE);
    ui::text(205,m::kHeaderTextY,"byp",f.bypass?m::spot():m::ghost());ui::text(246,m::kHeaderTextY,f.long_tail?"long":"old",m::spot());right(m::right_edge(320),m::kHeaderTextY,"x del",m::ghost());

    memory_traces(f,82,85,158,84);
    for(int p=0;p<6;++p)if(p!=G::P_FREEZE)draw_parameter_fader(p,f);
    static const char* notes[]={"c","c#","d","d#","e","f","f#","g","g#","a","a#","b"};
    const uint32_t hz=G::root_millihz(f.live[0],f.live[6]);
    const int note=(int)std::lround(69+12*std::log2(hz/440000.0));
    if(f.held)std::snprintf(label,sizeof label,"root held");
    else std::snprintf(label,sizeof label,"- %s%d %lu.%02luhz +",notes[(note%12+12)%12],note/12-1,(unsigned long)(hz/1000),(unsigned long)((hz%1000)/10));
    ui::text(82,28,label,f.held?m::ghost():m::ink());ui::text(93,43,"root",f.selected==0?m::spot():m::ghost());
    char value[24];
    value_text(value,sizeof value,G::P_FINE,f.live[6],f.live[6]);
    std::snprintf(label,sizeof label,"fine %s",f.held?"held":value);
    ui::text(87,64,label,f.held?m::ghost():f.selected==6?m::spot():m::ink());
    ui::text(216,67,"zero",m::ghost());draw_parameter_fader(G::P_FINE,f);
    value_text(value,sizeof value,G::P_RATE,f.live[7],f.live[6]);
    std::snprintf(label,sizeof label,"cycle %s",f.held?"held":value);
    ui::text(64,207,label,f.held?m::ghost():f.selected==7?m::spot():m::ink());
    ui::text(190,207,"slow >",m::ghost());draw_parameter_fader(G::P_RATE,f);
    for(int p:{1,2,3,5}){
        value_text(value,sizeof value,p,f.live[p],f.live[6]);
        std::snprintf(label,sizeof label,"%s %s",names[p],f.held&&p!=5?"held":value);
        const int x=p==1?31:p==2?137:p==3?229:267,y=p==1?181:p==2?193:p==3?43:190;
        ui::text(x,y,label,f.held&&p!=5?m::ghost():f.selected==p?m::spot():m::ghost());
    }
    ui::text(142,106,"freeze",f.selected==4||f.held?m::spot():m::ink());
    const char* freeze=f.freeze==32767&&f.held?"held":f.freeze>0?(f.held?"seal":"exit"):f.held?"on":"off";
    ui::text(160-ui::text_w(freeze)/2,122,freeze,f.held?m::spot():m::ghost());
    if(f.freeze==32767 && f.held)ui::line(145,135,175,135,m::spot());
    ui::text(m::kLeft,m::kTextY,"b back",m::ghost());right(m::right_edge(320),m::kTextY,"l: mod >",m::spot());
}
void draw_top_frame(const Frame&f){
    m::paper(400);module_heading::header(UnitKind::Ghost,m::spot());int x=14;
    for(int i=0;i<UNITS_PER_CHAIN;++i){
        const auto kind=UnitKind(f.kinds[i]);if(kind==UnitKind::Empty)continue;
        const char* name=m::module_name(kind);const int w=ui::text_w(name);
        if(x>14){ui::text(x,module_heading::kRouteY,"/",m::ghost());x+=14;}
        ui::text(x,module_heading::kRouteY,name,i==f.slot?m::spot():m::ghost());
        if(i==f.slot)ui::line(x,module_heading::kRouteUnderlineY,x+w-2,module_heading::kRouteUnderlineY,m::spot());
        if(f.bypassed[i])ui::line(x,module_heading::kRouteCrossY,x+w-2,module_heading::kRouteCrossY,m::ghost());
        x+=w+8;
    }
    char label[80],value[28];
    std::snprintf(label,sizeof label,"ch%02d / %02d",f.chain+1,f.slot+1);right(m::right_edge(400),m::kHeaderTextY,label,m::ghost());
    m::heading(14,module_heading::kDetailY,names[f.selected],m::spot());ui::text(14,79,descriptions[f.selected],m::ghost());
    const bool dormant=f.held && f.selected!=G::P_MIX && f.selected!=G::P_FREEZE;
    frame_value_text(value,sizeof value,f,f.edit[f.selected],f.edit[G::P_FINE]);hero_value(value,dormant);
    std::snprintf(label,sizeof label,"%s / %s",dormant?"held":"base",f.long_tail?"long":"old");
    ui::text(14,146,label,m::ghost());
    const auto& p=f.param;
    if(p.source==ModSource::None)std::snprintf(label,sizeof label,"mod off");
    else if(p.source==ModSource::EnvFollow)std::snprintf(label,sizeof label,"env ch%d %+d%%%s",p.env_chain+1,int(int32_t(p.depth)*100/32767),p.mod_enabled?"":" muted");
    else std::snprintf(label,sizeof label,"sigil %s %+d%%%s",route_name(p.source),int(int32_t(p.depth)*100/32767),p.mod_enabled?"":" muted");
    ui::text(14,170,label,p.source!=ModSource::None&&p.mod_enabled?m::spot():m::ghost());
    frame_value_text(value,sizeof value,f,f.live[f.selected],f.live[G::P_FINE]);
    std::snprintf(label,sizeof label,"live %s",dormant?"held":value);ui::text(14,188,label,m::ghost());
    right(386,module_heading::kDetailY,memory_state(f),f.figure.quiet?m::ghost():m::spot());
    memory_traces(f,177,68,203,114);right(386,192,"four memory traces",m::ghost());
    value_text(value,sizeof value,G::P_TONE,f.live[0],f.live[6]);
    std::snprintf(label,sizeof label,"root %s",f.held?"held":value);ui::text(14,210,label,m::ghost());
    value_text(value,sizeof value,G::P_FINE,f.live[6],f.live[6]);
    std::snprintf(label,sizeof label,"fine %s",f.held?"held":value);ui::text(160,210,label,m::ghost());
    value_text(value,sizeof value,G::P_RATE,f.live[7],f.live[6]);
    std::snprintf(label,sizeof label,"cycle %s",f.held?"held":value);right(386,210,label,m::ghost());
    const char* help=f.held?"sealed input / mix remains live":f.selected==G::P_SMEAR?"clear repeats > blurred dark tail":
        f.selected==G::P_FINE?"shared +/-50c / zero restores unity":f.selected==G::P_RATE?"drift cycle 2.5..120s / phase unchanged":
        f.selected==G::P_TONE?"nominal first well / not measured pitch":"four wells / fading memory";
    ui::text(m::kLeft,m::kTextY,help,m::ghost());
}
} // namespace

int ghost_focus_selected(){return selected;}
void ghost_focus_select(int param){if(param>=0 && param<8){if(param!=selected)release();selected=param;}}
void ghost_focus_release(){release();}
void ghost_focus_cancel(){release();frame={};identity=0;bound_engine=nullptr;bound_chain=bound_slot=-1;selected=G::P_TONE;}
void ghost_focus_value_text(char*out,size_t n,int p,q15 value,q15 fine){
    if(p==G::P_TONE&&frame.valid)fine=frame.edit[G::P_FINE];
    value_text(out,n,p,value,fine);
}
void ghost_focus_capture(){
    frame={};
    if(!capture_context()){if(param_lens_is_open())release();else ghost_focus_cancel();return;}
    {
        platform::AudioOut::Guard guard(*g_audio);
        Unit* at=g_eng->chains[g_edit_chain].units[g_edit_slot];
        if(!at||at->kind!=UnitKind::Ghost){ghost_focus_cancel();return;}
        const auto& u=*static_cast<G*>(at);bind(u);
        if(origin_unit && (!owns(u) || dormant(u,origin_param)))release();
        frame.valid=true;frame.chain=g_edit_chain;frame.slot=g_edit_slot;frame.selected=selected;
        frame.bypass=u.bypass;frame.mute=g_eng->chains[g_edit_chain].mute;
        frame.long_tail=u.long_tail;frame.note_steps=note_steps;
        // Capture one guarded unit instant, including a just-replaced unit.
        // A stale slot-only mailbox must not lend it the previous unit's memory.
        for(int p=0;p<8;++p){
            frame.base[p]=u.params[p].base;frame.edit[p]=edit_value(u,p);
            frame.live[p]=u.params[p].effective;frame.source[p]=u.params[p].src;
            if(u.params[p].mod_enabled)frame.mod_enabled_mask|=uint8_t(1u<<p);
        }
        frame.held=frame.live[G::P_FREEZE]>16384;
        frame.energy=uint16_t(std::max(0,int(u.visual_energy())));
        frame.freeze=uint16_t(std::max(0,int(u.visual_freeze())));
        frame.drift=u.visual_drift();
        g_eng->capture_param_inspector(g_edit_chain,g_edit_slot,selected,frame.param);
        for(int i=0;i<UNITS_PER_CHAIN;++i){
            const Unit* q=g_eng->chains[g_edit_chain].units[i];if(!q)continue;
            frame.kinds[i]=uint8_t(q->kind);
            frame.bypassed[i]=q->bypass;
        }
    }
    ghost_memory_visual::project(frame.energy,frame.freeze,frame.drift,
        frame.bypass||frame.mute||frame.live[G::P_MIX]==0,frame.figure);
}

bool ghost_focus_touch_body(G&u,int tx,int ty){
    release();if(!context(u))return false;platform::AudioOut::Guard guard(*g_audio);bind(u);
    if(ty<22&&hit(tx,ty,200,3,40,18)){u.bypass=!u.bypass;mark_project_mutated();return true;}
    if(hit(tx,ty,126,0,61,22)){note_steps=!note_steps;return true;}
    if(hit(tx,ty,243,0,38,22)){g_drag=Drag::None;if(frozen(u))return true;if(u.visual_freeze()>0||u.params[G::P_FREEZE].base>16384||u.params[G::P_FREEZE].effective>16384)return true;u.long_tail=!u.long_tail;mark_project_mutated();return true;}
    if(hit(tx,ty,82,24,150,18)){selected=G::P_TONE;if(!dormant(u,G::P_TONE))set_param(u,G::P_TONE,note_tone(root_note(edit_value(u,0))+(tx<157?-1:1)));return true;}
    if(hit(tx,ty,213,62,27,22)){selected=G::P_FINE;if(!dormant(u,G::P_FINE))set_param(u,G::P_FINE,0);return true;}
    for(const auto& c:CORRIDORS)if(hit(tx,ty,c.x,c.y,c.w,c.h)){
        const int p=c.param;selected=p;if(dormant(u,p))return true;
        if(p==G::P_FREEZE){set_param(u,p,edit_value(u,p)>16384?0:32767);return true;}
        begin_drag(u,p,DRAGS[p],tx,ty);return true;
    }
    return true; // Fixed corridors and their gaps never revive moving legacy targets.
}
void ghost_focus_drag(G&u,int tx,int ty){
    if(!origin_unit)return;
    if(!context(u)){release();return;}
    platform::AudioOut::Guard guard(*g_audio);
    if(!owns(u) || dormant(u,origin_param)){release();return;}
    const bool horizontal=origin_param==G::P_TONE || origin_param==G::P_SMEAR ||
        origin_param==G::P_FINE || origin_param==G::P_RATE;
    if(horizontal?tx==last_x:ty==last_y)return;
    last_x=tx;last_y=ty;
    switch(g_drag){
        case Drag::GhostFine:set_param(u,G::P_FINE,clamp_bi((int32_t)origin_value+(tx-origin_x)*64));break;
        case Drag::GhostRate:set_param(u,G::P_RATE,clamp_uni((int32_t)origin_value+(tx-origin_x)*32767/186));break;
        case Drag::GhostTone:{
            q15 v=clamp_uni((int32_t)origin_value+(tx-origin_x)*32767/(TONE_X1-TONE_X0));
            if(note_steps){const int steps=(tx-origin_x)/6;v=steps?note_tone(root_note(origin_value)+steps):origin_value;}
            set_param(u,G::P_TONE,v);break;
        }
        case Drag::GhostDecay:set_param(u,G::P_DECAY,clamp_uni((int32_t)origin_value+(origin_y-ty)*32767/(DECAY_Y1-DECAY_Y0)));break;
        case Drag::GhostSmear:set_param(u,G::P_SMEAR,clamp_uni((int32_t)origin_value+(tx-origin_x)*32767/(SMEAR_X1-SMEAR_X0)));break;
        case Drag::GhostDrift:set_param(u,G::P_DRIFT,clamp_uni((int32_t)origin_value+(origin_y-ty)*32767/(DRIFT_Y1-DRIFT_Y0)));break;
        case Drag::GhostMix:set_param(u,G::P_MIX,clamp_uni((int32_t)origin_value+(origin_y-ty)*32767/(MIX_Y1-MIX_Y0)));break;
        default:release();break;
    }
}
void ghost_focus_draw(const G&){if(frame.valid)draw_bottom_frame(frame);}
bool ghost_focus_draw_top(){if(!frame.valid)return false;draw_top_frame(frame);return true;}
} // namespace seance::platform::ui3ds
