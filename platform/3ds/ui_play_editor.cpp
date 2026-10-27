#include "ui_play_editor.h"
#include "ui_play_export.h"
#include "ui_module_focus_internal.h"
#include "ui_state.h"
#include "ui_theme.h"
#include "ui_palette.h"
#include "ui_sequencer.h"
#include "ui_param_lens.h"
#include "ui_sample_library.h"
#include "audio_out.h"
#include "editor_style.h"
#include "module_heading.h"
#include "play_readout.h"
#include "play_editor_layout.h"
#include "play_editor_type.h"
#include "../../core/ui/draw.h"
#include "../../core/ui/play_editor_tools.h"
#include "../../core/engine/play_window.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <type_traits>

namespace seance::platform::ui3ds {
namespace {
namespace m=editor_style; namespace pe=play_editor; namespace layout=play_editor_layout; namespace type=play_editor_type;
using C=PlayUnit;using Bank=trackr::synth::SampleBank;
constexpr int X=14,W=207,N=208,OV=360,HISTORY=16;
// Tool selection is orthogonal to the visible bounded sheet and pitch engine.
enum class Page:uint8_t {Trim,Cuts,Loop,Config,Auto,Edit,Tools,Fades,Zoom,History,Seq,Copy};
enum class Group:uint8_t {Voice,Listen,File};
enum class Copy:uint8_t {Save,Detach,Crop};
enum class Handle:uint8_t {In,Out,FadeIn,FadeOut,LoopIn,LoopOut,Crossfade};
enum class Gesture:uint8_t {None,Edge,Cut,Pan,Move,Scrub,Speed,CutSpeed,Position,Gain,Gate,Menu,Note,Tempo};
enum class Action:uint8_t {None,Source,Mode,Config,Tool,Sheet,Group,Back,Listen,History,Sequence,Add,Auto,
    Row,Footer,Confirm,AutoCount,Crop,Undo};
struct Document {
    PlayEditState edit{};pe::Cuts cuts{};q15 bases[C::P_COUNT]={};uint32_t routing=0;
    bool modern=false,quantize=false,bypass=false;
    C::PitchMode pitch_mode=C::PitchMode::Legacy;
};
bool same_edit(const PlayEditState&a,const PlayEditState&b){
    return a.enabled==b.enabled&&a.source_frames==b.source_frames&&a.start==b.start&&a.end==b.end&&
        a.fade_in==b.fade_in&&a.fade_out==b.fade_out&&a.loop_start==b.loop_start&&a.loop_end==b.loop_end&&
        a.crossfade==b.crossfade&&a.anchor_start==b.anchor_start&&a.anchor_length==b.anchor_length;
}
bool same(const Document&a,const Document&b){return same_edit(a.edit,b.edit)&&a.cuts==b.cuts&&
    !std::memcmp(a.bases,b.bases,sizeof a.bases)&&a.routing==b.routing&&a.modern==b.modern&&a.quantize==b.quantize&&a.bypass==b.bypass&&a.pitch_mode==b.pitch_mode;}
struct Context{uintptr_t unit=0,pcm=0;int chain=-1,slot=-1,sample=-1;uint32_t generation=0,frames=0;uint8_t channels=0;} ctx;
// One retained source switch; ordinary per-source gestures still have 16 levels.
// No PCM copy or deletion: both WAV/slot sources remain available after undo.
struct SourceUndo {Engine* engine=nullptr;Context source[2]{};Document doc[2]{};int side=1;} source_undo;
struct Options {
    Page page=Page::Trim,selection_page=Page::Trim;Handle handle=Handle::In;int selected=C::P_START,cut=-1;
    uint32_t first=0,span=0,token=0;int snap=0,sensitivity=60,auto_count=0;
    bool repeat=false,chain=false,live=false,add=false,scrub=false,fine=false,from_seq=false,snapped=false;
    uint32_t snap_frame=0;
    bool normalize=false,dc=false,bake=false,reverse=false,move=false;
    Group group=Group::Voice;Copy copy=Copy::Save;Page return_page=Page::Trim;
} view;
const char* notice=nullptr;
uint64_t direct_undo_until=0;
Document history[HISTORY],expected;int history_count=0,history_at=0;
struct Stroke {Gesture kind=Gesture::None;int x=0,y=0;uint32_t first=0,span=0;Document before{};bool changed=false,moved=false;
    Action action=Action::None;int argument=0;layout::Box box{};Page page=Page::Trim;Group group=Group::Voice;
} stroke;
pe::Envelope<N> detail;pe::Envelope<OV> overview;pe::Attacks attacks;
pe::Cuts proposed;
uint32_t chain_preview_generation=0;uintptr_t chain_preview_owner=0;int preview_cut=-1;
struct Frame {
    bool active=false,loaded=false,ready=false,overview_ready=false,busy=false,playing=false,audition=false,undo=false,redo=false,missing=false,mismatch=false,inline_undo=false;
    Options v{};Document d{};pe::Cuts proposed{};
    int chain=0,slot=0,sample=0;uint8_t channels=0;
    uint32_t frames=0,start=0,end=0,position=0;uint16_t gain=256;
    int8_t low[N]={},high[N]={},overview_low[OV]={},overview_high[OV]={};
    char name[33]={},notice[58]={};ParamInspector param{};
} frame;
static_assert(sizeof(Frame)<2304&&std::is_trivially_copyable<Frame>::value,"bounded PLAY editor frame");
int clamp(int64_t v,int lo=0,int hi=32767){return int(std::max<int64_t>(lo,std::min<int64_t>(hi,v)));}
bool context(){return g_eng&&g_audio&&g_view==View::Chains&&g_overlay==Overlay::Editor&&!g_seq_lock_armed&&
    g_edit_chain>=0&&g_edit_chain<NUM_CHAINS&&g_edit_slot>=0&&g_edit_slot<UNITS_PER_CHAIN;}
C* current(){if(!context())return nullptr;Unit*u=g_eng->chains[g_edit_chain].units[g_edit_slot];
    if(view.from_seq){int last=-1;for(int s=0;s<UNITS_PER_CHAIN;++s){const Unit* at=g_eng->chains[g_edit_chain].units[s];if(at&&!at->bypass&&at->sequencer_properties().source)last=s;}
        if(last!=g_edit_slot&&!(u&&u->bypass&&last<g_edit_slot))return nullptr;}
    return u&&u->kind==UnitKind::SamplePlayer?static_cast<C*>(u):nullptr;}
Document document(const C&u){Document d;d.edit=u.edit_state();d.modern=u.modern();d.quantize=u.pitch_quantize;d.bypass=u.bypass;d.pitch_mode=u.pitch_mode();
    for(int p=0;p<C::P_COUNT;++p){d.bases[p]=u.params[p].base;d.routing=(d.routing*16777619u)^uint32_t(uint16_t(u.params[p].mod_depth))^(uint32_t(u.params[p].src)<<16);}
    const auto&s=Bank::instance().slot(u.sample_slot);std::copy(s.chops,s.chops+16,d.cuts.at);d.cuts.reverse=s.slice_reverse_mask;
    std::copy(s.slice_speed_q8,s.slice_speed_q8+16,d.cuts.speed_q8);return d;}
bool valid_context(const C&u){const auto&b=Bank::instance();if(current()!=&u||u.sample_slot!=ctx.sample)return false;const auto&s=b.slot(u.sample_slot);
    return uintptr_t(&u)==ctx.unit&&g_edit_chain==ctx.chain&&g_edit_slot==ctx.slot&&b.generation(ctx.sample)==ctx.generation&&
        uintptr_t(s.data.data())==ctx.pcm&&s.playable_frames()==ctx.frames&&s.channels==ctx.channels;}
void restart_note_owner();
void stop_locked(){preview_cut=-1;if(view.token)g_eng->stop_play_preview(view.token);view.token=0;
    if(chain_preview_owner&&ctx.chain>=0&&ctx.slot>=0){Unit*u=g_eng->chains[ctx.chain].units[ctx.slot];
        if(uintptr_t(u)==chain_preview_owner&&u->kind==UnitKind::SamplePlayer&&!g_eng->transport_playing&&
            Bank::instance().generation(ctx.sample)==chain_preview_generation&&static_cast<C*>(u)->preview_active())static_cast<C*>(u)->release();}
    chain_preview_owner=0;}
void reset_history(const Document&d){history[0]=expected=d;history_count=1;history_at=0;stroke={};}
void finish(){if(stroke.changed){
        if(history_at==HISTORY-1){for(int i=1;i<HISTORY;++i)history[i-1]=history[i];--history_at;}
        history[++history_at]=expected;history_count=history_at+1;}
    stroke={};}
void reconcile_workspace(const C&u){
    // Legacy six-control PLAY cannot own a direct-position workspace or lens.
    // This also covers undo and externally replaced/restored authored modes.
    if(!u.modern())view.scrub=false;
    if(view.selected>=u.num_params)view.selected=C::P_SPEED;
}
bool identify(C&u){if(current()!=&u)return false;
    if(u.sample_slot<0||u.sample_slot>=trackr::synth::SAMPLE_BANK_SIZE)return false;
    if(!valid_context(u)){source_undo.engine=nullptr;stop_locked();const bool return_to_seq=view.from_seq;view={};view.from_seq=return_to_seq;notice=nullptr;
        const auto&b=Bank::instance();const auto&s=b.slot(u.sample_slot);
        ctx={uintptr_t(&u),uintptr_t(s.data.data()),g_edit_chain,g_edit_slot,u.sample_slot,b.generation(u.sample_slot),s.playable_frames(),s.channels};
        view.selected=u.pitch_mode()==C::PitchMode::Legacy?C::P_SPEED:C::P_NOTE;
        view.span=ctx.frames;overview.reset(0,ctx.frames);detail.reset(0,ctx.frames);attacks.reset();reset_history(document(u));
    }else if(!same(document(u),expected)){
        source_undo.engine=nullptr;stop_locked();reset_history(document(u));view.add=view.live=false;notice="external edit / history cleared";
    }
    reconcile_workspace(u);return true;
}
void changed(C&u){direct_undo_until=0;expected=document(u);stroke.changed=true;mark_project_mutated();}
void command_begin(){finish();stroke.before=expected;}
void command_end(C&u){if(!same(document(u),expected))changed(u);finish();}
bool editable(C&u){if(g_eng->play_editor_busy()){notice="recording / editor protected";return false;}
    if(!valid_context(u)||ctx.frames<2||(ctx.channels!=1&&ctx.channels!=2))return false;
    if(!u.edit_source_matches()){notice="source changed / choose source again";return false;}
    return true;}
PlayEditState prepare(const C&u){auto e=u.edit_state();
    const auto w=u.projected_window(u.params[C::P_START].base,u.params[C::P_LEN].base,0);
    if(!e.enabled||!u.edit_source_matches()){
        e={};e.enabled=true;e.source_frames=ctx.frames;e.start=w.start;e.end=w.end;e.loop_start=e.start;e.loop_end=e.end;
    }else if(w.valid){e.start=w.start;e.end=w.end;e.loop_start=w.loop_start;e.loop_end=w.loop_end;
        e.fade_in=w.fade_in;e.fade_out=w.fade_out;e.crossfade=w.crossfade;}
    e.anchor_start=u.params[C::P_START].base;e.anchor_length=u.params[C::P_LEN].base;
    return e;
}
bool set_edit(C&u,PlayEditState e){
    if(!editable(u))return false;
    if(!u.modern()){notice="choose gate or one shot to enable exact edits";return false;}
    e.start=uint32_t(clamp(e.start,0,int(ctx.frames)-2));e.end=uint32_t(clamp(e.end,int(e.start)+2,int(ctx.frames)));
    e.fade_in=std::min(e.fade_in,e.end-e.start);e.fade_out=std::min(e.fade_out,e.end-e.start);
    e.loop_start=uint32_t(clamp(e.loop_start,int(e.start),int(e.end)-2));e.loop_end=uint32_t(clamp(e.loop_end,int(e.loop_start)+2,int(e.end)));
    e.crossfade=std::min(e.crossfade,(e.loop_end-e.loop_start-1)/2);
    if(same_edit(e,u.edit_state()))return false;
    if(!u.set_edit_state(e,ctx.generation)){notice="source changed / edit refused";return false;}
    changed(u);return true;
}
void apply_cuts(C&u,const pe::Cuts&c){auto&s=Bank::instance().slot(u.sample_slot);
    if(c==document(u).cuts)return;
    std::copy(c.at,c.at+16,s.chops);s.slice_reverse_mask=c.reverse;
    std::copy(c.speed_q8,c.speed_q8+16,s.slice_speed_q8);changed(u);}
void selection(const C&u,uint32_t&a,uint32_t&b){const auto d=document(u);
    if(view.selection_page==Page::Cuts&&d.cuts.valid(view.cut,ctx.frames)){
        a=d.cuts.at[view.cut];b=d.cuts.end(view.cut,ctx.frames);return;}
    const auto w=u.projected_window(u.params[C::P_START].base,u.params[C::P_LEN].base,0);a=w.start;b=w.valid?w.end:w.start;
    if(view.selection_page==Page::Loop&&d.edit.enabled){const auto e=prepare(u);a=e.loop_start;b=e.loop_end;}
}
bool preview_locked(C&u,uint32_t a,uint32_t b,bool repeat){
    if(!editable(u)||u.bypass||g_eng->chains[ctx.chain].mute){notice="audition unavailable";return false;}
    stop_locked();bool reverse=false;uint16_t rate=256;auto d=document(u);
    if(view.selection_page==Page::Cuts&&d.cuts.valid(view.cut,ctx.frames)){
        reverse=d.cuts.reverse&(1u<<view.cut);rate=d.cuts.speed_q8[view.cut];preview_cut=view.cut;}
    if(view.scrub&&view.chain){notice="scrub uses source audition / choose source";return false;}
    if(!view.chain){view.token=g_eng->start_play_preview(ctx.chain,ctx.slot,a,b,repeat,reverse,rate,view.scrub);
        notice=view.token?"source audition / independent mono bus":"audition unavailable";return view.token!=0;}
    if(g_eng->transport_playing){notice="chain audition needs stopped transport";return false;}
    for(int s=ctx.slot+1;s<UNITS_PER_CHAIN;++s){Unit*at=g_eng->chains[ctx.chain].units[s];
        if(at&&!at->bypass&&at->sequencer_properties().source){notice="another source follows play";return false;}}
    g_eng->trigger_chain(ctx.chain);
    const bool ok=u.preview_frames(a,b,repeat,ctx.generation,reverse,rate);
    if(ok){chain_preview_owner=uintptr_t(&u);chain_preview_generation=ctx.generation;}
    notice=ok?"chain audition / transport takes voice back":"audition unavailable";return ok;
}
void listen(C&u){uint32_t a,b;selection(u,a,b);preview_locked(u,a,b,view.repeat);}
uint32_t at_x(int x);
void scrub_at(C&u,int x,bool start){
    uint32_t a,b;selection(u,a,b);if(b<=a+1)return;
    if(start&&!preview_locked(u,a,b,true))return;
    const uint32_t at=uint32_t(clamp(at_x(x),a,b-1));
    const q15 position=q15(uint64_t(at-a)*32767/(b-a-1));
    if(view.token)g_eng->scrub_play_preview(view.token,position,start);
}
bool audition_active(const C&u){return g_eng->play_preview_active(view.token)||(chain_preview_owner==uintptr_t(&u)&&u.preview_active()&&u.playback_active()&&!g_eng->transport_playing);}
uint32_t audition_position(const C&u){return g_eng->play_preview_active(view.token)?g_eng->play_preview_position(view.token):u.visual_source_frame();}
void zoom(C&u,int direction,bool fit=false){uint32_t a,b;selection(u,a,b);const uint32_t centre=a+(b-a)/2;
    uint32_t span=fit?std::max<uint32_t>(2,(b-a)+(b-a)/4):direction>0?std::max<uint32_t>(2,view.span/2):std::min(ctx.frames,view.span*2);
    view.span=std::min(ctx.frames,span);view.first=uint32_t(clamp(int64_t(centre)-view.span/2,0,ctx.frames-view.span));}
uint32_t at_x(int x){return view.first+uint32_t(uint64_t(clamp(x-X,0,W))*view.span/W);}
uint32_t snapped(uint32_t requested,uint32_t result){view.snapped=result!=requested;view.snap_frame=result;return result;}
uint32_t snap(uint32_t at){view.snapped=false;const uint32_t radius=std::max<uint32_t>(2,std::min<uint32_t>(512,view.span/40));
    if(view.snap==2)return snapped(at,attacks.nearest(at,radius));
    if(view.snap==3){const uint32_t beat=g_eng->frames_per_beat();if(!beat)return at;
        const uint32_t grid=std::max<uint32_t>(1,beat/4);const uint32_t nearest=uint32_t((uint64_t(at)+grid/2)/grid*grid);
        if(nearest<=ctx.frames&&(at>nearest?at-nearest:nearest-at)<=radius)return snapped(at,nearest);}
    if(view.snap==1){ // bounded local scan, first channel sign plus stereo magnitude tie-break
        const auto&s=Bank::instance().slot(ctx.sample);uint32_t best=at,dist=radius+1;int best_amp=65537;
        for(uint32_t f=std::max<uint32_t>(1,at>radius?at-radius:1);f<std::min(ctx.frames,at+radius+1);++f){
            int a=s.data[size_t(f-1)*ctx.channels],b=s.data[size_t(f)*ctx.channels];
            if((a<=0&&b>=0)||(a>=0&&b<=0)){uint32_t d=f>at?f-at:at-f;int amp=std::abs(b);
                if(ctx.channels==2)amp+=std::abs(int(s.data[size_t(f)*2+1]));
                if(d<dist||(d==dist&&amp<best_amp)){best=f;dist=d;best_amp=amp;}}}return snapped(at,best);}
    return at;
}
void begin(Gesture kind,int x,int y){finish();stroke.kind=kind;stroke.x=x;stroke.y=y;stroke.first=view.first;stroke.span=view.span;stroke.before=expected;
    if(kind==Gesture::Edge||kind==Gesture::Move){C*u=current();if(u)stroke.before.edit=prepare(*u);}
    g_drag=Drag::PlaySliceSeam;}
void nudge(C&u,int delta){if(!editable(u))return;view.snapped=false;command_begin();
    if(view.selection_page==Page::Cuts&&view.cut>=0){auto c=document(u).cuts;if(c.move(view.cut,int64_t(c.at[view.cut])+delta,ctx.frames))apply_cuts(u,c);}
    else{auto e=prepare(u);auto add=[&](uint32_t v){return uint32_t(clamp(int64_t(v)+delta,0,ctx.frames));};
        switch(view.handle){case Handle::In:e.start=std::min(add(e.start),e.end-2);break;case Handle::Out:e.end=std::max(add(e.end),e.start+2);break;
        case Handle::FadeIn:e.fade_in=add(e.fade_in);break;case Handle::FadeOut:e.fade_out=add(e.fade_out);break;
        case Handle::LoopIn:e.loop_start=std::min(add(e.loop_start),e.loop_end-2);break;case Handle::LoopOut:e.loop_end=std::max(add(e.loop_end),e.loop_start+2);break;
        case Handle::Crossfade:e.crossfade=add(e.crossfade);break;}set_edit(u,e);}
    finish();
}
bool source_undo_ready(const C&u,int delta){
    if(source_undo.engine!=g_eng || (delta<0?source_undo.side!=1:source_undo.side!=0) ||
        uintptr_t(&u)!=source_undo.source[0].unit || !same(document(u),source_undo.doc[source_undo.side]))return false;
    const auto&bank=Bank::instance();
    for(int i=0;i<2;++i){const auto&c=source_undo.source[i];const auto&s=bank.slot(c.sample);const auto&d=source_undo.doc[i];
        if(bank.generation(c.sample)!=c.generation || uintptr_t(s.data.data())!=c.pcm || s.playable_frames()!=c.frames || s.channels!=c.channels ||
           std::memcmp(s.chops,d.cuts.at,sizeof s.chops) || s.slice_reverse_mask!=d.cuts.reverse ||
           std::memcmp(s.slice_speed_q8,d.cuts.speed_q8,sizeof s.slice_speed_q8))return false;}
    return true;
}
void switch_source(C&u,int delta){
    if(!source_undo_ready(u,delta))return;
    auto saved=source_undo;const int side=1-saved.side;const auto&d=saved.doc[side];
    stop_locked();g_eng->stop_play_preview();u.reset();u.sample_slot=saved.source[side].sample;
    u.set_modern(d.modern);u.set_pitch_mode(d.pitch_mode);u.pitch_quantize=d.quantize;u.bypass=d.bypass;
    restart_note_owner(); // source switch always reset this voice, including same-mode undo
    for(int p=0;p<C::P_COUNT;++p){if(u.params[p].base!=d.bases[p])u.params[p].slew_ready=false;u.params[p].base=d.bases[p];}
    u.set_edit_state(d.edit,saved.source[side].generation);
    identify(u);source_undo=saved;source_undo.side=side;mark_project_mutated();
    notice=side?"redo cut / original kept":"undo cut / original restored";
}
void undo(C&u,int delta){finish();if(!editable(u))return;int next=history_at+delta;
    if(next<0||next>=history_count){switch_source(u,delta);return;}
    const auto&d=history[next];if(!same(document(u),expected)){reset_history(document(u));return;}
    const bool was_modern=u.modern(),was_playing=u.playback_active();const auto was_mode=u.pitch_mode();
    u.set_modern(d.modern);
    if(d.edit.enabled&&!u.set_edit_state(d.edit,ctx.generation)){u.set_modern(was_modern);u.set_pitch_mode(was_mode);return;}
    if(!d.edit.enabled&&u.edit_state().enabled)u.restore_edit_state({});
    u.set_pitch_mode(d.pitch_mode);
    u.pitch_quantize=d.quantize;u.bypass=d.bypass;
    if(was_mode!=d.pitch_mode||(was_playing&&!u.playback_active()))restart_note_owner();
    for(int p=0;p<C::P_COUNT;++p)u.params[p].base=d.bases[p];
    auto&s=Bank::instance().slot(ctx.sample);
    std::copy(d.cuts.at,d.cuts.at+16,s.chops);s.slice_reverse_mask=d.cuts.reverse;
    std::copy(d.cuts.speed_q8,d.cuts.speed_q8+16,s.slice_speed_q8);
    history_at=next;expected=document(u);reconcile_workspace(u);mark_project_mutated();stop_locked();
}
void live_mark(C&u){if(!view.live||!audition_active(u)||!editable(u))return;
    auto c=document(u).cuts;const int id=c.insert(audition_position(u),ctx.frames);
    if(id<0){notice="mark refused / full or too close";return;}
    command_begin();apply_cuts(u,c);view.cut=id;finish();notice="live mark / exact source frame";
}
void analyze(){
    if(!ctx.unit||ctx.frames<2||(ctx.channels!=1&&ctx.channels!=2))return;
    if(detail.first!=view.first||detail.last!=view.first+view.span)detail.reset(view.first,view.first+view.span);
    // Eight bounded 4/8KiB copies per display frame, then exhaustive reduction
    // outside the audio guard. No PCM is retained in the draw frame.
    int16_t pcm[2048*2];
    for(int pass=0;pass<8;++pass){bool full=!overview.ready;uint32_t start=full?overview.next:detail.next;
        uint32_t end=full?overview.last:detail.last;if(start>=end)break;uint32_t count=std::min<uint32_t>(2048,end-start);uint8_t channels;
        {platform::AudioOut::Guard guard(*g_audio);C*u=current();if(!u||!valid_context(*u)||g_eng->play_editor_busy())return;
            const auto&s=Bank::instance().slot(ctx.sample);channels=s.channels;std::memcpy(pcm,s.data.data()+size_t(start)*channels,size_t(count)*channels*sizeof(int16_t));}
        if(full){overview.feed(pcm,start,count,channels);attacks.feed(pcm,start,count,channels);
            if(detail.next==start&&detail.last==overview.last)detail.feed(pcm,start,count,channels);}
        else detail.feed(pcm,start,count,channels);
    }
}
const char* handle_name(Handle h){static const char*names[]={"in","out","fade in","fade out","loop in","loop out","crossfade"};return names[int(h)];}
const char* snap_name(){static const char*names[]={"off","zero","attack","beat"};return names[frame.v.snap];}
void seconds(char*out,size_t size,uint32_t frames){std::snprintf(out,size,"%u.%03us",unsigned(frames/32000),unsigned(uint64_t(frames%32000)*1000/32000));}
void right(int x,int y,const char*s,u32 color){ui::text(x-ui::text_w(s),y,s,color);}
int fx(uint32_t at,uint32_t a,uint32_t b,int x,int w){if(b<=a)return x;return x+int(uint64_t(clamp(int64_t(at)-a,0,int(b-a)))*w/(b-a));}
// Visible endpoint handles use the same geometry for draw and input. A tiny
// selection gets separate vertical lanes, never two competing edge owners.
struct TrimHandles {layout::Box in{},out{};bool has_in=false,has_out=false,overlap=false;int a=0,b=0;};
TrimHandles trim_handles(uint32_t start,uint32_t end,uint32_t first,uint32_t span){
    TrimHandles h;h.a=fx(start,first,first+span,X,W);h.b=fx(end,first,first+span,X,W);
    h.has_in=span&&start>=first&&start<=first+span;h.has_out=span&&end>=first&&end<=first+span;
    const int a=clamp(h.a-16,10,193),b=clamp(h.b-16,10,193);h.overlap=h.has_in&&h.has_out&&a+32>b;
    h.in={a,110,a+32,h.overlap?142:174};h.out={b,h.overlap?142:110,b+32,174};return h;
}
void draw_wave(bool full){
    const int x=full?20:X,w=full?359:W,top=full?92:120,bottom=full?154:174,zero=(top+bottom)/2,height=(bottom-top)/2-3;
    const uint32_t a=full?0:frame.v.first,b=full?frame.frames:frame.v.first+frame.v.span;
    const bool ready=full?frame.overview_ready:frame.ready;auto at=[&](uint32_t p){return fx(p,a,b,x,w);};
    const char* warning=frame.busy?"recording / protected":frame.missing?"missing source":frame.mismatch?"source changed / reload":!frame.loaded?"choose a sample":!ready?"analyzing source...":nullptr;
    if(warning){ui::text(x+4,zero-4,warning,m::ghost());return;}
    ui::rect(x,zero,w+1,1,palette::shadow());
    const int8_t*lo=full?frame.overview_low:frame.low;const int8_t*hi=full?frame.overview_high:frame.high;
    const bool cuts=frame.v.selection_page==Page::Cuts&&!frame.v.scrub;
    for(int col=0;col<=w;++col){uint32_t source=a+uint32_t(uint64_t(col)*(b-a)/(w+1));
        const bool selected=!frame.v.scrub&&source>=frame.start&&source<frame.end;
        int yy=zero-int(hi[col])*height/128,yy2=zero-int(lo[col])*height/128;
        ui::rect(x+col,yy,1,std::max(1,yy2-yy+1),selected?m::ink():m::ghost());}
    const auto&table=frame.v.page==Page::Auto?frame.proposed:frame.d.cuts;
    if(cuts||frame.v.page==Page::Auto)for(int id=0;id<16;++id){if(!table.valid(id,frame.frames)||table.at[id]<a||table.at[id]>b)continue;
        const int xx=at(table.at[id]);ui::rect(xx,top,1,bottom-top,id==frame.v.cut?m::spot():palette::shadow());}
    if(!frame.v.scrub){for(uint32_t p:{frame.start,frame.end})if(p>=a&&p<=b){const int xx=at(p);ui::rect(xx,top,1,bottom-top,m::spot());
            ui::rect(std::max(x,xx-3),top,std::min(x+w,xx+3)-std::max(x,xx-3)+1,2,m::spot());}
        if(cuts)ui::rect(at(frame.start),bottom,std::max(1,at(frame.end)-at(frame.start)),2,m::spot());
        if(frame.v.selection_page==Page::Trim&&frame.d.edit.enabled){const auto&e=frame.d.edit;
            if(e.fade_in)ui::line(at(e.start),bottom-1,at(std::min(e.end,e.start+e.fade_in)),top,m::spot());
            if(e.fade_out)ui::line(at(e.end-e.fade_out),top,at(e.end),bottom-1,m::spot());}
        if(frame.v.selection_page==Page::Loop&&frame.d.edit.enabled){const auto&e=frame.d.edit;
            if(e.crossfade)ui::line(at(e.loop_end-e.crossfade),bottom-1,at(e.loop_end),top,m::spot());}}
    if(!full&&!frame.v.scrub&&frame.v.selection_page==Page::Trim){const auto h=trim_handles(frame.start,frame.end,frame.v.first,frame.v.span);
        if(h.has_in)ui::text(clamp(h.a-6,14,209),111,"in",m::spot());
        if(h.has_out)ui::text(clamp(h.b-9,14,203),h.overlap?163:111,"out",m::spot());}
    const uint32_t cursor=frame.playing?frame.position:frame.start+uint32_t(uint64_t(frame.end-frame.start)*clamp(frame.d.bases[C::P_POSITION])/32767);
    if((frame.playing||frame.v.scrub)&&cursor>=a&&cursor<=b){const int xx=at(cursor);
        if(frame.v.scrub){ui::rect(xx,top+3,1,bottom-top-6,m::spot());
            if(frame.d.bases[C::P_HOLD]>16383){ui::rect(std::max(x,xx-3),top+3,1,bottom-top-6,m::spot());ui::rect(std::min(x+w,xx+3),top+3,1,bottom-top-6,m::spot());}}
        else for(int yy=top;yy<bottom;yy+=4)ui::rect(xx,yy,1,2,m::spot());}
}
void seam(C&u){auto e=prepare(u);if(e.loop_end<=e.loop_start+1)return;
    if(!preview_locked(u,e.loop_start,e.loop_end,true))return;
    const uint32_t margin=std::min<uint32_t>(3200,(e.loop_end-e.loop_start)/2);
    const uint32_t start=u.params[C::P_SPEED].effective<0?e.loop_start+margin:e.loop_end-1-margin;
    if(view.token)g_eng->seek_play_preview(view.token,start,true);else u.preview_seek(start,true);
    notice="loop seam / repeat without musical loop write";
}
void close_menu(){finish();view.page=view.selection_page;notice=nullptr;}
void sheet(Page page){finish();if(page==Page::History||page==Page::Copy)view.return_page=view.page;
    if(page==Page::Fades&&view.handle!=Handle::FadeIn&&view.handle!=Handle::FadeOut)view.handle=Handle::FadeIn;
    if(page==Page::Edit&&view.selection_page==Page::Trim&&view.handle!=Handle::In&&view.handle!=Handle::Out)view.handle=Handle::In;
    if(page==Page::Edit&&view.selection_page==Page::Loop&&view.handle<Handle::LoopIn)view.handle=Handle::LoopIn;
    view.page=page;notice=nullptr;}
void back_sheet(){finish();if(view.page==Page::Copy||view.page==Page::History)view.page=view.return_page;else close_menu();}
void change_tool(C&u,Page page){finish();stop_locked();view.page=view.selection_page=page;view.add=view.live=view.move=false;notice=nullptr;
    if(page==Page::Cuts){const auto cuts=document(u).cuts;if(!cuts.valid(view.cut,ctx.frames))view.cut=cuts.select(0,ctx.frames);view.selected=C::P_SLICE;}
    else {view.handle=page==Page::Loop?Handle::LoopIn:Handle::In;view.selected=C::P_START;}}
void restart_note_owner(){
    // Retain the legato ARP ownership reset when an explicit engine operation ends a voice.
    if(!g_eng->transport_playing||ctx.chain<0||ctx.chain>=NUM_CHAINS||ctx.slot<0||ctx.slot>=UNITS_PER_CHAIN)return;
    const Unit* target=g_eng->chains[ctx.chain].units[ctx.slot];
    if(!target||target->bypass||g_eng->chains[ctx.chain].mute)return;
    for(int s=ctx.slot-1;s>=0;--s){Unit* u=g_eng->chains[ctx.chain].units[s];if(!u||u->bypass)continue;
        if(ArpUnit::accepts(u))break;
        if(u->kind==UnitKind::Arp){static_cast<ArpUnit*>(u)->reset();break;}}
}
void mode(C&u,int which){
    if(!editable(u))return;
    if(which>=3&&!u.modern()){notice="enable gate / one shot in config first";return;}
    stop_locked();command_begin();
    if(which<3){const auto next=which==0?C::PitchMode::Normal:which==1?C::PitchMode::Stretch:C::PitchMode::Legacy;
        if(next!=u.pitch_mode()){if(!u.modern()&&next!=C::PitchMode::Legacy)u.set_modern(true);u.set_pitch_mode(next);restart_note_owner();}
        view.scrub=false;view.selected=next==C::PitchMode::Legacy?C::P_SPEED:C::P_NOTE;
    }else {view.scrub=true;view.chain=false;view.selected=C::P_POSITION;u.params[C::P_HOLD].base=which==4?32767:0;}
    command_end(u);view.page=view.selection_page;view.add=view.live=false;
    notice=which>=3?"touch workspace / hold is stored; seq unchanged":which==1?"stretch / manual tempo, independent note":which==0?"normal / source prepared at c4":"tape / original varispeed";
}
void queue(Action action,int argument,layout::Box box,int x,int y){begin(Gesture::Menu,x,y);stroke.action=action;stroke.argument=argument;stroke.box=box;stroke.page=view.page;stroke.group=view.group;}
void choose_cut(C&u,int delta){const auto cuts=document(u).cuts;int ids[16],n=0;
    for(int i=0;i<16;++i)if(cuts.valid(i,ctx.frames))ids[n++]=i;
    std::sort(ids,ids+n,[&](int a,int b){return cuts.at[a]<cuts.at[b];});int at=0;
    for(int i=0;i<n;++i)if(ids[i]==view.cut)at=i;
    if(n)view.cut=ids[clamp(at+delta,0,n-1)];
    view.selected=C::P_SLICE;
}
void propose(){proposed=view.auto_count<0?pe::Cuts{}:view.auto_count?pe::Cuts::equal(view.auto_count,ctx.frames):attacks.cuts(ctx.frames,view.sensitivity);}
void auto_sheet(){stop_locked();view.add=view.live=false;view.auto_count=0;propose();sheet(Page::Auto);}
void copy_sheet(Copy copy){view.copy=copy;sheet(Page::Copy);}
void toggle(C&u,int p){command_begin();u.params[p].base=u.params[p].base>16383?0:32767;view.selected=p;command_end(u);}
void row_action(C&u,int row,bool right_half){
    const int delta=right_half?1:-1;
    if(view.page==Page::Config){
        if(view.group==Group::Listen){if(row==0){stop_locked();if(view.scrub)notice="scratch / hold uses source audition";else view.chain=!view.chain;}
            else if(row==1){view.repeat=!view.repeat;if(audition_active(u))listen(u);}
            else if(row==2||row==3){uint32_t a,b;selection(u,a,b);const uint32_t length=std::min<uint32_t>(3200,b-a);
                if(row==2)preview_locked(u,a,a+length,view.repeat);else preview_locked(u,b-length,b,view.repeat);}
            else if(row==4)seam(u);else stop_locked();
            return;}
        if(view.group==Group::File){if(row==0)view.normalize=!view.normalize;else if(row==1)view.dc=!view.dc;
            else if(row==2)view.bake=!view.bake;else if(row==3)view.reverse=!view.reverse;
            else if(editable(u))copy_sheet(row==4?Copy::Save:Copy::Detach);
            return;}
        if(!editable(u))return;
        if(row==0){command_begin();if(!u.modern()){u.set_modern(true);u.params[C::P_LOOP].base=0;u.params[C::P_MODE].base=0;}
            else u.params[C::P_MODE].base=play_mode::is_gate(u.params[C::P_MODE].base)?0:32767;
            view.selected=C::P_MODE;command_end(u);}
        else if(row==1)toggle(u,C::P_LOOP); // legacy PLAY has an audible loop parameter too
        else if(row==3){command_begin();stop_locked();const int raw=u.params[C::P_SPEED].base;u.params[C::P_SPEED].base=q15(raw?-raw:-8192);view.selected=C::P_SPEED;command_end(u);}
        else if(row==5){command_begin();stop_locked();u.bypass=!u.bypass;command_end(u);}
        return;
    }
    if(view.page==Page::History){if(row<2)undo(u,row?1:-1);return;}
    if(view.page==Page::Zoom){if(row==0||row==1)zoom(u,row?1:-1);else if(row==2)zoom(u,0,true);
        else if(row==3){view.first=0;view.span=ctx.frames;}else if(row==5)view.fine=!view.fine;
        return;}
    if(view.page==Page::Auto){if(row==1){view.sensitivity=clamp(view.sensitivity+delta*10,0,100);view.auto_count=0;propose();}
        else if(row==3){view.auto_count=-1;propose();}
        return;}
    if(view.page==Page::Seq){if(row==3){view.selected=C::P_POSITION;notice="position selected / hold l for modulation";}
        else if(row==4)sheet(Page::History);
        else if(row<2&&editable(u)){if(!u.modern()){notice="enable gate / one shot first";return;}
            stop_locked();toggle(u,row?C::P_HOLD:C::P_SCRUB);notice=row?"hold is stored / touch and sequence":"sequence scrub explicitly changed";}return;}
    if(view.page==Page::Tools){
        if(view.selection_page==Page::Cuts){if(row==0){if(!editable(u))return;view.live=!view.live;view.add=false;close_menu();
                if(view.live){preview_locked(u,0,ctx.frames,false);notice="live cutting / press a to mark";}else stop_locked();}
            else if(row==1){auto_sheet();view.auto_count=-1;propose();}
            else if(row==2)sheet(Page::Zoom);else if(row==3){view.snap=(view.snap+1)%4;view.snapped=false;}
            else if(row==4)view.fine=!view.fine;else sheet(Page::History);
            return;}
        if(row==1){zoom(u,0,true);close_menu();}else if(row==3)sheet(Page::Fades);else if(row==4)sheet(Page::Zoom);else if(row==5)sheet(Page::History);
        else if(editable(u)){if(row==2)copy_sheet(Copy::Crop);else{command_begin();auto e=prepare(u);e.start=0;e.end=ctx.frames;set_edit(u,e);finish();}}
        return;
    }
    if(view.page==Page::Fades){if(row==2)view.handle=view.handle==Handle::FadeIn?Handle::FadeOut:Handle::FadeIn;
        else if(row==3)nudge(u,delta);else if(row==4)view.fine=!view.fine;
        else if(row==5&&editable(u)){command_begin();auto e=prepare(u);e.fade_in=e.fade_out=0;set_edit(u,e);finish();}
        return;}
    if(view.page!=Page::Edit)return;
    if(view.selection_page==Page::Cuts){if(row==0){choose_cut(u,delta);return;}if(row==2){nudge(u,delta);return;}
        if((row==3||row==5)&&editable(u)){auto cuts=document(u).cuts;if(!cuts.valid(view.cut,ctx.frames))return;
            command_begin();if(row==3){cuts.reverse^=uint16_t(1u<<view.cut);apply_cuts(u,cuts);}
            else {int id=cuts.merge(view.cut,ctx.frames);if(id>=0){apply_cuts(u,cuts);view.cut=id;}}finish();}return;}
    if(view.selection_page==Page::Loop){if(row==3)nudge(u,delta);else if(row==4){view.snap=(view.snap+1)%4;view.snapped=false;}else if(row==5)view.fine=!view.fine;return;}
    if(row==0)view.handle=view.handle==Handle::In?Handle::Out:Handle::In;
    else if(row==2)nudge(u,delta);else if(row==3){view.snap=(view.snap+1)%4;view.snapped=false;}
    else if(row==4)view.fine=!view.fine;else if(row==5){view.move=!view.move;close_menu();notice="move selection / drag wave; edit to turn off";}
}
void footer_action(C&u){
    if(view.page==Page::Config){if(view.group==Group::Voice&&u.pitch_mode()==C::PitchMode::Legacy&&editable(u)){command_begin();u.pitch_quantize=!u.pitch_quantize;command_end(u);}else back_sheet();}
    else if(view.page==Page::Edit){if(view.selection_page==Page::Loop)seam(u);else sheet(Page::Tools);}
    else back_sheet();
}
void export_copy(bool direct=false){
    C*owner=nullptr;Context before;Document d;PlayExportOptions options;Copy copy;
    {platform::AudioOut::Guard guard(*g_audio);owner=current();
        if(!owner||!valid_context(*owner)||!same(document(*owner),expected)||!editable(*owner)||
           (direct?view.page!=Page::Trim:view.page!=Page::Copy)||param_lens_is_open())return;
        copy=direct?Copy::Crop:view.copy;before=ctx;d=document(*owner);selection(*owner,options.first,options.end);
        options.crop=copy==Copy::Crop;options.detach=copy==Copy::Detach;
        options.normalize=view.normalize;options.dc=view.dc;options.bake=view.bake;options.reverse=view.reverse;
        if(options.crop&&(view.selection_page!=Page::Trim||view.scrub))return;
        stop_locked();}
    // Blocking filesystem/PCM work is deliberately outside AudioOut::Guard.
    const char*result=play_export_copy(*owner,options);
    platform::AudioOut::Guard guard(*g_audio);if(current()!=owner)return;
    const bool switched=owner->sample_slot!=before.sample;identify(*owner);
    if(switched&&copy==Copy::Crop){source_undo.engine=g_eng;source_undo.source[0]=before;source_undo.source[1]=ctx;
        source_undo.doc[0]=d;source_undo.doc[1]=document(*owner);source_undo.side=1;direct_undo_until=osGetTime()+10000;}
    if(!switched&&!direct)back_sheet();
    notice=result;
}
void action(Action action,int argument){
    bool browser=false,io=false,direct=false;
    {platform::AudioOut::Guard guard(*g_audio);C*u=current();if(!u||!valid_context(*u)||!same(document(*u),expected)||param_lens_is_open())return;
        switch(action){
        case Action::Source:stop_locked();browser=true;break;
        case Action::Mode:mode(*u,argument);break;
        case Action::Config:if(view.page==Page::Config)close_menu();else sheet(Page::Config);break;
        case Action::Tool:change_tool(*u,Page(argument));break;
        case Action::Sheet:sheet(Page(argument));break;
        case Action::Group:view.group=Group(argument);break;
        case Action::Back:back_sheet();break;
        case Action::Listen:if(audition_active(*u))stop_locked();else listen(*u);break;
        case Action::History:sheet(Page::History);break;
        case Action::Undo:if(osGetTime()<direct_undo_until&&history_at==0&&source_undo_ready(*u,-1)){undo(*u,-1);direct_undo_until=0;}break;
        case Action::Crop:if(view.page==Page::Trim&&!view.scrub&&u->modern()&&editable(*u)){uint32_t a,b;selection(*u,a,b);if(a||b<ctx.frames){io=true;direct=true;}}break;
        case Action::Sequence:stop_locked();view.from_seq=false;sequencer_open_slices(ctx.chain);break;
        case Action::Add:if(editable(*u)){view.add=!view.add;view.live=false;notice=view.add?"tap wave to add cut / shared source":nullptr;}break;
        case Action::Auto:auto_sheet();break;
        case Action::AutoCount:view.auto_count=argument;propose();break;
        case Action::Row:row_action(*u,argument/2,argument%2!=0);break;
        case Action::Footer:footer_action(*u);break;
        case Action::Confirm:if(view.page==Page::Copy)io=true;
            else if(view.page==Page::Auto&&editable(*u)){if(!overview.ready&&!view.auto_count){notice="wait for source analysis";break;}
                command_begin();apply_cuts(*u,proposed);finish();close_menu();view.cut=proposed.select(0,ctx.frames);notice="shared cuts replaced / IDs not remapped";}break;
        case Action::None:break;}
    }
    if(browser){C*u=current();if(u)module_focus_touch_common_header(*u,155,10);}
    if(io)export_copy(direct);
}
int workspace(const Frame&f){return f.v.scrub?(f.d.bases[C::P_HOLD]>16383?4:3):f.d.pitch_mode==C::PitchMode::Normal?0:f.d.pitch_mode==C::PitchMode::Stretch?1:2;}
const char* workspace_name(const Frame&f){static const char*names[]={"normal","stretch","tape","scratch","hold"};return names[workspace(f)];}
void diamond(int x,int y,u32 color,bool filled){for(int row=-3;row<=3;++row){int half=(3-std::abs(row))/2;if(filled)ui::rect(x-half,y+row,half*2+1,1,color);else{ui::rect(x-half,y+row,1,1,color);ui::rect(x+half,y+row,1,1,color);}}}
void button(layout::Box b,const char*label,bool active=false,bool enabled=true){
    const u32 color=!enabled?palette::shadow():active?m::spot():m::ghost();
    // Footer actions share the caption anchor, but keep their full touch boxes.
    const int y=b.y0==layout::foot.y0&&b.y1==layout::foot.y1?m::kTextY:(b.y0+b.y1)/2-4;
    ui::text((b.x0+b.x1-ui::text_w(label))/2,y,label,color);
}
void rail(){ui::text(265,m::kHeaderTextY,"mode",m::ghost());static const char*names[]={"normal","stretch","tape","scratch","hold"};
    for(int i=0;i<5;++i){const bool selected=workspace(frame)==i;type::word(277,56+i*32,names[i],22,selected&&frame.v.page!=Page::Config?m::spot():m::ghost(),true);
        if(selected)diamond(239,50+i*32,frame.v.page==Page::Config?palette::shadow():m::spot(),frame.v.page!=Page::Config);}
    type::word(277,228,"config",22,frame.v.page==Page::Config?m::spot():m::ghost(),true);
    if(frame.v.page==Page::Config)diamond(239,223,m::spot(),true);
}
void row(int i,const char*label,const char*value,bool enabled=true){ui::text(14,77+i*24,label,enabled?m::ink():m::ghost());right(221,77+i*24,value,enabled?m::spot():palette::shadow());}
void split_row(int i,const char*label,const char*left,const char*right_label){ui::text(14,77+i*24,label,m::ink());ui::text(132,77+i*24,left,m::spot());right(221,77+i*24,right_label,m::spot());}
void sample_value(char*t,size_t n,uint32_t value){std::snprintf(t,n,"%u smp",unsigned(value));}
uint32_t handle_value(const PlayEditState&e,Handle h){switch(h){case Handle::In:return e.start;case Handle::Out:return e.end;case Handle::FadeIn:return e.fade_in;case Handle::FadeOut:return e.fade_out;case Handle::LoopIn:return e.loop_start;case Handle::LoopOut:return e.loop_end;case Handle::Crossfade:return e.crossfade;}return 0;}
void draw_sheet(){const auto&v=frame.v;const auto&d=frame.d;char t[64];const bool cut=d.cuts.valid(v.cut,frame.frames);
    const char*title=v.page==Page::Config?"config":v.page==Page::Auto?"auto cuts":v.page==Page::Fades?"fades":v.page==Page::Zoom?"zoom":v.page==Page::History?"history":v.page==Page::Seq?"seq scrub":v.page==Page::Copy?(v.copy==Copy::Save?"save copy":v.copy==Copy::Detach?"detach raw":"crop copy"):v.selection_page==Page::Cuts?"slices":v.selection_page==Page::Loop?"loop":"trim";
    // Keep the workflow title below/right of the fixed PLAY identity, not over its lower glyphs.
    type::word(78,29,title,28,m::ink());
    if(v.page==Page::Config){static const char*groups[]={"voice","listen","file"};for(int i=0;i<3;++i){button(layout::tab(i),groups[i],i==int(v.group));if(i==int(v.group))ui::rect(30+i*72,62,27,1,m::spot());}
        if(v.group==Group::Voice){row(0,"trigger",!d.modern?"legacy >":play_mode::is_gate(d.bases[C::P_MODE])?"gate >":"one shot >");row(1,"loop",d.bases[C::P_LOOP]>16383?"on":"off");
            const bool gate=d.modern&&play_mode::is_gate(d.bases[C::P_MODE]);std::snprintf(t,sizeof t,"%s step",play_mode::gate_label(d.bases[C::P_GATE]));row(2,"gate length",gate?t:"--",gate);
            row(3,"direction",d.bases[C::P_SPEED]<0?"reverse":"forward");std::snprintf(t,sizeof t,"%u%%",unsigned(clamp(d.bases[C::P_GAIN]))*100u/32767u);row(4,"gain / drag",t);row(5,"bypass",d.bypass?"on":"off");
            if(d.pitch_mode==C::PitchMode::Legacy){button(layout::footer(0),"back");std::snprintf(t,sizeof t,"quantize %s",d.quantize?"on":"off");button({82,216,225,240},t,d.quantize);}else button(layout::foot,"back to wave");
        }else if(v.group==Group::Listen){row(0,"route",v.chain?"chain >":"source >");row(1,"repeat",v.repeat?"on":"off");row(2,"listen start",">");row(3,"listen end",">");row(4,"listen seam",">");row(5,"stop audition",frame.audition?">":"--",frame.audition);button(layout::foot,"back / private preview");
        }else{row(0,"normalize",v.normalize?"-1db":"off");row(1,"remove dc",v.dc?"on":"off");row(2,"bake fades",v.bake?"on":"off");row(3,"bake reverse",v.reverse?"on":"off");row(4,"save selection","wav >");row(5,"detach raw","copy >");button(layout::foot,"back / source untouched");}return;}
    if(v.page==Page::Copy){ui::text(14,49,"new wav / original kept",m::ghost());
        seconds(t,sizeof t,v.copy==Copy::Detach?frame.frames:frame.end-frame.start);row(0,"source length",t);
        row(1,"processing",v.copy==Copy::Save?"options below":"raw only");
        row(2,"normalize",v.copy==Copy::Save&&v.normalize?"-1db":"off");row(3,"remove dc",v.copy==Copy::Save&&v.dc?"on":"off");
        row(4,"fades / reverse",v.copy==Copy::Save?(v.bake?(v.reverse?"on / on":"on / off"):(v.reverse?"off / on":"off / off")):"off / off");
        row(5,"play source",v.copy==Copy::Save?"unchanged":"switch to copy");button(layout::footer(0),"cancel");button(layout::footer(2),"confirm",true);return;}
    if(v.page==Page::Auto){ui::text(14,49,"shared source / replace cuts",m::ghost());const int counts[]={0,4,8,16};const char*names[]={"attack","4","8","16"};for(int i=0;i<4;++i)button({10+i*54,69,64+i*54,93},names[i],v.auto_count==counts[i]);
        std::snprintf(t,sizeof t,"sense %d%%",v.sensitivity);split_row(1,t,"-","+");std::snprintf(t,sizeof t,"%d cuts",frame.proposed.count(frame.frames));row(2,"proposed",t);
        row(3,"clear all cuts",v.auto_count<0?"pending":"choose >");row(4,"old step IDs","not remapped",false);row(5,"analysis",frame.overview_ready?"ready":"loading",frame.overview_ready);
        button(layout::footer(0),"cancel");button(layout::footer(2),"confirm",true);return;}
    if(v.page==Page::History){ui::text(14,49,"per-source edits / 16 levels",m::ghost());row(0,"undo",frame.undo?">":"--",frame.undo);row(1,"redo",frame.redo?">":"--",frame.redo);button(layout::foot,"back");return;}
    if(v.page==Page::Zoom){ui::text(14,49,"view only / source unchanged",m::ghost());row(0,"zoom out","-");row(1,"zoom in","+");row(2,"fit selection",">");row(3,"whole source",">");row(4,"pan / drag","< >");row(5,"fine drag",v.fine?"on":"off");button(layout::foot,"back to wave");return;}
    if(v.page==Page::Seq){ui::text(14,49,"patch playback / explicit",m::ghost());row(0,"sequence scrub",d.bases[C::P_SCRUB]>16383?"on":"off");row(1,"hold behavior",d.bases[C::P_HOLD]>16383?"hold":"scratch");
        std::snprintf(t,sizeof t,"%u%%",unsigned(clamp(d.bases[C::P_POSITION]))*100u/32767u);row(2,"position / drag",t);row(3,"mod position","select >");row(4,"history",">");row(5,"then hold l","for modulation",false);button(layout::foot,"back to wave");return;}
    if(v.page==Page::Tools){ui::text(14,49,"tools / 2 of 2",m::ghost());if(v.selection_page==Page::Cuts){row(0,"live cuts / a marks",v.live?"on":"off");row(1,"clear all cuts","confirm >");row(2,"zoom / pan",">");row(3,"snap",snap_name());row(4,"fine drag",v.fine?"on":"off");row(5,"history",">");}
        else{row(0,"reset trim","whole >");row(1,"fit selection",">");row(2,"crop to copy","confirm >");row(3,"fades",">");row(4,"zoom / pan",">");row(5,"history",">");}button(layout::foot,"back to wave");return;}
    if(v.page==Page::Fades){ui::text(14,49,"exact frames / drag values",m::ghost());sample_value(t,sizeof t,d.edit.fade_in);row(0,"fade in",t);sample_value(t,sizeof t,d.edit.fade_out);row(1,"fade out",t);row(2,"nudge handle",handle_name(v.handle));split_row(3,"one frame","-1","+1");row(4,"fine drag",v.fine?"on":"off");row(5,"reset fades","zero >");button(layout::foot,"back to wave");return;}
    ui::text(14,49,v.selection_page==Page::Cuts?"IDs stable / cuts are shared":"exact frames / drag values",m::ghost());
    if(v.selection_page==Page::Cuts){std::snprintf(t,sizeof t,"id %02d / %02d",v.cut+1,d.cuts.count(frame.frames));split_row(0,t,"<",">");sample_value(t,sizeof t,cut?d.cuts.at[v.cut]:0);row(1,"cut seam / drag",cut?t:"--",cut);split_row(2,"one frame","-1","+1");row(3,"reverse",cut&&(d.cuts.reverse&(1u<<v.cut))?"on":"off",cut);
        std::snprintf(t,sizeof t,"%.2fx",cut?double(d.cuts.speed_q8[v.cut])/256.:1.);row(4,d.pitch_mode==C::PitchMode::Normal?"stored cut rate":"cut tempo / drag",t,cut);row(5,"merge selected",cut?">":"--",cut);button(layout::footer(0),"back");button(layout::footer(2),"more >");}
    else if(v.selection_page==Page::Loop){sample_value(t,sizeof t,d.edit.loop_start);row(0,"loop in",t);sample_value(t,sizeof t,d.edit.loop_end);row(1,"loop out",t);sample_value(t,sizeof t,d.edit.crossfade);row(2,"crossfade",t);split_row(3,handle_name(v.handle),"-1","+1");row(4,"snap",snap_name());row(5,"fine drag",v.fine?"on":"off");button(layout::footer(0),"back");button(layout::footer(2),"seam",true);}
    else{row(0,"selected edge",handle_name(v.handle));sample_value(t,sizeof t,handle_value(d.edit,v.handle));row(1,"position / drag",t);split_row(2,"one frame","-1","+1");row(3,"snap",snap_name());row(4,"fine drag",v.fine?"on":"off");row(5,"move selection",v.move?"on":"off");button(layout::footer(0),"back");button(layout::footer(2),"more >");}
}
} // namespace
void play_editor_capture(){
    frame={};if(!context()){if(g_eng&&g_audio){platform::AudioOut::Guard guard(*g_audio);stop_locked();}ctx={};stroke={};source_undo.engine=nullptr;
        if(g_overlay!=Overlay::SampleBrowser)view.from_seq=false;
        return;}
    {platform::AudioOut::Guard guard(*g_audio);C*u=current();if(!u||!identify(*u)){stop_locked();stroke={};
        if(view.from_seq){g_view=View::Sequencer;g_overlay=Overlay::None;g_drag=Drag::None;view.from_seq=false;}
        return;}}
    analyze();
    {platform::AudioOut::Guard guard(*g_audio);C*u=current();if(!u||!valid_context(*u))return;
        frame.active=true;frame.loaded=ctx.frames>=2&&(ctx.channels==1||ctx.channels==2);frame.v=view;frame.d=document(*u);frame.proposed=proposed;
        frame.chain=ctx.chain;frame.slot=ctx.slot;frame.sample=ctx.sample;frame.channels=ctx.channels;frame.frames=ctx.frames;
        frame.busy=g_eng->play_editor_busy();frame.audition=audition_active(*u);frame.playing=frame.audition||((u->visual_state()&C::VisualPlaying)&&!u->bypass&&!g_eng->chains[ctx.chain].mute);
        if(frame.busy){overview.reset(0,ctx.frames);detail.reset(view.first,view.first+view.span);attacks.reset();}
        frame.position=audition_position(*u);selection(*u,frame.start,frame.end);frame.undo=history_at>0||source_undo_ready(*u,-1);frame.redo=history_at+1<history_count||source_undo_ready(*u,1);
        frame.inline_undo=osGetTime()<direct_undo_until&&history_at==0&&source_undo_ready(*u,-1)&&!frame.busy;
        g_eng->capture_param_inspector(ctx.chain,ctx.slot,view.selected,frame.param);
        frame.mismatch=!u->edit_source_matches();if(!frame.mismatch)frame.d.edit=prepare(*u);
        const auto&p=Bank::instance().provenance(ctx.sample);frame.missing=p.missing;const char*name=std::strrchr(p.path,'/');name=name?name+1:p.path;
        std::snprintf(frame.name,sizeof frame.name,"%.32s",*name?name:p.missing?"missing source":"sample");
        if(notice)std::snprintf(frame.notice,sizeof frame.notice,"%.57s",notice);
    }
    frame.ready=detail.ready;frame.overview_ready=overview.ready;
    int peak=0;if(overview.ready)for(int i=0;i<OV;++i)peak=std::max(peak,std::max(-int(overview.low[i]),int(overview.high[i])));
    frame.gain=uint16_t(clamp(peak?int(24576u*256u/unsigned(peak)):256,256,16384));
    if(frame.ready)detail.freeze(frame.low,frame.high,frame.gain);
    if(frame.overview_ready)overview.freeze(frame.overview_low,frame.overview_high,frame.gain);
}
int play_editor_selected(){return view.selected;}
void play_editor_select(int p){if(p>=0&&p<C::P_COUNT)view.selected=p;}
void play_editor_release(bool touch_up){Action pending=Action::None;int argument=0;
    if(g_eng&&g_audio){platform::AudioOut::Guard guard(*g_audio);C*u=current();
        if(stroke.kind==Gesture::Scrub)stop_locked();
        else if(u&&valid_context(*u)&&stroke.changed&&view.repeat&&!view.live&&stroke.kind!=Gesture::Speed&&stroke.kind!=Gesture::CutSpeed&&stroke.kind!=Gesture::Gain&&stroke.kind!=Gesture::Position&&stroke.kind!=Gesture::Note&&stroke.kind!=Gesture::Tempo&&audition_active(*u))listen(*u);
        if(touch_up&&u&&valid_context(*u)&&same(document(*u),expected)&&!param_lens_is_open()&&
           stroke.kind==Gesture::Menu&&!stroke.moved&&view.page==stroke.page&&view.group==stroke.group&&g_drag!=Drag::None){pending=stroke.action;argument=stroke.argument;}
    }
    finish();if(pending!=Action::None)action(pending,argument);
}
bool play_editor_back(){if(!context())return false;platform::AudioOut::Guard guard(*g_audio);stop_locked();finish();
    if(view.page>Page::Loop){back_sheet();return true;}
    if(view.from_seq){g_view=View::Sequencer;g_overlay=Overlay::None;view.from_seq=false;ctx={};return true;}return false;}
void play_editor_open_cuts(int chain,int slot,bool from_sequencer){
    if(!g_eng||!g_audio||chain<0||chain>=NUM_CHAINS||slot<0||slot>=UNITS_PER_CHAIN)return;
    platform::AudioOut::Guard guard(*g_audio);Unit*u=g_eng->chains[chain].units[slot];if(!u||u->kind!=UnitKind::SamplePlayer)return;
    stop_locked();finish();g_edit_chain=g_sel_chain=chain;g_edit_slot=g_sel_slot=slot;g_view=View::Chains;g_overlay=Overlay::Editor;g_drag=Drag::None;
    g_seq_lock_armed=false;view.from_seq=from_sequencer;
    if(identify(*static_cast<C*>(u))){view.page=view.selection_page=Page::Cuts;view.scrub=view.live=view.add=false;view.selected=C::P_SLICE;auto c=document(*static_cast<C*>(u)).cuts;
        view.cut=c.select(0,ctx.frames);if(view.cut<0)for(int i=0;i<16;++i)if(c.valid(i,ctx.frames)){view.cut=i;break;}}
}
unsigned play_editor_context_keys(unsigned down,unsigned held){
    if(!context()||!view.from_seq||param_lens_is_open())return 0;
    unsigned owned=down&(KEY_X|KEY_Y|KEY_R);
    if((down&KEY_START)&&!(held&KEY_L))owned|=KEY_START;
    platform::AudioOut::Guard guard(*g_audio);C*u=current();if(!u||!identify(*u))return owned;
    if((down&KEY_X)&&(held&KEY_L)){
        if(g_eng->remove_step_lock(ctx.chain,sequencer_selected_step(),ctx.slot,C::P_SLICE,sequencer_edit_bank()))mark_project_mutated();
    }
    if((down&KEY_START)&&!(held&KEY_L)){stop_locked();if(held&KEY_R){g_eng->transport_stop();g_eng->transport_reset();}
        else if(g_eng->transport_playing)g_eng->transport_stop();else{
        // Seq-origin START has the same audible-score semantics as the sequencer.
        g_eng->set_sequencer_enabled(true);g_eng->transport_play();}}
    return owned;
}
bool play_editor_button(unsigned down){if(!(down&KEY_A)||!context()||param_lens_is_open())return false;
    platform::AudioOut::Guard guard(*g_audio);C*u=current();if(!u||!identify(*u))return false;finish();
    if(view.live)live_mark(*u);else if(view.scrub)notice="drag wave to audition / lift stops";else listen(*u);return true;}
bool play_editor_touch(C&u,int x,int y){if(!context()||param_lens_is_open())return false;
    platform::AudioOut::Guard guard(*g_audio);if(!identify(u))return false;finish();
    auto hit=[&](layout::Box box,Action a,int arg=0){if(!box.contains(x,y))return false;queue(a,arg,box,x,y);return true;};
    auto drag=[&](Gesture gesture,int param){view.selected=param;if(editable(u))begin(gesture,x,y);};
    auto edge=[&](Handle handle){view.handle=handle;view.move=false;drag(Gesture::Edge,handle==Handle::In||handle==Handle::LoopIn?C::P_START:C::P_LEN);};
    for(int i=0;i<5;++i)if(hit(layout::mode(i),Action::Mode,i))return true;
    if(hit(layout::config,Action::Config))return true;
    if(view.page>Page::Loop){
        if(view.page==Page::Config)for(int i=0;i<3;++i)if(hit(layout::tab(i),Action::Group,i))return true;
        if(view.page==Page::Auto||view.page==Page::Copy){if(hit(layout::footer(0),Action::Back)||hit(layout::footer(2),Action::Confirm))return true;}
        else if(view.page==Page::Edit){if(hit(layout::footer(0),Action::Back)||hit(layout::footer(2),Action::Footer))return true;}
        else if(view.page==Page::Config&&view.group==Group::Voice&&u.pitch_mode()==C::PitchMode::Legacy){if(hit(layout::footer(0),Action::Back)||hit({82,216,225,240},Action::Footer))return true;}
        else if(hit(layout::foot,Action::Footer))return true;
        if(view.page==Page::Copy)return true;
        for(int i=0;i<6;++i)if(layout::row(i).contains(x,y)){
            if(view.page==Page::Config&&view.group==Group::Voice){
                if(i==2){view.selected=C::P_GATE;if(u.modern()&&play_mode::is_gate(u.params[C::P_MODE].base)&&editable(u))begin(Gesture::Gate,x,y);return true;}
                if(i==4){drag(Gesture::Gain,C::P_GAIN);return true;}}
            if(view.page==Page::Fades&&i<2){edge(i?Handle::FadeOut:Handle::FadeIn);return true;}
            if(view.page==Page::Edit){
                if(view.selection_page==Page::Cuts&&i==1){view.selected=C::P_SLICE;if(document(u).cuts.valid(view.cut,ctx.frames)&&editable(u))begin(Gesture::Cut,x,y);return true;}
                if(view.selection_page==Page::Cuts&&i==4){view.selected=C::P_SPEED;if(document(u).cuts.valid(view.cut,ctx.frames)&&editable(u))begin(Gesture::CutSpeed,x,y);return true;}
                if(view.selection_page==Page::Trim&&i==1){edge(view.handle);return true;}
                if(view.selection_page==Page::Loop&&i<3){edge(i==0?Handle::LoopIn:i==1?Handle::LoopOut:Handle::Crossfade);return true;}}
            if(view.page==Page::Zoom&&i==4){begin(Gesture::Pan,x,y);return true;}
            if(view.page==Page::Seq&&i==2){view.selected=C::P_POSITION;if(u.modern()&&editable(u))begin(Gesture::Position,x,y);return true;}
            if(view.page==Page::Auto&&i==0){const int counts[]={0,4,8,16};const int column=clamp((x-10)/54,0,3);hit({10+column*54,69,64+column*54,93},Action::AutoCount,counts[column]);return true;}
            if((view.page==Page::Auto&&(i==2||i>=4))||(view.page==Page::History&&i>=2)||(view.page==Page::Seq&&i==5))return true;
            const bool split=(view.page==Page::Auto&&i==1)||(view.page==Page::Fades&&i==3)||(view.page==Page::Edit&&
                (view.selection_page==Page::Cuts?(i==0||i==2):view.selection_page==Page::Loop?i==3:i==2));
            auto box=layout::row(i);const bool second=x>=176;if(split){if(second)box.x0=176;else box.x1=176;}
            hit(box,Action::Row,i*2+(second?1:0));return true;
        }
        return true;
    }
    if(hit(layout::source,Action::Source))return true;
    if(layout::primary.contains(x,y)){drag(view.scrub?Gesture::Position:u.pitch_mode()==C::PitchMode::Legacy?Gesture::Speed:Gesture::Note,
            view.scrub?C::P_POSITION:u.pitch_mode()==C::PitchMode::Legacy?C::P_SPEED:C::P_NOTE);return true;}
    if(!view.scrub&&layout::secondary.contains(x,y)){const bool tempo=u.pitch_mode()==C::PitchMode::Stretch;drag(tempo?Gesture::Tempo:Gesture::Gain,tempo?C::P_SPEED:C::P_GAIN);return true;}
    if(!view.scrub)for(int i=0;i<3;++i)if(hit(layout::tool(i),Action::Tool,i))return true;
    if(hit(layout::footer(2),Action::Sequence))return true;
    if(view.scrub){if(hit(layout::context_left,Action::Sheet,int(Page::Zoom))||hit(layout::context_right,Action::Sheet,int(Page::Seq)))return true;
        if(hit({10,216,150,240},Action::History))return true;
    }else{
        if(hit(layout::footer(0),Action::Listen)||hit(layout::footer(1),osGetTime()<direct_undo_until&&history_at==0&&source_undo_ready(u,-1)?Action::Undo:Action::History))return true;
        if(view.page==Page::Cuts){if(hit(layout::context(0),Action::Add)||hit(layout::context(1),Action::Auto)||hit(layout::context(2),Action::Sheet,int(Page::Edit)))return true;}
        else if(view.page==Page::Trim){if(hit(layout::context(0),Action::Crop)||hit(layout::context(1),Action::Sheet,int(Page::Zoom))||hit(layout::context(2),Action::Sheet,int(Page::Edit)))return true;}
        else {if(hit(layout::context_right,Action::Sheet,int(Page::Edit)))return true;
            if(layout::context_left.contains(x,y)){edge(Handle::Crossfade);return true;}}
    }
    if(!view.scrub&&view.page==Page::Trim){uint32_t a,b;selection(u,a,b);const auto h=trim_handles(a,b,view.first,view.span);
        if(h.has_in&&h.in.contains(x,y)){edge(Handle::In);return true;}
        if(h.has_out&&h.out.contains(x,y)){edge(Handle::Out);return true;}
        if(!view.move)return true; // the middle is passive unless explicitly in move mode
    }
    if(!layout::wave.contains(x,y))return true;
    if(!editable(u)){if(ctx.frames<2)notice="load a valid sample first";return true;}
    if(view.scrub){begin(Gesture::Scrub,x,y);scrub_at(u,x,true);return true;}
    if(view.page==Page::Cuts){auto cuts=document(u).cuts;const uint32_t at=std::min(ctx.frames-1,at_x(x));
        if(view.add){int id=cuts.insert(snap(at),ctx.frames);view.add=false;
            if(id<0){notice="cut refused / full or too close";return true;}command_begin();apply_cuts(u,cuts);view.cut=id;view.selected=C::P_SLICE;finish();return true;}
        if(y>=162){int near=-1,distance=13,candidates=0;
            for(int id=0;id<16;++id)if(cuts.valid(id,ctx.frames)&&cuts.at[id]>=view.first&&cuts.at[id]<=view.first+view.span){
                const int d=std::abs(x-fx(cuts.at[id],view.first,view.first+view.span,X,W));if(d<=12)++candidates;if(d<distance){distance=d;near=id;}}
            if(candidates>1){near=view.cut;if(!cuts.valid(near,ctx.frames)||std::abs(x-fx(cuts.at[near],view.first,view.first+view.span,X,W))>12){notice="close cuts / choose ID in edit";return true;}}
            if(near>=0){view.cut=near;view.selected=C::P_SLICE;begin(Gesture::Cut,x,y);return true;}}
        view.cut=cuts.select(at,ctx.frames);view.selected=C::P_SLICE;if(view.cut>=0)listen(u);return true;
    }
    if(view.move&&view.page==Page::Trim)begin(Gesture::Move,x,y);
    else {const bool start=y<147;edge(view.page==Page::Loop?(start?Handle::LoopIn:Handle::LoopOut):(start?Handle::In:Handle::Out));}
    return true;
}
void play_editor_drag(C&u,int x,int y){if(stroke.kind==Gesture::None)return;
    if(!context()||g_drag==Drag::None||param_lens_is_open()){play_editor_release(false);return;}
    platform::AudioOut::Guard guard(*g_audio);if(!valid_context(u)||!same(document(u),expected)||(g_eng->play_editor_busy()&&stroke.kind!=Gesture::Menu&&stroke.kind!=Gesture::Pan)){
        if(stroke.kind==Gesture::Scrub)stop_locked();
        stroke={};notice="gesture cancelled / context changed";return;}
    const int delta=x-stroke.x;
    if(stroke.kind==Gesture::Menu){
        if(std::abs(y-stroke.y)>=8||std::abs(delta)>=8||!stroke.box.contains(x,y))stroke.moved=true;
        return;}
    if(stroke.kind==Gesture::Pan){view.first=uint32_t(clamp(int64_t(stroke.first)+int64_t(delta)*ctx.frames/W,0,ctx.frames-view.span));return;}
    if(!editable(u))return;
    const int64_t movement=view.fine?delta:int64_t(delta)*stroke.span/W;
    if(stroke.kind==Gesture::Scrub){scrub_at(u,x,false);return;}
    if(stroke.kind==Gesture::CutSpeed){auto cuts=document(u).cuts;if(!cuts.valid(view.cut,ctx.frames))return;
        const int rate=clamp(int(stroke.before.cuts.speed_q8[view.cut])+delta*(view.fine?1:4),64,1024);
        cuts.speed_q8[view.cut]=uint16_t(rate);apply_cuts(u,cuts);
        if(preview_cut==view.cut){if(view.token)g_eng->set_play_preview_speed(view.token,uint16_t(rate));
            else if(chain_preview_owner==uintptr_t(&u)&&u.preview_active())u.set_preview_speed(uint16_t(rate));}
        return;}
    if(stroke.kind==Gesture::Note){const int step=delta/(view.fine?16:4);
        const q15 next=step?musical_note::encode(musical_note::decode(stroke.before.bases[C::P_NOTE])+step):stroke.before.bases[C::P_NOTE];
        if(u.params[C::P_NOTE].base!=next){u.params[C::P_NOTE].base=next;changed(u);}return;}
    if(stroke.kind==Gesture::Tempo){if(!delta&&!stroke.changed)return;const int origin=stroke.before.bases[C::P_SPEED];
        const int magnitude=clamp(std::abs(origin)+delta*(view.fine?8:128),2048,32767);
        const q15 next=delta?q15(origin<0?-magnitude:magnitude):q15(origin);
        if(u.params[C::P_SPEED].base!=next){u.params[C::P_SPEED].base=next;changed(u);}return;}
    if(stroke.kind==Gesture::Position){const q15 next=q15(clamp(int(stroke.before.bases[C::P_POSITION])+delta*(view.fine?1:256)));
        if(u.params[C::P_POSITION].base!=next){u.params[C::P_POSITION].base=next;changed(u);}return;}
    if(stroke.kind==Gesture::Speed||stroke.kind==Gesture::Gain||stroke.kind==Gesture::Gate){int p=stroke.kind==Gesture::Speed?C::P_SPEED:stroke.kind==Gesture::Gain?C::P_GAIN:C::P_GATE;
        q15 next;if(p==C::P_GATE){int steps=delta>=18?(delta-18)/40+1:delta<=-18?-((-delta-18)/40+1):0;next=play_mode::gate_value(clamp(play_mode::gate_index(stroke.before.bases[p])+steps,0,5));}
        else next=q15(clamp(int64_t(stroke.before.bases[p])+int64_t(delta)*(p==C::P_SPEED?65534:32767)/128,p==C::P_SPEED?-32767:0,32767));
        if(u.params[p].base!=next){u.params[p].base=next;changed(u);}return;}
    if(!movement&&!stroke.changed)return;
    if(stroke.kind==Gesture::Cut){auto c=stroke.before.cuts;uint32_t target=snap(uint32_t(clamp(int64_t(c.at[view.cut])+movement,0,ctx.frames)));
        if(c.move(view.cut,target,ctx.frames)||c!=document(u).cuts)apply_cuts(u,c);
        return;}
    auto e=stroke.before.edit;if(!e.enabled){e=prepare(u); // preserve original q15 endpoints throughout first exact gesture
        const auto w=play_window(ctx.frames,nullptr,0,8192,stroke.before.bases[C::P_START],stroke.before.bases[C::P_LEN],0,false,true);
        e.start=w.start;e.end=w.end;e.loop_start=e.start;e.loop_end=e.end;}
    auto shift=[&](uint32_t v){return uint32_t(clamp(int64_t(v)+movement,0,ctx.frames));};
    if(stroke.kind==Gesture::Move){const int64_t d=std::max(-int64_t(e.start),std::min<int64_t>(ctx.frames-e.end,movement));e.start+=d;e.end+=d;e.loop_start+=d;e.loop_end+=d;}
    else switch(view.handle){case Handle::In:e.start=std::min(snap(shift(e.start)),e.end-2);break;case Handle::Out:e.end=std::max(snap(shift(e.end)),e.start+2);break;
        case Handle::FadeIn:e.fade_in=shift(e.fade_in);break;case Handle::FadeOut:e.fade_out=uint32_t(clamp(int64_t(e.fade_out)-movement,0,ctx.frames));break;
        case Handle::LoopIn:e.loop_start=std::min(snap(shift(e.loop_start)),e.loop_end-2);break;case Handle::LoopOut:e.loop_end=std::max(snap(shift(e.loop_end)),e.loop_start+2);break;
        case Handle::Crossfade:e.crossfade=shift(e.crossfade);break;}
    set_edit(u,e);(void)y;
}
namespace {
void primary_text(char*t,size_t n,const Frame&f){
    if(f.v.scrub)std::snprintf(t,n,"%u",unsigned(clamp(f.d.bases[C::P_POSITION]))*100u/32767u);
    else if(f.d.pitch_mode!=C::PitchMode::Legacy)musical_note::name(t,n,musical_note::decode(f.d.bases[C::P_NOTE]));
    else {const auto w=play_window(2,nullptr,0,f.d.bases[C::P_SPEED],0,32767,0,f.d.quantize,f.d.modern);std::snprintf(t,n,"%.2f",double(w.increment)/65536.);}
}
} // namespace
void play_editor_draw(){m::paper(320);if(!frame.active)return;const auto&v=frame.v;const auto&d=frame.d;char t[64];
    module_heading::header(UnitKind::SamplePlayer,m::spot());
    rail();if(v.page>Page::Loop){draw_sheet();return;}
    m::header_text(56,207,frame.name,m::ink());right(221,m::kHeaderTextY,">",m::spot());
    ui::text(14,37,v.scrub?"position":d.pitch_mode==C::PitchMode::Legacy?"speed":"note",m::ghost());
    primary_text(t,sizeof t,frame);const int size=!v.scrub&&d.pitch_mode==C::PitchMode::Legacy?28:40;
    const int width=type::value(14,size==28?76:77,t,size,m::ink());
    if(v.scrub||d.pitch_mode==C::PitchMode::Legacy)ui::text(14+width+5,68,v.scrub?"%":"x",m::ghost());
    if(v.scrub){ui::text(124,40,"touch preview",m::spot());ui::text(124,58,d.bases[C::P_HOLD]>16383?"still = sound":"move to sound",m::ghost());ui::text(124,73,"lift = stop",m::ghost());ui::text(14,94,"drag the wave",m::spot());}
    else {const bool tempo=d.pitch_mode==C::PitchMode::Stretch;ui::text(126,37,tempo?"tempo":"gain",m::ghost());
        if(tempo)std::snprintf(t,sizeof t,"%.2f",double(std::max(2048,std::abs(int(d.bases[C::P_SPEED]))))/8192.);
        else std::snprintf(t,sizeof t,"%u",unsigned(clamp(d.bases[C::P_GAIN]))*100u/32767u);
        const int w=type::value(126,76,t,28,tempo?m::spot():m::ink());ui::text(126+w+5,68,tempo?"x":"%",m::ghost());
        const char*names[]={"trim","slices","loop"};for(int i=0;i<3;++i){const auto box=layout::tool(i);button(box,names[i],i==int(v.selection_page));}}
    draw_wave(false);
    if(v.scrub){ui::text(14,181,"source audition",m::ghost());right(221,181,"1x",m::ghost());button(layout::context_left,"zoom >");button(layout::context_right,d.bases[C::P_SCRUB]>16383?"seq on >":"seq off >");
        button({10,216,150,240},"history / l: mod");
    }else{
        if(v.page==Page::Cuts){std::snprintf(t,sizeof t,"%02d / %02d",v.cut+1,d.cuts.count(frame.frames));ui::text(14,181,t,m::spot());seconds(t,sizeof t,frame.end-frame.start);right(221,181,t,m::ghost());
            button(layout::context(0),v.add?"tap wave":"+ cut",v.add);button(layout::context(1),"auto");button(layout::context(2),"edit >");}
        else {seconds(t,sizeof t,frame.start);ui::text(14,181,t,m::ghost());seconds(t,sizeof t,frame.end);right(221,181,t,m::ghost());
            if(v.page==Page::Trim){button(layout::context(0),"crop",true,!frame.busy&&d.modern&&(frame.start>0||frame.end<frame.frames));const double zoom=v.span?double(frame.frames)/v.span:1.;std::snprintf(t,sizeof t,"zoom %.0fx",zoom);
                button(layout::context(1),ui::text_w(t)<=68?t:"zoom >");button(layout::context(2),v.move?"move on":"edit >",v.move);}
            else {std::snprintf(t,sizeof t,"seam %ums",unsigned(uint64_t(d.edit.crossfade)*1000/32000));button(layout::context_left,ui::text_w(t)<=100?t:"crossfade");button(layout::context_right,"edit >");}}
        button(layout::footer(0),frame.audition?"stop":"listen",true);button(layout::footer(1),frame.inline_undo?"undo":"history >",frame.inline_undo,frame.undo||frame.redo);
    }
    button(layout::footer(2),"seq >");
}
bool play_editor_top(){if(!frame.active)return false;m::paper(400);const auto&v=frame.v;const auto&d=frame.d;char t[80],a[24];
    module_heading::header(UnitKind::SamplePlayer,m::spot());
    m::header_text(90,m::right_edge(400)-ui::text_w("ch00 / 00")-module_frame_layout::kCaptionGap,frame.name,m::ink());seconds(a,sizeof a,frame.frames);
    std::snprintf(t,sizeof t,"%s / %s",a,frame.channels==2?"stereo > mono":"mono");ui::text(90,36,t,m::ghost());
    std::snprintf(t,sizeof t,"ch%02d / %02d",frame.chain+1,frame.slot+1);right(m::right_edge(400),m::kHeaderTextY,t,m::ghost());
    right(380,36,!d.modern?"legacy":d.bases[C::P_SCRUB]>16383?(d.bases[C::P_HOLD]>16383?"seq hold":"seq scratch"):play_mode::is_gate(d.bases[C::P_MODE])?"gate":d.bases[C::P_LOOP]>16383?"loop":"one shot",m::ghost());
    type::word(20,73,workspace_name(frame),22,m::spot());right(380,62,frame.param.temp?"temporary":frame.audition?(v.chain?"chain audition":"source audition"):v.scrub?"touch preview":"sample overview",m::ghost());
    draw_wave(true);
    if(frame.frames)for(int i=0;i<=4;++i){const uint32_t at=uint32_t(uint64_t(i)*frame.frames/4);seconds(t,sizeof t,at);const int xx=20+i*359/4;
        ui::rect(xx,157,1,4,palette::shadow());ui::text(clamp(xx,20,380-ui::text_w(t)),166,t,m::ghost());}
    if(v.first||v.span<frame.frames){const int left=fx(v.first,0,frame.frames,20,359),r=fx(v.first+v.span,0,frame.frames,20,359);ui::rect(left,158,std::max(1,r-left),2,m::spot());}
    if(v.page==Page::Config){type::word(20,213,"config",28,m::ink());
        ui::text(200,192,v.group==Group::Voice?"playback in the patch":v.group==Group::Listen?"private audition options":"save a separate wav",m::ink());
        ui::text(200,210,v.group==Group::Voice?"not preview repeat":v.group==Group::Listen?"source or effects chain":"original stays unchanged",m::ghost());
    }else if(!v.scrub&&v.page==Page::Trim){ui::text(20,193,"kept length",m::ghost());std::snprintf(t,sizeof t,"%.2f",double(frame.end-frame.start)/ENGINE_SR);type::value(20,227,t,40,m::ink());
        ui::text(200,195,frame.inline_undo?"original kept":"crop makes a shorter sample",m::ink());ui::text(200,214,frame.inline_undo?"undo restores it":"drag in / out to shorten",m::ghost());
    }else {ui::text(20,190,v.scrub?"position":d.pitch_mode==C::PitchMode::Legacy?"speed":"note",m::ghost());primary_text(t,sizeof t,frame);
        const int w=type::value(20,225,t,40,m::ink());if(v.scrub||d.pitch_mode==C::PitchMode::Legacy)ui::text(20+w+5,216,v.scrub?"%":"x",m::ghost());
        if(v.scrub){ui::text(200,193,d.bases[C::P_HOLD]>16383?"up to 50ms under cursor":"move cursor to sound",m::ink());ui::text(200,211,"lift stops source audition",m::ghost());}
        else if(d.pitch_mode==C::PitchMode::Stretch){std::snprintf(t,sizeof t,"tempo %.2fx / independent",double(std::max(2048,std::abs(int(d.bases[C::P_SPEED]))))/8192.);ui::text(200,193,t,m::ink());ui::text(200,211,"manual tempo / no bpm sync",m::ghost());}
        else {ui::text(200,193,d.pitch_mode==C::PitchMode::Normal?"note changes duration":"speed changes pitch",m::ink());ui::text(200,211,d.pitch_mode==C::PitchMode::Normal?"source prepared at c4":"original varispeed",m::ghost());}}
    const char*status=frame.busy?"recording / editor protected":frame.mismatch?"source changed / choose source again":frame.missing?"missing source / choose another sample":frame.notice[0]?frame.notice:nullptr;
    if(v.snapped&&!frame.busy){std::snprintf(t,sizeof t,"snap %s / source frame %u",snap_name(),unsigned(v.snap_frame));status=t;}
    if(status)ui::text(m::kLeft,m::kTextY,status,m::ghost());
    return true;
}
// PUBLIC_API
} // namespace seance::platform::ui3ds
