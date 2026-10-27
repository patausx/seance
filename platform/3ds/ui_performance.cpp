#include "ui_performance.h"
#include "ui_transition.h"
#include "ui_order.h"
#include "performance_tabs.h"
#include "performance_return.h"
#include "ui_state.h"
#include "ui_theme.h"
#include "ui_palette.h"
#include "ui_nav.h"
#include "menu_style.h"
#include "inspector_gothic.h"
#include "audio_out.h"
#include "../../core/ui/draw.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace seance;
namespace seance::platform::ui3ds {
namespace {
namespace m = menu_style;
constexpr int FADER_TOP = 80, FADER_BOTTOM = 163, FADER_HEIGHT = FADER_BOTTOM-FADER_TOP;
constexpr int XF_X = 60, XF_W = 200;
int strip_x(int chain) { return 8 + chain * 78; }
int g_fader_chain = -1, g_fader_y = 0, g_fader_position = 0;
q15 g_fader_initial = 0;
bool g_input_blocked = false;
performance_return::Point g_return;
int g_pending_pad = -1, g_pending_scene = -1;
u32 g_pending_since = 0;
bool g_scene_captured = false, g_scene_ready = false;
int g_morph_flash = 0;
const char* g_morph_notice = nullptr;
int g_feedback_scene = -1;
u32 g_feedback_since = 0;
bool g_feedback_ok = false;
q15 g_peak_start[NUM_CHAINS] = {}, g_peak_marker[NUM_CHAINS] = {};
u32 g_peak_since[NUM_CHAINS] = {};
// Presentation only. Published sequencer hit ages, plus a separate manual-tap
// confirmation; never derive a strike from step movement or a made-up waveform.
uint64_t g_manual_since[NUM_CHAINS] = {};
bool g_manual_active[NUM_CHAINS] = {};
u8 g_strike_alpha[NUM_CHAINS] = {};
uint64_t g_mute_since[NUM_CHAINS] = {};
bool g_mute_known[NUM_CHAINS] = {}, g_last_mute[NUM_CHAINS] = {}, g_mute_active[NUM_CHAINS] = {};
u8 g_mute_alpha[NUM_CHAINS] = {};
static_assert(sizeof(g_manual_since)+sizeof(g_manual_active)+sizeof(g_strike_alpha)+
    sizeof(g_mute_since)+sizeof(g_mute_known)+sizeof(g_last_mute)+sizeof(g_mute_active)+sizeof(g_mute_alpha)<=96,
    "performance motion must remain small fixed UI state");

u32 mix_color(u32 a, u32 b, unsigned amount) {
    u32 out=0xff000000u;
    for(unsigned shift=0;shift<24;shift+=8)
        out|=((((a>>shift)&255)*(255-amount)+((b>>shift)&255)*amount+127)/255)<<shift;
    return out;
}
void center_text(int cx,int y,const char* text,u32 ink) {ui::text(cx-ui::text_w(text)/2,y,text,ink);}
void column_bands(bool top) {
    const int offset=top?40:0,y0=top?39:24,y1=top?240:193;
    for(int c=0;c<NUM_CHAINS;++c) {
        const int x=offset+4+c*78;
        const bool selected=c==g_sel_chain;
        if(selected)ui::rect(x+1,y0,74,y1-y0,mix_color(m::paper_color(120),m::spot(),10));
        const u32 edge=selected?m::spot():m::branch();
        ui::rect(x,y0,1,y1-y0,edge);ui::rect(x+75,y0,1,y1-y0,edge);
        if(top)ui::rect(x,y0,76,1,edge);
    }
}
void strike_accent(int c,int cx,int y) {
    if(g_strike_alpha[c])ui::rect(cx-11,y,23,1,
        (m::spot()&0x00ffffffu)|(u32(g_strike_alpha[c])<<24));
}
void reset_motion() {
    for(int c=0;c<NUM_CHAINS;++c){
        g_manual_active[c]=false;g_strike_alpha[c]=0;
        g_mute_known[c]=g_mute_active[c]=false;g_mute_alpha[c]=0;
    }
}

void tick_peak_markers() {
    // Presentation only: latest published post-volume peaks, never PCM/RMS/clip.
    // 60 UI frames held, then a fixed 120-frame full-scale fall (~1s + 2s).
    for(int c=0;c<NUM_CHAINS;++c) {
        const u32 age=g_ui_frame-g_peak_since[c];
        const u32 fall=age>60?std::min<u32>(age-60,120):0;
        int level=std::max(0,(int)g_peak_start[c]-(int)(fall*32767/120));
        const int live=g_ui_snapshot.chain_mute[c]?0:std::max(0,(int)g_ui_snapshot.chain_peak[c]);
        if(live>0&&live>=level) {level=live;g_peak_start[c]=(q15)live;g_peak_since[c]=g_ui_frame;}
        g_peak_marker[c]=(q15)level;
    }
}

// Quadratic travel gives more physical room to quiet fades; exact silence/unity.
int fader_position(q15 volume) {
    return (int)std::lround(std::sqrt(std::max(0, (int)volume)/32767.0)*32767.0);
}
void level_text(char* out, size_t n, q15 volume) {
    if (volume <= 0) std::snprintf(out,n,"off");
    else {
        const double db=20.0*std::log10(volume/32767.0);
        std::snprintf(out,n,"%.1fdb",db>-0.05?0.0:db);
    }
}
void notice(const char* text) { g_morph_notice=text; g_morph_flash=120; }
bool input_allowed() { return g_view==View::Performance && g_overlay==Overlay::None && !nav_is_open() && !g_input_blocked; }
void finish_return() {
    if(!g_return.active())return;
    bool restored=false;
    if(g_eng && g_audio) {
        platform::AudioOut::Guard guard(*g_audio);restored=g_return.restore(*g_eng);
    } else g_return.reset();
    // The still-held stylus cannot immediately write the excursion back.
    performance_view_touch_release(false);
    if(!restored)notice("return cancelled / context changed");
}
void trigger_chain(int chain) {
    if (chain < 0 || chain >= NUM_CHAINS) return;
    g_sel_chain=chain;
    {
        platform::AudioOut::Guard guard(*g_audio);
        g_eng->request_chain_trigger(chain,LaunchQuantize::Immediate);
    }
    g_manual_since[chain]=osGetTime();g_manual_active[chain]=true;
}
void toggle_mute(int chain) {
    platform::AudioOut::Guard guard(*g_audio);
    g_eng->chains[chain].mute=!g_eng->chains[chain].mute;
    mark_project_mutated();
}
void set_fader(int ty) {
    const int chain=g_fader_chain;
    if (chain<0 || !input_allowed()) return;
    const int position=std::max(0,std::min(32767,g_fader_position+(g_fader_y-ty)*32767/FADER_HEIGHT));
    const q15 next=ty==g_fader_y?g_fader_initial:(q15)((int64_t)position*position/32767);
    platform::AudioOut::Guard guard(*g_audio);
    if (next==g_eng->chains[chain].volume) return;
    g_eng->set_chain_volume_smooth(chain,next);
    if(g_return.active())g_return.levels_changed|=1u<<chain;
    else mark_project_mutated();
}
void set_morph(int tx) {
    const q15 next=norm_h(tx,XF_X,XF_W,false);
    platform::AudioOut::Guard guard(*g_audio);
    if (!g_eng->global_scene_topology_compatible()) {
        notice("hold a then b / same cards required"); return;
    }
    if(g_return.active()) {
        // Temporary sound moves preserve live step locks as well as all levels.
        if(g_eng->apply_transition_scene_morph(next))g_return.sound_changed=true;
        return;
    }
    // The four physical mixer levels own volume here. Keep the legacy stored
    // scene format/law intact, but never let this surface's morph undo a fade.
    q15 volumes[NUM_CHAINS];
    for (int c=0;c<NUM_CHAINS;++c) volumes[c]=g_eng->chains[c].volume;
    g_eng->apply_global_scene_morph(next);
    for (int c=0;c<NUM_CHAINS;++c) g_eng->chains[c].volume=volumes[c];
    mark_project_mutated();
}
void capture_scene(int which) {
    g_feedback_scene=which;g_feedback_since=g_ui_frame;g_feedback_ok=false;
    platform::AudioOut::Guard guard(*g_audio);
    if (which==1) {
        bool a=true;for(int c=0;c<NUM_CHAINS;++c)a&=g_eng->scenes[c][0].valid;
        if (!a || !g_eng->scene_topology_compatible(0)) { notice("capture a first / keep the same cards");return; }
    }
    g_eng->capture_global_scene(which);
    // Register the captured endpoint without reapplying sound or changing levels.
    for(int c=0;c<NUM_CHAINS;++c)g_eng->scene_morph[c]=which?32767:0;
    notice(which ? "b saved / morph keeps mixer levels" : "a saved / change sound then hold b");
    mark_project_mutated();
    g_scene_ready=g_eng->global_scene_topology_compatible();
    g_feedback_ok=true;
}
void kill_legacy_performance_outputs() {
    platform::AudioOut::Guard guard(*g_audio);
    g_eng->set_sequencer_fill(false);
    for(int c=0;c<NUM_CHAINS;++c)g_eng->set_live_retrigger(c,0,false);
    g_eng->apply_global_fx_mask(0);
}
}

void performance_view_buttons(u32 down,u32 held,u32 up) {
    if(order::active()) {order::buttons(down,held,up);return;}
    if(transition::active()) {transition::buttons(down,held,up);return;}
    const bool blocked=g_view!=View::Performance || g_overlay!=Overlay::None || nav_is_open() ||
        (held&(KEY_R|KEY_SELECT|KEY_B)) || (down&(KEY_START|KEY_SELECT|KEY_B));
    bool stale=false;
    if(g_return.active()) {
        if(g_eng && g_audio) {platform::AudioOut::Guard guard(*g_audio);stale=!g_return.matches(*g_eng);}
        else stale=true;
    }
    if(g_return.active() && (blocked || stale || (up&KEY_L) || !(held&KEY_L))) {
        finish_return();g_input_blocked=true;return;
    }
    g_input_blocked=blocked;
    if(g_input_blocked) {performance_view_touch_release(false);return;}
    if((down&KEY_L) && (held&KEY_L) && !(up&KEY_L) && g_eng && g_audio) {
        platform::AudioOut::Guard guard(*g_audio);g_return.capture(*g_eng);
        g_pending_pad=g_pending_scene=-1;g_scene_captured=false;
    }
    if(held&KEY_L) {
        // Only a fresh local L press owns a return point; no rearming after nav.
        g_input_blocked=!g_return.active();
        if(g_input_blocked)performance_view_touch_release(false);
        return; // L only owns faders, not trigger/mute/capture shortcuts.
    }
    if (!input_allowed()) return;
    if (down&KEY_LEFT) g_sel_chain=(g_sel_chain+NUM_CHAINS-1)%NUM_CHAINS;
    if (down&KEY_RIGHT)g_sel_chain=(g_sel_chain+1)%NUM_CHAINS;
    if (down&KEY_A)trigger_chain(g_sel_chain);
    if (down&KEY_X)toggle_mute(g_sel_chain);
}
void performance_view_touch_down(int tx,int ty) {
    if(order::active()) {order::touch_down(tx,ty);return;}
    if(transition::active()) {transition::touch_down(tx,ty);return;}
    performance_view_touch_release(false);
    if (!input_allowed()) return;
    if(!g_return.active()) {
        const int tab=performance_tabs::target(tx,ty);
        if(tab==performance_tabs::Order) {order::open();return;}
        if(tab==performance_tabs::Banks) {transition::open();return;}
        if(tab==performance_tabs::Perform)return;
    }
    for(int c=0;c<NUM_CHAINS;++c) {
        const int x=strip_x(c);
        if(!g_return.active() && hit(tx,ty,x,24,70,29)) { g_sel_chain=c;g_pending_pad=c;return; }
        if(hit(tx,ty,x,74,70,97)) {
            g_sel_chain=c;g_fader_chain=c;g_fader_y=ty;
            { platform::AudioOut::Guard guard(*g_audio);g_fader_initial=g_eng->chains[c].volume; }
            g_fader_position=fader_position(g_fader_initial);
            // Reuse the existing routed performance drag; private ownership
            // distinguishes the vertical fader from the horizontal morph.
            g_drag=Drag::PerformanceMorph;return;
        }
        if(!g_return.active() && hit(tx,ty,x,175,70,18)) {g_sel_chain=c;toggle_mute(c);return;}
    }
    for(int s=0;s<2;++s)if(!g_return.active() && hit(tx,ty,s?270:8,199,42,25)) {
        g_pending_scene=s;g_pending_since=g_ui_frame;g_feedback_scene=-1;return;
    }
    if(hit(tx,ty,XF_X,199,XF_W+1,25)) {g_drag=Drag::PerformanceMorph;set_morph(tx);}
}
void performance_view_touch_hold(int tx,int ty) {
    if(order::active()) {order::touch_hold(tx,ty);return;}
    if(transition::active()) {transition::touch_drag(tx,ty);return;}
    if(!input_allowed()) {performance_view_touch_release(false);return;}
    if(g_pending_pad>=0&&!hit(tx,ty,strip_x(g_pending_pad),24,70,29))g_pending_pad=-1;
    if(g_pending_scene<0||g_scene_captured)return;
    if(!hit(tx,ty,g_pending_scene?270:8,199,42,25)) {g_pending_scene=-1;return;}
    if(g_ui_frame-g_pending_since>=24) {g_scene_captured=true;capture_scene(g_pending_scene);}
}
void performance_view_touch_release(bool commit_tap) {
    if(order::active())order::touch_release(commit_tap);
    if(transition::active())transition::touch_release(commit_tap);
    if(commit_tap&&g_pending_pad>=0&&input_allowed())trigger_chain(g_pending_pad);
    g_pending_pad=g_pending_scene=g_fader_chain=-1;g_scene_captured=false;
    if(g_drag==Drag::PerformanceMorph)g_drag=Drag::None;
}
void performance_view_touch_drag(int tx,int ty) {
    if(order::active()) {order::touch_drag(tx,ty);return;}
    if(transition::active()) {transition::touch_drag(tx,ty);return;}
    if(!input_allowed()) {performance_view_touch_release(false);return;}
    if(g_drag!=Drag::PerformanceMorph)return;
    if(g_fader_chain>=0)set_fader(ty);else set_morph(tx);
}
void performance_view_draw_top() {
    if(order::active()) {order::draw_top();return;}
    m::paper(400);m::heading(40,8,"perform",m::ink());
    char text[48];
    std::snprintf(text,sizeof text,"%d bpm / %s",g_ui_snapshot.bpm,g_ui_snapshot.transport_playing?"play":"stop");
    ui::text(359-ui::text_w(text),9,text,m::ink());
    const auto& transition=g_ui_snapshot.pattern_transition;
    if(transition.active)std::snprintf(text,sizeof text,"bank %d>%d / %d%%",transition.source+1,transition.destination+1,transition::position_percent(transition.position));
    else if(g_ui_snapshot.pending_pattern<NUM_PATTERNS)std::snprintf(text,sizeof text,"bank %d / queued %d",g_ui_snapshot.active_pattern+1,g_ui_snapshot.pending_pattern+1);
    else std::snprintf(text,sizeof text,"bank %d / beat %d",g_ui_snapshot.active_pattern+1,
        int(g_ui_snapshot.beats_per_bar?g_ui_snapshot.beat_count%g_ui_snapshot.beats_per_bar:0)+1);
    ui::text(359-ui::text_w(text),24,text,m::ghost());
    column_bands(true);
    for(int c=0;c<NUM_CHAINS;++c) {
        const int x=44+78*c,cx=x+37;
        const bool muted=g_ui_snapshot.chain_mute[c];
        const char number[2]={char('1'+c),0};
        const auto* glyph=inspector_gothic::digit(number[0]);
        // Lower the identity block as a whole; keep the mixer below unchanged.
        inspector_gothic::draw_numeric(cx-(glyph?glyph->width:16)/2,84,number,
            muted?m::ghost():c==g_sel_chain?m::spot():m::ink(),m::spot());
        strike_accent(c,cx,92);
        int count=0;
        for(int slot=0;slot<UNITS_PER_CHAIN;++slot) {
            const auto kind=(UnitKind)g_ui_snapshot.unit_kind[c][slot];
            if(kind==UnitKind::Empty)continue;
            const int y=97+13*count++;
            if(count>1)ui::rect(cx,y-4,1,2,m::branch());
            center_text(cx,y,m::module_name(kind),muted||(g_ui_snapshot.unit_bypass[c]&(1u<<slot))?m::ghost():m::ink());
        }
        const int length=std::max(1,std::min(64,int(g_ui_snapshot.sequence_length[c])));
        const int step=g_ui_snapshot.sequence_step[c];
        const auto direction=(SequenceDirection)g_ui_snapshot.sequence_direction[c];
        const char* arrow=direction==SequenceDirection::Reverse?"<":direction==SequenceDirection::PingPong?"<>":">";
        if(!g_ui_snapshot.sequencer_enabled)std::snprintf(text,sizeof text,"seq off");
        else if(step>=length||transition.active)std::snprintf(text,sizeof text,"--/%02d %s",length,arrow);
        else std::snprintf(text,sizeof text,"%02d/%02d %s",step+1,length,arrow);
        center_text(cx,178,text,m::ghost());
        for(int s=0;s<64;++s) {
            const int gx=x+6+(s%16)*4,gy=190+(s/16)*7;
            if(g_ui_snapshot.sequencer_enabled&&g_ui_snapshot.transport_playing&&!transition.active&&s==step&&step<length) {
                if(muted)ui::frame(gx-1,gy-1,4,5,m::spot());
                else ui::rect(gx-1,gy-1,4,5,m::spot());
            } else ui::rect(gx,gy+1,2,s<length?2:1,s<length?m::ghost():m::branch());
        }
        // Neither "quiet" nor "live" asserts a particular silence/source cause.
        const char* status=!count?"empty":muted?"muted":
            (g_ui_snapshot.pending_triggers&(1u<<c))?"queued":g_ui_snapshot.chain_peak[c]>0?"live":"quiet";
        center_text(cx,224,status,muted?m::spot():m::ghost());
        if(g_mute_alpha[c])ui::rect(cx-19,235,39,1,(m::spot()&0x00ffffffu)|(u32(g_mute_alpha[c])<<24));
    }
}

void performance_view_draw() {
    if(order::active()) {order::draw();return;}
    if(transition::active()) {transition::draw();return;}
    m::paper(320);column_bands(false);
    performance_tabs::draw(performance_tabs::Perform);
    char text[64];
    for(int c=0;c<NUM_CHAINS;++c) {
        const int x=strip_x(c),cx=x+27;
        const bool selected=c==g_sel_chain,muted=g_ui_snapshot.chain_mute[c];
        const u32 ink=muted?m::ghost():m::ink();
        center_text(x+33,32,"trig",selected?m::spot():m::ghost());
        if(selected)ui::rect(x+22,45,23,1,m::spot());
        strike_accent(c,x+33,47);
        level_text(text,sizeof text,g_ui_snapshot.chain_volume[c]);
        center_text(x+33,64,text,ink);
        ui::rect(cx,FADER_TOP,1,FADER_HEIGHT+1,m::branch());
        for(int t=0;t<=4;++t) {
            const int y=FADER_BOTTOM-t*FADER_HEIGHT/4;
            ui::rect(cx-13,y,6,1,m::branch());
        }
        const int thumb=FADER_BOTTOM-fader_position(g_ui_snapshot.chain_volume[c])*FADER_HEIGHT/32767;
        ui::rect(cx,thumb,1,FADER_BOTTOM-thumb+1,muted?m::ghost():m::spot());
        ui::rect(cx-15,thumb-5,31,11,selected?m::spot():ink);
        ui::line(cx-11,thumb,cx+11,thumb,m::paper_color(thumb),1);
        if(g_return.active()) {
            const int home=FADER_BOTTOM-fader_position(g_return.sound[c].volume)*FADER_HEIGHT/32767;
            ui::frame(cx+18,home-2,5,5,m::spot());
        }
        const int peak=std::max(0,std::min(32767,(int)g_ui_snapshot.chain_peak[c]));
        ui::line(x+61,FADER_TOP,x+61,FADER_BOTTOM,m::branch());
        if(peak&&!muted)ui::rect(x+59,FADER_BOTTOM-peak*FADER_HEIGHT/32767,4,std::max(1,peak*FADER_HEIGHT/32767),m::spot());
        // Publication happens after tick in main: never place the history marker
        // below a newer live bar. Drawing itself does not advance any state.
        const int remembered=std::max((int)g_peak_marker[c],muted?0:peak);
        if(remembered)ui::rect(x+56,FADER_BOTTOM-remembered*FADER_HEIGHT/32767,11,2,muted?m::ghost():m::ink());
        if(muted) {ui::rect(x,175,70,18,m::spot());ui::text(x+23,180,"mute",m::paper_color(180));}
        else {ui::frame(x,175,70,18,m::branch());ui::text(x+23,180,"mute",m::ghost());}
        if(g_mute_alpha[c])ui::rect(x+9,194,52,1,(m::spot()&0x00ffffffu)|(u32(g_mute_alpha[c])<<24));
    }
    ui::line(8,197,312,197,m::branch());
    for(int s=0;s<2;++s) {
        bool valid=true;for(int c=0;c<NUM_CHAINS;++c)valid&=(g_ui_snapshot.scene_valid[c]&(1u<<s))!=0;
        const int x=s?270:8;
        const bool pending=g_pending_scene==s&&!g_scene_captured;
        const bool feedback=g_feedback_scene==s&&((g_pending_scene==s&&g_scene_captured)||g_ui_frame-g_feedback_since<60);
        if(feedback) {
            ui::rect(x+1,200,40,23,g_feedback_ok?m::spot():m::branch());
            const u32 ink=(palette::visual_graphite()||g_feedback_ok)?m::paper_color(211):m::ink();
            ui::text(x+7,203,s?"b":"a",ink);
            if(g_feedback_ok) {ui::line(x+26,205,x+30,208,ink);ui::line(x+30,208,x+36,202,ink);}
            else ui::text(x+30,203,"!",ink);
            ui::text(x+6,215,g_feedback_ok?"saved":"retry",ink);
        } else if(pending) {
            const int elapsed=(int)std::min<u32>(23,g_ui_frame-g_pending_since);
            const int fill=elapsed*23/24;
            // Preserve original pink fills; graphite needs dark fills beneath
            // light glyphs at every crossed text row, rather than white tracks.
            if(fill) {ui::rect(x+1,223-fill,40,fill,palette::visual_graphite()?palette::cell():m::branch());ui::rect(x+1,223-fill,40,2,palette::visual_graphite()?palette::subdued():m::spot());}
            char progress[16];std::snprintf(progress,sizeof progress,"%c %d%%",s?'b':'a',elapsed*100/24);
            ui::text(x+6,203,progress,m::ink());ui::text(x+9,215,"hold",m::ink());
        } else {
            ui::text(x+5,203,s?"b":"a",valid?m::spot():m::ghost());ui::text(x+17,203,valid?"set":"---",m::ghost());
            ui::text(x+9,215,"hold",m::ghost());
        }
        ui::frame(x,199,42,25,pending||feedback||valid?m::spot():m::branch());
    }
    int32_t morph=0;for(int c=0;c<NUM_CHAINS;++c)morph+=g_ui_snapshot.scene_morph[c];morph=std::max(0,std::min(32767,(int)(morph/NUM_CHAINS)));
    const u32 color=g_scene_ready?m::spot():m::ghost();
    ui::line(XF_X,215,XF_X+XF_W,215,m::branch(),2);
    for(int t=0;t<=4;++t)ui::line(XF_X+t*XF_W/4,211,XF_X+t*XF_W/4,219,m::branch());
    const int thumb=XF_X+morph*XF_W/32767;ui::rect(thumb-3,209,7,13,color);
    std::snprintf(text,sizeof text,"morph %d%%",transition::position_percent(morph));ui::text(160-ui::text_w(text)/2,200,text,color);
    if(g_return.active()) {
        int home=0;for(int c=0;c<NUM_CHAINS;++c)home+=g_return.morph[c];
        home=XF_X+(home/NUM_CHAINS)*XF_W/32767;
        ui::frame(home-2,224,5,4,m::spot());
        m::footer(320,"l held / release to return","");
    } else if(g_pending_scene>=0&&!g_scene_captured)m::footer(320,"hold to capture / release to cancel","");
    else if(g_morph_flash>0&&g_morph_notice)m::footer(320,g_morph_notice,"");
    else m::footer(320,"slide level / hold a,b save","l return / x mute");
}
void performance_view_tick() {
    if(g_return.active() && (g_view!=View::Performance || g_overlay!=Overlay::None || nav_is_open()))finish_return();
    transition::tick();order::tick();
    if(g_morph_flash>0)--g_morph_flash;
    if(g_view==View::Performance) {
        tick_peak_markers();
        if(g_eng) {platform::AudioOut::Guard guard(*g_audio);g_scene_ready=g_eng->global_scene_topology_compatible();}
    }
}
void performance_view_capture() {
    const uint64_t now=osGetTime();
    if(g_view!=View::Performance||transition::active()||order::active()||g_overlay!=Overlay::None||nav_is_open()) {reset_motion();return;}
    for(int c=0;c<NUM_CHAINS;++c) {
        unsigned alpha=0;
        if(g_manual_active[c]) {
            if(now<g_manual_since[c]||now-g_manual_since[c]>=150)g_manual_active[c]=false;
            else alpha=unsigned((150-(now-g_manual_since[c]))*255/150);
        }
        const auto& hit=g_ui_snapshot.sequence_hit[c];
        if(g_ui_snapshot.transport_playing&&g_ui_snapshot.sequencer_enabled&&
           hit.step<SEQ_STEPS&&hit.bank<NUM_PATTERNS&&hit.duration_ms&&hit.age_ms<hit.duration_ms)
            alpha=std::max(alpha,unsigned(hit.duration_ms-hit.age_ms)*255/hit.duration_ms);
        g_strike_alpha[c]=g_ui_snapshot.chain_mute[c]?0:u8(alpha);
        const bool muted=g_ui_snapshot.chain_mute[c];
        if(g_mute_known[c]&&g_last_mute[c]!=muted){g_mute_since[c]=now;g_mute_active[c]=true;}
        g_last_mute[c]=muted;g_mute_known[c]=true;
        if(now<g_mute_since[c]||now-g_mute_since[c]>=160)g_mute_active[c]=false;
        g_mute_alpha[c]=g_mute_active[c]?u8((160-(now-g_mute_since[c]))*255/160):0;
    }
}
void performance_view_leave() {finish_return();performance_view_touch_release(false);transition::close();order::close();reset_motion();}
void performance_release_all() {performance_view_leave();kill_legacy_performance_outputs();}
void performance_view_reset() {
    finish_return();performance_view_touch_release(false);transition::reset();order::reset();kill_legacy_performance_outputs();reset_motion();
    g_feedback_scene=-1;
    for(int c=0;c<NUM_CHAINS;++c) {g_peak_start[c]=g_peak_marker[c]=0;g_peak_since[c]=g_ui_frame;}
    g_morph_flash=0;g_morph_notice=nullptr;g_scene_ready=false;g_input_blocked=false;performance_view_tick();
}
}
