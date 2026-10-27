#include "ui_module_focus_internal.h"
#include "ui_state.h"
#include "ui_sequencer.h"
#include "noise_editor_visual.h"
#include "ui_theme.h"
#include "ui_param_lens.h"
#include "module_inspector.h"
#include "editor_style.h"
#include "module_heading.h"
#include "audio_out.h"
#include "../../core/ui/draw.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <type_traits>

namespace seance::platform::ui3ds {
namespace {
namespace m=editor_style;
namespace l=noise_editor_visual;
using N=NoiseUnit;
struct Frame {
    uint16_t mod_links=0;
    bool valid=false,modern=false,setup=false,bypass=false,mute=false,lock=false,lock_full=false,audible=false;
    int8_t chain=0,slot=0,selected=0;
    q15 edit[N::P_COUNT]={},live[N::P_COUNT]={};
    uint16_t envelope=0,peak=0;
    int8_t base[l::kResponsePoints]={},onset[l::kResponsePoints]={};
    uint8_t contour[l::kEnvelopePoints]={};
    char values[N::P_COUNT][18]={};
    uint8_t kinds[UNITS_PER_CHAIN]={};bool bypassed[UNITS_PER_CHAIN]={};
} frame;
static_assert(sizeof(Frame)<1024 && std::is_trivially_copyable<Frame>::value,
              "bounded pointer-free Noise scalars; draw never reads engine or PCM");
struct Gesture {
    Engine* engine=nullptr;uintptr_t unit=0;View view=View::Chains;
    int chain=-1,slot=-1,bank=-1,step=-1,lock_chain=-1,param=-1,start=0;
    q15 value=0;bool locked=false,modern=false,setup=false;
} gesture;
struct ModelKey {q15 tone=0,body=0,resonance=0,sweep=0,attack=0,decay=0,mode=0;bool modern=false;};
ModelKey cached={};bool cache_valid=false;
int8_t cache_base[l::kResponsePoints]={},cache_onset[l::kResponsePoints]={};
uint8_t cache_contour[l::kEnvelopePoints]={};
int selected=N::P_DENSITY,bound_chain=-1,bound_slot=-1;
uintptr_t bound_unit=0;bool setup=false,bound_modern=false;
struct Control {int param,x,y,w,h,label_x,label_y,rail_y,extent;const char* name;};
constexpr Control controls[]={
    {N::P_DENSITY,8,65,152,36,14,70,92,138,"density"},
    {N::P_GAIN,170,65,142,36,178,70,92,128,"gain"},
    {N::P_TONE,8,155,152,33,14,160,180,138,"tone"},
    {N::P_BODY,170,155,142,33,178,160,180,128,"body"},
    {N::P_ATTACK,8,190,152,32,14,195,215,138,"attack"},
    {N::P_DECAY,170,190,142,32,178,195,215,128,"decay"}
};
constexpr int modern_params[4][2]={{N::P_DENSITY,N::P_GAIN},{N::P_TONE,N::P_BODY},
    {N::P_RESONANCE,N::P_SWEEP},{N::P_ATTACK,N::P_DECAY}};
constexpr const char* modern_names[4][2]={{"grain rate","gain"},{"tone","low cut"},
    {"resonance","sweep"},{"attack","decay"}};
constexpr int colour_x[]={69,128,179,260};
constexpr int colour_left[]={62,120,171,243};
constexpr int colour_right[]={116,167,239,312};
constexpr const char* colour_names[]={"white","pink","crackle","metal"};
int uni(int value){return std::max(0,std::min(32767,value));}
int colour(q15 value){return std::min(3,uni(value)*4/32768);}
bool capture_context(){return g_eng && g_audio && g_overlay==Overlay::Editor &&
    (g_view==View::Chains || (g_view==View::Sequencer && g_seq_lock_armed)) &&
    g_edit_chain>=0 && g_edit_chain<NUM_CHAINS && g_edit_slot>=0 && g_edit_slot<UNITS_PER_CHAIN;}
bool context(){return !param_lens_is_open() && capture_context();}
bool lock_context(){return g_seq_lock_armed && g_seq_lock_chain==g_edit_chain &&
    g_seq_lock_step>=0 && g_seq_lock_step<SEQ_STEPS;}
int clamp(int v,int lo=0,int hi=32767){return std::max(lo,std::min(v,hi));}
int bank(){const int b=sequencer_edit_bank();return b>=0&&b<NUM_PATTERNS?b:g_eng->active_pattern;}
int gesture_bank(){return g_seq_lock_armed?bank():g_eng->active_pattern;}
void release(){gesture={};if(g_drag==Drag::NoiseDensity)g_drag=Drag::None;}
void bind(const N& u){if(bound_unit!=uintptr_t(&u)||bound_chain!=g_edit_chain||bound_slot!=g_edit_slot||bound_modern!=u.modern()){
    release();setup=false;selected=N::P_DENSITY;bound_unit=uintptr_t(&u);bound_chain=g_edit_chain;
    bound_slot=g_edit_slot;bound_modern=u.modern();cache_valid=false;}}
q15 edit_value(const N& u,int p){
    if(lock_context()){
        const auto& step=g_eng->sequence_pattern(bank()).steps[g_edit_chain][g_seq_lock_step];
        for(int i=0;i<std::min(int(step.lock_count),SEQ_LOCKS);++i)
            if(step.locks[i].slot==g_edit_slot&&step.locks[i].param==p)return step.locks[i].value;
    }
    return u.params[p].base;
}
void set_value(N& u,int p,q15 value){ // caller owns audio guard; full bank never edits base
    if((g_seq_lock_armed&&!lock_context())||p<0||p>=u.num_params)return;
    if(g_seq_lock_armed){
        auto& step=g_eng->sequence_pattern(bank()).steps[g_edit_chain][g_seq_lock_step];
        const int count=std::min(int(step.lock_count),SEQ_LOCKS);int found=-1;
        for(int i=0;i<count;++i)if(step.locks[i].slot==g_edit_slot&&step.locks[i].param==p){found=i;break;}
        if(found>=0){if(step.locks[found].value==value)return;step.locks[found].value=value;}
        else {if(count>=SEQ_LOCKS){g_seq_lock_flash=90;return;}
            step.locks[count]={uint8_t(g_edit_slot),uint8_t(p),value};step.lock_count=uint8_t(count+1);}
    }else {if(u.params[p].base==value)return;u.params[p].base=value;}
    mark_project_mutated();cache_valid=false;
}
bool name_touch(int p,int x,int y,int w,int tx,int ty){
    if(!hit(tx,ty,x-3,y-4,w+6,16))return false;
    selected=p;
    if(lock_context()&&g_eng->remove_step_lock(g_seq_lock_chain,g_seq_lock_step,g_edit_slot,p,bank()))mark_project_mutated();
    cache_valid=false;return true;
}
void begin(N& u,int p,int x){
    selected=p;gesture={};gesture.engine=g_eng;gesture.unit=uintptr_t(&u);gesture.view=g_view;
    gesture.chain=g_edit_chain;gesture.slot=g_edit_slot;gesture.bank=gesture_bank();
    gesture.step=g_seq_lock_step;gesture.lock_chain=g_seq_lock_chain;gesture.locked=g_seq_lock_armed;
    gesture.param=p;gesture.start=x;gesture.value=edit_value(u,p);gesture.setup=setup;
    gesture.modern=u.modern();g_drag=Drag::NoiseDensity;g_drag_param=p;
}
bool owns(const N& u){return gesture.engine==g_eng&&gesture.unit==uintptr_t(&u)&&
    gesture.view==g_view&&gesture.chain==g_edit_chain&&gesture.slot==g_edit_slot&&
    gesture.bank==gesture_bank()&&gesture.param==selected&&gesture.locked==g_seq_lock_armed&&
    gesture.setup==setup&&gesture.modern==u.modern()&&g_eng->chains[g_edit_chain].units[g_edit_slot]==&u&&
    (!gesture.locked||(lock_context()&&gesture.step==g_seq_lock_step&&gesture.lock_chain==g_seq_lock_chain));}
int position(int p,q15 value,int left,int end){const int lo=p==N::P_SWEEP?-32767:0;
    return left+(clamp(value,lo)-lo)*(end-left)/(32767-lo);}
void value_text(char* out,size_t size,int p,q15 v,bool modern,int colour_index=0){
    if(!modern){module_inspector_value_text(out,size,UnitKind::Noise,p,v);return;}
    if(p==N::P_COLOUR){std::snprintf(out,size,"%s",colour_names[colour(v)]);return;}
    if(p==N::P_MODE){std::snprintf(out,size,"%s",N::mode_index(v)==0?"auto":N::mode_index(v)==1?"hit":"drone");return;}
    const auto time=[&](uint32_t frames){const uint32_t ms=(uint64_t(frames)*1000+ENGINE_SR/2)/ENGINE_SR;
        if(ms<1000)std::snprintf(out,size,"%ums",unsigned(ms));
        else std::snprintf(out,size,"%u.%02us",unsigned(ms/1000),unsigned((ms%1000)/10));};
    if(p==N::P_DENSITY){const unsigned hz=colour_index==2?N::dust_millihz(v):N::grain_millihz(v);
        if(!hz)std::snprintf(out,size,"off");
        else if(hz<1000)std::snprintf(out,size,"0.%01u/s",(hz+50)/100);
        else std::snprintf(out,size,"%u.%u%s",hz/1000,(hz%1000)/100,colour_index==2?"/s":"hz");}
    else if(p==N::P_TONE||p==N::P_BODY){const uint32_t hz=p==N::P_TONE?N::tone_millihz(v):N::body_millihz(v);
        if(!hz)std::snprintf(out,size,"off");else std::snprintf(out,size,"%u.%uhz",unsigned(hz/1000),unsigned((hz%1000)/100));}
    else if(p==N::P_SWEEP){const int cents=N::sweep_cents(v),mag=std::abs(cents);
        std::snprintf(out,size,"%c%d.%02dst",cents<0?'-':'+',mag/100,mag%100);}
    else if(p==N::P_ATTACK)time(N::attack_frames(v));
    else if(p==N::P_DECAY)time(N::decay_frames(v));
    else if(p==N::P_SWEEP_TIME)time(N::sweep_frames(v));
    else if(p==N::P_EVENT_LENGTH)time(N::event_frames(v));
    else std::snprintf(out,size,"%u%%",unsigned((uint32_t(uni(v))*100+16383)/32767));
}
void marker(int x,int y,u32 color,bool hollow=false){
    if(hollow){ui::line(x,y-3,x+3,y,color);ui::line(x+3,y,x,y+3,color);
        ui::line(x,y+3,x-3,y,color);ui::line(x-3,y,x,y-3,color);}
    else for(int dy=-3;dy<=3;++dy){const int w=3-std::abs(dy);ui::rect(x-w,y+dy,2*w+1,1,color);}
}
void right(int x,int y,const char* text,u32 color){ui::text(x-ui::text_w(text),y,text,color);}
void meter(int y,const char* name,uint16_t value,bool available,u32 color){
    ui::text(154,y-4,name,m::ghost());
    ui::line(180,y,266,y,m::branch());
    const int stop=180+uni(value)*86/32767;
    for(int x=180;x<=266;x+=4)ui::line(x,y-3,x,y+3,available && x<stop?color:m::branch());
    if(available)marker(stop,y,color);
    char text[12];if(available)std::snprintf(text,sizeof text,"%u%%",unsigned((uint32_t(value)*100+16383)/32767));
    else std::snprintf(text,sizeof text,"--");
    right(306,y-4,text,available?color:m::ghost());
}
void calculate_model(const ModelKey& key){
    constexpr double log_span=6.684611727667927; // log(16000/20), fixed model axis
    const bool drone=N::drone_mode(key.mode,key.decay);
    const int cents=drone?0:N::sweep_cents(key.sweep);
    for(int i=0;i<l::kResponsePoints;++i){
        const float hz=float(20.0*std::exp(log_span*i/(l::kResponsePoints-1)));
        const float dry=N::model_response_db(hz,key.tone,key.body,key.resonance);
        const float swept=N::model_response_db(hz,key.tone,key.body,key.resonance,cents);
        auto map=[](float db){return int8_t(clamp(int(std::lround((db+48.f)*63.f/60.f)),0,63));};
        frame.base[i]=map(dry);frame.onset[i]=map(swept);
    }
    const auto a=N::attack_frames(key.attack),d=N::decay_frames(key.decay);
    const uint32_t duration=drone?std::max(uint32_t(ENGINE_SR*3/10),a*2):std::max(uint32_t(1),a+d);
    for(int i=0;i<l::kEnvelopePoints;++i){
        const uint32_t at=uint32_t(uint64_t(i)*duration/(l::kEnvelopePoints-1));
        frame.contour[i]=uint8_t(clamp(int(std::lround(N::model_envelope(at,a,d,drone)*34.f)),0,34));
    }
    std::memcpy(cache_base,frame.base,sizeof cache_base);
    std::memcpy(cache_onset,frame.onset,sizeof cache_onset);
    std::memcpy(cache_contour,frame.contour,sizeof cache_contour);
    cached=key;cache_valid=true;
}
} // namespace

int noise_focus_selected(){return selected;}
void noise_focus_select(int param){if(param>=0&&param<N::P_COUNT){if(param!=selected)release();selected=param;}}
void noise_focus_release(){release();}
bool noise_focus_modern(){return frame.valid&&frame.modern;}
void noise_focus_value_text(char* out,size_t size,int p,q15 v){
    const int colour_index=frame.valid?colour(frame.lock?frame.edit[N::P_COLOUR]:frame.live[N::P_COLOUR]):0;
    value_text(out,size,p,v,noise_focus_modern(),colour_index);
}
bool noise_focus_touch_body(N& u,int tx,int ty){
    if(!context()){release();return false;}
    platform::AudioOut::Guard guard(*g_audio);
    if(g_eng->chains[g_edit_chain].units[g_edit_slot]!=&u)return false;
    bind(u);release();
    if(u.modern()&&l::contains(l::kSetup,tx,ty)){setup=!setup;return true;}
    if(!u.modern()&&hit(tx,ty,164,0,76,26)){
        if(!g_seq_lock_armed){N defaults;
            for(int p=N::LEGACY_COUNT;p<N::P_COUNT;++p)u.params[p]=defaults.params[p];
            for(auto& scene:g_eng->scenes[g_edit_chain])if(scene.kind[g_edit_slot]==UnitKind::Noise)
                for(int p=N::LEGACY_COUNT;p<N::P_COUNT;++p)scene.base[g_edit_slot][p]=defaults.params[p].base;
            u.set_modern(true);mark_project_mutated();bind(u);}
        return true;}
    if(ty<26){if(!g_seq_lock_armed&&hit(tx,ty,246,0,38,25)){
        u.bypass=!u.bypass;mark_project_mutated();}return true;}
    if(g_seq_lock_armed&&!lock_context())return true;
    if(u.modern()&&setup){
        for(int i=0;i<2;++i)if(l::contains(l::kSetupControls[i],tx,ty)){
            const int p=i?N::P_SWEEP_TIME:N::P_EVENT_LENGTH;
            selected=p;const int y=i?148:73;
            if(lock_context()&&name_touch(p,14,y,ui::text_w(i?"sweep time":"event length"),tx,ty))return true;
            if((!i&&colour(edit_value(u,N::P_COLOUR))!=2)||
                (i&&N::drone_mode(edit_value(u,N::P_MODE),edit_value(u,N::P_DECAY))))return true;
            begin(u,p,tx);return true;}
        return true;
    }
    if(u.modern()){
        for(int i=0;i<4;++i)if(l::contains(l::kChoice[i],tx,ty)){
            selected=N::P_COLOUR;static constexpr q15 pick[4]={0,10922,21845,32767};
            set_value(u,selected,pick[i]);return true;}
        for(int i=0;i<2;++i)if(l::contains(l::kMode[i],tx,ty)){
            selected=N::P_MODE;set_value(u,selected,i?32767:16384);return true;}
        for(int row=0;row<4;++row)for(int col=0;col<2;++col){
            const int x=l::kColumns[col],p=modern_params[row][col],y=l::kRows[row];
            if(!hit(tx,ty,x-6,y-5,146,32))continue;
            selected=p;const bool drone=N::drone_mode(edit_value(u,N::P_MODE),edit_value(u,N::P_DECAY));
            if(lock_context()&&name_touch(p,x,y,ui::text_w(modern_names[row][col]),tx,ty))return true;
            if((p==N::P_SWEEP||p==N::P_DECAY)&&drone)return true;
            begin(u,p,tx);return true;}
        return true;
    }
    if(name_touch(N::P_COLOUR,14,33,36,tx,ty)||name_touch(N::P_MODE,14,118,24,tx,ty))return true;
    for(int i=0;i<4;++i)if(hit(tx,ty,colour_left[i],27,colour_right[i]-colour_left[i],36)){
        selected=N::P_COLOUR;set_value(u,selected,q15(i*32767/3));return true;}
    for(int i=0;i<2;++i)if(hit(tx,ty,8+i*67,132,62,21)){
        selected=N::P_MODE;set_value(u,selected,i?32767:16384);return true;}
    for(const auto& c:controls)if(hit(tx,ty,c.x,c.y,c.w,c.h)){
        selected=c.param;
        if(lock_context()&&name_touch(selected,c.label_x,c.label_y,ui::text_w(c.name),tx,ty))return true;
        begin(u,selected,tx);return true;}
    return true;
}
void noise_focus_drag(N& u,int tx,int){
    if(gesture.param<0)return;
    if(!context()||g_drag!=Drag::NoiseDensity){release();return;}
    platform::AudioOut::Guard guard(*g_audio);
    if(!owns(u)){release();return;}
    const int p=gesture.param,delta=tx-gesture.start;
    if(u.modern()){
        const bool drone=N::drone_mode(edit_value(u,N::P_MODE),edit_value(u,N::P_DECAY));
        if((p==N::P_SWEEP||p==N::P_DECAY)&&drone){release();return;}
        if(p==N::P_SWEEP_TIME&&N::drone_mode(edit_value(u,N::P_MODE),edit_value(u,N::P_DECAY))){release();return;}
        if(p==N::P_EVENT_LENGTH&&colour(edit_value(u,N::P_COLOUR))!=2){release();return;}
        set_value(u,p,q15(clamp(int(gesture.value)+delta*(p==N::P_SWEEP?65534:32767)/296,
            p==N::P_SWEEP?-32767:0)));return;
    }
    for(const auto& c:controls)if(c.param==p){
        set_value(u,p,q15(clamp(int(gesture.value)+delta*32767/c.extent)));return;}
}
void noise_focus_capture(){
    frame={};if(!capture_context()){release();bound_unit=0;bound_chain=bound_slot=-1;
        setup=false;selected=N::P_DENSITY;cache_valid=false;return;}
    ModelKey key={};
    {
        platform::AudioOut::Guard guard(*g_audio);
        const auto& chain=g_eng->chains[g_edit_chain];const Unit* unit=chain.units[g_edit_slot];
        if(!unit||unit->kind!=UnitKind::Noise){release();bound_unit=0;bound_chain=bound_slot=-1;
            setup=false;selected=N::P_DENSITY;cache_valid=false;return;}
        const auto& u=*static_cast<const N*>(unit);bind(u);
        if(gesture.param>=0&&!owns(u))release();
        if(selected>=u.num_params){selected=N::P_DENSITY;release();}
        frame.valid=true;frame.modern=u.modern();frame.setup=setup;
        frame.bypass=u.bypass;frame.mute=chain.mute;frame.chain=g_edit_chain;frame.slot=g_edit_slot;
        frame.lock=lock_context();frame.lock_full=frame.lock&&
            g_eng->sequence_pattern(bank()).steps[g_edit_chain][g_seq_lock_step].lock_count>=SEQ_LOCKS;
        frame.selected=selected;frame.mod_links=focus_mod_links(u);
        for(int i=0;i<UNITS_PER_CHAIN;++i){frame.kinds[i]=chain.units[i]?uint8_t(chain.units[i]->kind):uint8_t(UnitKind::Empty);
            frame.bypassed[i]=chain.units[i]&&chain.units[i]->bypass;}
        for(int p=0;p<u.num_params;++p){frame.edit[p]=edit_value(u,p);
            frame.live[p]=u.modern()?u.control_value(p):u.params[p].effective;}
        const q15* s=frame.lock?frame.edit:frame.live;
        key={s[N::P_TONE],s[N::P_BODY],s[N::P_RESONANCE],s[N::P_SWEEP],
            s[N::P_ATTACK],s[N::P_DECAY],s[N::P_MODE],u.modern()};
        const bool tele=g_ui_snapshot.effect_focus_valid&&g_ui_snapshot.effect_focus_kind==uint8_t(UnitKind::Noise)&&
            g_ui_snapshot.effect_focus_chain==g_edit_chain&&g_ui_snapshot.effect_focus_slot==g_edit_slot;
        frame.audible=tele&&g_ui_snapshot.effect_focus_audible&&!u.bypass&&!chain.mute;
        if(frame.audible){frame.envelope=std::min(32767,int(g_ui_snapshot.generator_phase_hi)*256+g_ui_snapshot.generator_phase_lo);
            frame.peak=std::min(32767,int(g_ui_snapshot.generator_energy));}
    }
    const int c=colour(frame.lock?frame.edit[N::P_COLOUR]:frame.live[N::P_COLOUR]);
    for(int p=0;p<(frame.modern?N::P_COUNT:N::LEGACY_COUNT);++p)
        value_text(frame.values[p],sizeof frame.values[p],p,frame.edit[p],frame.modern,c);
    if(frame.modern){
        if(!cache_valid||cached.tone!=key.tone||cached.body!=key.body||cached.resonance!=key.resonance||
            cached.sweep!=key.sweep||cached.attack!=key.attack||cached.decay!=key.decay||
            cached.mode!=key.mode||cached.modern!=key.modern)calculate_model(key);
        else {std::memcpy(frame.base,cache_base,sizeof cache_base);std::memcpy(frame.onset,cache_onset,sizeof cache_onset);
            std::memcpy(frame.contour,cache_contour,sizeof cache_contour);}
    }
}
void noise_focus_draw(const N&){
    m::paper(320);if(!frame.valid)return;
    module_heading::header(UnitKind::Noise,m::spot());
    char text[24];std::snprintf(text,sizeof text,"ch%02d / %02d",frame.chain+1,frame.slot+1);
    ui::text(76,m::kHeaderTextY,text,m::ghost());
    if(frame.modern)ui::text(205,m::kHeaderTextY,frame.setup?"main":"setup",m::spot());
    else ui::text(170,m::kHeaderTextY,"extend",frame.lock?m::ghost():m::spot());
    ui::text(258,m::kHeaderTextY,"byp",frame.bypass?m::spot():m::ghost());
    ui::text(m::right_edge(320)-ui::text_w("x"),m::kHeaderTextY,"x",m::ghost());
    if(frame.bypass)ui::line(258,m::kHeaderMarkY,275,m::kHeaderMarkY,m::spot());
    // The held-L sheet owns the body. Do not paint ordinary control labels
    // through its depth/source rows; retain the shared module identity/footer.
    if(param_lens_is_open()){m::footer(320,"b back","l: mod >");return;}
    const q15* state=frame.lock?frame.edit:frame.live;
    const bool drone=N::drone_mode(state[N::P_MODE],state[N::P_DECAY]);
    if(frame.modern){
        if(frame.setup){
            char heading[32];std::snprintf(heading,sizeof heading,"%s / setup",colour_names[colour(state[N::P_COLOUR])]);
            ui::text(14,38,heading,m::spot());right(306,38,drone?"drone":"hit",m::ghost());
            constexpr int pp[2]={N::P_EVENT_LENGTH,N::P_SWEEP_TIME};
            constexpr int ys[2]={73,148};constexpr const char* names[2]={"event length","sweep time"};
            for(int i=0;i<2;++i){const int p=pp[i],y=ys[i];
                const bool dim=(!i&&colour(state[N::P_COLOUR])!=2)||
                    (i&&drone);
                const auto color=dim?m::ghost():frame.selected==p?m::spot():m::ink();
                ui::text(14,y,names[i],color);right(306,y,frame.values[p],color);
                module_fader::horizontal(14,306,y+19,position(p,frame.edit[p],14,306),
                    position(p,frame.live[p],14,306),{bool(frame.mod_links&(1u<<p)),dim,frame.bypass||frame.mute});
            }
            ui::text(14,111,"length of each individual crackle",m::ghost());
            ui::text(14,186,"time to return to the stored tone",m::ghost());
        }else{
            for(int i=0;i<4;++i){const int x=l::kChoice[i].x+6;
                const u32 c=i==colour(frame.edit[N::P_COLOUR])?m::spot():m::ghost();
                ui::text(x,36,colour_names[i],c);
                if(i==colour(frame.edit[N::P_COLOUR]))ui::line(x,49,x+ui::text_w(colour_names[i])-2,49,c);
            }
            for(int i=0;i<2;++i){const int x=i?70:14;const char* name=i?"drone":"hit";
                const u32 c=i==int(N::drone_mode(frame.edit[N::P_MODE],frame.edit[N::P_DECAY]))?m::spot():m::ghost();
                ui::text(x,63,name,c);if(c==m::spot())ui::line(x,75,x+ui::text_w(name)-2,75,c);
            }
            right(306,63,drone?"continuous":"a trig",m::ghost());
            for(int row=0;row<4;++row)for(int col=0;col<2;++col){
                const int p=modern_params[row][col],x=l::kColumns[col],end=l::kEnds[col],y=l::kRows[row];
                const bool dim=(p==N::P_SWEEP||p==N::P_DECAY)&&drone;
                const u32 c=dim?m::ghost():frame.selected==p?m::spot():m::ink();
                ui::text(x,y,p==N::P_DENSITY&&colour(state[N::P_COLOUR])==2?"density":modern_names[row][col],c);
                right(end,y,frame.values[p],c);
                module_fader::horizontal(x,end,y+19,position(p,frame.edit[p],x,end),
                    position(p,frame.live[p],x,end),{bool(frame.mod_links&(1u<<p)),dim,frame.bypass||frame.mute},
                    p==N::P_SWEEP?position(p,0,x,end):-1);
            }
        }
        if(frame.lock)m::footer(320,frame.lock_full?"lock full":"p-lock / name: remove",frame.setup?"main":"b back");
        else m::footer(320,frame.setup?"tap main":"b back","l: mod >");
        return;
    }
    ui::text(14,33,"colour",frame.selected==N::P_COLOUR?m::spot():m::ghost());
    const int edit_colour=colour(frame.edit[N::P_COLOUR]),live_colour=colour(frame.live[N::P_COLOUR]);
    for(int i=0;i<4;++i){
        const int x=colour_x[i];ui::text(x,45,colour_names[i],i==edit_colour?m::spot():m::ghost());
        if(i==edit_colour)ui::line(x,58,x+ui::text_w(colour_names[i])-1,58,m::spot());
        if(i==live_colour && i!=edit_colour)marker(x+ui::text_w(colour_names[i])/2,58,m::spot(),true);
    }
    for(const auto& c:controls){
        const int p=c.param;
        const bool dim=(p==N::P_DECAY && drone) || (p==N::P_DENSITY && colour(state[N::P_COLOUR])==1);
        const auto color=dim?m::ghost():frame.selected==p?m::spot():m::ink();
        ui::text(c.label_x,c.label_y,c.name,!dim && frame.selected==p?m::spot():m::ghost());
        right(c.label_x+c.extent,c.label_y,frame.values[p],color);
        const int edit=c.label_x+uni(frame.edit[p])*c.extent/32767;
        const int live=c.label_x+uni(frame.live[p])*c.extent/32767;
        module_fader::horizontal(c.label_x,c.label_x+c.extent,c.rail_y,edit,live,
            {bool(frame.mod_links&(1u<<p)),dim,frame.bypass||frame.mute});
    }
    ui::text(14,103,colour(state[N::P_COLOUR])==1?"pink / density stored":colour(state[N::P_COLOUR])==2?"event probability":"temporal grain",m::ghost());
    right(306,103,frame.bypass?"bypassed":frame.mute?"muted":drone?"decay kept":"a trig",m::ghost());
    ui::text(14,118,"mode",frame.selected==N::P_MODE?m::spot():m::ghost());
    if(N::mode_index(frame.edit[N::P_MODE])==0)ui::text(86,118,"auto",m::ghost());
    const int edit_mode=N::drone_mode(frame.edit[N::P_MODE],frame.edit[N::P_DECAY])?1:0;
    const int live_mode=N::drone_mode(frame.live[N::P_MODE],frame.live[N::P_DECAY])?1:0;
    for(int i=0;i<2;++i){
        const int x=20+i*67;const char* label=i?"drone":"hit";
        ui::text(x,137,label,i==edit_mode?m::spot():m::ghost());
        if(i==edit_mode)ui::line(x,150,x+ui::text_w(label)-1,150,m::spot());
        if(i==live_mode && i!=edit_mode)marker(x+ui::text_w(label)/2,150,m::spot(),true);
    }
    // Published pre-draw envelope and block peak only; unavailable != synthetic waveform.
    meter(122,"env",frame.envelope,frame.audible,m::ink());
    meter(144,"out",frame.peak,frame.audible,m::spot());
    ui::line(14,223,306,223,m::branch());
    if(frame.lock)m::footer(320,frame.lock_full?"lock full":"p-lock / name: remove","b back");
    else m::footer(320,"b back","l: mod >");
}
bool noise_focus_draw_top(){
    if(!frame.valid||!frame.modern||param_lens_is_open())return false;
    m::paper(400);module_heading::header(UnitKind::Noise,m::spot());
    char text[40];std::snprintf(text,sizeof text,"ch%02d / %02d",frame.chain+1,frame.slot+1);
    right(m::right_edge(400),m::kHeaderTextY,text,m::ghost());
    int rx=14;for(int i=0;i<UNITS_PER_CHAIN;++i){const auto kind=UnitKind(frame.kinds[i]);
        if(kind==UnitKind::Empty)continue;
        if(rx>14){ui::text(rx,module_heading::kRouteY,"/",m::ghost());rx+=14;}
        const char* name=m::module_name(kind);const int width=ui::text_w(name);
        if(rx+width>386)break;
        ui::text(rx,module_heading::kRouteY,name,i==frame.slot?m::spot():m::ghost());
        if(i==frame.slot)ui::line(rx,module_heading::kRouteUnderlineY,rx+width-2,module_heading::kRouteUnderlineY,m::spot());
        if(frame.bypassed[i])ui::line(rx,module_heading::kRouteCrossY,rx+width-2,module_heading::kRouteCrossY,m::ghost());
        rx+=width+8;
    }
    ui::text(14,55,"filter profile",m::ink());
    ui::line(180,59,192,59,m::ghost());ui::text(198,55,"base",m::ghost());
    ui::line(248,59,260,59,m::spot());ui::text(266,55,"onset",m::spot());
    right(386,55,"model",m::ghost());
    constexpr int x0=l::kGraphLeft,x1=l::kGraphRight,top=l::kGraphTop,bottom=l::kGraphBottom;
    for(int db:{0,-24,-48}){const int y=bottom-(db+48)*63/60;
        ui::line(x0,y,x1,y,palette::shadow());}
    ui::text(14,82,"0",m::ghost());ui::text(14,132,"-48",m::ghost());
    constexpr int ticks[5]={82,155,240,325,386};
    constexpr const char* names[5]={"50","200","1k","5k","16k"};
    for(int i=0;i<5;++i){const int x=ticks[i];
        ui::line(x,top,x,bottom,palette::shadow());
        if(i==4)right(386,143,names[i],m::ghost());
        else ui::text(x-ui::text_w(names[i])/2,143,names[i],m::ghost());}
    for(int i=1;i<l::kResponsePoints;++i){
        const int a=x0+(i-1)*(x1-x0)/(l::kResponsePoints-1),b=x0+i*(x1-x0)/(l::kResponsePoints-1);
        ui::line(a,bottom-frame.base[i-1],b,bottom-frame.base[i],m::ghost());
        ui::line(a,bottom-frame.onset[i-1],b,bottom-frame.onset[i],m::spot());
    }
    ui::text(14,163,"amp contour",m::ink());
    std::snprintf(text,sizeof text,"a %s / %s",frame.values[N::P_ATTACK],
        N::drone_mode(frame.lock?frame.edit[N::P_MODE]:frame.live[N::P_MODE],
            frame.lock?frame.edit[N::P_DECAY]:frame.live[N::P_DECAY])?"hold":frame.values[N::P_DECAY]);
    right(386,163,text,m::ghost());
    ui::line(x0,214,x1,214,palette::shadow());ui::line(x0,180,x0,214,palette::shadow());
    for(int i=1;i<l::kEnvelopePoints;++i){
        ui::line(x0+(i-1)*(x1-x0)/(l::kEnvelopePoints-1),214-frame.contour[i-1],
            x0+i*(x1-x0)/(l::kEnvelopePoints-1),214-frame.contour[i],m::ink());
    }
    if(frame.audible)std::snprintf(text,sizeof text,"out %u%%",unsigned((uint32_t(frame.peak)*100+16383)/32767));
    else std::snprintf(text,sizeof text,"out --");
    m::footer(400,frame.lock?"lock model / not audio":"filter + amp model / not audio",text);
    return true;
}
} // namespace seance::platform::ui3ds
