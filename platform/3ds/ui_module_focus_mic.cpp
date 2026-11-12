#include "ui_module_focus_internal.h"
#include "ui_state.h"
#include "ui_theme.h"
#include "ui_palette.h"
#include "ui_param_lens.h"
#include "editor_style.h"
#include "module_heading.h"
#include "mic_ui.h"
#include "mic_relief.h"
#include "audio_out.h"
#include "../../core/ui/draw.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <type_traits>

namespace seance::platform::ui3ds {
namespace {
namespace m=editor_style;
namespace geo=mic_ui;
namespace relief=mic_relief;
using C=MicUnit;
constexpr int GAIN_SPAN=279;
struct Frame {
    uint16_t mod_links=0;
    bool valid=false,bypass=false,mute=false,lens=false;
    bool output_valid=false,clip=false;
    int8_t chain=0,slot=0,selected=0,sign=0;
    q15 edit[C::P_COUNT]={},live[C::P_COUNT]={};
    char gain[16]={},level[16]={};
    int16_t meter_x=-1;
    relief::Status status=relief::Status::Waiting;
    relief::Plot plot;
} frame;
static_assert(sizeof(Frame)<=5500 && std::is_trivially_copyable<Frame>::value,
              "bounded copied MIC frame; never read live PCM/engine during drawing");
// Shared physical input is copied only while guarded, reduced outside guard.
q15 input_copy[C::INPUT_VISUAL_SAMPLES];
static_assert(C::INPUT_VISUAL_SAMPLES==relief::SAMPLES,"raw window contract");
relief::History history;
struct Gesture {
    uintptr_t unit=0;Engine* engine=nullptr;platform::AudioOut* audio=nullptr;
    int chain=-1,slot=-1,bank=-1,x=0,param=-1,action=0;
    View view=View::Chains;bool bypass=false;
    q15 value=0;geo::Box box{};
} gesture;
enum Action { DragGain=0, Choice, Bypass };
int selected=C::P_GAIN;
bool blocked_frame=false;
struct Freshness {
    uintptr_t unit=0;Engine* engine=nullptr;int chain=-1,slot=-1;
    uint32_t process_serial=0;uint64_t process_time=0,clock=0;
} freshness;
bool capture_context() {
    return g_eng && g_audio && g_overlay==Overlay::Editor &&
        g_view==View::Chains &&
        g_edit_chain>=0 && g_edit_chain<NUM_CHAINS && g_edit_slot>=0 && g_edit_slot<UNITS_PER_CHAIN;
}
bool context(){return !param_lens_is_open() && capture_context();}
C* current(){if(!capture_context())return nullptr;Unit*u=g_eng->chains[g_edit_chain].units[g_edit_slot];return u&&u->kind==UnitKind::MicIn?static_cast<C*>(u):nullptr;}
q15 edit_value(const C&u,int p){return u.params[p].base;}
bool owns(const C&u){
    return context() && !blocked_frame && g_drag==Drag::MicGain && gesture.engine==g_eng && gesture.audio==g_audio &&
        gesture.unit==uintptr_t(&u) && current()==&u && gesture.chain==g_edit_chain && gesture.slot==g_edit_slot &&
        gesture.view==g_view && gesture.bank==g_eng->active_pattern &&
        gesture.bypass==u.bypass;
}
void begin(C&u,int x,int p,int action,geo::Box box){
    gesture={};gesture.unit=uintptr_t(&u);gesture.engine=g_eng;gesture.audio=g_audio;
    gesture.chain=g_edit_chain;gesture.slot=g_edit_slot;gesture.view=g_view;
    gesture.bank=g_eng->active_pattern;
    gesture.x=x;gesture.param=p;gesture.action=action;gesture.box=box;gesture.bypass=u.bypass;
    if(p>=0)gesture.value=edit_value(u,p);
    g_drag=Drag::MicGain;
}
void set_value(C&u,int p,q15 v){
    if(v==edit_value(u,p))return;
    u.params[p].base=v;mark_project_mutated();
}
int gain_x(q15 v){return 20+(std::max(0,int(v))*GAIN_SPAN+16383)/32767;}
void marker(int x,int y,u32 c){
    ui::line(x,y-3,x+3,y,c);ui::line(x+3,y,x,y+3,c);
    ui::line(x,y+3,x-3,y,c);ui::line(x-3,y,x,y-3,c);
}
void right(int x,int y,const char*s,u32 color){ui::text(x-ui::text_w(s),y,s,color);}
void capture_level(int peak){
    if(!frame.output_valid){std::snprintf(frame.level,sizeof frame.level,"--");return;}
    if(!peak){std::snprintf(frame.level,sizeof frame.level,"silent");return;}
    const double db=20*std::log10(double(peak)/32768.0);
    std::snprintf(frame.level,sizeof frame.level,"%.1f dbfs",std::abs(db)<.05?0.0:db);
    frame.meter_x=58+int(std::lround((std::max(-60.0,db)+60.0)*214/60));
}
void draw_relief(){
    for(int row=0;row<relief::ROWS;++row){
        const u32 ink=row==5||row==12?m::spot():m::ink();
        const u32 color=(ink&0xffffffu)|(uint32_t((32+65*row/14)*255/100)<<24);
        const int x0=relief::left(row),w=relief::width(row);
        const auto*y=frame.plot.y[row];int at=0;
        while(at<=w){
            if(y[at]==relief::HIDDEN){++at;continue;}
            const int start=at;
            if(at<w&&y[at+1]!=relief::HIDDEN){const int slope=int(y[at+1])-y[at];++at;
                while(at<w&&y[at+1]!=relief::HIDDEN&&int(y[at+1])-y[at]==slope)++at;}
            ui::line(x0+start,y[start],x0+at,y[at],color);
            // Adjacent visible strokes share their endpoint; hidden gaps do not.
            if(at==start||at==w||y[at+1]==relief::HIDDEN)++at;
        }
    }
}
} // namespace
int mic_focus_selected(){return selected;}
void mic_focus_select(int p){if(p>=0&&p<C::P_COUNT){if(p!=selected)mic_focus_release();selected=p;}}
void mic_focus_release(){gesture={};if(g_drag==Drag::MicGain)g_drag=Drag::None;}
void mic_focus_input(u32 down,bool blocked){
    blocked_frame=blocked||bool(down&~(KEY_TOUCH|KEY_ZL));
    if(!gesture.unit)return;
    if(blocked_frame||!context()){mic_focus_release();return;}
    platform::AudioOut::Guard guard(*g_audio);C*u=current();if(!u||!owns(*u))mic_focus_release();
}
bool mic_focus_touch_body(C&u,int x,int y){
    mic_focus_release();if(!context()||blocked_frame)return false;
    platform::AudioOut::Guard guard(*g_audio);if(current()!=&u)return false;
    if(geo::bypass.has(x,y)){begin(u,x,-1,Bypass,geo::bypass);return true;}
    const geo::Box names[]={geo::gain_name,geo::input_name,geo::cut_name};
    for(int p=0;p<C::P_COUNT;++p)if(names[p].has(x,y)){
        selected=p;return true;}
    if(geo::gain_value.has(x,y)||geo::gain_rail.has(x,y)){
        selected=C::P_GAIN;begin(u,x,C::P_GAIN,DragGain,geo::gain_rail);return true;}
    if(geo::open.has(x,y)||geo::mute.has(x,y)){
        selected=C::P_MON;begin(u,x,C::P_MON,Choice,geo::open.has(x,y)?geo::open:geo::mute);return true;}
    for(int i=0;i<3;++i)if(geo::cut(i).has(x,y)){
        selected=C::P_LOW_CUT;begin(u,x,C::P_LOW_CUT,Choice,geo::cut(i));return true;}
    return true; // spaces/passive labels never fall through to retired controls
}
void mic_focus_drag(C&u,int x,int y){
    if(!gesture.unit)return;
    if(!context()){mic_focus_release();return;}
    platform::AudioOut::Guard guard(*g_audio);if(!owns(u)){mic_focus_release();return;}
    if(gesture.action!=DragGain){if(!gesture.box.has(x,y))mic_focus_release();return;}
    const int64_t next=int64_t(gesture.value)+(int64_t(x)-gesture.x)*32767/GAIN_SPAN;
    set_value(u,C::P_GAIN,q15(std::max<int64_t>(0,std::min<int64_t>(32767,next))));
}
void mic_focus_touch_release(){
    if(gesture.unit&&context()&&!blocked_frame){platform::AudioOut::Guard guard(*g_audio);C*u=current();
        if(u&&owns(*u)){
            const int p=gesture.param;
            if(gesture.action==Bypass){u->bypass=!u->bypass;mark_project_mutated();}
            else if(p>=0&&edit_value(*u,p)==gesture.value){
                if(gesture.action==Choice){
                    const q15 value=p==C::P_MON?(gesture.box.x==geo::open.x?32767:0):
                        gesture.box.x==geo::cut(0).x?0:gesture.box.x==geo::cut(1).x?16384:32767;
                    set_value(*u,p,value);
                }
            }
        }
    }
    mic_focus_release();
}
void mic_focus_capture(){
    frame={};if(!capture_context()){mic_focus_release();freshness={};history.reset();return;}
    int count=0,output_peak=0;uint32_t input_serial=0,process_serial=0;uintptr_t identity=0;
    {platform::AudioOut::Guard guard(*g_audio);const C*u=current();
        if(!u){mic_focus_release();freshness={};history.reset();return;}
        identity=uintptr_t(u);if(gesture.unit&&!owns(*u))mic_focus_release();
        frame.valid=true;frame.bypass=u->bypass;frame.mute=g_eng->chains[g_edit_chain].mute;
        frame.lens=param_lens_is_open();
        frame.chain=g_edit_chain;frame.slot=g_edit_slot;frame.selected=selected;frame.mod_links=focus_mod_links(*u);
        for(int p=0;p<C::P_COUNT;++p){frame.edit[p]=edit_value(*u,p);frame.live[p]=u->params[p].effective;}
        if(g_mic_ok)count=C::copy_input(input_copy,C::INPUT_VISUAL_SAMPLES);
        input_serial=C::input_serial();process_serial=u->visual_process_serial();output_peak=u->visual_output_peak();frame.clip=u->visual_clip();
    }
    const uint64_t now=osGetTime();
    if(freshness.engine!=g_eng||freshness.unit!=identity||freshness.chain!=g_edit_chain||freshness.slot!=g_edit_slot||now<freshness.clock){
        freshness={};history.reset();freshness.process_time=now;
    }
    if(freshness.process_serial!=process_serial)freshness.process_time=now;
    freshness.engine=g_eng;freshness.unit=identity;freshness.chain=g_edit_chain;freshness.slot=g_edit_slot;
    freshness.process_serial=process_serial;freshness.clock=now;
    history.update(input_copy,count,input_serial,now,g_mic_ok);frame.status=history.status;history.project(frame.plot);
    const bool input_valid=frame.status==relief::Status::Live;
    frame.output_valid=input_valid&&process_serial&&now-freshness.process_time<relief::STALE_MS;
    const bool closed=frame.bypass||frame.mute||frame.live[C::P_MON]<=16384;
    if(closed){output_peak=0;frame.output_valid=input_valid;}
    frame.clip=frame.clip&&frame.output_valid&&!closed;capture_level(output_peak);
    if(frame.edit[C::P_GAIN]>0){const double db=20*std::log10(double(frame.edit[C::P_GAIN])/8192.0);
        std::snprintf(frame.gain,sizeof frame.gain,"%.1f",std::abs(db));frame.sign=std::abs(db)<.05?0:db<0?-1:1;}
    else std::snprintf(frame.gain,sizeof frame.gain,"silent");
}
void mic_focus_draw(const C&){
    m::paper(320);if(!frame.valid)return;
    char text[24];std::snprintf(text,sizeof text,"ch%02d / %02d",frame.chain+1,frame.slot+1);
    module_heading::header(UnitKind::MicIn,m::spot());
    ui::text(54,m::kHeaderTextY,text,m::ghost());ui::text(242,m::kHeaderTextY,"byp",frame.bypass?m::spot():m::ghost());right(m::right_edge(320),m::kHeaderTextY,"x del",m::ghost());
    geo::word(20,65,"gain",frame.selected==C::P_GAIN?m::spot():m::ink());
    if(frame.edit[C::P_GAIN]>0){const int x=geo::number(276,66,frame.gain,m::ink());
        if(frame.sign)ui::text(x-12,53,frame.sign<0?"-":"+",m::ink());}
    else geo::word(276,66,"silent",m::ink(),2);
    ui::text(286,56,"db",m::ghost());
    const int unity=gain_x(8192);ui::text(unity-3,103,"0",m::ghost());
    module_fader::horizontal(20,299,94,gain_x(frame.edit[C::P_GAIN]),gain_x(frame.live[C::P_GAIN]),
        {bool(frame.mod_links&(1u<<C::P_GAIN)),false,frame.bypass||frame.mute},unity);
    ui::text(20,138,"input",frame.selected==C::P_MON?m::spot():m::ghost());
    const bool opened=frame.edit[C::P_MON]>16384,live_open=frame.live[C::P_MON]>16384;
    geo::word(155,152,"open",opened?m::spot():m::ghost(),1);geo::word(264,152,"mute",!opened?m::spot():m::ghost(),1);
    if(opened!=live_open)marker(live_open?198:300,145,m::spot());
    ui::text(20,191,"low-cut",frame.selected==C::P_LOW_CUT?m::spot():m::ghost());
    const int xs[]={138,207,279};const char*words[]={"off","80","160"};
    const int edit_mode=C::low_cut_mode(frame.edit[C::P_LOW_CUT]),live_mode=C::low_cut_mode(frame.live[C::P_LOW_CUT]);
    for(int i=0;i<3;++i){geo::word(xs[i],203,words[i],i==edit_mode?m::spot():m::ghost(),1);
        if(i==live_mode&&i!=edit_mode)marker(xs[i],212,m::spot());}
    ui::text(m::kLeft,m::kTextY,"b back",m::ghost());
    right(m::right_edge(320),m::kTextY,"l: mod >",m::ghost());
}
bool mic_focus_draw_top(){
    if(!frame.valid||frame.lens)return false;
    m::paper(400);module_heading::header(UnitKind::MicIn,m::spot());char text[24];
    std::snprintf(text,sizeof text,"ch%02d / %02d",frame.chain+1,frame.slot+1);right(m::right_edge(400),m::kHeaderTextY,text,m::ghost());
    ui::text(20,59,"live input",m::ghost());
    if(frame.bypass||frame.mute||frame.live[C::P_MON]<=16384)
        right(380,59,frame.bypass?"bypassed":frame.mute?"chain muted":"source muted",m::ghost());
    if(frame.status==relief::Status::Live)draw_relief();
    else{const char*s=frame.status==relief::Status::Unavailable?"mic unavailable":frame.status==relief::Status::Stale?"no new input":"waiting for input";
        ui::text(200-ui::text_w(s)/2,123,s,m::ghost());}
    ui::text(20,204,"out",m::ghost());ui::line(58,208,272,208,palette::shadow());ui::line(272,206,272,210,m::ghost());
    if(frame.meter_x>=58){ui::line(58,208,frame.meter_x,208,m::spot());ui::line(frame.meter_x,205,frame.meter_x,211,m::spot());}
    right(380,204,frame.level,frame.meter_x>=0?m::ink():m::ghost());
    if(frame.clip)right(m::right_edge(400),m::kTextY,"clip",m::spot());
    return true;
}
} // namespace seance::platform::ui3ds
