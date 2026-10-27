#include "ui_module_focus_internal.h"
#include "ui_state.h"
#include "ui_sequencer.h"
#include "ui_theme.h"
#include "ui_param_lens.h"
#include "editor_style.h"
#include "module_heading.h"
#include "tilt_editor_visual.h"
#include "../../core/engine/tilt_edit.h"
#include "audio_out.h"
#include "../../core/ui/draw.h"
#include <algorithm>
#include <cstdio>
#include <type_traits>

namespace seance::platform::ui3ds {
namespace {
namespace m=editor_style;
namespace l=tilt_editor_visual;
using C=TiltUnit;
struct Frame {
    uint16_t mod_links=0;
    bool valid=false,bypass=false,mute=false,lock=false,lock_full=false,lock_armed=false,lens=false,changed=false;
    C::Revision revision=C::Revision::Extended;
    int8_t chain=0,slot=0,selected=0,route_first=0,route_last=UNITS_PER_CHAIN-1;
    q15 base[6]={},edit[6]={},live[6]={};char values[6][18]={},summary[44]={};
    uint8_t kinds[UNITS_PER_CHAIN]={};bool bypassed[UNITS_PER_CHAIN]={};
    l::Plot base_plot,plot;
} frame;
static_assert(sizeof(Frame)<1024&&std::is_trivially_copyable<Frame>::value,"bounded pointer-free Tilt; no model or engine work in draw");
struct Gesture {
    Engine* engine=nullptr;uintptr_t unit=0;View view=View::Chains;
    int chain=-1,slot=-1,bank=-1,step=-1,lock_chain=-1,param=-1,start=0;
    bool locked=false,fine=false;C::Revision revision=C::Revision::Extended;q15 value=0;
} gesture;
struct Key {q15 edit[6]={},values[6]={};C::Revision revision=C::Revision::Extended;bool bypass=false;};
Key cached_key;l::Plot cached_plot,cached_base;bool cache_valid=false;
int selected=C::P_TILT,bound_chain=-1,bound_slot=-1;
Engine* bound_engine=nullptr;uintptr_t bound_unit=0;C::Revision bound_revision=C::Revision::Extended;
bool fine_held=false,blocked_frame=false;
constexpr Drag DRAGS[6]={Drag::TiltBalance,Drag::TiltPivot,Drag::TiltDrive,Drag::TiltMix,Drag::TiltRange,Drag::TiltModDepth};
struct Control {int left,right,y,rail;const char* name;};
constexpr Control CONTROLS[6]={{14,306,39,65,"tilt"},{14,306,99,119,"pivot"},
    {172,306,154,171,"drive"},{172,306,196,213,"mix"},{14,148,154,171,"range"},{14,148,196,213,"wet trim"}};
bool capture_context(){return g_eng&&g_audio&&g_overlay==Overlay::Editor&&
    (g_view==View::Chains||(g_view==View::Sequencer&&g_seq_lock_armed))&&
    g_edit_chain>=0&&g_edit_chain<NUM_CHAINS&&g_edit_slot>=0&&g_edit_slot<UNITS_PER_CHAIN;}
bool context(){return !blocked_frame&&!param_lens_is_open()&&capture_context();}
bool lock_context(){return g_seq_lock_armed&&g_seq_lock_chain==g_edit_chain&&g_seq_lock_step>=0&&g_seq_lock_step<SEQ_STEPS;}
int bank(){const int b=sequencer_edit_bank();return b>=0&&b<NUM_PATTERNS?b:g_eng->active_pattern;}
int gesture_bank(){return g_seq_lock_armed?bank():g_eng->active_pattern;}
void release(){gesture={};for(auto drag:DRAGS)if(g_drag==drag)g_drag=Drag::None;}
void bind(const C& u){
    if(bound_engine!=g_eng||bound_unit!=uintptr_t(&u)||bound_chain!=g_edit_chain||bound_slot!=g_edit_slot||bound_revision!=u.revision()){
        release();selected=C::P_TILT;bound_engine=g_eng;bound_unit=uintptr_t(&u);bound_chain=g_edit_chain;bound_slot=g_edit_slot;
        bound_revision=u.revision();cache_valid=false;}
}
q15 edit_value(const C& u,int p){
    if(lock_context()){const auto& step=g_eng->sequence_pattern(bank()).steps[g_edit_chain][g_seq_lock_step];
        for(int i=0;i<std::min(int(step.lock_count),SEQ_LOCKS);++i)
            if(step.locks[i].slot==g_edit_slot&&step.locks[i].param==p)return step.locks[i].value;}
    return u.params[p].base;
}
void set_value(C& u,int p,q15 value){
    if((g_seq_lock_armed&&!lock_context())||p<0||p>=u.num_params)return;
    if(g_seq_lock_armed){auto& step=g_eng->sequence_pattern(bank()).steps[g_edit_chain][g_seq_lock_step];
        const int count=std::min(int(step.lock_count),SEQ_LOCKS);int found=-1;
        for(int i=0;i<count;++i)if(step.locks[i].slot==g_edit_slot&&step.locks[i].param==p){found=i;break;}
        if(found>=0){if(step.locks[found].value==value)return;step.locks[found].value=value;}
        else {if(count>=SEQ_LOCKS){g_seq_lock_flash=90;return;}step.locks[count]={uint8_t(g_edit_slot),uint8_t(p),value};step.lock_count=uint8_t(count+1);}
    }else {if(u.params[p].base==value)return;u.params[p].base=value;}
    mark_project_mutated();cache_valid=false;
}
int clamp(int value,int p){return std::max(p==C::P_TILT?-32767:0,std::min(32767,value));}
int position(q15 value,int p,int left,int right){const int low=p==C::P_TILT?-32767:0;return left+(clamp(value,p)-low)*(right-left)/(32767-low);}
void begin(C& u,int p,int x){
    selected=p;gesture={};gesture.engine=g_eng;gesture.unit=uintptr_t(&u);gesture.view=g_view;gesture.chain=g_edit_chain;gesture.slot=g_edit_slot;
    gesture.bank=gesture_bank();gesture.step=g_seq_lock_step;gesture.lock_chain=g_seq_lock_chain;gesture.locked=g_seq_lock_armed;
    gesture.param=p;gesture.start=x;gesture.value=edit_value(u,p);gesture.revision=u.revision();gesture.fine=fine_held;g_drag=DRAGS[p];g_drag_param=p;
}
bool owns(const C& u){return gesture.engine==g_eng&&gesture.unit==uintptr_t(&u)&&gesture.view==g_view&&gesture.chain==g_edit_chain&&
    gesture.slot==g_edit_slot&&gesture.bank==gesture_bank()&&gesture.param==selected&&gesture.param==g_drag_param&&gesture.locked==g_seq_lock_armed&&
    gesture.fine==fine_held&&gesture.revision==u.revision()&&g_eng->chains[g_edit_chain].units[g_edit_slot]==&u&&
    (!gesture.locked||(lock_context()&&gesture.step==g_seq_lock_step&&gesture.lock_chain==g_seq_lock_chain));}
void capture_route(){
    const auto width=[](int first,int last){int total=0;for(int i=first;i<=last;++i)if(UnitKind(frame.kinds[i])!=UnitKind::Empty){
        if(total)total+=22;
        total+=ui::text_w(m::module_name(UnitKind(frame.kinds[i])));}return total;};
    while(width(frame.route_first,frame.route_last)>372){
        if(frame.slot-frame.route_first>=frame.route_last-frame.slot&&frame.route_first<frame.slot)++frame.route_first;
        else if(frame.route_last>frame.slot)--frame.route_last;else break;}
}
void capture_plot(){
    Key key;key.revision=frame.revision;key.bypass=frame.bypass;std::copy(frame.edit,frame.edit+6,key.edit);
    const q15* values=frame.lock?frame.edit:frame.live;std::copy(values,values+6,key.values);
    frame.changed=!frame.bypass&&!std::equal(key.edit,key.edit+6,key.values);
    if(!cache_valid||key.revision!=cached_key.revision||key.bypass!=cached_key.bypass||
       !std::equal(key.edit,key.edit+6,cached_key.edit)||!std::equal(key.values,key.values+6,cached_key.values)){
        l::calculate(key.values,key.revision,key.bypass,cached_plot);
        if(frame.changed)l::calculate(key.edit,key.revision,key.bypass,cached_base);else cached_base=cached_plot;
        cached_key=key;cache_valid=true;}
    frame.plot=cached_plot;frame.base_plot=cached_base;
    char drive[18],mix[18];tilt_readout::value_text(drive,sizeof drive,C::P_DRIVE,values[C::P_DRIVE],frame.revision,values[C::P_RANGE]);
    tilt_readout::value_text(mix,sizeof mix,C::P_MIX,values[C::P_MIX],frame.revision,values[C::P_RANGE]);
    std::snprintf(frame.summary,sizeof frame.summary,"%s / wet %s",drive,mix);
}
void right(int x,int y,const char* text,u32 c){ui::text(x-ui::text_w(text),y,text,c);}
void control(int p){
    const auto& c=CONTROLS[p];const bool disabled=p==C::P_TRIM&&frame.revision==C::Revision::Legacy;
    ui::text(c.left,c.y,c.name,frame.selected==p?m::spot():m::ghost());
    right(p==C::P_TILT?252:c.right,c.y,frame.values[p],disabled?m::ghost():frame.selected==p?m::spot():m::ink());
    if(disabled){module_fader::horizontal_track(c.left,c.right,c.rail);return;}
    module_fader::horizontal(c.left,c.right,c.rail,position(frame.edit[p],p,c.left,c.right),position(frame.live[p],p,c.left,c.right),
        {bool(frame.mod_links&(1u<<p)),false,frame.bypass||frame.mute},p==C::P_TILT?position(0,p,c.left,c.right):p==C::P_TRIM?position(tilt_eq::TRIM_UNITY,p,c.left,c.right):-1);
}
void trace(const l::Plot& plot,u32 color,int width){for(int i=1;i<l::POINTS;++i)ui::line(l::X0+(i-1)*2,plot.y[i-1],l::X0+i*2,plot.y[i],color,width);}
}
int tilt_focus_selected(){return selected;}
void tilt_focus_select(int p){if(p>=0&&p<6){if(selected!=p)release();selected=p;}}
void tilt_focus_release(){release();}
void tilt_focus_input(u32 down,u32 held,bool blocked){
    const bool fine=(held&KEY_R)!=0;if(fine!=fine_held)release();fine_held=fine;blocked_frame=blocked;
    if(blocked||(down&~u32(KEY_TOUCH|KEY_R|KEY_ZL)))release();
}
void tilt_focus_value_text(char* out,size_t size,int p,q15 value,int law){
    const q15 range=law==2?frame.base[C::P_RANGE]:law==1?frame.edit[C::P_RANGE]:frame.live[C::P_RANGE];
    tilt_readout::value_text(out,size,p,value,frame.revision,range);
}
bool tilt_focus_touch_body(C& u,int tx,int ty){
    if(!context()){release();return false;}
    platform::AudioOut::Guard guard(*g_audio);if(g_eng->chains[g_edit_chain].units[g_edit_slot]!=&u)return false;
    bind(u);release();
    if(!u.extended()&&hit(tx,ty,164,0,76,26)){if(!g_seq_lock_armed&&tilt_edit::extend(*g_eng,g_edit_chain,g_edit_slot)){mark_project_mutated();bind(u);}return true;}
    if(ty<26){if(!g_seq_lock_armed&&hit(tx,ty,246,0,38,25)){u.bypass=!u.bypass;mark_project_mutated();}return true;}
    if(g_seq_lock_armed&&!lock_context())return true;
    if(hit(tx,ty,272,28,40,27)){selected=C::P_TILT;set_value(u,C::P_TILT,0);return true;}
    for(int p=0;p<u.num_params;++p){const auto& c=CONTROLS[p];
        const bool inside=p==C::P_TILT?(hit(tx,ty,8,28,260,27)||hit(tx,ty,8,55,304,33)):
            p==C::P_FREQ?hit(tx,ty,8,91,304,52):hit(tx,ty,c.left-6,c.y-8,c.right-c.left+12,35);
        if(!inside)continue;
        selected=p;
        if(lock_context()&&hit(tx,ty,c.left-3,c.y-4,ui::text_w(c.name)+6,16)){
            if(g_eng->remove_step_lock(g_seq_lock_chain,g_seq_lock_step,g_edit_slot,p,bank()))mark_project_mutated();
            cache_valid=false;return true;}
        begin(u,p,tx);return true;
    }
    return true;
}
void tilt_focus_drag(C& u,int tx,int){
    if(gesture.param<0)return;
    if(!context()||g_drag!=DRAGS[gesture.param]){release();return;}
    platform::AudioOut::Guard guard(*g_audio);if(!owns(u)){release();return;}
    const int p=gesture.param,range=p==C::P_TILT?65534:32767,extent=296*(gesture.fine?16:1);
    int next=clamp(int(gesture.value)+(tx-gesture.start)*range/extent,p);
    const int neutral=p==C::P_TILT?0:p==C::P_TRIM?tilt_eq::TRIM_UNITY:-1;
    if(neutral>=0&&((gesture.value<neutral&&next>neutral)||(gesture.value>neutral&&next<neutral))&&std::abs(next-neutral)<std::max(1,range/extent))next=neutral;
    set_value(u,p,q15(next));
}
void tilt_focus_capture(){
    frame={};if(!capture_context()){release();bound_engine=nullptr;bound_unit=0;cache_valid=false;return;}
    {platform::AudioOut::Guard guard(*g_audio);const auto& chain=g_eng->chains[g_edit_chain];const Unit* unit=chain.units[g_edit_slot];
        if(!unit||unit->kind!=UnitKind::Tilt){release();bound_engine=nullptr;bound_unit=0;cache_valid=false;return;}
        const auto& u=*static_cast<const C*>(unit);bind(u);if(gesture.param>=0&&!owns(u))release();if(selected>=u.num_params){selected=C::P_TILT;release();}
        frame.valid=true;frame.revision=u.revision();frame.bypass=u.bypass;frame.mute=chain.mute;frame.lock_armed=g_seq_lock_armed;frame.lens=param_lens_is_open();
        frame.lock=lock_context();frame.lock_full=frame.lock&&g_eng->sequence_pattern(bank()).steps[g_edit_chain][g_seq_lock_step].lock_count>=SEQ_LOCKS;
        frame.chain=g_edit_chain;frame.slot=g_edit_slot;frame.selected=selected;frame.mod_links=focus_mod_links(u);
        for(int i=0;i<UNITS_PER_CHAIN;++i){frame.kinds[i]=chain.units[i]?uint8_t(chain.units[i]->kind):uint8_t(UnitKind::Empty);frame.bypassed[i]=chain.units[i]&&chain.units[i]->bypass;}
        for(int p=0;p<6;++p){frame.base[p]=p<u.num_params?u.params[p].base:tilt_eq::TRIM_UNITY;
            frame.edit[p]=p<u.num_params?edit_value(u,p):tilt_eq::TRIM_UNITY;frame.live[p]=p<u.num_params?u.control_value(p):tilt_eq::TRIM_UNITY;}
    }
    for(int p=0;p<6;++p)tilt_readout::value_text(frame.values[p],sizeof frame.values[p],p,frame.edit[p],frame.revision,frame.edit[C::P_RANGE]);
    capture_route();if(!frame.lens)capture_plot(); // coefficient/complex/log math outside guard and draw
}
void tilt_focus_draw(const C&){
    m::paper(320);if(!frame.valid)return;module_heading::header(UnitKind::Tilt,m::spot());
    char text[24];std::snprintf(text,sizeof text,"ch%02d / %02d",frame.chain+1,frame.slot+1);ui::text(56,m::kHeaderTextY,text,m::ghost());
    if(frame.revision!=C::Revision::Extended&&!frame.lock_armed)ui::text(170,m::kHeaderTextY,"extend",m::spot());
    ui::text(258,m::kHeaderTextY,"byp",frame.bypass?m::spot():m::ghost());right(m::right_edge(320),m::kHeaderTextY,"x",m::ghost());
    if(frame.bypass)ui::line(258,m::kHeaderMarkY,275,m::kHeaderMarkY,m::spot());
    if(frame.lens){m::footer(320,"b back","l: mod >");return;}
    for(int p=0;p<6;++p)control(p);
    const auto zero=frame.edit[C::P_TILT]==0?m::spot():m::ghost();right(306,39,"zero",zero);ui::line(283,51,305,51,zero);
    ui::text(14,78,"body",m::ghost());right(306,78,"air",m::ghost());
    ui::text(14,131,frame.revision==C::Revision::Legacy&&frame.edit[C::P_RANGE]==0?"old linear pivot":"125hz",m::ghost());
    if(frame.revision!=C::Revision::Legacy||frame.edit[C::P_RANGE]!=0)right(306,131,"8khz",m::ghost());
    if(frame.lock)m::footer(320,frame.lock_full?"lock full":"p-lock / name: remove","b back");
    else {m::footer(320,"b back","l: mod >");ui::text(142,m::kTextY,"r fine",m::ghost());}
}
bool tilt_focus_draw_top(){
    if(!frame.valid||frame.lens)return false;
    m::paper(400);module_heading::header(UnitKind::Tilt,m::spot());char text[48];
    std::snprintf(text,sizeof text,"ch%02d / %02d",frame.chain+1,frame.slot+1);right(386,m::kHeaderTextY,text,m::ghost());
    int rx=14;for(int i=frame.route_first;i<=frame.route_last;++i){const auto kind=UnitKind(frame.kinds[i]);if(kind==UnitKind::Empty)continue;
        if(rx>14){ui::text(rx,module_heading::kRouteY,"/",m::ghost());rx+=14;}const char* name=m::module_name(kind);const int w=ui::text_w(name);
        ui::text(rx,module_heading::kRouteY,name,i==frame.slot?m::spot():m::ghost());
        if(i==frame.slot)ui::line(rx,module_heading::kRouteUnderlineY,rx+w-2,module_heading::kRouteUnderlineY,m::spot());
        if(frame.bypassed[i])ui::line(rx,module_heading::kRouteCrossY,rx+w-2,module_heading::kRouteCrossY,m::ghost());
        rx+=w+8;}
    if(rx+82<=386)right(386,module_heading::kRouteY,"body / air",m::ghost());
    const auto accent=frame.mute?m::ghost():m::spot();
    const char* label=frame.bypass?"bypass / dry":frame.lock?"lock eq":frame.mute?"muted eq":frame.revision==C::Revision::Legacy?"legacy eq":frame.revision==C::Revision::Clear?"clear eq":"eq response";
    ui::text(14,55,label,accent);right(386,55,frame.summary,m::ghost());
    for(int db:{12,0,-12}){const int y=l::response_y(db);right(32,y-4,db>0?"+12":db<0?"-12":"0",m::ghost());
        if(!db)ui::line(l::X0,y,l::X1,y,palette::shadow());else for(int x=l::X0;x<=l::X1;x+=6)ui::rect(x,y,1,1,palette::shadow());}
    for(int y=l::Y0;y<=l::Y1;y+=4)ui::rect(frame.plot.pivot_x,y,1,1,palette::shadow());
    if(frame.changed)trace(frame.base_plot,m::ghost(),1);
    trace(frame.plot,accent,2);
    const int x=frame.plot.pivot_x,y=frame.plot.pivot_y;
    ui::line(x,y-3,x+3,y,accent);ui::line(x+3,y,x,y+3,accent);ui::line(x,y+3,x-3,y,accent);ui::line(x-3,y,x,y-3,accent);
    const int freqs[]={20,100,1000,16000};const char* labels[]={"20","100","1k","16k"};
    for(int i=0;i<4;++i){const int tick=l::frequency_x(freqs[i]);ui::line(tick,183,tick,186,m::ghost());
        ui::text(tick-(i==0?0:i==3?ui::text_w(labels[i]):ui::text_w(labels[i])/2),190,labels[i],m::ghost());}
    if(frame.plot.over){ui::line(378,79,381,76,accent);ui::line(381,76,384,79,accent);}if(frame.plot.under){ui::line(378,173,381,176,accent);ui::line(381,176,384,173,accent);}
    std::snprintf(text,sizeof text,"low %s",frame.plot.low);ui::text(14,210,text,frame.mute?m::ghost():m::ink());
    std::snprintf(text,sizeof text,"pivot %s",frame.plot.pivot);ui::text(200-ui::text_w(text)/2,210,text,m::ghost());
    std::snprintf(text,sizeof text,"high %s",frame.plot.high);right(386,210,text,frame.mute?m::ghost():m::ink());
    m::footer(400,frame.lock?"lock draft / small signal":frame.mute?"muted / hypothetical eq":frame.changed?"base thin / effective eq":"eq model / not live audio","+/-12db");return true;
}
}
