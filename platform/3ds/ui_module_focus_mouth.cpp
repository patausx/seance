#include "ui_module_focus_internal.h"
#include "ui_state.h"
#include "ui_theme.h"
#include "ui_param_lens.h"
#include "module_inspector.h"
#include "editor_style.h"
#include "module_heading.h"
#include "audio_out.h"
#include "../../core/ui/draw.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <type_traits>

namespace seance::platform::ui3ds {
namespace {
namespace m=editor_style;
using C=MouthUnit;
constexpr const char* NAMES[C::P_COUNT]={"vowel","size","resonance","mix","out"};
constexpr const char* VOWELS[5]={"mouth-a","mouth-e","mouth-i","mouth-o","mouth-u"};
constexpr int PLOT_N=113, PLOT_X=42, PLOT_W=336, PLOT_Y=100, PLOT_H=69;
struct Frame {
    bool valid=false,bypass=false,mute=false,lock=false,full=false,lens=false;
    int chain=0,slot=0,selected=0;
    q15 edit[C::P_COUNT]={};
    ParamInspector routes[C::P_COUNT]={};
    C::Model model{};
    char values[C::P_COUNT][24]={},live[C::P_COUNT][24]={};
    char status[64]={},range[64]={},frequencies[3][20]={};
    uint8_t curve[PLOT_N]={};
    int16_t centers[3]={};
} frame;
static_assert(sizeof(Frame)<2048 && std::is_trivially_copyable<Frame>::value,
              "Mouth draws bounded frozen scalars, never PCM or live engine pointers");
int selected=0;
struct Gesture {
    Engine* engine=nullptr;
    uintptr_t unit=0;
    int chain=-1,slot=-1,step=-1,lock_chain=-1,bank=-1,param=-1,x=0;
    View view=View::Chains;
    q15 value=0;
    bool lock=false;
} gesture;
int uni(int v){return std::max(0,std::min(32767,v));}
bool context(){
    return g_eng && g_audio && g_overlay==Overlay::Editor &&
        (g_view==View::Chains || (g_view==View::Sequencer && g_seq_lock_armed)) &&
        g_edit_chain>=0 && g_edit_chain<NUM_CHAINS && g_edit_slot>=0 && g_edit_slot<UNITS_PER_CHAIN;
}
bool lock_context(){return g_seq_lock_armed && g_seq_lock_chain==g_edit_chain && g_seq_lock_step>=0 && g_seq_lock_step<SEQ_STEPS;}
q15 edit_value(const C& u,int p){return lock_context()?seq_display_value(g_edit_slot,p,u.params[p].base):u.params[p].base;}
bool owns(const C& u){
    return gesture.engine==g_eng && gesture.unit==uintptr_t(&u) && gesture.view==g_view &&
        gesture.chain==g_edit_chain && gesture.slot==g_edit_slot &&
        g_eng->chains[g_edit_chain].units[g_edit_slot]==&u && gesture.lock==g_seq_lock_armed &&
        gesture.bank==g_eng->active_pattern && (!gesture.lock || (lock_context() &&
        gesture.step==g_seq_lock_step && gesture.lock_chain==g_seq_lock_chain));
}
void change(C& u,int p,q15 value){
    if(value==edit_value(u,p))return;
    if(g_seq_lock_armed){if(lock_context())seq_record_lock(g_edit_slot,p,value);}
    else{u.params[p].base=value;mark_project_mutated();}
}
void right(int x,int y,const char* text,u32 color){ui::text(x-ui::text_w(text),y,text,color);}
int px(int v,int x,int extent){return x+uni(v)*extent/32767;}
void rail(int p,int x,int y,int extent){
    const auto& d=frame.routes[p];
    const int base=px(frame.edit[p],x,extent),live=px(d.effective,x,extent);
    module_fader::horizontal(x,x+extent,y,base,live,
        {d.source!=ModSource::None && d.mod_enabled,false,frame.bypass||frame.mute});
}
void source_tag(int p,int x,int y){
    const auto& d=frame.routes[p];if(d.source==ModSource::None)return;
    char text[8];const int sigil=mod_source_sigil(d.source);
    if(sigil>=0)std::snprintf(text,sizeof text,"%c",'a'+sigil);
    else std::snprintf(text,sizeof text,"env%d",d.env_chain+1);
    right(x,y,text,d.mod_enabled?m::spot():m::ghost());
}
void describe(){
    const auto& d=frame.routes[frame.selected];
    static const char* layers[]={"base","step lock","audition","perform","temp"};
    const char* layer=layers[std::max(0,std::min(4,int(d.layer)))];
    if(d.source==ModSource::None)std::snprintf(frame.status,sizeof frame.status,"%s / mod off%s",layer,frame.lens?" / modulation":"");
    else if(mod_source_sigil(d.source)>=0)std::snprintf(frame.status,sizeof frame.status,"%s / sigil %c / %s %+d%%",layer,'a'+mod_source_sigil(d.source),d.mod_enabled?"depth":"muted",int(d.depth)*100/32767);
    else std::snprintf(frame.status,sizeof frame.status,"%s / env%d / %s %+d%%",layer,d.env_chain+1,d.mod_enabled?"depth":"muted",int(d.depth)*100/32767);
    char lo[24],hi[24];mouth_focus_value_text(lo,sizeof lo,frame.selected,d.range_min);mouth_focus_value_text(hi,sizeof hi,frame.selected,d.range_max);
    std::snprintf(frame.range,sizeof frame.range,"range %s .. %s",lo,hi);
}
int fy(float db){return PLOT_Y+int(std::lround((24.f-std::max(-36.f,std::min(24.f,db)))*PLOT_H/60.f));}
void plot(){
    static bool valid=false;
    static C::Model previous{};
    static uint8_t cached[PLOT_N]={};
    // Frequency table and model cache are UI-only, outside audio guard and draw.
    static float frequencies[PLOT_N]={};
    if(!valid)for(int i=0;i<PLOT_N;++i)frequencies[i]=100.f*std::pow(80.f,float(i)/(PLOT_N-1));
    if(!valid || std::memcmp(&previous,&frame.model,sizeof previous)!=0){
        for(int i=0;i<PLOT_N;++i)cached[i]=uint8_t(fy(C::response_db(frame.model,frequencies[i])));
        previous=frame.model;valid=true;
    }
    std::copy(cached,cached+PLOT_N,frame.curve);
    for(int i=0;i<3;++i){
        frame.centers[i]=int16_t(PLOT_X+std::lround(PLOT_W*std::log(std::max(100.f,std::min(8000.f,frame.model.hz[i]))/100.f)/std::log(80.f)));
        std::snprintf(frame.frequencies[i],sizeof frame.frequencies[i],"f%d %.0fhz",i+1,double(frame.model.hz[i]));
    }
}
} // namespace

int mouth_focus_selected(){return selected;}
void mouth_focus_select(int p){if(p>=0 && p<C::P_COUNT)selected=p;}
void mouth_focus_release(){gesture={};}
void mouth_focus_value_text(char* out,size_t n,int p,q15 raw){
    if(!out || !n)return;
    const q15 v=q15(uni(raw));
    switch(p){
        case C::P_VOWEL:{
            constexpr char anchors[]="aeiou";
            const int scaled=int(v)*4,segment=std::min(3,scaled/32767);
            const int percent=((scaled-segment*32767)*100+16383)/32767;
            if(percent==0)std::snprintf(out,n,"%c",anchors[segment]);
            else if(percent==100)std::snprintf(out,n,"%c",anchors[segment+1]);
            else std::snprintf(out,n,"%c>%c %d%%",anchors[segment],anchors[segment+1],percent);
            break;
        }
        case C::P_SIZE:std::snprintf(out,n,"%.2fx",double(C::size_ratio(v)));break;
        case C::P_OUT:{const float db=C::output_db(v);std::snprintf(out,n,"%+.1fdb",double(std::fabs(db)<.05f?0:db));break;}
        case C::P_RESONANCE:case C::P_MIX:std::snprintf(out,n,"%d%%",(int(v)*100+16383)/32767);break;
        default:std::snprintf(out,n,"--");break;
    }
}
bool mouth_focus_touch_body(C& u,int tx,int ty){
    if(!context() || param_lens_is_open())return false;
    platform::AudioOut::Guard guard(*g_audio);
    mouth_focus_release();if(g_eng->chains[g_edit_chain].units[g_edit_slot]!=&u)return false;
    if(g_seq_lock_armed && !lock_context())return true;
    if(hit(tx,ty,252,205,60,18)){
        if(!g_seq_lock_armed){u.bypass=!u.bypass;mark_project_mutated();}return true;
    }
    if(hit(tx,ty,8,224,96,16)){g_overlay=Overlay::None;g_seq_lock_armed=false;g_drag=Drag::None;return true;}
    int p=-1,label_x=84,label_y=10;
    if(hit(tx,ty,8,5,304,128))p=C::P_VOWEL;
    for(int i=0;i<4;++i)if(hit(tx,ty,8+76*i,148,72,56)){p=i+1;label_x=14+76*i;label_y=155;break;}
    if(p<0)return true;
    selected=p;
    const int label_w=p==C::P_VOWEL?m::heading_width("vowel"):ui::text_w(NAMES[p]);
    if(lock_context() && hit(tx,ty,label_x-3,label_y-3,label_w+6,p==C::P_VOWEL?24:14)){
        if(g_eng->remove_step_lock(g_seq_lock_chain,g_seq_lock_step,g_edit_slot,p))mark_project_mutated();
        return true;
    }
    gesture.engine=g_eng;gesture.unit=uintptr_t(&u);gesture.chain=g_edit_chain;gesture.slot=g_edit_slot;
    gesture.step=g_seq_lock_step;gesture.lock_chain=g_seq_lock_chain;gesture.bank=g_eng->active_pattern;
    gesture.param=p;gesture.x=tx;gesture.value=edit_value(u,p);gesture.lock=g_seq_lock_armed;gesture.view=g_view;
    g_drag=Drag::MouthParam;return true;
}
void mouth_focus_drag(C& u,int tx,int){
    if(!context() || param_lens_is_open()){mouth_focus_release();return;}
    platform::AudioOut::Guard guard(*g_audio);
    if(gesture.param<0 || g_drag!=Drag::MouthParam || !owns(u)){mouth_focus_release();return;}
    const int p=gesture.param,extent=p==C::P_VOWEL?256:62;
    change(u,p,q15(uni(gesture.value+(tx-gesture.x)*32767/extent)));
}
void mouth_focus_capture(){
    frame={};if(!context()){mouth_focus_release();return;}
    {
        platform::AudioOut::Guard guard(*g_audio);
        const auto& chain=g_eng->chains[g_edit_chain];const Unit* unit=chain.units[g_edit_slot];
        if(!unit || unit->kind!=UnitKind::Mouth){mouth_focus_release();return;}
        const auto& u=*static_cast<const C*>(unit);
        if(gesture.unit && (!owns(u) || param_lens_is_open() || g_drag!=Drag::MouthParam))mouth_focus_release();
        frame.valid=true;frame.bypass=u.bypass;frame.mute=chain.mute;frame.lock=lock_context();frame.full=frame.lock && g_seq_lock_flash>0;
        frame.lens=false;frame.chain=g_edit_chain;frame.slot=g_edit_slot;frame.selected=selected;
        frame.model=u.visual_model();
        for(int p=0;p<C::P_COUNT;++p){frame.edit[p]=edit_value(u,p);g_eng->capture_param_inspector(g_edit_chain,g_edit_slot,p,frame.routes[p]);}
    }
    if(frame.lock){frame.model=C::model_from_values(frame.edit);if(frame.bypass)frame.model.mix=0;}
    for(int p=0;p<C::P_COUNT;++p){
        mouth_focus_value_text(frame.values[p],sizeof frame.values[p],p,frame.edit[p]);
        mouth_focus_value_text(frame.live[p],sizeof frame.live[p],p,frame.routes[p].effective);
    }
    describe();plot();
}
void mouth_focus_draw(const C&){
    m::paper(320);if(!frame.valid)return;
    module_heading::header(UnitKind::Mouth,m::spot());
    m::heading(84,10,"vowel",frame.selected==C::P_VOWEL?m::spot():m::ink());
    right(m::right_edge(320),m::kHeaderTextY,frame.values[C::P_VOWEL],m::spot());
    const int segment=std::min(3,int(frame.edit[C::P_VOWEL])*4/32767);
    for(int i=0;i<5;++i)m::heading(32+i*64-m::heading_width(VOWELS[i])/2,56,VOWELS[i],i==segment||i==segment+1?m::spot():m::ghost());
    rail(C::P_VOWEL,32,103,256);
    ui::text(16,121,"continuous morph",m::ghost());source_tag(C::P_VOWEL,304,121);
    ui::line(16,141,304,141,m::branch());
    for(int p=1;p<C::P_COUNT;++p){
        const int x=14+(p-1)*76;
        ui::text(x,155,NAMES[p],frame.selected==p?m::spot():m::ghost());
        ui::text(x,173,frame.values[p],m::ink());rail(p,x,194,62);
    }
    ui::text(16,211,frame.selected==C::P_OUT?"out = wet trim":frame.selected==C::P_SIZE?"size: larger = lower":"outline edit / fill live",m::ghost());
    // Bypass has an independent hit region, never a hidden route/footer target.
    right(304,211,"byp",frame.bypass?m::spot():m::ghost());
    if(frame.bypass)ui::line(286,221,303,221,m::spot());
    ui::line(16,223,304,223,m::branch());
    if(frame.lock)m::footer(320,frame.full?"lock full":"p-lock / name: remove","b back");
    else m::footer(320,"b back","l: mod >");
}
bool mouth_focus_draw_top(){
    if(!frame.valid)return false;
    m::paper(400);module_heading::header(UnitKind::Mouth,m::spot());
    char text[64];std::snprintf(text,sizeof text,"ch%02d / %02d",frame.chain+1,frame.slot+1);right(m::right_edge(400),m::kHeaderTextY,text,m::ghost());
    ui::text(16,46,NAMES[frame.selected],m::spot());
    std::snprintf(text,sizeof text,"%s %s / live %s",frame.lock?"lock":"base",frame.values[frame.selected],frame.live[frame.selected]);right(384,46,text,m::ink());
    ui::text(16,63,frame.status,m::ghost());
    ui::text(16,83,frame.lock?"lock draft response / db":"applied response / db",m::ghost());
    for(int db:{24,0,-24,-36}){
        const int y=fy(float(db));std::snprintf(text,sizeof text,"%d",db);right(34,y-3,text,m::ghost());ui::line(PLOT_X,y,PLOT_X+PLOT_W,y,m::branch());
    }
    const u32 color=frame.mute?m::ghost():m::spot();
    for(int i=1;i<PLOT_N;++i)ui::line(PLOT_X+(i-1)*3,frame.curve[i-1],PLOT_X+i*3,frame.curve[i],color);
    // Actual smoothed nominal band centres, not claimed peaks of mixed response.
    for(int i=0;i<3;++i){const int x=frame.centers[i];for(int y=PLOT_Y+3;y<PLOT_Y+PLOT_H;y+=7)ui::rect(x,y,1,1,m::ghost());}
    ui::text(42,180,"100",m::ghost());ui::text(217,180,"1k",m::ghost());right(378,180,"8k hz",m::ghost());
    for(int i=0;i<3;++i)ui::text(16+i*128,196,frame.frequencies[i],m::ink());
    ui::text(16,212,frame.range,m::ghost());
    ui::line(16,225,384,225,m::branch());ui::text(m::kLeft,m::kTextY,"model / not audio",m::ghost());
    right(m::right_edge(400),m::kTextY,frame.full?"lock full":frame.bypass?(frame.model.mix>0?"bypass / ramp":"bypass / dry"):frame.mute?"chain muted":frame.selected==C::P_OUT?"out = wet trim":frame.selected==C::P_SIZE?"larger = lower":"pitch unchanged",m::spot());
    return true;
}
} // namespace seance::platform::ui3ds
