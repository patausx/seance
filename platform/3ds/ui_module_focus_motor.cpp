#include "ui_module_focus_internal.h"
#include "motor_editor_visual.h"
#include "ui_state.h"
#include "ui_sequencer.h"
#include "ui_param_lens.h"
#include "module_inspector.h"
#include "editor_style.h"
#include "module_heading.h"
#include "audio_out.h"
#include "../../core/engine/musical_note.h"
#include "../../core/ui/draw.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <type_traits>

namespace seance::platform::ui3ds {
namespace {
namespace m=editor_style;
namespace l=motor_editor_visual;
using M=MotorUnit;
constexpr int FFT=4096,MAX_SAMPLES=8192, SPEC=80;
constexpr int params[4][2]={{M::P_RPM,M::P_SYNC},{M::P_WIDTH,M::P_LOAD},
                             {M::P_JITTER,M::P_JITTER_SPEED},{M::P_RESONANCE,M::P_GAIN}};
constexpr const char* names[4][2]={{"rate","hard sync"},{"width","load"},
                                    {"jitter","jitter speed"},{"resonance","gain"}};
constexpr const char* source_names[3]={"saw","pulse","mix"};
constexpr int source_x[3]={30,91,151};
constexpr int source_left[3]={8,62,121};
constexpr int source_right[3]={58,119,182};
struct Frame {
    q15 edit[M::P_COUNT]={},live[M::P_COUNT]={},base[M::P_COUNT]={};
    int8_t raw_lo[l::kCols]={},raw_hi[l::kCols]={},out_lo[l::kCols]={},out_hi[l::kCols]={};
    uint8_t spectrum[SPEC]={};
    uint16_t spectrum_labels[5]={};
    char values[M::P_COUNT][18]={},nominal_note[6]={};
    uint16_t links=0,energy=0;
    uint8_t kinds[UNITS_PER_CHAIN]={};bool bypassed[UNITS_PER_CHAIN]={};
    int32_t nominal=0,window_ms10=0;
    int8_t chain=0,slot=0,selected=M::P_RPM,arp_offset=0,arp_owner=-1;
    bool valid=false,modern=false,bypass=false,mute=false;
    bool audible=false,sync_limited=false,arp_gate=false;
} frame;
static_assert(sizeof(Frame)<2048 && std::is_trivially_copyable<Frame>::value,
              "MOTOR GPU draws only a bounded pointer-free frame");
struct Gesture {
    Engine* engine=nullptr; uintptr_t unit=0;
    View view=View::Chains;
    int chain=-1,slot=-1,bank=-1,param=-1,start=0;
    q15 value=0;bool modern=false,note=false;
} gesture;
struct Key { q15 values[M::P_COUNT]={};int8_t arp_offset=0,arp_owner=-1;bool modern=false,arp_gate=false; };
Key cached_key={}; bool cache_valid=false;
int selected=M::P_RPM,bound_chain=-1,bound_slot=-1;
uintptr_t bound_unit=0;bool bound_modern=false;
// Fixed scratch is used in capture only, never in draw or on the audio thread.
q15 raw_scratch[MAX_SAMPLES],out_scratch[MAX_SAMPLES];
float fft_real[FFT],fft_imag[FFT],hann[FFT];
float roots_re[12],roots_im[12];int8_t bins[FFT/2+1];bool fft_ready=false;
void prepare_fft(){if(fft_ready)return;
    constexpr double PI=3.14159265358979323846;
    const double lo=std::log(50.),span=std::log(16000.)-lo;
    for(int i=0;i<FFT;++i)hann[i]=float(.5-.5*std::cos(2*PI*i/(FFT-1)));
    for(int stage=0;stage<12;++stage){const int len=2<<stage;
        roots_re[stage]=float(std::cos(2*PI/len));roots_im[stage]=float(-std::sin(2*PI/len));}
    bins[0]=-1;for(int i=1;i<=FFT/2;++i){const double hz=double(i)*ENGINE_SR/FFT;
        bins[i]=hz<50||hz>16000?-1:int8_t(std::min(SPEC-1,int((std::log(hz)-lo)*SPEC/span)));}
    fft_ready=true;
}
int clamp(int v,int lo=0,int hi=32767){return std::max(lo,std::min(hi,v));}
bool hit(int x,int y,int px,int py,int w,int h){return x>=px&&x<px+w&&y>=py&&y<py+h;}
int source_index(q15 value){return std::min(2,(clamp(value)*3)>>15);}
bool context(){return g_eng&&g_audio&&g_overlay==Overlay::Editor&&
    g_view==View::Chains&&
    g_edit_chain>=0&&g_edit_chain<NUM_CHAINS&&g_edit_slot>=0&&g_edit_slot<UNITS_PER_CHAIN;}
void release(){gesture={};if(g_drag==Drag::MotorRpm)g_drag=Drag::None;}
void bind(const M& u){if(bound_chain!=g_edit_chain||bound_slot!=g_edit_slot||bound_unit!=uintptr_t(&u)||bound_modern!=u.modern()){
    release();selected=M::P_RPM;bound_chain=g_edit_chain;bound_slot=g_edit_slot;
    bound_unit=uintptr_t(&u);bound_modern=u.modern();cache_valid=false;
}}
q15 edit_value(const M& u,int p){
    return u.params[p].base;
}
void set_value(M& u,int p,q15 v){ // caller holds guard
    if(p<0||p>=u.num_params||edit_value(u,p)==v)return;
    u.params[p].base=v;
    mark_project_mutated();cache_valid=false;
}
void begin(M& u,int p,int x){
    selected=p;gesture={};gesture.engine=g_eng;gesture.unit=uintptr_t(&u);
    gesture.view=g_view;gesture.chain=g_edit_chain;gesture.slot=g_edit_slot;
    gesture.bank=g_eng->active_pattern;
    gesture.param=p;gesture.start=x;gesture.value=edit_value(u,p);
    gesture.modern=u.modern();gesture.note=edit_value(u,M::P_MODE)>=16384;g_drag=Drag::MotorRpm;g_drag_param=p;
}
bool owns(const M& u){return gesture.engine==g_eng&&gesture.unit==uintptr_t(&u)&&
    gesture.view==g_view&&gesture.chain==g_edit_chain&&gesture.slot==g_edit_slot&&
    gesture.bank==g_eng->active_pattern&&gesture.param==selected&&
    gesture.modern==u.modern()&&gesture.note==(edit_value(u,M::P_MODE)>=16384)&&
    g_eng->chains[g_edit_chain].units[g_edit_slot]==&u;}
void right(int x,int y,const char* s,u32 color){ui::text(x-ui::text_w(s),y,s,color);}
int ordinate(q15 sample){return clamp(int(sample)*30/32768,-30,30);}
void value(char* out,size_t n,int p,q15 v,bool note){
    (void)note; // RPM is the stored free-rate even while a NOTE is sounding.
    if(p==M::P_RPM){const unsigned hz=M::rate_millihz(v);
        std::snprintf(out,n,"%u.%uhz",hz/1000,(hz%1000)/100);}
    else if(p==M::P_NOTE)musical_note::name(out,n,musical_note::decode(v));
    else if(p==M::P_SOURCE)std::snprintf(out,n,"%s",source_names[source_index(v)]);
    else if(p==M::P_MODE)std::snprintf(out,n,"%s",v>=16384?"note":"free");
    else if(p==M::P_SYNC){const uint32_t r=M::sync_ratio_q16(v);
        const uint32_t centi=uint32_t((uint64_t(r)*100+32768)/65536);
        if(r<=65536)std::snprintf(out,n,"off");
        else std::snprintf(out,n,"%u.%02ux",unsigned(centi/100),unsigned(centi%100));}
    else if(p==M::P_JITTER_SPEED){const uint32_t hz=M::jitter_millihz(v);
        std::snprintf(out,n,"%u.%uhz",unsigned(hz/1000),unsigned((hz%1000)/100));}
    else if(p==M::P_WIDTH){const uint32_t width=M::width_q32(v);std::snprintf(out,n,"%u%%",unsigned((uint64_t(width)*100+0x80000000ull)>>32));}
    else std::snprintf(out,n,"%u%%",unsigned((uint32_t(clamp(v))*100+16383)/32767));
}
void transform(float* re,float* im){
    for(int i=1,j=0;i<FFT;++i){int bit=FFT>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;
        if(i<j){std::swap(re[i],re[j]);std::swap(im[i],im[j]);}}
    for(int len=2,stage=0;len<=FFT;len<<=1,++stage){const float cr=roots_re[stage],ci=roots_im[stage];
        for(int start=0;start<FFT;start+=len){float wr=1,wi=0;
            for(int j=0;j<len/2;++j){const int a=start+j,b=a+len/2;
                const float vr=wr*re[b]-wi*im[b],vi=wr*im[b]+wi*re[b];
                re[b]=re[a]-vr;im[b]=im[a]-vi;re[a]+=vr;im[a]+=vi;
                const float next=wr*cr-wi*ci;wi=wr*ci+wi*cr;wr=next;
            }
        }
    }
}
void model(const Key& key){
    prepare_fft();
    const double low=std::log(50.),range=std::log(16000.)-low;
    constexpr int hz_labels[5]={50,200,1000,5000,16000};
    for(int i=0;i<5;++i)frame.spectrum_labels[i]=uint16_t(l::kPlotLeft+int(
        (std::log(double(hz_labels[i]))-low)*(l::kCols-1)/range+0.5));
    M u(key.modern);for(int p=0;p<u.num_params;++p){u.params[p].base=key.values[p];u.params[p].target=key.values[p];u.params[p].effective=key.values[p];}
    u.reset();u.arp_voice.owner=key.arp_owner;u.arp_voice.semitones=key.arp_offset;
    u.arp_voice.gate=key.arp_gate;u.control_tick();
    frame.nominal=int32_t(u.nominal_millihz());
    if(key.modern&&key.values[M::P_MODE]>=16384 &&
       musical_note::decode(key.values[M::P_NOTE])+key.arp_offset<=127 &&
       ((uint64_t(M::note_millihz(key.values[M::P_NOTE]))*Unit::arp_ratio_q24(key.arp_offset)+(1u<<23))>>24)<=14000000u)
        musical_note::name(frame.nominal_note,sizeof frame.nominal_note,
            clamp(musical_note::decode(key.values[M::P_NOTE])+key.arp_offset,0,127));
    else frame.nominal_note[0]='\0';
    const uint32_t hz=uint32_t(std::max<int32_t>(18000,frame.nominal));
    const int window=clamp(int((uint64_t(ENGINE_SR)*3000+hz/2)/hz),1,MAX_SAMPLES);
    const int count=std::max(FFT,window);
    frame.window_ms10=int((uint64_t(window)*10000+ENGINE_SR/2)/ENGINE_SR);
    // The window is three NOMINAL master cycles; jitter may vary individual crossings.
    constexpr int BLOCK=128;
    for(int i=0;i<count;i+=BLOCK)u.process_with_raw(out_scratch+i,raw_scratch+i,std::min(BLOCK,count-i));
    frame.sync_limited=u.sync_limited();
    const int start=count-window;
    for(int x=0;x<l::kCols;++x){
        int first=start+x*window/l::kCols,last=start+(x+1)*window/l::kCols;
        if(first==last)last=first+1;
        last=std::min(last,count);
        q15 rlo=32767,rhi=-32768,olo=32767,ohi=-32768;
        for(int i=first;i<last;++i){rlo=std::min(rlo,raw_scratch[i]);rhi=std::max(rhi,raw_scratch[i]);
            olo=std::min(olo,out_scratch[i]);ohi=std::max(ohi,out_scratch[i]);}
        frame.raw_lo[x]=int8_t(ordinate(rlo));frame.raw_hi[x]=int8_t(ordinate(rhi));
        frame.out_lo[x]=int8_t(ordinate(olo));frame.out_hi[x]=int8_t(ordinate(ohi));
    }
    float mean=0;for(int i=count-FFT;i<count;++i)mean+=out_scratch[i]/32768.f;mean/=FFT;
    for(int i=0;i<FFT;++i){fft_real[i]=(out_scratch[count-FFT+i]/32768.f-mean)*hann[i];fft_imag[i]=0;}
    transform(fft_real,fft_imag);
    std::memset(frame.spectrum,0,sizeof frame.spectrum);
    float spectrum_peak[SPEC]={};
    for(int i=1;i<=FFT/2;++i){const int b=bins[i];if(b<0)continue;
        const float energy=fft_real[i]*fft_real[i]+fft_imag[i]*fft_imag[i];
        spectrum_peak[b]=std::max(spectrum_peak[b],energy);
    }
    for(int b=0;b<SPEC;++b){
        // Coherent Hann gain FFT/4; fixed 0dBFS reference, no frame-by-frame normalization.
        const double amp=std::sqrt(double(spectrum_peak[b]))*(4.0/FFT);
        const double db=20*std::log10(std::max(amp,1e-12));
        frame.spectrum[b]=uint8_t(clamp(int((db+60)*39/60),0,39));
    }
    cached_key=key;cache_valid=true;
}
} // namespace

void motor_focus_reset_reference(q15* pre,q15* out,int frames,q15 rpm,q15 jitter,
                                 q15 resonance,q15 load,q15 gain,q15 source){
    if(frames<=0)return;
    M u(false);const q15 values[6]={rpm,jitter,resonance,load,gain,source};
    for(int p=0;p<6;++p)u.params[p].base=u.params[p].effective=u.params[p].target=values[p];
    u.reset();u.control_tick();
    // Historical caller permits a null output tap; process the actual legacy DSP.
    constexpr int BLOCK=128;q15 discard[BLOCK];
    for(int i=0;i<frames;i+=BLOCK)u.process_with_raw(out?out+i:discard,pre?pre+i:nullptr,std::min(BLOCK,frames-i));
}
int motor_focus_selected(){return selected;}
void motor_focus_select(int p){if(p>=0&&p<M::P_COUNT){if(selected!=p)release();selected=p;}}
void motor_focus_release(){release();}
void motor_focus_value_text(char* out,size_t n,int p,q15 v,bool note){value(out,n,p,v,note);}
bool motor_focus_modern(){return frame.valid&&frame.modern;}
bool motor_focus_note_mode(){return frame.valid&&frame.modern&&frame.edit[M::P_MODE]>=16384;}
bool motor_focus_touch_body(M& u,int tx,int ty){
    if(!context()||param_lens_is_open()){release();return false;}
    platform::AudioOut::Guard guard(*g_audio);release();
    if(g_eng->chains[g_edit_chain].units[g_edit_slot]!=&u)return false;
    bind(u);
    if(ty<26){if(hit(tx,ty,246,0,38,25)){u.bypass=!u.bypass;mark_project_mutated();}return true;}
    if(!u.modern()&&hit(tx,ty,184,27,48,33)){
        {M defaults;for(int p=M::P_WIDTH;p<M::P_COUNT;++p)u.params[p]=defaults.params[p];
            // Legacy scene padding is not authored PWM/note data. Initialize the
            // appended values on explicit conversion, preserving all six old ones.
            for(auto& scene:g_eng->scenes[g_edit_chain])if(scene.kind[g_edit_slot]==UnitKind::Motor)
                for(int p=M::P_WIDTH;p<M::P_COUNT;++p)scene.base[g_edit_slot][p]=defaults.params[p].base;
            u.set_modern(true);mark_project_mutated();bind(u);}return true;}
    if(hit(tx,ty,8,29,175,31)){
        for(int i=0;i<3;++i)if(hit(tx,ty,source_left[i],29,source_right[i]-source_left[i],31)){
            selected=M::P_SOURCE; // choice buttons write the stored base
            static constexpr q15 pick[3]={0,16384,32767};set_value(u,selected,pick[i]);return true;}
        return true;
    }
    if(u.modern()&&hit(tx,ty,224,29,90,31)){
        selected=M::P_MODE; // tapping free must not reveal a stored note-mode base
        set_value(u,selected,tx<269?0:32767);return true;}
    for(int row=0;row<4;++row)for(int col=0;col<2;++col){
        const int x=col?l::kRight:l::kLeft,p=params[row][col];
        if(!hit(tx,ty,x-6,l::kHitY[row],146,40))continue;
        selected=(p==M::P_RPM&&u.modern()&&edit_value(u,M::P_MODE)>=16384)?M::P_NOTE:p;
        if((!u.modern()&&(p==M::P_SYNC||p==M::P_WIDTH||p==M::P_JITTER_SPEED))||
           (p==M::P_WIDTH&&source_index(edit_value(u,M::P_SOURCE))==0))return true;
        begin(u,selected,tx);return true;
    }
    return true;
}
void motor_focus_drag(M& u,int tx,int){
    if(gesture.param<0)return;
    if(!context()||param_lens_is_open()||g_drag!=Drag::MotorRpm){release();return;}
    platform::AudioOut::Guard guard(*g_audio);if(!owns(u)){release();return;}
    if(gesture.param==M::P_WIDTH&&source_index(edit_value(u,M::P_SOURCE))==0){release();return;}
    const int dx=tx-gesture.start;
    if(gesture.param==M::P_NOTE){const int step=dx/4;
        set_value(u,M::P_NOTE,step?musical_note::encode(clamp(musical_note::decode(gesture.value)+step,
            musical_note::MIN,musical_note::MAX)):gesture.value);}
    else set_value(u,gesture.param,q15(clamp(int(gesture.value)+dx*32767/296)));
}
void motor_focus_capture(){
    frame.valid=false;if(!context()){release();return;}
    Key key={};
    {
        platform::AudioOut::Guard guard(*g_audio);
        const auto& chain=g_eng->chains[g_edit_chain];const Unit* unit=chain.units[g_edit_slot];
        if(!unit||unit->kind!=UnitKind::Motor){release();return;}const auto& u=*static_cast<const M*>(unit);
        bind(u);if(gesture.param>=0&&!owns(u))release();
        frame.valid=true;frame.chain=g_edit_chain;frame.slot=g_edit_slot;frame.selected=selected;
        frame.modern=u.modern();frame.bypass=u.bypass;frame.mute=chain.mute;
        frame.links=focus_mod_links(u);
        for(int i=0;i<UNITS_PER_CHAIN;++i){frame.kinds[i]=chain.units[i]?uint8_t(chain.units[i]->kind):uint8_t(UnitKind::Empty);
            frame.bypassed[i]=chain.units[i]&&chain.units[i]->bypass;}
        for(int p=0;p<M::P_COUNT;++p){if(p<u.num_params){frame.edit[p]=edit_value(u,p);
            frame.live[p]=p==M::P_NOTE?u.params[p].target:u.params[p].effective;frame.base[p]=u.params[p].base;}
            key.values[p]=frame.live[p];}
        // The mode switch is decided by the actual target, not the slewed continuous value.
        if(u.modern()){key.values[M::P_MODE]=u.params[M::P_MODE].target;
            key.values[M::P_NOTE]=u.params[M::P_NOTE].target;}
        key.modern=frame.modern;
        if(u.note_mode()&&u.arp_voice.owner>=0){key.arp_owner=u.arp_voice.owner;
            key.arp_offset=u.arp_voice.semitones;key.arp_gate=u.arp_voice.gate;}
        frame.arp_offset=key.arp_offset;frame.arp_owner=key.arp_owner;frame.arp_gate=key.arp_gate;
        const bool tele=g_ui_snapshot.effect_focus_valid&&g_ui_snapshot.effect_focus_kind==uint8_t(UnitKind::Motor)&&
            g_ui_snapshot.effect_focus_chain==g_edit_chain&&g_ui_snapshot.effect_focus_slot==g_edit_slot;
        frame.audible=tele&&g_ui_snapshot.effect_focus_audible&&!u.bypass&&!chain.mute;
        frame.energy=frame.audible?g_ui_snapshot.generator_energy:0;
    }
    const bool note=frame.modern&&key.values[M::P_MODE]>=16384;
    for(int p=0;p<M::P_COUNT;++p)value(frame.values[p],sizeof frame.values[p],p,frame.edit[p],note);
    if(!cache_valid||key.modern!=cached_key.modern||key.arp_owner!=cached_key.arp_owner||
       key.arp_offset!=cached_key.arp_offset||key.arp_gate!=cached_key.arp_gate||
       !std::equal(std::begin(key.values),std::end(key.values),std::begin(cached_key.values)))model(key);
}
void motor_focus_draw(const M&){
    m::paper(320);if(!frame.valid)return;
    module_heading::header(UnitKind::Motor,m::spot());char text[32];
    std::snprintf(text,sizeof text,"ch%02d / %02d",frame.chain+1,frame.slot+1);
    ui::text(102,m::kHeaderTextY,text,m::ghost());
    ui::text(258,m::kHeaderTextY,"byp",frame.bypass?m::spot():m::ghost());
    right(m::right_edge(320),m::kHeaderTextY,"x",m::ghost());
    if(frame.bypass)ui::line(258,m::kHeaderMarkY,275,m::kHeaderMarkY,m::spot());
    for(int i=0;i<3;++i){ui::text(source_x[i]-ui::text_w(source_names[i])/2,38,source_names[i],
            i==source_index(frame.edit[M::P_SOURCE])?m::spot():m::ghost());
        if(i==source_index(frame.edit[M::P_SOURCE]))ui::rect(source_x[i]-8,52,17,1,m::spot());}
    if(frame.modern){const bool note=frame.edit[M::P_MODE]>=16384;
        ui::text(224,38,"free",note?m::ghost():m::spot());ui::text(276,38,"note",note?m::spot():m::ghost());
        ui::rect(note?276:224,52,note?25:25,1,m::spot());}
    else ui::text(184,38,"extend",m::spot());
    const bool note=frame.modern&&frame.edit[M::P_MODE]>=16384;
    for(int row=0;row<4;++row)for(int col=0;col<2;++col){
        const int x=col?l::kRight:l::kLeft,y=l::kLabelY[row],p=params[row][col];
        const int shown=(p==M::P_RPM&&note)?M::P_NOTE:p;
        const bool disabled=(!frame.modern&&(p==M::P_SYNC||p==M::P_WIDTH||p==M::P_JITTER_SPEED))||
            (p==M::P_WIDTH&&source_index(frame.edit[M::P_SOURCE])==0);
        const u32 color=disabled?m::ghost():frame.selected==shown?m::spot():m::ink();
        ui::text(x,y,p==M::P_RPM&&note?"pitch":names[row][col],color);
        if(!disabled)right(x+l::kRailLength,y,frame.values[shown],color);
        else if(p==M::P_WIDTH&&frame.modern)right(x+l::kRailLength,y,"saw: n/a",m::ghost());
        const int edit=x+clamp(frame.edit[shown])*l::kRailLength/32767;
        const int live=x+clamp(frame.live[shown])*l::kRailLength/32767;
        module_fader::horizontal(x,x+l::kRailLength,l::kRailY[row],edit,live,
            {bool(frame.links&(1u<<shown)),disabled,frame.bypass||frame.mute});
    }
    if(frame.arp_owner>=0&&note){std::snprintf(text,sizeof text,"arp %+dst",int(frame.arp_offset));
        m::footer(320,text,"l: mod >");}
    else m::footer(320,frame.mute?"muted":frame.bypass?"bypassed":"outline base / fill live","l: mod >");
}
bool motor_focus_draw_top(){
    if(!frame.valid||param_lens_is_open())return false;
    m::paper(400);module_heading::header(UnitKind::Motor,m::spot());char text[48];
    std::snprintf(text,sizeof text,"ch%02d / %02d",frame.chain+1,frame.slot+1);
    right(m::right_edge(400),m::kHeaderTextY,text,m::ghost());
    int route_x=14;
    for(int i=0;i<UNITS_PER_CHAIN;++i){const UnitKind kind=UnitKind(frame.kinds[i]);if(kind==UnitKind::Empty)continue;
        if(route_x>14){ui::text(route_x-9,module_heading::kRouteY,"/",m::ghost());}
        const char* name=m::module_name(kind);const int w=ui::text_w(name);
        ui::text(route_x,module_heading::kRouteY,name,i==frame.slot?m::spot():m::ghost());
        if(i==frame.slot)ui::line(route_x,module_heading::kRouteUnderlineY,route_x+w-2,module_heading::kRouteUnderlineY,m::spot());
        if(frame.bypassed[i])ui::line(route_x,module_heading::kRouteCrossY,route_x+w-2,module_heading::kRouteCrossY,m::ghost());
        route_x+=w+12;
    }
    ui::line(32,58,42,58,m::ghost());ui::text(47,54,"raw",m::ghost());
    ui::line(94,58,104,58,m::spot());ui::text(110,54,"out",m::spot());
    const uint32_t sync=M::sync_ratio_q16(frame.live[M::P_SYNC]);
    if(frame.modern&&sync>65536){const uint32_t centi=uint32_t((uint64_t(sync)*100+32768)/65536);
        std::snprintf(text,sizeof text,"sync %u.%02ux%s",unsigned(centi/100),unsigned(centi%100),
            frame.sync_limited?" limited":"");}
    else std::snprintf(text,sizeof text,"sync off");
    right(386,54,text,m::ghost());
    for(int y:{l::kPlotTop,l::kPlotZero,l::kPlotBottom})ui::line(l::kPlotLeft,y,l::kPlotRight,y,palette::shadow());
    for(int x:{l::kPlotLeft,l::kPlotLeft+l::kCols/3,l::kPlotLeft+2*l::kCols/3,l::kPlotRight})
        ui::line(x,l::kPlotTop,x,l::kPlotBottom,palette::shadow());
    ui::text(14,l::kPlotTop-3,"+1",m::ghost());ui::text(20,l::kPlotZero-3,"0",m::ghost());
    ui::text(14,l::kPlotBottom-3,"-1",m::ghost());
    for(int x=0;x<l::kCols;++x){const int at=l::kPlotLeft+x;
        if(x){const int last=x-1;
            ui::line(at-1,l::kPlotZero-(int(frame.raw_hi[last])+frame.raw_lo[last])/2,
                     at,l::kPlotZero-(int(frame.raw_hi[x])+frame.raw_lo[x])/2,m::ghost());
            ui::line(at-1,l::kPlotZero-(int(frame.out_hi[last])+frame.out_lo[last])/2,
                     at,l::kPlotZero-(int(frame.out_hi[x])+frame.out_lo[x])/2,m::spot());
        }
        ui::line(at,l::kPlotZero-frame.raw_hi[x],at,l::kPlotZero-frame.raw_lo[x],m::ghost());
        ui::line(at,l::kPlotZero-frame.out_hi[x],at,l::kPlotZero-frame.out_lo[x],m::spot());}
    ui::text(l::kPlotLeft,142,"0ms",m::ghost());
    std::snprintf(text,sizeof text,"%d.%dms / 3 nominal cycles",int(frame.window_ms10/10),int(frame.window_ms10%10));
    right(386,142,text,m::ghost());
    ui::text(14,157,"spectrum",m::ghost());right(386,157,"-60..0dbfs / 7.8hz bin",m::ghost());
    ui::line(l::kPlotLeft,l::kSpectrumBottom,l::kPlotRight,l::kSpectrumBottom,palette::shadow());
    for(int i=0;i<SPEC;++i){const int x=l::kPlotLeft+i*(l::kCols-1)/(SPEC-1);
        if(frame.spectrum[i])ui::line(x,l::kSpectrumBottom,x,l::kSpectrumBottom-frame.spectrum[i],m::spot());}
    ui::text(frame.spectrum_labels[0],211,"50",m::ghost());
    ui::text(frame.spectrum_labels[1],211,"200",m::ghost());
    ui::text(frame.spectrum_labels[2],211,"1k",m::ghost());
    ui::text(frame.spectrum_labels[3],211,"5k",m::ghost());
    right(frame.spectrum_labels[4]+1,211,"16k",m::ghost());
    const int p=frame.selected;
    const char* label=p==M::P_NOTE?"note":p==M::P_SYNC?"sync":p==M::P_JITTER_SPEED?"jitter hz":
        p==M::P_WIDTH?"width":p==M::P_MODE?"mode":p==M::P_SOURCE?"source":
        p==M::P_RPM?"rate":p==M::P_JITTER?"jitter":p==M::P_RESONANCE?"res":p==M::P_GAIN?"gain":"load";
    std::snprintf(text,sizeof text,"reset model / %s %s",label,frame.values[p]);
    ui::text(m::kLeft,m::kTextY,text,m::ghost());
    if(frame.nominal_note[0])std::snprintf(text,sizeof text,"%s / %u.%uhz nominal",frame.nominal_note,
        unsigned(frame.nominal/1000),unsigned((frame.nominal%1000)/100));
    else std::snprintf(text,sizeof text,"%u.%uhz nominal",unsigned(frame.nominal/1000),
        unsigned((frame.nominal%1000)/100));
    right(m::right_edge(400),m::kTextY,text,m::ghost());
    return true;
}
} // namespace seance::platform::ui3ds
