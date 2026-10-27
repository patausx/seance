#include "ui_module_focus_internal.h"
#include "ui_state.h"
#include "ui_sequencer.h"
#include "ui_theme.h"
#include "ui_param_lens.h"
#include "module_inspector.h"
#include "editor_style.h"
#include "module_heading.h"
#include "ring_readout.h"
#include "ring_editor_visual.h"
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
namespace model=ring_reference;
namespace l=ring_editor_visual;
using C=RingUnit;
union Visual {model::Plot legacy;l::Plot modern;Visual():modern{}{}};
struct Frame {
    uint16_t mod_links=0;
    bool valid=false,lens=false,modern=false,slow=false,ready=false,bypass=false,mute=false,lock=false,lock_full=false;
    int8_t chain=0,slot=0,selected=0;uint8_t progress=0;
    q15 edit[C::P_COUNT]={},live[C::P_COUNT]={};char values[C::P_COUNT][18]={},base[18]={};
    uint8_t kinds[UNITS_PER_CHAIN]={};bool bypassed[UNITS_PER_CHAIN]={};Visual plot;
} frame;
static_assert(sizeof(Frame)<5120&&std::is_trivially_copyable<Frame>::value,"bounded pointer-free Ring frame; no engine/PCM in draw");
struct Gesture {
    Engine* engine=nullptr;uintptr_t unit=0;View view=View::Chains;
    int chain=-1,slot=-1,bank=-1,step=-1,lock_chain=-1,param=-1,x=0,y=0,axis=0;
    bool locked=false,modern=false,slow=false,note=false;u32 modifiers=0;q15 value=0,fine=0;
} gesture;
int selected=C::P_FREQ,bound_chain=-1,bound_slot=-1;Engine* bound_engine=nullptr;
uintptr_t bound_unit=0;bool bound_modern=false,slow=false;u32 modifiers=0;
l::Model modern_model;
model::Plot legacy_plot;bool legacy_valid=false,legacy_bypass=false;q15 legacy_key[C::LEGACY_COUNT]={};
struct Control {int x,y,w,h,label_x,label_y,rail_y,extent;const char* name;};
constexpr Control CONTROLS[6]={
    {8,27,146,46,14,32,65,128,"freq"},{168,27,146,46,174,32,65,128,"depth"},
    {8,177,72,44,14,182,214,62,"fdbk"},{84,177,72,44,90,182,214,62,"diode"},
    {236,177,72,44,242,182,214,62,"mix"},{160,177,72,44,166,182,214,62,"follow"}};
constexpr int params[4][2]={{C::P_FREQ,C::P_DEPTH},{C::P_FEEDBACK,C::P_DIODE},
    {C::P_FOLLOW,C::P_FOLLOW_TIME},{C::P_DRIVE,C::P_MIX}};
constexpr const char* names[4][2]={{"rate","depth"},{"feedback","diode"},{"follow","follow time"},{"drive","mix"}};
constexpr int choice_x[5]={14,70,114,220,270};
constexpr int choice_w[5]={50,40,58,46,42};
constexpr const char* choice_names[5]={"sine","tri","pulse","free","note"};
bool capture_context(){return g_eng&&g_audio&&g_overlay==Overlay::Editor&&
    (g_view==View::Chains||(g_view==View::Sequencer&&g_seq_lock_armed))&&
    g_edit_chain>=0&&g_edit_chain<NUM_CHAINS&&g_edit_slot>=0&&g_edit_slot<UNITS_PER_CHAIN;}
bool context(){return !param_lens_is_open()&&capture_context();}
bool lock_context(){return g_seq_lock_armed&&g_seq_lock_chain==g_edit_chain&&g_seq_lock_step>=0&&g_seq_lock_step<SEQ_STEPS;}
int bank(){const int b=sequencer_edit_bank();return b>=0&&b<NUM_PATTERNS?b:g_eng->active_pattern;}
int gesture_bank(){return g_seq_lock_armed?bank():g_eng->active_pattern;}
void release(){gesture={};if(g_drag==Drag::RingFreq)g_drag=Drag::None;}
q15 edit_value(const C& u,int p);
void bind(const C& u){if(bound_engine!=g_eng||bound_unit!=uintptr_t(&u)||bound_chain!=g_edit_chain||bound_slot!=g_edit_slot||bound_modern!=u.modern()){
    release();slow=false;selected=u.modern()&&edit_value(u,C::P_MODE)>=16384?C::P_NOTE:C::P_FREQ;bound_engine=g_eng;bound_unit=uintptr_t(&u);
    bound_chain=g_edit_chain;bound_slot=g_edit_slot;bound_modern=u.modern();modern_model.invalidate();legacy_valid=false;}}
q15 edit_value(const C& u,int p){if(lock_context()){
    const auto& step=g_eng->sequence_pattern(bank()).steps[g_edit_chain][g_seq_lock_step];
    for(int i=0;i<std::min(int(step.lock_count),SEQ_LOCKS);++i)if(step.locks[i].slot==g_edit_slot&&step.locks[i].param==p)return step.locks[i].value;}
    return u.params[p].base;}
bool note(const C& u){return u.modern()&&edit_value(u,C::P_MODE)>=16384;}
bool bipolar(int p,bool modern){return p==C::P_FEEDBACK||(modern&&(p==C::P_FOLLOW||p==C::P_FINE));}
int clamp(int v,int p,bool modern){return std::max(bipolar(p,modern)?-32767:0,std::min(32767,v));}
void set_value(C& u,int p,q15 v){if((g_seq_lock_armed&&!lock_context())||p<0||p>=u.num_params)return;
    if(g_seq_lock_armed){auto& step=g_eng->sequence_pattern(bank()).steps[g_edit_chain][g_seq_lock_step];
        const int count=std::min(int(step.lock_count),SEQ_LOCKS);int found=-1;
        for(int i=0;i<count;++i)if(step.locks[i].slot==g_edit_slot&&step.locks[i].param==p){found=i;break;}
        if(found>=0){if(step.locks[found].value==v)return;step.locks[found].value=v;}
        else {if(count>=SEQ_LOCKS){g_seq_lock_flash=90;return;}step.locks[count]={uint8_t(g_edit_slot),uint8_t(p),v};step.lock_count=uint8_t(count+1);}
    }else {if(u.params[p].base==v)return;u.params[p].base=v;}
    mark_project_mutated();}
bool remove_name(int p,int x,int y,const char* text,int tx,int ty){
    if(!lock_context()||!hit(tx,ty,x-3,y-4,ui::text_w(text)+6,16))return false;
    selected=p;if(g_eng->remove_step_lock(g_seq_lock_chain,g_seq_lock_step,g_edit_slot,p,bank()))mark_project_mutated();return true;}
void begin(C& u,int p,int x,int y){selected=p;gesture={};gesture.engine=g_eng;gesture.unit=uintptr_t(&u);gesture.view=g_view;
    gesture.chain=g_edit_chain;gesture.slot=g_edit_slot;gesture.bank=gesture_bank();gesture.step=g_seq_lock_step;
    gesture.lock_chain=g_seq_lock_chain;gesture.locked=g_seq_lock_armed;gesture.param=p;gesture.x=x;gesture.y=y;
    gesture.value=edit_value(u,p);gesture.fine=u.modern()?edit_value(u,C::P_FINE):0;gesture.modern=u.modern();
    gesture.slow=slow;gesture.note=note(u);gesture.modifiers=modifiers;g_drag=Drag::RingFreq;g_drag_param=p;}
bool owns(const C& u){return gesture.engine==g_eng&&gesture.unit==uintptr_t(&u)&&gesture.view==g_view&&
    gesture.chain==g_edit_chain&&gesture.slot==g_edit_slot&&gesture.bank==gesture_bank()&&gesture.param==selected&&
    gesture.param==g_drag_param&&gesture.modifiers==modifiers&&gesture.locked==g_seq_lock_armed&&gesture.modern==u.modern()&&
    gesture.slow==slow&&gesture.note==note(u)&&g_eng->chains[g_edit_chain].units[g_edit_slot]==&u&&
    (!gesture.locked||(lock_context()&&gesture.step==g_seq_lock_step&&gesture.lock_chain==g_seq_lock_chain));}
int position(q15 v,int p,int x,int end){const bool bi=bipolar(p,frame.modern);return x+(clamp(v,p,frame.modern)+(bi?32767:0))*(end-x)/(bi?65534:32767);}
u32 color(int p){return frame.selected==p?m::spot():m::ink();}
void right(int x,int y,const char* text,u32 c){ui::text(x-ui::text_w(text),y,text,c);}
void capture_plot(){const q15* values=frame.lock?frame.edit:frame.live;
    if(frame.modern){l::Key key;key.bypass=frame.bypass;key.slow=frame.slow;std::copy(values,values+C::P_COUNT,key.values);
        modern_model.advance(key);frame.ready=modern_model.ready();frame.progress=uint8_t(modern_model.done*100/modern_model.total());
        // An old key's completed graph is never shown while the new key is pending.
        if(frame.ready)frame.plot.modern=modern_model.plot;
    }else {if(!legacy_valid||legacy_bypass!=frame.bypass||!std::equal(values,values+6,legacy_key)){
            model::plot(values,frame.bypass,legacy_plot);std::copy(values,values+6,legacy_key);legacy_valid=true;legacy_bypass=frame.bypass;}
        new(&frame.plot.legacy)model::Plot(legacy_plot);frame.ready=true;}}
void trace(const model::Trace& t,u32 c){for(int i=0;i<model::COLUMNS;++i){const int x=model::X0+i;
    if(i)ui::line(x-1,t.last[i-1],x,t.first[i],c);
    ui::line(x,t.lo[i],x,t.hi[i],c);}}
void legacy_reference(){
    ui::line(14,82,24,82,m::ghost());ui::text(30,78,"test 250hz/.75fs",m::ghost());
    const auto out=frame.mute?m::ghost():m::spot();const char* label=frame.bypass?"byp dry":frame.lock?"lock out":frame.mute?"muted":"out";
    const int label_x=310-ui::text_w(label);ui::line(label_x-18,82,label_x-8,82,out);ui::text(label_x,78,label,out);
    for(int level:{1,0,-1}){const int y=model::ZERO-level*model::HEIGHT;const char* label=level>0?"+1":level<0?"-1":"0";
        ui::text(56-ui::text_w(label),y-4,label,m::ghost());if(!level)ui::line(model::X0,y,306,y,m::branch());
        else for(int x=model::X0;x<=306;x+=5)ui::rect(x,y,1,1,m::branch());}
    trace(frame.plot.legacy.source,m::ghost());trace(frame.plot.legacy.shaped,out);
    const bool paused=frame.bypass||(frame.lock?frame.edit:frame.live)[C::P_DEPTH]==0;
    ui::text(14,150,paused?"paused":"carrier",m::ghost());ui::line(model::X0,model::CARRIER_ZERO,306,model::CARRIER_ZERO,m::branch());
    trace(frame.plot.legacy.carrier,paused?m::ghost():m::ink());const int ticks[3]={model::X0,185,306};const char* labels[3]={"0","16","32ms"};
    for(int i=0;i<3;++i){ui::line(ticks[i],162,ticks[i],164,m::branch());ui::text(ticks[i]-(i==2?ui::text_w(labels[i]):0),167,labels[i],m::ghost());}}
void control(int p,int x,int end,int y,const char* name){const bool dim=p==C::P_FOLLOW_TIME&&frame.edit[C::P_FOLLOW]==0;
    const u32 c=dim?m::ghost():color(p);ui::text(x,y,name,c);right(end,y,frame.values[p],c);
    module_fader::horizontal(x+1,end-1,y+20,position(frame.edit[p],p,x+1,end-1),position(frame.live[p],p,x+1,end-1),
        {bool(frame.mod_links&(1u<<p)),dim,frame.bypass||frame.mute},bipolar(p,frame.modern)?position(0,p,x+1,end-1):-1);}
} // namespace
int ring_focus_selected(){return selected;}
void ring_focus_select(int p){if(p>=0&&p<C::P_COUNT){if(p!=selected)release();selected=p;}}
void ring_focus_release(){release();}
bool ring_focus_modern(){return frame.valid&&frame.modern;}
void ring_focus_value_text(char* out,size_t size,int p,q15 v){ring_readout::value_text(out,size,p,v,ring_focus_modern());}
void ring_focus_input(u32 down,u32 held,bool blocked){const u32 next=held&(KEY_L|KEY_R|KEY_ZL|KEY_ZR);
    if(blocked||next!=modifiers||(down&~KEY_TOUCH))release();
    modifiers=next;}
bool ring_focus_touch_body(C& u,int tx,int ty){if(!context()){release();return false;}
    platform::AudioOut::Guard guard(*g_audio);if(g_eng->chains[g_edit_chain].units[g_edit_slot]!=&u)return false;bind(u);release();
    if(u.modern()&&hit(tx,ty,198,0,48,26)){slow=!slow;return true;}
    if(!u.modern()&&hit(tx,ty,164,0,76,26)){if(!g_seq_lock_armed){C defaults;
        for(int p=C::LEGACY_COUNT;p<C::P_COUNT;++p)u.params[p]=defaults.params[p];
        for(auto& scene:g_eng->scenes[g_edit_chain])if(scene.kind[g_edit_slot]==UnitKind::Ring)
            for(int p=C::LEGACY_COUNT;p<C::P_COUNT;++p)scene.base[g_edit_slot][p]=defaults.params[p].base;
        u.set_modern(true);mark_project_mutated();bind(u);}return true;}
    if(ty<26){if(!g_seq_lock_armed&&hit(tx,ty,246,0,38,25)){u.bypass=!u.bypass;mark_project_mutated();}return true;}
    if(g_seq_lock_armed&&!lock_context())return true;
    if(u.modern()){
        for(int i=0;i<5;++i)if(hit(tx,ty,choice_x[i]-6,27,choice_w[i],24)){
            selected=i<3?C::P_WAVE:C::P_MODE;set_value(u,selected,i<3?q15(i*32767/2):i==3?0:32767);return true;}
        // Auxiliary selection/removal caption only; actual fine editing owns the
        // full primary 146x35 control via vertical axis, not this 104x14 shortcut.
        if(hit(tx,ty,208,51,104,14)){selected=C::P_FINE;
            if(lock_context()&&g_eng->remove_step_lock(g_seq_lock_chain,g_seq_lock_step,g_edit_slot,C::P_FINE,bank()))mark_project_mutated();
            return true;}
        for(int row=0;row<4;++row)for(int col=0;col<2;++col){const int x=l::kControlLeft[col],end=l::kControlRight[col],y=l::kRows[row];
            const int p=row==0&&col==0&&note(u)?C::P_NOTE:params[row][col];const char* name=p==C::P_NOTE?"pitch":names[row][col];
            if(!hit(tx,ty,x-6,y-5,end-x+12,35))continue;
            selected=p;
            if(remove_name(p,x,y,name,tx,ty))return true;
            begin(u,p,tx,ty);return true;}
        return true;}
    for(int p=0;p<6;++p){const auto& c=CONTROLS[p];if(!hit(tx,ty,c.x,c.y,c.w,c.h))continue;
        selected=p;if(remove_name(p,c.label_x,c.label_y,c.name,tx,ty))return true;begin(u,p,tx,ty);return true;}return true;
}
void ring_focus_drag(C& u,int tx,int ty){if(gesture.param<0)return;if(!context()||g_drag!=Drag::RingFreq){release();return;}
    platform::AudioOut::Guard guard(*g_audio);if(!owns(u)){release();return;}
    const int p=gesture.param,dx=tx-gesture.x,dy=gesture.y-ty;
    if(u.modern()&&p==C::P_FINE&&gesture.axis==2){set_value(u,p,q15(clamp(int(gesture.fine)+dy*32767/100,p,true)));return;}
    if(u.modern()&&(p==C::P_FREQ||p==C::P_NOTE)){
        if(!gesture.axis){if(std::max(std::abs(dx),std::abs(dy))<4)return;gesture.axis=std::abs(dy)>std::abs(dx)?2:1;}
        if(gesture.axis==2){
            // MOD must follow the parameter actually edited, not the inactive
            // coarse rate. Keep the original fine origin and vertical ownership.
            selected=gesture.param=g_drag_param=C::P_FINE;
            set_value(u,C::P_FINE,q15(clamp(int(gesture.fine)+dy*32767/100,C::P_FINE,true)));return;}
        if(p==C::P_NOTE){const q15 next=dx==0?gesture.value:musical_note::encode(musical_note::decode(gesture.value)+dx/4);
            set_value(u,p,next);return;}}
    const int range=bipolar(p,u.modern())?65534:32767,extent=u.modern()?296:CONTROLS[p].extent;
    set_value(u,p,q15(clamp(int(gesture.value)+dx*range/extent,p,u.modern())));
}
void ring_focus_capture(){frame={};if(!capture_context()){release();bound_engine=nullptr;bound_unit=0;modern_model.invalidate();legacy_valid=false;return;}
    {platform::AudioOut::Guard guard(*g_audio);const auto& chain=g_eng->chains[g_edit_chain];const Unit* unit=chain.units[g_edit_slot];
        if(!unit||unit->kind!=UnitKind::Ring){release();bound_engine=nullptr;bound_unit=0;modern_model.invalidate();legacy_valid=false;return;}
        const auto& u=*static_cast<const C*>(unit);bind(u);if(gesture.param>=0&&!owns(u))release();
        if(selected>=u.num_params){selected=C::P_FREQ;release();}
        frame.valid=true;frame.lens=param_lens_is_open();frame.modern=u.modern();frame.slow=slow;frame.bypass=u.bypass;frame.mute=chain.mute;frame.lock=lock_context();
        frame.lock_full=frame.lock&&g_eng->sequence_pattern(bank()).steps[g_edit_chain][g_seq_lock_step].lock_count>=SEQ_LOCKS;
        frame.chain=g_edit_chain;frame.slot=g_edit_slot;frame.selected=selected;frame.mod_links=focus_mod_links(u);
        for(int i=0;i<UNITS_PER_CHAIN;++i){frame.kinds[i]=chain.units[i]?uint8_t(chain.units[i]->kind):uint8_t(UnitKind::Empty);
            frame.bypassed[i]=chain.units[i]&&chain.units[i]->bypass;}
        for(int p=0;p<u.num_params;++p){frame.edit[p]=edit_value(u,p);frame.live[p]=u.modern()?u.control_value(p):u.params[p].effective;}}
    for(int p=0;p<(frame.modern?C::P_COUNT:C::LEGACY_COUNT);++p)ring_readout::value_text(frame.values[p],sizeof frame.values[p],p,frame.edit[p],frame.modern);
    if(frame.modern)ring_readout::frequency(frame.base,sizeof frame.base,C::base_millihz(frame.lock?frame.edit:frame.live));
    if(!frame.lens)capture_plot(); // bounded private DSP outside guard/GPU; none under MOD
    else modern_model.work=0;
}
void ring_focus_draw(const C&){m::paper(320);if(!frame.valid)return;module_heading::header(UnitKind::Ring,m::spot());
    char text[32];std::snprintf(text,sizeof text,"ch%02d / %02d",frame.chain+1,frame.slot+1);ui::text(56,m::kHeaderTextY,text,m::ghost());
    if(frame.modern)ui::text(206,m::kHeaderTextY,frame.slow?"4s":"32ms",m::spot());else ui::text(170,m::kHeaderTextY,"extend",frame.lock?m::ghost():m::spot());
    ui::text(258,m::kHeaderTextY,"byp",frame.bypass?m::spot():m::ghost());ui::text(m::right_edge(320)-ui::text_w("x"),m::kHeaderTextY,"x",m::ghost());
    if(frame.bypass)ui::line(258,m::kHeaderMarkY,275,m::kHeaderMarkY,m::spot());
    if(frame.lens){m::footer(320,"b back","l: mod >");return;}
    if(frame.modern){for(int i=0;i<5;++i){const bool active=i<3?i==C::wave_index(frame.edit[C::P_WAVE]):(i==4)==(frame.edit[C::P_MODE]>=16384);
            const u32 c=active?m::spot():m::ghost();ui::text(choice_x[i],35,choice_names[i],c);
            if(active)ui::line(choice_x[i],47,choice_x[i]+ui::text_w(choice_names[i])-2,47,c);}
        ui::text(14,55,"rate horiz / fine vert",m::ghost());std::snprintf(text,sizeof text,"fine %s",frame.values[C::P_FINE]);right(306,55,text,color(C::P_FINE));
        for(int row=0;row<4;++row)for(int col=0;col<2;++col){const int p=row==0&&col==0&&frame.edit[C::P_MODE]>=16384?C::P_NOTE:params[row][col];
            control(p,l::kControlLeft[col],l::kControlRight[col],l::kRows[row],p==C::P_NOTE?"pitch":names[row][col]);}
    }else {for(int p=0;p<6;++p){const auto& c=CONTROLS[p];ui::text(c.label_x,c.label_y,c.name,frame.selected==p?m::spot():p<2?m::ink():m::ghost());
            if(p<2)ui::text(c.label_x+c.extent-24,c.label_y,p==0?"base":"core",m::ghost());
            ui::text(c.label_x,p<2?48:196,frame.values[p],color(p));
            module_fader::horizontal(c.label_x,c.label_x+c.extent,c.rail_y,position(frame.edit[p],p,c.label_x,c.label_x+c.extent),position(frame.live[p],p,c.label_x,c.label_x+c.extent),
                {bool(frame.mod_links&(1u<<p)),false,frame.bypass||frame.mute},p==C::P_FEEDBACK?position(0,p,c.label_x,c.label_x+c.extent):-1);}
        legacy_reference();ui::line(14,223,306,223,m::branch());}
    if(frame.lock)m::footer(320,frame.lock_full?"lock full":"p-lock / name: remove","b back");else m::footer(320,"b back","l: mod >");
}
bool ring_focus_draw_top(){if(!frame.valid||!frame.modern||frame.lens)return false;
    m::paper(400);module_heading::header(UnitKind::Ring,m::spot());char text[64];
    std::snprintf(text,sizeof text,"base carrier %s",frame.base);m::header_text(56,296,text,m::ink());
    std::snprintf(text,sizeof text,"ch%02d / %02d",frame.chain+1,frame.slot+1);right(m::right_edge(400),m::kHeaderTextY,text,m::ghost());
    int rx=14;for(int i=0;i<UNITS_PER_CHAIN;++i){const auto k=UnitKind(frame.kinds[i]);if(k==UnitKind::Empty)continue;
        if(rx>14){ui::text(rx,module_heading::kRouteY,"/",m::ghost());rx+=14;}const char* name=m::module_name(k);const int w=ui::text_w(name);if(rx+w>386)break;
        ui::text(rx,module_heading::kRouteY,name,i==frame.slot?m::spot():m::ghost());
        if(i==frame.slot)ui::line(rx,module_heading::kRouteUnderlineY,rx+w-2,module_heading::kRouteUnderlineY,m::spot());
        if(frame.bypassed[i])ui::line(rx,module_heading::kRouteCrossY,rx+w-2,module_heading::kRouteCrossY,m::ghost());
        rx+=w+8;}
    ui::text(14,55,frame.slow?"4s tone envelope / 250hz":"32ms burst / 250hz",m::ghost());right(386,55,"fixed +/-1fs",m::ghost());
    const q15* state=frame.lock?frame.edit:frame.live;const bool paused=frame.bypass||state[C::P_DEPTH]==0;
    const char* labels[]={"in",paused?"pause":"car",frame.bypass?"dry":"out"};
    for(int t=0;t<3;++t){const int zero=l::kZero[t],height=l::kHeight[t];const u32 c=t==2?(frame.mute?m::ghost():m::spot()):m::ghost();
        ui::text(14,zero-height-8,labels[t],c);ui::line(l::kLeft,zero,l::kRight,zero,m::branch());
        for(int x=l::kLeft;x<=l::kRight;x+=8){ui::rect(x,zero-height,1,1,m::branch());ui::rect(x,zero+height,1,1,m::branch());}
        if(frame.ready)for(int i=0;i<l::kColumns;++i){const auto& col=frame.plot.modern.trace[t][i];const int x=l::kLeft+i;
            if(i)ui::line(x-1,zero-frame.plot.modern.trace[t][i-1].last,x,zero-col.first,c);
            ui::line(x,zero-col.high,x,zero-col.low,c);}}
    if(!frame.ready){std::snprintf(text,sizeof text,"updating reference %u%%",unsigned(frame.progress));ui::text(110,160,text,m::ghost());}
    ui::text(l::kLeft,199,"0",m::ghost());ui::text(198,199,frame.slow?"2":"16",m::ghost());right(386,199,frame.slow?"4s":"32ms",m::ghost());
    const char* help=frame.selected==C::P_DEPTH?"depth: inner dry / modulation core":frame.selected==C::P_MIX?"mix: final dry / processed":
        frame.selected==C::P_FOLLOW?"follow: input level, not pitch tracking":frame.selected==C::P_FOLLOW_TIME?"follow time: release / kept at follow 0":"rate horiz / fine vert / no pitch tracking";
    ui::text(14,214,help,m::ghost());
    m::footer(400,frame.lock?"lock reset ref / not live audio":frame.mute?"muted / hypothetical reset ref":frame.bypass?"bypass / paused reset ref":"reset reference / not live audio",frame.ready?"ready":"updating");return true;
}
} // namespace seance::platform::ui3ds
