#include "ui_module_focus_internal.h"
#include "ui_state.h"
#include "ui_sequencer.h"
#include "ui_theme.h"
#include "ui_param_lens.h"
#include "module_inspector.h"
#include "editor_style.h"
#include "module_heading.h"
#include "strain_reference.h"
#include "strain_editor_visual.h"
#include "strain_readout.h"
#include "audio_out.h"
#include "../../core/ui/draw.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <type_traits>

namespace seance::platform::ui3ds {
namespace {
namespace m=editor_style;
namespace model=strain_reference;
namespace l=strain_editor_visual;
using C=StrainUnit;
union Visual {model::Plot legacy;l::Plot modern;Visual():modern{}{}};
struct Frame {
    uint16_t mod_links=0;
    bool valid=false,modern=false,setup=false,bypass=false,mute=false,lens=false;
    int8_t chain=0,slot=0,selected=0,route_first=0,route_last=UNITS_PER_CHAIN-1;
    q15 edit[C::P_COUNT]={},live[C::P_COUNT]={};char values[C::P_COUNT][18]={};
    char cycle[18]={},edit_cycle[18]={};
    uint8_t kinds[UNITS_PER_CHAIN]={};bool bypassed[UNITS_PER_CHAIN]={};
    Visual plot;
} frame;
static_assert(sizeof(Frame)<2560 && std::is_trivially_copyable<Frame>::value,
              "bounded pointer-free strain frame; no engine/PCM/model work in draw");
struct Gesture {
    Engine* engine=nullptr;uintptr_t unit=0;View view=View::Chains;
    int chain=-1,slot=-1,bank=-1,param=-1,start=0;
    bool modern=false,setup=false,fine=false;q15 value=0;
} gesture;
struct Key {q15 values[C::P_COUNT]={};bool modern=false,bypass=false;};
Key cached_key;Visual cached_plot;bool cache_valid=false;
int selected=C::P_DRIVE,bound_chain=-1,bound_slot=-1;
Engine* bound_engine=nullptr;uintptr_t bound_unit=0;
bool setup=false,bound_modern=false,fine_held=false,blocked_frame=false;
struct Control {int x,y,w,h,label_x,label_y,rail_y,extent;const char* name;};
// Persisted order is independent of the historical or modern layout.
constexpr Control CONTROLS[C::LEGACY_COUNT]={
    {8,27,146,46,14,32,65,128,"drive"}, {84,177,72,44,90,182,214,62,"bias"},
    {8,177,72,44,14,182,214,62,"body"}, {168,27,146,46,174,32,65,128,"teeth"},
    {160,177,72,44,166,182,214,62,"fdbk"}, {236,177,72,44,242,182,214,62,"mix"}
};
constexpr int main_params[4][2]={{C::P_DRIVE,C::P_TEETH},{C::P_BODY,C::P_BIAS},
    {C::P_FEEDBACK,C::P_MOTION},{C::P_TRIM,C::P_MIX}};
constexpr const char* main_names[4][2]={{"drive","teeth"},{"body","bias"},{"feedback","motion"},{"wet trim","mix"}};
bool capture_context(){return g_eng&&g_audio&&g_overlay==Overlay::Editor&&
    g_view==View::Chains&&
    g_edit_chain>=0&&g_edit_chain<NUM_CHAINS&&g_edit_slot>=0&&g_edit_slot<UNITS_PER_CHAIN;}
bool context(){return !blocked_frame&&!param_lens_is_open()&&capture_context();}
void release(){gesture={};if(g_drag==Drag::StrainParam)g_drag=Drag::None;}
void bind(const C& u){
    if(bound_engine!=g_eng||bound_unit!=uintptr_t(&u)||bound_chain!=g_edit_chain||bound_slot!=g_edit_slot||bound_modern!=u.modern()){
        release();setup=false;selected=C::P_DRIVE;bound_engine=g_eng;bound_unit=uintptr_t(&u);
        bound_chain=g_edit_chain;bound_slot=g_edit_slot;bound_modern=u.modern();cache_valid=false;
    }
}
q15 edit_value(const C& u,int p){
    return u.params[p].base;
}
void set_value(C& u,int p,q15 value){
    if(p<0||p>=u.num_params)return;
    if(u.params[p].base==value)return;
    u.params[p].base=value;
    mark_project_mutated();cache_valid=false;
}
int clamp(int v,int p){return std::max(p==C::P_BIAS?-32767:0,std::min(32767,v));}
u32 color(int p){return frame.selected==p?m::spot():m::ink();}
int position(q15 value,int p,int left,int right){
    const bool bi=p==C::P_BIAS;
    return left+(clamp(value,p)+(bi?32767:0))*(right-left)/(bi?65534:32767);
}
void begin(C& u,int p,int x){
    selected=p;gesture={};gesture.engine=g_eng;gesture.unit=uintptr_t(&u);gesture.view=g_view;
    gesture.chain=g_edit_chain;gesture.slot=g_edit_slot;gesture.bank=g_eng->active_pattern;
    gesture.param=p;gesture.start=x;gesture.value=edit_value(u,p);gesture.setup=setup;
    gesture.modern=u.modern();gesture.fine=fine_held;g_drag=Drag::StrainParam;g_drag_param=p;
}
bool owns(const C& u){return gesture.engine==g_eng&&gesture.unit==uintptr_t(&u)&&gesture.view==g_view&&
    gesture.chain==g_edit_chain&&gesture.slot==g_edit_slot&&gesture.bank==g_eng->active_pattern&&
    gesture.param==selected&&gesture.setup==setup&&gesture.fine==fine_held&&
    gesture.modern==u.modern()&&g_eng->chains[g_edit_chain].units[g_edit_slot]==&u;}
void capture_route(){
    // Keep complete labels and the selected slot even if future names outgrow
    // the full six-slot route. Current longest six labels still all fit.
    auto width=[](int first,int last){int total=0;
        for(int i=first;i<=last;++i)if(UnitKind(frame.kinds[i])!=UnitKind::Empty){
            if(total)total+=22;
            total+=ui::text_w(m::module_name(UnitKind(frame.kinds[i])));}
        return total;};
    while(width(frame.route_first,frame.route_last)>m::right_edge(400)-m::kLeft){
        if(frame.slot-frame.route_first>=frame.route_last-frame.slot && frame.route_first<frame.slot)++frame.route_first;
        else if(frame.route_last>frame.slot)--frame.route_last;
        else break;
    }
}
void capture_plot(){
    Key key;key.modern=frame.modern;key.bypass=frame.bypass;
    std::copy(frame.live,frame.live+C::P_COUNT,key.values);
    if(!cache_valid||key.modern!=cached_key.modern||key.bypass!=cached_key.bypass||
        !std::equal(std::begin(key.values),std::end(key.values),std::begin(cached_key.values))){
        // Placement construction activates only a bounded union member; no heap.
        if(key.modern){new(&cached_plot.modern) l::Plot{};l::calculate(key.values,key.bypass,cached_plot.modern);}
        else {new(&cached_plot.legacy) model::Plot{};model::plot(key.values,key.bypass,cached_plot.legacy);}
        cached_key=key;cache_valid=true;
    }
    frame.plot=cached_plot;
}
void right(int x,int y,const char* text,u32 c){ui::text(x-ui::text_w(text),y,text,c);}
void control(int p,int left,int end,int y,const char* name){
    ui::text(left,y,name,frame.selected==p?m::spot():m::ghost());right(end,y,frame.values[p],color(p));
    module_fader::horizontal(left,end,y+23,
        position(frame.edit[p],p,left,end),position(frame.live[p],p,left,end),
        {bool(frame.mod_links&(1u<<p)),false,frame.bypass||frame.mute},
        p==C::P_BIAS?position(0,p,left,end):p==C::P_TRIM?position(C::DEFAULT_TRIM,p,left,end):-1);
}
void trace(const model::Trace& t,u32 c){
    for(int i=0;i<model::COLUMNS;++i){const int x=model::X0+i;
        if(i)ui::line(x-1,t.last[i-1],x,t.first[i],c);
        ui::line(x,t.lo[i],x,t.hi[i],c);}
}
void legacy_reference(){
    ui::line(14,82,24,82,m::ghost());ui::text(30,78,"test 250hz/.75fs",m::ghost());
    const auto out=frame.mute?m::ghost():m::spot();
    const char* label=frame.bypass?"byp dry":frame.mute?"muted":"out";
    const int label_x=310-ui::text_w(label);ui::line(label_x-18,82,label_x-8,82,out);ui::text(label_x,78,label,out);
    for(int level:{1,0,-1}){const int y=model::ZERO-level*model::HEIGHT;
        const char* label=level>0?"+1":level<0?"-1":"0";ui::text(56-ui::text_w(label),y-4,label,m::ghost());
        if(!level)ui::line(model::X0,y,306,y,m::branch());
        else for(int x=model::X0;x<=306;x+=5)ui::rect(x,y,1,1,m::branch());}
    for(int y=95;y<146;y+=4)ui::rect(185,y,1,1,m::branch());
    trace(frame.plot.legacy.source,m::ghost());trace(frame.plot.legacy.shaped,out);
    const int ticks[3]={model::X0,185,306};const char* labels[3]={"0","16","32ms"};
    for(int i=0;i<3;++i){ui::line(ticks[i],146,ticks[i],148,m::branch());
        ui::text(ticks[i]-(i==2?ui::text_w(labels[i]):0),151,labels[i],m::ghost());}
    ui::line(306,163,306,171,m::branch());ui::line(306,171,222,171,m::branch());
    ui::text(99,167,"1ms / dc / lp return",m::ghost());
    ui::line(91,171,model::X0,171,m::branch());ui::line(model::X0,171,model::X0,163,m::branch());
    ui::line(model::X0-3,166,model::X0,163,m::ghost());ui::line(model::X0,163,model::X0+3,166,m::ghost());
}
} // namespace
int strain_focus_selected(){return selected;}
void strain_focus_select(int p){if(p>=0&&p<C::P_COUNT){if(p!=selected)release();selected=p;}}
void strain_focus_release(){release();}
bool strain_focus_modern(){return frame.valid&&frame.modern;}
bool strain_focus_back(){if(!context()||!setup)return false;release();setup=false;return true;}
void strain_focus_value_text(char* out,size_t size,int p,q15 value){strain_readout::value_text(out,size,p,value,strain_focus_modern());}
void strain_focus_input(u32 down,u32 held,bool blocked){
    const bool fine=(held&KEY_R)!=0;
    if(fine!=fine_held)release();
    fine_held=fine;
    blocked_frame=blocked;
    if(blocked||(down&~u32(KEY_TOUCH|KEY_R|KEY_ZL)))release();
}
bool strain_focus_touch_body(C& u,int tx,int ty){
    if(!context()){release();return false;}
    platform::AudioOut::Guard guard(*g_audio);
    if(g_eng->chains[g_edit_chain].units[g_edit_slot]!=&u)return false;
    bind(u);release();
    if(u.modern()&&hit(tx,ty,198,0,48,26)){setup=!setup;return true;}
    if(!u.modern()&&hit(tx,ty,164,0,76,26)){
        {C defaults;
            for(int p=C::LEGACY_COUNT;p<C::P_COUNT;++p)u.params[p]=defaults.params[p];
            for(auto& scene:g_eng->scenes[g_edit_chain])if(scene.kind[g_edit_slot]==UnitKind::Strain)
                for(int p=C::LEGACY_COUNT;p<C::P_COUNT;++p)scene.base[g_edit_slot][p]=defaults.params[p].base;
            u.set_modern(true);mark_project_mutated();bind(u);}
        return true;
    }
    if(ty<26){if(hit(tx,ty,246,0,38,25)){u.bypass=!u.bypass;mark_project_mutated();}return true;}
    if(u.modern()){
        if(setup){if(hit(tx,ty,8,l::kSetupY-8,304,42)){
            selected=C::P_RATE;
            begin(u,C::P_RATE,tx);}}
        else for(int row=0;row<4;++row)for(int col=0;col<2;++col){
            const int p=main_params[row][col],x=l::kControlLeft[col],end=l::kControlRight[col],y=l::kMainY[row];
            if(!hit(tx,ty,x-6,y-8,end-x+12,42))continue;
            selected=p;begin(u,p,tx);return true;}
        return true;
    }
    for(int p=0;p<C::LEGACY_COUNT;++p){const auto& c=CONTROLS[p];if(!hit(tx,ty,c.x,c.y,c.w,c.h))continue;
        selected=p;begin(u,p,tx);return true;}
    return true;
}
void strain_focus_drag(C& u,int tx,int){
    if(gesture.param<0)return;
    if(!context()||g_drag!=Drag::StrainParam){release();return;}
    platform::AudioOut::Guard guard(*g_audio);if(!owns(u)){release();return;}
    const int p=gesture.param,range=p==C::P_BIAS?65534:32767,delta=tx-gesture.start;
    const int extent=u.modern()?296:CONTROLS[p].extent;
    const int movement=u.modern()&&p==C::P_BIAS&&gesture.fine?delta:delta*range/extent;
    int next=clamp(int(gesture.value)+movement,p);
    // A signed crossing lands on exact zero, rather than skipping it at 221raw/px.
    if(p==C::P_BIAS&&((gesture.value<0&&next>0)||(gesture.value>0&&next<0))&&std::abs(next)<range/extent)next=0;
    set_value(u,p,q15(next));
}
void strain_focus_capture(){
    frame={};if(!capture_context()){release();bound_engine=nullptr;bound_unit=0;setup=false;selected=C::P_DRIVE;cache_valid=false;return;}
    {
        platform::AudioOut::Guard guard(*g_audio);
        const auto& chain=g_eng->chains[g_edit_chain];const Unit* unit=chain.units[g_edit_slot];
        if(!unit||unit->kind!=UnitKind::Strain){release();bound_engine=nullptr;bound_unit=0;setup=false;selected=C::P_DRIVE;cache_valid=false;return;}
        const auto& u=*static_cast<const C*>(unit);bind(u);
        if(gesture.param>=0&&!owns(u))release();
        if(selected>=u.num_params){selected=C::P_DRIVE;release();}
        frame.valid=true;frame.modern=u.modern();frame.setup=setup;frame.bypass=u.bypass;frame.mute=chain.mute;frame.lens=param_lens_is_open();
        frame.chain=g_edit_chain;frame.slot=g_edit_slot;frame.selected=selected;frame.mod_links=focus_mod_links(u);
        for(int i=0;i<UNITS_PER_CHAIN;++i){frame.kinds[i]=chain.units[i]?uint8_t(chain.units[i]->kind):uint8_t(UnitKind::Empty);
            frame.bypassed[i]=chain.units[i]&&chain.units[i]->bypass;}
        for(int p=0;p<u.num_params;++p){frame.edit[p]=edit_value(u,p);frame.live[p]=u.modern()?u.control_value(p):u.params[p].effective;}
    }
    for(int p=0;p<(frame.modern?C::P_COUNT:C::LEGACY_COUNT);++p)
        strain_readout::value_text(frame.values[p],sizeof frame.values[p],p,frame.edit[p],frame.modern);
    if(frame.modern){strain_readout::cycle_text(frame.cycle,sizeof frame.cycle,frame.live[C::P_RATE]);
        strain_readout::cycle_text(frame.edit_cycle,sizeof frame.edit_cycle,frame.edit[C::P_RATE]);}
    capture_route();
    if(!frame.lens)capture_plot(); // actual private DSP / core motion outside guard, before GPU
}
void strain_focus_draw(const C&){
    m::paper(320);if(!frame.valid)return;
    module_heading::header(UnitKind::Strain,m::spot());
    char text[24];std::snprintf(text,sizeof text,"ch%02d / %02d",frame.chain+1,frame.slot+1);
    ui::text(78,m::kHeaderTextY,text,m::ghost());
    if(frame.modern)ui::text(206,m::kHeaderTextY,frame.setup?"main":"setup",m::spot());
    else ui::text(170,m::kHeaderTextY,"extend",m::spot());
    ui::text(258,m::kHeaderTextY,"byp",frame.bypass?m::spot():m::ghost());
    right(m::right_edge(320),m::kHeaderTextY,"x",m::ghost());
    if(frame.bypass)ui::line(258,m::kHeaderMarkY,275,m::kHeaderMarkY,m::spot());
    if(frame.lens){m::footer(320,"b back","l: mod >");return;}
    if(frame.modern){
        if(frame.setup){ui::text(14,43,"motion / setup",m::spot());control(C::P_RATE,14,306,l::kSetupY,"rate");
            ui::text(14,125,"one cycle",m::ghost());right(306,125,frame.edit_cycle,m::ink());
            ui::text(14,158,"motion 0 = no internal movement",m::ghost());
            ui::text(14,176,"body keeps its stored tone",m::ghost());
            ui::text(14,202,"wet trim is outside the return loop",m::ghost());}
        else for(int row=0;row<4;++row)for(int col=0;col<2;++col)
            control(main_params[row][col],l::kControlLeft[col],l::kControlRight[col],l::kMainY[row],main_names[row][col]);
        m::footer(320,frame.setup?"b main":frame.selected==C::P_BIAS?"r: fine bias / b back":"b back","l: mod >");
        return;
    }
    for(int p=0;p<C::LEGACY_COUNT;++p){const auto& c=CONTROLS[p];const bool top=p==C::P_DRIVE||p==C::P_TEETH;
        ui::text(c.label_x,c.label_y,c.name,frame.selected==p?m::spot():top?m::ink():m::ghost());
        if(top)ui::text(c.label_x+c.extent-24,c.label_y,p==C::P_DRIVE?"gain":"fold",m::ghost());
        ui::text(c.label_x,top?48:196,frame.values[p],color(p));
        module_fader::horizontal(c.label_x,c.label_x+c.extent,c.rail_y,
            position(frame.edit[p],p,c.label_x,c.label_x+c.extent),position(frame.live[p],p,c.label_x,c.label_x+c.extent),
            {bool(frame.mod_links&(1u<<p)),false,frame.bypass||frame.mute},p==C::P_BIAS?position(0,p,c.label_x,c.label_x+c.extent):-1);}
    legacy_reference();ui::line(14,223,306,223,m::branch());
    m::footer(320,"b back","l: mod >");
}
bool strain_focus_draw_top(){
    if(!frame.valid||!frame.modern||frame.lens)return false;
    m::paper(400);module_heading::header(UnitKind::Strain,m::spot());
    char text[48];std::snprintf(text,sizeof text,"ch%02d / %02d",frame.chain+1,frame.slot+1);
    right(m::right_edge(400),m::kHeaderTextY,text,m::ghost());
    int rx=14;for(int i=frame.route_first;i<=frame.route_last;++i){const auto kind=UnitKind(frame.kinds[i]);if(kind==UnitKind::Empty)continue;
        if(rx>14){ui::text(rx,module_heading::kRouteY,"/",m::ghost());rx+=14;}
        const char* name=m::module_name(kind);const int w=ui::text_w(name);
        ui::text(rx,module_heading::kRouteY,name,i==frame.slot?m::spot():m::ghost());
        if(i==frame.slot)ui::line(rx,module_heading::kRouteUnderlineY,rx+w-2,module_heading::kRouteUnderlineY,m::spot());
        if(frame.bypassed[i])ui::line(rx,module_heading::kRouteCrossY,rx+w-2,module_heading::kRouteCrossY,m::ghost());
        rx+=w+8;
    }
    const char* name=frame.selected==C::P_RATE?"rate":frame.selected==C::P_TRIM?"wet trim":frame.selected==C::P_MOTION?"motion":CONTROLS[frame.selected].name;
    std::snprintf(text,sizeof text,"%s %s",name,frame.values[frame.selected]);
    if(rx+8+ui::text_w(text)<=386)right(386,module_heading::kRouteY,text,m::ink());
    const u32 out=frame.mute?m::ghost():m::spot();
    ui::line(14,59,26,59,m::ghost());ui::text(32,55,"in",m::ghost());
    ui::line(64,59,76,59,out);ui::text(82,55,frame.bypass?"dry":"out",out);
    right(386,55,"test / 250hz burst",m::ghost());
    for(int level:{1,0,-1}){const int y=l::kZero-level*l::kHeight;
        ui::text(14,y-4,level>0?"+1":level<0?"-1":"0",m::ghost());
        if(!level)ui::line(l::kLeft,y,l::kRight,y,palette::shadow());
        else for(int x=l::kLeft;x<=l::kRight;x+=5)ui::rect(x,y,1,1,palette::shadow());}
    for(int y=72;y<147;y+=4)ui::rect(210,y,1,1,palette::shadow());
    for(int i=0;i<l::kColumns;++i){const int x=l::kLeft+i;const auto& c=l::source.columns[i];
        if(i)ui::line(x-1,l::kZero-l::source.columns[i-1].last,x,l::kZero-c.first,m::ghost());
        ui::line(x,l::kZero-c.high,x,l::kZero-c.low,m::ghost());}
    const auto& plot=frame.plot.modern;
    for(int i=0;i<l::kColumns;++i){const int x=l::kLeft+i;const auto& c=plot.out[i];
        if(i)ui::line(x-1,l::kZero-plot.out[i-1].last,x,l::kZero-c.first,out);
        ui::line(x,l::kZero-c.high,x,l::kZero-c.low,out);}
    ui::text(34,151,"0",m::ghost());ui::text(204,151,"16",m::ghost());right(386,151,"32ms",m::ghost());
    ui::text(14,170,"body motion",m::ghost());std::snprintf(text,sizeof text,"idle / %s",frame.cycle);right(386,170,text,m::ghost());
    ui::line(l::kLeft,plot.body,l::kRight,plot.body,palette::shadow());
    for(int i=1;i<l::kMotionPoints;++i)ui::line(l::kLeft+(i-1)*2,plot.motion[i-1],l::kLeft+i*2,plot.motion[i],out);
    m::footer(400,frame.mute?"muted / reset ref / not live":frame.bypass?"bypass / reset ref / not live":"reset reference / not live","1ms return");
    return true;
}
} // namespace seance::platform::ui3ds
