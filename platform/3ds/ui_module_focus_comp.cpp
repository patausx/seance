#include "ui_module_focus_internal.h"
#include "ui_state.h"
#include "ui_theme.h"
#include "ui_param_lens.h"
#include "editor_style.h"
#include "module_heading.h"
#include "inspector_gothic.h"
#include "audio_out.h"
#include "../../core/ui/draw.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <type_traits>

namespace seance::platform::ui3ds {
namespace {
namespace m = editor_style;
using C = CompUnit;
constexpr const char* NAMES[C::P_COUNT] = {
    "threshold", "ratio", "attack", "release", "makeup", "mix", "knee", "detector hp", "release clock"
};
struct Frame {
    bool valid=false, bypass=false, mute=false, lock=false, full=false, lens=false, detail=false;
    int chain=0, slot=0, selected=0;
    int32_t bpm=120<<16;
    q15 edit[C::P_COUNT]={};
    ParamInspector routes[C::P_COUNT]={};
    char values[C::P_COUNT][24]={}, live_release[36]={};
    C::Visual visual{};
    uint8_t curve[53]={};
} frame;
static_assert(sizeof(Frame)<2048 && std::is_trivially_copyable<Frame>::value,
              "COMP draws frozen bounded scalars, never live audio/engine data");
int selected=0;
bool details=false;
uintptr_t page_unit=0;
Engine* page_engine=nullptr;
struct Gesture {
    uintptr_t unit=0;
    Engine* engine=nullptr;
    int chain=-1, slot=-1, step=-1, lock_chain=-1, bank=-1, param=-1, x=0;
    q15 value=0;
    bool lock=false, sync=false;
} gesture;

int uni(int v) { return std::max(0,std::min(32767,v)); }
bool context() {
    return g_eng && g_audio && g_overlay==Overlay::Editor &&
        (g_view==View::Chains || (g_view==View::Sequencer && g_seq_lock_armed)) &&
        g_edit_chain>=0 && g_edit_chain<NUM_CHAINS && g_edit_slot>=0 && g_edit_slot<UNITS_PER_CHAIN;
}
bool lock_context() {
    return g_seq_lock_armed && g_seq_lock_chain==g_edit_chain && g_seq_lock_step>=0 && g_seq_lock_step<SEQ_STEPS;
}
q15 edit_value(const C& u,int p) {
    return lock_context()?seq_display_value(g_edit_slot,p,u.params[p].base):u.params[p].base;
}
void sync_page(const C& u) {
    if(page_engine!=g_eng || page_unit!=uintptr_t(&u)) {
        gesture={}; details=false; selected=0; page_engine=g_eng; page_unit=uintptr_t(&u);
    }
}
bool owns(const C& u) {
    return gesture.engine==g_eng && gesture.unit==uintptr_t(&u) &&
        gesture.chain==g_edit_chain && gesture.slot==g_edit_slot &&
        g_eng->chains[g_edit_chain].units[g_edit_slot]==&u && gesture.lock==g_seq_lock_armed &&
        (!gesture.lock || (lock_context() && gesture.step==g_seq_lock_step &&
        gesture.lock_chain==g_seq_lock_chain && gesture.bank==g_eng->active_pattern));
}
void change(C& u,int p,q15 value) {
    if(value==edit_value(u,p))return;
    if(g_seq_lock_armed) { if(lock_context())seq_record_lock(g_edit_slot,p,value); }
    else {u.params[p].base=value;mark_project_mutated();}
}
void format(char* out,size_t n,int p,q15 value,bool sync,int32_t bpm) {
    if(!out || !n)return;
    const q15 v=q15(uni(value));
    switch(p) {
        case C::P_THRESHOLD: std::snprintf(out,n,"%.1fdb",double(C::threshold_db(v))); break;
        case C::P_RATIO: std::snprintf(out,n,"%.1f:1",double(C::ratio(v))); break;
        case C::P_ATTACK: std::snprintf(out,n,"%.1fms",double(C::attack_ms(v))); break;
        case C::P_RELEASE:
            if(sync)std::snprintf(out,n,"%s",C::release_label(v));
            else std::snprintf(out,n,"%.0fms",double(C::release_ms(v,false,bpm)));
            break;
        case C::P_MAKEUP: std::snprintf(out,n,"%+.1fdb",double(C::makeup_db(v))); break;
        case C::P_MIX: std::snprintf(out,n,"%d%%",(int(v)*100+16383)/32767); break;
        case C::P_KNEE: std::snprintf(out,n,"%.1fdb",double(C::knee_db(v))); break;
        case C::P_HP:
            if(!v)std::snprintf(out,n,"off");
            else std::snprintf(out,n,"%.0fhz",double(C::hp_hz(v)));
            break;
        case C::P_CLOCK: std::snprintf(out,n,"%s",v>16383?"sync":"free"); break;
        default: std::snprintf(out,n,"--"); break;
    }
}
void right(int x,int y,const char* text,u32 color) { ui::text(x-ui::text_w(text),y,text,color); }
int px(int v,int x,int width) {return x+uni(v)*(width-1)/32767;}
void source_tag(int p,int x,int y) {
    const auto& d=frame.routes[p];
    if(d.source==ModSource::None)return;
    char text[2]={'e',0};
    if(mod_source_sigil(d.source)>=0)text[0]='a'+mod_source_sigil(d.source);
    ui::text(x,y,text,d.mod_enabled?m::spot():m::ghost());
}
void rail(int p,int x,int y,int width) {
    const auto& d=frame.routes[p];
    const int base=px(frame.edit[p],x,width),live=px(d.effective,x,width);
    module_fader::horizontal(x,x+width-1,y,base,live,
        {d.source!=ModSource::None && d.mod_enabled,false,frame.bypass||frame.mute});
}
void field(int p,int x,int y,int width=138) {
    const u32 color=frame.selected==p?m::spot():m::ghost();
    const char* name=NAMES[p];
    if(p==C::P_RELEASE)name=frame.edit[C::P_CLOCK]>16383?"release/sync":"release/free";
    ui::text(x,y,name,color);
    // Source occupies its own small slot near the rail, never the physical value.
    source_tag(p,x,y+12);
    right(x+width,y,frame.values[p],m::ink());
    rail(p,x+10,y+15,width-10);
}
void modulation_status(char* out,size_t n) {
    const auto& d=frame.routes[frame.selected];
    const bool muted=d.source!=ModSource::None && !d.mod_enabled;
    if(frame.full && !muted) {std::snprintf(out,n,"lock full / 4 per step");return;}
    if(frame.lock && !muted) {std::snprintf(out,n,"outline edit / fill live");return;}
    if(d.source==ModSource::None) {std::snprintf(out,n,"%s / hold l: mod",d.temp?"temp":"base");return;}
    const char* state=muted?(frame.full?"muted / lock full":frame.lock?"muted / lock":"muted"):d.temp?"temp":"hold l";
    if(d.source==ModSource::EnvFollow)std::snprintf(out,n,"env%d / %+d%% / %s",d.env_chain+1,int(d.depth)*100/32767,state);
    else std::snprintf(out,n,"sigil %c / %+d%% / %s",'a'+mod_source_sigil(d.source),int(d.depth)*100/32767,state);
}
void meter(int y,const char* label,float db,u32 color) {
    char text[24];std::snprintf(text,sizeof text,"%.1f dbfs",double(db));
    ui::text(180,y,label,m::ghost());right(380,y,text,m::ink());
    ui::line(180,y+15,379,y+15,m::branch());
    const int width=std::max(0,std::min(200,int((db+48)*200/48)));
    if(width)ui::rect(180,y+13,width,4,color);
    const int tx=180+int((C::threshold_db(frame.routes[C::P_THRESHOLD].effective)+48)*199/48);
    ui::line(tx,y+11,tx,y+19,m::ghost());
}
} // namespace

int comp_focus_selected() {return selected;}
void comp_focus_select(int p) {if(p>=0 && p<C::P_COUNT){selected=p;details=p>=C::P_KNEE;}}
void comp_focus_release() {gesture={};}
bool comp_focus_back() {
    if(!details)return false;
    details=false;selected=C::P_THRESHOLD;comp_focus_release();return true;
}
void comp_focus_value_text(char* out,size_t n,int p,q15 value) {
    if(!out || !n)return;
    // The shared lens formats base/control/effective without a clock argument.
    // Name both laws rather than pretend base CLOCK is the live/modulated mode.
    if(p==C::P_RELEASE) {
        std::snprintf(out,n,"f%.0fms s%s",double(C::release_ms(value,false,120<<16)),C::release_label(value));
        return;
    }
    format(out,n,p,value,false,frame.bpm);
}
bool comp_focus_touch_body(C& u,int tx,int ty) {
    if(!context() || param_lens_is_open())return false;
    platform::AudioOut::Guard guard(*g_audio);
    comp_focus_release();
    if(g_eng->chains[g_edit_chain].units[g_edit_slot]!=&u)return false;
    sync_page(u);
    if(g_seq_lock_armed && !lock_context())return true;
    if(hit(tx,ty,64,0,100,30)) {
        if(!comp_focus_back()){g_overlay=Overlay::None;g_seq_lock_armed=false;g_drag=Drag::None;}
        return true;
    }
    if(hit(tx,ty,252,218,68,22)) {
        if(!g_seq_lock_armed){u.bypass=!u.bypass;mark_project_mutated();}
        return true;
    }
    if(!details && hit(tx,ty,166,176,148,39)) {details=true;selected=C::P_KNEE;return true;}
    int p=-1,label_x=12,label_y=43;
    if(!details) {
        if(hit(tx,ty,6,34,308,87))p=C::P_THRESHOLD;
        for(int i=0;i<5;++i) {
            const int x=(i&1)?170:12,y=128+(i/2)*30;
            if(hit(tx,ty,x-5,y-5,148,30)){p=i+1;label_x=x;label_y=y;break;}
        }
    } else {
        for(int i=0;i<3;++i) {
            const int y=64+i*48;
            if(hit(tx,ty,8,y-6,304,42)){p=C::P_KNEE+i;label_x=16;label_y=y;break;}
        }
    }
    if(p<0)return true;
    selected=p;
    if(lock_context() && hit(tx,ty,label_x-3,label_y-3,ui::text_w(NAMES[p])+6,13)) {
        if(g_eng->remove_step_lock(g_seq_lock_chain,g_seq_lock_step,g_edit_slot,p))mark_project_mutated();
        return true;
    }
    if(p==C::P_CLOCK){change(u,p,edit_value(u,p)>16383?0:32767);return true;}
    gesture.unit=uintptr_t(&u);gesture.engine=g_eng;gesture.chain=g_edit_chain;gesture.slot=g_edit_slot;
    gesture.step=g_seq_lock_step;gesture.lock_chain=g_seq_lock_chain;gesture.bank=g_eng->active_pattern;
    gesture.param=p;gesture.x=tx;gesture.value=edit_value(u,p);gesture.lock=g_seq_lock_armed;
    gesture.sync=edit_value(u,C::P_CLOCK)>16383;g_drag=Drag::CompParam;
    return true;
}
void comp_focus_drag(C& u,int tx,int) {
    if(!context() || param_lens_is_open()){comp_focus_release();return;}
    platform::AudioOut::Guard guard(*g_audio);
    if(gesture.param<0 || g_drag!=Drag::CompParam || !owns(u)){comp_focus_release();return;}
    const int p=gesture.param,dx=tx-gesture.x;
    if(p==C::P_RELEASE && gesture.sync) {
        // One deliberate drag detent = one musical bucket; stationary touch is exact.
        const int delta=dx/12;
        const int from=C::release_index(gesture.value),to=std::max(0,std::min(C::RELEASE_DIVISIONS-1,from+delta));
        change(u,p,to==from?gesture.value:C::release_value(to));
    } else {
        const int width=p==C::P_THRESHOLD?295:p>=C::P_KNEE?276:128;
        change(u,p,q15(uni(gesture.value+dx*32767/width)));
    }
}
void comp_focus_capture() {
    frame={};
    if(!context()){comp_focus_release();page_unit=0;page_engine=nullptr;return;}
    {
        platform::AudioOut::Guard guard(*g_audio);
        const auto& chain=g_eng->chains[g_edit_chain];const Unit* unit=chain.units[g_edit_slot];
        if(!unit || unit->kind!=UnitKind::Comp){comp_focus_release();page_unit=0;page_engine=nullptr;return;}
        const auto& u=*static_cast<const C*>(unit);sync_page(u);
        if(gesture.unit && (!owns(u) || param_lens_is_open() || g_drag!=Drag::CompParam))comp_focus_release();
        frame.valid=true;frame.bypass=u.bypass;frame.mute=chain.mute;frame.lock=lock_context();
        frame.full=frame.lock && g_seq_lock_flash>0;frame.lens=false;frame.detail=details;
        frame.chain=g_edit_chain;frame.slot=g_edit_slot;frame.selected=selected;frame.bpm=u.transport_bpm_q16();
        frame.visual=u.visual();
        for(int p=0;p<C::P_COUNT;++p) {
            frame.edit[p]=edit_value(u,p);
            g_eng->capture_param_inspector(g_edit_chain,g_edit_slot,p,frame.routes[p]);
        }
    }
    for(int p=0;p<C::P_COUNT;++p)format(frame.values[p],sizeof frame.values[p],p,frame.edit[p],frame.edit[C::P_CLOCK]>16383,frame.bpm);
    const bool live_sync=frame.routes[C::P_CLOCK].effective>16383;
    char release[24];format(release,sizeof release,C::P_RELEASE,frame.routes[C::P_RELEASE].effective,live_sync,frame.bpm);
    std::snprintf(frame.live_release,sizeof frame.live_release,"release %s %.16s",live_sync?"sync":"free",release);
    // Static wet-before-makeup characteristic, not a pretend input waveform.
    const float threshold=C::threshold_db(frame.routes[C::P_THRESHOLD].effective);
    const float ratio=C::ratio(frame.routes[C::P_RATIO].effective),knee=C::knee_db(frame.routes[C::P_KNEE].effective);
    for(int i=0;i<53;++i) {
        const float in=-48+48.f*i/52,over=in-threshold;
        float out=in;
        if(knee>0 && std::fabs(over)<knee*.5f)out+=(1/ratio-1)*(over+knee*.5f)*(over+knee*.5f)/(2*knee);
        else if(over>0)out=threshold+over/ratio;
        frame.curve[i]=uint8_t(std::max(0,std::min(61,int((out+48)*61/48+.5f))));
    }
}
void comp_focus_draw(const C&) {
    m::paper(320);if(!frame.valid)return;
    module_heading::header(UnitKind::Comp,m::spot());
    ui::text(70,m::kHeaderTextY,frame.detail?"< controls":"< chain",m::ghost());
    right(m::right_edge(320),m::kHeaderTextY,frame.detail?"comp / detail":frame.lock?"comp / lock":frame.bypass?"comp / bypass":"comp / active",m::spot());
    ui::line(12,30,307,30,m::branch());
    if(!frame.detail) {
        ui::text(12,43,"threshold",frame.selected==C::P_THRESHOLD?m::spot():m::ghost());source_tag(C::P_THRESHOLD,78,43);
        ui::text(12,62,"lower = more",m::ghost());
        // Source tag has a dedicated position, away from threshold helper text.
        const auto hero=inspector_gothic::draw_numeric(157,77,frame.values[C::P_THRESHOLD],m::ink(),m::spot());
        if(hero.drawn)ui::text(hero.end_x+3,65,hero.suffix,m::spot());
        const int input=12+int(std::max(0.f,std::min(48.f,frame.visual.detector_db+48))*295/48);
        // Detector marker stays above; edit/live share the lancet rail below it.
        const int label=std::max(12,std::min(260,input-21));
        ui::text(label,80,"detector",m::ghost());ui::line(input-2,90,input,92,m::ghost());ui::line(input,92,input+2,90,m::ghost());
        rail(C::P_THRESHOLD,12,97,296);
        ui::text(12,109,"-48",m::ghost());ui::text(150,109,"-24",m::ghost());right(308,109,"0 db",m::ghost());
        for(int i=0;i<5;++i)field(i+1,(i&1)?170:12,128+(i/2)*30);
        ui::text(170,184,"detail >",m::spot());ui::text(170,199,"knee / hp / clock",m::ghost());
    } else {
        ui::text(16,42,"shape / detector / timing",m::ghost());
        for(int i=0;i<3;++i)field(C::P_KNEE+i,16,64+i*48,288);
        ui::text(16,93,"soft transition at threshold",m::ghost());
        ui::text(16,141,"bass filter in detector only",m::ghost());
        ui::text(16,189,"release = one-pole time constant",m::ghost());
        ui::text(16,204,"sync follows applied tempo",m::ghost());
    }
    ui::line(12,218,307,218,m::branch());
    char text[48];modulation_status(text,sizeof text);ui::text(m::kLeft,m::kTextY,text,m::ghost());
    right(m::right_edge(320),m::kTextY,"bypass",frame.bypass?m::spot():m::ink());
}
bool comp_focus_draw_top() {
    if(!frame.valid)return false;
    m::paper(400);module_heading::header(UnitKind::Comp,m::spot());
    ui::text(138,m::kHeaderTextY,frame.live_release,m::ghost());
    char text[48];std::snprintf(text,sizeof text,"ch%02d / %02d",frame.chain+1,frame.slot+1);right(m::right_edge(400),m::kHeaderTextY,text,m::ghost());
    ui::text(16,46,"gain reduction",m::spot());
    std::snprintf(text,sizeof text,"%.1f",double(frame.visual.gr_db));
    const auto hero=inspector_gothic::draw_numeric(16,100,text,m::ink(),m::spot());
    if(hero.drawn)ui::text(hero.end_x+3,88,"db",m::spot());
    meter(48,"input",frame.visual.input_db,m::ghost());meter(80,"output",frame.visual.output_db,m::spot());
    ui::line(16,112,383,112,m::branch());ui::text(16,122,"reduction / history",m::spot());ui::text(278,122,"in > out",m::ghost());
    float max_gr=12;
    for(int i=0;i<frame.visual.history_count;++i)max_gr=std::max(max_gr,frame.visual.history[i]);
    // Adaptive, explicitly labelled range: heavy compression is not silently clipped.
    const int scale=int(std::ceil(max_gr/12))*12;
    for(int i=0;i<3;++i) {
        const int y=141+i*30;std::snprintf(text,sizeof text,"%d",i*scale/2);right(32,y-3,text,m::ghost());
        ui::line(40,y,248,y,m::branch());
    }
    int previous_x=0,previous_y=0;
    const int count=std::min(64,int(frame.visual.history_count));
    for(int i=0;i<count;++i) {
        const int x=40+(64-count+i)*208/63,y=141+std::min(60,int(frame.visual.history[i]*60/scale+.5f));
        ui::line(x,142,x,std::max(142,y),(m::spot()&0xffffffu)|(64u<<24));
        if(i)ui::line(previous_x,previous_y,x,y,m::spot());
        previous_x=x;previous_y=y;
    }
    if(count)ui::rect(previous_x-1,previous_y-1,3,3,m::spot());
    ui::text(40,211,"-2s",m::ghost());right(248,211,"now",m::ghost());
    ui::line(278,141,278,202,m::branch());ui::line(278,202,382,202,m::branch());
    for(int i=0;i<52;i+=3)ui::line(278+i*2,202-i*61/52,280+i*2,202-(i+1)*61/52,m::ghost());
    for(int i=1;i<53;++i)ui::line(278+(i-1)*2,202-frame.curve[i-1],278+i*2,202-frame.curve[i],m::ink());
    std::snprintf(text,sizeof text,"curve / %.1f:1",double(C::ratio(frame.routes[C::P_RATIO].effective)));
    ui::text(278,211,text,m::ghost());
    ui::line(16,224,383,224,m::branch());ui::text(m::kLeft,m::kTextY,"wet / before makeup",m::ghost());
    const char* state=frame.bypass?"bypass / warm":frame.mute?"chain muted":
        frame.routes[C::P_MIX].effective==0?"dry":frame.visual.clip?"output clip":"active";
    right(m::right_edge(400),m::kTextY,state,m::spot());
    return true;
}
} // namespace seance::platform::ui3ds
