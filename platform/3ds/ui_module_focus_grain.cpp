#include "ui_module_focus_internal.h"
#include "ui_state.h"
#include "ui_theme.h"
#include "ui_palette.h"
#include "ui_param_lens.h"
#include "module_inspector.h"
#include "editor_style.h"
#include "module_heading.h"
#include "gran_ui.h"
#include "audio_out.h"
#include "../../core/ui/draw.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <type_traits>

namespace seance::platform::ui3ds {
namespace {
namespace m=editor_style;
namespace geo=gran_ui;
using C=GrainUnit;
using Bank=trackr::synth::SampleBank;
constexpr int PARAMS[2][6]={{C::P_POS,C::P_SIZE,C::P_SPRAY,C::P_PITCH,C::P_GAIN,C::P_SCAN},
    {C::P_DENS,C::P_JITTER,C::P_VOICES,C::P_ATTACK,C::P_RELEASE,C::P_PITCH_DEV}};
constexpr const char* NAMES[C::P_COUNT]={"position","size","spray","speed","gain","density","jitter","attack","release","p.dev","voices","scan"};
constexpr const char* MODES[]={"flow","gate","burst"};
constexpr const char* PADS[]={"restart","hold","burst"};
constexpr const char* TABS[]={"grain","cloud","setup"};
struct HeadPlot {int32_t x[17]={},cursor=0;uint8_t y[17]={},alpha=0;};
template<int N> struct Plot {
    int32_t pos=14,anchor=14,left=14,right=14,crop_left=14,crop_right=14;
    uint8_t low[N]={},high[N]={};
    HeadPlot heads[C::MAX_GRAINS];
    char times[3][20]={};
};
struct Frame {
    bool valid=false,loaded=false,bypass=false,mute=false,lock=false,full=false,sync=false,clean=false,fine=false,lens=false,gate=false,pad=false;
    uint8_t zoom=4,page=0,selected=C::P_SIZE,mode=0,window=0,count=4,remaining=0,division=0,active=0,limit=1;
    int8_t chain=0,slot=0;
    uint16_t mod_links=0;
    uint32_t frames=0,view_start=0,view_span=0;
    q15 edit[C::P_COUNT]={},live[C::P_COUNT]={};
    ParamInspector route{};
    char values[C::P_COUNT][20]={},live_value[24]={},name[28]={},duration[20]={};
    uint8_t envelope[33]={};
    Plot<373> top;
    Plot<293> bottom;
} frame;
static_assert(sizeof(Frame)<=4096 && std::is_trivially_copyable<Frame>::value,
              "bounded frozen GRAN frame; no engine/PCM pointers in draw");
struct Gesture {
    uintptr_t unit=0,data=0;
    Engine* engine=nullptr;
    platform::AudioOut* audio=nullptr;
    int chain=-1,slot=-1,step=-1,lock_chain=-1,bank=-1,sample=-1,channels=0;
    int param=-1,action=-1,x=0,y=0,last_x=0,last_y=0;
    q15 value=0;
    View view=View::Chains;
    bool locked=false,clean=false,fine=false,wave=false,pad=false,bypass=false,mute=false;
    C::LaunchMode mode=C::LaunchMode::Flow;
    C::Window window=C::Window::Linear;
    uint32_t token=0,generation=0,frames=0,span=0,start=0;
    geo::Box box{};
} gesture;
int selected=C::P_SIZE,page=0,zoom=4;
bool fine=false,blocked_frame=false;
uintptr_t view_unit=0;Engine* view_engine=nullptr;int view_chain=-1,view_slot=-1;
enum Action {Scan=20,Fine,Bypass,Source,Window,Count,Zoom,Clock,Upgrade};
int clamp(int v,int lo=0,int hi=32767){return std::max(lo,std::min(hi,v));}
bool grain_page(int p){return p<=C::P_GAIN||p==C::P_SCAN;}
bool bipolar(int p){return p==C::P_PITCH||p==C::P_SCAN;}
bool context(){return g_eng&&g_audio&&g_overlay==Overlay::Editor&&
    (g_view==View::Chains||(g_view==View::Sequencer&&g_seq_lock_armed))&&
    g_edit_chain>=0&&g_edit_chain<NUM_CHAINS&&g_edit_slot>=0&&g_edit_slot<UNITS_PER_CHAIN;}
bool locked(){return g_seq_lock_armed&&g_seq_lock_chain==g_edit_chain&&g_seq_lock_step>=0&&g_seq_lock_step<SEQ_STEPS;}
C* current(){if(!context())return nullptr;Unit*u=g_eng->chains[g_edit_chain].units[g_edit_slot];return u&&u->kind==UnitKind::Grain?static_cast<C*>(u):nullptr;}
void view_context(const C&u){if(view_engine!=g_eng||view_unit!=uintptr_t(&u)||view_chain!=g_edit_chain||view_slot!=g_edit_slot){
    fine=false;zoom=4;page=grain_page(selected)?0:1;view_engine=g_eng;view_unit=uintptr_t(&u);view_chain=g_edit_chain;view_slot=g_edit_slot;}}
q15 edit_value(const C&u,int p){return locked()?seq_display_value(g_edit_slot,p,u.params[p].base):u.params[p].base;}
bool owns(const C&u){
    if(!context()||param_lens_is_open()||gesture.engine!=g_eng||gesture.audio!=g_audio||gesture.unit!=uintptr_t(&u)||
       gesture.chain!=g_edit_chain||gesture.slot!=g_edit_slot||gesture.view!=g_view||gesture.locked!=g_seq_lock_armed||
       gesture.clean!=u.clean||gesture.mode!=u.launch_mode||gesture.window!=u.window||g_drag!=Drag::GrainPosition)return false;
    if(g_eng->chains[g_edit_chain].units[g_edit_slot]!=&u)return false;
    if(gesture.locked&&(!locked()||gesture.step!=g_seq_lock_step||gesture.lock_chain!=g_seq_lock_chain||gesture.bank!=g_eng->active_pattern))return false;
    if(gesture.sample!=u.sample_slot)return false;
    if(u.sample_slot>=0&&u.sample_slot<trackr::synth::SAMPLE_BANK_SIZE){const auto&s=Bank::instance().slot(u.sample_slot);
        if(gesture.generation!=Bank::instance().generation(u.sample_slot)||gesture.data!=uintptr_t(s.data.data())||
            gesture.frames!=s.playable_frames()||gesture.channels!=s.channels)return false;}
    if(gesture.pad&&(u.bypass||g_eng->chains[g_edit_chain].mute))return false;
    return true;
}
void begin(C&u,int x,int y){
    gesture={};gesture.unit=uintptr_t(&u);gesture.engine=g_eng;gesture.audio=g_audio;gesture.chain=g_edit_chain;gesture.slot=g_edit_slot;
    gesture.view=g_view;gesture.locked=g_seq_lock_armed;gesture.step=g_seq_lock_step;gesture.lock_chain=g_seq_lock_chain;
    gesture.bank=g_eng->active_pattern;gesture.clean=u.clean;gesture.mode=u.launch_mode;gesture.window=u.window;
    gesture.x=gesture.last_x=x;gesture.y=gesture.last_y=y;gesture.fine=fine;gesture.sample=u.sample_slot;
    if(u.sample_slot>=0&&u.sample_slot<trackr::synth::SAMPLE_BANK_SIZE){const auto&s=Bank::instance().slot(u.sample_slot);
        gesture.frames=s.playable_frames();gesture.channels=s.channels;gesture.data=uintptr_t(s.data.data());gesture.generation=Bank::instance().generation(u.sample_slot);}
    g_drag=Drag::GrainPosition;
}
void arm(C&u,int x,int y,int action,geo::Box box){begin(u,x,y);gesture.action=action;gesture.box=box;}
void change(C&u,int p,q15 value){if(value==edit_value(u,p))return;
    if(g_seq_lock_armed){if(locked())seq_record_lock(g_edit_slot,p,value);}else{u.params[p].base=value;mark_project_mutated();}}
void right(int x,int y,const char*t,u32 c){ui::text(x-ui::text_w(t),y,t,c);}
void seconds(char*out,size_t n,uint32_t f){std::snprintf(out,n,"%.2fs",double(f)/ENGINE_SR);}
void marker(int x,int y,u32 c){ui::line(x-2,y,x,y-3,c);ui::line(x,y-3,x+2,y,c);ui::line(x+2,y,x,y+3,c);ui::line(x,y+3,x-2,y,c);}
void clipped(int x0,int y0,int x1,int y1,int right,u32 c){
    if(x0>x1){std::swap(x0,x1);std::swap(y0,y1);}if(x1<14||x0>right)return;
    const int64_t dx=int64_t(x1)-x0,dy=y1-y0;const int a=std::max(14,x0),b=std::min(right,x1);
    const int ya=dx?y0+int((int64_t(a)-x0)*dy/dx):y0,yb=dx?y0+int((int64_t(b)-x0)*dy/dx):y1;
    ui::line(a,ya,b,yb,c);
}
template<int N> int project(int64_t at,uint32_t first,uint32_t span){return 14+int((at-int64_t(first)*65536)*(N-1)/(int64_t(span)*65536));}
template<int N> void capture_plot(Plot<N>&p,const trackr::synth::Sample&s,uint32_t first,uint32_t span,
                                  uint32_t pos,uint32_t anchor,uint32_t spray,const C::VisualGrain(&heads)[8],bool top){
    const int zero=top?112:115,amp=top?30:26;
    const auto px=[&](int64_t q){return project<N>(q,first,span);};
    p.pos=px(int64_t(pos)*65536);p.anchor=px(int64_t(anchor)*65536);
    p.left=clamp(px((int64_t(pos)-spray)*65536),14,14+N-1);p.right=clamp(px((int64_t(pos)+spray)*65536),14,14+N-1);
    p.crop_left=px(int64_t(frame.view_start)*65536);p.crop_right=px(int64_t(frame.view_start+frame.view_span)*65536);
    for(int x=0;x<N;++x){const uint32_t a=first+uint64_t(x)*span/N,b=first+uint64_t(x+1)*span/N,n=std::min<uint32_t>(8,std::max<uint32_t>(1,b-a));
        int lo=32767,hi=-32768;for(uint32_t j=0;j<n;++j){const uint32_t at=std::min<uint32_t>(frame.frames-1,a+(n>1?uint64_t(j)*(b-a-1)/(n-1):0));
            for(int ch=0;ch<s.channels;++ch){const int v=s.data[size_t(at)*s.channels+ch];lo=std::min(lo,v);hi=std::max(hi,v);}}
        p.low[x]=zero-lo*amp/32768;p.high[x]=zero-hi*amp/32768;}
    for(int i=0;i<8;++i){const auto&h=heads[i];if(!h.active||!h.length)continue;auto&g=p.heads[i];
        g.alpha=frame.bypass||frame.mute?0:uint32_t(h.envelope)*190/32767;g.cursor=px(h.position_q16);
        for(int j=0;j<17;++j){g.x[j]=px(h.start_q16+(h.end_q16-h.start_q16)*j/16);
            g.y[j]=uint32_t(C::visual_window_q15(uint64_t(h.length-1)*j/16,h.length,h.attack,h.release,h.window))*12/32767;}}
    for(int i=0;i<3;++i)seconds(p.times[i],sizeof p.times[i],first+uint64_t(i)*span/2);
    if(!top){char t[20];std::snprintf(t,sizeof t,"%.2fs / x%u",double(first+span/2)/ENGINE_SR,unsigned(frame.zoom));std::memcpy(p.times[1],t,sizeof t);}
}
template<int N> void draw_plot(const Plot<N>&p,bool top){
    if(!frame.loaded){ui::text(14,115,"no source / tap sample",m::ghost());return;}
    const int end=14+N-1,zero=top?112:115,base=top?99:104,bracket=top?65:85;
    if(top){const int left=clamp(p.crop_left,14,end),right=clamp(p.crop_right,14,end);
        ui::rect(left,69,right-left+1,80,(m::spot()&0xffffffu)|(14u<<24));ui::line(left,70,left,148,palette::shadow());ui::line(right,70,right,148,palette::shadow());}
    ui::line(14,zero,end,zero,palette::shadow());
    for(int x=0;x<N;++x)ui::line(14+x,p.high[x],14+x,p.low[x],14+x>=p.left&&14+x<=p.right?m::ink():m::ghost());
    ui::line(p.left,bracket,p.right,bracket,m::spot());ui::line(p.left,bracket,p.left,bracket+2,m::spot());ui::line(p.right,bracket,p.right,bracket+2,m::spot());
    if(p.pos>=14&&p.pos<=end)for(int i=0;i<3;++i)clipped(p.pos-2+i,bracket-5+i,p.pos+2-i,bracket-5+i,end,m::spot());
    for(const auto&h:p.heads){if(!h.alpha)continue;const u32 c=(m::spot()&0xffffffu)|(uint32_t(h.alpha)<<24);
        for(int j=1;j<17;++j)clipped(h.x[j-1],base-h.y[j-1],h.x[j],base-h.y[j],end,c);
        if(h.cursor>=14&&h.cursor<=end){ui::line(h.cursor,base,h.cursor,base+20,c);clipped(h.cursor-2,base-1,h.cursor,base+2,end,c);clipped(h.cursor,base+2,h.cursor+2,base-1,end,c);}}
    if(p.anchor>=14&&p.anchor<=end)marker(p.anchor,top?145:139,m::ink());
    const int y=top?154:145;for(int i=0;i<3;++i){const int x=14+i*(N-1)/2;ui::line(x,y,x,y+2,palette::shadow());
        const int xx=x-(i==1?ui::text_w(p.times[i])/2:i==2?ui::text_w(p.times[i]):0);ui::text(xx,top?160:149,p.times[i],m::ghost());}
}
bool dormant(int p){return (p==C::P_SCAN&&!frame.clean)||(p==C::P_DENS&&frame.sync)||
    (frame.clean&&frame.window==1&&(p==C::P_ATTACK||p==C::P_RELEASE));}
} // namespace

int grain_focus_selected(){return selected;}
void grain_focus_select(int p){if(p<0||p>=C::P_COUNT)return;if(p!=selected)grain_focus_release();selected=p;page=grain_page(p)?0:1;}
void grain_focus_release(){
    // A saved pointer is never dereferenced after replacement; find current ownership.
    if(gesture.token&&g_audio&&g_eng==gesture.engine){platform::AudioOut::Guard guard(*g_audio);
        for(auto&ch:g_eng->chains)for(Unit*u:ch.units)if(u&&uintptr_t(u)==gesture.unit&&u->kind==UnitKind::Grain)static_cast<C*>(u)->pad_end(gesture.token);}
    gesture={};
    if(g_drag==Drag::GrainPosition)g_drag=Drag::None;
}
void grain_focus_input(u32 down,u32 held,bool blocked){
    blocked_frame=blocked||(down&~(KEY_TOUCH|KEY_ZL));
    if(!gesture.unit)return;
    if(blocked_frame||!context()||param_lens_is_open()){grain_focus_release();return;}
    platform::AudioOut::Guard guard(*g_audio);C*u=current();
    if(!u||!owns(*u)||(gesture.pad&&!(held&KEY_TOUCH)))grain_focus_release();
}
bool grain_focus_touch_header(C&,int,int){return false;}
bool grain_focus_touch_body(C&u,int x,int y){
    grain_focus_release();if(!context()||param_lens_is_open()||blocked_frame)return false;
    platform::AudioOut::Guard guard(*g_audio);if(current()!=&u)return false;view_context(u);
    if(g_seq_lock_armed&&!locked())return true;
    if(geo::scan.has(x,y)){if(u.clean)arm(u,x,y,Scan,geo::scan);else if(!g_seq_lock_armed)arm(u,x,y,Upgrade,geo::scan);return true;}
    if(geo::fine.has(x,y)){arm(u,x,y,Fine,geo::fine);return true;}
    if(geo::bypass.has(x,y)){if(!g_seq_lock_armed)arm(u,x,y,Bypass,geo::bypass);return true;}
    for(int i=0;i<3;++i){if(geo::mode(i).has(x,y)){if(u.clean&&!g_seq_lock_armed)arm(u,x,y,i,geo::mode(i));return true;}
        if(geo::tab(i).has(x,y)){arm(u,x,y,10+i,geo::tab(i));return true;}}
    if(geo::pad.has(x,y)){
        if(u.clean&&!g_seq_lock_armed&&!u.bypass&&!g_eng->chains[g_edit_chain].mute&&u.sample_slot>=0&&u.sample_slot<trackr::synth::SAMPLE_BANK_SIZE){
            const auto&s=Bank::instance().slot(u.sample_slot);if(!s.empty()&&s.playable_frames()>=256){begin(u,x,y);gesture.pad=true;gesture.box=geo::pad;gesture.token=u.pad_begin();}}
        return true;
    }
    if(geo::source.has(x,y)){if(!g_seq_lock_armed)arm(u,x,y,Source,geo::source);return true;}
    if(geo::wave.has(x,y)){
        selected=C::P_POS;page=0;
        if(u.sample_slot<0||u.sample_slot>=trackr::synth::SAMPLE_BANK_SIZE)return true;
        const auto&s=Bank::instance().slot(u.sample_slot);if(s.empty()||!s.playable_frames())return true;
        begin(u,x,y);gesture.param=C::P_POS;gesture.value=edit_value(u,C::P_POS);gesture.wave=true;
        gesture.span=std::max<uint32_t>(1,(uint64_t(gesture.frames)+zoom-1)/zoom);
        const uint32_t pos=uint64_t(u.clean?u.scan_position():u.params[C::P_POS].effective)*gesture.frames>>15;
        gesture.start=pos>gesture.span/2?std::min(pos-gesture.span/2,gesture.frames-gesture.span):0;return true;
    }
    if(page==2){for(int i=0;i<4;++i)if(geo::setup(i).has(x,y)){
        const int action=i==0?Window:i==1?Count:i==2?Zoom:Clock;
        if(i==2||(!g_seq_lock_armed&&(i==3||u.clean)&&(i!=1||u.launch_mode==C::LaunchMode::Burst)))arm(u,x,y,action,geo::setup(i));
        return true;}return true;}
    for(int i=0;i<6;++i){if(!geo::param(i).has(x,y))continue;const int p=PARAMS[page][i];selected=p;
        const auto b=geo::param(i);if(locked()&&hit(x,y,b.x+3,b.y,ui::text_w(NAMES[p])+6,12)){
            if(g_eng->remove_step_lock(g_seq_lock_chain,g_seq_lock_step,g_edit_slot,p))mark_project_mutated();
            return true;}
        if((!u.clean&&p==C::P_SCAN)||(u.clean&&u.window==C::Window::Hann&&(p==C::P_ATTACK||p==C::P_RELEASE)))return true;
        begin(u,x,y);gesture.param=p;gesture.value=edit_value(u,p);return true;}
    return true;
}
void grain_focus_drag(C&u,int x,int y){
    if(!gesture.unit)return;
    if(!context()||param_lens_is_open()||blocked_frame){grain_focus_release();return;}
    platform::AudioOut::Guard guard(*g_audio);if(!owns(u)){grain_focus_release();return;}
    if(gesture.action>=0||gesture.pad){
        const int64_t dx=int64_t(x)-gesture.x,dy=int64_t(y)-gesture.y;
        if(!gesture.box.has(x,y)||(!gesture.pad&&dx*dx+dy*dy>64))grain_focus_release();
        return;
    }
    if(gesture.param<0||x==gesture.last_x)return;
    gesture.last_x=x;gesture.last_y=y;
    const int p=gesture.param,sensitivity=gesture.fine?32:1;
    const int64_t delta=gesture.wave?int64_t(x-gesture.x)*32768*gesture.span/(int64_t(292)*gesture.frames*sensitivity):
        int64_t(x-gesture.x)*(bipolar(p)?65534:32767)/(84*sensitivity);
    change(u,p,q15(std::max<int64_t>(bipolar(p)?-32767:0,std::min<int64_t>(32767,int64_t(gesture.value)+delta))));
}
void grain_focus_touch_release(){
    C*browser=nullptr;
    if(gesture.unit&&context()&&!param_lens_is_open()&&!blocked_frame){platform::AudioOut::Guard guard(*g_audio);C*u=current();
        if(u&&owns(*u)){const int a=gesture.action;bool changed=false;
            if(a>=0&&a<3&&!g_seq_lock_armed&&u->clean){const auto mode=C::LaunchMode(a);changed=u->launch_mode!=mode;if(changed)u->set_launch_mode(mode);}
            else if(a>=10&&a<13){page=a-10;if(page<2&&grain_page(selected)!=(page==0))selected=page?C::P_DENS:C::P_SIZE;}
            else if(a==Fine)fine=!fine;
            else if(a==Scan){selected=C::P_SCAN;page=0;change(*u,C::P_SCAN,0);}
            else if(a==Zoom)zoom=zoom==16?1:zoom*2;
            else if(!g_seq_lock_armed){
                if(a==Bypass){u->bypass=!u->bypass;changed=true;}
                else if(a==Upgrade&&!u->clean){u->clean=true;u->reset();u->control_tick();changed=true;}
                else if(a==Window&&u->clean){u->set_window(u->window==C::Window::Linear?C::Window::Hann:C::Window::Linear);changed=true;}
                else if(a==Count&&u->clean&&u->launch_mode==C::LaunchMode::Burst){u->set_burst_count(u->burst_count>=16?1:u->burst_count+1);changed=true;}
                else if(a==Clock){if(!u->clock_sync){u->clock_sync=true;u->sync_division=0;}else if(u->sync_division>=3){u->clock_sync=false;u->sync_division=0;}else ++u->sync_division;
                    u->set_transport_bpm_q16(g_eng->effective_bpm_q16());changed=true;}
                else if(a==Source)browser=u;
            }
            if(changed)mark_project_mutated();
        }
    }
    grain_focus_release();
    if(browser&&context()&&!g_seq_lock_armed)module_focus_touch_common_header(*browser,150,10); // browser I/O outside guard
}
void grain_focus_capture(){
    frame={};if(!context()){grain_focus_release();if(!param_lens_is_open()){view_unit=0;view_engine=nullptr;fine=false;zoom=4;}return;}
    C::VisualGrain heads[8]={};
    {platform::AudioOut::Guard guard(*g_audio);C*u=current();if(!u){grain_focus_release();return;}view_context(*u);
        if(gesture.unit&&(!owns(*u)||param_lens_is_open()))grain_focus_release();
        const auto&ch=g_eng->chains[g_edit_chain];frame.valid=true;frame.bypass=u->bypass;frame.mute=ch.mute;frame.lock=g_seq_lock_armed;frame.full=frame.lock&&g_seq_lock_flash>0;
        frame.chain=g_edit_chain;frame.slot=g_edit_slot;frame.page=page;frame.selected=selected;frame.clean=u->clean;frame.fine=fine;frame.zoom=zoom;frame.lens=param_lens_is_open();
        frame.mode=u->clean?clamp(int(u->launch_mode),0,2):0;frame.window=u->clean?clamp(int(u->window),0,1):0;
        frame.count=clamp(u->burst_count,1,16);frame.remaining=u->burst_remaining();frame.gate=u->gate_open();frame.pad=gesture.pad&&gesture.unit==uintptr_t(u);
        frame.sync=u->clock_sync;frame.division=std::min(3,int(u->sync_division));frame.limit=C::voice_limit(u->params[C::P_VOICES].effective);frame.mod_links=focus_mod_links(*u);
        for(int p=0;p<C::P_COUNT;++p){frame.edit[p]=edit_value(*u,p);frame.live[p]=u->params[p].effective;}
        g_eng->capture_param_inspector(g_edit_chain,g_edit_slot,selected,frame.route);
        if(u->sample_slot>=0&&u->sample_slot<trackr::synth::SAMPLE_BANK_SIZE){const auto&s=Bank::instance().slot(u->sample_slot);frame.frames=s.playable_frames();
            frame.loaded=!s.empty()&&frame.frames&&(s.channels==1||s.channels==2);
            const auto&prov=Bank::instance().provenance(u->sample_slot);const char*name=std::strrchr(prov.path,'/');name=name?name+1:prov.path;
            std::snprintf(frame.name,sizeof frame.name,"s%02u / %.20s",unsigned(u->sample_slot+1),name[0]?name:frame.loaded?"recording":"empty");
            if(frame.loaded){frame.view_span=std::max<uint32_t>(1,(uint64_t(frame.frames)+zoom-1)/zoom);
                const uint32_t pos=uint64_t(clamp(u->clean?u->scan_position():frame.live[C::P_POS]))*frame.frames>>15;
                const uint32_t anchor=uint64_t(clamp(frame.edit[C::P_POS]))*frame.frames>>15,spray=uint64_t(clamp(frame.live[C::P_SPRAY]))*frame.frames>>16;
                frame.view_start=pos>frame.view_span/2?std::min(pos-frame.view_span/2,frame.frames-frame.view_span):0;
                if(gesture.wave){frame.view_span=gesture.span;frame.view_start=gesture.start;}
                u->capture_visual_grains(heads);for(const auto&h:heads)frame.active+=h.active;
                capture_plot(frame.top,s,0,frame.frames,pos,anchor,spray,heads,true);
                capture_plot(frame.bottom,s,frame.view_start,frame.view_span,pos,anchor,spray,heads,false);
            }
        }
    }
    for(int p=0;p<C::P_COUNT;++p){if(p==C::P_SCAN&&!frame.clean)std::snprintf(frame.values[p],sizeof frame.values[p],"v2 only");
        else module_inspector_value_text(frame.values[p],sizeof frame.values[p],UnitKind::Grain,p,frame.edit[p]);}
    module_inspector_value_text(frame.live_value,sizeof frame.live_value,UnitKind::Grain,selected,frame.live[selected]);
    seconds(frame.duration,sizeof frame.duration,frame.frames);
    const uint32_t len=C::grain_length_frames(frame.live[C::P_SIZE]),attack=uint64_t(len)*uint16_t(frame.live[C::P_ATTACK])>>16,release=uint64_t(len)*uint16_t(frame.live[C::P_RELEASE])>>16;
    for(int j=0;j<33;++j)frame.envelope[j]=uint32_t(C::visual_window_q15(uint64_t(len-1)*j/32,len,attack,release,C::Window(frame.window)))*15/32767;
}
void grain_focus_draw(const C&){
    m::paper(320);if(!frame.valid)return;char text[64];std::snprintf(text,sizeof text,"ch%02d / %02d",frame.chain+1,frame.slot+1);module_heading::header(UnitKind::Grain,m::spot());ui::text(60,m::kHeaderTextY,text,m::ghost());
    ui::text(126,m::kHeaderTextY,frame.clean?"scan 0":"old >",frame.clean?m::ghost():m::spot());ui::text(frame.fine?178:190,m::kHeaderTextY,frame.fine?"fine /32":"wide",m::ghost());
    ui::text(242,m::kHeaderTextY,"byp",frame.bypass?m::spot():m::ghost());right(m::right_edge(320),m::kHeaderTextY,"x del",m::ghost());if(frame.bypass)ui::line(242,m::kHeaderMarkY,258,m::kHeaderMarkY,m::spot());
    for(int i=0;i<3;++i){const int x=14+62*i;geo::word(x,49,MODES[i],frame.mode==i?m::spot():m::ghost());if(frame.mode==i)ui::line(x,54,x+28,54,m::spot());}
    const bool enabled=frame.clean&&!frame.lock&&!frame.bypass&&!frame.mute&&frame.loaded&&frame.frames>=256;
    if(frame.pad)ui::rect(214,26,98,30,(m::spot()&0xffffffu)|(22u<<24));
    geo::word(263,49,PADS[frame.mode],enabled?m::spot():m::ghost(),true);ui::line(220,54,306,54,frame.pad?m::spot():palette::shadow());
    ui::text(14,65,frame.name[0]?frame.name:"no sample",m::ink());
    for(int i=0;i<3;++i){const int x=188+42*i;ui::text(x,65,TABS[i],frame.page==i?m::spot():m::ghost());if(frame.page==i)ui::line(x,77,x+28,77,m::spot());}
    draw_plot(frame.bottom,false);
    if(frame.page<2){for(int i=0;i<6;++i){const int p=PARAMS[frame.page][i],x=14+(i%3)*104,y=165+(i/3)*34;const bool quiet=dormant(p);const u32 c=quiet?m::ghost():frame.selected==p?m::spot():m::ink();
        ui::text(x,y,frame.sync&&p==C::P_DENS?"dens free":NAMES[p],c);
        right(x+84,y+12,frame.window==1&&(p==C::P_ATTACK||p==C::P_RELEASE)?"stored":frame.values[p],frame.selected==p&&!quiet?c:m::ghost());
        const auto at=[&](q15 v){return x+(clamp(v,bipolar(p)?-32767:0)+(bipolar(p)?32767:0))*84/(bipolar(p)?65534:32767);};
        module_fader::horizontal(x,x+84,y+25,at(frame.edit[p]),at(frame.live[p]),{bool(frame.mod_links&(1u<<p)),quiet,frame.bypass||frame.mute},bipolar(p)?at(p==C::P_PITCH?8192:0):-1,true);
    }}else{
        const char*labels[]={"window","burst count","zoom","clock"};const char*clocks[]={"1/8","1/16","1/32","1/64"};
        for(int i=0;i<4;++i){const int x=14+(i%2)*156,y=165+(i/2)*34;const bool enable=i==2||(!frame.lock&&(i==3||frame.clean)&&(i!=1||frame.mode==2));
            ui::text(x,y,labels[i],enable?m::ink():m::ghost());
            if(i==0)std::snprintf(text,sizeof text,"%s%s",frame.window?"hann":"linear",enable?" >":"");
            if(i==1)std::snprintf(text,sizeof text,"%u%s",unsigned(frame.count),frame.mode==2?"":" / stored");
            if(i==2)std::snprintf(text,sizeof text,"x%u >",unsigned(frame.zoom));
            if(i==3)std::snprintf(text,sizeof text,"%s%s",frame.sync?clocks[frame.division]:"free",enable?" >":"");
            right(x+136,y+12,text,i==0&&enable?m::spot():m::ghost());ui::line(x,y+25,x+136,y+25,palette::shadow());}
    }
    if(frame.lock)std::snprintf(text,sizeof text,frame.full?"lock full":"p-lock / name: remove");
    else if(frame.bypass||frame.mute)std::snprintf(text,sizeof text,frame.bypass?"paused":"muted / scan runs");
    else if(!frame.clean)std::snprintf(text,sizeof text,"legacy / old >: v2");
    else if(frame.window==1&&frame.page==1)std::snprintf(text,sizeof text,"hann / atk + rel stored");
    else std::snprintf(text,sizeof text,"wave: position / pad: %s",PADS[frame.mode]);
    ui::text(m::kLeft,m::kTextY,text,m::ghost());right(m::right_edge(320),m::kTextY,frame.lock?"b back":"l: mod >",m::ghost());
}
bool grain_focus_draw_top(){
    if(!frame.valid||!frame.clean||frame.lock||frame.lens)return false;
    m::paper(400);module_heading::header(UnitKind::Grain,m::spot());ui::text(92,m::kHeaderTextY,"sample cloud",m::ghost());
    char text[64];std::snprintf(text,sizeof text,"ch%02d / %02d",frame.chain+1,frame.slot+1);right(m::right_edge(400),m::kHeaderTextY,text,m::ghost());
    ui::text(14,47,frame.name[0]?frame.name:"no sample",m::ink());
    if(frame.bypass||frame.mute)std::snprintf(text,sizeof text,frame.bypass?"paused":"muted / scan runs");
    else if(frame.mode==2)std::snprintf(text,sizeof text,"burst / %u left",unsigned(frame.remaining));
    else std::snprintf(text,sizeof text,"%s / %s",MODES[frame.mode],frame.mode==0?"continuous":frame.gate?"held":"waiting");
    right(386,47,text,m::spot());draw_plot(frame.top,true);
    std::snprintf(text,sizeof text,"source / %s",frame.duration);ui::text(14,177,text,m::ghost());
    std::snprintf(text,sizeof text,"shade: lower zoom x%u",unsigned(frame.zoom));right(386,177,text,m::ghost());
    std::snprintf(text,sizeof text,"%s / base",NAMES[frame.selected]);ui::text(14,193,text,m::spot());
    geo::number(14,230,frame.values[frame.selected],m::ink(),m::ghost());ui::text(174,193,"window",m::ghost());
    for(int j=1;j<33;++j)ui::line(174+(j-1)*2,224-frame.envelope[j-1],174+j*2,224-frame.envelope[j],m::spot());
    ui::text(250,215,frame.window?"hann":"linear",m::ghost());
    if(dormant(frame.selected))std::snprintf(text,sizeof text,"stored / not used");
    else std::snprintf(text,sizeof text,"live %.18s",frame.live_value);
    right(386,193,text,m::spot());
    const auto&d=frame.route;
    if(d.source==ModSource::None)std::snprintf(text,sizeof text,"mod off");
    else if(d.source==ModSource::EnvFollow)std::snprintf(text,sizeof text,"env%d%s",d.env_chain+1,d.mod_enabled?"":" muted");
    else std::snprintf(text,sizeof text,"sg.%c%s",'a'+mod_source_sigil(d.source),d.mod_enabled?"":" muted");
    right(386,207,text,m::ghost());
    char scan[24];module_inspector_value_text(scan,sizeof scan,UnitKind::Grain,C::P_SCAN,frame.live[C::P_SCAN]);
    std::snprintf(text,sizeof text,"%u/%u grains / scan %s",unsigned(frame.active),unsigned(frame.limit),scan);right(m::right_edge(400),m::kTextY,text,m::ghost());return true;
}
} // namespace seance::platform::ui3ds
