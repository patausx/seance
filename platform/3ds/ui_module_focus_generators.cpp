#include "ui_module_focus_internal.h"
#include "ui_state.h"
#include "ui_param_lens.h"
#include "fm_context.h"
#include "fm_editor_visual.h"
#include "ui_sequencer.h"
#include "module_frame_layout.h"
#include "editor_style.h"
#include "module_heading.h"
#include "audio_out.h"
#include "../../core/ui/draw.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>

namespace seance::platform::ui3ds {
namespace {
namespace m=editor_style;
using F=FmUnit;
struct Frame {
    uint16_t mod_links=0;
    bool valid=false,bypass=false,mute=false,drone=false;
    bool phase_reset=false,alias_safe=false,telemetry=false,audible=false,extended=false,harmonic=false;
    F::Wave carrier_wave=F::Wave::Sine,mod_wave=F::Wave::Sine;
    int8_t chain=0,slot=0,selected=F::P_INDEX,page=0;
    q15 edit[12]={},live[12]={};
    uint16_t phase=0,amp_live=0;
    uint8_t index_live=0,feedback_live=0;
    char values[12][20]={};
} frame;
static_assert(sizeof(Frame)<=384&&std::is_trivially_copyable<Frame>::value,
              "bounded pointer-free exact FM frame; draw never reads engine or audio");

struct Control {int param,x,y,w,h,extent,rail;const char* name;bool large=false;};
constexpr Control voice_controls[]={
    {F::P_PITCH,8,50,148,74,272,114,"pitch",true},
    {F::P_RATIO,164,50,148,74,272,114,"ratio",true},
    {F::P_INDEX,8,127,148,41,136,156,"index"},
    {F::P_FEEDBACK,164,127,148,41,136,156,"feedback"},
    {F::P_RATIO_FINE,8,172,148,40,336,201,"fine"},
    {F::P_GAIN,164,172,148,40,136,201,"gain"}
};
constexpr Control motion_controls[]={
    {F::P_ATTACK,8,50,304,40,292,79,"attack"},
    {F::P_DECAY,8,91,304,62,292,123,"decay"},
    {F::P_INDEX_FALL,8,154,148,35,136,183,"index fall"},
    {F::P_FEEDBACK_FALL,164,154,148,35,136,183,"fb fall"},
    {F::P_PITCH_ENV,8,190,148,34,168,218,"pitch env"},
    {F::P_PITCH_TIME,164,190,148,34,136,218,"env time"}
};
static_assert(motion_controls[1].x+6==module_frame_layout::kLeft);
static_assert(motion_controls[1].x+motion_controls[1].w-6==module_frame_layout::right_edge(320));
constexpr const char* wave_names[]={"sine","tri","saw","pulse"};
constexpr int wave_x[]={84,144,204,274};
bool harmonic_edit=false;

int selected=F::P_INDEX,page=0,drag_param=-1,origin_x=0,origin_last_x=0,origin_chain=-1,origin_slot=-1;
int origin_bank=-1,origin_page=-1;
Engine* origin_engine=nullptr;
View origin_view=View::Chains;
uintptr_t origin_unit=0;
q15 origin_value=0;

int clamp_uni(int v){return std::max(0,std::min(32767,v));}
bool inside(int px,int py,int x,int y,int w,int h){return px>=x&&px<x+w&&py>=y&&py<y+h;}
bool capture_context(){return g_eng&&g_audio&&g_overlay==Overlay::Editor&&
    g_view==View::Chains&&
    g_edit_chain>=0&&g_edit_chain<NUM_CHAINS&&g_edit_slot>=0&&g_edit_slot<UNITS_PER_CHAIN;}
bool context(){return !param_lens_is_open() && capture_context();}
bool motion_param(int p){return p==F::P_ATTACK||p==F::P_DECAY||p==F::P_INDEX_FALL||p==F::P_FEEDBACK_FALL||p==F::P_PITCH_ENV||p==F::P_PITCH_TIME;}
bool dormant(const F&u,int p){return (u.drone&&motion_param(p))||p>=u.num_params;}
int page_for(int p){return motion_param(p)?1:0;}
bool bipolar(int p){return p==F::P_RATIO_FINE||p==F::P_PITCH_ENV;}
q15 harmonic_value(int raw){
    // Editing aid only: never quantizes existing values, fine or Sigils on its own.
    constexpr int ratios[]={2048,4096,6144,8192,12288,16384,20480,24576,32768};
    int best=0,distance=65536;
    for(int q12:ratios){const int v=((q12-2048)*32767+30719)/30720;const int d=std::abs(raw-v);if(d<distance){best=v;distance=d;}}
    return q15(best);
}
q15 edit_value(const F&u,int p){
    return u.params[p].base;
}
bool owns(const F&u){return origin_engine==g_eng&&origin_unit==(uintptr_t)&u&&origin_chain==g_edit_chain&&origin_slot==g_edit_slot&&origin_view==g_view&&
    origin_bank==g_eng->active_pattern&&origin_page==page&&drag_param==selected&&
    g_eng->chains[g_edit_chain].units[g_edit_slot]==&u;}
void set_value(F&u,int p,q15 value){
    if(edit_value(u,p)==value)return;
    u.params[p].base=value;
    mark_project_mutated();
}
void right(int x,int y,const char*text,u32 color){ui::text(x-ui::text_w(text),y,text,color);}
const Control* find_control(int p){for(const auto&c:voice_controls)if(c.param==p)return&c;for(const auto&c:motion_controls)if(c.param==p)return&c;return nullptr;}
Drag drag_kind(int p){switch(p){case F::P_PITCH:return Drag::FmPitch;case F::P_RATIO:return Drag::FmRatio;case F::P_INDEX:return Drag::FmIndex;
    case F::P_FEEDBACK:return Drag::FmFeedback;case F::P_DECAY:return Drag::FmDecay;case F::P_GAIN:return Drag::FmGain;
    case F::P_INDEX_FALL:return Drag::FmIndexFall;case F::P_ATTACK:return Drag::FmAttack;case F::P_RATIO_FINE:return Drag::FmRatioFine;
    case F::P_FEEDBACK_FALL:return Drag::FmFeedbackFall;default:return Drag::FmModDepth;}}
void begin(F&u,const Control&c,int tx){drag_param=c.param;origin_x=origin_last_x=tx;origin_value=edit_value(u,c.param);
    origin_unit=(uintptr_t)&u;origin_chain=g_edit_chain;origin_slot=g_edit_slot;origin_view=g_view;
    origin_engine=g_eng;origin_bank=g_eng->active_pattern;origin_page=page;
    g_drag=drag_kind(c.param);g_drag_param=c.param;}

uint32_t pitch_increment(q15 pitch){int note100=2400+int((int64_t(uint16_t(pitch))*7200)>>15);note100=((note100+50)/100)*100;
    static const uint32_t semi_q16[]={65536,69433,73562,77936,82570,87480,92682,98193,104032,110218,116772,123715};
    const uint32_t semis=uint32_t(note100/100),octave=semis/12,note=semis%12;uint64_t freq_q16=(uint64_t(1071616)*semi_q16[note])>>16;
    freq_q16<<=octave;return uint32_t((freq_q16<<16)/ENGINE_SR);}
int32_t sine(uint32_t phase){const uint32_t p=phase>>16;const int32_t tri=p<0x8000u?int32_t(p)*2-32768:32767-(int32_t(p)-32768)*2;
    const int32_t magnitude=tri<0?-tri:tri;return int32_t((int64_t(tri)*(65536-magnitude))>>15);}
q15 sat(int32_t x){return x>32767?32767:x<-32768?-32768:q15(x);}

struct Reference {
    uint32_t carrier_phase=0,mod_phase=0,carrier_inc=0,mod_inc=0;
    q15 last_mod=0,amp=0,index_env=32767,feedback_env=32767;
    q15 base_index=0,base_feedback=0,gain=0;
    uint16_t decay_step=8,attack_step=32767,index_fall_step=0,feedback_fall_step=0;
    bool drone=false,attacking=false,index_fall=false,feedback_fall=false;
    Reference(const q15*v,bool drone_mode,bool safe):drone(drone_mode){
        carrier_inc=pitch_increment(v[F::P_PITCH]);mod_inc=F::modulation_increment(carrier_inc,v[F::P_RATIO],v[F::P_RATIO_FINE],drone,safe);
        base_index=v[F::P_INDEX];base_feedback=v[F::P_FEEDBACK];gain=v[F::P_GAIN];
        decay_step=uint16_t(16u+((32767u-uint16_t(v[F::P_DECAY]))*496u)/32767u);
        const uint32_t attack_samples=1u+uint32_t(uint16_t(v[F::P_ATTACK]))*639u/32767u;
        attack_step=uint16_t((32766u+attack_samples-1u)/attack_samples);
        const uint32_t i=uint16_t(v[F::P_INDEX_FALL]),f=uint16_t(v[F::P_FEEDBACK_FALL]);
        index_fall=i!=0;feedback_fall=f!=0;index_fall_step=i?uint16_t(8u+i*248u/32767u):0;feedback_fall_step=f?uint16_t(8u+f*248u/32767u):0;
        if(drone){amp=0;index_env=feedback_env=32767;attacking=false;}else{index_env=feedback_env=32767;attacking=v[F::P_ATTACK]>0;amp=attacking?1:32767;}
    }
    q15 step(q15*mod_out=nullptr,q15*amp_out=nullptr,q15*index_out=nullptr,q15*feedback_out=nullptr){
        const q15 index=(drone?index_env<32767:index_fall)?q15((int32_t(base_index)*index_env)>>15):base_index;
        const q15 feedback=(drone?feedback_env<32767:feedback_fall)?q15((int32_t(base_feedback)*feedback_env)>>15):base_feedback;
        if(amp_out)*amp_out=amp;
        if(index_out)*index_out=index_env;
        if(feedback_out)*feedback_out=feedback_env;
        mod_phase+=mod_inc;
        const int32_t feedback_phase=int32_t(int64_t(last_mod)*feedback*2);
        last_mod=sat(sine(mod_phase+uint32_t(feedback_phase)));
        if(mod_out)*mod_out=last_mod;
        const int32_t modulation=int32_t((int64_t(last_mod)*index*8)>>15);
        carrier_phase+=carrier_inc;
        const q15 out=sat(int32_t((int64_t(sine(carrier_phase+uint32_t(int64_t(modulation)*32768)))*amp*gain)>>30));
        if(drone){amp=q15(std::min(32767,int(amp)+F::DRONE_FADE_STEP));index_env=q15(std::min(32767,int(index_env)+F::DRONE_FADE_STEP));feedback_env=q15(std::min(32767,int(feedback_env)+F::DRONE_FADE_STEP));return out;}
        if(attacking){const int32_t next=int32_t(amp)+attack_step;if(next>=32767){amp=32767;attacking=false;}else amp=q15(next);}
        else if(amp>0){int32_t fall=(int32_t(amp)*decay_step)>>15;if(fall<1)fall=1;amp=q15(amp>fall?amp-fall:0);}
        if(index_fall_step&&index_env>0){int32_t fall=(int32_t(index_env)*index_fall_step)>>15;if(fall<1)fall=1;index_env=q15(index_env>fall?index_env-fall:0);}
        if(feedback_fall_step&&feedback_env>0){int32_t fall=(int32_t(feedback_env)*feedback_fall_step)>>15;if(fall<1)fall=1;feedback_env=q15(feedback_env>fall?feedback_env-fall:0);}
        return out;
    }
};
void format_value(int p,q15 value,char*out,size_t size){
    if(!out||!size)return;
    fm_context_value_text(out,unsigned(size),p,value);
    if((p==F::P_INDEX_FALL||p==F::P_FEEDBACK_FALL)&&value==0)std::snprintf(out,size,"off");
    if(p==F::P_RATIO||p==F::P_RATIO_FINE){
        const size_t length=std::strlen(out);
        if(length+1<size){out[length]='x';out[length+1]='\0';}
    }
}
void draw_header(){
    char text[24];m::paper(320);if(!frame.valid)return;
    module_heading::header(UnitKind::Fm,m::spot());
    std::snprintf(text,sizeof text,"ch%02d / %02d",frame.chain+1,frame.slot+1);ui::text(51,m::kHeaderTextY,text,m::ghost());
    ui::text(126,m::kHeaderTextY,frame.drone?"drone":"strike",m::spot());ui::line(126,m::kHeaderMarkY,160,m::kHeaderMarkY,m::spot());
    // The published scalar is an amplitude envelope, NOT an output peak meter.
    ui::text(180,m::kHeaderTextY,"amp",m::ghost());ui::rect(204,m::kHeaderTextY+4,25,2,m::branch());
    if(frame.audible){const int w=std::min(25,int(frame.amp_live)*25/32767);if(w)ui::rect(204,m::kHeaderTextY+4,w,2,m::spot());}
    ui::text(252,m::kHeaderTextY,"byp",frame.bypass?m::spot():m::ghost());if(frame.bypass)ui::line(252,m::kHeaderMarkY,269,m::kHeaderMarkY,m::spot());ui::text(m::right_edge(320)-ui::text_w("x"),m::kHeaderTextY,"x",m::ghost());
    static const char* tabs[]={"voice","motion","operators"};static const int x[]={14,118,224};
    for(int i=0;i<3;++i){ui::text(x[i],module_frame_layout::kRouteY,tabs[i],frame.page==i?m::spot():m::ghost());if(frame.page==i)ui::rect(x[i],module_frame_layout::kRouteUnderlineY,ui::text_w(tabs[i])-2,1,m::spot());}
}
void operators(){
    if(!frame.extended){
        ui::text(14,59,"legacy sound preserved",m::ink());
        ui::text(14,82,"enable extended >",m::spot());
        ui::text(14,103,"adds shapes + pitch envelope",m::ghost());
        ui::text(14,119,"existing ten controls stay intact",m::ghost());
    }else{
        ui::text(14,59,"carrier / audible tone",m::ghost());
        ui::text(14,108,"modulator / bends the carrier",m::ghost());
        for(int row=0;row<2;++row){const int y=81+49*row;const int active=int(row?frame.mod_wave:frame.carrier_wave);
            for(int i=0;i<4;++i){ui::text(wave_x[i]-ui::text_w(wave_names[i])/2,y,wave_names[i],active==i?m::spot():m::ghost());
                if(active==i)ui::rect(wave_x[i]-13,y+13,26,1,m::spot());}
        }
    }
    ui::text(14,162,"ratio edit",m::ghost());ui::text(126,162,"free",frame.harmonic?m::ghost():m::spot());ui::text(204,162,"harmonic",frame.harmonic?m::spot():m::ghost());
    ui::text(14,181,"snaps drags only / fine stays free",m::ghost());
    ui::text(14,204,"phase",m::ghost());ui::text(54,204,frame.drone?"held":frame.phase_reset?"reset":"free",frame.drone?m::ghost():m::spot());
    ui::text(170,204,"guard",m::ghost());ui::text(213,204,frame.alias_safe?"12khz cap":"raw",frame.alias_safe?m::spot():m::ink());
}
int control_position(const Control&c,q15 value){
    const int x0=c.x+6,w=c.w-12;
    if(bipolar(c.param))return x0+(std::max(-32767,int(value))+32767)*w/65534;
    const int phase=c.param==F::P_DECAY?fm_visual::decay_position(value):clamp_uni(value);
    return x0+phase*w/32767;
}
void draw_control(const Control&c){
    const bool unavailable=!frame.extended&&c.param>=10,held=frame.drone&&motion_param(c.param);
    const u32 color=unavailable?m::ghost():frame.selected==c.param?m::spot():held?m::ghost():m::ink();
    const int x=c.x+6,end=c.x+c.w-6;
    ui::text(x,c.y+(c.large?7:9),c.name,color);
    if(c.large){
        if(c.param==F::P_PITCH)fm_visual::note(x,101,frame.values[c.param],m::ink(),m::spot());
        else {char number[20];fm_context_value_text(number,sizeof number,c.param,frame.edit[c.param]);
            const int after=fm_visual::number(x,101,number,m::ink(),m::spot());ui::text(after+4,91,"x",m::ghost());}
    }else right(end,c.y+9,unavailable?"extended":held?"held":frame.values[c.param],frame.selected==c.param?color:m::ghost());
    if(unavailable){module_fader::horizontal_track(x,end,c.rail);return;}
    module_fader::horizontal(x,end,c.rail,control_position(c,frame.edit[c.param]),control_position(c,frame.live[c.param]),
        {bool(frame.mod_links&(1u<<c.param)),held,frame.bypass||frame.mute},
        bipolar(c.param)?control_position(c,0):-1);
    if(c.param==F::P_DECAY){
        const int ms[]={14,40,120,442};
        for(int i=0;i<4;++i){const int at=x+fm_visual::decay_tick(ms[i])*(end-x)/32767;
            ui::line(at,129,at,131,m::ghost());char tick[12];std::snprintf(tick,sizeof tick,"%d%s",ms[i],i==3?"ms":"");
            const int text_x=i==0?x:i==3?end-ui::text_w(tick):at-ui::text_w(tick)/2;
            ui::text(text_x,137,tick,m::ghost());}
    }
}
} // namespace

void fm_focus_reset_reference(q15*mod,q15*out,q15*amp,q15*index,q15*feedback,int frames,const q15*values,bool drone,bool alias_safe){
    if(frames<=0||!values)return;
    Reference model(values,drone,alias_safe);
    for(int i=0;i<frames;++i){
        q15 mod_sample=0,amp_sample=0,index_sample=0,feedback_sample=0;
        const q15 output=model.step(&mod_sample,&amp_sample,&index_sample,&feedback_sample);
        if(mod)mod[i]=mod_sample;
        if(out)out[i]=output;
        if(amp)amp[i]=amp_sample;
        if(index)index[i]=index_sample;
        if(feedback)feedback[i]=feedback_sample;
    }
}
int fm_focus_selected(){return selected;}
int fm_focus_page(const F& u){
    if(selected>=u.num_params){fm_focus_release();selected=F::P_INDEX;if(page!=2)page=0;}
    return page;
}
void fm_focus_select(int param){if(param>=0&&param<12){if(param!=selected)fm_focus_release();selected=param;page=page_for(param);}}
void fm_focus_release(){drag_param=-1;origin_unit=0;origin_engine=nullptr;if(g_drag>=Drag::FmPitch&&g_drag<=Drag::FmModDepth)g_drag=Drag::None;}

bool fm_focus_touch_body(F&u,int tx,int ty){if(!context()){fm_focus_release();return false;}platform::AudioOut::Guard guard(*g_audio);fm_focus_release();
    if(g_eng->chains[g_edit_chain].units[g_edit_slot]!=&u)return false;
    fm_focus_page(u);
    if(ty<25){
        if(inside(tx,ty,126,0,48,25)){u.drone=!u.drone;mark_project_mutated();}
        else if(inside(tx,ty,242,0,44,25)){u.bypass=!u.bypass;mark_project_mutated();}
        return true;
    }
    if(ty<49){if(tx>=8&&tx<312){page=tx<108?0:tx<214?1:2;
        if(page<2&&page_for(selected)!=page)selected=page==1?F::P_DECAY:F::P_INDEX;
    }return true;}
    if(page==2){
        if(inside(tx,ty,116,151,77,24)){harmonic_edit=false;return true;}
        if(inside(tx,ty,196,151,112,24)){harmonic_edit=true;return true;}
        if(!u.extended&&inside(tx,ty,8,74,204,23)){
            // Explicit upgrade only. Stale old ParamBank slots never become new routes.
            F defaults;u.params[F::P_PITCH_ENV]=defaults.params[F::P_PITCH_ENV];u.params[F::P_PITCH_TIME]=defaults.params[F::P_PITCH_TIME];
            u.carrier_wave=u.mod_wave=F::Wave::Sine;u.set_extended(true);mark_project_mutated();return true;
        }
        if(u.extended)for(int row=0;row<2;++row)for(int i=0;i<4;++i)if(inside(tx,ty,wave_x[i]-28,72+49*row,56,29)){
            auto& wave=row?u.mod_wave:u.carrier_wave;if(int(wave)!=i){wave=F::Wave(i);mark_project_mutated();}return true;
        }
        if(inside(tx,ty,8,193,140,29)){if(!u.drone){u.phase_reset=!u.phase_reset;mark_project_mutated();}return true;}
        if(inside(tx,ty,164,193,148,29)){u.alias_safe=!u.alias_safe;u.control_tick();mark_project_mutated();return true;}
        return true;
    }
    const Control*found=nullptr;const auto touch=[&](const auto&controls){for(const auto&c:controls)if(inside(tx,ty,c.x,c.y,c.w,c.h)){found=&c;break;}};
    if(page==0)touch(voice_controls);else touch(motion_controls);
    if(!found||found->param>=u.num_params)return true;
    selected=found->param;
    if(dormant(u,selected))return true;
    begin(u,*found,tx);
    return true;
}
void fm_focus_drag(F&u,int tx,int){
    if(drag_param<0)return;
    if(!context()||g_drag!=drag_kind(drag_param)){fm_focus_release();return;}
    platform::AudioOut::Guard guard(*g_audio);
    if(!owns(u)||dormant(u,drag_param)){fm_focus_release();return;}
    tx=std::max(0,std::min(319,tx));
    if(tx==origin_last_x)return;
    origin_last_x=tx;
    const Control*c=find_control(drag_param);
    if(!c)return;
    q15 next=origin_value;
    if(drag_param==F::P_DECAY)next=fm_visual::decay_drag(origin_value,tx-origin_x,c->extent);
    else if(bipolar(drag_param)){
        const int value=int(origin_value)+(tx-origin_x)*32767/c->extent;
        next=q15(std::max(-32767,std::min(32767,value)));
    }else{
        next=q15(clamp_uni(int(origin_value)+(tx-origin_x)*32767/c->extent));
        if(drag_param==F::P_RATIO&&harmonic_edit&&tx!=origin_x)next=harmonic_value(next);
    }
    set_value(u,drag_param,next);
}
// Another module's editor: no guard entry. Chain slots are written only by this UI thread.
void fm_focus_capture(){frame={};if(!capture_context()){fm_focus_release();return;}
    {const Unit*unit=g_eng->chains[g_edit_chain].units[g_edit_slot];if(!unit||unit->kind!=UnitKind::Fm){fm_focus_release();return;}}{
        platform::AudioOut::Guard guard(*g_audio);const auto&chain=g_eng->chains[g_edit_chain];const Unit*unit=chain.units[g_edit_slot];
        if(!unit||unit->kind!=UnitKind::Fm){fm_focus_release();return;}const auto&u=*static_cast<const F*>(unit);if(drag_param>=0&&!owns(u))fm_focus_release();
        frame.valid=true;frame.bypass=u.bypass;frame.mute=chain.mute;
        frame.extended=u.extended;frame.carrier_wave=u.carrier_wave;frame.mod_wave=u.mod_wave;frame.harmonic=harmonic_edit;
        frame.mod_links=focus_mod_links(u);
        fm_focus_page(u);
        frame.drone=u.drone;frame.phase_reset=u.phase_reset;frame.alias_safe=u.alias_safe;frame.chain=g_edit_chain;frame.slot=g_edit_slot;frame.selected=selected;frame.page=page;
        for(int p=0;p<u.num_params;++p){frame.edit[p]=edit_value(u,p);frame.live[p]=u.params[p].effective;}
        frame.telemetry=g_ui_snapshot.effect_focus_valid&&g_ui_snapshot.effect_focus_kind==uint8_t(UnitKind::Fm)&&g_ui_snapshot.effect_focus_chain==g_edit_chain&&g_ui_snapshot.effect_focus_slot==g_edit_slot;
        frame.audible=frame.telemetry&&g_ui_snapshot.effect_focus_audible&&!u.bypass&&!chain.mute;if(frame.audible){frame.phase=uint16_t(g_ui_snapshot.generator_phase_hi*256u+g_ui_snapshot.generator_phase_lo);
            frame.amp_live=g_ui_snapshot.generator_energy;frame.index_live=g_ui_snapshot.fm_index_envelope;frame.feedback_live=g_ui_snapshot.fm_feedback_envelope;}}
    for(int p=0;p<12;++p)format_value(p,frame.edit[p],frame.values[p],sizeof frame.values[p]);
}
void fm_focus_draw(const F&){
    draw_header();if(!frame.valid)return;
    if(frame.page==0){for(const auto&c:voice_controls)draw_control(c);ui::text(14,213,frame.harmonic?"ratio edit: harmonic":"ratio edit: free",m::ghost());}
    else if(frame.page==1){for(const auto&c:motion_controls)draw_control(c);}
    else operators();
    m::footer(320,frame.mute?"chain muted":frame.bypass?"bypassed":frame.page==1?(frame.drone?"drone / envelopes held":"motion"):frame.page==2?"operators":"voice","l: mod >");
}

} // namespace seance::platform::ui3ds
