#include "fm_context.h"
#include "fm_editor_visual.h"
#include "ui_module_focus_internal.h"
#include "ui_sequencer.h"
#include "ui_state.h"
#include "ui_chains.h"
#include "editor_style.h"
#include "module_heading.h"
#include "module_frame_layout.h"
#include "../../core/ui/draw.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <type_traits>

namespace seance::platform::ui3ds {
namespace {
namespace m=editor_style;
using Fm=FmUnit;
enum class Figure:uint8_t {Voice,Amplitude,Index,Feedback,Pitch,Operators};
struct Plot {
    int8_t mod[fm_visual::kColumns]={},out[fm_visual::kColumns]={};
    fm_visual::Curve envelope={};
    uint16_t span_ms=300;
    int16_t pitch_cents=0;
};
struct Frame : Plot {
    ParamInspector detail;
    uint8_t kinds[UNITS_PER_CHAIN]={};
    bool bypassed[UNITS_PER_CHAIN]={};
    int8_t project=-1;
    q15 edit=0,values[12]={};
    Figure figure=Figure::Voice;
    bool dirty=false,lock=false,drone=false,phase_reset=false;
    bool safe=false,mute=false,bypass=false,extended=false;
    Fm::Wave carrier_wave=Fm::Wave::Sine,mod_wave=Fm::Wave::Sine;
};
static_assert(sizeof(Frame)<=1536&&std::is_trivially_copyable<Frame>::value,
              "bounded frozen FM upper; no engine pointers or draw-time DSP");
Frame frame;
const char* name(int p) {
    static const char* names[]={"pitch","ratio","index","feedback","amp decay","gain",
        "index fall","attack","ratio fine","feedback fall","pitch envelope","pitch env time"};
    return p>=0&&p<12?names[p]:"parameter";
}
const char* layer(uint8_t value) {
    static const char* names[]={"base","step lock","audition","perform","temp"};
    return names[value<5?value:0];
}
bool motion(int p) {
    return p==Fm::P_ATTACK||p==Fm::P_DECAY||p==Fm::P_INDEX_FALL||p==Fm::P_FEEDBACK_FALL||p==Fm::P_PITCH_ENV||p==Fm::P_PITCH_TIME;
}
bool dormant(){return frame.drone&&motion(frame.detail.param);}
int uni(q15 value){return value<0?0:value;}
int midi(q15 value){return (2400+int((int64_t(uni(value))*7200)>>15)+50)/100;}
void note(q15 raw,char* out,size_t size) {
    static const char* notes[]={"c","c#","d","d#","e","f","f#","g","g#","a","a#","b"};
    const int n=midi(raw);std::snprintf(out,size,"%s%d",notes[n%12],n/12-1);
}
void value(int p,q15 raw,char* out,size_t size) {
    switch(p) {
    case Fm::P_PITCH:note(raw,out,size);break;
    case Fm::P_RATIO:{const int q12=2048+uni(raw)*30720/32767;
        const int n=(q12*100+2048)/4096;std::snprintf(out,size,"%d.%02d",n/100,n%100);break;}
    case Fm::P_INDEX:{const int n=uni(raw)*200/32767;std::snprintf(out,size,"%d.%02d",n/100,n%100);break;}
    case Fm::P_RATIO_FINE:{const int fine=int(int64_t(raw)*25000/32767);
        std::snprintf(out,size,"%c0.%05d",raw<0?'-':'+',fine<0?-fine:fine);break;}
    case Fm::P_PITCH_ENV:{const int cents=Fm::pitch_env_cents(raw),n=cents<0?-cents:cents;
        std::snprintf(out,size,"%c%d.%02dst",cents<0?'-':'+',n/100,n%100);break;}
    case Fm::P_PITCH_TIME:std::snprintf(out,size,"%ums",unsigned(Fm::pitch_env_samples(raw)/32));break;
    case Fm::P_ATTACK:std::snprintf(out,size,"%dms",uni(raw)*20/32767);break;
    case Fm::P_DECAY:std::snprintf(out,size,"%dms",fm_visual::nominal_decay_ms(raw));break;
    default:std::snprintf(out,size,"%d",uni(raw)*99/32767);break;
    }
}
int edit_bank(){const int bank=sequencer_edit_bank();return bank>=0&&bank<NUM_PATTERNS?bank:g_eng->active_pattern;}
q15 edit_value(const Fm& fm,int p) {
    if(frame.lock){
        const auto& step=g_eng->sequence_pattern(edit_bank()).steps[g_edit_chain][g_seq_lock_step];
        for(int i=0;i<std::min(int(step.lock_count),SEQ_LOCKS);++i)
            if(step.locks[i].slot==g_edit_slot&&step.locks[i].param==p)return step.locks[i].value;
    }
    return fm.params[p].base;
}
uint32_t reference_increment(q15 pitch) {
    // Exact historical pitch_phase_increment law, including its octave origin.
    // Reference excludes external ARP/chain processing; do not repair core tuning here.
    const int n=midi(pitch);static constexpr uint32_t semi[]={65536,69433,73562,77936,82570,87480,92682,98193,104032,110218,116772,123715};
    uint64_t frequency=(uint64_t(1071616)*semi[n%12])>>16;frequency<<=n/12;
    return uint32_t((frequency<<16)/ENGINE_SR);
}
void build_voice() {
    // One bounded actual-core reset reference outside the audio guard, not live PCM.
    Fm model;model.set_extended(frame.extended);model.carrier_wave=frame.carrier_wave;model.mod_wave=frame.mod_wave;
    model.drone=frame.drone;model.alias_safe=frame.safe;model.phase_reset=frame.phase_reset;
    for(int p=0;p<model.num_params;++p){model.params[p].base=frame.values[p];model.params[p].snap();}
    model.reset();model.control_tick();model.trigger();
    const uint32_t increment=std::max<uint32_t>(1u,reference_increment(frame.values[Fm::P_PITCH]));
    const int span=std::max(2,std::min(2048,int(((uint64_t(2)<<32)+increment-1)/increment)));
    q15 a_out=0,b_out=0,a_mod=0,b_mod=0;
    model.process(&a_out,1);a_mod=model.visual_modulator();model.process(&b_out,1);b_mod=model.visual_modulator();
    int at=0;
    for(int col=0;col<fm_visual::kColumns;++col) {
        const int phase=col*span,target=phase/(fm_visual::kColumns-1),part=phase%(fm_visual::kColumns-1);
        while(at<target){a_out=b_out;a_mod=b_mod;model.process(&b_out,1);b_mod=model.visual_modulator();++at;}
        const int mod=int(a_mod)+(int(b_mod)-a_mod)*part/(fm_visual::kColumns-1);
        const int output=int(a_out)+(int(b_out)-a_out)*part/(fm_visual::kColumns-1);
        frame.mod[col]=int8_t(mod*19/32768);frame.out[col]=int8_t(output*20/32768);
    }
}
void project() {
    if(frame.figure==Figure::Operators||dormant())return;
    static Plot cached;static q15 values[12]={};static unsigned policy=~0u;
    q15 key[12]={};
    if(frame.figure==Figure::Voice)std::memcpy(key,frame.values,sizeof key);
    else if(frame.figure==Figure::Amplitude){key[Fm::P_ATTACK]=frame.values[Fm::P_ATTACK];key[Fm::P_DECAY]=frame.values[Fm::P_DECAY];}
    else if(frame.figure==Figure::Pitch){key[Fm::P_PITCH_ENV]=frame.values[Fm::P_PITCH_ENV];key[Fm::P_PITCH_TIME]=frame.values[Fm::P_PITCH_TIME];}
    else {const int p=frame.figure==Figure::Index?Fm::P_INDEX_FALL:Fm::P_FEEDBACK_FALL;key[p]=frame.values[p];}
    const unsigned next=unsigned(frame.figure)|(unsigned(frame.extended)<<3)|(unsigned(frame.drone)<<4)|
        (unsigned(frame.safe)<<5)|(unsigned(frame.phase_reset)<<6)|(unsigned(frame.carrier_wave)<<7)|(unsigned(frame.mod_wave)<<9);
    if(next==policy&&std::memcmp(values,key,sizeof values)==0){static_cast<Plot&>(frame)=cached;return;}
    switch(frame.figure){
    case Figure::Voice:build_voice();break;
    case Figure::Amplitude:fm_visual::amplitude(frame.envelope,frame.values[Fm::P_ATTACK],frame.values[Fm::P_DECAY]);frame.span_ms=300;break;
    case Figure::Index:case Figure::Feedback:
        fm_visual::falling(frame.envelope,frame.values[frame.figure==Figure::Index?Fm::P_INDEX_FALL:Fm::P_FEEDBACK_FALL]);frame.span_ms=600;break;
    case Figure::Pitch:
        frame.pitch_cents=int16_t(Fm::pitch_env_cents(frame.values[Fm::P_PITCH_ENV]));
        frame.span_ms=uint16_t(Fm::pitch_env_samples(frame.values[Fm::P_PITCH_TIME])/32);break;
    case Figure::Operators:break;
    }
    cached=static_cast<const Plot&>(frame);std::memcpy(values,key,sizeof values);policy=next;
}
void header() {
    char text[48];module_heading::header(UnitKind::Fm,m::spot());
    if(frame.project>=0)std::snprintf(text,sizeof text,"patch%02u%s",unsigned(frame.project+1),frame.dirty?" / unsaved":"");
    else std::snprintf(text,sizeof text,"new / unsaved");
    m::header_text(68,222,text,m::ghost());
    std::snprintf(text,sizeof text,"chain %02u / %s",unsigned(frame.detail.chain+1),frame.drone?"drone":"strike");
    ui::text(m::right_edge(400)-ui::text_w(text),m::kHeaderTextY,text,m::ghost());
    int total=ui::text_w("out");
    for(int s=0;s<UNITS_PER_CHAIN;++s)if(frame.kinds[s]!=uint8_t(UnitKind::Empty))total+=ui::text_w(m::module_name(UnitKind(frame.kinds[s])))+26;
    const int gap=total>352?14:26;int x=14;
    for(int s=0;s<UNITS_PER_CHAIN;++s){
        if(frame.kinds[s]==uint8_t(UnitKind::Empty))continue;
        const char* text=m::module_name(UnitKind(frame.kinds[s]));const int w=ui::text_w(text);
        ui::text(x,module_frame_layout::kRouteY,text,s==frame.detail.slot?m::spot():frame.bypassed[s]?m::ghost():m::ink());
        if(s==frame.detail.slot)ui::rect(x,module_frame_layout::kRouteUnderlineY,w-2,1,m::spot());
        if(frame.bypassed[s])ui::rect(x,module_frame_layout::kRouteCrossY,w-2,1,m::branch());
        ui::text(x+w+(gap-6)/2,module_frame_layout::kRouteY,"/",m::ghost());x+=w+gap;
    }
    ui::text(x,module_frame_layout::kRouteY,"out",m::ghost());
}
void hero() {
    const auto& d=frame.detail;const q15 chosen=frame.lock?frame.edit:d.base;
    char text[64],number[24],live[24],shown[24];
    std::snprintf(text,sizeof text,"%s / %s",name(d.param),frame.lock?"edit lock":"base");m::caption(14,56,200,text,m::spot());
    value(d.param,chosen,number,sizeof number);value(d.param,d.effective,live,sizeof live);value(d.param,chosen,shown,sizeof shown);
    const char* suffix="";
    if(d.param==Fm::P_ATTACK||d.param==Fm::P_DECAY||d.param==Fm::P_PITCH_TIME){number[std::strlen(number)-2]='\0';suffix="ms";}
    else if(d.param==Fm::P_PITCH_ENV){number[std::strlen(number)-2]='\0';suffix="st";}
    else if(d.param==Fm::P_RATIO||d.param==Fm::P_RATIO_FINE)suffix="x";
    const bool light=!motion(d.param);
    if(d.param==Fm::P_PITCH)fm_visual::note(14,101,number,m::ink(),m::spot());
    else if(fm_visual::numeric_width(number,light)+ui::text_w(suffix)+6<=196){
        const int after=fm_visual::number(14,light?101:98,number,m::ink(),m::spot(),light);
        if(*suffix)ui::text(after+6,91,suffix,m::ghost());
    }else m::caption(14,82,196,shown,m::ink());
    if(std::strcmp(live,shown)!=0)std::snprintf(text,sizeof text,"live %s",live);
    else if(frame.figure==Figure::Voice){char ratio[20];value(Fm::P_RATIO,frame.values[Fm::P_RATIO],ratio,sizeof ratio);std::snprintf(text,sizeof text,"ratio %sx",ratio);}
    else if(frame.figure==Figure::Amplitude){char attack[20];value(Fm::P_ATTACK,frame.values[Fm::P_ATTACK],attack,sizeof attack);std::snprintf(text,sizeof text,"attack %s",attack);}
    else std::snprintf(text,sizeof text,"%s",frame.extended?"extended":"legacy");
    m::caption(220,62,166,text,m::ghost());
    static const char* waves[]={"sine","tri","saw","pulse"};
    if(frame.figure==Figure::Voice||frame.figure==Figure::Operators)
        std::snprintf(text,sizeof text,"%s > %s",waves[frame.extended?unsigned(frame.mod_wave)&3u:0],waves[frame.extended?unsigned(frame.carrier_wave)&3u:0]);
    else std::snprintf(text,sizeof text,"%s",frame.figure==Figure::Amplitude?"amp envelope":frame.figure==Figure::Index?"index envelope":frame.figure==Figure::Feedback?"feedback envelope":"pitch / next strike");
    m::caption(220,78,166,text,m::ink());
    if(dormant())std::snprintf(text,sizeof text,"held in drone");
    else if(frame.lock){value(d.param,d.base,number,sizeof number);std::snprintf(text,sizeof text,"stored base %s",number);}
    else if(d.layer!=0){value(d.param,d.control,number,sizeof number);std::snprintf(text,sizeof text,"%s %s",layer(d.layer),number);}
    else std::snprintf(text,sizeof text,"%s",frame.figure==Figure::Operators?"stored shapes":frame.figure==Figure::Voice?"effective / reset":"effective / next strike");
    m::caption(220,93,166,text,m::ghost());
}
void axis(int milliseconds) {
    char text[16];
    for(int i=0;i<4;++i){const int x=14+i*124;ui::line(x,198,x,201,m::ghost());
        std::snprintf(text,sizeof text,"%d%s",milliseconds*i/3,i==3?"ms":"");
        const int tx=i==0?x:i==3?x-ui::text_w(text):x-ui::text_w(text)/2;ui::text(tx,207,text,m::ghost());}
}
void diagram() {
    const u32 spot=frame.bypass||frame.mute?m::ghost():m::spot();
    if(frame.figure==Figure::Operators){
        // Authored shape identities only: no private synthesis behind settings.
        static constexpr int wave[4][9]={{0,7,10,7,0,-7,-10,-7,0},{0,5,10,5,0,-5,-10,-5,0},{-10,-7,-5,-2,0,2,5,7,10},{10,10,10,10,-10,-10,-10,-10,10}};
        ui::text(46,119,"modulator",m::ghost());ui::text(254,119,"carrier",m::ghost());
        for(int row=0;row<2;++row){const int x=row?238:30;
            const unsigned shape=frame.extended?unsigned(row?frame.carrier_wave:frame.mod_wave)&3u:0;
            for(int i=1;i<9;++i)ui::line(x+(i-1)*16,162-wave[shape][i-1]*2,x+i*16,162-wave[shape][i]*2,row?m::ink():spot);}
        ui::text(194,158,">",m::ghost());return;
    }
    if(dormant()){ui::line(14,197,386,197,m::ghost());ui::text(14,145,"held in drone / saved settings",m::ghost());return;}
    if(frame.figure==Figure::Voice){
        ui::text(14,106,"mod",spot);ui::text(m::right_edge(400)-ui::text_w("out"),106,"out",m::ink());
        for(int y:{140,186})for(int x=14;x<386;x+=8)ui::line(x,y,std::min(x+2,386),y,palette::shadow());
        for(int i=1;i<fm_visual::kColumns;++i){
            ui::line(13+i,140-frame.mod[i-1],14+i,140-frame.mod[i],spot);
            ui::line(13+i,186-frame.out[i-1],14+i,186-frame.out[i],frame.bypass||frame.mute?m::ghost():m::ink());}
    }else if(frame.figure==Figure::Pitch){
        ui::text(14,109,"+/-48st",m::ghost());ui::line(14,157,386,157,palette::shadow());
        const int first=157-int(frame.pitch_cents)*36/4800;
        ui::line(14,first,385,157,spot);axis(frame.span_ms);
    }else{
        ui::text(14,109,"1",m::ghost());ui::line(14,197,386,197,palette::shadow());
        for(int i=1;i<fm_visual::kColumns;++i)ui::line(13+i,197-frame.envelope[i-1],14+i,197-frame.envelope[i],spot);
        for(int i=0;i<fm_visual::kColumns;i+=8)for(int y=202-frame.envelope[i];y<197;y+=6)ui::rect(14+i,y,1,1,palette::shadow());
        axis(frame.span_ms);
    }
}
void footer() {
    const auto& d=frame.detail;char route[64];
    if(d.source==ModSource::None)std::snprintf(route,sizeof route,"mod off");
    else {
        char source[20];if(mod_source_sigil(d.source)>=0)std::snprintf(source,sizeof source,"sigil %c",'a'+mod_source_sigil(d.source));
        else std::snprintf(source,sizeof source,"env ch%u",unsigned(d.env_chain+1));
        const char* response=d.source!=ModSource::EnvFollow?"":d.env_response==EnvResponse::Body?" / body":d.env_response==EnvResponse::Slow?" / slow":" / fast";
        std::snprintf(route,sizeof route,"%s %s%+d%%%s",source,d.mod_enabled?"":"muted ",int(d.depth)*100/32767,response);
        char low[24],high[24],text[96];value(d.param,d.range_min,low,sizeof low);value(d.param,d.range_max,high,sizeof high);
        std::snprintf(text,sizeof text,"range %s .. %s%s",low,high,d.clip_low||d.clip_high?" / clip":"");
        m::caption(14,218,372,text,m::ghost());
    }
    const char* state=frame.bypass?"bypass / model":frame.mute?"muted / model":dormant()?"drone / held":frame.lock?"edit lock / model":frame.figure==Figure::Voice?"2 base cycles / reset":frame.figure==Figure::Operators?"operators":frame.figure==Figure::Amplitude?"decay: nominal ms":"next strike / model";
    m::footer(400,state,route);
}
} // namespace

void fm_context_value_text(char* out,unsigned size,int param,q15 raw){if(out&&size)value(param,raw,out,size);}
void fm_context_capture(int selected) {
    frame={};
    if(!g_eng||!g_audio||g_overlay!=Overlay::Editor||selected<0||selected>=12||
       (g_view!=View::Chains&&(g_view!=View::Sequencer||!g_seq_lock_armed))||
       g_edit_chain<0||g_edit_chain>=NUM_CHAINS||g_edit_slot<0||g_edit_slot>=UNITS_PER_CHAIN)return;
    {
        platform::AudioOut::Guard guard(*g_audio);
        const Chain& chain=g_eng->chains[g_edit_chain];const Unit* unit=chain.units[g_edit_slot];
        if(!unit||unit->kind!=UnitKind::Fm)return;
        const auto& fm=*static_cast<const Fm*>(unit);
        const int page=fm_focus_page(fm);if(selected>=unit->num_params)selected=Fm::P_INDEX;
        g_eng->capture_param_inspector(g_edit_chain,g_edit_slot,selected,frame.detail);if(!frame.detail.valid)return;
        const int active=project_active_slot();frame.project=active>=0&&active<8?int8_t(active):-1;frame.dirty=project_is_dirty();
        frame.drone=fm.drone;frame.phase_reset=fm.phase_reset;frame.safe=fm.alias_safe;frame.mute=chain.mute;frame.bypass=fm.bypass;
        frame.extended=fm.extended;frame.carrier_wave=fm.carrier_wave;frame.mod_wave=fm.mod_wave;
        frame.lock=g_seq_lock_armed&&g_seq_lock_chain==g_edit_chain&&g_seq_lock_step>=0&&g_seq_lock_step<SEQ_STEPS;
        frame.edit=edit_value(fm,selected);
        for(int p=0;p<unit->num_params;++p)frame.values[p]=frame.lock?edit_value(fm,p):fm.params[p].effective;
        frame.figure=page==2?Figure::Operators:!motion(selected)?Figure::Voice:
            selected==Fm::P_PITCH_ENV||selected==Fm::P_PITCH_TIME?Figure::Pitch:
            selected==Fm::P_INDEX_FALL?Figure::Index:selected==Fm::P_FEEDBACK_FALL?Figure::Feedback:Figure::Amplitude;
        for(int s=0;s<UNITS_PER_CHAIN;++s){const Unit* at=chain.units[s];frame.kinds[s]=uint8_t(at?at->kind:UnitKind::Empty);frame.bypassed[s]=at&&at->bypass;}
    }
    project();
}
bool fm_context_draw_top(){if(!frame.detail.valid)return false;m::paper(400);header();hero();diagram();footer();return true;}
} // namespace seance::platform::ui3ds
