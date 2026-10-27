#include "ui_module_focus_internal.h"
#include "ui_state.h"
#include "ui_theme.h"
#include "ui_param_lens.h"
#include "module_inspector.h"
#include "editor_style.h"
#include "module_heading.h"
#include "inspector_gothic.h"
#include "audio_out.h"
#include "../../core/ui/draw.h"
#include <algorithm>
#include <cstdio>
#include <type_traits>

namespace seance::platform::ui3ds {
namespace {
namespace m = editor_style;
using B = BirdUnit;
constexpr int TRACE=257, MINI=65;
constexpr const char* NAMES[B::P_COUNT]={"pitch","sweep","length","trill","air","gain","contour","syllable"};
constexpr int ORDER[B::P_COUNT]={B::P_PITCH,B::P_SWEEP,B::P_TIME,B::P_TRILL,B::P_CONTOUR,B::P_SYLLABLE,B::P_AIR,B::P_GAIN};
struct Frame {
    bool valid=false, bypass=false, mute=false, lock=false, full=false, lens=false;
    int chain=0, slot=0, selected=0;
    q15 edit[B::P_COUNT]={}, ready[B::P_COUNT]={};
    ParamInspector routes[B::P_COUNT]={};
    B::Visual visual{};
    uint8_t pitch[TRACE]={}, envelope[TRACE]={}, contour[MINI]={};
    int cursor_x=0, cursor_y=0;
    uint32_t duration=0;
    char values[B::P_COUNT][20]={}, ready_value[20]={};
} frame;
static_assert(sizeof(Frame)<=2048 && std::is_trivially_copyable<Frame>::value,
              "BIRD frozen bounded scalars/model points; no live engine or PCM in draw");
int selected=0;
struct Gesture {
    uintptr_t unit=0;
    Engine* engine=nullptr;
    int chain=-1, slot=-1, step=-1, lock_chain=-1, bank=-1, param=-1, x=0, width=128;
    bool locked=false;
    q15 value=0;
} gesture;
bool bipolar(int p) {return p==B::P_SWEEP || p==B::P_CONTOUR;}
int clamp(int v,int p) {return std::max(bipolar(p)?-32767:0,std::min(32767,v));}
int position(q15 v,int p,int x,int width) {
    const bool bi=bipolar(p);
    return x+(clamp(v,p)+(bi?32767:0))*width/(bi?65534:32767);
}
bool context() {
    return g_eng && g_audio && g_overlay==Overlay::Editor &&
        (g_view==View::Chains || (g_view==View::Sequencer && g_seq_lock_armed)) &&
        g_edit_chain>=0 && g_edit_chain<NUM_CHAINS && g_edit_slot>=0 && g_edit_slot<UNITS_PER_CHAIN;
}
bool lock_context() {
    return g_seq_lock_armed && g_seq_lock_chain==g_edit_chain &&
        g_seq_lock_step>=0 && g_seq_lock_step<SEQ_STEPS;
}
bool owns(const B& u) {
    return gesture.engine==g_eng && gesture.unit==uintptr_t(&u) &&
        gesture.chain==g_edit_chain && gesture.slot==g_edit_slot &&
        g_eng->chains[g_edit_chain].units[g_edit_slot]==&u && gesture.locked==g_seq_lock_armed &&
        (!gesture.locked || (lock_context() && gesture.step==g_seq_lock_step &&
        gesture.lock_chain==g_seq_lock_chain && gesture.bank==g_eng->active_pattern));
}
q15 edit_value(const B& u,int p) {
    return lock_context()?seq_display_value(g_edit_slot,p,u.params[p].base):u.params[p].base;
}
void change(B& u,int p,q15 value) {
    if(value==edit_value(u,p))return;
    if(g_seq_lock_armed) {if(lock_context())seq_record_lock(g_edit_slot,p,value);}
    else {u.params[p].base=value;mark_project_mutated();}
}
void start(B& u,int p,int x,int width) {
    selected=p;gesture.unit=uintptr_t(&u);gesture.engine=g_eng;
    gesture.chain=g_edit_chain;gesture.slot=g_edit_slot;gesture.step=g_seq_lock_step;
    gesture.lock_chain=g_seq_lock_chain;gesture.bank=g_eng->active_pattern;
    gesture.locked=g_seq_lock_armed;gesture.param=p;gesture.x=x;gesture.width=width;
    gesture.value=edit_value(u,p);g_drag=Drag::BirdParam;
}
void right(int x,int y,const char* text,u32 color) {ui::text(x-ui::text_w(text),y,text,color);}
u32 tint(u32 color,int) {
    // True translucent ink over the common screen-space material, not a flat
    // row-color approximation that cuts stripes through the quiet smoke.
    return (color&0xffffffu)|(64u<<24);
}
void marker(int x,int y,u32 color) {
    ui::rect(x-3,y-3,7,1,color);ui::rect(x-3,y+3,7,1,color);
    ui::rect(x-3,y-2,1,5,color);ui::rect(x+3,y-2,1,5,color);
}
// Octave-spaced anchors, linearly interpolated within each octave. Bounded UI
// projection, not a spectrum or an audio analysis; avoids per-point log calls.
int pitch_height(uint32_t mhz,int height) {
    mhz=std::max<uint32_t>(125000,std::min<uint32_t>(10000000,mhz));
    uint32_t anchor=125000;int octave=0;
    while(octave<6 && mhz>=anchor*2){anchor*=2;++octave;}
    const int coordinate=octave*32768+int(uint64_t(mhz-anchor)*32768/anchor);
    return height-int(int64_t(coordinate)*height/204800);
}
void field(int index) {
    const int p=ORDER[index],x=(index&1)?164:14,y=100+(index/2)*28;
    const auto& d=frame.routes[p];const u32 color=frame.selected==p?m::spot():m::ink();
    ui::text(x,y,NAMES[p],color);right(x+138,y,frame.values[p],color);
    if(bipolar(p))ui::text(x+ui::text_w(NAMES[p])+6,y,"zero",m::ghost());
    if(d.source!=ModSource::None) {
        char tag[2]={'e',0};
        if(mod_source_sigil(d.source)>=0)tag[0]='a'+mod_source_sigil(d.source);
        ui::text(x,y+13,tag,d.mod_enabled?m::spot():m::ghost());
    }
    const int rx=x+10,ry=y+15;
    const int ready=position(frame.ready[p],p,rx,128);
    module_fader::horizontal(rx,rx+128,ry,position(frame.edit[p],p,rx,128),ready,
        {d.source!=ModSource::None && d.mod_enabled,false,frame.bypass||frame.mute},
        bipolar(p)?rx+64:-1);
}
void route_text(char* out,size_t n) {
    const auto& d=frame.routes[frame.selected];
    const bool muted=d.source!=ModSource::None && !d.mod_enabled;
    if(frame.full && !muted){std::snprintf(out,n,"lock full / 4 per step");return;}
    if(d.source==ModSource::None) {
        std::snprintf(out,n,"%s",frame.lock?"stored lock / name: remove":frame.lens?"held l / modulation":"a: call / l: modulation");return;
    }
    const char* state=muted?(frame.full?"muted / lock full":"muted"):frame.selected==B::P_GAIN?"live gain":"next call";
    if(d.source==ModSource::EnvFollow)std::snprintf(out,n,"env%d / %+d%% / %s",d.env_chain+1,int(d.depth)*100/32767,state);
    else std::snprintf(out,n,"sigil %c / %+d%% / %s",'a'+mod_source_sigil(d.source),int(d.depth)*100/32767,state);
}
} // namespace
int bird_focus_selected() {return selected;}
void bird_focus_select(int p) {if(p>=0 && p<B::P_COUNT)selected=p;}
void bird_focus_release() {gesture={};}
bool bird_focus_touch_body(B& u,int tx,int ty) {
    if(!context() || param_lens_is_open())return false;
    platform::AudioOut::Guard guard(*g_audio);
    bird_focus_release();
    if(g_eng->chains[g_edit_chain].units[g_edit_slot]!=&u)return false;
    if(g_seq_lock_armed && !lock_context())return true;
    if(ty<25) {
        if(!g_seq_lock_armed && hit(tx,ty,252,0,60,25)){u.bypass=!u.bypass;mark_project_mutated();}
        return true;
    }
    if(hit(tx,ty,8,30,304,59)){start(u,B::P_CONTOUR,tx,284);return true;}
    for(int i=0;i<B::P_COUNT;++i) {
        const int p=ORDER[i],x=(i&1)?164:14,y=100+(i/2)*28;
        if(!hit(tx,ty,x-6,y-4,150,28))continue;
        selected=p;
        if(lock_context() && hit(tx,ty,x-3,y-3,ui::text_w(NAMES[p])+6,12)) {
            if(g_eng->remove_step_lock(g_seq_lock_chain,g_seq_lock_step,g_edit_slot,p))mark_project_mutated();
            return true;
        }
        if(bipolar(p) && hit(tx,ty,x+ui::text_w(NAMES[p])+3,y-3,30,12)){change(u,p,0);return true;}
        start(u,p,tx,128);return true;
    }
    return true;
}
void bird_focus_drag(B& u,int tx,int) {
    if(!context() || param_lens_is_open()){bird_focus_release();return;}
    platform::AudioOut::Guard guard(*g_audio);
    if(gesture.param<0 || g_drag!=Drag::BirdParam || !owns(u)){bird_focus_release();return;}
    const int p=gesture.param,range=bipolar(p)?65534:32767;
    change(u,p,q15(clamp(gesture.value+(tx-gesture.x)*range/gesture.width,p)));
}
void bird_focus_capture() {
    frame={};if(!context()){bird_focus_release();return;}
    {
        platform::AudioOut::Guard guard(*g_audio);
        const auto& chain=g_eng->chains[g_edit_chain];const Unit* unit=chain.units[g_edit_slot];
        if(!unit || unit->kind!=UnitKind::Bird){bird_focus_release();return;}
        const auto& u=*static_cast<const B*>(unit);
        if(gesture.unit && (!owns(u) || param_lens_is_open() || g_drag!=Drag::BirdParam))bird_focus_release();
        frame.valid=true;frame.bypass=u.bypass;frame.mute=chain.mute;frame.lock=lock_context();
        frame.full=frame.lock && g_seq_lock_flash>0;frame.lens=false;
        frame.chain=g_edit_chain;frame.slot=g_edit_slot;frame.selected=selected;frame.visual=u.visual();
        for(int p=0;p<B::P_COUNT;++p) {
            frame.edit[p]=edit_value(u,p);frame.ready[p]=u.params[p].effective;
            g_eng->capture_param_inspector(g_edit_chain,g_edit_slot,p,frame.routes[p]);
        }
    }
    // Pure model work after releasing the guard. No audio render, PCM read or
    // DSP advancement. The top follows latched values until this call ends.
    const q15* values=frame.visual.active?frame.visual.latched:frame.ready;
    const auto shape=B::prepare_shape(values);frame.duration=B::duration_samples(values[B::P_TIME]);
    for(int i=0;i<TRACE;++i) {
        const auto point=B::shape_point(shape,uint32_t(uint64_t(i)*(frame.duration-1)/(TRACE-1)));
        frame.pitch[i]=uint8_t(pitch_height(point.frequency_millihz,60));
        frame.envelope[i]=uint8_t(std::max(0,int(point.envelope_q15))*15/32767);
    }
    if(frame.visual.active) {
        const auto point=B::shape_point(shape,frame.visual.age);
        frame.cursor_x=int(uint64_t(frame.visual.age)*334/std::max<uint32_t>(1,frame.visual.duration-1));
        frame.cursor_y=pitch_height(point.frequency_millihz,60);
    }
    q15 contour[B::P_COUNT];std::copy(frame.edit,frame.edit+B::P_COUNT,contour);
    contour[B::P_TRILL]=contour[B::P_SYLLABLE]=0;
    const auto mini=B::prepare_shape(contour);const uint32_t duration=B::duration_samples(contour[B::P_TIME]);
    for(int i=0;i<MINI;++i)frame.contour[i]=uint8_t(pitch_height(B::shape_point(mini,uint32_t(uint64_t(i)*(duration-1)/(MINI-1))).frequency_millihz,33));
    for(int p=0;p<B::P_COUNT;++p)module_inspector_value_text(frame.values[p],sizeof frame.values[p],UnitKind::Bird,p,frame.edit[p]);
    module_inspector_value_text(frame.ready_value,sizeof frame.ready_value,UnitKind::Bird,frame.selected,frame.ready[frame.selected]);
}
void bird_focus_draw(const B&) {
    m::paper(320);if(!frame.valid)return;
    module_heading::header(UnitKind::Bird,m::spot());
    char text[48];std::snprintf(text,sizeof text,"ch%02d / %02d",frame.chain+1,frame.slot+1);
    ui::text(62,m::kHeaderTextY,text,m::ghost());ui::text(276,m::kHeaderTextY,"byp",frame.bypass?m::spot():m::ghost());
    if(frame.bypass)ui::line(276,m::kHeaderMarkY,293,m::kHeaderMarkY,m::spot());
    ui::text(18,33,"contour / drag",frame.selected==B::P_CONTOUR?m::spot():m::ink());
    right(302,33,frame.lock?"stored lock":"stored shape",m::ghost());
    ui::line(18,84,302,84,m::branch());
    for(int i=1;i<MINI;++i)ui::line(18+(i-1)*284/(MINI-1),48+frame.contour[i-1],18+i*284/(MINI-1),48+frame.contour[i],m::spot());
    for(int i=0;i<MINI;i+=4)ui::line(18+i*284/(MINI-1),50+frame.contour[i],18+i*284/(MINI-1),83,tint(m::spot(),70));
    for(int i=0;i<B::P_COUNT;++i)field(i);
    ui::text(14,214,frame.full?"lock full / existing locks editable":"handle: stored / bead: next / gain live",m::ghost());
    ui::line(14,223,306,223,m::branch());
    if(frame.lock)m::footer(320,"p-lock / name: remove","b back");else m::footer(320,"b back","l: mod >");
}
bool bird_focus_draw_top() {
    if(!frame.valid)return false;
    m::paper(400);module_heading::header(UnitKind::Bird,m::spot());
    ui::text(140,m::kHeaderTextY,frame.visual.active?"call / latched":"next call",m::ghost());
    char text[64];std::snprintf(text,sizeof text,"ch%02d / %02d",frame.chain+1,frame.slot+1);right(m::right_edge(400),m::kHeaderTextY,text,m::ghost());
    const char* title=frame.selected==B::P_CONTOUR?"contour / arc < return > hook":
        frame.selected==B::P_SYLLABLE?"syllable / pitch per trill":NAMES[frame.selected];
    ui::text(16,46,title,m::spot());
    right(384,46,frame.lock?"edit lock":"stored",m::ghost());
    const auto hero=inspector_gothic::draw_numeric(16,94,frame.values[frame.selected],m::ink(),m::spot());
    if(hero.drawn)ui::text(hero.end_x+3,82,hero.suffix,m::spot());
    std::snprintf(text,sizeof text,"%s %s",frame.selected==B::P_GAIN?"live":"next",frame.ready_value);right(384,84,text,m::ghost());
    ui::line(16,107,383,107,m::branch());
    ui::text(16,114,"pitch / hz",m::ghost());right(384,114,"shape / not pcm",m::ghost());
    const int grid_y[]={126,157,186};const char* grid_text[]={"10k","1k","125"};
    for(int i=0;i<3;++i){right(39,grid_y[i]-3,grid_text[i],m::ghost());ui::line(48,grid_y[i],382,grid_y[i],m::branch());}
    for(int i=1;i<TRACE;++i)ui::line(48+(i-1)*334/(TRACE-1),126+frame.pitch[i-1],48+i*334/(TRACE-1),126+frame.pitch[i],m::spot());
    ui::text(16,195,"env",m::ghost());
    for(int i=0;i<TRACE;++i)if(frame.envelope[i]) {
        const int x=48+i*334/(TRACE-1);ui::line(x,209-frame.envelope[i],x,209,tint(m::ink(),202));
    }
    if(frame.visual.active) {
        const int x=48+frame.cursor_x,y=126+frame.cursor_y;const u32 color=frame.bypass||frame.mute?m::ghost():m::ink();
        ui::line(x,125,x,210,tint(color,160));marker(x,y,color);
    }
    ui::text(48,214,"0",m::ghost());std::snprintf(text,sizeof text,"%ums",unsigned((frame.duration+16)/32));right(383,214,text,m::ghost());
    ui::line(16,225,383,225,m::branch());route_text(text,sizeof text);ui::text(m::kLeft,m::kTextY,text,m::ghost());
    right(m::right_edge(400),m::kTextY,frame.bypass?"paused / bypass":frame.mute?"chain muted":frame.visual.active?"singing":"ready",m::spot());
    return true;
}
} // namespace seance::platform::ui3ds
