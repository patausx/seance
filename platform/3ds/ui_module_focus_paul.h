// Private production controller/render owner, included once by ui_module_focus.cpp.
#pragma once
#include "ui_module_focus_internal.h"
#include "paul_ui.h"
#include "paul_readout.h"
#include "ui_state.h"
#include "ui_theme.h"
#include "ui_param_lens.h"
#include "ui_sample_library.h"
#include "ui_sequencer.h"
#include "module_heading.h"
#include "editor_style.h"
#include "audio_out.h"
#include <cmath>
#include <cstdio>
#include <cstring>
namespace seance::platform::ui3ds {
namespace {
namespace m = editor_style;
namespace pg = paul_ui;
using PU=PaulUnit;using PB=trackr::synth::SampleBank;
pg::Frame paul_frame;
pg::SpectrumCache paul_spectrum_cache;
constexpr const char* paul_names[]={"position","stretch","window","freeze","gain","pitch","gate","start","end","loop"};
int paul_selected=PU::P_STRETCH,paul_page=0;
bool paul_wait_lift=false,paul_blocked=false;
uintptr_t paul_view_unit=0;Engine* paul_view_engine=nullptr;int paul_view_chain=-1,paul_view_slot=-1;
struct PaulTouch {
    Engine* engine=nullptr;platform::AudioOut* audio=nullptr;uintptr_t unit=0;
    int chain=-1,slot=-1,sample=-1,bank=-1,page=0,param=-1,action=-1,x=0,y=0;
    uint32_t generation=0;View view=View::Chains;bool bypass=false,mute=false,busy=false;
    PU::Mode mode=PU::Mode::Flow;PU::Direction direction=PU::Direction::Forward;
    pg::Box box{};q15 origin=0;bool absolute=false;
} paul_touch;
enum PaulAction {PSource=20,PSetup,PFreeze,PFlow,PGate,PMod,PBack,PToggle,PDirection};
bool paul_loaded(const PU& u);
bool paul_direction_allowed(const PU& u);
bool paul_context(){return g_eng&&g_audio&&g_overlay==Overlay::Editor&&
    g_view==View::Chains&&
    g_edit_chain>=0&&g_edit_chain<NUM_CHAINS&&g_edit_slot>=0&&g_edit_slot<UNITS_PER_CHAIN;}
PU* paul_current(){if(!paul_context())return nullptr;auto* u=g_eng->chains[g_edit_chain].units[g_edit_slot];
    return u&&u->kind==UnitKind::Paul?static_cast<PU*>(u):nullptr;}
uint32_t paul_generation(int s){return s>=0&&s<trackr::synth::SAMPLE_BANK_SIZE?PB::instance().generation(s):0;}
q15 paul_edit(const PU& u,int p){return u.params[p].base;}
bool paul_quiet(const PU& u){return u.bypass||g_eng->chains[g_edit_chain].mute||g_eng->recording_active_or_pending();}
bool paul_parameter_allowed(const PU& u,int p){return !paul_quiet(u)&&
    !u.params[p].temp_override&&!u.params[p].step_lock_override;}
bool paul_owns(const PU& u){return paul_context()&&!param_lens_is_open()&&
    paul_touch.bypass==u.bypass&&paul_touch.mute==g_eng->chains[g_edit_chain].mute&&
    paul_touch.busy==g_eng->recording_active_or_pending()&&
    (!paul_quiet(u)||paul_touch.action==PBack||paul_touch.action==PSetup)&&
    paul_touch.engine==g_eng&&paul_touch.audio==g_audio&&paul_touch.unit==uintptr_t(&u)&&
    paul_touch.chain==g_edit_chain&&paul_touch.slot==g_edit_slot&&paul_touch.view==g_view&&
    paul_touch.page==paul_page&&paul_touch.mode==u.mode&&paul_touch.direction==u.direction&&
    (paul_touch.action!=PDirection||paul_direction_allowed(u))&&
    paul_touch.sample==u.sample_slot&&paul_touch.generation==paul_generation(u.sample_slot)&&
    paul_touch.bank==g_eng->active_pattern&&
    (paul_touch.param<0||paul_parameter_allowed(u,paul_touch.param));}
void paul_cancel(){if(paul_touch.unit)paul_wait_lift=true;paul_touch={};if(g_drag==Drag::PaulParam)g_drag=Drag::None;}
void paul_begin(PU& u,int x,int y){paul_touch={};paul_touch.engine=g_eng;paul_touch.audio=g_audio;paul_touch.unit=uintptr_t(&u);
    paul_touch.chain=g_edit_chain;paul_touch.slot=g_edit_slot;paul_touch.sample=u.sample_slot;paul_touch.generation=paul_generation(u.sample_slot);
    paul_touch.bank=g_eng->active_pattern;paul_touch.page=paul_page;paul_touch.view=g_view;
    paul_touch.mode=u.mode;paul_touch.direction=u.direction;paul_touch.bypass=u.bypass;paul_touch.mute=g_eng->chains[g_edit_chain].mute;
    paul_touch.busy=g_eng->recording_active_or_pending();paul_touch.x=x;paul_touch.y=y;g_drag=Drag::PaulParam;}
void paul_arm(PU& u,int x,int y,int a,pg::Box b){paul_begin(u,x,y);paul_touch.action=a;paul_touch.box=b;}
void paul_write(PU& u,int p,q15 v){if(v==paul_edit(u,p))return;
    u.params[p].base=v;mark_project_mutated();}
bool paul_loaded(const PU& u){if(u.sample_slot<0||u.sample_slot>=trackr::synth::SAMPLE_BANK_SIZE)return false;
    const auto& s=PB::instance().slot(u.sample_slot);return !s.empty()&&s.playable_frames()&&(s.channels==1||s.channels==2);}
bool paul_direction_allowed(const PU& u){if(paul_quiet(u)||!paul_loaded(u))return false;
    // Configuration is not a lockable parameter and must not interrupt an
    // independent temporary/performance/audition/step-lock parameter owner.
    for(int p=0;p<PU::P_COUNT;++p)if(u.params[p].temp_override||u.params[p].performance_override||
        u.params[p].audition_override||u.params[p].step_lock_override)return false;
    return true;}
q15 paul_strip_position(const PU& u,int x){return pg::strip_position(x,PB::instance().slot(u.sample_slot).playable_frames(),
    u.params[PU::P_START].effective,u.params[PU::P_END].effective);}
void paul_right(int x,int y,const char* s,u32 c){ui::text(x-ui::text_w(s),y,s,c);}
void paul_word(pg::Box b,const char* s,bool on,bool dim){int x=b.x+(b.w-ui::text_w(s))/2;
    // Header words share the metadata baseline; body words retain their target.
    ui::text(x,b.y==8?m::kHeaderTextY:b.y+8,s,dim?m::ghost():on?m::spot():m::ink());
    if(on)ui::line(x,b.y+21,x+ui::text_w(s)-1,b.y+21,dim?m::ghost():m::spot());}
void paul_slider(int p,pg::Box b,bool compact){const auto& f=paul_frame;const auto& d=f.params[p];
    // Covered readout labels are omitted behind the shared MOD sheet; retain
    // the real underlying rail/geometry, not two colliding caption layers.
    if(!f.lens){ui::text(b.x,b.y,paul_names[p],f.selected==p?m::spot():m::ink());paul_right(b.x+b.w,b.y+(compact?10:0),f.values[p],m::ghost());}
    const int left=compact?b.x:b.x+12,travel=compact?128:268,y=b.y+(compact?24:24);
    module_fader::horizontal(left,left+travel,y,pg::parameter_x(f.edit[p],d.bipolar,left,travel),pg::parameter_x(d.effective,d.bipolar,left,travel),
        {d.source!=ModSource::None&&d.mod_enabled,!f.loaded||f.busy,f.bypass||f.mute},d.bipolar?pg::parameter_x(0,true,left,travel):-1,compact);}
} // namespace
namespace paul_ui {const Frame& frozen(){return paul_frame;}
#ifdef SEANCE_PAUL_UI_TEST
QolStats test_qol_stats(){return paul_spectrum_cache.stats;}
#endif
}
int paul_focus_selected(){return paul_selected;}
void paul_focus_select(int p){if(p<0||p>=PU::P_COUNT)return;if(p!=paul_selected)paul_cancel();paul_selected=p;}
void paul_focus_release(){paul_cancel();}
bool paul_focus_back(){paul_cancel();if(paul_page){paul_page=0;return true;}return false;}
void paul_focus_input(u32 down,u32 held,bool blocked){paul_blocked=blocked||((down|held)&~u32(KEY_TOUCH|KEY_ZL));
    if(!(held&KEY_TOUCH)){paul_wait_lift=false;if(paul_blocked)paul_cancel();return;}
    if(paul_blocked||!paul_context()||param_lens_is_open()){paul_cancel();paul_wait_lift=true;return;}
    if(paul_touch.unit){platform::AudioOut::Guard guard(*g_audio);auto* u=paul_current();if(!u||!paul_owns(*u))paul_cancel();}}
bool paul_focus_touch_body(PU& u,int x,int y){if(!paul_context()||param_lens_is_open()||paul_blocked||paul_wait_lift)return true;
    platform::AudioOut::Guard guard(*g_audio);if(paul_current()!=&u||paul_touch.unit)return true;
    if(pg::back.has(x,y)){paul_arm(u,x,y,PBack,pg::back);return true;}
    if(pg::setup.has(x,y)){paul_arm(u,x,y,PSetup,pg::setup);return true;}
    if(paul_quiet(u))return true;
    const auto direction_box=paul_page?pg::setup_direction:pg::direction;
    if(direction_box.has(x,y)){if(paul_direction_allowed(u))paul_arm(u,x,y,PDirection,direction_box);return true;}
    {if(pg::source.has(x,y)){paul_arm(u,x,y,PSource,pg::source);return true;}
        if(pg::mod.has(x,y)){paul_arm(u,x,y,PMod,pg::mod);return true;}
        if(paul_page){if(pg::flow.has(x,y)){paul_arm(u,x,y,PFlow,pg::flow);return true;}
            if(pg::gate.has(x,y)){paul_arm(u,x,y,PGate,pg::gate);return true;}}}
    if(paul_page){for(int p=0;p<PU::P_COUNT;++p){auto b=pg::parameter(p);if(!b.has(x,y))continue;paul_selected=p;
        if(!paul_parameter_allowed(u,p))return true;
        if(p==PU::P_FREEZE||p==PU::P_LOOP){paul_arm(u,x,y,PToggle,b);paul_touch.param=p;return true;}
        paul_begin(u,x,y);paul_touch.param=p;paul_touch.origin=paul_edit(u,p);paul_touch.box=b;return true;}return true;}
    if(!paul_loaded(u))return true;
    if(pg::freeze.has(x,y)&&paul_parameter_allowed(u,PU::P_FREEZE)){paul_selected=PU::P_FREEZE;paul_arm(u,x,y,PFreeze,pg::freeze);paul_touch.param=PU::P_FREEZE;return true;}
    const pg::Box boxes[]={pg::strip,pg::stretch,pg::window};const int ids[]={PU::P_POSITION,PU::P_STRETCH,PU::P_WINDOW};
    for(int i=0;i<3;++i)if(boxes[i].has(x,y)){int p=ids[i];paul_selected=p;if(!paul_parameter_allowed(u,p))return true;
        paul_begin(u,x,y);paul_touch.param=p;paul_touch.box=boxes[i];paul_touch.absolute=true;
        const int left=i?26:14,travel=i?268:291;
        paul_write(u,p,i?q15(std::clamp(x-left,0,travel)*32767/travel):paul_strip_position(u,x));return true;}
    return true;}
void paul_focus_drag(PU& u,int x,int y){if(!paul_touch.unit)return;if(!paul_context()||param_lens_is_open()||paul_blocked){paul_cancel();return;}
    platform::AudioOut::Guard guard(*g_audio);if(!paul_owns(u)){paul_cancel();return;}
    if(paul_touch.action>=0){int64_t dx=int64_t(x)-paul_touch.x,dy=int64_t(y)-paul_touch.y;
        if(!paul_touch.box.has(x,y)||dx*dx+dy*dy>64){paul_cancel();}return;}
    const int p=paul_touch.param;if(p<0)return;const bool bi=u.params[p].bipolar;
    q15 value;if(paul_touch.absolute){
        value=p==PU::P_POSITION?paul_strip_position(u,x):q15(std::clamp(x-26,0,268)*32767/268);}
    else value=q15(std::clamp<int64_t>(int64_t(paul_touch.origin)+(int64_t(x)-paul_touch.x)*(bi?65534:32767)/128,bi?-32767:0,32767));
    paul_write(u,p,value);}
void paul_focus_touch_release(){int action=-1;bool browser=false;
    if(paul_touch.unit&&paul_context()&&!param_lens_is_open()&&!paul_blocked){platform::AudioOut::Guard guard(*g_audio);auto* u=paul_current();
        if(u&&paul_owns(*u)){action=paul_touch.action;
            if(action==PSetup)paul_page=paul_page?0:1;
            else if(action==PFreeze||action==PToggle){int p=paul_touch.param;paul_write(*u,p,paul_edit(*u,p)>16383?0:32767);}
            else {if(action==PSource)browser=true;
                if(action==PDirection){u->set_direction(PU::Direction((uint8_t(u->direction)+1)%3));mark_project_mutated();}
                if(action==PFlow||action==PGate){auto mode=action==PFlow?PU::Mode::Flow:PU::Mode::Gate;if(u->mode!=mode){u->set_mode(mode);mark_project_mutated();}}}}}
    paul_cancel();paul_wait_lift=false;
    if(browser){scan_sample_files();g_overlay=Overlay::SampleBrowser;}
    else if(action==PMod){module_focus_open_mod();param_lens_touch_release();}
    else if(action==PBack){if(paul_page)paul_page=0;else g_overlay=Overlay::None;}}
void paul_focus_value_text(char* out,size_t n,int p,q15 v){paul_readout::value_text(out,n,p,v);}
void paul_focus_capture(){paul_frame={};if(!paul_context()){paul_spectrum_cache.invalidate();paul_cancel();paul_page=0;paul_view_unit=0;paul_view_engine=nullptr;return;}
    pg::SpectrumKey spectrum_key;
    // No DSP here. PCM overview is at most eight probes/column/channel and is
    // retained only for this source generation/frame binding. Magnitudes and all
    // control owners are copied in one brief guard, display logarithms after it.
    static int wave_slot=-1;static uint32_t wave_generation=0,wave_frames=0;
    static uint8_t low[372]={},high[372]={};
    {
        platform::AudioOut::Guard guard(*g_audio);auto* u=paul_current();if(!u){paul_spectrum_cache.invalidate();paul_cancel();paul_page=0;paul_view_unit=0;paul_view_engine=nullptr;return;}
        if(paul_view_unit!=uintptr_t(u)||paul_view_engine!=g_eng||paul_view_chain!=g_edit_chain||paul_view_slot!=g_edit_slot){
            paul_spectrum_cache.invalidate();paul_cancel();paul_page=0;paul_view_unit=uintptr_t(u);paul_view_engine=g_eng;paul_view_chain=g_edit_chain;paul_view_slot=g_edit_slot;}
        if(paul_touch.unit&&!paul_owns(*u)){paul_cancel();}auto& f=paul_frame;
        f.valid=true;f.chain=g_edit_chain;f.slot=g_edit_slot;f.sample_slot=u->sample_slot;f.mode=uint8_t(u->mode);f.direction=uint8_t(u->direction);f.page=paul_page;
        f.direction_disabled=!paul_direction_allowed(*u);spectrum_key.engine=uintptr_t(g_eng);spectrum_key.unit=uintptr_t(u);
        f.selected=paul_selected;f.lens=param_lens_is_open();f.bypass=u->bypass;f.mute=g_eng->chains[g_edit_chain].mute;
        f.busy=g_eng->recording_active_or_pending();g_eng->capture_paul_visual(g_edit_chain,g_edit_slot,f.visual);
        for(int p=0;p<PU::P_COUNT;++p){f.edit[p]=paul_edit(*u,p);g_eng->capture_param_inspector(g_edit_chain,g_edit_slot,p,f.params[p]);}
        if(f.sample_slot>=0&&f.sample_slot<trackr::synth::SAMPLE_BANK_SIZE){const auto& s=PB::instance().slot(f.sample_slot);
            f.frames=s.playable_frames();f.generation=paul_generation(f.sample_slot);f.loaded=paul_loaded(*u);
            const char* path=PB::instance().provenance(f.sample_slot).path;const char* name=std::strrchr(path,'/');name=name?name+1:path;
            std::snprintf(f.source_name,sizeof f.source_name,"s%02u / %.18s",unsigned(f.sample_slot+1),name[0]?name:f.loaded?"sample":"empty");
            if(f.loaded){if(wave_slot!=f.sample_slot||wave_generation!=f.generation||wave_frames!=f.frames){
                for(int x=0;x<372;++x){uint32_t a=uint64_t(x)*f.frames/372,b=std::max(a+1,uint32_t(uint64_t(x+1)*f.frames/372));int lo=32767,hi=-32768;
                    for(int j=0;j<8;++j){uint32_t at=std::min(f.frames-1,a+uint32_t(uint64_t(j)*(b-a-1)/7));
                        for(int ch=0;ch<s.channels;++ch){int v=s.data[size_t(at)*s.channels+ch];lo=std::min(lo,v);hi=std::max(hi,v);}}
                    low[x]=uint8_t(90-lo*31/32768);high[x]=uint8_t(90-hi*31/32768);}
                wave_slot=f.sample_slot;wave_generation=f.generation;wave_frames=f.frames;}
                std::memcpy(f.low,low,sizeof low);std::memcpy(f.high,high,sizeof high);}}
    }
    auto& f=paul_frame;for(int p=0;p<PU::P_COUNT;++p)paul_readout::value_text(f.values[p],sizeof f.values[p],p,f.edit[p]);
    paul_readout::time_text(f.duration,sizeof f.duration,(uint64_t(f.frames)*1000+ENGINE_SR/2)/ENGINE_SR);
    paul_readout::time_text(f.position,sizeof f.position,(uint64_t(std::max<int64_t>(0,f.visual.position_q16))*1000+uint64_t(65536)*ENGINE_SR/2)/(uint64_t(65536)*ENGINE_SR));
    if(f.loaded&&!f.bypass&&!f.mute&&!f.busy&&f.visual.loaded&&f.visual.active&&f.visual.serial&&
        f.visual.sample_slot==f.sample_slot&&f.visual.generation==f.generation&&f.visual.frames==f.frames){
        f.top_cursor=pg::project_source(f.visual.position_q16,f.frames,14,372);f.bottom_cursor=pg::project_source(f.visual.position_q16,f.frames,14,292);
        f.window_left=pg::project_source(f.visual.window_start_q16,f.frames,14,372);f.window_right=pg::project_source(f.visual.window_end_q16,f.frames,14,372);
        if(f.visual.spectrum_valid){
            static_assert(PU::Visual::spectrum_count==pg::spectrum_groups,"honest LINEAR96 display");
            spectrum_key.chain=f.chain;spectrum_key.slot=f.slot;spectrum_key.sample=f.sample_slot;
            spectrum_key.generation=f.generation;spectrum_key.frames=f.frames;spectrum_key.window=f.visual.window;
            spectrum_key.serial=f.visual.serial;spectrum_key.mode=f.mode;spectrum_key.direction=f.direction;
            paul_spectrum_cache.copy(spectrum_key,f.visual.magnitudes,f.spectrum);
        }else paul_spectrum_cache.invalidate();
    }else paul_spectrum_cache.invalidate();
}
void paul_focus_draw(const PU&){const auto& f=paul_frame;m::paper(320);if(!f.valid)return;
    module_heading::header(UnitKind::Paul,m::spot());m::header_text(76,238,f.source_name[0]?f.source_name:"source >",m::ink());
    paul_word(pg::setup,"setup",f.page!=0,false);
    if(f.page&&f.lens){m::footer(320,"mod >","back >");return;}
    if(f.page){paul_word(pg::setup_direction,paul_readout::direction_text(f.direction),true,f.direction_disabled);
        paul_word(pg::flow,"flow",f.mode==0,f.busy);paul_word(pg::gate,"gate",f.mode==1,f.busy);
        for(int p=0;p<PU::P_COUNT;++p){auto b=pg::parameter(p);if(p==PU::P_FREEZE||p==PU::P_LOOP){ui::text(b.x,b.y,paul_names[p],f.selected==p?m::spot():m::ink());
                paul_word({b.x,b.y+8,b.w,21},f.values[p],f.edit[p]>16383,f.bypass||f.mute||f.busy);}
            else paul_slider(p,b,true);}}
    else{ui::text(14,44,"position",f.selected==PU::P_POSITION?m::spot():m::ink());
        paul_word(pg::direction,paul_readout::direction_text(f.direction),true,f.direction_disabled);
        paul_word(pg::freeze,"freeze",f.edit[PU::P_FREEZE]>16383,!f.loaded||f.bypass||f.mute||f.busy);
        ui::line(14,90,305,90,m::branch());for(int x:{14,159,305})ui::line(x,86,x,94,m::ghost());
        if(f.bottom_cursor>=0)ui::line(f.bottom_cursor,76,f.bottom_cursor,105,m::spot());
        ui::text(14,112,"0",m::ghost());paul_right(306,112,f.loaded?f.duration:"--",m::ghost());
        paul_slider(PU::P_STRETCH,pg::stretch,false);paul_slider(PU::P_WINDOW,pg::window,false);}
    m::footer(320,"mod >","back >");
}
bool paul_focus_draw_top(){const auto& f=paul_frame;if(!f.valid)return false;m::paper(400);
    module_heading::header(UnitKind::Paul,m::spot());m::header_text(76,280,f.source_name[0]?f.source_name:"no source",m::ink());
    char text[48];std::snprintf(text,sizeof text,"ch%02d / %02d",f.chain+1,f.slot+1);paul_right(386,m::kHeaderTextY,text,m::ghost());
    std::snprintf(text,sizeof text,"stretch %.39s",f.values[PU::P_STRETCH]);ui::text(14,38,text,m::spot());
    std::snprintf(text,sizeof text,"%s / %s",f.top_cursor<0?"waiting":f.visual.frozen?"freeze":paul_readout::direction_text(f.direction),f.top_cursor<0?"--":f.position);paul_right(386,38,text,m::ink());
    if(f.loaded){if(f.window_left>=0&&f.window_right>=f.window_left)ui::rect(f.window_left,58,f.window_right-f.window_left+1,65,(m::spot()&0xffffffu)|(28u<<24));
        for(int x=0;x<372;++x)ui::line(14+x,f.high[x],14+x,f.low[x],m::ink());
        if(f.top_cursor>=0)ui::line(f.top_cursor,56,f.top_cursor,124,m::spot());
        for(int x:{14,199,385}){ui::line(x,129,x,133,m::branch());}ui::text(14,137,"0",m::ghost());paul_right(386,137,f.duration,m::ghost());}
    else ui::text(14,97,"choose a source below",m::ghost());
    ui::text(14,154,"source fft / hann",m::ghost());paul_right(386,154,"-72..0 dbfs",m::ghost());
    const unsigned rectangles=pg::spectrum_runs(f.spectrum,[](int x,int y,int w,int h){ui::rect(x,y,w,h,m::ink());});
#ifdef SEANCE_PAUL_UI_TEST
    paul_spectrum_cache.stats.spectrum_rectangles=rectangles;paul_spectrum_cache.stats.waveform_lines=f.loaded?372:0;
#else
    (void)rectangles;
#endif
    if(f.top_cursor<0)ui::text(14,185,"no consumed source analysis",m::ghost());
    ui::line(14,210,385,210,m::branch());ui::text(14,214,"0 hz",m::ghost());paul_right(386,214,"16 khz / linear96",m::ghost());
    if(f.lens)std::snprintf(text,sizeof text,"mod / %s",paul_names[f.selected]);
    m::footer(400,f.bypass?"bypassed":f.mute?"muted":f.busy?"recording":!f.loaded?"empty source":"source analysis",f.lens?text:"actual consumed hop");return true;}
} // namespace seance::platform::ui3ds
