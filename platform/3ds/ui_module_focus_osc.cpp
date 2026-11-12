#include "ui_module_focus_internal.h"
#include "ui_state.h"
#include "ui_theme.h"
#include "ui_param_lens.h"
#include "module_inspector.h"
#include "osc_readout.h"
#include "editor_style.h"
#include "module_heading.h"
#include "small_button_motion.h"
#include "audio_out.h"
#include "../../core/ui/draw.h"
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <type_traits>

namespace seance::platform::ui3ds {
namespace {
namespace m=editor_style;
using C=OscUnit;
constexpr int GRAPH_X=30,GRAPH_W=272,COLS=273,ZERO=92,AMP=28;
constexpr int SHAPE_X=14,SHAPE_W=278,SHAPE_Y=148;
constexpr int PARAMS[6]={C::P_PITCH,C::P_PW,C::P_FOLD,C::P_SUB,C::P_DRIFT,C::P_GAIN};
constexpr const char* NAMES[6]={"pitch","width","fold","sub","drift","gain"};
struct Frame {
    MusicalScale scale{};
    uint16_t mod_links=0;
    bool valid=false,bypass=false,mute=false,morph=false,qnt=false,clean=false,fine=false,lens=false;
    int8_t chain=0,slot=0,selected=C::P_SHAPE;
    q15 edit[C::P_COUNT]={},live[C::P_COUNT]={};
    uint8_t pre_low[COLS]={},pre_high[COLS]={},out_low[COLS]={},out_high[COLS]={};
    char values[C::P_COUNT][24]={}, live_values[C::P_COUNT][24]={};
    ParamInspector routes[C::P_COUNT]={};
    C::Visual visual={};
} frame;
static_assert(sizeof(Frame)<=3072 && std::is_trivially_copyable<Frame>::value,
              "bounded pointer-free OSC model frame; no engine or PCM in draw");
int selected=C::P_SHAPE,drag_param=-1,origin_x=0,origin_chain=-1,origin_slot=-1,origin_bank=-1;
bool origin_fine=false,fine=false; View origin_view=View::Chains;
uintptr_t origin_unit=0,focus_unit=0; int focus_chain=-1,focus_slot=-1;
Engine* origin_engine=nullptr; Engine* focus_engine=nullptr; q15 origin_value=0;
constexpr int CLEAN_W=130;
int clean_x(int index){return 14+(index%2)*162;}
int clean_y(int index){return 100+(index/2)*41;}
const char* param_name(int p){return p==C::P_SHAPE?"shape":NAMES[std::find(PARAMS,PARAMS+6,p)-PARAMS];}

int clamp(int value){return std::max(0,std::min(32767,value));}
bool context(){return g_eng&&g_audio&&g_overlay==Overlay::Editor&&
    g_view==View::Chains&&
    g_edit_chain>=0&&g_edit_chain<NUM_CHAINS&&g_edit_slot>=0&&g_edit_slot<UNITS_PER_CHAIN;}
bool owns(const C&u){return origin_engine==g_eng&&origin_unit==(uintptr_t)&u&&origin_chain==g_edit_chain&&origin_slot==g_edit_slot&&
    origin_view==g_view&&g_eng->chains[g_edit_chain].units[g_edit_slot]==&u;}
q15 edit_value(const C&u,int p){return u.params[p].base;}
void rail(int p,int x,int y,int width){
    const int base=x+clamp(frame.edit[p])*width/32767;
    const int live=x+clamp(frame.live[p])*width/32767;
    const bool linked=(p!=C::P_SHAPE || frame.morph) && (frame.mod_links&(1u<<p));
    module_fader::horizontal(x,x+width,y,base,live,{linked,false,frame.bypass||frame.mute},
        -1,!frame.clean && p==C::P_SHAPE);
}
int raw(C::Wave wave,uint32_t phase,uint32_t pw){
    if(wave==C::Wave::Saw)return int32_t(phase>>16)-32768;
    if(wave==C::Wave::Pulse)return phase<pw?-32767:32767;
    const uint32_t p=phase>>16;const int32_t tri=p<0x8000u?int32_t(p)*2-32768:32767-(int32_t(p)-32768)*2;
    if(wave==C::Wave::Tri)return tri;
    const int32_t a=tri<0?-tri:tri;const int32_t value=int32_t((int64_t(tri)*(65536-a))>>15);
    return int(std::max<int32_t>(-32768, std::min<int32_t>(32767, value)));
}
int morph(q15 shape,uint32_t phase,uint32_t pw){
    const int v=clamp(shape);C::Wave a=C::Wave::Sine,b=C::Wave::Tri;int start=0,span=10922;
    if(v>21845){a=C::Wave::Saw;b=C::Wave::Pulse;start=21845;span=10922;}
    else if(v>10922){a=C::Wave::Tri;b=C::Wave::Saw;start=10922;span=10923;}
    const int x=raw(a,phase,pw),y=raw(b,phase,pw);return x+int(int64_t(y-x)*(v-start)/span);
}
int final_sample(const q15*value,uint32_t phase,uint32_t sub_phase){
    const uint32_t pw=0x40000000u+(uint32_t(uint16_t(value[C::P_PW]))<<15);
    int shaped=morph(value[C::P_SHAPE],phase,pw);
    shaped=int((int64_t(shaped)*(32767+int(uint16_t(value[C::P_FOLD]))*6))>>15);
    for(int pass=0;pass<4&&(shaped>32767||shaped<-32768);++pass){if(shaped>32767)shaped=65534-shaped;if(shaped<-32768)shaped=-65536-shaped;}
    shaped=std::max(-32768,std::min(32767,shaped));const int sub=(sub_phase&0x80000000u)?32767:-32767;
    shaped=std::max(-32768,std::min(32767,shaped+((sub-shaped)*int(value[C::P_SUB])>>15)));
    return int((int64_t(shaped)*value[C::P_GAIN])>>15);
}
int sy(int sample){return ZERO-sample*AMP/32768;}
void model(){
    const q15*value=frame.live;constexpr int probes=8,total=COLS*probes;
    for(int col=0;col<COLS;++col){int pre_lo=32767,pre_hi=-32768,out_lo=32767,out_hi=-32768;
        for(int j=0;j<probes;++j){const uint64_t at=uint64_t(col*probes+j);const uint32_t phase=uint32_t(at*(uint64_t(1)<<33)/total);
            const uint32_t sub_phase=uint32_t(at*(uint64_t(1)<<32)/total);const uint32_t pw=0x40000000u+(uint32_t(uint16_t(value[C::P_PW]))<<15);
            const int pre=morph(value[C::P_SHAPE],phase,pw),out=final_sample(value,phase,sub_phase);
            pre_lo=std::min(pre_lo,pre);pre_hi=std::max(pre_hi,pre);out_lo=std::min(out_lo,out);out_hi=std::max(out_hi,out);}
        frame.pre_low[col]=uint8_t(sy(pre_hi));frame.pre_high[col]=uint8_t(sy(pre_lo));
        frame.out_low[col]=uint8_t(sy(out_hi));frame.out_high[col]=uint8_t(sy(out_lo));
    }
}
int control_x(int index){return 14+(index%3)*104;}int control_y(int index){return 166+(index/3)*31;}
void graph(){
    ui::text(74,33,"2 cycles / model",m::ghost());
    ui::line(199,37,209,37,m::ghost(),2);ui::text(214,33,"raw",m::ghost());
    ui::line(253,37,263,37,m::spot(),2);ui::text(268,33,"out",m::spot());
    for(int level=-1;level<=1;++level){
        const int y=ZERO-level*AMP;
        ui::text(10,y-4,level>0?"+1":level<0?"-1":"0",m::ghost());
        ui::line(GRAPH_X,y,GRAPH_X+GRAPH_W,y,level?m::ghost():m::branch());
    }
    // Draw all raw columns before out, so a thick neighbour cannot erase out.
    // A flat bin still needs a nonzero segment on Citro2D; model extrema stay exact.
    for(bool out:{false,true})for(int col=0;col<COLS;++col){
        const int x=GRAPH_X+col,lo=out?frame.out_low[col]:frame.pre_low[col];
        const int hi=out?frame.out_high[col]:frame.pre_high[col];
        ui::line(x,lo,x,std::max(lo+1,hi),out?m::spot():m::ghost(),2);
    }
}
void right(int x,int y,const char* text,u32 color){ui::text(x-ui::text_w(text),y,text,color);}
void source_tag(int p,int x,int y){
    const auto source=frame.routes[p].source;if(source==ModSource::None)return;
    char text[2]={'e',0};if(mod_source_sigil(source)>=0)text[0]='a'+mod_source_sigil(source);
    ui::text(x,y,text,frame.routes[p].mod_enabled?m::spot():m::ghost());
}
void clean_lower(){
    module_heading::header(UnitKind::Osc,m::spot());ui::text(92,38,"clean",m::spot());
    const char* fine_label=frame.fine?"fine /32":"wide";
    small_press::text(244,28,68,24,306-ui::text_w(fine_label),38,fine_label,frame.fine?m::spot():m::ghost());
    ui::text(14,56,"shape",frame.selected==C::P_SHAPE?m::spot():m::ink());source_tag(C::P_SHAPE,52,56);
    right(306,56,frame.values[C::P_SHAPE],m::ink());
    for(int i=0;i<4;++i){const int x=14+i*SHAPE_W/3;
        ui::text(x-(i?i==3?30:9:0),84,i==0?"sine":i==1?"tri":i==2?"saw":"pulse",m::ghost());}
    rail(C::P_SHAPE,14,75,SHAPE_W);
    for(int i=0;i<6;++i){const int p=PARAMS[i],x=clean_x(i),y=clean_y(i);char compact[24];
        std::snprintf(compact,sizeof compact,"%s",frame.values[p]);
        if(char* end=std::strstr(compact," / "))*end=0;
        // Keep the main instrument spare: note identity here, exact cents+Hz above.
        if(p==C::P_PITCH)for(char* at=compact;*at;++at)if(*at=='+'||*at=='-'){*at=0;break;}
        ui::text(x,y,NAMES[i],frame.selected==p?m::spot():m::ink());source_tag(p,x+40,y);
        right(x+CLEAN_W,y,compact,m::ghost());
        rail(p,x,y+19,CLEAN_W);
    }
    ui::line(14,224,306,224,m::branch());
    m::footer(320,"outline stored / fill live","l: mod >");
}
void split_value(const char* text,int y,u32 color){
    char first[24];std::snprintf(first,sizeof first,"%s",text);
    const char* suffix=nullptr;if(char* split=std::strstr(first," / ")){*split=0;suffix=split+3;}
    ui::text(270,y,first,color);if(suffix)ui::text(270,y+12,suffix,m::ghost());
}
int audio_y(int sample){return 110-std::max(-32768,std::min(32767,sample))*42/32768;}
void clean_scope(){
    const auto& v=frame.visual;
    ui::text(24,45,"raw",m::ghost());ui::line(50,49,62,49,m::ghost(),2);
    ui::text(77,45,"out",m::spot());ui::line(103,49,115,49,m::spot(),2);
    right(252,45,"2 cycles",m::ghost());
    for(int level=-1;level<=1;++level){const int y=110-level*42;
        ui::text(6,y-4,level>0?"+1":level<0?"-1":"0",m::ghost());ui::line(24,y,252,y,level?m::ghost():m::branch());}
    if(v.valid){
        for(bool out:{false,true})for(int col=0;col<C::Visual::COLUMNS;++col){if(!v.occupied[col])continue;
            const int x=24+col*226/(C::Visual::COLUMNS-1);
            const int lo=audio_y(out?v.out_max[col]:v.pre_max[col]),hi=audio_y(out?v.out_min[col]:v.pre_min[col]);
            ui::rect(x,lo,2,std::max(1,hi-lo+1),out?m::spot():m::ghost());
        }
    }else ui::text(66,126,frame.bypass?"bypassed":"waiting for audio",m::ghost());
    int count=0;for(auto at:v.occupied)count+=at!=0;
    char text[48];std::snprintf(text,sizeof text,"%u samples / %d of 96 bins",unsigned(v.samples_per_cycle),count);
    ui::text(24,162,v.valid?text:"audio-time / no model",m::ghost());
    bool clipped=false;for(int i=0;i<v.pitch_count;++i){const int n=(v.pitch_head+C::Visual::HISTORY-v.pitch_count+i)%C::Visual::HISTORY;
        clipped|=std::abs(int(v.pitch_cents[n]))>100;}
    ui::text(24,181,clipped?"pitch +/-100ct / clipped":"pitch offset / +/-100ct",m::ghost());
    ui::line(24,202,252,202,m::branch());
    for(int i=0;i<v.pitch_count;++i){const int n=(v.pitch_head+C::Visual::HISTORY-v.pitch_count+i)%C::Visual::HISTORY;
        const int value=v.pitch_cents[n],x=24+(C::Visual::HISTORY-v.pitch_count+i)*226/(C::Visual::HISTORY-1),y=202-std::max(-100,std::min(100,value))*14/100;
        ui::rect(x,y,2,2,m::spot());if(std::abs(value)>100)ui::rect(x-1,y-1,4,1,m::spot());}
}
} // namespace

int osc_focus_selected(){return selected;}
void osc_focus_select(int param){if(param>=0&&param<C::P_COUNT){if(param!=selected)osc_focus_release();selected=param;}}
void osc_focus_release(){drag_param=-1;origin_unit=0;if(g_drag==Drag::OscShape)g_drag=Drag::None;}

bool osc_focus_touch_body(C&u,int tx,int ty){
    if(!context()||param_lens_is_open()){osc_focus_release();return false;}osc_focus_release();platform::AudioOut::Guard guard(*g_audio);
    if(g_eng->chains[g_edit_chain].units[g_edit_slot]!=&u)return false;
    if(ty<25){
        if(hit(tx,ty,226,0,34,25)){u.bypass=!u.bypass;mark_project_mutated();}
        else if(hit(tx,ty,284,0,28,25)){u.pitch_quantize=!u.pitch_quantize;mark_project_mutated();}
        return true;
    }
    if(u.clean&&hit(tx,ty,244,28,68,24)){fine=!fine;return true;}
    int param=-1;
    if(hit(tx,ty,8,u.clean?54:128,296,34)){param=C::P_SHAPE;
        if(!u.shape_morph){u.params[param].base=C::shape_anchor(u.wave);u.params[param].snap();u.shape_morph=true;mark_project_mutated();}}
    else for(int i=0;i<6;++i)if(hit(tx,ty,(u.clean?clean_x(i):control_x(i))-6,(u.clean?clean_y(i):control_y(i))-3,u.clean?142:96,u.clean?35:30)){param=PARAMS[i];break;}
    if(param<0)return true;
    selected=param;
    drag_param=param;origin_x=tx;origin_value=edit_value(u,param);origin_unit=(uintptr_t)&u;origin_chain=g_edit_chain;origin_slot=g_edit_slot;
    origin_view=g_view;
    origin_bank=g_eng->active_pattern;origin_engine=g_eng;origin_fine=u.clean&&fine;g_drag=Drag::OscShape;return true;
}
void osc_focus_drag(C&u,int tx,int){
    if(drag_param<0)return;
    if(!context()||param_lens_is_open()||g_drag!=Drag::OscShape){osc_focus_release();return;}
    platform::AudioOut::Guard guard(*g_audio);
    if(!owns(u)){osc_focus_release();return;}const int extent=(drag_param==C::P_SHAPE?SHAPE_W:u.clean?CLEAN_W:84)*(origin_fine?32:1);
    const q15 next=q15(clamp(origin_value+(tx-origin_x)*32767/extent));if(next==edit_value(u,drag_param))return;
    u.params[drag_param].base=next;mark_project_mutated();
}
void osc_focus_capture(){
    frame={};
    if(!g_eng||!g_audio){osc_focus_release();fine=false;focus_unit=0;focus_engine=nullptr;return;}
    {
        platform::AudioOut::Guard guard(*g_audio);
        Unit* unit=context()?g_eng->chains[g_edit_chain].units[g_edit_slot]:nullptr;
        C* focus=unit&&unit->kind==UnitKind::Osc?static_cast<C*>(unit):nullptr;
        const bool changed=focus_unit!=uintptr_t(focus)||focus_engine!=g_eng||focus_chain!=g_edit_chain||focus_slot!=g_edit_slot;
        if(focus&&changed)focus->set_visual_capture(false);
        // Iterate current ownership, never dereference a remembered pointer after
        // patch replacement. Disabled collectors do no work on the audio thread.
        for(auto& chain:g_eng->chains)for(Unit* at:chain.units)
            if(at&&at->kind==UnitKind::Osc){auto& osc=*static_cast<C*>(at);osc.set_visual_capture(&osc==focus&&osc.clean&&!osc.bypass);}
        if(!focus){osc_focus_release();fine=false;focus_unit=0;focus_engine=nullptr;return;}
        const auto& u=*focus;const auto& chain=g_eng->chains[g_edit_chain];
        if(changed){fine=false;focus_unit=uintptr_t(focus);focus_engine=g_eng;focus_chain=g_edit_chain;focus_slot=g_edit_slot;}
        if(drag_param>=0&&(!owns(u)||param_lens_is_open()))osc_focus_release();
        frame.valid=true;frame.bypass=u.bypass;frame.mute=chain.mute;
        frame.morph=u.shape_morph;frame.clean=u.clean;frame.fine=fine;frame.lens=false;
        frame.qnt=u.pitch_quantize;frame.scale=g_eng->project_scale();frame.chain=g_edit_chain;frame.slot=g_edit_slot;frame.selected=selected;
        frame.mod_links=focus_mod_links(u);
        for(int p=0;p<C::P_COUNT;++p){frame.edit[p]=edit_value(u,p);frame.live[p]=u.params[p].effective;
            if(u.clean)g_eng->capture_param_inspector(g_edit_chain,g_edit_slot,p,frame.routes[p]);}
        if(!frame.morph)frame.edit[C::P_SHAPE]=frame.live[C::P_SHAPE]=C::shape_anchor(u.wave);
        if(u.clean)frame.visual=u.visual();
    }
    for(int p=0;p<C::P_COUNT;++p){
        osc_focus_value_text(frame.values[p],sizeof frame.values[p],p,frame.edit[p]);
        osc_focus_value_text(frame.live_values[p],sizeof frame.live_values[p],p,frame.live[p]);
    }
    if(!frame.clean){
        for(char* at=frame.values[C::P_PITCH];*at;++at)if(*at==' '){*at=0;break;}
        std::snprintf(frame.values[C::P_SUB],sizeof frame.values[C::P_SUB],"%d%%",(int(frame.edit[C::P_SUB])*100+16383)/32767);
        model();
    }
}
void osc_focus_value_text(char* out,size_t size,int p,q15 value){
    if(!osc_readout::value_text(out,size,p,value,frame.clean,frame.qnt,frame.scale))
        module_inspector_value_text(out,size,UnitKind::Osc,p,value);
}
void osc_focus_draw(const C&){
    m::paper(320);if(!frame.valid)return;char text[48];std::snprintf(text,sizeof text,"ch%02d / %02d",frame.chain+1,frame.slot+1);
    ui::text(58,m::kHeaderTextY,text,m::ghost());ui::text(174,m::kHeaderTextY,"x del",m::ghost());small_press::text(226,0,34,25,230,m::kHeaderTextY,"byp",frame.bypass?m::spot():m::ghost());small_press::text(284,0,28,25,m::right_edge(320)-ui::text_w("qnt"),m::kHeaderTextY,"qnt",frame.qnt?m::spot():m::ghost());
    if(frame.bypass)ui::line(230,m::kHeaderMarkY,246,m::kHeaderMarkY,m::spot());
    if(frame.clean){clean_lower();return;}
    module_heading::header(UnitKind::Osc,m::spot());graph();
    ui::text(SHAPE_X,130,frame.morph?"shape":"shape / legacy",frame.selected==C::P_SHAPE?m::spot():m::ink());
    const char* shape=frame.values[C::P_SHAPE];ui::text(306-ui::text_w(shape),130,shape,frame.selected==C::P_SHAPE?m::spot():m::ghost());
    for(int i=0;i<4;++i){const int x=SHAPE_X+i*SHAPE_W/3;ui::text(x-(i?i==3?30:9:0),153,i==0?"sine":i==1?"tri":i==2?"saw":"pulse",m::ghost());}
    rail(C::P_SHAPE,SHAPE_X,SHAPE_Y,SHAPE_W);
    // Like Filter: outlined edit position, violet smoothed effective position.
    // Capture already owns both scalars; no timer or engine reads in draw.
    for(int i=0;i<6;++i){const int p=PARAMS[i],x=control_x(i),y=control_y(i);
        const u32 c=frame.selected==p?m::spot():m::ink();ui::text(x,y,NAMES[i],c);ui::text(x+84-ui::text_w(frame.values[p]),y,frame.values[p],frame.selected==p?c:m::ghost());
        rail(p,x,y+18,84);
    }
    ui::line(10,224,310,224,m::branch());
    m::footer(320,frame.mute?"muted":frame.bypass?"bypassed":"outline base / fill live","l: mod >");
}
bool osc_focus_draw_top(){
    if(!frame.valid||!frame.clean)return false;
    m::paper(400);module_heading::header(UnitKind::Osc,m::spot());ui::text(94,m::kHeaderTextY,"clean / before fx",m::ghost());
    char text[64];std::snprintf(text,sizeof text,"ch%02d / %02d",frame.chain+1,frame.slot+1);right(m::right_edge(400),m::kHeaderTextY,text,m::ghost());
    clean_scope();ui::line(261,43,261,216,m::branch());
    const int p=frame.selected;const auto& d=frame.routes[p];
    ui::text(270,45,param_name(p),m::spot());
    ui::text(270,68,d.temp?"stored / temp":"stored",m::ghost());
    split_value(frame.values[p],82,m::ink());
    ui::text(270,112,"live control",m::ghost());split_value(frame.live_values[p],126,m::spot());
    if(d.source==ModSource::None)std::snprintf(text,sizeof text,"route off");
    else if(d.source==ModSource::EnvFollow)std::snprintf(text,sizeof text,d.mod_enabled?"env ch%d / %+d%%":"env%d muted %+d%%",d.env_chain+1,int(d.depth)*100/32767);
    else std::snprintf(text,sizeof text,d.mod_enabled?"sigil %c / %+d%%":"sg.%c muted %+d%%",'a'+mod_source_sigil(d.source),int(d.depth)*100/32767);
    ui::text(270,158,text,d.source==ModSource::None || !d.mod_enabled?m::ghost():m::spot());
    static const char* layers[]={"base","step lock","audition","performance","temp"};
    ui::text(270,174,layers[std::min(4,int(d.layer))],m::ghost());
    std::snprintf(text,sizeof text,"audio %.1fhz",double(frame.visual.frequency_millihz)/1000.0);
    ui::text(270,194,frame.visual.valid?text:"audio pending",m::ghost());
    ui::line(16,225,384,225,m::branch());ui::text(m::kLeft,m::kTextY,"osc out / pitch vs stored",m::ghost());
    const char* state=frame.bypass?"bypassed":frame.mute?"chain muted":frame.lens?"modulation":"fixed +/-fs";
    right(m::right_edge(400),m::kTextY,state,m::spot());return true;
}
} // namespace seance::platform::ui3ds
