#include "ui_transition.h"
#include "ui_order.h"
#include "performance_tabs.h"
#include "performance_return.h"
#include "ui_nav.h"
#include "ui_state.h"
#include "ui_theme.h"
#include "menu_style.h"
#include "module_fader.h"
#include "inspector_gothic.h"
#include "audio_out.h"
#include "../../core/ui/draw.h"
#include <algorithm>
#include <cstdio>

namespace seance::platform::ui3ds::transition {
namespace {
namespace m = menu_style;
enum class Mode : uint8_t { Rhythm, Sound, Both };
enum class Gesture : uint8_t { None, Source, Destination, Rail, Keep, Fx, Launch, TakeOver };
struct Frame {
    PatternTransitionState rhythm;
    uint8_t bank[2] = {0,1};
    TransitionFxState fx;
    uint8_t playing = 0, pending = 255;
    LaunchQuantize quantize = LaunchQuantize::Bar;
    q15 sound_position = 0;
    bool scenes_ready = false, sound_linked = false, running = false, chain_running = false;
} g_frame;
static_assert(sizeof(Frame) <= 64, "bounded scalar transition capture");
bool g_open = false, g_blocked = false, g_rhythm_seen = false;
performance_return::Point g_return;
int g_return_position = 0;
Mode g_mode = Mode::Rhythm;
Gesture g_gesture = Gesture::None;
struct LaunchPress {
    Engine* owner = nullptr;
    uint8_t bank = 255, playing = 255, pending = 255;
    LaunchQuantize quantize = LaunchQuantize::Bar;
    bool running = false, blending = false, chain_running = false;
} g_launch_press;
static_assert(sizeof(LaunchPress)<=32,"bounded release-only bank command");
struct QueuedLaunch {
    Engine* owner = nullptr;
    uint8_t from = 255, to = 255;
} g_queued_launch;
static_assert(sizeof(QueuedLaunch)<=16,"bounded UI launch completion observer");
bool g_launch_key_frame = false;
int g_origin_bank = -1;
int g_bank[2] = {0,1}, g_candidate = 0, g_anchor_y = 0, g_start_x = 0;
int g_rail_x = 0, g_rail_position = 0, g_position = 0;
TransitionFxKind g_fx_kind = TransitionFxKind::None;
int g_fx_x = 0, g_fx_initial = 0, g_echo_amount = 16384, g_roll_division = 2;
uint32_t g_fx_epoch = 0;
const char* g_notice = nullptr;
int g_notice_frames = 0;
int clamp(int v, int lo, int hi) { return std::max(lo,std::min(hi,v)); }
bool allowed() { return g_open && !g_blocked && g_view==View::Performance && g_overlay==Overlay::None && !nav_is_open(); }
void notice(const char* s) { g_notice=s;g_notice_frames=150; }
int endpoint() { return g_gesture==Gesture::Source?0:g_gesture==Gesture::Destination?1:-1; }
const char* launch_when(LaunchQuantize quantize) {
    switch(quantize) {
        case LaunchQuantize::Immediate:return "now";
        case LaunchQuantize::Sixteenth:return "1/16";
        case LaunchQuantize::Beat:return "beat";
        case LaunchQuantize::Bar:return "bar";
    }
    return "?";
}
// Called under the audio guard. A stale cancel must never become a launch.
bool launch_matches() {
    const auto& p=g_launch_press;
    return g_eng && p.owner==g_eng && p.bank==g_bank[1] && p.playing==g_eng->active_pattern &&
        p.pending==g_eng->pending_pattern && p.quantize==g_eng->pattern_launch_quantize &&
        p.running==g_eng->transport_playing && p.blending==g_eng->pattern_transition().active &&
        p.chain_running==g_eng->pattern_chain_playing;
}
void poll_queued_launch() {
    if(!g_queued_launch.owner)return;
    if(g_queued_launch.owner!=g_eng || !g_audio) {g_queued_launch={};return;}
    platform::AudioOut::Guard guard(*g_audio);
    if(g_eng->active_pattern==g_queued_launch.to) {
        // Also dirty on completion: a save may have occurred while waiting.
        if(g_queued_launch.from!=g_queued_launch.to)mark_project_mutated();
        g_queued_launch={};
    } else if(g_eng->pending_pattern!=g_queued_launch.to)g_queued_launch={};
}
void capture() {
    if(!g_eng || !g_audio) return;
    platform::AudioOut::Guard guard(*g_audio);
    g_frame.rhythm=g_eng->pattern_transition();
    g_frame.fx=g_eng->transition_fx();
    g_frame.playing=g_eng->active_pattern;g_frame.pending=g_eng->pending_pattern;
    g_frame.quantize=g_eng->pattern_launch_quantize;g_frame.running=g_eng->transport_playing;
    g_frame.chain_running=g_eng->pattern_chain_playing;
    if(g_gesture==Gesture::Launch && !launch_matches()) {
        g_gesture=Gesture::None;g_launch_press.owner=nullptr;
        if(g_drag==Drag::PerformanceMorph)g_drag=Drag::None;
    }
    g_frame.scenes_ready=g_eng->global_scene_topology_compatible();
    g_frame.sound_position=g_eng->scene_morph[0];g_frame.sound_linked=true;
    for(int c=1;c<NUM_CHAINS;++c)g_frame.sound_linked&=g_eng->scene_morph[c]==g_frame.sound_position;
    for(int side=0;side<2;++side) {
        const int bank=endpoint()==side?g_candidate:g_bank[side];
        g_frame.bank[side]=(uint8_t)bank;
    }
}
void begin_launch() {
    if(!g_eng || !g_audio || g_launch_key_frame)return;
    {
        platform::AudioOut::Guard guard(*g_audio);
        auto& p=g_launch_press;p.owner=g_eng;p.bank=(uint8_t)g_bank[1];
        p.playing=g_eng->active_pattern;p.pending=g_eng->pending_pattern;
        p.quantize=g_eng->pattern_launch_quantize;p.running=g_eng->transport_playing;
        p.blending=g_eng->pattern_transition().active;p.chain_running=g_eng->pattern_chain_playing;
    }
    g_gesture=Gesture::Launch;g_drag=Drag::PerformanceMorph;capture();
}
void commit_launch() {
    poll_queued_launch();
    if(!g_eng || !g_audio)return;
    platform::AudioOut::Guard guard(*g_audio);
    if(!launch_matches()) {notice("bank action changed / tap again");return;}
    if(g_eng->pattern_transition().active) {notice("keep bank before launch");return;}
    if(g_launch_press.pending<NUM_PATTERNS) {
        // Existing current-bank request only cancels pending; no clock reset.
        g_eng->request_pattern(g_eng->active_pattern,false);g_queued_launch={};
        notice("queued launch cancelled");return;
    }
    const int target=g_launch_press.bank,previous=g_eng->active_pattern;
    if(target<0 || target>=NUM_PATTERNS)return;
    if(target==previous && !g_eng->pattern_chain_playing) {notice("bank already playing");return;}
    // Never rewrite the project's launch quantum or start the transport here.
    g_eng->request_pattern(target);
    if(target!=previous)mark_project_mutated();
    if(g_eng->pending_pattern==target) {
        g_queued_launch={g_eng,(uint8_t)previous,(uint8_t)target};
        notice("bank queued / cancel below");
    } else notice("bank playing / sound and levels held");
}
int rhythm_position() { return g_frame.rhythm.active?g_frame.rhythm.position:0; }
void finish_return() {
    if(!g_return.active())return;
    bool restored=false;
    if(g_eng && g_audio) {
        platform::AudioOut::Guard guard(*g_audio);restored=g_return.restore(*g_eng);
    } else g_return.reset();
    touch_release(false);capture();
    // Clearing our own temporary blend is not an external bank launch/reset.
    if(restored)g_rhythm_seen=g_frame.rhythm.active;
    g_position=g_mode==Mode::Sound?g_frame.sound_position:rhythm_position();
    if(!restored)notice("return cancelled / context changed");
}
const char* mode_block_reason(Mode mode) {
    if(mode==Mode::Rhythm)return nullptr;
    if(!g_frame.scenes_ready)return "capture matching a/b in mixer";
    if(!g_frame.sound_linked)return "align chain a/b in mixer";
    if(mode==Mode::Both && g_frame.sound_position!=rhythm_position())
        return "match rhythm and a/b positions";
    return nullptr;
}
bool keep_endpoint() {
    const auto& state=g_frame.rhythm;
    return state.active && g_frame.pending>=NUM_PATTERNS &&
        (state.position==0 || state.position==32767 || state.source==state.destination);
}
void select_mode(Mode mode) {
    capture();
    if(const char* reason=mode_block_reason(mode)) {notice(reason);return;}
    // Selecting a scope never applies a scene or changes scheduled rhythm. The
    // excluded layer holds its current state, and the rail picks up its owner.
    g_mode=mode;g_position=mode==Mode::Sound?g_frame.sound_position:rhythm_position();
    g_notice_frames=0;
}
void set_position(int next) {
    next=clamp(next,0,32767);
    if(next==g_position) return; // touch-down and returning to exact origin are inert
    platform::AudioOut::Guard guard(*g_audio);
    if(g_mode!=Mode::Rhythm && !g_eng->global_scene_topology_compatible()) {
        notice("a/b changed / return to mixer");return;
    }
    if(g_mode!=Mode::Sound && !g_eng->set_pattern_transition(g_bank[0],g_bank[1],(q15)next)) {
        notice("finish queued bank launch first");return;
    }
    if(g_mode!=Mode::Sound) {
        g_rhythm_seen=true;
        if(g_return.active()) {g_return.rhythm_changed=true;g_return.last_rhythm=g_eng->pattern_transition();}
    }
    if(g_mode!=Mode::Rhythm) {
        g_eng->apply_transition_scene_morph((q15)next);
        if(g_return.active())g_return.sound_changed=true;
        else mark_project_mutated();
    }
    g_position=next;
}
void keep_bank() {
    platform::AudioOut::Guard guard(*g_audio);
    const auto state=g_eng->pattern_transition();
    if(!state.active) {notice("move the rhythm rail first");return;}
    if(state.position!=0 && state.position!=32767 && state.source!=state.destination) {
        notice("move rhythm rail to either end");return;
    }
    const int previous=g_eng->active_pattern;
    if(!g_eng->finish_pattern_transition()) {notice("bank launch owns playback");return;}
    if(previous!=g_eng->active_pattern)mark_project_mutated();
    g_bank[0]=g_eng->active_pattern;g_bank[1]=(g_bank[0]+1)%4;g_origin_bank=g_bank[0];
    g_mode=Mode::Rhythm;g_position=0;g_rhythm_seen=false;
    notice("bank kept / sound and levels held");
}
void commit_bank(int side) {
    if(g_candidate==g_bank[side])return;
    platform::AudioOut::Guard guard(*g_audio);
    const auto state=g_eng->pattern_transition();
    const int pos=state.active?state.position:0;
    // Never replace a contributing endpoint, even while stopped. Same-bank
    // endpoints are also protected until the opposite end is reached.
    if(state.active && ((side==0 && pos<32767)||(side==1 && pos>0))) {
        notice(side==0?"source live / move rail right":"target live / move rail left");return;
    }
    int next[2]={g_bank[0],g_bank[1]};next[side]=g_candidate;
    if(state.active && !g_eng->set_pattern_transition(next[0],next[1],state.position)) {
        notice("endpoint busy / try again");return;
    }
    g_bank[side]=g_candidate;notice("bank selected / no launch");
}
void draw_bank(int side) {
    const int cx=side?276:36;
    const char number[2]={(char)('1'+g_frame.bank[side]),0};
    const auto* glyph=inspector_gothic::digit(number[0]);
    const u32 color=side?m::spot():m::ink();
    const char* label=side?"to":"from";
    ui::text(cx-ui::text_w(label)/2,29,label,m::ghost());
    if(glyph)inspector_gothic::draw_numeric(cx-glyph->width,41-glyph->top*2,number,color,m::spot(),2);
    const char* status=endpoint()==side?"stage":g_frame.bank[side]==g_frame.pending?"queued":
        !g_frame.rhythm.active&&g_frame.bank[side]==g_frame.playing?"play":"swipe";
    ui::text(cx-ui::text_w(status)/2,115,status,endpoint()==side?m::spot():m::ghost());
}
void release_fx() {
    if(g_eng && g_audio) {
        platform::AudioOut::Guard guard(*g_audio);
        g_eng->release_transition_fx();
    }
    g_fx_kind=TransitionFxKind::None;
}
void start_fx(int pad,int x) {
    g_fx_kind=(TransitionFxKind)(pad+1);g_fx_x=x;
    g_fx_initial=pad==0?0:pad==1?g_echo_amount:g_roll_division;
    bool ok=false;
    {
        platform::AudioOut::Guard guard(*g_audio);
        ok=g_eng->set_transition_fx(g_fx_kind,g_fx_initial);
        g_fx_epoch=g_eng->transition_fx().epoch;
    }
    if(!ok) {g_fx_kind=TransitionFxKind::None;notice("fx buffer unavailable");return;}
    g_gesture=Gesture::Fx;g_drag=Drag::PerformanceMorph;g_notice_frames=0;capture();
}
bool owns_fx() {
    capture();
    return g_frame.fx.epoch==g_fx_epoch && g_frame.fx.kind==g_fx_kind;
}
void slide_fx(int x) {
    if(!owns_fx()) {touch_release(false);return;}
    int next=g_fx_initial;
    if(g_fx_kind==TransitionFxKind::Roll) {
        // Same saturating 18px/24px/12px hysteresis as bank swipe.
        while(x-g_fx_x>=18 || g_fx_x-x>=18) {
            const int delta=x>g_fx_x?1:-1;
            const int value=clamp(g_roll_division+delta,0,3);
            if(value==g_roll_division) {g_fx_x=x;break;}
            g_roll_division=value;g_fx_x+=delta*24;
        }
        next=g_roll_division;
    } else {
        const int scale=g_fx_kind==TransitionFxKind::Filter?65534:32767;
        const int delta=(clamp(x,-320,640)-g_fx_x)*scale;
        next=clamp(g_fx_initial+(delta+(delta<0?-40:40))/80,
                   g_fx_kind==TransitionFxKind::Filter?-32767:0,32767);
        if(g_fx_kind==TransitionFxKind::Echo)g_echo_amount=next;
    }
    platform::AudioOut::Guard guard(*g_audio);
    g_eng->set_transition_fx(g_fx_kind,next);
}
void draw_fx() {
    const char* names[]={"filter","echo","roll"};
    const char* divisions[]={"1/4","1/8","1/16","1/32"};
    for(int pad=0;pad<3;++pad) {
        const int x=8+104*pad, x0=x+8, x1=x+88, y=203;
        const bool held=g_frame.fx.kind==(TransitionFxKind)(pad+1);
        const u32 color=held?m::spot():m::ink();
        m::heading(x+8,153,names[pad],color);
        char value[24];int thumb=x0;
        if(pad==0) {
            const int amount=held?g_frame.fx.amount:0;
            if(amount==0)std::snprintf(value,sizeof value,"dry");
            else std::snprintf(value,sizeof value,"%s %d%%",amount<0?"lp":"hp",(amount<0?-amount:amount)*100/32767);
            thumb=x+48+amount*40/32767;

        } else if(pad==1) {
            std::snprintf(value,sizeof value,"1/4 / %d%%",(held?g_frame.fx.amount:g_echo_amount)*100/32767);
            thumb=x0+(held?g_frame.fx.amount:g_echo_amount)*80/32767;
        } else {
            const int division=clamp(g_frame.fx.roll_division,0,3);
            std::snprintf(value,sizeof value,"%s / %s",held?(g_frame.fx.ready?"held":"wait"):"off",divisions[held?division:g_roll_division]);
            thumb=x0+(held?division:g_roll_division)*80/3;

        }
        ui::text(x+8,183,value,held?color:m::ghost());
        if(held)ui::line(x0,148,x1,148,m::spot());
        module_fader::horizontal(x0,x1,y,thumb,thumb,{false,g_return.active(),!held},pad==0?x+48:-1);
    }
}
}

bool active() { return g_open; }
void forget_launch_tracking() {g_queued_launch={};}
void open() {
    finish_return();touch_release(false);g_open=true;g_blocked=false;g_launch_key_frame=false;capture();
    if(g_frame.rhythm.active) {
        g_bank[0]=g_frame.rhythm.source;g_bank[1]=g_frame.rhythm.destination;
    } else if(g_origin_bank!=g_frame.playing) {
        g_bank[0]=g_frame.playing;g_bank[1]=(g_bank[0]+1)%4;
    }
    g_origin_bank=g_frame.playing;g_rhythm_seen=g_frame.rhythm.active;
    if(g_mode!=Mode::Rhythm && (!g_frame.scenes_ready || !g_frame.sound_linked ||
       (g_mode==Mode::Both && g_frame.sound_position!=rhythm_position())))g_mode=Mode::Rhythm;
    g_position=g_mode==Mode::Sound?g_frame.sound_position:rhythm_position();capture();
}
void close() {finish_return();touch_release(false);release_fx();g_open=false;}
void reset() {
    close();g_blocked=false;g_mode=Mode::Rhythm;g_position=0;g_origin_bank=-1;g_notice_frames=0;g_notice=nullptr;
    g_echo_amount=16384;g_roll_division=2;g_launch_key_frame=false;
    // Navigation may reset the view while a queued launch still owns playback.
    // Only successful project replacement forgets its completion tracking.
    // Main uses this on navigation as well as patch swaps. The engine owns
    // transient rhythm lifetime; a view reset must not silently undo a transition.
}
void touch_down(int x,int y) {
    touch_release(false);if(!allowed())return;
    // L owns only the fader: no new endpoints, scenes, FX or keep-bank action.
    if(g_return.active() && !hit(x,y,8,124,304,22))return;
    const int tab=performance_tabs::target(x,y);
    if(tab==performance_tabs::Order) {close();order::open();return;}
    if(tab==performance_tabs::Perform) {close();return;}
    if(tab==performance_tabs::Banks)return;
    if(g_frame.chain_running && hit(x,y,96,94,128,20)) {g_gesture=Gesture::TakeOver;g_drag=Drag::PerformanceMorph;return;}
    for(int side=0;side<2;++side)if(hit(x,y,side?244:8,26,side?68:60,98)) {
        g_gesture=side?Gesture::Destination:Gesture::Source;g_candidate=g_bank[side];
        g_anchor_y=y;g_start_x=x;g_drag=Drag::PerformanceMorph;return;
    }
    if(hit(x,y,76,28,54,31)) {select_mode(Mode::Rhythm);return;}
    if(hit(x,y,132,28,50,31)) {select_mode(Mode::Sound);return;}
    if(hit(x,y,184,28,52,31)) {select_mode(Mode::Both);return;}
    if(hit(x,y,8,124,304,22)) {
        g_gesture=Gesture::Rail;g_rail_x=x;g_rail_position=g_position;g_drag=Drag::PerformanceMorph;return;
    }
    for(int pad=0;pad<3;++pad)if(hit(x,y,8+104*pad,148,96,68)) {start_fx(pad,x);return;}
    if(hit(x,y,8,218,76,22)) {release_fx();notice("fx released / echo tail may finish");capture();return;}
    if(hit(x,y,88,218,148,22)) {begin_launch();return;}
    if(hit(x,y,244,218,68,22))g_gesture=Gesture::Keep;
}
void touch_drag(int x,int y) {
    if(!allowed()) {touch_release(false);return;}
    if(g_gesture==Gesture::TakeOver && !hit(x,y,96,94,128,20)) {touch_release(false);return;}
    if(g_gesture==Gesture::Keep && !hit(x,y,244,218,68,22)) {g_gesture=Gesture::None;return;}
    if(g_gesture==Gesture::Launch && !hit(x,y,88,218,148,22)) {touch_release(false);return;}
    const int side=endpoint();
    if(side>=0) {
        if(x<g_start_x-36 || x>g_start_x+36) {touch_release(false);notice("swipe cancelled");return;}
        // 18px threshold, 24px per bank, 12px reversal hysteresis. Saturating
        // ends re-anchor: no wrap and no invisible overscroll debt. No dpad.
        while(g_anchor_y-y>=18 || y-g_anchor_y>=18) {
            const int delta=g_anchor_y>y?1:-1;
            const int next=clamp(g_candidate+delta,0,3);
            if(next==g_candidate) {g_anchor_y=y;break;}
            g_candidate=next;g_anchor_y-=delta*24;
        }
    } else if(g_gesture==Gesture::Fx) {
        // Capture never transfers to another pad/rail while the stylus is held.
        // Escaping the pad band vertically cancels until a fresh touch-down.
        if(y<148 || y>=216) {touch_release(false);return;}
        slide_fx(x);
    } else if(g_gesture==Gesture::Rail) {
        const int delta=(x-g_rail_x)*32767;
        // Symmetric nearest rounding: two half-width drags must reach the true
        // endpoint, not 32766 displayed as100% but refused by keep-bank.
        set_position(g_rail_position+(delta+(delta<0?-140:140))/280);
    }
    capture();
}
void touch_release(bool commit) {
    const int side=endpoint();
    if(commit && allowed()) {
        if(side>=0)commit_bank(side);
        else if(g_gesture==Gesture::Keep)keep_bank();
        else if(g_gesture==Gesture::Launch)commit_launch();
        else if(g_gesture==Gesture::TakeOver && g_eng && g_audio) {
            platform::AudioOut::Guard guard(*g_audio);
            if(g_eng->take_over_pattern_chain()) {
                g_bank[0]=g_eng->active_pattern;g_origin_bank=g_bank[0];
                g_position=0;g_mode=Mode::Rhythm;
                notice("manual live / order paused");
            }
        }
    }
    if(g_gesture==Gesture::Fx)release_fx();
    g_gesture=Gesture::None;g_launch_press.owner=nullptr;
    if(g_drag==Drag::PerformanceMorph)g_drag=Drag::None;
    if(g_open)capture();
}
void buttons(u32 down,u32 held,u32 up) {
    g_launch_key_frame=(down&~(KEY_TOUCH|KEY_ZL))!=0;
    if(g_launch_key_frame && g_gesture==Gesture::Launch)touch_release(false);
    const bool blocked=!g_open || g_view!=View::Performance || g_overlay!=Overlay::None || nav_is_open() ||
        (held&(KEY_R|KEY_SELECT|KEY_B)) || (down&(KEY_START|KEY_SELECT|KEY_B));
    bool stale=false;
    if(g_return.active()) {
        if(g_eng && g_audio) {platform::AudioOut::Guard guard(*g_audio);stale=!g_return.matches(*g_eng);}
        else stale=true;
    }
    if(g_return.active() && (blocked || stale || (up&KEY_L) || !(held&KEY_L))) {
        finish_return();g_blocked=true;return;
    }
    g_blocked=blocked;
    if(g_blocked) {touch_release(false);return;}
    if((down&KEY_L) && (held&KEY_L) && !(up&KEY_L) && g_eng && g_audio) {
        if(g_gesture!=Gesture::Rail)touch_release(false);
        platform::AudioOut::Guard guard(*g_audio);g_return.capture(*g_eng);g_return_position=g_position;
    }
    if(held&KEY_L) {
        g_blocked=!g_return.active();
        if(g_blocked)touch_release(false);
    }
    // No chain trigger/mute, capture or bank-selection shortcuts leak through.
}
void tick() {
    poll_queued_launch(); // completion remains visible even after leaving banks
    if(g_notice_frames>0)--g_notice_frames;
    if(!g_open)return;
    if(g_view!=View::Performance || g_overlay!=Overlay::None || nav_is_open()) {finish_return();touch_release(false);}
    capture();
    if(g_gesture==Gesture::Fx && (g_frame.fx.epoch!=g_fx_epoch || g_frame.fx.kind!=g_fx_kind))touch_release(false);
    if((g_rhythm_seen && !g_frame.rhythm.active) || g_frame.playing!=g_origin_bank) {
        // Expected auto-bank advances reset only the rail/candidates, not an
        // unrelated pad held by the same stylus. Epoch/lifecycle checks above
        // still cancel stale FX, and a manual/external launch still releases.
        const bool keep_fx=g_frame.chain_running&&g_gesture==Gesture::Fx&&
            g_frame.fx.epoch==g_fx_epoch&&g_frame.fx.kind==g_fx_kind;
        finish_return();if(!keep_fx)touch_release(false);g_bank[0]=g_frame.playing;g_bank[1]=(g_bank[0]+1)%4;
        g_origin_bank=g_frame.playing;g_mode=Mode::Rhythm;g_position=0;
        notice("bank playback changed / rail reset");capture();
    }
    g_rhythm_seen=g_frame.rhythm.active;
}
void draw() {
    m::paper(320);
    performance_tabs::draw(performance_tabs::Banks);
    char text[48];
    draw_bank(0);draw_bank(1);
    const char* modes[]={"rhythm","sound","both"};const int xs[]={84,143,195};
    for(int i=0;i<3;++i) {
        const bool blocked=mode_block_reason((Mode)i)!=nullptr;
        ui::text(xs[i],41,modes[i],(int)g_mode==i?m::spot():blocked?m::ghost():m::ink());
        if(blocked)ui::text(xs[i]+ui::text_w(modes[i])+2,41,"*",m::ghost());
        if((int)g_mode==i)ui::line(xs[i],53,xs[i]+ui::text_w(modes[i])-1,53,m::spot());
    }
    // Explain readiness before a failed tap; mode selection never snaps sound.
    const char* hint=!g_frame.scenes_ready?"a/b: capture in mixer":
        !g_frame.sound_linked?"a/b: align in mixer":
        g_frame.sound_position!=rhythm_position()?"both: match positions":"a/b scenes ready";
    ui::text(160-ui::text_w(hint)/2,59,hint,m::ghost());
    const int percent=position_percent(g_position);
    std::snprintf(text,sizeof text,"%d%%",percent);
    ui::text(160-ui::text_w(text,2)/2,73,text,m::spot(),2);
    const char* link=g_return.active()?"release l / return point":g_frame.chain_running?"take over":
        g_mode==Mode::Rhythm?"sound held":g_mode==Mode::Sound?"a/b scenes / rhythm held":"banks + a/b scenes";
    ui::text(160-ui::text_w(link)/2,100,link,g_frame.chain_running?m::spot():m::ghost());
    if(g_frame.chain_running&&!g_return.active())ui::rect(160-ui::text_w(link)/2,111,ui::text_w(link),1,m::spot());
    const char* keep_hint=g_return.active()?"release l":g_frame.pending<NUM_PATTERNS?"bank queued":
        !g_frame.rhythm.active?"launch below":keep_endpoint()?"at bank edge":"rail to edge";
    ui::text(160-ui::text_w(keep_hint)/2,115,keep_hint,m::ghost());
    const int thumb=20+g_position*280/32767;
    module_fader::horizontal(20,300,134,thumb,thumb,{},160);
    if(g_return.active())ui::frame(18+g_return_position*280/32767,141,5,4,m::spot());
    draw_fx();
    if(g_notice_frames>0 && g_notice)ui::text(8,213,g_notice,m::spot());
    else if(g_frame.pending<NUM_PATTERNS) {
        std::snprintf(text,sizeof text,"bank %d queued / @%s",g_frame.pending+1,launch_when(g_frame.quantize));
        ui::text(8,213,text,m::spot());
    } else if(g_frame.rhythm.active)ui::text(8,213,keep_endpoint()?"keep bank before launch":"rail to edge / keep before launch",m::ghost());
    else if(g_frame.chain_running)ui::text(8,213,"order running / take over for rhythm",m::ghost());
    else if(g_frame.fx.releasing)ui::text(8,213,"releasing fx",m::ghost());
    else if(g_frame.fx.echo_tail)ui::text(8,213,"echo tail",m::spot());
    else if(g_frame.fx.kind==TransitionFxKind::None)ui::text(8,213,"swipe banks / launch below",m::ghost());
    else ui::text(8,213,"temporary / lift to release",m::ghost());
    ui::text(8,229,"release fx",g_return.active()?m::ghost():m::spot());
    const bool pending=g_frame.pending<NUM_PATTERNS;
    if(g_return.active())std::snprintf(text,sizeof text,"release l");
    else if(pending)std::snprintf(text,sizeof text,"cancel queue");
    else if(!g_frame.rhythm.active && g_frame.bank[1]==g_frame.playing && !g_frame.chain_running)
        std::snprintf(text,sizeof text,"playing %d",g_frame.playing+1);
    else std::snprintf(text,sizeof text,"launch %d @%s",g_frame.bank[1]+1,launch_when(g_frame.running?g_frame.quantize:LaunchQuantize::Immediate));
    const bool launch_ready=!g_return.active() && !g_frame.rhythm.active &&
        (pending || g_frame.bank[1]!=g_frame.playing || g_frame.chain_running);
    ui::text(162-ui::text_w(text)/2,229,text,launch_ready?m::spot():m::ghost());
    if(g_gesture==Gesture::Launch)ui::line(100,239,224,239,m::spot());
    ui::text(258,229,"keep bank",!g_return.active()&&keep_endpoint()?m::spot():m::ghost());
}
}
