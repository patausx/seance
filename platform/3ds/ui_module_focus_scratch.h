// Private production implementation included once by ui_module_focus.cpp.
#pragma once
#include "ui_module_focus_internal.h"
#include "scratch_ui.h"
#include "scratch_readout.h"
#include "ui_state.h"
#include "ui_theme.h"
#include "ui_param_lens.h"
#include "ui_sample_library.h"
#include "ui_sequencer.h"
#include "module_inspector.h"
#include "module_heading.h"
#include "editor_style.h"
#include "audio_out.h"
#include "sigil_surface.h"
#include "module_fader_painter.h"
#include <cstdio>
#include <cstring>

namespace seance::platform::ui3ds {
namespace {
namespace m = editor_style;
namespace sg = scratch_ui;
using SC = ScratchUnit;
using SB = trackr::synth::SampleBank;
sg::Frame scratch_frame;
constexpr const char* scratch_names[]={"position","gain","window","speed","pitch","follow","gate"};
// HOLD keeps all params; SCRATCH omits its dormant window/pitch controls.
constexpr int scratch_setup[]={SC::P_PITCH,SC::P_WINDOW,SC::P_FOLLOW,SC::P_GAIN,SC::P_GATE,SC::P_POSITION,SC::P_SPEED};
constexpr int scratch_setup_count(SC::Mode mode){return mode==SC::Mode::Hold?SC::P_COUNT:SC::P_COUNT-2;}
constexpr int scratch_setup_param(SC::Mode mode,int row){return scratch_setup[row+(mode==SC::Mode::Hold?0:2)];}
int scratch_selected=SC::P_POSITION,scratch_page=0;
bool scratch_wait_lift=false,scratch_blocked=false,scratch_gesture_failed=false;
uintptr_t scratch_view_unit=0;Engine* scratch_view_engine=nullptr;int scratch_view_chain=-1,scratch_view_slot=-1;
struct ScratchTrace {q15 x=0,y=0;uint64_t time=0;bool join=false;} scratch_trace[12];
unsigned scratch_trace_count=0;
struct ScratchTouch {
    Engine* engine=nullptr; platform::AudioOut* audio=nullptr;
    uintptr_t unit=0;int chain=-1,slot=-1,sample=-1,step=-1,bank=-1,page=0;
    View view=View::Chains;bool lock=false;uint32_t generation=0,token=0;
    SC::Mode mode=SC::Mode::Scratch;SC::Map map=SC::Map::PositionGain;
    int axis_x=-1,axis_y=-1;
    int x=0,y=0,param=-1,action=-1;sg::Box box{};
    q15 ox=0,oy=0;bool field=false;
} scratch_touch;
enum ScratchAction {SSource=10,SSetup,SScratch,SHold,SSpeed,SPosition,SMod,SBack,SAxisX,SAxisY,SAxisChoice,SLatch};
struct ScratchWords {
    int axis=-1,selected=0,count=0,params[5]={};
    ScratchTouch owner{};
    bool opening=false;
    uint32_t ink=0,spot=0;
    word_menu::State motion{};
    sg::Frame background{};
} scratch_words;
bool scratch_context(){return g_eng&&g_audio&&g_overlay==Overlay::Editor&&
    (g_view==View::Chains||(g_view==View::Sequencer&&g_seq_lock_armed))&&
    g_edit_chain>=0&&g_edit_chain<NUM_CHAINS&&g_edit_slot>=0&&g_edit_slot<UNITS_PER_CHAIN;}
SC* scratch_current(){if(!scratch_context())return nullptr;auto* u=g_eng->chains[g_edit_chain].units[g_edit_slot];
    return u&&u->kind==UnitKind::Scratch?static_cast<SC*>(u):nullptr;}
int scratch_bank(){const int b=sequencer_edit_bank();return g_seq_lock_armed&&b>=0&&b<NUM_PATTERNS?b:g_eng->active_pattern;}
q15 scratch_edit(const SC& u,int p){return g_seq_lock_armed?seq_display_value(g_edit_slot,p,u.params[p].base):u.params[p].base;}
bool scratch_owns(const SC& u){return scratch_context()&&!param_lens_is_open()&&
    scratch_touch.engine==g_eng&&scratch_touch.audio==g_audio&&scratch_touch.unit==uintptr_t(&u)&&
    scratch_touch.chain==g_edit_chain&&scratch_touch.slot==g_edit_slot&&scratch_touch.view==g_view&&
    scratch_touch.lock==g_seq_lock_armed&&scratch_touch.page==scratch_page&&scratch_touch.mode==u.mode&&scratch_touch.map==u.map&&
    scratch_touch.axis_x==u.axis_x&&scratch_touch.axis_y==u.axis_y&&
    scratch_touch.sample==u.sample_slot&&
    (u.sample_slot<0||(u.sample_slot<trackr::synth::SAMPLE_BANK_SIZE&&scratch_touch.generation==SB::instance().generation(u.sample_slot)))&&
    (!g_seq_lock_armed||(scratch_touch.step==g_seq_lock_step&&scratch_touch.bank==scratch_bank()&&g_seq_lock_chain==g_edit_chain))&&
    (!scratch_touch.field||g_eng->scratch_gesture_active(scratch_touch.token));}
bool scratch_menu_owns(const SC& u){
    const auto saved=scratch_touch;scratch_touch=scratch_words.owner;
    const bool valid=scratch_owns(u)&&!g_seq_lock_armed&&scratch_words.ink==m::ink()&&scratch_words.spot==m::spot();
    scratch_touch=saved;return valid;
}
void scratch_close_words(){scratch_words.axis=-1;scratch_words.owner={};scratch_words.motion={};}
void scratch_cancel(){
    if(scratch_touch.token&&g_audio&&g_eng==scratch_touch.engine){platform::AudioOut::Guard guard(*g_audio);g_eng->cancel_scratch_gesture(scratch_touch.token);}
    if(scratch_touch.unit)scratch_wait_lift=true;
    // Cancellation is not a graphic/audio tail. A real lift may retain its trace.
    scratch_trace_count=0;
    scratch_touch={};if(g_drag==Drag::ScratchParam)g_drag=Drag::None;
}
void scratch_cancel_all(){if(scratch_words.axis>=0){scratch_close_words();scratch_wait_lift=true;}scratch_cancel();}
void scratch_begin(SC& u,int x,int y){
    scratch_touch={};scratch_touch.engine=g_eng;scratch_touch.audio=g_audio;scratch_touch.unit=uintptr_t(&u);
    scratch_touch.chain=g_edit_chain;scratch_touch.slot=g_edit_slot;scratch_touch.sample=u.sample_slot;
    scratch_touch.generation=u.sample_slot>=0&&u.sample_slot<trackr::synth::SAMPLE_BANK_SIZE?SB::instance().generation(u.sample_slot):0;
    scratch_touch.step=g_seq_lock_step;scratch_touch.bank=scratch_bank();scratch_touch.page=scratch_page;
    scratch_touch.view=g_view;scratch_touch.lock=g_seq_lock_armed;scratch_touch.mode=u.mode;scratch_touch.map=u.map;
    scratch_touch.axis_x=u.axis_x;scratch_touch.axis_y=u.axis_y;
    scratch_touch.x=x;scratch_touch.y=y;scratch_trace_count=0;g_drag=Drag::ScratchParam;
}
void scratch_arm(SC& u,int x,int y,int action,sg::Box b){scratch_begin(u,x,y);scratch_touch.action=action;scratch_touch.box=b;}
int scratch_at(q15 value,bool bi,int first,int extent){return first+(std::clamp(int(value),bi?-32767:0,32767)+(bi?32767:0))*extent/(bi?65534:32767);}
q15 scratch_absolute(int coordinate,int first,int extent,bool bi,bool inverse=false){
    int v=std::clamp(coordinate-first,0,extent-1);if(inverse)v=extent-1-v;
    return q15(int64_t(v)*(bi?65534:32767)/(extent-1)-(bi?32767:0));
}
void scratch_right(int x,int y,const char* s,u32 c){ui::text(x-ui::text_w(s),y,s,c);}
void scratch_open_words(SC& u,int axis){
    int ax,ay;u.axis_params(ax,ay);scratch_selected=axis?ay:ax;
    scratch_words={};scratch_words.axis=axis;scratch_words.owner=scratch_touch;
    scratch_words.owner.action=-1;scratch_words.owner.field=false;scratch_words.owner.token=0;
    scratch_words.ink=m::ink();scratch_words.spot=m::spot();scratch_words.opening=true;
    for(int p=0;p<SC::P_COUNT;++p)if(SC::axis_parameter_available(u.mode,p)&&p!=(axis?ax:ay)){
        if(p==scratch_selected)scratch_words.selected=scratch_words.count;
        scratch_words.params[scratch_words.count++]=p;}
    scratch_words.background=scratch_frame;
}
sg::Box scratch_menu_box(int i){const auto& l=scratch_frame.menu_layout;return {l.left,l.y-8+i*l.step,l.right-l.left,l.step};}
void scratch_route(char* out,size_t n,int p){const auto& d=scratch_frame.params[p];
    if(d.source==ModSource::None)std::snprintf(out,n,"%s / mod off",scratch_names[p]);
    else if(d.source==ModSource::EnvFollow)std::snprintf(out,n,"env%d > %s%s",d.env_chain+1,scratch_names[p],d.mod_enabled?"":" muted");
    else std::snprintf(out,n,"sg.%c > %s %+d%%%s",'a'+mod_source_sigil(d.source),scratch_names[p],int(d.depth)*100/32767,d.mod_enabled?"":" muted");}
template<class P> void scratch_word(const P& p,sg::Box b,const char* s,bool selected=false,bool dim=false){int x=b.x+(b.w-ui::text_w(s))/2;
    p.text(x,b.y+8,s,dim?m::ghost():selected?m::spot():m::ink());
    if(selected)p.line(x,b.y+20,x+ui::text_w(s)-1,b.y+20,m::spot());}
template<class P> void scratch_field(const P& p){const auto& f=scratch_frame;const auto b=sg::field;
    const bool enabled=f.loaded&&!f.bypass&&!f.mute&&!f.lock&&!f.gesture_failed;
    for(int x:{b.x,b.x+b.w-1})for(int y:{b.y,b.y+b.h-1}){int dx=x==b.x?9:-9,dy=y==b.y?9:-9;
        p.line(x,y,x+dx,y,enabled?m::ink():m::ghost());p.line(x,y,x,y+dy,enabled?m::ink():m::ghost());}
    const int ax=f.axis_x,ay=f.axis_y;
    // Assigned bipolar zero is parameter space. The dashed head is audio space.
    if(f.params[ax].bipolar){int zx=scratch_at(0,true,b.x,b.w-1);p.line(zx,b.y,zx,b.y+b.h-1,m::branch());}
    if(f.params[ay].bipolar){int zy=b.y+b.h-1-scratch_at(0,true,0,b.h-1);p.line(b.x,zy,b.x+b.w-1,zy,m::branch());}
    if(f.bottom_cursor>=0){for(int y=b.y;y<b.y+b.h;y+=6)p.line(f.bottom_cursor,y,f.bottom_cursor,std::min(y+3,b.y+b.h-1),m::ink());
        p.line(f.bottom_cursor,b.y,f.bottom_cursor,b.y+6,m::spot());}
    if(!enabled)return;
    const auto& px=f.params[ax];const auto& py=f.params[ay];
    for(unsigned i=0;i<f.trace_count;++i){if(!f.trace_alpha[i])continue;
        const int x=scratch_at(f.trace_x[i],px.bipolar,b.x,b.w-1),y=b.y+b.h-1-scratch_at(f.trace_y[i],py.bipolar,0,b.h-1);
        const u32 c=(m::spot()&0xffffffu)|(uint32_t(f.trace_alpha[i])<<24);
        if(i&&f.trace_join[i]&&f.trace_alpha[i-1]){const int ox=scratch_at(f.trace_x[i-1],px.bipolar,b.x,b.w-1),oy=b.y+b.h-1-scratch_at(f.trace_y[i-1],py.bipolar,0,b.h-1);p.line(ox,oy,x,y,c);}
        p.rect(x,y,1,1,c);
    }
    int x=scratch_at(px.target,px.bipolar,b.x,b.w-1),y=b.y+b.h-1-scratch_at(py.target,py.bipolar,0,b.h-1);
    // Target crosshair never masquerades as the actual source head.
    p.line(std::max(b.x,x-3),y,std::min(b.x+b.w-1,x+3),y,m::spot());
    p.line(x,std::max(b.y,y-3),x,std::min(b.y+b.h-1,y+3),m::spot());
}
} // namespace
namespace scratch_ui {const Frame& frozen(){return scratch_frame;}}
int scratch_focus_selected(){return scratch_selected;}
void scratch_focus_select(int p){if(p<0||p>=SC::P_COUNT)return;if(p!=scratch_selected)scratch_cancel_all();scratch_selected=p;}
void scratch_focus_release(){scratch_cancel_all();}
void scratch_focus_input(u32 down,u32 held,bool blocked){
    scratch_blocked=blocked||(down&~(KEY_TOUCH|KEY_ZL));
    if(scratch_blocked||!scratch_context()||param_lens_is_open()){
        // Let the existing B dispatcher close this disclosure, not the editor.
        if(!blocked&&(down&~(KEY_TOUCH|KEY_ZL))==KEY_B&&scratch_words.axis>=0&&scratch_context()&&!param_lens_is_open())scratch_cancel();
        else scratch_cancel_all();
        if(held&KEY_TOUCH)scratch_wait_lift=true;
    }else if(scratch_touch.unit||scratch_words.axis>=0){
        platform::AudioOut::Guard guard(*g_audio);auto* u=scratch_current();
        if(!u||(scratch_touch.unit&&!scratch_owns(*u))||(scratch_words.axis>=0&&!scratch_menu_owns(*u)))scratch_cancel_all();
    }
    if(!(held&KEY_TOUCH))scratch_wait_lift=false; // normal lift commits in the release path
}
bool scratch_focus_back(){const bool menu=scratch_words.axis>=0;scratch_cancel_all();if(menu)return true;if(scratch_page){scratch_page=0;return true;}return false;}
bool scratch_focus_touch_body(SC& u,int x,int y){
    if(!scratch_context()||param_lens_is_open()||scratch_blocked||scratch_wait_lift)return true;
    platform::AudioOut::Guard guard(*g_audio);if(scratch_current()!=&u)return true;
    if(scratch_touch.unit)return true;
    if(scratch_words.axis>=0){
        if(!scratch_menu_owns(u)){scratch_cancel_all();return true;}
        for(int i=0;i<scratch_words.count;++i)if(scratch_menu_box(i).has(x,y)){
            scratch_arm(u,x,y,SAxisChoice,scratch_menu_box(i));scratch_touch.param=scratch_words.params[i];scratch_words.selected=i;return true;}
        scratch_cancel_all();scratch_wait_lift=true;return true;
    }
    if(sg::setup.has(x,y)){scratch_arm(u,x,y,SSetup,sg::setup);return true;}
    if(sg::back.has(x,y)){scratch_arm(u,x,y,SBack,sg::back);return true;}
    if(!g_seq_lock_armed){
        const sg::Box boxes[]={sg::source,sg::scratch,sg::hold,sg::speed,sg::position,sg::mod,sg::latch};
        const int actions[]={SSource,SScratch,SHold,SSpeed,SPosition,SMod,SLatch};
        for(int i=0;i<7;++i)if(boxes[i].has(x,y)){scratch_arm(u,x,y,actions[i],boxes[i]);return true;}}
    int ax,ay;u.axis_params(ax,ay);
    for(int i=0;i<2;++i)if(!scratch_page&&sg::axis(i).has(x,y)){
        scratch_selected=i?ay:ax;if(!g_seq_lock_armed)scratch_arm(u,x,y,i?SAxisY:SAxisX,sg::axis(i));return true;}
    if(scratch_page==1||g_seq_lock_armed){for(int i=0;i<scratch_setup_count(u.mode);++i){auto b=sg::parameter(i);if(!b.has(x,y))continue;
        const int p=scratch_setup_param(u.mode,i);scratch_selected=p;
        if(g_seq_lock_armed&&g_seq_lock_chain!=g_edit_chain)return true;
        if(g_seq_lock_armed&&y<b.y+10&&x<b.x+ui::text_w(scratch_names[p])){
            if(g_eng->remove_step_lock(g_seq_lock_chain,g_seq_lock_step,g_edit_slot,p,scratch_bank()))mark_project_mutated();
            return true;}
        scratch_begin(u,x,y);scratch_touch.param=p;scratch_touch.ox=scratch_edit(u,p);return true;}return true;}
    if(scratch_page==0&&sg::field.has(x,y)){
        if(g_seq_lock_armed)return true;
        const q15 vx=scratch_absolute(x,sg::field.x,sg::field.w,u.params[ax].bipolar);
        const q15 vy=scratch_absolute(y,sg::field.y,sg::field.h,u.params[ay].bipolar,true);
        const q15 source=scratch_absolute(x,sg::field.x,sg::field.w,false);
        const uint32_t token=g_eng->begin_scratch_gesture_at(g_edit_chain,g_edit_slot,vx,vy,source);
        scratch_gesture_failed=!token;
        if(!token){scratch_wait_lift=true;return true;}
        scratch_begin(u,x,y);scratch_touch.field=true;scratch_touch.token=token;scratch_touch.ox=vx;scratch_touch.oy=vy;
    }
    return true;
}
void scratch_focus_drag(SC& u,int x,int y){if(!scratch_touch.unit)return;
    if(!scratch_context()||param_lens_is_open()||scratch_blocked){scratch_cancel();return;}
    platform::AudioOut::Guard guard(*g_audio);if(!scratch_owns(u)){scratch_cancel();return;}
    if(scratch_touch.action>=0){const int64_t dx=int64_t(x)-scratch_touch.x,dy=int64_t(y)-scratch_touch.y;
        if(!scratch_touch.box.has(x,y)||dx*dx+dy*dy>64)scratch_cancel();
        return;}
    const auto relative=[](q15 v,int64_t delta,int extent,bool bi){return q15(std::clamp<int64_t>(int64_t(v)+delta*(bi?65534:32767)/extent,bi?-32767:0,32767));};
    if(scratch_touch.field){int ax,ay;u.axis_params(ax,ay);
        const auto vx=scratch_absolute(x,sg::field.x,sg::field.w,u.params[ax].bipolar);
        const auto vy=scratch_absolute(y,sg::field.y,sg::field.h,u.params[ay].bipolar,true);
        if(!g_eng->set_scratch_gesture(scratch_touch.token,vx,vy))scratch_cancel();
    }else if(scratch_touch.param>=0){int p=scratch_touch.param;auto value=relative(scratch_touch.ox,int64_t(x)-scratch_touch.x,128,u.params[p].bipolar);
        if(value!=scratch_edit(u,p)){if(g_seq_lock_armed)seq_record_lock(g_edit_slot,p,value);else{u.params[p].base=value;mark_project_mutated();}}}
}
void scratch_focus_touch_release(){
    int action=-1;SC* browser=nullptr;bool normal_field=false;
    const unsigned saved_count=scratch_trace_count;ScratchTrace saved_trace[12];std::memcpy(saved_trace,scratch_trace,sizeof saved_trace);
    if(scratch_touch.unit&&scratch_context()&&!param_lens_is_open()&&!scratch_blocked){platform::AudioOut::Guard guard(*g_audio);auto* u=scratch_current();
        if(u&&scratch_owns(*u)){
            action=scratch_touch.action;
            if(scratch_touch.field){g_eng->end_scratch_gesture(scratch_touch.token);scratch_touch.token=0;normal_field=true;}
            else if(action==SSetup)scratch_page=scratch_page==1?0:1;
            else if(!g_seq_lock_armed){bool changed=false;
                if(action==SAxisX||action==SAxisY)scratch_open_words(*u,action==SAxisY?1:0);
                else if(action==SAxisChoice&&scratch_words.axis>=0&&scratch_menu_owns(*u)){
                    const int p=scratch_touch.param;int ax,ay;u->axis_params(ax,ay);
                    changed=(scratch_words.axis?ay:ax)!=p&&u->set_axis(scratch_words.axis,p);
                    if(changed)scratch_selected=p;
                    scratch_close_words();
                }else if(action==SScratch||action==SHold){const auto mode=action==SScratch?SC::Mode::Scratch:SC::Mode::Hold;changed=u->mode!=mode;if(changed)u->set_mode(mode);}
                else if(action==SSpeed||action==SPosition){const auto map=action==SSpeed?SC::Map::SpeedPitch:SC::Map::PositionGain;
                    changed=u->map!=map||u->axis_x!=-1||u->axis_y!=-1;if(changed)u->set_map(map);}
                else if(action==SLatch){const bool enable=!u->latched();
                    scratch_gesture_failed=!g_eng->set_scratch_latch(g_edit_chain,g_edit_slot,enable);}
                else if(action==SSource)browser=u;
                if(changed){scratch_gesture_failed=false;mark_project_mutated();}
            }
        }else scratch_cancel_all();
    }
    scratch_cancel();scratch_wait_lift=false;
    if(normal_field){scratch_trace_count=saved_count;std::memcpy(scratch_trace,saved_trace,sizeof saved_trace);}
    if(browser){scratch_close_words();scan_sample_files();g_overlay=Overlay::SampleBrowser;}
    else if(action==SMod){scratch_close_words();module_focus_open_mod();param_lens_touch_release();}
    else if(action==SBack){scratch_close_words();if(scratch_page)scratch_page=0;else g_overlay=Overlay::None;}
}
void scratch_focus_value_text(char* out,size_t n,int p,q15 v){scratch_readout::value_text(out,n,p,v);}
static void scratch_capture_locked(){
    scratch_frame={};if(!scratch_context()){scratch_cancel_all();scratch_gesture_failed=false;scratch_page=0;scratch_view_unit=0;scratch_view_engine=nullptr;return;}
    auto* u=scratch_current();if(!u){scratch_cancel_all();scratch_gesture_failed=false;scratch_page=0;scratch_view_unit=0;scratch_view_engine=nullptr;return;}
    if(scratch_view_unit!=uintptr_t(u)||scratch_view_engine!=g_eng||scratch_view_chain!=g_edit_chain||scratch_view_slot!=g_edit_slot){
        scratch_cancel_all();scratch_gesture_failed=false;scratch_page=0;scratch_view_unit=uintptr_t(u);scratch_view_engine=g_eng;scratch_view_chain=g_edit_chain;scratch_view_slot=g_edit_slot;}
    if((scratch_touch.unit&&(!scratch_owns(*u)||param_lens_is_open()))||(scratch_words.axis>=0&&!scratch_menu_owns(*u)))scratch_cancel_all();
    if(!SC::parameter_available(u->mode,scratch_selected)){int ax,ay;u->axis_params(ax,ay);scratch_selected=ax;}
    auto& f=scratch_frame;f.valid=true;f.chain=g_edit_chain;f.slot=g_edit_slot;f.mode=uint8_t(u->mode);f.map=uint8_t(u->map);
    f.page=g_seq_lock_armed?1:scratch_page;f.selected=scratch_selected;f.lock=g_seq_lock_armed;f.lens=param_lens_is_open();
    f.bypass=u->bypass;f.mute=g_eng->chains[g_edit_chain].mute;f.sample_slot=u->sample_slot;f.visual=u->visual();
    f.gesture=scratch_touch.field&&scratch_touch.token;f.origin_x=scratch_touch.ox;f.origin_y=scratch_touch.oy;
    u->axis_params(f.axis_x,f.axis_y);f.latched=u->latched();f.gesture_failed=scratch_gesture_failed;
    for(int i=0;i<2;++i)std::snprintf(f.axis_labels[i],sizeof f.axis_labels[i],"%c %s >",i?'y':'x',scratch_names[i?f.axis_y:f.axis_x]);
    for(int p=0;p<SC::P_COUNT;++p){f.edit[p]=scratch_edit(*u,p);g_eng->capture_param_inspector(g_edit_chain,g_edit_slot,p,f.params[p]);
        scratch_focus_value_text(f.values[p],sizeof f.values[p],p,f.edit[p]);}
    if(u->sample_slot<0||u->sample_slot>=trackr::synth::SAMPLE_BANK_SIZE)return;
    const auto& s=SB::instance().slot(u->sample_slot);f.frames=s.playable_frames();f.generation=SB::instance().generation(u->sample_slot);
    f.loaded=!s.empty()&&f.frames&&(s.channels==1||s.channels==2);f.view_last=f.frames?f.frames-1:0;
    f.latch_available=f.loaded&&!f.bypass&&!f.mute&&!f.lock&&!f.gesture_failed&&!g_eng->recording_active_or_pending();
    for(int p:{f.axis_x,f.axis_y})if(u->params[p].performance_override||u->params[p].step_lock_override||
        (u->params[p].temp_override&&!f.gesture))f.latch_available=false;
    const char* path=SB::instance().provenance(u->sample_slot).path;const char* name=std::strrchr(path,'/');name=name?name+1:path;
    std::snprintf(f.source_name,sizeof f.source_name,"s%02u / %.18s",unsigned(u->sample_slot+1),name[0]?name:f.loaded?"sample":"empty");
    if(!f.loaded)return;
    // Trace samples are captured actual resolved target pairs, not interpolated
    // stylus fiction. Context cancellation clears; a real lift fades for 240ms.
    const uint64_t now=osGetTime();const int ax=f.axis_x,ay=f.axis_y;
    if(f.gesture&&!f.bypass&&!f.mute&&!f.lock){const q15 x=f.params[ax].target,y=f.params[ay].target;
        if(!scratch_trace_count||scratch_trace[scratch_trace_count-1].x!=x||scratch_trace[scratch_trace_count-1].y!=y){
            bool join=false;if(scratch_trace_count){const auto& last=scratch_trace[scratch_trace_count-1];join=now>=last.time&&now-last.time<=80&&
                std::abs(int(x)-last.x)*4<=(u->params[ax].bipolar?65534:32767)&&std::abs(int(y)-last.y)*4<=(u->params[ay].bipolar?65534:32767);}
            if(scratch_trace_count==12){for(unsigned i=1;i<12;++i)scratch_trace[i-1]=scratch_trace[i];--scratch_trace_count;scratch_trace[0].join=false;}
            scratch_trace[scratch_trace_count++]={x,y,now,join};}}
    f.trace_count=scratch_trace_count;for(unsigned i=0;i<scratch_trace_count;++i){const auto& t=scratch_trace[i];f.trace_x[i]=t.x;f.trace_y[i]=t.y;f.trace_join[i]=t.join;
        f.trace_alpha[i]=now>=t.time&&now-t.time<240?uint8_t((240-(now-t.time))*160/240):0;}
    // Bounded eight real PCM probes per overview column, L/R union; no invented wave.
    for(int x=0;x<372;++x){uint32_t a=uint64_t(x)*f.frames/372,b=uint64_t(x+1)*f.frames/372;
        b=std::max(a+1,b);int lo=32767,hi=-32768;
        for(int j=0;j<8;++j){uint32_t at=uint32_t(std::min<uint64_t>(f.frames-1,a+uint64_t(j)*(b-a-1)/7));
            for(int ch=0;ch<s.channels;++ch){int v=s.data[size_t(at)*s.channels+ch];lo=std::min(lo,v);hi=std::max(hi,v);}}
        f.low[x]=uint8_t(110-lo*46/32768);f.high[x]=uint8_t(110-hi*46/32768);}
    if(f.visual.loaded&&f.visual.active&&!f.bypass&&!f.mute&&f.visual.sample_slot==f.sample_slot&&
        f.visual.generation==f.generation&&f.visual.frames==f.frames){
        f.top_cursor=sg::project_source(f.visual.position_q16,f.view_first,f.view_last,14,372);
        f.bottom_cursor=sg::project_source(f.visual.position_q16,f.view_first,f.view_last,sg::field.x,sg::field.w);
        // This same snapshot owns the inclusive last-used reader UNION, including
        // source-edge shifts and old/new latched spans during a live window edit.
        if(f.mode==uint8_t(SC::Mode::Hold)){
            f.window_left=sg::project_source(f.visual.window_start_q16,f.view_first,f.view_last,14,372);
            f.window_right=sg::project_source(f.visual.window_end_q16,f.view_first,f.view_last,14,372);}}
}
template<class P> void scratch_bottom(const P& p){const auto& f=scratch_frame;if(!f.valid)return;
    // Native chrome uses the actual shared renderer; only the receding cache
    // adapts its same registered mask, as the existing PLAY word surface does.
    if(!p.cache)module_heading::header(UnitKind::Scratch,m::spot());
    else{const auto* title=module_heading::label(UnitKind::Scratch);
        if(title){const int stride=(title->width+7)/8;const auto* bits=module_heading_data::bitmap+title->offset;
            for(int y=0;y<title->height;++y)for(int x=0;x<title->width;++x)if(bits[y*stride+x/8]&(0x80>>(x&7)))
                p.rect(module_heading::kX+x,module_heading::kY+y,1,1,m::spot());}}
    p.text(76,m::kHeaderTextY,f.source_name[0]?f.source_name:"source >",m::ink());
    p.right(m::right_edge(320),m::kHeaderTextY,"setup",m::ghost());
    scratch_word(p,sg::scratch,"scratch",f.mode==0,f.lock);scratch_word(p,sg::hold,"hold",f.mode==1,f.lock);
    scratch_word(p,sg::speed,"speed",f.map==2,f.lock);scratch_word(p,sg::position,"position",f.map!=2,f.lock);
    if(f.page==1){for(int i=0;i<scratch_setup_count(SC::Mode(f.mode));++i){const int id=scratch_setup_param(SC::Mode(f.mode),i);const auto b=sg::parameter(i);const auto& d=f.params[id];
        // Shared MOD owns its labels; don't print duplicate setup text beneath it.
        if(!f.lens){p.text(b.x,b.y,scratch_names[id],f.selected==id?m::spot():m::ink());p.right(b.x+b.w,b.y+12,f.values[id],m::ghost());}
        const int edit=scratch_at(f.edit[id],d.bipolar,b.x,128),live=scratch_at(d.effective,d.bipolar,b.x,128);
        module_fader::horizontal(p,b.x,b.x+128,b.y+25,edit,live,{d.source!=ModSource::None&&d.mod_enabled,false,f.bypass||f.mute},d.bipolar?scratch_at(0,true,b.x,128):-1,true);}}
    else{
        p.text(sg::axis(0).x,66,f.axis_labels[0],f.selected==f.axis_x?m::spot():m::ink());
        p.right(306,66,f.axis_labels[1],f.selected==f.axis_y?m::spot():m::ink());scratch_field(p);}
    p.text(m::kLeft,m::kTextY,f.lock?"p-lock / name: remove":"mod >",m::ghost());
    if(!f.lock)p.text(sg::latch.x+(sg::latch.w-ui::text_w("latch off"))/2,m::kTextY,f.latched?"latch on":"latch off",
        f.latched?m::spot():f.latch_available?m::ink():m::ghost());
    p.right(m::right_edge(320),m::kTextY,"back >",m::ghost());
}
void scratch_focus_capture(){
    if(g_audio){platform::AudioOut::Guard guard(*g_audio);scratch_capture_locked();}
    else{scratch_frame={};scratch_cancel_all();}
    auto& f=scratch_frame;
    if(scratch_words.axis<0||!f.valid)return;
    f.menu_axis=scratch_words.axis;f.menu_count=scratch_words.count;f.menu_selected=scratch_words.selected;
    std::memcpy(f.menu_params,scratch_words.params,sizeof f.menu_params);
    const int anchor=f.menu_axis?306-ui::text_w(f.axis_labels[1]):sg::axis(0).x;
    int widest=0;for(int i=0;i<f.menu_count;++i)widest=std::max(widest,ui::text_w(scratch_names[f.menu_params[i]]));
    const int branch=std::clamp(anchor+8,14,306-widest-14);
    f.menu_layout={branch,branch+14,86,28,branch,306};
    // Capture the actual receding native sheet once, AFTER the audio guard.
    if(scratch_words.opening){const auto saved=f;scratch_frame=scratch_words.background;
        auto& cache=sigil_surface::bottom_cache;
        for(int y=0;y<sigil_surface::H;++y)for(int x=0;x<sigil_surface::W;++x){uint32_t color=0xff000000u;
            for(unsigned shift:{0u,8u,16u}){
                unsigned sum=0;
                for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx)
                    sum+=(m::sample(false,float(x*2+dx),float(y*2+dy))>>shift)&255;
                color|=((sum+2)/4)<<shift;
            }
            cache.pixels[y*sigil_surface::W+x]=color;}
        scratch_bottom(sigil_surface::Painter{&cache});cache.finish();scratch_frame=saved;}
    f.menu_motion=word_menu::capture(scratch_words.motion,osGetTime(),unsigned(f.menu_axis+1),f.menu_selected,f.menu_layout,
        [&](int i){return scratch_names[f.menu_params[std::clamp(i,0,f.menu_count-1)]];},scratch_words.opening);
    scratch_words.opening=false;
}
void scratch_focus_draw(const SC&){const auto& f=scratch_frame;m::paper(320);if(!f.valid)return;
    if(f.menu_axis<0){scratch_bottom(sigil_surface::Painter{});return;}
    sigil_surface::bottom_cache.draw();const sigil_surface::Painter p{};
    const int anchor=f.menu_axis?306-ui::text_w(f.axis_labels[1]):sg::axis(0).x;
    p.text(anchor,66,f.axis_labels[f.menu_axis],m::spot());
    word_menu::draw(p,f.menu_layout,f.menu_motion,f.menu_count,f.menu_selected,[&](int i){return scratch_names[f.menu_params[i]];},
        [](int,int,unsigned,bool){},m::ink(),m::spot(),m::branch());
}
bool scratch_focus_draw_top(){const auto& f=scratch_frame;if(!f.valid)return false;m::paper(400);
    module_heading::header(UnitKind::Scratch,m::spot());m::header_text(76,270,f.source_name[0]?f.source_name:"no source",m::ink());
    char text[64];std::snprintf(text,sizeof text,"ch%02d / %02d",f.chain+1,f.slot+1);scratch_right(386,m::kHeaderTextY,text,m::ghost());
    const int ax=f.axis_x,ay=f.axis_y;std::snprintf(text,sizeof text,"%s / %s + %s",f.mode?"hold":"scratch",scratch_names[ax],scratch_names[ay]);ui::text(14,38,text,m::ink());
    if(f.loaded){if(f.window_left>=0&&f.window_right>=f.window_left)ui::rect(f.window_left,62,f.window_right-f.window_left+1,96,(m::spot()&0xffffffu)|(22u<<24));
        for(int x=0;x<372;++x)ui::line(14+x,f.high[x],14+x,f.low[x],m::ink());
        if(f.top_cursor>=0)ui::line(f.top_cursor,60,f.top_cursor,158,m::spot());}
    else ui::text(14,112,"choose a source below",m::ghost());
    const auto& d=f.params[f.selected];char value[24];scratch_focus_value_text(value,sizeof value,f.selected,d.effective);
    std::snprintf(text,sizeof text,"%s / live %s",scratch_names[f.selected],value);ui::text(14,178,text,m::spot());
    scratch_focus_value_text(value,sizeof value,f.selected,d.control);std::snprintf(text,sizeof text,"control %s",value);ui::text(14,194,text,m::ghost());
    scratch_focus_value_text(value,sizeof value,f.selected,d.target);std::snprintf(text,sizeof text,"target %s",value);scratch_right(386,194,text,m::ghost());
    scratch_route(text,sizeof text,f.selected);ui::text(14,210,text,m::ghost());
    m::footer(400,f.bypass?"bypassed":f.mute?"muted":!f.loaded?"empty source":f.visual.active?"actual source head / center":"no voice",
        "source head / target cross");return true;
}
} // namespace seance::platform::ui3ds
