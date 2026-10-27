#include "ui_state.h"
#include "ui_sigils.h"
#include "ui_patchbay.h"
#include "ui_nav.h"
#include "sigil_surface.h"
#include "ui_chains.h"
#include "menu_style.h"
#include "module_fader_painter.h"
#include "audio_out.h"
#include "../../core/engine/units.h"
#include "../../core/engine/sigil_transform.h"
#include "../../core/ui/draw.h"
#include <cstdio>
#include <cstring>
#include <utility>
#include <algorithm>
#include <type_traits>
using namespace seance;
namespace seance::platform::ui3ds {
// ===================== sigil view =====================
namespace {
bool undo_drawn[NUM_SIGILS] = {};
}

void sigil_snapshot_undo(int idx) {
    for (int i = 0; i < SIGIL_POINTS; ++i) g_sigil_undo[idx][i] = g_eng->sigils[idx].table[i];
    g_sigil_undo_valid[idx] = true;
    undo_drawn[idx] = g_eng->sigils[idx].drawn;
}

void sigil_tool_apply(int tool) {
    if (!g_eng || !g_audio || g_sel_sigil < 0 || g_sel_sigil >= NUM_SIGILS) return;
    q15 candidate[SIGIL_POINTS];
    uint32_t jitter_state = g_ui_rng;
    if (tool == 8) {
        {
            platform::AudioOut::Guard g(*g_audio);
            for (int i = 0; i < SIGIL_POINTS; ++i) candidate[i] = g_eng->sigils[g_sel_sigil].table[i];
        }
        jitter_state = jitter_sigil_curve(candidate, jitter_state, 4096);
    }
    platform::AudioOut::Guard g(*g_audio);
    Sigil& s = g_eng->sigils[g_sel_sigil];
    if (tool != 0 && tool != 6 && tool != 7 && tool != 8) sigil_snapshot_undo(g_sel_sigil);
    bool mutated = true;
    switch (tool) {
        case 0:
            if (g_sigil_undo_valid[g_sel_sigil]) {
                mutated = s.drawn != undo_drawn[g_sel_sigil] ||
                    std::memcmp(s.table, g_sigil_undo[g_sel_sigil], sizeof s.table) != 0;
                q15 tmp[SIGIL_POINTS];
                for (int i = 0; i < SIGIL_POINTS; ++i) { tmp[i] = s.table[i]; s.table[i] = g_sigil_undo[g_sel_sigil][i]; }
                for (int i = 0; i < SIGIL_POINTS; ++i) g_sigil_undo[g_sel_sigil][i] = tmp[i];
                const bool drawn = s.drawn;
                s.drawn = undo_drawn[g_sel_sigil];
                undo_drawn[g_sel_sigil] = drawn;
            } else mutated = false;
            break;
        case 1: {
            q15 tmp[SIGIL_POINTS];
            tmp[0] = s.table[0]; tmp[SIGIL_POINTS - 1] = s.table[SIGIL_POINTS - 1];
            for (int i = 1; i < SIGIL_POINTS - 1; ++i)
                tmp[i] = (q15)(((int32_t)s.table[i - 1] + s.table[i] * 2 + s.table[i + 1]) / 4);
            for (int i = 0; i < SIGIL_POINTS; ++i) s.table[i] = tmp[i];
            break;
        }
        case 2: for (auto& v : s.table) v = (q15)(((int32_t)v / 4096) * 4096); break;
        case 3: for (auto& v : s.table) v = (q15)(v == -32768 ? 32767 : -v); break;
        case 4:
            for (int i = 0; i < SIGIL_POINTS / 2; ++i) { q15 t = s.table[i]; s.table[i] = s.table[SIGIL_POINTS - 1 - i]; s.table[SIGIL_POINTS - 1 - i] = t; }
            break;
        case 5: {
            q15 tmp[8]; for (int i = 0; i < 8; ++i) tmp[i] = s.table[SIGIL_POINTS - 8 + i];
            for (int i = SIGIL_POINTS - 1; i >= 8; --i) s.table[i] = s.table[i - 8];
            for (int i = 0; i < 8; ++i) s.table[i] = tmp[i];
            break;
        }
        case 6:
            for (int i = 0; i < SIGIL_POINTS; ++i) g_sigil_clip[i] = s.table[i];
            g_sigil_clip_valid = true; mutated = false; break;
        case 7:
            if (g_sigil_clip_valid) {
                sigil_snapshot_undo(g_sel_sigil);
                for (int i = 0; i < SIGIL_POINTS; ++i) s.table[i] = g_sigil_clip[i];
                s.drawn = true;
            } else mutated = false;
            break;
        case 8: {
            sigil_snapshot_undo(g_sel_sigil);
            g_ui_rng = jitter_state;
            for (int i = 0; i < SIGIL_POINTS; ++i) s.table[i] = candidate[i];
            s.drawn = true;
            break;
        }
        case 9: s.clear(); break;
        default: mutated = false; break;
    }
    if (mutated) mark_project_mutated();
}

namespace {
namespace m = menu_style;
using sigil_surface::Painter;
constexpr int X0=20,X1=300,ZERO=93,TOP=64,BOTTOM=122;
constexpr const char* SHAPES[]={"drawn","sine","triangle","saw","square","random","smooth"};
constexpr const char* MODES[]={"loop","shot","step","rand","smooth","follow"};
constexpr const char* DIVS[]={"4b","2b","1b","1/2","1/4","1/8"};
enum class Menu:uint8_t {None,Sigil,Shape,Mode,Polarity,Restart,Mod};
enum class Kind:uint8_t {None,Canvas,Rate,Depth,Phase,ModDepth};
struct Route {char name[43]={};q15 depth=0;bool enabled=true;};
struct Frame {
    q15 table[SIGIL_POINTS]={},wave[SIGIL_POINTS]={};Route routes[3];SigilConfig config;
    q15 value=0,depth=0,rate=0,mod_depth=0;
    uint32_t phase=0,increment=0;
    uint16_t route_count=0,assignments[NUM_SIGILS]={};
    uint8_t sigil=0,mode=0,division=0,chain=0,menu_item=0;
    ModSource source=ModSource::None;Menu menu=Menu::None;
    bool valid=false,sync=false,drawn=false,undo=false,periodic=false,running=false,follow_active=false;
};
static_assert(sizeof(Frame)<=1024,"bounded eight-sigil frame");
static_assert(std::is_trivially_copyable<Frame>::value,"no live pointers in draw");
Frame frame;
sigil_surface::Cache& blur=sigil_surface::bottom_cache;
Menu menu=Menu::None;int menu_item=0;bool blur_refresh=false;
Engine* menu_engine=nullptr;platform::AudioOut* menu_audio=nullptr;int menu_sigil=-1;
uint32_t blur_colors[4]={};
struct Gesture {
    Kind kind=Kind::None;Engine* engine=nullptr;platform::AudioOut* audio=nullptr;
    int sigil=-1,x=0,y=0,initial=0;SigilMode mode=SigilMode::Loop;SigilShape shape=SigilShape::Drawn;
    bool sync=false,relative=false;
} gesture;
int clamp(int v,int lo,int hi){return std::max(lo,std::min(hi,v));}
int percent(q15 v){return int(v)*100/32767;}
bool legacy_random(int mode){return mode==int(SigilMode::RandomStep)||mode==int(SigilMode::RandomSmooth);}
bool ready(){return g_eng&&g_audio&&g_view==View::Sigils&&g_overlay==Overlay::None&&!nav_is_open()&&g_sel_sigil>=0&&g_sel_sigil<NUM_SIGILS;}
bool hit(int x,int y,int l,int t,int r,int b){return x>=l&&x<r&&y>=t&&y<b;}
void lower(char* out,size_t n,const char* in){size_t i=0;for(;in&&in[i]&&i+1<n;++i)out[i]=in[i]>='A'&&in[i]<='Z'?char(in[i]+'a'-'A'):in[i];out[i]=0;}
const char* source_name(ModSource src){static const char* names[]={"a","b","c","d","e","f","g","h"};const int i=mod_source_sigil(src);return i>=0?names[i]:src==ModSource::EnvFollow?"env":"none";}
int legacy_millihz(q15 raw){return int((3277LL+((int64_t(raw)*raw)>>15)*80)*1000/65536);}
SigilConfig editable_config(){auto c=g_eng->sigil_config[g_sel_sigil];if(c.legacy){c.legacy_rate=true;c.legacy=false;}c.enabled=true;return c;}
void feedback(Kind kind);
void clear_trail();
void config_change(const SigilConfig& c){
    const auto old=g_eng->sigil_config[g_sel_sigil];
    if(!g_eng->set_sigil_config(g_sel_sigil,c))return;
    mark_project_mutated();clear_trail();
    if(old.phase_degrees!=c.phase_degrees)feedback(Kind::Phase);
    if(old.rate_millihz!=c.rate_millihz||old.legacy_rate!=c.legacy_rate)feedback(Kind::Rate);
}
int mod_count(){return g_sel_sigil+2;}
ModSource mod_at(int row){return row==0?ModSource::None:row<=g_sel_sigil?sigil_mod_source(row-1):ModSource::EnvFollow;}
int menu_count(Menu m){switch(m){case Menu::Sigil:return NUM_SIGILS;case Menu::Shape:return 7;case Menu::Mode:return 6;case Menu::Mod:return mod_count();case Menu::None:return 0;default:return 2;}}
struct MenuLayout {int branch,x,y,step,left,right;};
MenuLayout layout(Menu m){switch(m){case Menu::Sigil:return {143,157,55,18,132,254};case Menu::Shape:return {62,76,78,20,51,178};case Menu::Mode:return {232,246,78,20,222,314};case Menu::Polarity:return {20,34,141,20,10,132};case Menu::Restart:return {176,190,141,20,165,308};case Menu::Mod:return {66,80,38,18,55,159};default:return {};}}
// UI-only, fixed-size motion. Inputs queue feedback; capture advances it after
// the audio guard. Draw uses only motion_frame, never clocks or live pointers.
constexpr unsigned TRIGGER=6,TRAIL_RESET=1u<<7;
struct TrailPoint {uint64_t at=0;uint32_t phase=0;q15 value=0;};
struct Motion {
    Engine* engine=nullptr;platform::AudioOut* audio=nullptr;int sigil=-1;
    uint64_t last=0,opened=0,focused=0,pulses[7]={};
    Menu menu=Menu::None;int item=-1;bool opening=false;
    unsigned pending=0;int pen=-1;
    float from_y=0,from_width=0,y=0,width=0;
    TrailPoint trail[6];int trail_count=0;
} motion;
struct MotionFrame {
    unsigned reveal=255,pulses[7]={};int underline_y=0,underline_width=0,pen=-1;
    struct Point {uint32_t phase=0;q15 value=0;unsigned alpha=0;} trail[6];
    int trail_count=0;
} motion_frame;
static_assert(sizeof(Motion)+sizeof(MotionFrame)<=640,"bounded UI-only sigil motion");
uint32_t faded(uint32_t color,unsigned alpha){return (color&0xffffffu)|(((color>>24)*alpha/255)<<24);}
float ease(uint64_t now,uint64_t start,unsigned duration){
    const float t=now<start?1.f:std::min(1.f,float(now-start)/duration);
    return t*t*(3.f-2.f*t);
}
constexpr unsigned HISTORY_MS=3000,HISTORY_STEP_MS=50,HISTORY_SIZE=64;
struct History {
    struct Sample {uint64_t at=0;q15 value=0;} samples[HISTORY_SIZE];
    uint64_t last_capture=0;unsigned head=0,count=0;
} history;
struct HistoryFrame {
    struct Point {int16_t x=0,y=0;} points[HISTORY_SIZE+1];
    unsigned count=0;bool active=false,stepped=false;
} history_frame;
static_assert(sizeof(History)+sizeof(HistoryFrame)<=1536,"bounded selected-sigil history");
static_assert(std::is_trivially_copyable<HistoryFrame>::value,"pointer-free history draw");
void forget_motion(){motion={};motion_frame={};history={};history_frame={};}
void capture_history(const Frame& previous,bool same_context,uint64_t now){
    history_frame={};
    const bool stochastic=legacy_random(frame.mode)||frame.config.shape==SigilShape::Random||frame.config.shape==SigilShape::Smooth;
    if(!stochastic||!ready()){history={};return;}
    if(!same_context||!previous.valid||previous.sigil!=frame.sigil||previous.mode!=frame.mode||
       previous.config.shape!=frame.config.shape||previous.config.enabled!=frame.config.enabled||
       previous.config.legacy!=frame.config.legacy||(frame.mode==5&&previous.chain!=frame.chain)||
       now<history.last_capture||now-history.last_capture>200)history={};
    history.last_capture=now;
    history_frame.active=true;
    history_frame.stepped=frame.mode==int(SigilMode::RandomStep)||
        (!legacy_random(frame.mode)&&frame.config.shape==SigilShape::Random);
    // One real snapshot per >=50ms, never fill missed intervals or predict RNG.
    const unsigned latest=(history.head+HISTORY_SIZE-1)%HISTORY_SIZE;
    if(!history.count||now-history.samples[latest].at>=HISTORY_STEP_MS){
        history.samples[history.head]={now,frame.value};history.head=(history.head+1)%HISTORY_SIZE;
        history.count=std::min(HISTORY_SIZE,history.count+1);
    }
    for(unsigned i=0;i<history.count;++i){
        const auto sample=history.samples[(history.head+HISTORY_SIZE-history.count+i)%HISTORY_SIZE];
        const uint64_t age=now-sample.at;if(age>HISTORY_MS)continue;
        history_frame.points[history_frame.count++]={int16_t(380-age*360/HISTORY_MS),int16_t(110-int(sample.value)*37/32767)};
    }
    // The right edge is the current captured value, not a held or extrapolated tip.
    if(history.samples[(history.head+HISTORY_SIZE-1)%HISTORY_SIZE].at!=now)
        history_frame.points[history_frame.count++]={380,int16_t(110-int(frame.value)*37/32767)};
}
void draw_history(const sigil_surface::TopPainter& p){
    p.line(20,110,380,110,m::branch());
    for(unsigned i=1;i<history_frame.count;++i){
        const auto a=history_frame.points[i-1],b=history_frame.points[i];
        if(history_frame.stepped){p.line(a.x,a.y,b.x,a.y,m::ink());p.line(b.x,a.y,b.x,b.y,m::ink());}
        else p.line(a.x,a.y,b.x,b.y,m::ink());
    }
    if(history_frame.count){const auto tip=history_frame.points[history_frame.count-1];p.bead(tip.x,tip.y,m::spot(),true);}
    for(int x:{20,140,260,380})p.line(x,149,x,151,m::branch());
    p.text(20,154,"history / 3s / ui sampled",m::ghost());p.right(380,154,"now",m::ghost());
}
bool motion_context(){
    if(motion.engine==g_eng&&motion.audio==g_audio&&motion.sigil==g_sel_sigil)return true;
    forget_motion();motion.engine=g_eng;motion.audio=g_audio;motion.sigil=g_sel_sigil;return false;
}
void feedback(Kind kind){motion.pending|=1u<<unsigned(kind);}
void clear_trail(){motion.pending|=TRAIL_RESET;}
const char* menu_label(Menu which,int row,char* text,size_t size){
    if(which==Menu::Sigil){std::snprintf(text,size,"sigil %c",'a'+row);return text;}
    if(which==Menu::Shape)return SHAPES[row];
    if(which==Menu::Mode)return MODES[row];
    if(which==Menu::Polarity)return row?"unipolar":"bipolar";
    if(which==Menu::Restart)return row?"transport":"free";
    if(row==0)return "none";
    if(row<=frame.sigil){std::snprintf(text,size,"sigil %c",'a'+row-1);return text;}
    return "env";
}
void capture_motion(const Frame& previous,bool same_context){
    const uint64_t now=osGetTime();
    if(now<motion.last||now-motion.last>200){
        // A stalled/backwards UI clock must not replay an old stroke or sweep.
        for(auto& pulse:motion.pulses)pulse=0;
        motion.trail_count=0;motion.menu=Menu::None;
    }
    motion_frame={};
    for(unsigned i=1;i<=TRIGGER;++i){
        if(motion.pending&(1u<<i))motion.pulses[i]=now+1;
        const auto stamp=motion.pulses[i];const unsigned duration=i==unsigned(Kind::Canvas)?180:140;
        if(stamp&&now>=stamp-1&&now-(stamp-1)<duration)
            motion_frame.pulses[i]=255-unsigned((now-(stamp-1))*255/duration);
    }
    motion_frame.pen=motion.pen;
    if(frame.menu!=Menu::None){
        const auto l=layout(frame.menu);char text[24];
        const float target_y=l.y+frame.menu_item*l.step+12;
        const float target_width=ui::text_w(menu_label(frame.menu,frame.menu_item,text,sizeof text))-1;
        if(motion.opening||motion.menu!=frame.menu){
            motion.opened=motion.focused=now;motion.item=frame.menu_item;
            motion.from_y=motion.y=target_y;motion.from_width=motion.width=target_width;
        }else{
            const float t=ease(now,motion.focused,100);
            const float old_y=l.y+motion.item*l.step+12;
            const float old_width=ui::text_w(menu_label(frame.menu,motion.item,text,sizeof text))-1;
            motion.y=motion.from_y+(old_y-motion.from_y)*t;
            motion.width=motion.from_width+(old_width-motion.from_width)*t;
            if(motion.item!=frame.menu_item){
                motion.from_y=motion.y;motion.from_width=motion.width;
                motion.focused=now;motion.item=frame.menu_item;
            }
        }
        motion_frame.reveal=unsigned(ease(now,motion.opened,140)*255);
        motion_frame.underline_y=int(motion.y+.5f);motion_frame.underline_width=int(motion.width+.5f);
    }
    motion.menu=frame.menu;motion.opening=false;
    const bool changed=!same_context||!previous.valid||previous.menu!=Menu::None||
        previous.mode!=frame.mode||previous.config.shape!=frame.config.shape||
        previous.config.unipolar!=frame.config.unipolar||previous.depth!=frame.depth||
        previous.config.phase_degrees!=frame.config.phase_degrees||
        std::memcmp(previous.table,frame.table,sizeof frame.table)!=0;
    if(changed||!frame.periodic||frame.menu!=Menu::None||motion.pending||frame.phase<previous.phase)
        motion.trail_count=0;
    if(frame.periodic&&frame.menu==Menu::None){
        // Real recent output samples only. Cap the visible phase span (~23px on
        // bottom), expire at 90ms, and never join the two ends of a wrapped cycle.
        int kept=0;
        for(int i=0;i<motion.trail_count;++i){const auto p=motion.trail[i];
            if(now>=p.at&&now-p.at<90&&frame.phase>=p.phase&&frame.phase-p.phase<0x15555555u)
                motion.trail[kept++]=p;
        }
        motion.trail_count=kept;
        for(int i=0;i<kept;++i){const auto p=motion.trail[i];
            if(p.phase<frame.phase)motion_frame.trail[motion_frame.trail_count++]={p.phase,p.value,unsigned((90-(now-p.at))*160/90)};
        }
        if(!kept||((frame.phase!=motion.trail[kept-1].phase||frame.value!=motion.trail[kept-1].value)&&now>motion.trail[kept-1].at)){
            if(kept==6){for(int i=1;i<6;++i)motion.trail[i-1]=motion.trail[i];--motion.trail_count;}
            motion.trail[motion.trail_count++]={now,frame.phase,frame.value};
        }
    }
    motion.pending=0;motion.last=now;
}
template<class P> void draw_trail(const P& p,int left,int width,int zero,int height){
    for(int i=0;i<motion_frame.trail_count;++i){
        const auto a=motion_frame.trail[i];
        const uint32_t next_phase=i+1<motion_frame.trail_count?motion_frame.trail[i+1].phase:frame.phase;
        const q15 next_value=i+1<motion_frame.trail_count?motion_frame.trail[i+1].value:frame.value;
        // Do not draw a diagonal shortcut through square/drawn discontinuities.
        const int x=left+int((uint64_t(a.phase)*width)>>32),y=zero-int(a.value)*height/32767;
        if(std::abs(int(next_value)-int(a.value))<8192)
            p.line(x,y,left+int((uint64_t(next_phase)*width)>>32),zero-int(next_value)*height/32767,faded(m::spot(),a.alpha));
        else p.rect(x,y,2,1,faded(m::spot(),a.alpha));
    }
}
void close_menu(){menu=Menu::None;menu_engine=nullptr;menu_audio=nullptr;menu_sigil=-1;motion.opening=false;}
void open_menu(Menu next){
    sigils_view_touch_release();menu=next;menu_engine=g_eng;menu_audio=g_audio;menu_sigil=g_sel_sigil;blur_refresh=true;motion.opening=true;
    const auto& c=g_eng->sigil_config[g_sel_sigil];const auto& s=g_eng->sigils[g_sel_sigil];
    if(next==Menu::Sigil)menu_item=g_sel_sigil;
    else if(next==Menu::Shape)menu_item=int(c.shape);
    else if(next==Menu::Mode)menu_item=int(s.mode);
    else if(next==Menu::Polarity)menu_item=c.unipolar;
    else if(next==Menu::Restart)menu_item=int(c.restart);
    else {menu_item=0;for(int i=0;i<mod_count();++i)if(mod_at(i)==g_eng->sigil_mods[g_sel_sigil].src)menu_item=i;}
}
void select_menu(int row){
    if(row<0||row>=menu_count(menu))return;
    auto& s=g_eng->sigils[g_sel_sigil];const auto old=g_eng->sigil_config[g_sel_sigil];
    if(menu==Menu::Sigil)g_sel_sigil=row;
    else if(menu==Menu::Mode){if(int(s.mode)!=row){s.mode=SigilMode(row);mark_project_mutated();}}
    else if(menu==Menu::Shape){if(int(old.shape)!=row||!old.enabled){auto c=editable_config();c.shape=SigilShape(row);config_change(c);}}
    else if(menu==Menu::Polarity){if(old.unipolar!=bool(row)){auto c=editable_config();c.unipolar=row;config_change(c);}}
    else if(menu==Menu::Restart){if(int(old.restart)!=row){auto c=editable_config();c.restart=SigilRestart(row);config_change(c);}}
    else if(menu==Menu::Mod){const auto mod=g_eng->sigil_mods[g_sel_sigil];if(mod.src!=mod_at(row)){g_eng->set_sigil_mod(g_sel_sigil,mod_at(row),mod.depth);mark_project_mutated();}}
    close_menu();
}
void begin(Kind kind,int x,int y,int initial,bool relative){
    gesture={kind,g_eng,g_audio,g_sel_sigil,x,y,initial,g_eng->sigils[g_sel_sigil].mode,g_eng->sigil_config[g_sel_sigil].shape,g_eng->sigil_sync[g_sel_sigil],relative};
    g_drag=kind==Kind::Canvas?Drag::SigCanvas:kind==Kind::Depth?Drag::SigDepth:kind==Kind::ModDepth?Drag::SigModDepth:Drag::SigRate;
}
bool gesture_valid(){return ready()&&menu==Menu::None&&gesture.engine==g_eng&&gesture.audio==g_audio&&gesture.sigil==g_sel_sigil&&gesture.mode==g_eng->sigils[g_sel_sigil].mode&&gesture.shape==g_eng->sigil_config[g_sel_sigil].shape&&gesture.sync==g_eng->sigil_sync[g_sel_sigil]&&g_drag!=Drag::None;}
void plot(int tx,int ty){
    const int px=clamp((tx-X0)*(SIGIL_POINTS-1)/(X1-X0),0,SIGIL_POINTS-1);
    int py=clamp((ZERO-ty)*32767/(ZERO-TOP),-32767,32767);
    if(!g_smooth_active){g_smooth_py=py;g_smooth_active=true;}else g_smooth_py=(g_smooth_py*2+py)/3;
    py=g_smooth_py;auto& s=g_eng->sigils[g_sel_sigil];q15 old[SIGIL_POINTS];std::memcpy(old,s.table,sizeof old);const bool drawn=s.drawn;
    if(g_last_px>=0)s.stroke_to(g_last_px,q15(g_last_py),px,q15(py));else s.set_point(px,q15(py));
    g_last_px=px;g_last_py=py;
    if(!drawn||std::memcmp(old,s.table,sizeof old)){mark_project_mutated();feedback(Kind::Canvas);motion.pen=px;}
    if(!g_eng->sigil_config[g_sel_sigil].enabled){auto c=g_eng->sigil_config[g_sel_sigil];c.enabled=true;config_change(c);}
}
void apply_drag(int x,int y){
    const int dx=clamp(x,-1024,1024)-gesture.x-(clamp(y,-1024,1024)-gesture.y);
    auto& s=g_eng->sigils[g_sel_sigil];
    if(gesture.kind==Kind::Canvas){plot(clamp(x,-1024,1024),clamp(y,-1024,1024));return;}
    if(gesture.kind==Kind::Rate){
        if(gesture.sync){int v=clamp(gesture.initial+dx/20,0,5);if(v!=g_eng->sigil_div[g_sel_sigil]){g_eng->set_sigil_div(g_sel_sigil,v);mark_project_mutated();feedback(Kind::Rate);}}
        else if(dx){auto c=editable_config();c.legacy_rate=false;c.rate_millihz=uint16_t(clamp(gesture.initial+dx*50,50,20000));config_change(c);}
    }else if(gesture.kind==Kind::Phase){const int v=clamp((x-110)*359/190,0,359);if(v!=g_eng->sigil_config[g_sel_sigil].phase_degrees){auto c=editable_config();c.phase_degrees=uint16_t(v);config_change(c);}}
    else {
        const bool depth=gesture.kind==Kind::Depth;const int center=depth?254:245,width=depth?92:110;
        const q15 value=q15(clamp(gesture.relative?gesture.initial+dx*65534/width:(x-center)*65534/width,-32767,32767));
        const q15 old=depth?s.depth:g_eng->sigil_mods[g_sel_sigil].depth;
        if(value!=old){if(depth)s.depth=value;else g_eng->set_sigil_mod(g_sel_sigil,g_eng->sigil_mods[g_sel_sigil].src,value);mark_project_mutated();feedback(gesture.kind);}
    }
}
void reset(){if(g_eng->sigils[g_sel_sigil].mode==SigilMode::Follow)g_eng->request_chain_trigger(g_eng->step_dest[g_sel_sigil],g_eng->launch_quantize);else g_eng->reset_sigil(g_sel_sigil);motion.pending|=1u<<TRIGGER;clear_trail();}
void back(){sigils_close_mode_menu();if(!patchbay_sigil_back())g_view=View::Chains;}
void rail(const Painter& p,int left,int right,int y,q15 v,bool disabled=false){
    const int center=(left+right)/2,at=center+int(v)*(right-left)/65534;
    module_fader::horizontal(p,left,right,y,at,at,{false,disabled,false},center);
}
void anchor(const Painter& p,Menu which){
    const bool rnd=legacy_random(frame.mode),follow=frame.mode==5;char text[16];
    if(which==Menu::Sigil){text[0]=char('a'+frame.sigil);text[1]=0;p.heading(147,14,text,m::spot());p.chevron(178,24,m::spot());p.line(144,36,182,36,m::spot());}
    if(which==Menu::Shape){p.text(62,47,rnd?"mode-owned":SHAPES[int(frame.config.shape)],rnd?m::ghost():m::spot());if(!rnd)p.chevron(65+ui::text_w(SHAPES[int(frame.config.shape)]),50,m::spot());}
    if(which==Menu::Mode){p.text(246,47,MODES[frame.mode],m::ink());p.chevron(307,50,m::ink());}
    if(which==Menu::Polarity){p.text(16,190,frame.config.unipolar?"unipolar":"bipolar",rnd?m::ghost():m::ink());if(!rnd)p.chevron(76,193,m::ink());}
    if(which==Menu::Restart){p.text(186,190,frame.config.restart==SigilRestart::Transport?"transport":"free",follow?m::ghost():m::ink());if(!follow)p.chevron(300,193,m::ink());}
    if(which==Menu::Mod){p.text(80,211,source_name(frame.source),frame.sync||follow?m::ghost():m::ink());p.chevron(112,214,m::ghost());}
}
void editor(const Painter& p){
    p.heading(16,11,"sigils",m::ink());anchor(p,Menu::Sigil);p.right(304,23,"undo",frame.undo?m::ink():m::ghost());
    p.text(16,47,"shape",m::ghost());anchor(p,Menu::Shape);
    if(frame.mode==2||frame.mode==5){const char chain[]={'c',char('1'+frame.chain),' ','>',0};p.text(204,47,chain,m::spot());}else p.text(204,47,"mode",m::ghost());
    anchor(p,Menu::Mode);
    for(int x:{20,160,300})p.line(x,92,x,96,m::branch());
    const bool editable=frame.config.shape==SigilShape::Drawn&&!legacy_random(frame.mode);
    // Drawn shows the saved, editable ink; presets show the engine oracle after
    // polarity/depth. Stochastic output is a held scalar, never a made-up cycle.
    const q15* points=editable?frame.table:frame.wave;
    if(editable||frame.periodic){int py=ZERO-int(points[0])*29/32767;for(int i=1;i<SIGIL_POINTS;++i){int x=X0+i*280/127,y=ZERO-int(points[i])*29/32767;p.line(X0+(i-1)*280/127,py,x,y,m::ink());py=y;}}
    else {int y=ZERO-int(frame.value)*29/32767;p.line(X0,y,X1,y,m::ink());p.text(20,65,"live output / non-periodic",m::ghost());}
    if(editable&&motion_frame.pulses[unsigned(Kind::Canvas)]&&motion_frame.pen>=0){
        const int pen=motion_frame.pen;const auto color=faded(m::spot(),motion_frame.pulses[unsigned(Kind::Canvas)]);
        for(int i=std::max(1,pen-4);i<=std::min(SIGIL_POINTS-1,pen+4);++i)
            p.line(X0+(i-1)*280/127,ZERO-int(frame.table[i-1])*29/32767,X0+i*280/127,ZERO-int(frame.table[i])*29/32767,color);
        p.rect(X0+pen*280/127-1,ZERO-int(frame.table[pen])*29/32767-1,3,3,color);
    }
    if(frame.periodic){draw_trail(p,X0,280,ZERO,29);int x=X0+int((uint64_t(frame.phase)*280)>>32);int y=ZERO-int(frame.value)*29/32767;p.bead(x,y,m::spot(),true);}
    for(int x:{20,90,160,230,300})p.line(x,124,x,126,m::branch());
    char text[48];const bool follow=frame.mode==5,rnd=legacy_random(frame.mode);
    if(follow){std::snprintf(text,sizeof text,"play c%u >",frame.chain+1);p.text(16,138,text,m::spot());p.text(96,138,frame.follow_active?"playing":"no play",m::ghost());}
    else {
        p.text(16,138,"free",frame.sync?m::ghost():m::spot());p.text(52,138,"sync",frame.sync?m::spot():m::ghost());p.line(frame.sync?52:16,151,frame.sync?75:39,151,m::spot());
        if(frame.sync)std::snprintf(text,sizeof text,"%s",DIVS[frame.division]);else {int mhz=frame.config.legacy||frame.config.legacy_rate?legacy_millihz(frame.rate):frame.config.rate_millihz;std::snprintf(text,sizeof text,"%d.%02d",mhz/1000,(mhz%1000)/10);}
        // b suffix remains small; numbers and sigil letters are native Gossip28.
        for(char* q=text;*q;++q)if(*q=='b')*q=0;
        const int end=p.number(96,132,text,m::spot());p.text(end+3,145,frame.sync?"b":"hz",m::ghost());
        if(motion_frame.pulses[unsigned(Kind::Rate)])p.line(96,158,std::min(199,end),158,faded(m::spot(),motion_frame.pulses[unsigned(Kind::Rate)]));
    }
    p.text(208,138,"depth",m::ghost());std::snprintf(text,sizeof text,"%+d%%",percent(frame.depth));p.right(304,138,text,m::ink());rail(p,208,300,153,frame.depth);
    if(motion_frame.pulses[unsigned(Kind::Depth)]){const int x=254+int(frame.depth)*92/65534;p.line(x-3,160,x+3,160,faded(m::spot(),motion_frame.pulses[unsigned(Kind::Depth)]));}
    p.text(16,168,"phase",m::ghost());std::snprintf(text,sizeof text,"%u deg",frame.config.phase_degrees);p.text(58,168,follow||rnd?"--":text,follow||rnd?m::ghost():m::spot());
    if(follow||rnd)module_fader::horizontal_track(p,110,300,173);
    else {const int x=110+frame.config.phase_degrees*190/359;
        module_fader::horizontal(p,110,300,173,x,x);
        if(motion_frame.pulses[unsigned(Kind::Phase)])p.line(x-3,179,x+3,179,faded(m::spot(),motion_frame.pulses[unsigned(Kind::Phase)]));}
    anchor(p,Menu::Polarity);p.text(126,190,"restart",m::ghost());anchor(p,Menu::Restart);
    p.text(16,211,"rate mod",m::ghost());anchor(p,Menu::Mod);p.text(152,211,"amount",m::ghost());
    rail(p,190,300,220,frame.mod_depth,follow||frame.sync);std::snprintf(text,sizeof text,"%+d%%",percent(frame.mod_depth));p.right(304,207,text,follow||frame.sync?m::ghost():m::ink());
    if(!follow&&!frame.sync&&motion_frame.pulses[unsigned(Kind::ModDepth)]){const int x=245+int(frame.mod_depth)*110/65534;p.line(x-3,227,x+3,227,faded(m::spot(),motion_frame.pulses[unsigned(Kind::ModDepth)]));}
    const char* action=frame.mode==1||follow?"trigger":"reset";
    p.text(16,231,action,sigil_surface::Cache::blend(m::ink(),m::spot(),motion_frame.pulses[TRIGGER],255));
    if(motion_frame.pulses[TRIGGER])p.line(16,228,16+ui::text_w(action)-1,228,faded(m::spot(),motion_frame.pulses[TRIGGER]));
    std::snprintf(text,sizeof text,"sigilbay %02u",frame.route_count);p.text(101,231,text,m::spot());p.right(304,231,"b back",m::ghost());
}
void shape_miniature(const Painter& p,int row,int y,uint32_t color){
    // Choice icons, not live output: generated forms are unit amplitude; drawn
    // uses the saved contour. Random/smooth have fixed illustrative knots (no RNG).
    constexpr int segments=24,left=136,width=36;
    static constexpr q15 knots[]={-16384,24575,0,-24575,16384};
    int py=0;
    for(int i=0;i<=segments;++i){
        int value;
        if(row==int(SigilShape::Drawn))value=frame.table[i*(SIGIL_POINTS-1)/segments];
        else if(row<int(SigilShape::Random))value=sigil_wave_sample(SigilShape(row),uint32_t(uint64_t(i)*0xffffffffu/segments));
        else {const int k=i/6,t=i%6;value=knots[k];
            if(row==int(SigilShape::Smooth)&&k<4)value+=(int(knots[k+1])-value)*t*t*(18-2*t)/216;}
        const int yy=y+4-value*4/32767,x=left+i*width/segments;
        if(i){const int previous_x=left+(i-1)*width/segments;
            if(row==int(SigilShape::Random)||row==int(SigilShape::Square)){
                p.line(previous_x,py,x,py,color);if(py!=yy)p.line(x,py,x,yy,color);
            }else p.line(previous_x,py,x,yy,color);
        }
        py=yy;
    }
}
void menu_draw(){
    Painter p;anchor(p,frame.menu);const auto l=layout(frame.menu);
    const int count=frame.menu==Menu::Mod?frame.sigil+2:frame.menu==Menu::Sigil?NUM_SIGILS:frame.menu==Menu::Shape?7:frame.menu==Menu::Mode?6:2;
    const int reveal=int(motion_frame.reveal),root=l.y+frame.menu_item*l.step+4;
    const int first=root+(l.y+4-root)*reveal/255,last=root+(l.y+(count-1)*l.step+4-root)*reveal/255;
    p.line(l.branch,first,l.branch,last,faded(m::branch(),unsigned(reveal)));
    const unsigned alpha=unsigned(40+215*reveal/255);
    if(frame.menu==Menu::Sigil)p.right(248,43,"links",faded(m::ghost(),alpha));
    for(int i=0;i<count;++i){char text[24];const char* name=menu_label(frame.menu,i,text,sizeof text);
        const bool selected=i==frame.menu_item;const auto color=selected?m::spot():m::ink();const int y=l.y+i*l.step;
        if(y+4>=first&&y+4<=last)p.line(l.branch,y+4,l.branch+(l.x-7-l.branch)*reveal/255,y+4,faded(selected?color:m::branch(),unsigned(reveal)));
        p.text(l.x,y,name,faded(color,alpha));
        if(frame.menu==Menu::Shape)shape_miniature(p,i,y,faded(selected?m::spot():m::ghost(),alpha));
        if(frame.menu==Menu::Sigil){std::snprintf(text,sizeof text,"%u",unsigned(frame.assignments[i]));
            p.right(248,y,text,faded(selected&&frame.assignments[i]?m::spot():m::ghost(),alpha));}
    }
    p.line(l.x,motion_frame.underline_y,l.x+motion_frame.underline_width,motion_frame.underline_y,faded(m::spot(),unsigned(40+215*reveal/255)));
}
} // namespace

// Both immutable textures are allocated pre-audio and uploaded only at the
// existing synchronized GPU-frame hook. Views share them, never allocate per menu.
bool sigils_visual_init(){const bool bottom=blur.init();const bool top=sigil_surface::top_cache.init();return bottom&&top;}
void sigils_visual_shutdown(){blur.shutdown();sigil_surface::top_cache.shutdown();}
void sigils_gpu_frame(){blur.upload();sigil_surface::top_cache.upload();}
void sigils_view_touch_release(){
    if(gesture.kind!=Kind::None)g_drag=Drag::None;
    gesture={};g_last_px=-1;g_smooth_active=false;
    if(g_sig_canvas_mutated){mark_project_mutated();g_sig_canvas_mutated=false;}
}
void sigils_close_mode_menu(){sigils_view_touch_release();close_menu();forget_motion();}
void sigils_forget_undo(){sigils_close_mode_menu();for(int i=0;i<NUM_SIGILS;++i){g_sigil_undo_valid[i]=false;undo_drawn[i]=false;}frame={};blur.valid=false;}
bool sigils_view_buttons(u32 down,u32 held){
    if(!ready())return false;
    motion_context();
    if(down&(KEY_SELECT|KEY_START)){sigils_close_mode_menu();return false;}
    const u32 local=down&(KEY_A|KEY_B|KEY_X|KEY_Y|KEY_UP|KEY_DOWN|KEY_LEFT|KEY_RIGHT|KEY_L|KEY_R);
    if(!local)return false;
    sigils_view_touch_release();platform::AudioOut::Guard guard(*g_audio);
    if(menu!=Menu::None){
        if(local&KEY_B)close_menu();else if(local&KEY_A)select_menu(menu_item);
        else {const int n=menu_count(menu);if(local&(KEY_UP|KEY_LEFT))menu_item=(menu_item+n-1)%n;if(local&(KEY_DOWN|KEY_RIGHT))menu_item=(menu_item+1)%n;}
        return true;
    }
    if(local&KEY_B){back();return true;}
    if(local&KEY_Y){g_view=View::Chains;return true;}
    if(local&KEY_A){reset();return true;}
    if(local&KEY_X){if(!legacy_random(int(g_eng->sigils[g_sel_sigil].mode)))open_menu(Menu::Shape);return true;}
    if(local&(KEY_UP|KEY_DOWN)){g_sel_sigil=(g_sel_sigil+((local&KEY_UP)?NUM_SIGILS-1:1))%NUM_SIGILS;return true;}
    if(local&(KEY_LEFT|KEY_RIGHT)){
        const int delta=(local&KEY_RIGHT)?1:-1;
        if(held&KEY_L){g_sel_sigil=(g_sel_sigil+delta+NUM_SIGILS)%NUM_SIGILS;return true;}
        if(g_eng->sigils[g_sel_sigil].mode==SigilMode::Follow)return true;
        if(g_eng->sigil_sync[g_sel_sigil]){int v=clamp(g_eng->sigil_div[g_sel_sigil]+delta,0,5);if(v!=g_eng->sigil_div[g_sel_sigil]){g_eng->set_sigil_div(g_sel_sigil,v);mark_project_mutated();feedback(Kind::Rate);}}
        else {auto c=editable_config();const int current=(c.legacy_rate||c.legacy)?legacy_millihz(g_eng->sigil_rate(g_sel_sigil)):c.rate_millihz;c.legacy_rate=false;c.rate_millihz=uint16_t(clamp(current+delta*50,50,20000));config_change(c);}
    }
    return true;
}
void sigils_view_touch_down(int x,int y){
    if(!ready()){sigils_close_mode_menu();return;}if(gesture.kind!=Kind::None)return;
    motion_context();
    if(menu==Menu::None&&hit(x,y,254,5,313,39)){sigil_tool_apply(0);return;}
    platform::AudioOut::Guard guard(*g_audio);
    if(menu!=Menu::None){const auto l=layout(menu);const int row=(y-l.y+5)/l.step;
        if(x>=l.left&&x<l.right&&y>=l.y-5&&row<menu_count(menu))select_menu(row);else close_menu();return;}
    auto& s=g_eng->sigils[g_sel_sigil];const auto& c=g_eng->sigil_config[g_sel_sigil];const bool rnd=legacy_random(int(s.mode)),follow=s.mode==SigilMode::Follow;
    if(hit(x,y,135,5,190,39)){open_menu(Menu::Sigil);return;}
    if(hit(x,y,52,39,185,61)){if(!rnd)open_menu(Menu::Shape);return;}
    if(hit(x,y,196,39,235,61)){if(s.mode==SigilMode::Steps||follow){g_eng->set_step_dest(g_sel_sigil,(g_eng->step_dest[g_sel_sigil]+1)%NUM_CHAINS);mark_project_mutated();}return;}
    if(hit(x,y,235,39,319,61)){open_menu(Menu::Mode);return;}
    if(hit(x,y,12,61,309,127)){if(c.shape==SigilShape::Drawn&&!rnd){sigil_snapshot_undo(g_sel_sigil);begin(Kind::Canvas,x,y,0,false);g_last_px=-1;g_smooth_active=false;plot(x,y);}return;}
    if(hit(x,y,12,128,84,161)){
        if(follow){g_eng->set_step_dest(g_sel_sigil,(g_eng->step_dest[g_sel_sigil]+1)%NUM_CHAINS);mark_project_mutated();}
        else {bool sync=x>=48;if(sync!=g_eng->sigil_sync[g_sel_sigil]){g_eng->set_sigil_sync(g_sel_sigil,sync);mark_project_mutated();feedback(Kind::Rate);}}return;
    }
    if(hit(x,y,84,128,202,162)){if(!follow)begin(Kind::Rate,x,y,g_eng->sigil_sync[g_sel_sigil]?g_eng->sigil_div[g_sel_sigil]:(c.legacy||c.legacy_rate)?legacy_millihz(g_eng->sigil_rate(g_sel_sigil)):c.rate_millihz,true);return;}
    if(hit(x,y,202,128,312,162)){begin(Kind::Depth,x,y,s.depth,y<146);if(y>=146)apply_drag(x,y);return;}
    if(hit(x,y,104,163,310,183)){if(!follow&&!rnd){begin(Kind::Phase,x,y,0,false);apply_drag(x,y);}return;}
    if(hit(x,y,12,182,115,202)){if(!rnd)open_menu(Menu::Polarity);return;}
    if(hit(x,y,174,182,311,202)){if(!follow)open_menu(Menu::Restart);return;}
    if(hit(x,y,67,202,124,226)){if(!follow&&!g_eng->sigil_sync[g_sel_sigil])open_menu(Menu::Mod);return;}
    if(hit(x,y,139,202,312,226)){if(!follow&&!g_eng->sigil_sync[g_sel_sigil]){begin(Kind::ModDepth,x,y,g_eng->sigil_mods[g_sel_sigil].depth,y<216);if(y>=216)apply_drag(x,y);}return;}
    if(y>=226){if(x<89)reset();else if(x<239){sigils_close_mode_menu();patchbay_enter_sigil(g_sel_sigil);g_view=View::Patchbay;}else back();}
}
void sigils_view_touch_drag(int x,int y){if(gesture.kind==Kind::None)return;if(!ready()){sigils_close_mode_menu();return;}platform::AudioOut::Guard guard(*g_audio);if(!gesture_valid()){sigils_view_touch_release();return;}apply_drag(x,y);}
void sigils_capture(){
    const Frame previous=frame;
    frame={};
    if(!g_eng||!g_audio||g_view!=View::Sigils||g_sel_sigil<0||g_sel_sigil>=NUM_SIGILS){sigils_close_mode_menu();return;}
    if(g_overlay!=Overlay::None||nav_is_open())sigils_close_mode_menu();
    if(menu!=Menu::None&&(menu_engine!=g_eng||menu_audio!=g_audio||menu_sigil!=g_sel_sigil))close_menu();
    const bool same_context=motion_context();
    {
        platform::AudioOut::Guard guard(*g_audio);
        if(gesture.kind!=Kind::None&&!gesture_valid())sigils_view_touch_release();
        const auto& s=g_eng->sigils[g_sel_sigil];frame.valid=true;frame.sigil=uint8_t(g_sel_sigil);frame.config=g_eng->sigil_config[g_sel_sigil];frame.mode=uint8_t(s.mode);
        frame.value=s.value;frame.phase=s.phase;frame.increment=s.phase_inc;frame.depth=s.depth;frame.drawn=s.drawn;frame.running=s.running;frame.follow_active=s.follow_active;
        std::memcpy(frame.table,s.table,sizeof frame.table);frame.periodic=g_eng->capture_sigil_wave(g_sel_sigil,frame.wave,SIGIL_POINTS);
        frame.sync=g_eng->sigil_sync[g_sel_sigil];frame.division=uint8_t(clamp(g_eng->sigil_div[g_sel_sigil],0,5));frame.rate=g_eng->sigil_rate(g_sel_sigil);
        frame.source=g_eng->sigil_mods[g_sel_sigil].src;frame.mod_depth=g_eng->sigil_mods[g_sel_sigil].depth;frame.chain=uint8_t(g_eng->step_dest[g_sel_sigil]);
        frame.undo=g_sigil_undo_valid[g_sel_sigil];frame.menu=menu;frame.menu_item=uint8_t(menu_item);
        const ModSource src=sigil_mod_source(g_sel_sigil);
        for(int c=0;c<NUM_CHAINS;++c)for(int slot=0;slot<UNITS_PER_CHAIN;++slot){const Unit* u=g_eng->chains[c].units[slot];if(!u)continue;
            for(int p=0;p<u->num_params;++p){const auto& param=u->params[p];const int source=mod_source_sigil(param.src);
                if(source>=0)++frame.assignments[source];
                if(param.src==src){if(frame.route_count<3){auto& r=frame.routes[frame.route_count];char name[23];lower(name,sizeof name,param.name);std::snprintf(r.name,sizeof r.name,"%c:%c %.8s / %.22s",'1'+c,'1'+slot,m::module_name(u->kind),name);r.depth=param.mod_depth;r.enabled=param.mod_enabled;}++frame.route_count;}}}
        for(int i=0;i<2;++i){const auto mod=g_eng->clock_mod(i==0?ClockTarget::Tempo:ClockTarget::Swing);const int source=mod_source_sigil(mod.src);
            if(source>=0)++frame.assignments[source];
            if(mod.src==src){if(frame.route_count<3)std::snprintf(frame.routes[frame.route_count].name,sizeof frame.routes[0].name,"project / %s",i==0?"tempo":"swing");++frame.route_count;}}
        for(int i=0;i<NUM_SIGILS;++i){const auto mod=g_eng->sigil_mods[i];const int source=mod_source_sigil(mod.src);
            if(source>=0)++frame.assignments[source];
            if(i>g_sel_sigil&&mod.src==src){if(frame.route_count<3){auto& r=frame.routes[frame.route_count];std::snprintf(r.name,sizeof r.name,"sigil %c / rate",'a'+i);r.depth=mod.depth;r.enabled=!g_eng->sigil_sync[i]&&g_eng->sigils[i].mode!=SigilMode::Follow;}++frame.route_count;}}
    }
    capture_motion(previous,same_context);
    capture_history(previous,same_context,motion.last);
    // Software blur after releasing audio, immutable until menu/theme changes.
    const uint32_t colors[]={m::ink(),m::spot(),m::paper_color(0),m::paper_color(239)};
    if(menu!=Menu::None&&(blur_refresh||std::memcmp(colors,blur_colors,sizeof colors))){blur.clear();editor(Painter{&blur});blur.finish();std::memcpy(blur_colors,colors,sizeof colors);blur_refresh=false;}
}
void sigils_view_draw(){
    m::paper(320);if(!frame.valid)return;
    if(frame.menu==Menu::None){editor(Painter{});return;}
    if(!blur.draw()){editor(Painter{});m::paper_rect(0,0,320,240,185);}
    menu_draw();
}
void sigils_top_draw(){
    m::paper(400);if(!frame.valid)return;sigil_surface::TopPainter p;char text[64];
    m::page_heading(page_layout::Title::Sigils,m::ink());std::snprintf(text,sizeof text,"sigil %c / %s",'a'+frame.sigil,MODES[frame.mode]);p.right(380,25,text,m::ghost());
    std::snprintf(text,sizeof text,"output %+d%%",percent(frame.value));p.text(20,54,text,m::spot());
    const char* status=!frame.config.enabled?"inactive / choose shape":legacy_random(frame.mode)?"legacy mode owns random output":frame.mode==5?"follow / actual play cursor":frame.config.legacy||frame.config.legacy_rate?"legacy rate preserved":SHAPES[int(frame.config.shape)];
    p.right(380,54,status,m::ghost());
    if(frame.periodic){int py=110-int(frame.wave[0])*37/32767;for(int i=1;i<SIGIL_POINTS;++i){int x=20+i*360/127,y=110-int(frame.wave[i])*37/32767;p.line(20+(i-1)*360/127,py,x,y,m::ink());py=y;}
        draw_trail(p,20,360,110,37);
        p.bead(20+int((uint64_t(frame.phase)*360)>>32),110-int(frame.value)*37/32767,m::spot(),true);}
    else if(history_frame.active)draw_history(p);
    else {int y=110-int(frame.value)*37/32767;p.line(20,y,380,y,m::ink());p.text(20,145,"live output / not a predicted waveform",m::ghost());}
    p.line(20,169,380,169,m::branch());std::snprintf(text,sizeof text,"destinations / %02u",frame.route_count);p.text(20,178,text,m::ghost());
    for(int i=0;i<std::min(3,int(frame.route_count));++i){p.text(28,192+i*13,frame.routes[i].name,m::ink());std::snprintf(text,sizeof text,frame.routes[i].enabled?"%+d%%":"off",percent(frame.routes[i].depth));p.right(380,192+i*13,text,m::spot());}
    if(!frame.route_count)p.text(28,199,"none / open sigilbay to connect",m::ghost());
    const bool paused=frame.sync||frame.mode==5;
    m::footer(400,paused?"rate mod paused / sync or follow":"one source / separate cable amounts",frame.mode==1||frame.mode==5?"a trigger":"a reset");
}
} // namespace seance::platform::ui3ds
