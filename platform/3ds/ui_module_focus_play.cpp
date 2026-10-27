#include "ui_play_editor.h"
#include "ui_module_focus_internal.h"
#include "ui_state.h"
#include "ui_theme.h"
#include "ui_param_lens.h"
#include "ui_sequencer.h"
#include "editor_style.h"
#include "module_heading.h"
#include "play_readout.h"
#include "inspector_gothic.h"
#include "audio_out.h"
#include "../../core/engine/play_window.h"
#include "../../core/engine/play_mode.h"
#include "../../core/ui/draw.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <type_traits>

namespace seance::platform::ui3ds {
namespace {
namespace m=editor_style;
using C=PlayUnit;
using Bank=trackr::synth::SampleBank;
constexpr int X=10, W=300, COLS=301;
struct Frame {
    uint16_t mod_links=0;
    bool valid=false,loaded=false,trim=false,modern=false,lock=false,full=false;
    bool bypass=false,mute=false,busy=false,playing=false,loop=false,quantize=false,undo=false,lens=false;
    int8_t chain=0,slot=0,sample=0,selected=0;
    uint8_t zoom=0;
    C::PitchMode pitch_mode=C::PitchMode::Legacy;
    q15 edit[C::P_COUNT]={},live[C::P_COUNT]={};
    uint32_t frames=0,reads=0,start=0,end=0,window_start=0,window_end=0,position=0,view_start=0,view_end=0;
    uint16_t wave_gain=256;
    int8_t low[COLS]={},high[COLS]={};
    char name[37]={},notice[40]={};
    ParamInspector detail{};
} frame;
static_assert(sizeof(Frame)<=1024 && std::is_trivially_copyable<Frame>::value,"bounded pointer-free PLAY frame");
int selected=C::P_SPEED,zoom=0,view_start=0;
bool trim=false;
uintptr_t context_unit=0,context_pcm=0;
int context_chain=-1,context_slot=-1,context_sample=-1;
uint32_t context_generation=0,context_frames=0;
const char* notice=nullptr;
enum class Gesture { None, Start, End, Speed, Gain, Pan, Gate };
struct Stroke {
    Gesture kind=Gesture::None;
    int x=0,start=0,end=32767,value=0,view=0,span=32767,step=-1,bank=-1;
    bool lock=false,remembered=false;
} stroke;
struct Undo { bool valid=false; q15 start=0,length=0,after_start=0,after_length=0; } undo;
uintptr_t preview_unit=0;
int preview_chain=-1,preview_slot=-1;
int clamp(int v,int lo=0,int hi=32767){return std::max(lo,std::min(hi,v));}
int view_span(){return 32767>>zoom;}
int min_span(){return std::max(1,int((65534ull+std::max<uint32_t>(2u,context_frames)-1)/std::max<uint32_t>(2u,context_frames)));}
bool context(){return g_eng&&g_audio&&g_overlay==Overlay::Editor&&g_edit_chain>=0&&g_edit_chain<NUM_CHAINS&&
    g_edit_slot>=0&&g_edit_slot<UNITS_PER_CHAIN&&(g_view==View::Chains||(g_view==View::Sequencer&&g_seq_lock_armed));}
bool locked(){return g_seq_lock_armed&&g_seq_lock_chain==g_edit_chain&&g_seq_lock_step>=0&&g_seq_lock_step<SEQ_STEPS;}
q15 value(const C& u,int p){return locked()?seq_display_value(g_edit_slot,p,u.params[p].base):u.params[p].base;}
bool busy(){
    if(g_eng->pending_rec_start||g_eng->pending_rec_stop||g_eng->next_recorder_deadline()!=UINT64_MAX)return true;
    for(const auto& chain:g_eng->chains)for(const Unit* u:chain.units)
        if(u&&u->kind==UnitKind::Recorder&&static_cast<const RecUnit*>(u)->writer_open())return true;
    return false;
}
void stop_preview(){
    if(preview_unit&&g_eng&&g_audio){
        platform::AudioOut::Guard guard(*g_audio);
        Unit* u=g_eng->chains[preview_chain].units[preview_slot];
        if((uintptr_t)u==preview_unit&&u->kind==UnitKind::SamplePlayer&&!g_eng->transport_playing)
            static_cast<C*>(u)->release();
    }
    preview_unit=0;preview_chain=preview_slot=-1;
}
void release(){stroke={};}
bool identify(C& u){
    if(g_eng->chains[g_edit_chain].units[g_edit_slot]!=&u)return false;
    if(u.sample_slot<0||u.sample_slot>=trackr::synth::SAMPLE_BANK_SIZE)return false;
    const auto& b=Bank::instance();const auto& s=b.slot(u.sample_slot);
    const bool same=context_unit==(uintptr_t)&u&&context_chain==g_edit_chain&&context_slot==g_edit_slot&&
        context_sample==u.sample_slot&&context_generation==b.generation(u.sample_slot)&&
        context_pcm==(uintptr_t)s.data.data()&&context_frames==s.playable_frames();
    if(!same){
        stop_preview();release();undo={};zoom=view_start=0;trim=false;notice=nullptr;
        context_unit=(uintptr_t)&u;context_chain=g_edit_chain;context_slot=g_edit_slot;context_sample=u.sample_slot;
        context_generation=b.generation(u.sample_slot);context_pcm=(uintptr_t)s.data.data();context_frames=s.playable_frames();
        selected=u.pitch_mode()==C::PitchMode::Legacy?C::P_SPEED:C::P_NOTE;
    }
    return true;
}
void write_value(C& u,int p,q15 next){
    if(p<0||p>=u.num_params||next==value(u,p))return;
    if(g_seq_lock_armed){if(locked())seq_record_lock(g_edit_slot,p,next);}
    else {u.params[p].base=next;mark_project_mutated();}
}
bool pair_room(C& u,q15 a,q15 len){
    if(!g_seq_lock_armed)return true;
    if(!locked())return false;
    // Preflight BOTH endpoint writes. No half-trim if the four lock cells are full.
    const auto& step=g_eng->pattern.steps[g_seq_lock_chain][g_seq_lock_step];
    int needed=0;
    for(int p:{C::P_START,C::P_LEN}){
        if(value(u,p)==(p==C::P_START?a:len))continue;
        bool found=false;for(int n=0;n<step.lock_count&&n<SEQ_LOCKS;++n)
            if(step.locks[n].slot==g_edit_slot&&step.locks[n].param==p)found=true;
        if(!found)++needed;
    }
    if(step.lock_count+needed>SEQ_LOCKS){g_seq_lock_flash=90;notice="trim needs two lock cells";return false;}
    return true;
}
void set_edges(C& u,int a,int b,bool remember=true){
    const int minimum=min_span();
    b=clamp(b,minimum,32767);a=clamp(a,0,b-minimum);
    const q15 start=q15(a),len=q15(b-a);
    if(start==value(u,C::P_START)&&len==value(u,C::P_LEN))return;
    if(!pair_room(u,start,len))return;
    if(!g_seq_lock_armed&&remember&&!stroke.remembered){
        undo={true,u.params[C::P_START].base,u.params[C::P_LEN].base,start,len};stroke.remembered=true;
    }
    write_value(u,C::P_START,start);write_value(u,C::P_LEN,len);
    if(!g_seq_lock_armed&&undo.valid){undo.after_start=start;undo.after_length=len;}
    stroke.remembered=true;
}
void begin(C& u,Gesture kind,int x){
    stroke={};stroke.kind=kind;stroke.x=x;stroke.start=value(u,C::P_START);
    stroke.end=clamp(stroke.start+value(u,C::P_LEN));stroke.span=view_span();stroke.view=view_start;
    stroke.lock=g_seq_lock_armed;stroke.step=g_seq_lock_step;stroke.bank=g_eng->active_pattern;
    if(kind==Gesture::Start){selected=C::P_START;g_drag=Drag::PlayStart;}
    if(kind==Gesture::End){selected=C::P_LEN;g_drag=Drag::PlayLength;}
    if(kind==Gesture::Speed){selected=u.pitch_mode()==C::PitchMode::Legacy?C::P_SPEED:C::P_NOTE;g_drag=Drag::PlaySpeed;}
    if(kind==Gesture::Gain){selected=C::P_GAIN;g_drag=Drag::PlayGain;}
    if(kind==Gesture::Pan)g_drag=Drag::PlaySliceSeam;
    if(kind==Gesture::Gate){selected=C::P_GATE;g_drag=Drag::PlayModDepth;}
    stroke.value=selected<u.num_params?value(u,selected):0;
}
void zoom_at(C& u,int next,bool fit=false){
    const int a=value(u,C::P_START),b=clamp(a+value(u,C::P_LEN));
    int centre=selected==C::P_START?a:selected==C::P_LEN?b:(a+b)/2;
    if(fit){next=0;while(next<7&&(32767>>(next+1))>=std::max(4,(b-a)*5/4))++next;centre=(a+b)/2;}
    zoom=clamp(next,0,7);view_start=clamp(centre-view_span()/2,0,32767-view_span());
}
void preview(C& u){
    if(g_seq_lock_armed){notice="preview in play, not lock editor";return;}
    if(g_eng->transport_playing){notice="stop transport to preview";return;}
    if(busy()||u.bypass||g_eng->chains[g_edit_chain].mute){notice="preview unavailable";return;}
    for(int n=g_edit_slot+1;n<UNITS_PER_CHAIN;++n){const Unit* at=g_eng->chains[g_edit_chain].units[n];
        if(at&&!at->bypass&&at->sequencer_properties().source){notice="another source follows play";return;}}
    g_eng->trigger_chain(g_edit_chain);
    u.preview(value(u,C::P_START),value(u,C::P_LEN),value(u,C::P_SLICE));
    preview_unit=(uintptr_t)&u;preview_chain=g_edit_chain;preview_slot=g_edit_slot;
    notice="preview / one pass through chain";
}
void seconds(char* text,size_t size,uint32_t frames){
    std::snprintf(text,size,"%u.%03us",unsigned(frames/32000),unsigned(uint64_t(frames%32000)*1000/32000));
}
void capture_wave(const trackr::synth::Sample& sample){
    // Eight source probes per visible column and channel. Independent of WAV length.
    // This is a sampled min/max envelope, not an exhaustive transient peak cache.
    int16_t low[COLS],high[COLS];int peak=0;
    const uint32_t span=frame.view_end-frame.view_start;
    for(int x=0;x<COLS;++x){int lo=32767,hi=-32768;
        for(int p=0;p<8;++p){const uint32_t at=frame.view_start+uint32_t(uint64_t(x*8+p)*(span-1)/(COLS*8-1));
            for(int c=0;c<sample.channels;++c){const int v=sample.data[size_t(at)*sample.channels+c];++frame.reads;lo=std::min(lo,v);hi=std::max(hi,v);}}
        low[x]=lo;high[x]=hi;peak=std::max(peak,std::max(-lo,hi));}
    frame.wave_gain=uint16_t(clamp(peak?int(24576u*256u/unsigned(peak)):256,256,16384));
    for(int x=0;x<COLS;++x){frame.low[x]=int8_t(clamp(int(low[x])*frame.wave_gain/65536,-128,127));
        frame.high[x]=int8_t(clamp(int(high[x])*frame.wave_gain/65536,-128,127));}
}
int wave_x(uint32_t at){
    const auto a=frame.view_start,b=frame.view_end;
    if(at<=a)return X;
    if(at>=b)return X+W;
    return X+int(uint64_t(at-a)*W/(b-a));
}
void text_right(int x,int y,const char* text,u32 color){ui::text(x-ui::text_w(text),y,text,color);}
void wave(int top,int bottom){
    const int zero=(top+bottom)/2,height=(bottom-top)/2-3;
    ui::line(X,zero,X+W,zero,m::branch());
    if(!frame.loaded){ui::text(100,zero-4,"no sample loaded",m::ghost());return;}
    for(int x=0;x<COLS;++x){
        const uint32_t at=frame.view_start+uint32_t(uint64_t(x)*(frame.view_end-frame.view_start)/W);
        const bool inside=at>=frame.window_start&&at<frame.window_end;
        const int a=zero-int(frame.high[x])*height/128,b=zero-int(frame.low[x])*height/128;
        ui::rect(X+x,a,1,std::max(1,b-a+1),inside?(frame.mute?m::ghost():m::ink()):m::branch());
    }
    // Separate upper/lower handle lanes remain usable even when edges share one pixel.
    for(int end=0;end<2;++end){const auto at=end?frame.end:frame.start;
        if(at<frame.view_start||at>frame.view_end)continue;
        const int x=wave_x(at);const u32 color=frame.selected==(end?C::P_LEN:C::P_START)?m::spot():m::ghost();
        ui::rect(x,top,1,bottom-top+1,color);
        const int y=end?bottom:top;ui::rect(clamp(x-4,X,X+W-8),y,9,3,color);
    }
    if(frame.playing&&frame.position>=frame.view_start&&frame.position<frame.view_end){
        const int x=wave_x(frame.position);ui::rect(x,top+5,1,bottom-top-9,m::spot());
        ui::rect(clamp(x-2,X,X+W-4),zero-2,5,5,m::spot());
    }
}
void rail(int x,int y,int w,int p,bool bipolar){
    const auto at=[&](q15 raw){const int v=clamp(raw,bipolar?-32767:0,32767);
        return x+(v+(bipolar?32767:0))*w/(bipolar?65534:32767);};
    module_fader::horizontal(x,x+w,y,at(frame.edit[p]),at(frame.live[p]),
        {bool(frame.mod_links&(1u<<p)),false,frame.bypass||frame.mute},at(bipolar?8192:32767));
}
const char* param_name(int p){static const char* names[]={"speed","start","length","loop","gain","slice","playback","gate","position","scrub","scrub style","note"};return names[clamp(p,0,C::P_COUNT-1)];}
void format_value(char* out,size_t size,int p,q15 raw){
    if(p==C::P_START||p==C::P_LEN){seconds(out,size,uint32_t(uint64_t(clamp(raw))*frame.frames/32767));return;}
    play_focus_value_text(out,size,p,raw);
}
} // namespace

int play_focus_selected(){return g_seq_lock_armed?selected:play_editor_selected();}
void play_focus_select(int p){if(p>=0&&p<C::P_COUNT)selected=p;play_editor_select(p);}
void play_focus_release(){release();play_editor_release(false);}
void play_focus_touch_release(C&){release();play_editor_release();}
bool play_focus_back(C&){if(!g_seq_lock_armed)return play_editor_back();stop_preview();release();if(trim){trim=false;return true;}return false;}
bool play_focus_touch_header(C&,int,int){return false;}
void play_focus_draw_header(const C&){}
void play_focus_value_text(char* out,size_t size,int p,q15 raw){
    // The p-lock editor owns this frozen context; normal PLAY has its own DTO.
    play_readout::value_text(out,size,p,raw,frame.modern,frame.quantize,frame.pitch_mode);
}

bool play_focus_touch_body(C& u,int tx,int ty){
    if(!g_seq_lock_armed)return play_editor_touch(u,tx,ty);
    if(!context()||param_lens_is_open())return false;
    if(ty>=25&&ty<48){
        {platform::AudioOut::Guard guard(*g_audio);if(!identify(u))return false;}
        release();notice=nullptr;
        if(g_seq_lock_armed)return true;
        stop_preview();
        if(hit(tx,ty,274,26,44,22)){
            platform::AudioOut::Guard guard(*g_audio);u.pitch_quantize=!u.pitch_quantize;mark_project_mutated();return true;
        }
        // Filesystem browser scan runs OUTSIDE the audio guard.
        if(tx<32)module_focus_touch_common_header(u,120,10);
        else if(tx>=242)module_focus_touch_common_header(u,185,10);
        else module_focus_touch_common_header(u,155,10);
        return true;
    }
    platform::AudioOut::Guard guard(*g_audio);
    if(!identify(u))return false;
    release();notice=nullptr;
    if(ty<25){
        if(tx<48){trim=false;return true;}
        if(tx<96){trim=true;if(selected!=C::P_START&&selected!=C::P_LEN)selected=C::P_START;return true;}
        if(hit(tx,ty,244,0,38,24)&&!g_seq_lock_armed){u.bypass=!u.bypass;mark_project_mutated();}
        return true;
    }
    if(ty>=225){
        if(g_seq_lock_armed)return true;
        if(tx<82)preview(u);
        else if(tx<140){u.release();preview_unit=0;notice="source released / fx tails remain";}
        else if(tx<205&&!g_seq_lock_armed){stop_preview();sequencer_open_slices(g_edit_chain);}
        return true;
    }
    if(g_seq_lock_armed&&!locked())return true;
    if(!trim){
        if(ty<72){
            if(tx<104){selected=C::P_MODE;
                if(!u.modern()){
                    if(g_seq_lock_armed){notice="enable gate outside lock editor";return true;}
                    u.set_modern(true);u.params[C::P_MODE].base=32767;u.params[C::P_LOOP].base=0;
                    mark_project_mutated();notice="gate enabled / loop off";
                }else write_value(u,C::P_MODE,32767);
            }else if(tx<208){selected=C::P_MODE;if(u.modern())write_value(u,C::P_MODE,0);}
            else {selected=C::P_LOOP;write_value(u,C::P_LOOP,value(u,C::P_LOOP)>16383?0:32767);}
            return true;
        }
        if(hit(tx,ty,10,153,142,43)){begin(u,Gesture::Speed,tx);return true;}
        if(hit(tx,ty,168,153,142,43)){begin(u,Gesture::Gain,tx);return true;}
        if(hit(tx,ty,10,199,300,24)&&u.modern()){begin(u,Gesture::Gate,tx);return true;}
        return true;
    }
    if(busy()){notice="recording / trim protected";return true;}
    if(context_frames<2)return true;
    if(hit(tx,ty,X,69,W+1,95)){
        // Finger proximity does not choose an ambiguous tiny handle: upper lane is START,
        // lower lane END. Both move relatively from the exact touch-down coordinate.
        begin(u,ty<116?Gesture::Start:Gesture::End,tx);return true;
    }
    if(ty>=167&&ty<191){
        if(tx<42)zoom_at(u,zoom-1);
        else if(tx<78)zoom_at(u,zoom+1);
        else if(tx<128)zoom_at(u,zoom,true);
        else begin(u,Gesture::Pan,tx);
        return true;
    }
    if(ty>=194&&ty<222){
        if(tx<76){selected=C::P_START;return true;}
        if(tx<151){selected=C::P_LEN;return true;}
        if(tx<230){
            const int delta=tx<190?-1:1;stroke.remembered=false;
            int a=value(u,C::P_START),b=clamp(a+value(u,C::P_LEN));
            if(selected==C::P_START)a=clamp(a+delta,0,b-min_span());else{selected=C::P_LEN;b=clamp(b+delta,a+min_span(),32767);}
            set_edges(u,a,b);return true;
        }
        if(g_seq_lock_armed){notice="undo / reset outside lock editor";return true;}
        if(tx<271){
            if(undo.valid&&u.params[C::P_START].base==undo.after_start&&u.params[C::P_LEN].base==undo.after_length){
                write_value(u,C::P_START,undo.start);write_value(u,C::P_LEN,undo.length);undo.valid=false;
            }else{undo.valid=false;notice="no trim to undo";}
        }else {set_edges(u,0,32767);zoom=view_start=0;}
        return true;
    }
    return true;
}
void play_focus_drag(C& u,int tx,int ty){
    if(!g_seq_lock_armed){play_editor_drag(u,tx,ty);return;}
    if(stroke.kind==Gesture::None)return;
    if(!context()||param_lens_is_open()||g_drag==Drag::None){release();return;}
    platform::AudioOut::Guard guard(*g_audio);
    if(!identify(u)||stroke.kind==Gesture::None)return;
    if(stroke.lock!=g_seq_lock_armed||(stroke.lock&&(!locked()||stroke.step!=g_seq_lock_step||stroke.bank!=g_eng->active_pattern))){release();return;}
    const int delta=tx-stroke.x;
    if(stroke.kind==Gesture::Pan){view_start=clamp(stroke.view-delta*stroke.span/170,0,32767-stroke.span);return;}
    if(stroke.kind==Gesture::Start||stroke.kind==Gesture::End){
        if(busy()){release();return;}
        const int d=delta*stroke.span/W;
        if(!d&&!stroke.remembered)return;
        if(stroke.kind==Gesture::Start)set_edges(u,clamp(stroke.start+d,0,stroke.end-min_span()),stroke.end);
        else set_edges(u,stroke.start,clamp(stroke.end+d,stroke.start+min_span(),32767));
    }else if(stroke.kind==Gesture::Gate){
        const int d=delta>=18?(delta-18)/40+1:delta<=-18?-((-delta-18)/40+1):0;
        write_value(u,C::P_GATE,play_mode::gate_value(clamp(play_mode::gate_index(q15(stroke.value))+d,0,5)));
    }else{
        const bool speed=stroke.kind==Gesture::Speed;
        if(speed&&selected==C::P_NOTE){const int steps=delta/4;
            write_value(u,C::P_NOTE,steps?musical_note::encode(musical_note::decode(q15(stroke.value))+steps):q15(stroke.value));
        }else write_value(u,speed?C::P_SPEED:C::P_GAIN,q15(clamp(stroke.value+delta*(speed?65534:32767)/128,speed?-32767:0,32767)));
    }
}
void play_focus_capture(){
    play_editor_capture();
    if(!g_seq_lock_armed){frame={};return;}
    frame={};
    if(!context()){stop_preview();release();context_unit=0;return;}
    platform::AudioOut::Guard guard(*g_audio);
    Unit* unit=g_eng->chains[g_edit_chain].units[g_edit_slot];
    if(!unit||unit->kind!=UnitKind::SamplePlayer){stop_preview();release();context_unit=0;return;}
    auto& u=*static_cast<C*>(unit);if(!identify(u))return;
    const auto& sample=Bank::instance().slot(u.sample_slot);
    frame.valid=true;frame.loaded=context_frames>=2&&(sample.channels==1||sample.channels==2);
    frame.trim=trim;frame.modern=u.modern();frame.pitch_mode=u.pitch_mode();frame.lock=locked();frame.full=g_seq_lock_flash>0;frame.lens=false;
    frame.bypass=u.bypass;frame.mute=g_eng->chains[g_edit_chain].mute;frame.busy=busy();frame.quantize=u.pitch_quantize;
    frame.chain=g_edit_chain;frame.slot=g_edit_slot;frame.sample=u.sample_slot;frame.selected=selected;
    if(selected>=u.num_params){selected=C::P_SPEED;frame.selected=selected;}
    frame.zoom=zoom;frame.frames=context_frames;frame.undo=undo.valid&&!frame.lock;
    frame.mod_links=focus_mod_links(u);
    for(int p=0;p<u.num_params;++p){frame.edit[p]=value(u,p);frame.live[p]=p==C::P_NOTE?u.params[p].target:u.params[p].effective;}
    g_eng->capture_param_inspector(g_edit_chain,g_edit_slot,selected,frame.detail);
    const auto& provenance=Bank::instance().provenance(u.sample_slot);
    const char* name=std::strrchr(provenance.path,'/');name=name?name+1:provenance.path;
    if(!*name)name=provenance.origin==trackr::synth::SampleOrigin::Recording?"recorded take":"sample";
    std::snprintf(frame.name,sizeof frame.name,"%s%.28s",provenance.missing?"missing ":"",name);
    if(notice)std::snprintf(frame.notice,sizeof frame.notice,"%s",notice);
    if(!frame.loaded)return;
    const auto trim_window=u.projected_window(frame.edit[C::P_START],frame.edit[C::P_LEN],0);
    frame.start=trim_window.start;frame.end=trim_window.end;
    const auto window=u.edit_source_matches()?play_edit_window(sample,frame.edit[C::P_SPEED],frame.edit[C::P_START],frame.edit[C::P_LEN],frame.edit[C::P_SLICE],u.pitch_quantize,u.modern(),u.edit_state()):PlayWindow{};
    frame.window_start=window.start;frame.window_end=window.valid?window.end:window.start;
    frame.playing=(u.visual_state()&C::VisualPlaying)&&!frame.bypass&&!frame.mute;
    frame.loop=(u.visual_state()&C::VisualLoop)!=0;
    frame.position=uint32_t(uint64_t(u.visual_position())*(context_frames-1)/32767);
    frame.view_start=trim?uint32_t(uint64_t(view_start)*context_frames/32767):0;
    frame.view_end=trim?uint32_t(uint64_t(view_start+view_span())*context_frames/32767):context_frames;
    if(frame.view_end<=frame.view_start+1){frame.view_start=std::min(frame.view_start,context_frames-2);frame.view_end=frame.view_start+2;}
    capture_wave(sample);
}
void play_focus_draw(const C&){
    if(!frame.valid){play_editor_draw();return;}
    m::paper(320);if(!frame.valid)return;
    char text[56],a[24],b[24];
    module_heading::header(UnitKind::SamplePlayer,frame.trim?m::ghost():m::spot());
    ui::text(60,m::kHeaderTextY,"trim",frame.trim?m::spot():m::ghost());
    if(frame.trim)ui::line(60,m::kHeaderMarkY,82,m::kHeaderMarkY,m::spot());
    std::snprintf(text,sizeof text,"ch%02d / %02d",frame.chain+1,frame.slot+1);ui::text(120,m::kHeaderTextY,text,m::ghost());
    ui::text(249,m::kHeaderTextY,"byp",frame.bypass?m::spot():m::ghost());text_right(m::right_edge(320),m::kHeaderTextY,"x del",m::ghost());
    ui::text(12,34,"<",m::ghost());std::snprintf(text,sizeof text,"%02d %.29s",frame.sample,frame.name);
    ui::text(32,34,text,m::ink());ui::text(250,34,">",m::ghost());ui::text(280,34,"qnt",frame.quantize?m::spot():m::ghost());
    if(!frame.trim){
        const bool gate=frame.modern&&play_mode::is_gate(frame.edit[C::P_MODE]);
        ui::text(14,56,frame.modern?"gate":"enable gate",gate?m::spot():m::ghost());
        ui::text(118,56,frame.modern?"one shot":"legacy",!gate?m::spot():m::ghost());
        ui::text(224,56,frame.edit[C::P_LOOP]>16383?"loop on":"loop off",frame.edit[C::P_LOOP]>16383?m::spot():m::ghost());
        ui::line(gate?14:118,69,gate?38:164,69,m::spot());
        seconds(a,sizeof a,frame.window_end-frame.window_start);std::snprintf(text,sizeof text,"window %s",a);ui::text(12,79,text,m::ghost());
        text_right(308,79,frame.playing?"source playing":"source idle",frame.playing?m::spot():m::ghost());
        wave(94,144);
        const int pitch=frame.pitch_mode==C::PitchMode::Legacy?C::P_SPEED:C::P_NOTE;
        ui::text(14,158,pitch==C::P_NOTE?"note":"speed",frame.selected==pitch?m::spot():m::ghost());
        play_focus_value_text(a,sizeof a,pitch,frame.edit[pitch]);text_right(146,158,a,m::ink());
        rail(16,183,128,pitch,pitch==C::P_SPEED);
        ui::text(172,158,"gain",frame.selected==C::P_GAIN?m::spot():m::ghost());
        play_focus_value_text(a,sizeof a,C::P_GAIN,frame.edit[C::P_GAIN]);text_right(306,158,a,m::ink());
        rail(174,183,128,C::P_GAIN,false);
        ui::text(14,204,"gate",frame.selected==C::P_GATE?m::spot():m::ghost());
        if(frame.modern){
            static const char* labels[]={"1/4","1/2","1","2","4","8"};
            for(int i=0;i<6;++i){const int x=69+i*42;const bool on=play_mode::gate_index(frame.edit[C::P_GATE])==i;
                ui::text(x,204,labels[i],on&&gate?m::spot():m::ghost());if(on)ui::line(x,217,x+16,217,gate?m::spot():m::branch());}
        }else ui::text(69,204,"old patch / no step gate",m::ghost());
    }else{
        seconds(a,sizeof a,frame.start);seconds(b,sizeof b,frame.end);
        std::snprintf(text,sizeof text,"in %s",a);ui::text(12,55,text,frame.selected==C::P_START?m::spot():m::ghost());
        std::snprintf(text,sizeof text,"out %s",b);text_right(308,55,text,frame.selected==C::P_LEN?m::spot():m::ghost());
        wave(76,158);
        ui::text(14,175,"-",m::ghost());ui::text(49,175,"+",m::spot());ui::text(86,175,"fit",m::ghost());
        std::snprintf(text,sizeof text,"%dx",1<<frame.zoom);ui::text(140,175,text,m::spot());
        ui::line(179,179,306,179,m::branch());
        if(frame.frames){const int left=179+int(uint64_t(frame.view_start)*127/frame.frames);
            const int right=179+int(uint64_t(frame.view_end)*127/frame.frames);ui::rect(left,177,std::max(2,right-left),5,m::spot());}
        ui::text(14,204,"start",frame.selected==C::P_START?m::spot():m::ghost());
        ui::text(88,204,"end",frame.selected==C::P_LEN?m::spot():m::ghost());
        ui::text(164,204,"-1",m::ghost());ui::text(204,204,"+1",m::spot());
        ui::text(236,204,"undo",frame.undo?m::spot():m::ghost());ui::text(282,204,"all",m::ghost());
    }
    ui::line(10,224,310,224,m::branch());
    if(frame.lock){ui::text(m::kLeft,m::kTextY,frame.full?"lock full":"p-lock / b back",m::spot());text_right(m::right_edge(320),m::kTextY,"trim: two params",m::ghost());}
    else {ui::text(m::kLeft,m::kTextY,"preview",m::spot());ui::text(91,m::kTextY,"stop",m::ghost());ui::text(152,m::kTextY,"cuts",m::ghost());text_right(m::right_edge(320),m::kTextY,"l: mod >",m::spot());}
}
bool play_focus_draw_top(){
    if(play_editor_top())return true;
    if(!frame.valid)return false;
    m::paper(400);char text[64],a[24],b[24];
    module_heading::header(UnitKind::SamplePlayer,m::spot());
    ui::text(56,m::kHeaderTextY,frame.trim?"/ trim":"/ playback",m::ghost());
    std::snprintf(text,sizeof text,"ch%02d / %02d",frame.chain+1,frame.slot+1);text_right(m::right_edge(400),m::kHeaderTextY,text,m::ghost());
    ui::text(14,37,frame.name,m::ink());
    const int p=frame.selected;
    ui::text(14,60,frame.trim&&p==C::P_LEN?"end":param_name(p),m::spot());
    ui::text(14,75,frame.lock?"edit lock":frame.detail.temp?"temporary":"edit / next trigger",m::ghost());
    if(frame.trim&&!frame.lens&&(p==C::P_START||p==C::P_LEN))seconds(a,sizeof a,p==C::P_START?frame.start:frame.end);
    else format_value(a,sizeof a,p,frame.lens&&frame.detail.temp?frame.detail.control:frame.edit[p]);
    const auto hero=inspector_gothic::draw_numeric(14,141,a,m::ink(),m::spot());
    if(hero.drawn)ui::text(hero.end_x+4,133,hero.suffix,m::spot());else ui::text(14,107,a,m::ink(),2);
    ui::line(236,59,236,204,m::branch());
    ui::text(252,61,"selected window",m::ghost());
    seconds(a,sizeof a,frame.window_start);seconds(b,sizeof b,frame.window_end);
    std::snprintf(text,sizeof text,"in  %s",a);ui::text(252,80,text,m::ink());
    std::snprintf(text,sizeof text,"out %s",b);ui::text(252,95,text,m::ink());
    ui::text(252,116,frame.modern?(play_mode::is_gate(frame.edit[C::P_MODE])?"sequencer gate":"one shot"):"legacy / ungated",m::spot());
    if(frame.modern){std::snprintf(text,sizeof text,"%s step / loop %s",play_mode::gate_label(frame.edit[C::P_GATE]),frame.edit[C::P_LOOP]>16383?"on":"off");ui::text(252,131,text,m::ghost());}
    const auto& d=frame.detail;
    std::snprintf(text,sizeof text,"mod %s",mod_source_name(d.source));ui::text(252,157,text,m::ghost());
    std::snprintf(text,sizeof text,"depth %+d%%",int(d.depth)*100/32767);ui::text(252,172,text,m::ghost());
    ui::text(14,163,frame.playing?"source playing / real position":"source idle",m::spot());
    if(frame.trim){const uint64_t us=uint64_t(frame.frames)*1000000/(32767ull*32000);
        std::snprintf(text,sizeof text,"fine 1 unit ~ %llu us",(unsigned long long)us);ui::text(14,181,text,m::ghost());
        ui::text(14,197,"upper: start / lower: end",m::ghost());
    }else{
        std::snprintf(text,sizeof text,"wave x%u.%u / visual only",frame.wave_gain/256,frame.wave_gain%256*10/256);ui::text(14,181,text,m::ghost());
        ui::text(14,197,"gate stops source, not fx tails",m::ghost());
    }
    const char* status=frame.notice[0]?frame.notice:frame.busy?"recording / trim protected":frame.bypass?"bypassed":frame.mute?"chain muted":frame.trim?(!frame.modern&&frame.edit[C::P_SLICE]>1927?"legacy cut ignores trim / play > enable gate":"non-destructive / original sample unchanged"):"window + loop latch on trigger / speed + gain live";
    m::footer(400,status,"");
    return true;
}
} // namespace seance::platform::ui3ds
