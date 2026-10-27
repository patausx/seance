#include "ui_module_focus_internal.h"
#include "ui_state.h"
#include "ui_sequencer.h"
#include "ui_theme.h"
#include "ui_param_lens.h"
#include "module_inspector.h"
#include "editor_style.h"
#include "module_heading.h"
#include "fold_wave_model.h"
#include "fold_editor_visual.h"
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
namespace model=fold_wave_model;
namespace l=fold_editor_visual;
using C=FoldUnit;
union Visual {model::Plot legacy;l::Plot modern;Visual():modern{}{};};
struct Frame {
    uint16_t mod_links=0;
    bool valid=false,modern=false,curve=false,bypass=false,mute=false,lock=false,lock_full=false,lock_armed=false,lens=false,ready=false;
    int8_t chain=0,slot=0,selected=0,route_first=0,route_last=UNITS_PER_CHAIN-1;
    q15 edit[C::P_COUNT]={},live[C::P_COUNT]={};
    char values[C::P_COUNT][18]={},reference_value[18]={};int16_t positions[C::P_COUNT][3]={};
    uint8_t kinds[UNITS_PER_CHAIN]={};bool bypassed[UNITS_PER_CHAIN]={};
    Visual plot;
} frame;
static_assert(sizeof(Frame)<4096&&std::is_trivially_copyable<Frame>::value,"bounded pointer-free Fold frame, no engine or DSP work in draw");
struct Gesture {
    Engine* engine=nullptr;uintptr_t unit=0;View view=View::Chains;
    int chain=-1,slot=-1,bank=-1,step=-1,lock_chain=-1,param=-1,selection=-1,start=0;
    bool locked=false,modern=false,curve=false,fine=false;q15 value=0;
} gesture;
int selected=C::P_DRIVE,bound_chain=-1,bound_slot=-1;
Engine* bound_engine=nullptr;uintptr_t bound_unit=0;
bool curve=false,bound_modern=false,fine_held=false,blocked_frame=false;
l::Model reference;
constexpr Drag DRAGS[C::P_COUNT]={Drag::FoldDrive,Drag::FoldTrim,Drag::FoldMode,Drag::FoldBias,Drag::FoldMix,Drag::FoldDrive,Drag::FoldDrive};
struct Control {int x,y,w,h,label_x,label_y,rail_x,rail_y,extent;};
constexpr Control CONTROLS[C::LEGACY_COUNT]={
    {8,27,146,46,14,32,14,64,128},{112,177,96,43,118,182,118,213,82},
    {164,27,150,46,174,32,186,48,106},{8,177,96,43,14,182,14,213,82},{216,177,96,43,222,182,222,213,82}
};
constexpr const char* NAMES[C::P_COUNT]={"drive","trim","morph","bias","mix","smooth","tone"};
bool capture_context(){return g_eng&&g_audio&&g_overlay==Overlay::Editor&&
    (g_view==View::Chains||(g_view==View::Sequencer&&g_seq_lock_armed))&&
    g_edit_chain>=0&&g_edit_chain<NUM_CHAINS&&g_edit_slot>=0&&g_edit_slot<UNITS_PER_CHAIN;}
bool context(){return !blocked_frame&&!param_lens_is_open()&&capture_context();}
bool lock_context(){return g_seq_lock_armed&&g_seq_lock_chain==g_edit_chain&&g_seq_lock_step>=0&&g_seq_lock_step<SEQ_STEPS;}
int bank(){const int b=sequencer_edit_bank();return b>=0&&b<NUM_PATTERNS?b:g_eng->active_pattern;}
int gesture_bank(){return g_seq_lock_armed?bank():g_eng->active_pattern;}
void release(){gesture={};for(auto d:DRAGS)if(g_drag==d){g_drag=Drag::None;break;}}
void bind(const C& u){
    if(bound_engine!=g_eng||bound_unit!=uintptr_t(&u)||bound_chain!=g_edit_chain||bound_slot!=g_edit_slot||bound_modern!=u.modern()){
        release();curve=false;selected=C::P_DRIVE;bound_engine=g_eng;bound_unit=uintptr_t(&u);
        bound_chain=g_edit_chain;bound_slot=g_edit_slot;bound_modern=u.modern();reference.invalidate();
    }
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
        else {if(count>=SEQ_LOCKS){g_seq_lock_flash=90;return;}
            step.locks[count]={uint8_t(g_edit_slot),uint8_t(p),value};step.lock_count=uint8_t(count+1);}
    }else {if(u.params[p].base==value)return;u.params[p].base=value;}
    mark_project_mutated();
}
void begin(C& u,int p,int x){
    if(p>=0)selected=p;
    gesture={};gesture.engine=g_eng;gesture.unit=uintptr_t(&u);gesture.view=g_view;
    gesture.chain=g_edit_chain;gesture.slot=g_edit_slot;gesture.bank=gesture_bank();
    gesture.step=g_seq_lock_step;gesture.lock_chain=g_seq_lock_chain;gesture.locked=g_seq_lock_armed;
    gesture.param=p;gesture.selection=selected;gesture.start=x;gesture.value=p>=0?edit_value(u,p):0;
    gesture.curve=curve;gesture.modern=u.modern();gesture.fine=fine_held;
    g_drag=p>=0?DRAGS[p]:Drag::FoldDrive;g_drag_param=p;
}
bool owns(const C& u){return gesture.engine==g_eng&&gesture.unit==uintptr_t(&u)&&gesture.view==g_view&&
    gesture.chain==g_edit_chain&&gesture.slot==g_edit_slot&&gesture.bank==gesture_bank()&&
    gesture.selection==selected&&gesture.locked==g_seq_lock_armed&&gesture.curve==curve&&gesture.fine==fine_held&&
    gesture.modern==u.modern()&&g_eng->chains[g_edit_chain].units[g_edit_slot]==&u&&
    (!gesture.locked||(lock_context()&&gesture.step==g_seq_lock_step&&gesture.lock_chain==g_seq_lock_chain));}
bool remove_name(int p,int x,int y,const char* name,int tx,int ty){
    if(!lock_context()||!hit(tx,ty,x-3,y-4,ui::text_w(name)+6,16))return false;
    selected=p;if(g_eng->remove_step_lock(g_seq_lock_chain,g_seq_lock_step,g_edit_slot,p,bank()))mark_project_mutated();return true;
}
u32 color(int p){return frame.selected==p?m::spot():m::ink();}
void right(int x,int y,const char* text,u32 c){ui::text(x-ui::text_w(text),y,text,c);}
void rail(int p,int left,int end,int y){module_fader::horizontal(left,end,y,
    frame.positions[p][0],frame.positions[p][1],{bool(frame.mod_links&(1u<<p)),false,frame.bypass||frame.mute},frame.positions[p][2]);}
void capture_route(){
    auto width=[](int first,int last){int total=0;for(int i=first;i<=last;++i)if(UnitKind(frame.kinds[i])!=UnitKind::Empty){
        if(total)total+=22;
        total+=ui::text_w(m::module_name(UnitKind(frame.kinds[i])));}return total;};
    while(width(frame.route_first,frame.route_last)>372){
        if(frame.slot-frame.route_first>=frame.route_last-frame.slot&&frame.route_first<frame.slot)++frame.route_first;
        else if(frame.route_last>frame.slot)--frame.route_last;else break;}
}
void capture_plot(){
    const q15* values=frame.lock?frame.edit:frame.live;
    if(frame.modern){l::Key key;key.bypass=frame.bypass;key.curve=frame.curve;
        std::copy(values,values+C::P_COUNT,key.values);reference.advance(key);frame.ready=reference.ready();
        if(frame.ready)frame.plot.modern=reference.plot;
    }else {
        static bool valid=false,bypass=false;static q15 previous[C::LEGACY_COUNT]={};static model::Plot cached;
        if(!valid||bypass!=frame.bypass||!std::equal(values,values+C::LEGACY_COUNT,previous)){
            model::plot(values,frame.bypass,cached);std::copy(values,values+C::LEGACY_COUNT,previous);valid=true;bypass=frame.bypass;}
        new(&frame.plot.legacy) model::Plot(cached);frame.ready=true;
    }
}
void waveform(){
    ui::line(14,84,24,84,m::ghost());ui::text(30,80,"test sine / .75fs",m::ghost());
    const u32 shape_color=frame.mute?m::ghost():m::spot();
    ui::line(236,84,250,84,shape_color);ui::text(256,80,frame.bypass?"byp dry":frame.mute?"muted":frame.lock?"lock mdl":"shaped",shape_color);
    for(int level=-1;level<=1;++level){const int y=model::ZERO-level*model::HEIGHT;
        ui::text(8,y-4,level>0?"+1":level<0?"-1":"0",m::ghost());
        if(level==0)ui::line(model::X0,y,model::X1,y,m::branch());else for(int x=model::X0;x<model::X1;x+=5)ui::rect(x,y,1,1,m::branch());}
    for(int col=1;col<model::COLUMNS;++col)ui::line(model::X0+col-1,frame.plot.legacy.source[col-1],model::X0+col,frame.plot.legacy.source[col],m::ghost());
    for(int col=0;col<model::COLUMNS;++col){const auto& point=frame.plot.legacy.shaped[col];const int x=model::X0+col;
        if(col)ui::line(x-1,frame.plot.legacy.shaped[col-1].last,x,point.first,shape_color,2);
        if(point.low!=point.high)ui::line(x,point.low,x,point.high,shape_color,2);}
}
void trace(const l::Column* points,u32 c){
    for(int col=0;col<l::kColumns;++col){const int x=l::kLeft+col;const auto& p=points[col];
        if(col)ui::line(x-1,l::kZero-points[col-1].last,x,l::kZero-p.first,c);
        ui::line(x,l::kZero-p.high,x,l::kZero-p.low,c);
        if(p.clipped&1)ui::line(x,74,x,76,c);
        if(p.clipped&2)ui::line(x,188,x,190,c);}
}
} // namespace
int fold_focus_selected(){return selected;}
void fold_focus_select(int p){if(p>=0&&p<C::P_COUNT){if(p!=selected)release();selected=p;}}
void fold_focus_release(){release();}
bool fold_focus_modern(){return frame.valid&&frame.modern;}
void fold_focus_value_text(char* out,size_t size,int p,q15 value){l::value_text(out,size,p,value,fold_focus_modern());}
void fold_focus_input(u32 down,u32 held,bool blocked){
    const bool fine=(held&KEY_R)!=0;if(fine!=fine_held)release();fine_held=fine;blocked_frame=blocked;
    if(blocked||(down&~u32(KEY_TOUCH|KEY_R|KEY_ZL)))release();
}
bool fold_focus_touch_body(C& u,int tx,int ty){
    if(!context()){release();return false;}
    platform::AudioOut::Guard guard(*g_audio);if(g_eng->chains[g_edit_chain].units[g_edit_slot]!=&u)return false;
    bind(u);release();
    if(u.modern()&&hit(tx,ty,198,0,48,26)){curve=!curve;return true;}
    if(!u.modern()&&hit(tx,ty,164,0,76,26)){if(!g_seq_lock_armed)begin(u,-2,tx);return true;}
    if(ty<26){if(!g_seq_lock_armed&&hit(tx,ty,246,0,38,25)){u.bypass=!u.bypass;mark_project_mutated();}return true;}
    if(g_seq_lock_armed&&!lock_context())return true;
    if(u.modern()){
        for(int i=0;i<3;++i)if(hit(tx,ty,8+i*48,114,46,24)){selected=C::P_MODE;set_value(u,C::P_MODE,q15(i==0?0:i==1?16384:32767));return true;}
        if(hit(tx,ty,268,114,44,24)){selected=C::P_BIAS;set_value(u,C::P_BIAS,0);return true;}
        for(const auto& c:l::controls)if(hit(tx,ty,c.x0,c.y0,c.x1-c.x0,c.y1-c.y0)){
            selected=c.p;if(remove_name(c.p,c.left,c.y,c.name,tx,ty))return true;begin(u,c.p,tx);return true;}
    }else for(int p=0;p<C::LEGACY_COUNT;++p){const auto& c=CONTROLS[p];if(!hit(tx,ty,c.x,c.y,c.w,c.h))continue;
        selected=p;if(remove_name(p,c.label_x,c.label_y,p==C::P_MODE?"mode":NAMES[p],tx,ty))return true;begin(u,p,tx);return true;}
    return true;
}
void fold_focus_drag(C& u,int tx,int ty){
    if(gesture.param==-1)return;
    if(!context()){release();return;}
    platform::AudioOut::Guard guard(*g_audio);if(!owns(u)){release();return;}
    if(gesture.param==-2){if(!hit(tx,ty,164,0,76,26))release();return;}
    if(g_drag!=DRAGS[gesture.param]){release();return;}
    const int p=gesture.param,delta=tx-gesture.start,range=p==C::P_BIAS?65534:32767;
    q15 next;
    if(u.modern()&&p==C::P_DRIVE){
        next=delta==0?gesture.value:l::drive_raw(l::drive_fraction(gesture.value)+double(delta)/(296*(gesture.fine?16:1)));
        if((gesture.value<0&&next>0)||(gesture.value>0&&next<0)){
            const double distance=std::abs(l::drive_fraction(next)-.4);if(distance<1.0/(296*(gesture.fine?16:1)))next=0;}
    }else {constexpr int extent=296;
        const int movement=gesture.fine?(p==C::P_BIAS?delta:delta*range/(extent*16)):delta*range/extent;
        int raw=l::clamp(int(gesture.value)+movement,p,u.modern());
        if(p==C::P_BIAS&&((gesture.value<0&&raw>0)||(gesture.value>0&&raw<0))&&std::abs(raw)<(gesture.fine?1:range/extent))raw=0;
        next=q15(raw);}
    // Stationary touchdown is inert. A moved drag may deliberately return to an
    // explicit zero lock, including when the underlying base is already zero.
    if(delta==0&&edit_value(u,p)==gesture.value)return;
    set_value(u,p,next);
}
void fold_focus_touch_release(){
    if(gesture.param!=-2){release();return;}
    if(!context()){release();return;}
    platform::AudioOut::Guard guard(*g_audio);Unit* base=g_eng->chains[g_edit_chain].units[g_edit_slot];
    if(!base||base->kind!=UnitKind::Fold){release();return;}auto& u=*static_cast<C*>(base);
    if(owns(u)&&!g_seq_lock_armed&&!u.modern()){
        C defaults(true);for(int p=C::LEGACY_COUNT;p<C::P_COUNT;++p)u.params[p]=defaults.params[p];
        for(auto& scene:g_eng->scenes[g_edit_chain])if(scene.kind[g_edit_slot]==UnitKind::Fold)
            for(int p=C::LEGACY_COUNT;p<C::P_COUNT;++p)scene.base[g_edit_slot][p]=defaults.params[p].base;
        u.set_modern(true);mark_project_mutated();bind(u);
    }
    release();
}
void fold_focus_capture(){
    frame={};if(!capture_context()){release();bound_engine=nullptr;bound_unit=0;curve=false;selected=C::P_DRIVE;reference.invalidate();return;}
    {
        platform::AudioOut::Guard guard(*g_audio);const auto& chain=g_eng->chains[g_edit_chain];const Unit* unit=chain.units[g_edit_slot];
        if(!unit||unit->kind!=UnitKind::Fold){release();bound_engine=nullptr;bound_unit=0;curve=false;selected=C::P_DRIVE;reference.invalidate();return;}
        const auto& u=*static_cast<const C*>(unit);bind(u);if(gesture.param!=-1&&!owns(u))release();
        if(selected>=u.num_params){selected=C::P_DRIVE;release();}
        frame.valid=true;frame.modern=u.modern();frame.curve=curve;frame.bypass=u.bypass;frame.mute=chain.mute;
        frame.lock_armed=g_seq_lock_armed;frame.lens=param_lens_is_open();frame.lock=lock_context();
        frame.lock_full=frame.lock&&g_eng->sequence_pattern(bank()).steps[g_edit_chain][g_seq_lock_step].lock_count>=SEQ_LOCKS;
        frame.chain=g_edit_chain;frame.slot=g_edit_slot;frame.selected=selected;frame.mod_links=focus_mod_links(u);
        for(int i=0;i<UNITS_PER_CHAIN;++i){frame.kinds[i]=chain.units[i]?uint8_t(chain.units[i]->kind):uint8_t(UnitKind::Empty);frame.bypassed[i]=chain.units[i]&&chain.units[i]->bypass;}
        for(int p=0;p<u.num_params;++p){frame.edit[p]=edit_value(u,p);frame.live[p]=u.modern()?u.control_value(p):u.params[p].effective;}
    }
    for(int p=0;p<(frame.modern?C::P_COUNT:C::LEGACY_COUNT);++p){
        l::value_text(frame.values[p],sizeof frame.values[p],p,frame.edit[p],frame.modern);
        int left=0,end=0;
        if(frame.modern){for(const auto& c:l::controls)if(c.p==p){left=c.left+1;end=c.right-1;break;}}
        else {left=CONTROLS[p].rail_x;end=left+CONTROLS[p].extent;}
        frame.positions[p][0]=l::position(frame.edit[p],p,left,end,frame.modern);
        frame.positions[p][1]=l::position(frame.live[p],p,left,end,frame.modern);
        frame.positions[p][2]=(p==C::P_BIAS||(frame.modern&&p==C::P_DRIVE))?l::position(0,p,left,end,frame.modern):p==C::P_MODE?l::position(16384,p,left,end,frame.modern):-1;
    }
    l::value_text(frame.reference_value,sizeof frame.reference_value,frame.selected,
        (frame.lock?frame.edit:frame.live)[frame.selected],frame.modern);
    capture_route();if(!frame.lens)capture_plot(); // outside guard; never model work under MOD or in draw
}
void fold_focus_draw(const C&){
    m::paper(320);if(!frame.valid)return;module_heading::header(UnitKind::Fold,m::spot());
    char text[24];std::snprintf(text,sizeof text,"ch%02d / %02d",frame.chain+1,frame.slot+1);ui::text(62,m::kHeaderTextY,text,m::ghost());
    if(frame.modern)ui::text(206,m::kHeaderTextY,frame.curve?"curve":"wave",m::spot());
    else if(!frame.lock_armed)ui::text(170,m::kHeaderTextY,"extend",m::spot());
    ui::text(258,m::kHeaderTextY,"byp",frame.bypass?m::spot():m::ghost());right(m::right_edge(320),m::kHeaderTextY,"x",m::ghost());
    if(frame.bypass)ui::line(258,m::kHeaderMarkY,275,m::kHeaderMarkY,m::spot());
    if(frame.lens){m::footer(320,"b back","l: mod >");return;}
    if(frame.modern){
        for(const auto& c:l::controls){ui::text(c.left,c.y,c.name,frame.selected==c.p?m::spot():m::ghost());right(c.right,c.y,frame.values[c.p],color(c.p));rail(c.p,c.left+1,c.right-1,c.rail);}
        const q15 mode=frame.edit[C::P_MODE];
        ui::text(14,122,"fold",mode==0?m::spot():m::ghost());ui::text(62,122,"wrap",mode==16384?m::spot():m::ghost());ui::text(110,122,"diode",mode==32767?m::spot():m::ghost());
        ui::text(172,122,"r: fine",m::ghost());right(306,122,"zero",frame.edit[C::P_BIAS]==0?m::spot():m::ghost());
    }else {
        ui::text(14,32,"drive",color(C::P_DRIVE));ui::text(14,48,frame.values[C::P_DRIVE],color(C::P_DRIVE));rail(C::P_DRIVE,14,142,64);
        ui::text(174,32,"mode",color(C::P_MODE));right(306,32,frame.values[C::P_MODE],color(C::P_MODE));rail(C::P_MODE,186,292,48);
        const q15 mode=frame.edit[C::P_MODE];ui::text(174,60,"fold",mode==0?m::spot():m::ghost());ui::text(227,60,"wrap",mode==16384?m::spot():m::ghost());ui::text(278,60,"diode",mode==32767?m::spot():m::ghost());
        waveform();for(int p:{C::P_BIAS,C::P_TRIM,C::P_MIX}){const auto& c=CONTROLS[p];ui::text(c.label_x,182,NAMES[p],frame.selected==p?m::spot():m::ghost());ui::text(c.label_x,196,frame.values[p],color(p));rail(p,c.rail_x,c.rail_x+c.extent,c.rail_y);}ui::line(14,223,306,223,m::branch());
    }
    if(frame.lock)m::footer(320,frame.lock_full?"lock full":"p-lock / name: remove","b back");else m::footer(320,"b back","l: mod >");
}
bool fold_focus_draw_top(){
    if(!frame.valid||!frame.modern||frame.lens)return false;
    m::paper(400);module_heading::header(UnitKind::Fold,m::spot());char text[64];
    std::snprintf(text,sizeof text,"ch%02d / %02d",frame.chain+1,frame.slot+1);right(386,m::kHeaderTextY,text,m::ghost());
    int rx=14;for(int i=frame.route_first;i<=frame.route_last;++i){const auto kind=UnitKind(frame.kinds[i]);if(kind==UnitKind::Empty)continue;
        if(rx>14){ui::text(rx,module_heading::kRouteY,"/",m::ghost());rx+=14;}const char* name=m::module_name(kind);const int w=ui::text_w(name);
        ui::text(rx,module_heading::kRouteY,name,i==frame.slot?m::spot():m::ghost());if(i==frame.slot)ui::line(rx,module_heading::kRouteUnderlineY,rx+w-2,module_heading::kRouteUnderlineY,m::spot());
        if(frame.bypassed[i])ui::line(rx,module_heading::kRouteCrossY,rx+w-2,module_heading::kRouteCrossY,m::ghost());
        rx+=w+8;}
    std::snprintf(text,sizeof text,"%s %s / mod %s",NAMES[frame.selected],frame.reference_value,frame.mod_links&(1u<<frame.selected)?"on":"off");
    if(rx+8+ui::text_w(text)<=386)right(386,module_heading::kRouteY,text,m::ink());
    const u32 out=frame.mute?m::ghost():m::spot();
    if(frame.curve){ui::text(14,55,"transfer",out);right(386,55,"drive + bias + smooth",m::ghost());}
    else {ui::line(14,59,26,59,m::ghost());ui::text(32,55,"in",m::ghost());ui::line(62,59,74,59,out);ui::text(80,55,frame.bypass?"dry":"out",out);right(386,55,"wave / 250hz .75fs",m::ghost());}
    for(int level:{1,0,-1}){const int y=l::kZero-level*l::kHeight;ui::text(14,y-4,level>0?"+1":level<0?"-1":"0",m::ghost());
        if(!level)ui::line(34,y,386,y,palette::shadow());else for(int x=34;x<=386;x+=5)ui::rect(x,y,1,1,palette::shadow());}
    for(int x:{34,210,386})for(int y=77;y<=187;y+=5)ui::rect(x,y,1,1,palette::shadow());
    if(frame.ready){trace(frame.plot.modern.source,m::ghost());trace(frame.plot.modern.output,out);}else ui::text(176,128,"updating",m::ghost());
    ui::text(34,198,frame.curve?"-1":"0",m::ghost());ui::text(207,198,frame.curve?"0":"4",m::ghost());right(386,198,frame.curve?"+1 in":"8ms",m::ghost());
    ui::text(14,214,frame.curve?"before dc / tone / trim / mix":"post dc / tone / trim / mix",m::ghost());
    m::footer(400,frame.lock?"lock ref / not live":frame.mute?"muted ref / not live":frame.bypass?"byp ref / not live":"reset ref / not live","+/-1fs");return true;
}
} // namespace seance::platform::ui3ds
