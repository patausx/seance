#include "ui_bounce.h"
#include "ui_state.h"
#include "sample_ownership.h"
#include "ui_nav.h"
#include "ui_theme.h"
#include "ui_palette.h"
#include "ui_workspace.h"
#include "ui_sequencer.h"
#include "ui_performance.h"
#include "menu_style.h"
#include "bounce_type_data.h"
#include "../../core/synth/wav_writer.h"
#include "../../core/ui/draw.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <sys/stat.h>
#include <type_traits>

namespace seance::platform::ui3ds {
namespace {
namespace m = menu_style;
constexpr int COLUMNS = 352;
constexpr int LENGTHS[] = {1, 2, 4, 8};
constexpr int CENTERS[] = {52, 124, 196, 268};
constexpr int LENGTH_BOUNDS[] = {16, 88, 160, 232, 304};
enum class Modal : uint8_t { None, Send, Discard };
enum class Action : uint8_t { None, Length1, Length2, Length4, Length8, Master, Chain1, Chain2, Chain3, Chain4, Record, Listen, Save, Send, Again, Confirm, Cancel };
constexpr const char* SOURCES[] = {"mix", "1", "2", "3", "4"};
constexpr int SOURCE_CENTERS[] = {48, 116, 170, 224, 278};
constexpr int SOURCE_BOUNDS[] = {20, 86, 143, 197, 251, 304};
struct Target {
    int x, y, width, height;
    bool contains(int px, int py) const { return hit(px,py,x,y,width,height); }
};
constexpr Target PRIMARY_TARGET{68,177,184,36};
constexpr Target LISTEN_TARGET{68,39,184,36};
constexpr Target SAVE_TARGET{68,79,184,36};
constexpr Target SEND_TARGET{68,119,184,36};
constexpr Target CONFIRM_TARGET{16,177,144,36};
constexpr Target CANCEL_TARGET{176,177,128,36};
struct Frame {
    BounceSnapshot take;
    bool valid = false, visible = false, transport = false, wave = false;
    bool available[4] = {};
    uint8_t selected = 2, chain = 0;
    BounceSource source = BounceSource::Master;
    bool source_muted = false, source_empty = false, listening = false, can_send = false;
    bool occupied[4] = {};
    uint32_t preview_position = 0, duration_frames = 0;
    Modal modal = Modal::None;
    int32_t bpm_q16 = 120 * 65536;
    int8_t low[COLUMNS] = {}, high[COLUMNS] = {};
    char notice[48] = {};
} frame;
static_assert(sizeof(Frame) <= 1024 && std::is_trivially_copyable<Frame>::value,
              "bounded frozen bounce frame; no pointers/PCM/DSP in draw");
struct WaveCache {
    Engine* owner = nullptr;
    uint32_t generation = 0, slot_generation = 0, frames = 0;
    int8_t low[COLUMNS] = {}, high[COLUMNS] = {};
} wave_cache;
struct Touch {
    Engine* owner = nullptr;
    Action action = Action::None;
    Modal modal = Modal::None;
    uint32_t generation = 0;
    int x = 0, y = 0;
} touch;
int selected = 2, destination = 0;
BounceSource selected_source = BounceSource::Master;
Modal modal = Modal::None;
uint32_t modal_generation = 0;
Engine* modal_owner = nullptr;
std::unique_ptr<BounceSendPlan> send_plan;
char notice[48] = {};

bool context() { return g_eng && g_audio && g_view == View::Bounce && g_overlay == Overlay::None && !nav_is_open(); }
void tell(const char* s) { std::snprintf(notice, sizeof notice, "%s", s); }
const char* result_text(BounceResult r) {
    switch (r) {
        case BounceResult::Ok: return "ready";
        case BounceResult::NoFreeSlot: return "no free sample slot";
        case BounceResult::MemoryBudget: return "too long / not enough sample memory";
        case BounceResult::TempoModulated: return "use a steady clock / no tempo mod";
        case BounceResult::NotSaved: return "save the take first";
        case BounceResult::UnsafeChain: return "chain has a protected recording";
        case BounceResult::Stale: return "state changed / try again";
        case BounceResult::Busy: return "stop the song / finish this take";
        default: return "cannot send this take / meter";
    }
}
const char* reason_text(BounceReason r) {
    switch (r) {
        case BounceReason::TempoChanged: return "tempo changed / partial audio only";
        case BounceReason::MeterChanged: return "meter changed / partial audio only";
        case BounceReason::SampleChanged: return "sample changed / discard this handle";
        case BounceReason::Reset: return "transport reset / not a full loop";
        case BounceReason::Stopped: return "stopped early / not a full loop";
        default: return "";
    }
}
bool waiting(const BounceSnapshot& t) { return t.status==BounceStatus::Armed && t.start_frame>0; }
BounceSnapshot current() {
    if (!g_eng || !g_audio) return {};
    platform::AudioOut::Guard guard(*g_audio);
    return g_eng->bounce_snapshot();
}
void close_modal() { modal = Modal::None; modal_owner = nullptr; send_plan.reset(); touch = {}; }
Action action_at(int x, int y) {
    if (modal != Modal::None) {
        if (modal == Modal::Send && hit(x,y,16,66,288,61))
            return Action(int(Action::Length1) + (x-16)/72);
        if (CONFIRM_TARGET.contains(x,y)) return Action::Confirm;
        if (CANCEL_TARGET.contains(x,y)) return Action::Cancel;
        return Action::None;
    }
    const auto t = current();
    if (t.active()) return PRIMARY_TARGET.contains(x,y) ? Action::Record : Action::None;
    if (t.slot >= 0) {
        if (t.take()) {
            if (LISTEN_TARGET.contains(x,y)) return Action::Listen;
            // A saved WAV is a status row, not an invisible repeat-save button.
            if (!t.saved && SAVE_TARGET.contains(x,y)) return Action::Save;
            if (t.status == BounceStatus::Complete && SEND_TARGET.contains(x,y)) return Action::Send;
        }
        return PRIMARY_TARGET.contains(x,y) ? Action::Again : Action::None;
    }
    for (int i=0;i<5;++i)
        if (hit(x,y,SOURCE_BOUNDS[i],42,SOURCE_BOUNDS[i+1]-SOURCE_BOUNDS[i],42))
            return Action(int(Action::Master)+i);
    for (int i=0;i<4;++i)
        if (hit(x,y,LENGTH_BOUNDS[i],116,LENGTH_BOUNDS[i+1]-LENGTH_BOUNDS[i],42))
            return Action(int(Action::Length1)+i);
    if (PRIMARY_TARGET.contains(x,y)) return Action::Record;
    return Action::None;
}
void select_destination(int chain) {
    // Preparation is outside the guard. The plan freezes all six unit identities;
    // commit rechecks them and owns retired module destruction after the guard.
    destination = std::max(0,std::min(3,chain));
    send_plan.reset(new(std::nothrow) BounceSendPlan);
    if (!send_plan) { tell("out of memory"); return; }
    BounceResult result;
    {
        platform::AudioOut::Guard guard(*g_audio);
        result = g_eng->plan_bounce_send(destination,*send_plan);
    }
    if (result != BounceResult::Ok) { send_plan.reset(); tell(result_text(result)); }
    else tell("");
}
struct ExportContext { Engine* engine; platform::AudioOut* audio; BounceSnapshot take; };
bool export_read(void* opaque, uint32_t first, uint32_t count, int16_t* out) {
    auto& c = *static_cast<ExportContext*>(opaque);
    if (g_eng != c.engine || g_audio != c.audio) return false;
    platform::AudioOut::Guard guard(*c.audio);
    return c.engine->read_bounce_frames(c.take,first,count,out);
}
bool unused_path(const char* path) {
    struct stat st{};
    if (stat(path,&st) == 0) return false;
    return errno == ENOENT;
}
bool save_take() {
    const auto take = current();
    if (!take.take()) { tell("no completed audio to save"); return false; }
    if (take.saved) { tell(""); return true; }
    char path[192], temporary[200], source_suffix[12] = {}; bool found = false;
    if(take.source!=BounceSource::Master)std::snprintf(source_suffix,sizeof source_suffix,"_chain%u",unsigned(take.source));
    const unsigned bpm100 = unsigned((int64_t(take.bpm_q16)*100+32768)/65536);
    for (unsigned i=1;i<=9999;++i) {
        const int n=std::snprintf(path,sizeof path,"%s/bounce_%04u_%u.%02ubpm_%ubar%s%s.wav",
            RECORDING_DIR,i,bpm100/100,bpm100%100,unsigned(take.bars),source_suffix,take.status==BounceStatus::Partial?"_partial":"");
        if (n<0 || n>=int(sizeof path)) break;
        std::snprintf(temporary,sizeof temporary,"%s.tmp",path);
        if (unused_path(path) && unused_path(temporary)) { found=true;break; }
    }
    if (!found) { tell("no free wav filename / sd error"); return false; }
    ExportContext c{g_eng,g_audio,take};
    int16_t scratch[1024*2]; // 4 KiB bounded copy; all SD writes are outside guard.
    const auto result = trackr::synth::save_wav_stream(path,bounce_channels(take.source),take.frames,ENGINE_SR,
        scratch,1024,export_read,&c);
    bool ok = result == trackr::synth::WavSaveResult::Ok;
    if (ok) {
        platform::AudioOut::Guard guard(*g_audio);
        ok = g_eng == c.engine && g_eng->mark_bounce_saved(take,path);
    }
    if (ok) { mark_project_mutated(); tell(""); }
    else tell("wav save failed / take is still here");
    return ok;
}
void retire_old_bounce_cache(const BounceSendPlan& plan, trackr::synth::Sample (&retired)[6]) {
    // Only after explicit chain replacement: evict its obsolete, saved bounce
    // cache, never an audible/shared/clipboard source or the on-disk recording.
    auto& bank=trackr::synth::SampleBank::instance();
    for(int i=0;i<6;++i) {
        const int slot=sample_ownership::unit_slot(plan.retired[i]);
        if(slot<0 || slot>=trackr::synth::SAMPLE_BANK_SIZE ||
           sample_ownership::count(*g_eng,slot,g_clip_unit))continue;
        const auto& p=bank.provenance(slot);
        const char* name=std::strrchr(p.path,'/');name=name?name+1:p.path;
        if(p.origin!=trackr::synth::SampleOrigin::Recording || std::strncmp(name,"bounce_",7))continue;
        bank.swap_sample_unchecked(slot,retired[i],trackr::synth::SampleOrigin::Runtime,"");
    }
}
bool occupied_chain(int chain) {
    for (const auto* u : g_eng->chains[chain].units) if (u) return true;
    return false;
}
void stop_listening() {
    if (!g_eng || !g_audio) return;
    platform::AudioOut::Guard guard(*g_audio);
    g_eng->stop_bounce_preview();
}
void release_take() {
    trackr::synth::Sample retired; // lifetime outside the audio guard
    BounceResult result;
    {
        platform::AudioOut::Guard guard(*g_audio);
        g_eng->stop_bounce_preview();
        result = g_eng->discard_bounce(retired);
    }
    close_modal();tell(result == BounceResult::Ok ? "" : result_text(result));
}
void dispatch(Action a) {
    if (!context() || a==Action::None) return;
    const auto t=current();
    if (modal!=Modal::None && (modal_owner!=g_eng || modal_generation!=t.generation)) {
        close_modal();tell("take changed / try again");return;
    }
    if (a>=Action::Master && a<=Action::Chain4) {
        if(modal!=Modal::None)return;
        if(t.slot>=0)return;
        selected_source=BounceSource(int(a)-int(Action::Master));tell("");return;
    }
    if (a>=Action::Length1 && a<=Action::Length8) {
        const int i=int(a)-int(Action::Length1);
        if (modal==Modal::Send) select_destination(i);
        else if (modal==Modal::None && t.slot<0) {
            BouncePlan p;BounceResult r;
            { platform::AudioOut::Guard guard(*g_audio);r=g_eng->plan_bounce(LENGTHS[i],p,selected_source); }
            if (r==BounceResult::Ok) {selected=i;tell("");} else tell(result_text(r));
        }
        return;
    }
    if (a==Action::Cancel) {close_modal();tell("");return;}
    if (a==Action::Confirm) {
        if (modal==Modal::Discard) {
            release_take();
        } else if (modal==Modal::Send) {
            if (!send_plan) {tell("choose a safe chain / stop transport");return;}
            BounceResult r=Engine::stage_bounce_send(*send_plan);
            trackr::synth::Sample retired_samples[6]; // destruction stays outside guard
            if (r==BounceResult::Ok) {
                platform::AudioOut::Guard guard(*g_audio);
                r=g_eng->commit_bounce_send(*send_plan);
                if(r==BounceResult::Ok) {
                    retire_old_bounce_cache(*send_plan,retired_samples);
                    g_eng->transport_reset(); // START now aligns the ordinary PLAY loop.
                }
            }
            const int chain=destination;
            close_modal();
            if (r==BounceResult::Ok) {
                workspace_reset_chain(chain);g_sel_chain=g_edit_chain=chain;g_sel_slot=g_edit_slot=0;
                char message[48];std::snprintf(message,sizeof message,"ready in chain %d / start to play",chain+1);
                mark_project_mutated();tell(message);
            } else tell(result_text(r));
        }
        return;
    }
    if (modal!=Modal::None) return;
    if (a==Action::Record) {
        if (t.active()) {
            {platform::AudioOut::Guard guard(*g_audio);g_eng->stop_bounce();}
            if (!current().frames) release_take(); else tell("");
            return;
        }
        if (t.slot>=0) return;
        if (!g_has_dsp) {tell("audio unavailable / cannot record");return;}
        BouncePlan p;BounceResult r;
        {platform::AudioOut::Guard guard(*g_audio);r=g_eng->plan_bounce(LENGTHS[selected],p,selected_source);}
        if (r==BounceResult::Ok) r=Engine::stage_bounce(p);
        if (r==BounceResult::Ok) {
            platform::AudioOut::Guard guard(*g_audio);
            const bool stopped=!g_eng->transport_playing;
            r=g_eng->commit_bounce(p,true);
            if(r==BounceResult::Ok && stopped)g_eng->set_sequencer_enabled(true);
        }
        tell(r==BounceResult::Ok?"":result_text(r));
    } else if (a==Action::Listen) {
        if (!t.take()) return;
        if (!g_has_dsp) {tell("audio unavailable / cannot listen");return;}
        platform::AudioOut::Guard guard(*g_audio);
        if (g_eng->bounce_preview_active()) {g_eng->stop_bounce_preview();tell("");}
        else {
            g_eng->transport_stop();
            tell(g_eng->start_bounce_preview()?"":"take unavailable / try again");
        }
    } else if (a==Action::Save) {
        if(t.take())save_take();
    } else if (a==Action::Send) {
        if (t.status!=BounceStatus::Complete) return;
        stop_listening();
        if (!save_take()) return; // an SD failure does not even pause the song
        {
            platform::AudioOut::Guard guard(*g_audio);
            g_eng->transport_stop();
            for (int i=0;i<4;++i) if (!occupied_chain(i)) {destination=i;break;}
        }
        modal=Modal::Send;modal_generation=t.generation;modal_owner=g_eng;select_destination(destination);
    } else if (a==Action::Again && t.slot>=0 && !t.active()) {
        stop_listening();
        if(t.saved || !t.frames) {release_take();return;}
        modal=Modal::Discard;modal_generation=t.generation;modal_owner=g_eng;tell("");
    }
}
void right(int x,int y,const char* s,u32 color) {ui::text(x-ui::text_w(s),y,s,color);}
const bounce_type::Label* find_word(const char* text, int size) {
    for (const auto& label : bounce_type::labels)
        if (label.size==size && !std::strcmp(text,label.text)) return &label;
    return nullptr;
}
void word(int x,int baseline,const char* text,int size,u32 color,bool center=false) {
    const auto* label=find_word(text,size);
    if (!label) {ui::text(center?x-ui::text_w(text)/2:x,baseline-8,text,color);return;}
    if (center) x-=label->width/2;
    const int stride=(label->width+7)/8;
    for (int y=0;y<label->height;) {
        const auto* bits=bounce_type::bitmap+label->offset+y*stride;
        int end=y+1;
        while (end<label->height && !std::memcmp(bits,bounce_type::bitmap+label->offset+end*stride,stride)) ++end;
        int run=-1;
        for (int column=0;column<=label->width;++column) {
            const bool on=column<label->width && (bits[column/8] & (0x80u>>(column&7)));
            if (on && run<0) run=column;
            if (!on && run>=0) {
                ui::rect(x+run,baseline+label->top+y,column-run,end-y,color);run=-1;
            }
        }
        y=end;
    }
}
void control(int baseline,const char* text,u32 color,const char* key=nullptr) {
    word(88,baseline,text,22,color);
    if (key) right(242,baseline-10,key,m::ghost());
}
void primary(const char* text,u32 color,int size=28) {word(160,202,text,size,color,true);}
void footer(const char* left=nullptr) {
    if (left) ui::text(24,225,left,m::ghost());
    right(296,225,"b back",m::ghost());
}
void ribbon(const BounceSnapshot& t) {
    // A single thin rail lives on the lower LCD. Only the current bar is accent.
    const unsigned bars=std::max(1u,std::min(8u,unsigned(t.bars)));
    const uint32_t elapsed=waiting(t)?0:std::min(t.frames,t.target_frames);
    const int current=elapsed && t.target_frames?int(std::min(uint64_t(bars-1),uint64_t(elapsed)*bars/t.target_frames)):-1;
    const int tip=30+(t.target_frames?int((uint64_t(elapsed)*260+t.target_frames/2)/t.target_frames):0);
    for (unsigned i=0;i<bars;++i) {
        const int a=30+int((260*i+bars/2)/bars);
        const int b=30+int((260*(i+1)+bars/2)/bars)-6;
        const u32 color=int(i)==current?m::spot():m::ghost();
        char number[4];std::snprintf(number,sizeof number,"%u",i+1);
        word((a+b)/2,116,number,22,color,true);
        ui::line(a,139,b,139,palette::shadow());
        if (tip>a) ui::line(a,139,std::min(b,tip),139,color);
    }
    if (elapsed) ui::line(tip,137,tip,141,m::spot());
}
void centered(int width,int y,const char* text,u32 color) {ui::text((width-ui::text_w(text))/2,y,text,color);}
void duration(char* out,size_t size,uint32_t frames) {
    const unsigned cs=unsigned((uint64_t(frames)*100+ENGINE_SR/2)/ENGINE_SR);
    std::snprintf(out,size,"%u.%02us",cs/100,cs%100);
}
void capture_wave(const trackr::synth::Sample& s, const BounceSnapshot& t) {
    if(wave_cache.owner==g_eng && wave_cache.generation==t.generation &&
       wave_cache.slot_generation==t.slot_generation && wave_cache.frames==t.frames)return;
    wave_cache.owner=g_eng;wave_cache.generation=t.generation;wave_cache.slot_generation=t.slot_generation;wave_cache.frames=t.frames;
    for(int x=0;x<COLUMNS;++x) {
        const uint32_t begin=uint64_t(x)*t.frames/COLUMNS;
        const uint32_t end=uint64_t(x+1)*t.frames/COLUMNS;
        int low=32767,high=-32768;
        for(int j=0;j<8;++j) {
            const uint32_t at=std::min(t.frames-1,begin+uint32_t(uint64_t(end>begin?end-begin-1:0)*j/7));
            for(int ch=0;ch<s.channels;++ch){const int v=s.data[size_t(at)*s.channels+ch];low=std::min(low,v);high=std::max(high,v);}
        }
        wave_cache.low[x]=int8_t(low*34/32768);wave_cache.high[x]=int8_t(high*34/32768);
    }
}
} // namespace

void bounce_enter() {
    stop_listening();close_modal();notice[0]=0;
    const auto t=current();if(t.slot>=0)selected_source=t.source;
}
void bounce_cancel_input() {stop_listening();close_modal();}
void bounce_buttons(u32 down,u32 held) {
    if(!context())return;
    if(down & ~(KEY_TOUCH|KEY_ZL))touch={};
    if(down&KEY_B){stop_listening();if(modal!=Modal::None){close_modal();tell("");}else {g_view=View::Chains;g_overlay=Overlay::None;}return;}
    if(down&KEY_START){
        close_modal();platform::AudioOut::Guard guard(*g_audio);
        if(!(held&KEY_L)){
            g_eng->stop_bounce_preview();
            if(held&KEY_R){g_eng->transport_stop();g_eng->transport_reset();}
            else if(g_eng->transport_playing)g_eng->transport_stop();
            else {g_eng->set_sequencer_enabled(true);g_eng->transport_play();}
        }
        return;
    }
    if(held&(KEY_L|KEY_R))return;
    if(modal==Modal::None && (down&(KEY_UP|KEY_DOWN))) {
        const int source=std::max(0,std::min(4,int(selected_source)+((down&KEY_DOWN)?1:-1)));
        dispatch(Action(int(Action::Master)+source));return;
    }
    if(down&(KEY_LEFT|KEY_RIGHT|KEY_UP|KEY_DOWN)) {
        const int delta=(down&(KEY_RIGHT|KEY_UP))?1:-1;
        const int i=std::max(0,std::min(3,(modal==Modal::Send?destination:selected)+delta));
        dispatch(Action(int(Action::Length1)+i));return;
    }
    const auto t=current();
    if(down&KEY_A)dispatch(modal!=Modal::None?Action::Confirm:t.take()?Action::Listen:t.slot>=0 && !t.active()?Action::Again:Action::Record);
    else if(down&KEY_X)dispatch(Action::Save);
    else if(down&KEY_Y)dispatch(Action::Send);
}
void bounce_touch_down(int x,int y) {
    touch={};if(!context())return;
    touch={g_eng,action_at(x,y),modal,current().generation,x,y};
}
void bounce_touch_hold(int x,int y) {
    if(!context() || touch.owner!=g_eng || touch.modal!=modal || action_at(x,y)!=touch.action ||
       std::abs(x-touch.x)>=12 || std::abs(y-touch.y)>=12)touch={};
}
void bounce_touch_release(bool commit) {
    const Touch before=touch;touch={};
    if(commit && context() && before.owner==g_eng && before.modal==modal && before.generation==current().generation &&
       action_at(before.x,before.y)==before.action)
        dispatch(before.action);
}
bool bounce_guard_project_change() {
    if(!g_eng || !g_audio)return false;
    {platform::AudioOut::Guard guard(*g_audio);if(!g_eng->bounce_blocks_patch_change())return false;}
    // A saved immutable WAV needs no extra discard ceremony before new/load/exit.
    const auto take=current();
    if(take.saved && !take.active()) {
        release_take();
        if(current().slot<0)return false;
    }
    if(g_view==View::Performance)performance_view_leave();
    sequencer_view_cancel_input();g_seq_lock_armed=false;g_seq_lock_flash=0;
    nav_close();g_view=View::Bounce;g_overlay=Overlay::None;g_drag=Drag::None;
    stop_listening();close_modal();tell("save wav or record again to release take");return true;
}
void bounce_capture() {
    frame={};if(!g_eng || !g_audio)return;
    frame.valid=true;frame.visible=g_view==View::Bounce && g_overlay==Overlay::None;
    {
        platform::AudioOut::Guard guard(*g_audio);
        frame.take=g_eng->bounce_snapshot();
        if(!frame.visible || nav_is_open())g_eng->stop_bounce_preview();
        if(!frame.visible)return; // other pages pay only for the small badge copy
        frame.transport=g_eng->transport_playing;frame.bpm_q16=g_eng->effective_bpm_q16();
        frame.listening=g_eng->bounce_preview_active();frame.preview_position=g_eng->bounce_preview_position();
        frame.source=frame.take.slot>=0?frame.take.source:selected_source;
        for(int i=0;i<4;++i)frame.occupied[i]=occupied_chain(i);
        frame.source_muted=frame.source!=BounceSource::Master && g_eng->chains[int(frame.source)-1].mute;
        frame.source_empty=frame.source!=BounceSource::Master && !frame.occupied[int(frame.source)-1];
        if(frame.take.slot<0) {
            frame.duration_frames=uint64_t(ENGINE_SR)*60*65536*g_eng->beats_per_bar*LENGTHS[selected]/std::max(int32_t(1),frame.bpm_q16);
            for(int i=0;i<4;++i){
                BouncePlan p;frame.available[i]=g_eng->plan_bounce(LENGTHS[i],p,frame.source)==BounceResult::Ok;
                if(i==selected && frame.available[i])frame.duration_frames=p.capacity_frames;
            }
        }
        if(frame.take.take()) {
            const auto* source=g_eng->bounce_sample(frame.take);
            if(source){capture_wave(*source,frame.take);frame.wave=true;
                std::copy(std::begin(wave_cache.low),std::end(wave_cache.low),frame.low);
                std::copy(std::begin(wave_cache.high),std::end(wave_cache.high),frame.high);}
        }
    }
    if(modal!=Modal::None && (modal_owner!=g_eng || modal_generation!=frame.take.generation))close_modal();
    frame.can_send=send_plan!=nullptr;
    frame.selected=uint8_t(selected);frame.chain=uint8_t(destination);frame.modal=modal;
    if(frame.take.slot>=0)for(int i=0;i<4;++i)if(LENGTHS[i]==frame.take.bars)frame.selected=uint8_t(i);
    std::snprintf(frame.notice,sizeof frame.notice,"%s",notice);
}
void bounce_draw_indicator() {
    if(!frame.valid || !frame.take.active())return;
    // Four-pixel global status in the outer top-screen margin, no covered labels,
    // fake pulse animation, engine access or extra text collision surface.
    if(frame.take.status==BounceStatus::Recording)ui::rect(392,4,4,4,m::spot());
    else {ui::rect(392,4,4,1,m::spot());ui::rect(392,7,4,1,m::spot());ui::rect(392,5,1,2,m::spot());ui::rect(395,5,1,2,m::spot());}
}
void bounce_draw_top() {
    m::paper(400);if(!frame.valid)return;
    const auto& t=frame.take;char text[64],number[12],seconds[20],tempo[20];
    const bool master=frame.source==BounceSource::Master;
    if(master)std::snprintf(text,sizeof text,"mix / stereo");
    else std::snprintf(text,sizeof text,"chain %u / mono",unsigned(frame.source));
    m::heading(24,20,"bounce",m::ink());right(376,25,text,m::ghost());
    const unsigned tenths=unsigned((int64_t(t.slot>=0?t.bpm_q16:frame.bpm_q16)*10+32768)/65536);
    if(tenths%10)std::snprintf(tempo,sizeof tempo,"%u.%u bpm",tenths/10,tenths%10);
    else std::snprintf(tempo,sizeof tempo,"%u bpm",tenths/10);
    if(t.active()) {
        centered(400,77,waiting(t)?"starts next bar":"recording",m::ghost());
        const unsigned progress=t.target_frames?std::min(unsigned(t.bars),unsigned(uint64_t(t.frames)*t.bars/t.target_frames)):0;
        std::snprintf(number,sizeof number,"%u",waiting(t)?0u:std::min(unsigned(t.bars),progress+1));
        word(200,135,number,40,m::ink(),true);
        std::snprintf(text,sizeof text,"of %u bars",unsigned(t.bars));centered(400,152,text,m::ghost());
        char total[20];duration(total,sizeof total,t.target_frames);duration(seconds,sizeof seconds,t.frames);
        seconds[std::strlen(seconds)-1]=0; // One seconds suffix for the whole elapsed / total readout.
        std::snprintf(text,sizeof text,"%s / %s",seconds,total);centered(400,188,text,m::ghost());
        if(frame.source_muted || frame.source_empty)
            centered(400,214,frame.source_muted?"chain muted / records silence":"chain empty / records silence",m::spot());
    } else if(frame.wave) {
        word(24,80,frame.listening?"listening":t.status==BounceStatus::Partial?"partial take":"take ready",22,m::ink());
        right(376,69,t.saved?"saved to recordings":"not saved",m::ghost());
        ui::line(24,137,375,137,palette::shadow());
        for(int x=0;x<COLUMNS;++x)ui::line(24+x,137-frame.high[x],24+x,137-frame.low[x],m::ghost());
        if(frame.listening && t.frames) {
            const int x=24+std::min(351,int(uint64_t(frame.preview_position)*352/t.frames));
            ui::line(x,100,x,174,m::spot());
        }
        duration(seconds,sizeof seconds,frame.listening?frame.preview_position:0);ui::text(24,184,seconds,m::ghost());
        duration(seconds,sizeof seconds,t.frames);right(376,184,seconds,m::ghost());
        std::snprintf(text,sizeof text,"%u bars / %s",unsigned(t.bars),tempo);
        centered(400,212,t.reason!=BounceReason::None?reason_text(t.reason):text,m::ghost());
        if(frame.transport)centered(400,229,t.status==BounceStatus::Complete?"listen / use pauses the song":"listen pauses the song",m::ghost());
    } else if(t.slot>=0) {
        centered(400,100,"no audio recorded",m::ink());
        centered(400,150,reason_text(t.reason),m::ghost());
    } else {
        centered(400,77,"record length",m::ghost());
        std::snprintf(number,sizeof number,"%d",LENGTHS[frame.selected]);
        word(200,135,number,40,m::ink(),true);
        centered(400,152,"bars",m::ghost());
        duration(seconds,sizeof seconds,frame.duration_frames);
        std::snprintf(text,sizeof text,"%s / %s",seconds,tempo);centered(400,188,text,m::ghost());
        if(frame.source_muted || frame.source_empty)
            centered(400,214,frame.source_muted?"chain muted / records silence":"chain empty / records silence",m::spot());
    }
}
void bounce_draw() {
    m::paper(320);if(!frame.valid)return;
    const auto& t=frame.take;
    if(frame.modal!=Modal::None) {
        const bool send=frame.modal==Modal::Send;
        word(24,39,send?"use in chain":"record again?",28,m::ink());
        if(send) {
            for(int i=0;i<4;++i){char n[4];std::snprintf(n,sizeof n,"%d",i+1);
                word(CENTERS[i],98,n,22,frame.chain==i?m::spot():m::ghost(),true);
                if(frame.chain==i)ui::rect(CENTERS[i],106,2,2,m::spot());
                const char* label=frame.occupied[i]?"used":"empty";
                ui::text(CENTERS[i]-ui::text_w(label)/2,116,label,m::ghost());}
            centered(320,139,frame.occupied[frame.chain]?"replaces modules + rhythm in all 8 banks":"sets this chain's rhythm in all 8 banks",m::ink());
            centered(320,155,frame.notice[0]?frame.notice:t.source==BounceSource::Master?"saved wav kept / play output is mono":"saved wav kept / song paused",frame.notice[0]?m::spot():m::ghost());
        } else {
            centered(320,85,"this take has not been saved.",m::ink());
            centered(320,112,"release it and set up a new recording?",m::ghost());
            centered(320,146,"your chains stay unchanged",m::ghost());
        }
        word(24,202,send?(frame.occupied[frame.chain]?"replace":"use chain"):"release take",22,send && !frame.can_send?m::ghost():m::spot());
        word(216,202,"cancel",22,m::ink());
        ui::text(24,225,"a confirm",m::ghost());right(296,225,"b cancel",m::ghost());return;
    }
    if(t.active()) {
        char text[32];
        if(frame.source==BounceSource::Master)std::snprintf(text,sizeof text,"mix / %u bars",unsigned(t.bars));
        else std::snprintf(text,sizeof text,"chain %u / %u bars",unsigned(frame.source),unsigned(t.bars));
        ui::text(24,25,text,m::ghost());right(296,25,waiting(t)?"waiting":"recording",m::ghost());
        ribbon(t);
        if(frame.notice[0])centered(320,164,frame.notice,m::spot());
        primary(waiting(t)?"cancel":"stop early",m::ink());
        footer(waiting(t)?"a cancel":"a stop early");return;
    }
    if(t.slot>=0) {
        if(t.take()) {
            control(64,frame.listening?"stop listening":"listen",m::spot(),"a");
            control(104,t.saved?"wav saved":"save wav",t.saved?m::ghost():m::ink(),t.saved?nullptr:"x");
            if(t.status==BounceStatus::Complete)control(144,"use in chain",m::ink(),"y");
            else ui::text(88,134,"partial take",m::ghost());
            primary("record again",m::ghost(),22);footer();
        } else {
            primary("record again",m::ink(),22);footer("a record again");
        }
        if(frame.notice[0])centered(320,164,frame.notice,m::spot());
        return;
    }
    ui::text(24,25,"source",m::ghost());
    for(int i=0;i<5;++i) {
        const bool active=int(frame.source)==i;
        const u32 c=active?m::spot():m::ghost();
        word(SOURCE_CENTERS[i],67,SOURCES[i],22,c,true);
        if(active)ui::rect(SOURCE_CENTERS[i],75,2,2,c);
    }
    ui::text(24,99,"bars",m::ghost());
    for(int i=0;i<4;++i){char n[4];std::snprintf(n,sizeof n,"%d",LENGTHS[i]);
        const u32 c=!frame.available[i]?palette::shadow():frame.selected==i?m::spot():m::ghost();
        word(CENTERS[i],141,n,28,c,true);
        if(frame.selected==i)ui::rect(CENTERS[i],149,2,2,c);
    }
    if(frame.notice[0])centered(320,164,frame.notice,m::spot());
    primary("record",frame.available[frame.selected]?m::ink():m::ghost());
    footer("a record");
}
} // namespace seance::platform::ui3ds
