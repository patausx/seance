#include "ui_module_focus_internal.h"
#include "ui_state.h"
#include "ui_theme.h"
#include "ui_param_lens.h"
#include "module_inspector.h"
#include "editor_style.h"
#include "module_heading.h"
#include "small_button_motion.h"
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
using C = ShiftUnit;
constexpr int X=18, EXTENT=284;
constexpr int ORDER[3]={C::P_FEEDBACK,C::P_DAMP,C::P_MIX};
constexpr const char* NAMES[C::P_COUNT]={"shift","feedback","mix","damp"};
struct Frame {
    bool valid=false, bypass=false, mute=false, fine=false, lens=false;
    int chain=0, slot=0, selected=0;
    q15 edit[C::P_COUNT]={};
    ParamInspector routes[C::P_COUNT]={};
    char values[C::P_COUNT][24]={}, live[C::P_COUNT][24]={};
    int16_t model_x[4][4]={};
    uint8_t model_h[4][4]={};
} frame;
static_assert(sizeof(Frame)<768 && std::is_trivially_copyable<Frame>::value,
              "SHIFT frozen bounded controls/model; no PCM or engine reads in draw");
int selected=0;
bool fine=false; // Editor sensitivity only: never persisted or modulated.
struct Gesture {
    uintptr_t unit=0;
    Engine* engine=nullptr;
    int chain=-1, slot=-1, param=-1, x=0;
    q15 value=0;
    bool fine=false;
} gesture;
bool context() {
    return g_eng && g_audio && g_overlay==Overlay::Editor &&
        g_view==View::Chains &&
        g_edit_chain>=0 && g_edit_chain<NUM_CHAINS && g_edit_slot>=0 && g_edit_slot<UNITS_PER_CHAIN;
}
q15 edit_value(const C& u,int p) {
    return u.params[p].base;
}
bool owns(const C& u) {
    return gesture.engine==g_eng && gesture.unit==uintptr_t(&u) &&
        gesture.chain==g_edit_chain && gesture.slot==g_edit_slot &&
        g_eng->chains[g_edit_chain].units[g_edit_slot]==&u;
}
int clamp(int v,int p) {return std::max(p==C::P_SHIFT?-32767:0,std::min(32767,v));}
int position(int v,int p) {
    const bool bi=p==C::P_SHIFT;
    return X+(clamp(v,p)+(bi?32767:0))*EXTENT/(bi?65534:32767);
}
void right(int x,int y,const char* text,u32 color) {ui::text(x-ui::text_w(text),y,text,color);}
u32 tint(u32 color,int) {
    return (color&0xffffffu)|(85u<<24); // ink over the actual material, not a flat row
}
void source_tag(int p,int x,int y) {
    const auto source=frame.routes[p].source;
    if(source==ModSource::None)return;
    char text[2]={'e',0};
    if(mod_source_sigil(source)>=0)text[0]='a'+mod_source_sigil(source);
    ui::text(x,y,text,frame.routes[p].mod_enabled?m::spot():m::ghost());
}
void rail(int p,int y) {
    const auto& d=frame.routes[p];
    module_fader::horizontal(X,X+EXTENT,y,position(frame.edit[p],p),position(d.effective,p),
        {d.source!=ModSource::None && d.mod_enabled,false,frame.bypass||frame.mute},
        p==C::P_SHIFT?position(0,p):-1);
}
void change(C& u,int p,q15 value) {
    if(value==edit_value(u,p))return;
    u.params[p].base=value;mark_project_mutated();
}
void model() {
    // Four explicit reference tones, not measured audio. Return heights use the
    // ideal one-pole response and nominal gain; DC/allpass/limiter are excluded.
    constexpr double PI=3.14159265358979323846;
    constexpr double frequencies[4]={1000,4000,8000,12000};
    const double shift=C::shift_millihz(frame.routes[C::P_SHIFT].effective)/1000.0;
    const double gain=C::feedback_permille(frame.routes[C::P_FEEDBACK].effective)/1000.0;
    const double alpha=C::damping_alpha(frame.routes[C::P_DAMP].effective)/1073741824.0;
    const double pole=1-alpha;
    for(int i=0;i<4;++i) {
        double hz=frequencies[i],level=1;
        for(int pass=0;pass<4;++pass) {
            if(pass>1)level*=gain*alpha/std::sqrt(1+pole*pole-2*pole*std::cos(2*PI*hz/32000));
            if(pass) {hz=std::abs(hz+shift);if(hz>16000)hz=32000-hz;}
            frame.model_x[pass][i]=int16_t(80+std::lround(hz*304/16000));
            frame.model_h[pass][i]=uint8_t(std::max(0L,std::min(12L,std::lround(12*level))));
        }
    }
}
} // namespace
int shift_focus_selected() {return selected;}
void shift_focus_select(int p) {if(p>=0 && p<C::P_COUNT)selected=p;}
void shift_focus_release() {gesture={};}
bool shift_focus_touch_body(C& u,int tx,int ty) {
    if(!context() || param_lens_is_open())return false;
    platform::AudioOut::Guard guard(*g_audio);
    shift_focus_release();
    if(g_eng->chains[g_edit_chain].units[g_edit_slot]!=&u)return false;
    if(ty<25) {
        if(hit(tx,ty,252,0,60,25)){u.bypass=!u.bypass;mark_project_mutated();}
        return true;
    }
    if(hit(tx,ty,204,28,58,25)){fine=!fine;selected=C::P_SHIFT;return true;}
    if(hit(tx,ty,264,28,48,25)){selected=C::P_SHIFT;change(u,C::P_SHIFT,0);return true;}
    int p=-1;
    if(hit(tx,ty,8,28,304,101))p=C::P_SHIFT;
    for(int i=0;i<3;++i)if(hit(tx,ty,8,132+i*28,304,28)){p=ORDER[i];break;}
    if(p<0)return true;
    selected=p;
    gesture.unit=uintptr_t(&u);gesture.engine=g_eng;gesture.chain=g_edit_chain;gesture.slot=g_edit_slot;
    gesture.param=p;gesture.x=tx;gesture.value=edit_value(u,p);gesture.fine=fine;
    g_drag=Drag::ShiftParam;
    return true;
}
void shift_focus_drag(C& u,int tx,int) {
    if(!context() || param_lens_is_open()){shift_focus_release();return;}
    platform::AudioOut::Guard guard(*g_audio);
    if(gesture.param<0 || g_drag!=Drag::ShiftParam || !owns(u)){shift_focus_release();return;}
    const int p=gesture.param,range=p==C::P_SHIFT?65534:32767;
    const int denominator=EXTENT*(p==C::P_SHIFT && gesture.fine?32:1);
    change(u,p,q15(clamp(gesture.value+(tx-gesture.x)*range/denominator,p)));
}
void shift_focus_capture() {
    frame={};if(!context()){shift_focus_release();return;}
    {
        platform::AudioOut::Guard guard(*g_audio);
        const auto& chain=g_eng->chains[g_edit_chain];const Unit* unit=chain.units[g_edit_slot];
        if(!unit || unit->kind!=UnitKind::Shift){shift_focus_release();return;}
        const auto& u=*static_cast<const C*>(unit);
        if(gesture.unit && (!owns(u) || param_lens_is_open() || g_drag!=Drag::ShiftParam))shift_focus_release();
        frame.valid=true;frame.bypass=u.bypass;frame.mute=chain.mute;
        frame.fine=fine;frame.lens=false;
        frame.chain=g_edit_chain;frame.slot=g_edit_slot;frame.selected=selected;
        for(int p=0;p<C::P_COUNT;++p) {
            frame.edit[p]=edit_value(u,p);
            g_eng->capture_param_inspector(g_edit_chain,g_edit_slot,p,frame.routes[p]);
        }
    }
    for(int p=0;p<C::P_COUNT;++p) {
        module_inspector_value_text(frame.values[p],sizeof frame.values[p],UnitKind::Shift,p,frame.edit[p]);
        module_inspector_value_text(frame.live[p],sizeof frame.live[p],UnitKind::Shift,p,frame.routes[p].effective);
    }
    model();
}
void shift_focus_draw(const C&) {
    m::paper(320);if(!frame.valid)return;
    module_heading::header(UnitKind::Shift,m::spot());
    char text[32];std::snprintf(text,sizeof text,"ch%02d / %02d",frame.chain+1,frame.slot+1);
    ui::text(70,m::kHeaderTextY,text,m::ghost());small_press::text(252,0,60,25,276,m::kHeaderTextY,"byp",frame.bypass?m::spot():m::ghost());
    if(frame.bypass)ui::line(276,m::kHeaderMarkY,293,m::kHeaderMarkY,m::spot());
    ui::text(X,35,"shift",frame.selected==C::P_SHIFT?m::spot():m::ink());source_tag(C::P_SHIFT,60,35);
    small_press::text(204,28,58,25,210,35,frame.fine?"fine /32":"wide",frame.fine?m::spot():m::ghost());
    small_press::text(264,28,48,25,278,35,"zero",m::ghost());
    const auto hero=inspector_gothic::draw_numeric(X,88,frame.values[C::P_SHIFT],m::ink(),m::spot());
    if(hero.drawn)ui::text(hero.end_x+3,76,hero.suffix,m::spot());
    right(302,65,"effective",m::ghost());right(302,81,frame.live[C::P_SHIFT],frame.bypass||frame.mute?m::ghost():m::spot());
    rail(C::P_SHIFT,106);
    ui::text(X,119,"-2k",m::ghost());ui::text(157,119,"0",m::ghost());right(302,119,"+2k hz",m::ghost());
    for(int i=0;i<3;++i) {
        const int p=ORDER[i],y=137+i*28;
        ui::text(X,y,NAMES[p],frame.selected==p?m::spot():m::ink());source_tag(p,88,y);
        right(302,y,frame.values[p],m::ink());rail(p,y+13);
    }
    ui::line(14,223,306,223,m::branch());
    m::footer(320,"b back","l: mod >");
}
bool shift_focus_draw_top() {
    if(!frame.valid)return false;
    m::paper(400);module_heading::header(UnitKind::Shift,m::spot());
    char text[64];std::snprintf(text,sizeof text,"ch%02d / %02d",frame.chain+1,frame.slot+1);right(m::right_edge(400),m::kHeaderTextY,text,m::ghost());
    ui::text(16,45,"frequency translation",m::spot());right(384,45,"model / not audio",m::ghost());
    const auto hero=inspector_gothic::draw_numeric(16,94,frame.live[C::P_SHIFT],m::ink(),m::spot());
    if(hero.drawn)ui::text(hero.end_x+3,82,hero.suffix,m::spot());
    for(int i=0;i<3;++i) {
        const int p=ORDER[i];std::snprintf(text,sizeof text,"%s %s",NAMES[p],frame.live[p]);right(384,63+i*15,text,m::ghost());
    }
    const auto& d=frame.routes[frame.selected];
    if(d.source==ModSource::None) {
        static const char* layers[]={"base","step lock","audition","performance","temp"};
        std::snprintf(text,sizeof text,"%s / %s%s",NAMES[frame.selected],layers[std::min(4,int(d.layer))],frame.lens?" / modulation":"");
    } else if(d.source==ModSource::EnvFollow)std::snprintf(text,sizeof text,"%s / env%d / %s %+d%%",NAMES[frame.selected],d.env_chain+1,d.mod_enabled?"depth":"muted",int(d.depth)*100/32767);
    else std::snprintf(text,sizeof text,"%s / sigil %c / %s %+d%%",NAMES[frame.selected],'a'+mod_source_sigil(d.source),d.mod_enabled?"depth":"muted",int(d.depth)*100/32767);
    ui::text(16,114,text,d.source==ModSource::None || !d.mod_enabled?m::ghost():m::spot());
    const char* labels[]={"in","1x","2x","3x"};
    for(int pass=0;pass<4;++pass) {
        const int y=144+pass*20;ui::text(16,y-6,labels[pass],pass==1?m::spot():m::ghost());
        ui::line(80,y,384,y,m::branch());
        for(int i=0;i<4;++i) {
            const int x=frame.model_x[pass][i],h=frame.model_h[pass][i];
            if(!h)continue;
            if(pass)ui::line(frame.model_x[pass-1][i],y-20,x,y-h,tint(m::spot(),y-10));
            ui::rect(x-1,y-h,2,h,pass && !frame.bypass && !frame.mute?m::spot():m::ghost());
        }
    }
    for(int i=0;i<5;++i) {
        const int x=80+i*76;std::snprintf(text,sizeof text,"%dk",i*4);
        if(i==4)right(x,215,text,m::ghost());else ui::text(x,215,text,m::ghost());
    }
    ui::line(16,226,384,226,m::branch());
    ui::text(m::kLeft,m::kTextY,"reference tones / ideal loop",m::ghost());
    const char* state=frame.bypass?"bypass / dry":frame.mute?"chain muted":frame.routes[C::P_MIX].effective==0?"dry mix":"zero != bypass";
    right(m::right_edge(400),m::kTextY,state,m::spot());
    return true;
}
} // namespace seance::platform::ui3ds
