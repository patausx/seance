#include "ui_play_editor.h"
#include "ui_play_export.h"
#include "ui_module_focus_internal.h"
#include "ui_state.h"
#include "ui_theme.h"
#include "ui_palette.h"
#include "ui_sequencer.h"
#include "ui_param_lens.h"
#include "ui_sample_library.h"
#include "ui_nav.h"
#include "audio_out.h"
#include "editor_style.h"
#include "module_heading.h"
#include "module_fader.h"
#include "play_readout.h"
#include "play_editor_layout.h"
#include "play_editor_precision.h"
#include "play_editor_edges.h"
#include "../../core/ui/draw.h"
#include "../../core/ui/play_editor_tools.h"
#include "../../core/ui/play_part_pitch.h"
#include "../../core/engine/play_window.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <type_traits>
#include "play_word_layout.h"
#include "ui_module_rand.h"
#include "sequencer_slices.h"

namespace seance::platform::ui3ds {
namespace {
namespace pw=play_word_layout;
namespace m=editor_style; namespace pe=play_editor; namespace layout=play_editor_layout;
using C=PlayUnit;using Bank=trackr::synth::SampleBank;
namespace precise=play_editor_precision;namespace pl=layout::precision;namespace edges=play_editor_edges;
constexpr int N=372,OV=360;
// Tool selection is orthogonal to the visible bounded sheet and pitch engine.
enum class Page:uint8_t {Trim,Cuts,Loop,Config,Auto,Edit,Tools,Fades,Zoom,Copy,Live,Processing};
enum class Group:uint8_t {Voice,File};
enum class Copy:uint8_t {Save,Detach,Crop};
enum class Handle:uint8_t {In,Out,FadeIn,FadeOut,LoopIn,LoopOut,Crossfade};
enum class Owner:uint8_t {First,Second,Third};
enum class Gesture:uint8_t {None,Edge,Cut,Pan,Move,Speed,CutSpeed,Gain,Gate,Menu,Note,Tempo,Choose,Insert,PartPitch,TapeSpeed};
enum class Action:uint8_t {None,Source,Tool,Sheet,Listen,Sequence,Surface,Owner,Fine,Zoom,Word};
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
struct Options {
    Page page=Page::Trim,selection_page=Page::Trim;Handle handle=Handle::In;int selected=C::P_START,cut=-1;
    uint32_t first=0,span=0,token=0;int snap=0,sensitivity=60,auto_count=0,scale=1;
    Owner owner=Owner::First;bool sound=false,cut_end=false;
    bool live=false,add=false,fine=false,from_seq=false,snapped=false;
    uint32_t snap_frame=0;
    bool normalize=false,dc=false,bake=false,reverse=false,move=false;
    Group group=Group::Voice;Copy copy=Copy::Save;Page return_page=Page::Trim;
} view;
const char* notice=nullptr;
Document expected;
struct Stroke {Gesture kind=Gesture::None;int x=0,y=0;uint32_t first=0,span=0;Document before{};bool changed=false,moved=false;
    Action action=Action::None;int argument=0;layout::Box box{};Page page=Page::Trim;Group group=Group::Voice;
    Context source{};Options owner{};PlayEditState edit{};int cut_target=-1;uint32_t candidate=0,snap_beat=0;bool precision=false,snap_ready=false;
    uint64_t pressed=0;int excursion=0;bool hold_possible=true,held=false,merge_preview=false,direct_cut=false;
    uint32_t audio_token=0;bool borrowed_audio=false,audio_attempted=false;
} stroke;
constexpr uint64_t kMergeHoldMs=450;
constexpr int kHoldMotion=5,kMergeExit=12,kDragThreshold=3;
precise::Envelope<N> detail;pe::Envelope<OV> overview;pe::Attacks attacks;
pe::Cuts proposed;
int preview_cut=-1;
struct Frame {
    bool active=false,loaded=false,ready=false,overview_ready=false,busy=false,playing=false,audition=false,missing=false,mismatch=false;
    Options v{};Document d{};pe::Cuts proposed{};
    int chain=0,slot=0,sample=0;uint8_t channels=0;
    uint32_t frames=0,start=0,end=0,position=0;uint16_t gain=256;
    int8_t low[N]={},high[N]={};
    int8_t dragging_side=-1,dragging_cut=-1;
    bool inserting=false,merging=false;int merge_left=-1,merge_removed=-1;uint32_t candidate=0;
    char name[33]={},notice[58]={};
    q15 live[C::P_COUNT]={};uint32_t links=0;
    bool quiet=false;
} frame;
static_assert(sizeof(Frame)<3072&&std::is_trivially_copyable<Frame>::value,"bounded PLAY editor frame");
int clamp(int64_t v,int lo=0,int hi=32767){return int(std::max<int64_t>(lo,std::min<int64_t>(hi,v)));}
bool context(){return g_eng&&g_audio&&!nav_is_open()&&g_view==View::Chains&&g_overlay==Overlay::Editor&&!g_seq_lock_armed&&
    g_edit_chain>=0&&g_edit_chain<NUM_CHAINS&&g_edit_slot>=0&&g_edit_slot<UNITS_PER_CHAIN;}
C* current(){if(!context())return nullptr;Unit*u=g_eng->chains[g_edit_chain].units[g_edit_slot];
    if(view.from_seq){int last=-1;for(int s=0;s<UNITS_PER_CHAIN;++s){const Unit* at=g_eng->chains[g_edit_chain].units[s];if(at&&!at->bypass&&at->sequencer_properties().source)last=s;}
        if(last!=g_edit_slot&&!(u&&u->bypass&&last<g_edit_slot))return nullptr;}
    return u&&u->kind==UnitKind::SamplePlayer?static_cast<C*>(u):nullptr;}
Document document(const C&u){Document d;d.edit=u.edit_state();d.modern=u.modern();d.quantize=u.pitch_quantize;d.bypass=u.bypass;d.pitch_mode=u.pitch_mode();
    for(int p=0;p<C::P_COUNT;++p){d.bases[p]=u.params[p].base;d.routing=(d.routing*16777619u)^uint32_t(uint16_t(u.params[p].mod_depth))^(uint32_t(u.params[p].src)<<16);}
    const auto&s=Bank::instance().slot(u.sample_slot);std::copy(s.chops,s.chops+16,d.cuts.at);d.cuts.reverse=s.slice_reverse_mask;
    std::copy(s.slice_speed_q8,s.slice_speed_q8+16,d.cuts.speed_q8);
    std::copy(s.slice_pitch_semitones,s.slice_pitch_semitones+16,d.cuts.pitch_semitones);return d;}
bool valid_context(const C&u){const auto&b=Bank::instance();if(current()!=&u||u.sample_slot!=ctx.sample)return false;const auto&s=b.slot(u.sample_slot);
    return uintptr_t(&u)==ctx.unit&&g_edit_chain==ctx.chain&&g_edit_slot==ctx.slot&&b.generation(ctx.sample)==ctx.generation&&
        uintptr_t(s.data.data())==ctx.pcm&&s.playable_frames()==ctx.frames&&s.channels==ctx.channels;}
void restart_note_owner();
void words_cancel(bool quarantine=false);
bool words_modal();
void words_activate(int operation,int argument);
void words_capture();
void words_tools();
void inline_draw_controls();
bool inline_touch_controls(C&,int,int);
void stop_stroke_locked();
void part_value(char*,size_t,int8_t);
void words_root();
void words_open(Page);
bool words_touch(C&,int,int);
bool words_release(bool);
void words_draw();
bool words_surface();
void stop_locked(){preview_cut=-1;if(g_eng){if(stroke.audio_token)g_eng->stop_play_preview(stroke.audio_token);if(view.token)g_eng->stop_play_preview(view.token);}view.token=0;stroke.audio_token=0;}
void reset_document(const Document&d){expected=d;stroke={};}
void finish(){stop_stroke_locked();stroke={};}
void reconcile_workspace(const C&u){if(view.selected>=u.num_params)view.selected=C::P_SPEED;}
bool identify(C&u){if(current()!=&u)return false;
    if(u.sample_slot<0||u.sample_slot>=trackr::synth::SAMPLE_BANK_SIZE)return false;
    if(!valid_context(u)){words_cancel(true);stop_locked();const bool return_to_seq=view.from_seq;view={};view.from_seq=return_to_seq;notice=nullptr;
        const auto&b=Bank::instance();const auto&s=b.slot(u.sample_slot);
        ctx={uintptr_t(&u),uintptr_t(s.data.data()),g_edit_chain,g_edit_slot,u.sample_slot,b.generation(u.sample_slot),s.playable_frames(),s.channels};
        view.selected=u.pitch_mode()==C::PitchMode::Legacy?C::P_SPEED:C::P_NOTE;
        view.span=ctx.frames;overview.reset(0,ctx.frames);detail.reset(0,ctx.frames);attacks.reset();reset_document(document(u));
    }else if(!same(document(u),expected)){
        words_cancel(true);stop_locked();reset_document(document(u));view.add=view.live=false;notice="external edit / gesture cancelled";
    }
    reconcile_workspace(u);return true;
}
void changed(C&u){expected=document(u);stroke.changed=true;mark_project_mutated();}
void command_begin(){finish();stroke.before=expected;}
void command_end(C&u){if(!same(document(u),expected))changed(u);finish();}
bool editable(C&u){if(g_eng->play_editor_busy()){notice="recording / editor protected";return false;}
    if(!valid_context(u)||ctx.frames<2||(ctx.channels!=1&&ctx.channels!=2))return false;
    if(!u.edit_source_matches()){notice="source changed / choose source again";return false;}
    if(Bank::instance().provenance(ctx.sample).missing){notice="missing source / choose another sample";return false;}
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
    std::copy(c.speed_q8,c.speed_q8+16,s.slice_speed_q8);
    std::copy(c.pitch_semitones,c.pitch_semitones+16,s.slice_pitch_semitones);changed(u);}
void selection(const C&u,uint32_t&a,uint32_t&b){const auto d=document(u);
    if(view.selection_page==Page::Cuts&&d.cuts.valid(view.cut,ctx.frames)){
        a=d.cuts.at[view.cut];b=d.cuts.end(view.cut,ctx.frames);return;}
    const auto w=u.projected_window(u.params[C::P_START].base,u.params[C::P_LEN].base,0);a=w.start;b=w.valid?w.end:w.start;
    if(view.selection_page==Page::Loop&&d.edit.enabled){const auto e=prepare(u);a=e.loop_start;b=e.loop_end;}
}
bool preview_locked(C&u,uint32_t a,uint32_t b,bool repeat){
    if(!editable(u)||u.bypass||g_eng->chains[ctx.chain].mute){notice="audition unavailable";return false;}
    stop_locked();bool reverse=false;uint16_t rate=256;int8_t pitch=0;auto d=document(u);
    if(view.selection_page==Page::Cuts&&d.cuts.valid(view.cut,ctx.frames)){
        reverse=d.cuts.reverse&(1u<<view.cut);rate=d.cuts.speed_q8[view.cut];pitch=d.cuts.pitch_semitones[view.cut];preview_cut=view.cut;}
    view.token=g_eng->start_play_preview(ctx.chain,ctx.slot,a,b,repeat,reverse,rate,false,pitch);
    notice=view.token?nullptr:"listen unavailable";return view.token!=0;
}
void listen(C&u){uint32_t a,b;selection(u,a,b);preview_locked(u,a,b,false);}
bool audition_active(const C&){return g_eng->play_preview_active(view.token);}
uint32_t audition_position(const C&u){return g_eng->play_preview_active(view.token)?g_eng->play_preview_position(view.token):u.visual_source_frame();}
void zoom(C&u,int direction,bool fit=false){
    if(stroke.kind!=Gesture::None)return;
    const int requested=fit?1:direction>0?view.scale*2:std::max(1,view.scale/2);
    uint32_t a,b;selection(u,a,b);
    const auto v=precise::zoom(ctx.frames,requested,a+(b-a)/2);
    view.first=v.first;view.span=v.span;view.scale=v.scale;
}
uint32_t snapped(uint32_t requested,uint32_t result){view.snapped=result!=requested;view.snap_frame=result;return result;}
uint32_t snap(uint32_t at){view.snapped=false;const uint32_t radius=std::max<uint32_t>(2,std::min<uint32_t>(512,view.span/40));
    if(view.snap==2){if(stroke.kind!=Gesture::None&&!stroke.snap_ready)return at;return snapped(at,attacks.nearest(at,radius));}
    if(view.snap==3){const uint32_t beat=stroke.kind==Gesture::None?g_eng->frames_per_beat():stroke.snap_beat;if(!beat)return at;
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
void begin(Gesture kind,int x,int y,int boundary=-1){finish();stroke.kind=kind;stroke.x=x;stroke.y=y;stroke.first=view.first;stroke.span=view.span;stroke.before=expected;
    stroke.source=ctx;stroke.owner=view;stroke.page=view.page;stroke.group=view.group;
    stroke.snap_beat=g_eng->frames_per_beat();stroke.snap_ready=overview.ready;
    stroke.precision=!view.sound&&view.page<=Page::Loop;stroke.pressed=osGetTime();
    if(kind==Gesture::Cut){
        stroke.direct_cut=boundary>=0;
        stroke.cut_target=boundary>=0?edges::cut_target(stroke.before.cuts,boundary,false,ctx.frames):
            stroke.precision?edges::cut_target(stroke.before.cuts,view.cut,view.cut_end,ctx.frames):view.cut;
        if(stroke.cut_target<0){stroke={};notice="fixed or ambiguous cut edge / drag refused";return;}
    }
    if(kind==Gesture::Edge||kind==Gesture::Move){C*u=current();if(u)stroke.edit=prepare(*u);}
    g_drag=Drag::PlaySliceSeam;
}
bool stroke_owner(){
    const auto&v=stroke.owner;
    return stroke.source.unit==ctx.unit&&stroke.source.pcm==ctx.pcm&&stroke.source.sample==ctx.sample&&stroke.source.generation==ctx.generation&&
        view.page==v.page&&view.selection_page==v.selection_page&&view.handle==v.handle&&view.cut==v.cut&&view.owner==v.owner&&
        view.fine==v.fine&&view.snap==v.snap&&view.span==stroke.span&&(stroke.kind==Gesture::Pan||view.first==stroke.first)&&
        view.scale==v.scale&&view.selected==v.selected&&view.sound==v.sound&&view.from_seq==v.from_seq&&
        view.move==v.move&&view.add==v.add&&view.live==v.live&&view.cut_end==v.cut_end&&
        (stroke.kind!=Gesture::Cut||!stroke.precision||stroke.direct_cut||stroke.cut_target==edges::cut_target(stroke.before.cuts,v.cut,v.cut_end,ctx.frames));
}
void nudge(C&u,int delta){if(!editable(u))return;view.snapped=false;command_begin();
    const bool primary_precision=!view.sound&&view.page<=Page::Loop;
    if(view.selection_page==Page::Cuts&&document(u).cuts.valid(view.cut,ctx.frames)){
        if(primary_precision&&view.owner!=Owner::Second){finish();return;}
        auto c=document(u).cuts;if(c.move(view.cut,int64_t(c.at[view.cut])+delta,ctx.frames))apply_cuts(u,c);}
    else if(view.selection_page!=Page::Cuts){auto e=prepare(u);
        if(primary_precision&&view.owner==Owner::Third){
            if(view.selection_page==Page::Loop){const int64_t d=std::clamp<int64_t>(delta,int64_t(e.start)-e.loop_start,int64_t(e.end)-e.loop_end);e.loop_start+=d;e.loop_end+=d;}
            else {const int64_t d=std::clamp<int64_t>(delta,-int64_t(e.start),int64_t(ctx.frames)-e.end);e.start+=d;e.end+=d;e.loop_start+=d;e.loop_end+=d;}
            set_edit(u,e);finish();return;
        }
        auto add=[&](uint32_t v){return uint32_t(clamp(int64_t(v)+delta,0,ctx.frames));};
        switch(view.handle){case Handle::In:e.start=std::min(add(e.start),e.end-2);break;case Handle::Out:e.end=std::max(add(e.end),e.start+2);break;
        case Handle::FadeIn:e.fade_in=add(e.fade_in);break;case Handle::FadeOut:e.fade_out=add(e.fade_out);break;
        case Handle::LoopIn:e.loop_start=std::min(add(e.loop_start),e.loop_end-2);break;case Handle::LoopOut:e.loop_end=std::max(add(e.loop_end),e.loop_start+2);break;
        case Handle::Crossfade:e.crossfade=add(e.crossfade);break;}set_edit(u,e);}
    finish();
}
void live_mark(C&u){if(!view.live||!audition_active(u)||!editable(u))return;
    auto c=document(u).cuts;const int id=c.insert(audition_position(u),ctx.frames);
    if(id<0){notice="mark refused / full or too close";return;}
    command_begin();apply_cuts(u,c);view.cut=id;view.cut_end=false;finish();notice="live mark / exact source frame";
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
void right(int x,int y,const char*s,u32 color){pw::text(x-ui::text_w(s),y,s,color);}
int fx(uint32_t at,uint32_t a,uint32_t b,int x,int w){if(b<=a)return x;return x+int(uint64_t(clamp(int64_t(at)-a,0,int(b-a)))*w/(b-a));}
void seam(C&u){auto e=prepare(u);if(e.loop_end<=e.loop_start+1)return;
    if(!preview_locked(u,e.loop_start,e.loop_end,true))return;
    const uint32_t margin=std::min<uint32_t>(3200,(e.loop_end-e.loop_start)/2);
    const uint32_t start=u.params[C::P_SPEED].effective<0?e.loop_start+margin:e.loop_end-1-margin;
    if(view.token)g_eng->seek_play_preview(view.token,start,true);
    notice="loop seam / repeat without musical loop write";
}
void primary_owner(){
    if(view.sound)return;
    view.move=view.owner==Owner::Third&&view.selection_page!=Page::Cuts;
    view.add=view.owner==Owner::Third&&view.selection_page==Page::Cuts;
    view.cut_end=false; // explicit choose/seam/add owners address the focused start
    if(view.selection_page!=Page::Cuts)view.handle=view.selection_page==Page::Loop?
        (view.owner==Owner::Second?Handle::LoopOut:Handle::LoopIn):(view.owner==Owner::Second?Handle::Out:Handle::In);
}
void close_menu(){finish();view.page=view.selection_page;primary_owner();notice=nullptr;}
void sheet(Page page){finish();if(page==Page::Copy)view.return_page=view.page;
    if(page==Page::Fades&&view.handle!=Handle::FadeIn&&view.handle!=Handle::FadeOut)view.handle=Handle::FadeIn;
    if(page==Page::Edit&&view.selection_page==Page::Trim&&view.handle!=Handle::In&&view.handle!=Handle::Out)view.handle=Handle::In;
    if(page==Page::Edit&&view.selection_page==Page::Loop&&view.handle<Handle::LoopIn)view.handle=Handle::LoopIn;
    view.page=page;notice=nullptr;}
void back_sheet(){finish();if(view.page==Page::Copy)view.page=view.return_page;else close_menu();}
void change_tool(C&u,Page page){finish();stop_locked();view.page=view.selection_page=page;view.sound=false;
    view.owner=page==Page::Cuts?Owner::Second:Owner::First;view.cut_end=false;view.add=view.live=view.move=false;notice=nullptr;
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
    if(which<0||which>2||!editable(u))return;
    stop_locked();command_begin();
    const auto next=which==0?C::PitchMode::Normal:which==1?C::PitchMode::Stretch:C::PitchMode::Legacy;
    if(next!=u.pitch_mode()){if(!u.modern()&&next!=C::PitchMode::Legacy)u.set_modern(true);u.set_pitch_mode(next);restart_note_owner();}
    view.selected=next==C::PitchMode::Legacy?C::P_SPEED:C::P_NOTE;
    command_end(u);view.add=view.live=false;
}
void queue(Action action,int argument,layout::Box box,int x,int y){begin(Gesture::Menu,x,y);stroke.action=action;stroke.argument=argument;stroke.box=box;stroke.page=view.page;stroke.group=view.group;}
void choose_cut(C&u,int delta){const auto cuts=document(u).cuts;int ids[16],n=0;
    for(int i=0;i<16;++i)if(cuts.valid(i,ctx.frames))ids[n++]=i;
    std::sort(ids,ids+n,[&](int a,int b){return cuts.at[a]<cuts.at[b];});int at=0;
    for(int i=0;i<n;++i)if(ids[i]==view.cut)at=i;
    if(n)view.cut=ids[clamp(at+delta,0,n-1)];
    view.cut_end=false;view.selected=C::P_SLICE;
}
void propose(){proposed=view.auto_count<0?pe::Cuts{}:view.auto_count?pe::Cuts::equal(view.auto_count,ctx.frames):attacks.cuts(ctx.frames,view.sensitivity);}
void export_copy(){
    C*owner=nullptr;PlayExportOptions options;Copy copy;
    {platform::AudioOut::Guard guard(*g_audio);owner=current();
        if(!owner||!valid_context(*owner)||!same(document(*owner),expected)||!editable(*owner)||
           view.page!=Page::Copy||param_lens_is_open())return;
        copy=view.copy;selection(*owner,options.first,options.end);
        options.crop=copy==Copy::Crop;options.detach=copy==Copy::Detach;
        options.normalize=view.normalize;options.dc=view.dc;options.bake=view.bake;options.reverse=view.reverse;
        if(options.crop&&view.selection_page!=Page::Trim)return;
        stop_locked();}
    // Blocking filesystem/PCM work is deliberately outside AudioOut::Guard.
    const char*result=play_export_copy(*owner,options);
    platform::AudioOut::Guard guard(*g_audio);if(current()!=owner)return;
    identify(*owner);
    notice=result;
}
void action(Action action,int argument){
    bool browser=false;
    {platform::AudioOut::Guard guard(*g_audio);C*u=current();if(!u||!valid_context(*u)||!same(document(*u),expected)||param_lens_is_open())return;
        switch(action){
        case Action::Source:if(!g_eng->play_editor_busy()){stop_locked();browser=true;}break;
        case Action::Tool:change_tool(*u,Page(argument));break;
        case Action::Sheet:if(Page(argument)==Page::Tools)words_tools();else {words_open(Page(argument));if(Page(argument)==Page::Config)view.group=Group::File;}break;
        case Action::Listen:if(view.live||audition_active(*u)){view.live=false;stop_locked();}else listen(*u);break;
        case Action::Surface:words_root();stop_locked();view.sound=argument==0;view.page=view.selection_page;
            if(!view.sound){view.live=false;primary_owner();}break;
        case Action::Owner:if(!g_eng->play_editor_busy()){
            view.owner=Owner(argument);view.live=false;primary_owner();
            view.selected=C::P_SLICE;view.snapped=false;
            notice=view.add?"press strip / lift inside to add / leave to cancel":nullptr;
        }break;
        case Action::Fine:if(editable(*u))view.fine=!view.fine;break;
        case Action::Zoom:zoom(*u,argument,argument==0);break;
        case Action::Sequence:stop_locked();view.from_seq=false;sequencer_open_slices(ctx.chain);break;
        case Action::Word:break;
        case Action::None:break;}
    }
    if(action==Action::Word)words_activate(argument&255,argument>>8);
    if(browser){C*u=current();if(u)module_focus_touch_common_header(*u,155,10);}
}
int workspace(const Frame&f){return f.d.pitch_mode==C::PitchMode::Normal?0:f.d.pitch_mode==C::PitchMode::Stretch?1:2;}
const char* workspace_name(const Frame&f){static const char*names[]={"normal","stretch","tape"};return names[workspace(f)];}
void sample_value(char*t,size_t n,uint32_t value){std::snprintf(t,n,"%u smp",unsigned(value));}
uint32_t handle_value(const PlayEditState&e,Handle h){switch(h){case Handle::In:return e.start;case Handle::Out:return e.end;case Handle::FadeIn:return e.fade_in;case Handle::FadeOut:return e.fade_out;case Handle::LoopIn:return e.loop_start;case Handle::LoopOut:return e.loop_end;case Handle::Crossfade:return e.crossfade;}return 0;}
// Drawing consumes the bounded frozen frame; no alternate legacy PLAY surface.
// Lower-only source boundary staff: two actual shared hollow lancet heads,
// not an edit/live slider pair. The upper wave keeps its existing renderer.
struct PrecisionStaff {int top,bottom;bool compact;};
PrecisionStaff precision_staff(bool overlap,int side){
    return overlap?PrecisionStaff{side?84:61,side?107:84,true}:PrecisionStaff{61,108,false};
}
void precision_grip(int x,PrecisionStaff g,u32 color){
    const int radius=g.compact?3:5;
    pw::line(x,g.top+2*radius,x,g.bottom-2*radius,color);
    module_fader::detail::diamond(module_fader::detail::PainterPen<sigil_surface::Painter>{pw::pen,x,g.top+radius,false},0,false,color,g.compact);
    module_fader::detail::diamond(module_fader::detail::PainterPen<sigil_surface::Painter>{pw::pen,x,g.bottom-radius,false},0,false,color,g.compact);
}
bool precision_head(int x,int y,int center,PrecisionStaff g){
    const int radius=g.compact?3:5,dx=x>center?x-center:center-x;
    for(int cy:{g.top+radius,g.bottom-radius}){
        const int row=y-cy;
        if(row>=-radius&&row<=radius&&dx<=module_fader::detail::half_width(row,g.compact))return true;
    }
    return false;
}
void precision_strip(){
    const auto&v=frame.v;const bool enabled=frame.loaded&&!frame.busy&&!frame.missing&&!frame.mismatch;
    if(!frame.loaded||!v.span){pw::line(14,84,306,84,m::ghost());pw::text(14,68,"no source",m::ghost());return;}
    const uint32_t first=v.first,last=first+v.span;
    const bool cut=v.selection_page==Page::Cuts;
    const auto&c=frame.d.cuts;
    const bool selected=!cut||c.valid(v.cut,frame.frames)||frame.merging;
    const auto h=edges::handles(frame.start,frame.end,first,v.span);
    const auto under_head=[&](int x,int y){return selected&&
        ((h.has_in&&precision_head(x,y,h.a,precision_staff(h.overlap,0)))||
         (h.has_out&&precision_head(x,y,h.b,precision_staff(h.overlap,1))));};
    const auto axis=[&](int a,int b,int y,u32 color){int run=a;
        for(int x=a;x<=b;++x)if(under_head(x,y)){if(run<x)pw::rect(run,y,x-run,1,color);run=x+1;}
        if(run<=b)pw::rect(run,y,b-run+1,1,color);};
    axis(14,306,84,m::branch());
    if(selected&&std::max(frame.start,first)<std::min(frame.end,last))axis(h.a,h.b,84,enabled?m::spot():m::ghost());
    if(cut)for(int id=0;id<16;++id)if(c.valid(id,frame.frames)){
        const auto a=c.at[id],b=c.end(id,frame.frames);if(a>=last||b<=first)continue;
        const int x0=edges::project(a,first,v.span),x1=edges::project(b,first,v.span);
        char t[24];std::snprintf(t,sizeof t,"%02d",id+1);
        if(c.pitch_semitones[id]||(c.reverse&(1u<<id))){char badge[24],pitch[8]={};
            if(c.pitch_semitones[id])std::snprintf(pitch,sizeof pitch,"%+d",int(c.pitch_semitones[id]));
            std::snprintf(badge,sizeof badge,"%02d%s%s",id+1,pitch,c.reverse&(1u<<id)?"r":"");
            if(ui::text_w(badge)+8<=x1-x0)std::snprintf(t,sizeof t,"%s",badge);
        }
        const int tx=(x0+x1-ui::text_w(t))/2;
        // Never squeeze badges or a selected id into neighbouring parts/heads.
        if(ui::text_w(t)+8<=x1-x0&&!(frame.merging&&id==frame.merge_removed)){
            bool collision=false;for(int x=tx-2;x<tx+ui::text_w(t)+2;++x)for(int y=66;y<78;++y)collision|=under_head(x,y);
            if(!collision)pw::text(tx,68,t,id==v.cut?m::spot():m::ghost());}
        if(a>=first&&a<=last){const int x=edges::project(a,first,v.span);
            if(!(frame.merging&&id==frame.merge_removed))for(int y=80;y<101;++y)if(!under_head(x,y))pw::rect(x,y,1,1,id==frame.dragging_cut?m::spot():m::branch());}
    }
    if(frame.inserting&&frame.candidate>=first&&frame.candidate<=last){const int x=edges::project(frame.candidate,first,v.span);
        for(int y=80;y<106;++y)if(!under_head(x,y))pw::rect(x,y,1,1,m::spot());}
    for(int side=0;selected&&side<2;++side)if(side?h.has_out:h.has_in)
        precision_grip(side?h.b:h.a,precision_staff(h.overlap,side),!enabled?m::ghost():frame.dragging_side==side?m::spot():m::ink());
    if(frame.merging){char t[40];std::snprintf(t,sizeof t,"join %02d + %02d",frame.merge_left+1,frame.merge_removed+1);
        const int tx=clamp((h.a+h.b-ui::text_w(t))/2,14,306-ui::text_w(t));bool collision=false;
        for(int x=tx-2;x<tx+ui::text_w(t)+2;++x)for(int y=92;y<104;++y)collision|=under_head(x,y);
        if(!collision)pw::text(tx,94,t,m::spot());}
    else if(!cut){char a[24],b[24];if(v.fine){sample_value(a,sizeof a,frame.start);sample_value(b,sizeof b,frame.end);}else{seconds(a,sizeof a,frame.start);seconds(b,sizeof b,frame.end);}
        if(h.b-h.a>ui::text_w(a)+ui::text_w(b)+24){pw::text(h.a+8,68,a,m::ghost());right(h.b-8,94,b,m::ghost());}}
}
void precision_bottom(){
    const auto&v=frame.v;const bool editable=frame.loaded&&!frame.busy&&!frame.missing&&!frame.mismatch;
    char t[64];std::snprintf(t,sizeof t,"%.32s >",frame.loaded?frame.name:"no source");
    pw::header(76,220,t,m::ink());right(306,m::kHeaderTextY,"files >",m::ghost());
    const char*names[]={"trim","chop","loop"};for(int i=0;i<3;++i){const auto b=pl::tool(i);
        pw::text(b.x0,40,names[i],i==int(v.selection_page)?m::spot():m::ink());
        if(i==int(v.selection_page))pw::line(b.x0,52,b.x0+ui::text_w(names[i])-1,52,m::spot());}
    precision_strip();
    pw::text(14,113,v.fine?"fine on":"fine off",editable?m::ink():m::ghost());
    const auto zoom_ink=frame.loaded?m::ink():m::ghost();
    pw::text(pl::zoom_out.x0+8,113,"-",zoom_ink);std::snprintf(t,sizeof t,"%dx",v.scale);
    pw::text(pl::zoom_fit.x0+2,113,t,zoom_ink);pw::text(pl::zoom_in.x0+8,113,"+",zoom_ink);
    inline_draw_controls();right(m::right_edge(320),m::kTextY,"seq >",m::ghost());
}
void precision_top(const Frame&f=frame){
    const Frame&frame=f;const auto&v=frame.v;char t[80];
    pw::header(76,246,frame.loaded?frame.name:"no source",m::ink());
    std::snprintf(t,sizeof t,"ch%02d / %02d",frame.chain+1,frame.slot+1);right(m::right_edge(400),m::kHeaderTextY,t,m::ghost());
    const char*names[]={"trim","chop","loop"};
    if(v.scale==1)std::snprintf(t,sizeof t,"%s / overview",names[int(v.selection_page)]);
    else std::snprintf(t,sizeof t,"%s / edge zoom %dx",names[int(v.selection_page)],v.scale);
    pw::text(14,38,t,m::ghost());right(386,38,frame.busy?"protected":frame.mismatch?"source changed":!frame.loaded?"empty":frame.channels==2?"stereo / 32 khz":"mono / 32 khz",m::ghost());
    const char*warning=frame.busy?"recording / protected":frame.missing?"missing source":frame.mismatch?"source changed / reload":!frame.loaded?"choose a source below":!frame.ready?"analyzing source...":nullptr;
    if(warning)pw::text(14,114,warning,m::ghost());
    else {
        const uint32_t first=v.first,last=first+v.span;const auto at=[&](uint32_t p){return fx(p,first,last,14,372);};
        const bool cuts=v.selection_page==Page::Cuts,selected_cut=!cuts||frame.d.cuts.valid(v.cut,frame.frames);
        if(cuts)for(int id=0;id<16;++id)if(frame.d.cuts.valid(id,frame.frames)){
            const uint32_t a=frame.d.cuts.at[id],b=frame.d.cuts.end(id,frame.frames);if(a>=last||b<=first)continue;
            const int x0=at(a),x1=at(b);std::snprintf(t,sizeof t,"%02d",id+1);
            if(frame.d.cuts.pitch_semitones[id]||(frame.d.cuts.reverse&(1u<<id))){char pitch[24];part_value(pitch,sizeof pitch,frame.d.cuts.pitch_semitones[id]);
                char badge[48];std::snprintf(badge,sizeof badge,"%02d %s%s",id+1,pitch,frame.d.cuts.reverse&(1u<<id)?" r":"");
                if(ui::text_w(badge)+8<=x1-x0)std::snprintf(t,sizeof t,"%s",badge);}
            if(ui::text_w(t)+8<=x1-x0&&!(frame.merging&&id==frame.merge_removed))pw::text((x0+x1-ui::text_w(t))/2,52,t,id==v.cut?m::spot():m::ghost());
            if(a>=first&&a<=last&&!(frame.merging&&id==frame.merge_removed)){const int x=std::min(385,at(a));for(int y=67;y<187;y+=4)pw::rect(x,y,1,2,m::branch());}
        }
        if(selected_cut&&std::max(frame.start,first)<std::min(frame.end,last))pw::rect(at(frame.start),66,std::max(1,at(frame.end)-at(frame.start)),122,(m::spot()&0x00ffffffu)|(20u<<24));
        for(int i=0;i<N;++i){const uint32_t begin=first+uint32_t(uint64_t(i)*v.span/N),end=first+uint32_t(uint64_t(i+1)*v.span/N);
            const bool selected=selected_cut&&end>frame.start&&begin<frame.end;
            const int y0=127-int(frame.high[i])*59/128,y1=127-int(frame.low[i])*59/128;
            pw::rect(14+i,y0,1,std::max(1,y1-y0+1),selected?m::spot():m::ink());}
        // Independent real endpoints: an offscreen boundary is never projected to an edge grip.
        for(int side=0;selected_cut&&side<2;++side){const uint32_t p=side?frame.end:frame.start;if(p<first||p>last)continue;
            const int x=std::min(385,at(p)),tip=clamp(x+(side?-4:4),14,385);
            pw::line(x,66,x,188,m::spot());pw::line(x,66,tip,66,m::spot());pw::line(x,188,tip,188,m::spot());}
        if(frame.inserting&&frame.candidate>=first&&frame.candidate<=last){const int x=std::min(385,at(frame.candidate));for(int y=68;y<187;y+=4)pw::rect(x,y,1,2,m::spot());}
        if(frame.playing&&frame.position>=first&&frame.position<last){const int x=std::min(385,at(frame.position));for(int y=68;y<187;y+=4)pw::rect(x,y,1,2,m::spot());}
        for(int i=0;i<=4;++i){const int x=14+i*371/4;seconds(t,sizeof t,first+uint32_t(uint64_t(i)*v.span/4));pw::line(x,196,x,202,m::branch());pw::text(clamp(x-ui::text_w(t)/2,14,386-ui::text_w(t)),207,t,m::ghost());}
    }
    char a[32],b[48];seconds(a,sizeof a,frame.end-frame.start);
    if(frame.merging)std::snprintf(t,sizeof t,"join %02d + %02d",frame.merge_left+1,frame.merge_removed+1);
    else if(v.selection_page==Page::Cuts&&frame.d.cuts.valid(v.cut,frame.frames)){char pitch[24];part_value(pitch,sizeof pitch,frame.d.cuts.pitch_semitones[v.cut]);
        std::snprintf(t,sizeof t,"%02d %s%s / %s",v.cut+1,pitch,frame.d.cuts.reverse&(1u<<v.cut)?" r":"",a);}
    else std::snprintf(t,sizeof t,"length %s",a);
    std::snprintf(b,sizeof b,"%dx / %s",v.scale,frame.playing?"playing":"stopped");
    m::footer(400,!frame.merging&&frame.notice[0]?frame.notice:t,b);
}
#include "play_word_menu_impl.inc"
#include "play_inline_impl.inc"
} // namespace
void play_editor_capture(){
    frame={};if(!context()){words_cancel(true);if(g_eng&&g_audio){platform::AudioOut::Guard guard(*g_audio);stop_locked();}ctx={};stroke={};
        if(g_overlay!=Overlay::SampleBrowser)view.from_seq=false;
        return;}
    {platform::AudioOut::Guard guard(*g_audio);C*u=current();if(!u||!identify(*u)){stop_locked();stroke={};
        if(view.from_seq){g_view=View::Sequencer;g_overlay=Overlay::None;g_drag=Drag::None;view.from_seq=false;}
        return;}}
    analyze();
    {platform::AudioOut::Guard guard(*g_audio);C*u=current();if(!u||!valid_context(*u)){stop_locked();finish();return;}
        if(param_lens_is_open()||g_eng->play_editor_busy()||u->bypass||g_eng->chains[ctx.chain].mute||
           (stroke.kind!=Gesture::None&&(!stroke_owner()||g_drag!=Drag::PlaySliceSeam))){stop_locked();finish();}
        latch_boundary_hold();
        frame.inserting=stroke.kind==Gesture::Insert&&!stroke.moved;frame.candidate=stroke.candidate;
        frame.active=true;frame.loaded=ctx.frames>=2&&(ctx.channels==1||ctx.channels==2);frame.v=view;frame.d=document(*u);frame.proposed=proposed;
        frame.chain=ctx.chain;frame.slot=ctx.slot;frame.sample=ctx.sample;frame.channels=ctx.channels;frame.frames=ctx.frames;
        frame.busy=g_eng->play_editor_busy();frame.audition=audition_active(*u);frame.playing=frame.audition||((u->visual_state()&C::VisualPlaying)&&!u->bypass&&!g_eng->chains[ctx.chain].mute);
        if(frame.busy){overview.reset(0,ctx.frames);detail.reset(view.first,view.first+view.span);attacks.reset();}
        frame.position=stroke.audio_token&&g_eng->play_preview_active(stroke.audio_token)?g_eng->play_preview_position(stroke.audio_token):audition_position(*u);selection(*u,frame.start,frame.end);
        if(stroke.audio_token&&g_eng->play_preview_active(stroke.audio_token))frame.playing=true;
        if(stroke.merge_preview){auto c=frame.d.cuts;const int left=c.merge(stroke.cut_target,ctx.frames);
            if(left>=0){frame.merging=true;frame.merge_left=left;frame.merge_removed=stroke.cut_target;frame.start=c.at[left];frame.end=c.end(left,ctx.frames);}}
        frame.quiet=u->bypass||g_eng->chains[ctx.chain].mute;
        for(int p=0;p<C::P_COUNT;++p){frame.live[p]=p==C::P_NOTE?u->params[p].target:u->params[p].effective;
            if(u->params[p].src!=ModSource::None&&u->params[p].mod_enabled)frame.links|=1u<<p;}
        frame.mismatch=!u->edit_source_matches();if(!frame.mismatch)frame.d.edit=prepare(*u);
        const auto&p=Bank::instance().provenance(ctx.sample);frame.missing=p.missing;const char*name=std::strrchr(p.path,'/');name=name?name+1:p.path;
        std::snprintf(frame.name,sizeof frame.name,"%.32s",*name?name:p.missing?"missing source":"sample");
        // Capture actual latched edge ownership under the audio guard. A
        // selected owner, pan/move/add, or cancelled stroke is not dragging.
        if(stroke.precision&&!frame.v.sound&&frame.v.page<=Page::Loop&&same(document(*u),expected)&&stroke_owner()&&
           !param_lens_is_open()&&g_drag==Drag::PlaySliceSeam&&!frame.busy&&!frame.missing&&!frame.mismatch){
            if(stroke.kind==Gesture::Edge){
                if(stroke.owner.handle==Handle::In||stroke.owner.handle==Handle::LoopIn)frame.dragging_side=0;
                else if(stroke.owner.handle==Handle::Out||stroke.owner.handle==Handle::LoopOut)frame.dragging_side=1;
            }else if(stroke.kind==Gesture::Cut){frame.dragging_cut=int8_t(stroke.cut_target);
                if(frame.d.cuts.valid(frame.v.cut,frame.frames)){
                    if(stroke.cut_target==frame.v.cut)frame.dragging_side=0;
                    else if(frame.d.cuts.at[stroke.cut_target]==frame.d.cuts.end(frame.v.cut,frame.frames))frame.dragging_side=1;
                }
            }
        }
        if(notice)std::snprintf(frame.notice,sizeof frame.notice,"%.57s",notice);
    }
    frame.ready=detail.ready;frame.overview_ready=overview.ready;
    int peak=0;if(overview.ready)for(int i=0;i<OV;++i)peak=std::max(peak,std::max(-int(overview.low[i]),int(overview.high[i])));
    frame.gain=uint16_t(clamp(peak?int(24576u*256u/unsigned(peak)):256,256,16384));
    if(frame.ready)detail.freeze(frame.low,frame.high,frame.gain);
    words_capture(); // motion and blur command stream only after the audio guard
}
int play_editor_selected(){return view.selected;}
void play_editor_select(int p){if(p>=0&&p<C::P_COUNT)view.selected=p;}
void play_editor_release(bool touch_up){if(words_release(touch_up))return;
    if(stroke.kind==Gesture::None){if(!touch_up&&g_eng&&g_audio){platform::AudioOut::Guard guard(*g_audio);stop_locked();}if(touch_up)words.quarantine=false;return;}
    Action pending=Action::None;int argument=0;
    if(g_eng&&g_audio){platform::AudioOut::Guard guard(*g_audio);C*u=current();
        const bool valid=u&&valid_context(*u)&&same(document(*u),expected)&&stroke_owner()&&!param_lens_is_open()&&g_drag==Drag::PlaySliceSeam&&!g_eng->play_editor_busy()&&
            (stroke.kind==Gesture::Menu||stroke.kind==Gesture::Choose||(!u->bypass&&!g_eng->chains[ctx.chain].mute));
        if(valid&&touch_up){
            if(stroke.kind==Gesture::Cut&&stroke.merge_preview&&stroke.held&&editable(*u)){
                auto c=document(*u).cuts;const int left=c.merge(stroke.cut_target,ctx.frames);
                if(left>=0){apply_cuts(*u,c);view.cut=left;view.cut_end=false;notice="joined / left id kept / removed id invalid";}
            }else if(!stroke.moved){
                if(stroke.kind==Gesture::Menu){pending=stroke.action;argument=stroke.argument;}
                else if(stroke.kind==Gesture::Insert&&editable(*u)){
                    auto c=stroke.before.cuts;const int id=c.insert(stroke.candidate,ctx.frames);
                    if(id>=0){apply_cuts(*u,c);view.cut=id;view.cut_end=false;view.selected=C::P_SLICE;notice="split / stable shared id";}
                    else notice="split refused / full or too close";
                }else if(stroke.kind==Gesture::Choose){
                    const int id=stroke.before.cuts.select(stroke.candidate,ctx.frames);
                    if(id>=0){view.cut=id;view.cut_end=false;view.selected=C::P_SLICE;notice=nullptr;}
                }
            }
        }
        // A normal lift stops only stroke-owned audio. Cancellation/context loss
        // stops both owners, never restoring document bytes into a new source.
        if(!valid||!touch_up)stop_locked();
        if(u&&valid_context(*u)&&!same(document(*u),expected)){stop_locked();reset_document(document(*u));}
        finish();
    }else stroke={};
    if(pending!=Action::None)action(pending,argument);
    if(touch_up)words.quarantine=false;
}
bool play_editor_back(){if(!context())return false;
    if(words_modal()||(words.depth&&words_surface())){words_activate(0,0);return true;}
    platform::AudioOut::Guard guard(*g_audio);stop_locked();finish();
    if(view.page>Page::Loop){back_sheet();return true;}
    if(view.from_seq){g_view=View::Sequencer;g_overlay=Overlay::None;view.from_seq=false;ctx={};return true;}return false;}
void play_editor_open_cuts(int chain,int slot,bool from_sequencer){
    if(!g_eng||!g_audio||chain<0||chain>=NUM_CHAINS||slot<0||slot>=UNITS_PER_CHAIN)return;
    platform::AudioOut::Guard guard(*g_audio);Unit*u=g_eng->chains[chain].units[slot];if(!u||u->kind!=UnitKind::SamplePlayer)return;
    stop_locked();finish();g_edit_chain=g_sel_chain=chain;g_edit_slot=g_sel_slot=slot;g_view=View::Chains;g_overlay=Overlay::Editor;g_drag=Drag::None;
    g_seq_lock_armed=false;view.from_seq=from_sequencer;
    if(identify(*static_cast<C*>(u))){view.page=view.selection_page=Page::Cuts;view.live=view.add=false;view.selected=C::P_SLICE;auto c=document(*static_cast<C*>(u)).cuts;
        view.sound=false;view.owner=Owner::Second;view.cut_end=false;view.move=false;
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
bool play_editor_button(unsigned down){if(!(down&KEY_A)||!context()||param_lens_is_open()||words_modal()||words.quarantine)return false;
    platform::AudioOut::Guard guard(*g_audio);C*u=current();if(!u||!identify(*u))return false;finish();
    if(view.live)live_mark(*u);else if(audition_active(*u))stop_locked();else listen(*u);return true;}
bool play_editor_touch(C&u,int x,int y){if(!context()||param_lens_is_open())return false;
    if(words_touch(u,x,y))return true;
    if(stroke.kind!=Gesture::None)return true;
    platform::AudioOut::Guard guard(*g_audio);if(!identify(u))return false;finish();
    auto hit=[&](layout::Box box,Action a,int arg=0){if(!box.contains(x,y))return false;queue(a,arg,box,x,y);return true;};
    if(hit(pl::source,Action::Source)||hit(pl::files,Action::Sheet,int(Page::Config)))return true;
    for(int i=0;i<3;++i)if(pl::tool(i).contains(x,y)){
        queue(i==int(view.selection_page)?Action::Sheet:Action::Tool,i==int(view.selection_page)?int(Page::Tools):i,pl::tool(i),x,y);return true;}
    if(hit(pl::fine,Action::Fine)||hit(pl::zoom_out,Action::Zoom,-1)||hit(pl::zoom_fit,Action::Zoom,0)||hit(pl::zoom_in,Action::Zoom,1))return true;
    if(hit(pl::footer(2),Action::Sequence))return true;
    if(inline_touch_controls(u,x,y))return true;
    const bool cut=view.selection_page==Page::Cuts;
    if(cut&&pl::labels.contains(x,y)){
        if(!ctx.frames)return true;
        begin(Gesture::Choose,x,y);stroke.box=pl::labels;stroke.candidate=std::min(ctx.frames-1,precise::strip_frame(x,view.first,view.span));return true;
    }
    if(!pl::strip.contains(x,y)&&!(pl::labels.contains(x,y)&&!cut))return true;
    uint32_t a,b;selection(u,a,b);const auto c=document(u).cuts;
    const bool valid=!cut||c.valid(view.cut,ctx.frames);const auto grips=edges::handles(a,b,view.first,view.span,cut);
    const int side=valid?grips.hit(x,y):-1;
    if(cut){
        if(!editable(u))return true;
        // Selected close endpoints have explicit split lanes. Other chronological
        // boundaries use the nearest visible 24px padded tick; duplicate imports refuse.
        int target=side>=0?edges::cut_target(c,view.cut,side!=0,ctx.frames):-1;
        if(side>=0&&target<0){notice="fixed or ambiguous source boundary";return true;}
        if(side<0){int nearest=13;for(int id=0;id<16;++id)if(c.valid(id,ctx.frames)&&c.at[id]>=view.first&&c.at[id]<=view.first+view.span){
            const int distance=std::abs(x-edges::project(c.at[id],view.first,view.span));
            if(distance<nearest){nearest=distance;target=id;}}
            if(target>=0&&edges::cut_target(c,target,false,ctx.frames)<0){notice="fixed or ambiguous source boundary";return true;}}
        view.owner=Owner::Second;view.move=view.add=view.live=false;view.snapped=false;
        if(target>=0){view.cut_end=c.valid(view.cut,ctx.frames)&&c.at[target]==c.end(view.cut,ctx.frames);view.selected=C::P_SLICE;begin(Gesture::Cut,x,y,target);}
        else {begin(Gesture::Insert,x,y);stroke.box=pl::strip;stroke.candidate=snap(std::min(ctx.frames-1,precise::strip_frame(x,view.first,view.span)));}
    }else if(side>=0){
        if(!editable(u))return true;
        view.owner=Owner(side);view.move=view.add=view.live=false;view.snapped=false;view.cut_end=false;
        view.handle=view.selection_page==Page::Loop?(side?Handle::LoopOut:Handle::LoopIn):(side?Handle::Out:Handle::In);
        view.selected=side?C::P_LEN:C::P_START;begin(Gesture::Edge,x,y);
    }else if(valid&&x>grips.a&&x<grips.b){
        if(!editable(u))return true;
        view.owner=Owner::Third;view.move=true;view.add=view.live=false;view.selected=C::P_START;begin(Gesture::Move,x,y);
    }else if(view.scale>1&&view.span<ctx.frames){view.move=false;begin(Gesture::Pan,x,y);}
    return true;
}
void play_editor_drag(C&u,int x,int y){if(stroke.kind==Gesture::None)return;
    if(!context()||g_drag==Drag::None||param_lens_is_open()){play_editor_release(false);return;}
    platform::AudioOut::Guard guard(*g_audio);if(!valid_context(u)||!same(document(u),expected)||!stroke_owner()||g_eng->play_editor_busy()){
        stop_locked();if(valid_context(u)&&!same(document(u),expected))reset_document(document(u));else finish();
        notice="gesture cancelled / context changed";return;}
    const int delta=x-stroke.x;
    if(stroke.kind==Gesture::Menu){
        if(std::abs(y-stroke.y)>=8||std::abs(delta)>=8||!stroke.box.contains(x,y))stroke.moved=true;
        return;}
    if(stroke.kind==Gesture::Insert||stroke.kind==Gesture::Choose){
        if(!stroke.box.contains(x,y)||std::abs(delta)>=8||std::abs(y-stroke.y)>=8)stroke.moved=true;
        if(stroke.moved&&stroke.owner.scale>1&&std::abs(delta)>=8)stroke.kind=Gesture::Pan;
        else return;
    }
    if(stroke.kind==Gesture::Pan){const int64_t d=int64_t(delta)*stroke.span/pl::width;
        view.first=uint32_t(clamp(int64_t(stroke.first)-d,0,ctx.frames-stroke.span));return;}
    if(!editable(u)||u.bypass||g_eng->chains[ctx.chain].mute){stop_locked();finish();return;}
    if(stroke.kind==Gesture::Cut&&stroke.precision){
        latch_boundary_hold();
        stroke.excursion=std::max(stroke.excursion,std::max(std::abs(delta),std::abs(y-stroke.y)));
        if(!stroke.held&&stroke.excursion>kHoldMotion)stroke.hold_possible=false;
        const bool was_merge=stroke.merge_preview;
        stroke.merge_preview=stroke.held&&(y<pl::strip.y0-kMergeExit||y>=pl::strip.y1+kMergeExit);
        if(was_merge&&!stroke.merge_preview)stroke_audio(u); // back-in also restores the audible range
        if(stroke.merge_preview){stroke.moved=true;stroke_audio(u);return;}
        if(stroke.held&&!pl::strip.contains(x,y))return;
    }
    if(std::abs(delta)<kDragThreshold&&!stroke.changed)return;
    stroke.moved=true;
    stroke_audio(u);
    struct AudioRefresh {C&unit;~AudioRefresh(){stroke_audio(unit);}} refresh{u};
    const int64_t movement=stroke.precision?precise::displacement(x,stroke.x,stroke.span,stroke.owner.fine):
        int64_t(delta)*stroke.span/(138*(stroke.owner.fine?8:1));
    if(stroke.kind==Gesture::PartPitch){auto cuts=document(u).cuts;if(!cuts.valid(view.cut,ctx.frames))return;
        const int steps=delta/(view.fine?16:4);if(!steps&&!stroke.changed)return;
        cuts.pitch_semitones[view.cut]=int8_t(clamp(int(stroke.before.cuts.pitch_semitones[view.cut])+steps,part_pitch::kMin,part_pitch::kMax));
        apply_cuts(u,cuts);return;}
    if(stroke.kind==Gesture::TapeSpeed){const int origin=stroke.before.bases[C::P_SPEED];
        const int magnitude=clamp(std::abs(origin)+delta*(view.fine?8:256),0,32767);
        const q15 next=delta?q15(origin<0?-magnitude:magnitude):q15(origin);
        if(u.params[C::P_SPEED].base!=next){u.params[C::P_SPEED].base=next;changed(u);}return;}
    if(stroke.kind==Gesture::CutSpeed){auto cuts=document(u).cuts;if(!cuts.valid(view.cut,ctx.frames))return;
        const int rate=clamp(int(stroke.before.cuts.speed_q8[view.cut])+delta*(view.fine?1:4),64,1024);
        cuts.speed_q8[view.cut]=uint16_t(rate);apply_cuts(u,cuts);
        if(preview_cut==view.cut&&view.token)g_eng->set_play_preview_speed(view.token,uint16_t(rate));
        return;}
    if(stroke.kind==Gesture::Note){const int step=delta/(view.fine?16:4);
        const q15 next=step?musical_note::encode(musical_note::decode(stroke.before.bases[C::P_NOTE])+step):stroke.before.bases[C::P_NOTE];
        if(u.params[C::P_NOTE].base!=next){u.params[C::P_NOTE].base=next;changed(u);}return;}
    if(stroke.kind==Gesture::Tempo){if(!delta&&!stroke.changed)return;const int origin=stroke.before.bases[C::P_SPEED];
        const int magnitude=clamp(std::abs(origin)+delta*(view.fine?8:128),2048,32767);
        const q15 next=delta?q15(origin<0?-magnitude:magnitude):q15(origin);
        if(u.params[C::P_SPEED].base!=next){u.params[C::P_SPEED].base=next;changed(u);}return;}
    if(stroke.kind==Gesture::Gate&&view.page==Page::Auto){view.sensitivity=clamp(stroke.owner.sensitivity+delta*(view.fine?1:2),0,100);view.auto_count=0;propose();return;}
    if(stroke.kind==Gesture::Speed||stroke.kind==Gesture::Gain||stroke.kind==Gesture::Gate){int p=stroke.kind==Gesture::Speed?C::P_SPEED:stroke.kind==Gesture::Gain?C::P_GAIN:C::P_GATE;
        q15 next;if(p==C::P_GATE){int steps=delta>=18?(delta-18)/40+1:delta<=-18?-((-delta-18)/40+1):0;next=steps?play_mode::gate_value(clamp(play_mode::gate_index(stroke.before.bases[p])+steps,0,5)):stroke.before.bases[p];}
        else next=q15(clamp(int64_t(stroke.before.bases[p])+int64_t(delta)*(p==C::P_SPEED?65534:32767)/(128*(stroke.owner.fine?8:1)),p==C::P_SPEED?-32767:0,32767));
        if(u.params[p].base!=next){u.params[p].base=next;changed(u);}return;}
    if(!movement&&!stroke.changed)return;
    if(stroke.kind==Gesture::Cut){auto c=stroke.before.cuts;const int id=stroke.cut_target;
        if(id<0||id>=pe::CUTS)return;
        const uint32_t origin=c.at[id];
        const uint32_t target=movement?snap(uint32_t(clamp(int64_t(origin)+movement,0,ctx.frames))):origin;
        c.move(id,target,ctx.frames);apply_cuts(u,c);if(!movement)view.snapped=false;return;
    }
    // Out-and-back restores the original document bytes, including a previously disabled exact window.
    if(!movement){
        if(!same_edit(u.edit_state(),stroke.before.edit)&&u.set_edit_state(stroke.before.edit,ctx.generation))changed(u);
        view.snapped=false;return;
    }
    auto e=stroke.edit;
    auto shift=[&](uint32_t v){return uint32_t(clamp(int64_t(v)+movement,0,ctx.frames));};
    if(stroke.kind==Gesture::Move){
        if(stroke.owner.selection_page==Page::Loop){const int64_t d=std::clamp<int64_t>(movement,int64_t(e.start)-e.loop_start,int64_t(e.end)-e.loop_end);e.loop_start+=d;e.loop_end+=d;}
        else {const int64_t d=std::max(-int64_t(e.start),std::min<int64_t>(ctx.frames-e.end,movement));e.start+=d;e.end+=d;e.loop_start+=d;e.loop_end+=d;}
    }
    else switch(stroke.owner.handle){case Handle::In:e.start=std::min(snap(shift(e.start)),e.end-2);break;case Handle::Out:e.end=std::max(snap(shift(e.end)),e.start+2);break;
        case Handle::FadeIn:e.fade_in=shift(e.fade_in);break;case Handle::FadeOut:e.fade_out=uint32_t(clamp(int64_t(e.fade_out)-movement,0,ctx.frames));break;
        case Handle::LoopIn:e.loop_start=std::min(snap(shift(e.loop_start)),e.loop_end-2);break;case Handle::LoopOut:e.loop_end=std::max(snap(shift(e.loop_end)),e.loop_start+2);break;
        case Handle::Crossfade:e.crossfade=shift(e.crossfade);break;}
    set_edit(u,e);(void)y;
}
void play_editor_draw(){m::paper(320);if(!frame.active)return;
    module_heading::header(UnitKind::SamplePlayer,m::spot());
    if(words_surface()||word_frame.modal)words_draw();else precision_bottom();
}
bool play_editor_top(){if(!frame.active)return false;m::paper(400);
    module_heading::header(UnitKind::SamplePlayer,m::spot());
    precision_top(word_frame.modal?word_upper:frame);return true;
}
bool play_editor_word_modal(){return word_frame.modal;}
void play_editor_word_cancel(){words_cancel(true);}
bool play_editor_word_input(u32&down,u32 held,u32,bool blocked){
    if(!(held&KEY_TOUCH))words.quarantine=false;
    if(!words_modal()&&!words.quarantine)return false;
    bool valid=false;
    if(!blocked&&g_eng&&g_audio){platform::AudioOut::Guard guard(*g_audio);C*u=current();
        valid=u&&valid_context(*u)&&same(document(*u),expected)&&!g_eng->play_editor_busy()&&!param_lens_is_open()&&words.engine==g_eng&&words.ink==m::ink()&&words.spot==m::spot();}
    if(!valid){words_cancel((held&KEY_TOUCH)!=0);down&=KEY_SELECT|KEY_START|KEY_ZL;return (held&KEY_TOUCH)!=0;}
    if(down&(KEY_SELECT|KEY_START)){words_cancel((held&KEY_TOUCH)!=0);down&=KEY_SELECT|KEY_START|KEY_ZL;return true;}
    const unsigned local=down&~(KEY_TOUCH|KEY_ZL);
    if(local){words.press={};
        if(local&KEY_B){words_pop();words.quarantine=(held&KEY_TOUCH)!=0;}
        else if(local&KEY_A){if(words.confirm){if(word_frame.cells[1].enabled)words_activate(int(WordOp::Apply),int(words.operation));}
            else if(word_frame.count){const auto c=word_frame.cells[clamp(words.selected,0,word_frame.count-1)];if(c.enabled)words_activate(int(c.op),c.arg);}}
        else if(!words.confirm&&(local&(KEY_UP|KEY_DOWN)))words.selected=clamp(words.selected+((local&KEY_UP)?-1:1),0,word_frame.count-1);
        down&=KEY_ZL;return true;
    }
    return words.quarantine;
}
bool play_editor_word_touch(int x,int y){
    if(!context()||(!words_modal()&&!words.quarantine))return false;
    C*u=current();return u?words_touch(*u,x,y):false;
}
bool play_editor_word_drag(int x,int y){
    if(words.quarantine)return context();
    if(!words.press.owned)return false;
    auto&p=words.press;if(std::abs(x-p.x)>=8||std::abs(y-p.y)>=8||!p.box.contains(x,y))p.armed=false;
    return true;
}
bool play_editor_word_release(){
    if(words.quarantine){words.quarantine=false;words.press={};return true;}
    return words_release(true);
}
// PUBLIC_API
} // namespace seance::platform::ui3ds
