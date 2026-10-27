#include "ui_patchbay.h"
#include "ui_state.h"
#include "ui_nav.h"
#include "menu_style.h"
#include "module_fader.h"
#include "sigilbay_type.h"
#include "../../core/ui/draw.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <type_traits>

namespace seance::platform::ui3ds {
namespace {
namespace m = menu_style;
constexpr int ROWS=4, LEFT=102, RIGHT=210, FIRST=72, STEP=32, WIRE_STEPS=18;
enum class Page : uint8_t { Bay, Source };
enum class GestureKind : uint8_t { None, Cable, End, Amount };
struct Target { int8_t slot=-1,param=-1; };
bool same(Target a,Target b) { return a.slot==b.slot && a.param==b.param; }
struct Row {
    char module[10]={},param_name[23]={}; Target id;
    ModSource src=ModSource::None;
    q15 depth=0; bool enabled=true;
};
struct Route { char name[47]={}; q15 depth=0; bool enabled=true; };
struct Frame {
    Row rows[ROWS]; ModSource sources[ROWS]={};
    Route routes[2];
    q15 wave[128]={};
    char selected_name[39]={}, status[40]={};
    q15 base=0,effective=0,value=0,depth=0;
    uint32_t phase=0,increment=0;
    uint16_t route_count=0,target_count=0;
    int16_t drag_x=0,drag_y=0,wire_x[WIRE_STEPS+1]={},wire_y[WIRE_STEPS+1]={};
    q15 drag_depth=0;
    int8_t hover_row=-1,hover_source=-1,selected_slot=-1;
    uint8_t chain=0,src_page=0,dst_page=0,src_count=0,source=0,selected_row=255;
    uint8_t env_chain=0,env_response=0;
    Page page=Page::Bay;
    bool valid=false,selected=false,enabled=true,periodic=false,dragging=false;
    bool reconnecting=false,drag_enabled=true,blocked=false;
};
static_assert(sizeof(Frame)<=1024,"bounded patchbay frame");
static_assert(std::is_trivially_copyable<Frame>::value,"no engine references in draw");
Frame frame;
Page page=Page::Bay;
ModSource source=ModSource::SigilA;
Target selected;
int src_page=0,dst_page=0;
char status[40]={};
bool sigil_return=false,editor_return=false;
struct Gesture {
    GestureKind kind=GestureKind::None;
    Engine* engine=nullptr; platform::AudioOut* audio=nullptr;
    Unit* unit=nullptr; Target target;
    int chain=0,x=0,y=0,initial=0,last_x=0,last_y=0;
    ModSource src=ModSource::None;
    q15 depth=0; bool enabled=true;
} gesture;
// One transient UI spring, advanced only during capture after releasing audio.
// Endpoints stay attached to the actual target/stylus; only the cable belly lags.
struct WireMotion { float x=0,y=0,vx=0,vy=0; uint64_t last=0; bool active=false; } wire_motion;
int clamp(int v,int lo,int hi) { return std::max(lo,std::min(hi,v)); }
int percent(q15 v) { return int(v)*100/32767; }
bool is_sigil(ModSource s) { return mod_source_sigil(s)>=0; }
bool ready() { return g_eng&&g_audio&&g_sel_chain>=0&&g_sel_chain<NUM_CHAINS&&g_view==View::Patchbay&&g_overlay==Overlay::None&&!nav_is_open(); }
void message(const char* text) { std::snprintf(status,sizeof status,"%s",text); }
void lower(char* out,size_t n,const char* in) {
    size_t i=0; for(;in&&in[i]&&i+1<n;++i) out[i]=in[i]>='A'&&in[i]<='Z'?char(in[i]+'a'-'A'):in[i]; out[i]=0;
}
const char* source_name(ModSource s) {
    static const char* names[]={"none","sigil a","sigil b","sigil c","sigil d","env","sigil e","sigil f","sigil g","sigil h"};
    return names[clamp(int(s),0,9)];
}
// All helpers touching the engine are controller/capture-only, under AudioOut::Guard.
Unit* unit_at(Target t) {
    if(g_sel_chain<0||g_sel_chain>=NUM_CHAINS||t.slot<0||t.slot>=UNITS_PER_CHAIN) return nullptr;
    Unit* u=g_eng->chains[g_sel_chain].units[t.slot];
    return u&&t.param>=0&&t.param<u->num_params?u:nullptr;
}
Param* param_at(Target t) { Unit* u=unit_at(t); return u?&u->params[t.param]:nullptr; }
int target_count() { int n=0;for(auto* u:g_eng->chains[g_sel_chain].units)if(u)n+=u->num_params;return n; }
Target target_at(int n) {
    for(int s=0;s<UNITS_PER_CHAIN;++s) { auto* u=g_eng->chains[g_sel_chain].units[s];if(!u)continue;
        if(n<u->num_params)return {int8_t(s),int8_t(n)};
        n-=u->num_params;
    } return {};
}
int sources(ModSource* out) {
    int n=0;
    for(int i=0;i<NUM_SIGILS;++i)out[n++]=sigil_mod_source(i);
    out[n++]=ModSource::EnvFollow; return n;
}
void reveal_source(ModSource s) { ModSource list[9];const int n=sources(list);for(int i=0;i<n;++i)if(list[i]==s)src_page=i/ROWS; }
int row_at(int y) { return y>=60&&y<188?(y-60)/STEP:-1; }
int cable_y(int x,int y0,int y1) {
    const int t=clamp(x-LEFT,0,RIGHT-LEFT),w=RIGHT-LEFT;
    return y0+(y1-y0)*t/w+4*12*t*(w-t)/(w*w);
}
void select_target(Target t) {
    selected=t;auto* p=param_at(t);if(p&&p->src!=ModSource::None){source=p->src;reveal_source(source);}message("");
}
void start_gesture(GestureKind kind,int x,int y) {
    gesture={};gesture.kind=kind;gesture.engine=g_eng;gesture.audio=g_audio;gesture.chain=g_sel_chain;
    gesture.x=gesture.last_x=x;gesture.y=gesture.last_y=y;gesture.src=source;gesture.target=selected;
    if(auto* p=param_at(selected)){gesture.unit=unit_at(selected);gesture.depth=p->mod_depth;gesture.enabled=p->mod_enabled;}
}
bool context_matches() {
    if(!ready()||gesture.engine!=g_eng||gesture.audio!=g_audio||gesture.chain!=g_sel_chain)return false;
    if(gesture.kind==GestureKind::End||gesture.kind==GestureKind::Amount){
        auto* p=param_at(gesture.target);
        if(!p||unit_at(gesture.target)!=gesture.unit||p->src!=gesture.src||p->mod_enabled!=gesture.enabled)return false;
        if(p->mod_depth!=gesture.depth)return false;
    }
    return true;
}
void changed(bool yes) { if(yes)mark_project_mutated(); }
void edit_cable(int action) {
    auto* p=param_at(selected);if(!p||p->src==ModSource::None)return;
    if(action==0){p->mod_enabled=!p->mod_enabled;changed(true);}
    if(action==1){const q15 v=p->mod_depth==-32768?32767:q15(-p->mod_depth);if(v!=p->mod_depth){p->mod_depth=v;changed(true);}}
    if(action==2){p->src=ModSource::None;p->mod_depth=0;p->mod_enabled=true;changed(true);}
}
void open_source() {
    patchbay_cancel_input();message("");
    if(is_sigil(source)){g_sel_sigil=mod_source_sigil(source);sigil_return=true;g_view=View::Sigils;}
    else page=Page::Source;
}
void back() {
    patchbay_cancel_input();message("");if(page!=Page::Bay)page=Page::Bay;
    else if(editor_return){editor_return=false;g_view=View::Sigils;}else nav_open();
}
void set_page(bool left,int delta) {
    patchbay_cancel_input();message("");
    if(left){ModSource list[9];const int n=(sources(list)+ROWS-1)/ROWS;src_page=(src_page+delta+n)%n;}
    else {const int n=std::max(1,(target_count()+ROWS-1)/ROWS);dst_page=(dst_page+delta+n)%n;}
}
void change_chain(int delta) {
    patchbay_cancel_input();g_sel_chain=(g_sel_chain+delta+NUM_CHAINS)%NUM_CHAINS;dst_page=0;selected={};message("");
}
void apply_hold(int x,int y) {
    if(!context_matches()){patchbay_cancel_input();return;}
    gesture.last_x=clamp(x,0,319);gesture.last_y=clamp(y,0,239);
    if(gesture.kind==GestureKind::Amount){
        auto* p=param_at(selected);const q15 v=q15(clamp((x-167)*32767/87,-32767,32767));
        if(p->mod_depth!=v){p->mod_depth=v;gesture.depth=v;changed(true);}
    }
}
void connect(Target dest) {
    auto* to=param_at(dest);if(!to)return;
    if(gesture.kind==GestureKind::End&&same(dest,gesture.target))return;
    if(to->src!=ModSource::None){message("input occupied - remove first");return;}
    to->src=gesture.src;to->mod_depth=gesture.kind==GestureKind::End?gesture.depth:q15(16384);
    to->mod_enabled=gesture.kind==GestureKind::End?gesture.enabled:true;
    if(gesture.kind==GestureKind::End){auto* old=param_at(gesture.target);old->src=ModSource::None;old->mod_depth=0;old->mod_enabled=true;}
    changed(true);select_target(dest);
}
void label(int x,int y,const char* text,uint32_t c) { ui::text(x,y,text,c); }
void socket(int x,int y,uint32_t c,bool occupied,bool hovered=false) {
    ui::circle(x,y,hovered?4:3,c,occupied);
}
bool source_visible(ModSource src) {
    for(auto s:frame.sources)if(s!=ModSource::None&&s==src)return true;
    return false;
}
void source_tag(char* out,size_t n,ModSource src) {
    if(is_sigil(src))std::snprintf(out,n,"%c",'a'+mod_source_sigil(src));
    else std::snprintf(out,n,"env");
}
void wire_label(int x,int y,q15 depth,bool enabled) {
    char text[12];std::snprintf(text,sizeof text,enabled?"%+d%%":"off",percent(depth));
    x=clamp(x-ui::text_w(text)/2,116,RIGHT-28-ui::text_w(text));y=clamp(y-15,60,175);
    // Exact paper under the ink, not a badge/panel; crossing cables stay legible.
    m::paper_rect(x-1,y-1,ui::text_w(text)+2,10);label(x,y,text,m::spot());
}
void capture_wire() {
    if(!frame.dragging){wire_motion={};return;}
    int sy=FIRST;for(int i=0;i<ROWS;++i)if(uint8_t(frame.sources[i])==frame.source)sy=FIRST+i*STEP;
    int x0=LEFT,y0=sy,x1=clamp(frame.drag_x,6,314),y1=clamp(frame.drag_y,60,183);
    const int row=row_at(frame.drag_y);
    if(row>=0&&frame.drag_x>=198&&frame.rows[row].id.slot>=0){
        frame.hover_row=row;
        frame.blocked=frame.rows[row].src!=ModSource::None&&!(frame.reconnecting&&row==frame.selected_row);
        x1=RIGHT;y1=FIRST+row*STEP;
    }else if(frame.reconnecting&&frame.selected_row<ROWS&&row>=0&&frame.drag_x>=88&&frame.drag_x<=115&&frame.sources[row]!=ModSource::None){
        frame.hover_source=row;x0=RIGHT;y0=FIRST+frame.selected_row*STEP;x1=LEFT;y1=FIRST+row*STEP;
    }
    const float mx=(x0+x1)*.5f,my=(y0+y1)*.5f;
    const float dx=float(x1-x0),dy=float(y1-y0);
    const float sag=std::min(24.f,std::sqrt(dx*dx+dy*dy)*.12f);
    const uint64_t now=osGetTime();auto& w=wire_motion;
    if(!w.active||now<w.last||now-w.last>200){w={mx,my+sag,0,0,now,true};}
    unsigned elapsed=unsigned(std::min<uint64_t>(now-w.last,64));w.last=now;
    while(elapsed){const unsigned ms=std::min(elapsed,8u);const float dt=ms*.001f;elapsed-=ms;
        w.vx+=(190.f*(mx-w.x)-23.f*w.vx)*dt;w.vy+=(190.f*(my+sag-w.y)-23.f*w.vy)*dt;
        w.x+=w.vx*dt;w.y+=w.vy*dt;
    }
    const float bend_x=std::max(-14.f,std::min(14.f,w.x-mx));
    const float bend_y=std::max(0.f,std::min(32.f,w.y-my));
    for(int i=0;i<=WIRE_STEPS;++i){const float t=float(i)/WIRE_STEPS,bow=4*t*(1-t);
        frame.wire_x[i]=clamp(int(std::lround(x0+dx*t+bow*bend_x)),6,314);
        frame.wire_y[i]=clamp(int(std::lround(y0+dy*t+bow*bend_y)),60,183);
    }
}
void cable(int y0,int y1,uint32_t color,int thick) {
    int px=LEFT,py=y0;for(int n=1;n<=18;++n){const int x=LEFT+(RIGHT-LEFT)*n/18,y=cable_y(x,y0,y1);ui::line(px,py,x,y,color,thick);px=x;py=y;}
}
void wave(int x,int y,int w,int h,bool marker) {
    const int mid=y+h/2;ui::line(x,mid,x+w,mid,m::branch());
    if(frame.periodic){int px=x,py=mid-int(frame.wave[0])*h/2/32767;
        for(int i=1;i<128;++i){const int xx=x+i*w/127,yy=mid-int(frame.wave[i])*h/2/32767;ui::line(px,py,xx,yy,m::ink());px=xx;py=yy;}
    }else {const int yy=mid-int(frame.value)*h/2/32767;ui::line(x,yy,x+w,yy,m::ink());}
    if(marker){const int mx=x+int((uint64_t(frame.phase)*w)>>32),my=mid-int(frame.value)*h/2/32767;ui::line(mx,y-4,mx,y+h+4,m::branch());ui::circle(mx,my,3,m::spot(),false);}
}
void source_detail_top() {
    char text[64];
    m::heading(20,14,"source",m::ink());label(300,24,source_name(ModSource(frame.source)),m::ghost());
    std::snprintf(text,sizeof text,"%s / live output",source_name(ModSource(frame.source)));
    label(20,54,text,m::spot());std::snprintf(text,sizeof text,"out %+d%%",percent(frame.value));label(292,54,text,m::ink());
    wave(28,80,342,60,frame.periodic);
    if(frame.periodic){label(20,151,"0",m::ghost());label(190,151,"180",m::ghost());label(350,151,"360",m::ghost());}
    else label(20,151,"held value - not a periodic trace",m::ghost());
    ui::line(20,170,380,170,m::branch());std::snprintf(text,sizeof text,"destinations / %02u",frame.route_count);label(20,180,text,m::ghost());
    for(int i=0;i<2;++i)if(frame.routes[i].name[0]){label(28,196+i*15,frame.routes[i].name,m::ink());std::snprintf(text,sizeof text,frame.routes[i].enabled?"%+d%%":"off",percent(frame.routes[i].depth));label(344,196+i*15,text,m::spot());}
    label(20,229,"one source / separate cable amounts",m::ghost());
}
} // namespace

void patchbay_cancel_input(){gesture={};wire_motion={};}
void patchbay_forget(){patchbay_cancel_input();page=Page::Bay;source=ModSource::SigilA;selected={};src_page=dst_page=0;sigil_return=editor_return=false;message("");frame={};}
void patchbay_enter(){patchbay_cancel_input();page=Page::Bay;sigil_return=editor_return=false;message("");}
void patchbay_enter_sigil(int sigil){patchbay_enter();source=sigil_mod_source(sigil);selected={};reveal_source(source);editor_return=true;}
bool patchbay_sigil_back(){if(!sigil_return||g_view!=View::Sigils||g_overlay!=Overlay::None||nav_is_open())return false;sigil_return=false;g_view=View::Patchbay;page=Page::Bay;patchbay_cancel_input();return true;}
void patchbay_buttons(u32 down,u32 held){
    if(!ready())return;
    if(down&~KEY_TOUCH)patchbay_cancel_input();
    if(down&KEY_B){back();return;}
    platform::AudioOut::Guard guard(*g_audio);
    if(page==Page::Bay){
        if(down&KEY_LEFT){if(held&KEY_L)change_chain(-1);else set_page(false,-1);}
        if(down&KEY_RIGHT){if(held&KEY_L)change_chain(1);else set_page(false,1);}
        if(down&KEY_UP)set_page(true,-1);
        if(down&KEY_DOWN)set_page(true,1);
        if(down&KEY_A)open_source();
        if(down&KEY_X)open_source();
        if(down&KEY_Y)edit_cable(0);
    }else if(down&KEY_Y)page=Page::Bay;
}
void patchbay_touch_down(int x,int y){
    if(!ready()||gesture.kind!=GestureKind::None)return;
    platform::AudioOut::Guard guard(*g_audio);message("");
    if(page==Page::Source){
        if(y>=214){back();return;}
        if(y>=10&&y<40&&x>=196){ModSource list[9];int n=sources(list),at=0;for(int i=0;i<n;++i)if(list[i]==source)at=i;source=list[(at+(x<252?n-1:1))%n];reveal_source(source);return;}
        if(source==ModSource::EnvFollow){if(y>=80&&y<117)changed(g_eng->set_env_follow_source((g_eng->env_follow_source()+1)%NUM_CHAINS));if(y>=128&&y<171)changed(g_eng->set_env_follow_response(EnvResponse((int(g_eng->env_follow_response())+1)%3)));}
        else if(is_sigil(source))open_source();
        return;
    }
    if(y<40&&x>=197){change_chain(x<255?-1:1);return;}
    if(y>=40&&y<60){if(x<120)set_page(true,x<65?-1:1);else if(x>197)set_page(false,x<257?-1:1);return;}
    if(y>=216){if(x<116)open_source();else if(x<231)edit_cable(1);else edit_cable(2);return;}
    if(y>=190&&y<215){if(x<75)edit_cable(0);else if(x<=262){auto* p=param_at(selected);if(p&&p->src!=ModSource::None){source=p->src;start_gesture(GestureKind::Amount,x,y);apply_hold(x,y);}}return;}
    const int row=row_at(y);if(row<0)return;
    ModSource list[9];const int n=sources(list);const int si=src_page*ROWS+row;
    if(x<89){if(si<n){source=list[si];open_source();}return;}
    if(x<=115){if(si<n){source=list[si];selected={};start_gesture(GestureKind::Cable,x,y);}return;}
    if(x>=198){const Target t=target_at(dst_page*ROWS+row);if(t.slot<0)return;select_target(t);
        auto* p=param_at(t);if(x<222&&p->src!=ModSource::None){source=p->src;start_gesture(GestureKind::End,x,y);}return;
    }
    int best=9;Target found;
    for(int j=0;j<ROWS;++j){Target t=target_at(dst_page*ROWS+j);auto* p=param_at(t);if(!p||p->src==ModSource::None)continue;
        for(int k=0;k<ROWS&&src_page*ROWS+k<n;++k)if(list[src_page*ROWS+k]==p->src){int d=std::abs(y-cable_y(x,FIRST+k*STEP,FIRST+j*STEP));if(d<best){best=d;found=t;}}
    }if(found.slot>=0)select_target(found);
}
void patchbay_touch_hold(int x,int y){if(gesture.kind==GestureKind::None)return;if(!ready()){patchbay_cancel_input();return;}platform::AudioOut::Guard guard(*g_audio);apply_hold(x,y);}
void patchbay_touch_release(){
    if(gesture.kind==GestureKind::None)return;
    if(!ready()){patchbay_cancel_input();return;}
    platform::AudioOut::Guard guard(*g_audio);
    if(context_matches()&&(gesture.kind==GestureKind::Cable||gesture.kind==GestureKind::End)){
        const int x=gesture.last_x,y=gesture.last_y,row=row_at(y);
        if(row>=0&&x>=198){connect(target_at(dst_page*ROWS+row));}
        else if(row>=0&&x>=88&&x<=115&&gesture.kind==GestureKind::End){ModSource list[9];const int n=sources(list),at=src_page*ROWS+row;
            if(at<n){auto* p=param_at(gesture.target);if(p->src!=list[at]){p->src=list[at];source=p->src;changed(true);}}}
    }patchbay_cancel_input();
}
void patchbay_capture(){
    frame={};
    if(g_view!=View::Patchbay){patchbay_cancel_input();if(g_view!=View::Sigils)sigil_return=false;return;}
    if(!g_eng||!g_audio)return;
    if(g_overlay!=Overlay::None||nav_is_open())patchbay_cancel_input();
    {
    platform::AudioOut::Guard guard(*g_audio);
    g_sel_chain=clamp(g_sel_chain,0,NUM_CHAINS-1);
    if(gesture.kind!=GestureKind::None&&!context_matches())patchbay_cancel_input();
    frame.valid=true;frame.page=page;frame.chain=g_sel_chain;
    ModSource list[9];const int n=sources(list);src_page=clamp(src_page,0,(n-1)/ROWS);frame.src_count=n;frame.src_page=src_page;
    const int total=target_count();dst_page=clamp(dst_page,0,std::max(0,(total-1)/ROWS));frame.target_count=total;frame.dst_page=dst_page;
    for(int i=0;i<ROWS;++i){if(src_page*ROWS+i<n)frame.sources[i]=list[src_page*ROWS+i];
        auto& row=frame.rows[i];row.id=target_at(dst_page*ROWS+i);if(auto* u=unit_at(row.id)){
            auto& p=u->params[row.id.param];lower(row.param_name,sizeof row.param_name,p.name);
            std::snprintf(row.module,sizeof row.module,"%s",m::module_name(u->kind));
            row.src=p.src;row.depth=p.mod_depth;row.enabled=p.mod_enabled;if(same(selected,row.id))frame.selected_row=i;
        }
    }
    if(auto* u=unit_at(selected)){auto& p=u->params[selected.param];char name[23];lower(name,sizeof name,p.name);
        std::snprintf(frame.selected_name,sizeof frame.selected_name,"%.8s / %.22s",m::module_name(u->kind),name);frame.selected_slot=selected.slot;
        frame.selected=p.src!=ModSource::None;frame.base=p.base;frame.effective=p.effective;frame.depth=p.mod_depth;frame.enabled=p.mod_enabled;
        if(page==Page::Bay&&frame.selected)source=p.src;
    }
    frame.source=uint8_t(source);
    if(is_sigil(source)){const int i=mod_source_sigil(source);const auto& s=g_eng->sigils[i];frame.value=s.value;frame.phase=s.phase;frame.periodic=g_eng->capture_sigil_wave(i,frame.wave,128);}
    else {frame.value=g_eng->env_follow_value();}
    frame.env_chain=g_eng->env_follow_source();frame.env_response=uint8_t(g_eng->env_follow_response());
    for(int c=0;c<NUM_CHAINS;++c)for(int s=0;s<UNITS_PER_CHAIN;++s){auto* u=g_eng->chains[c].units[s];if(!u)continue;
        for(int p=0;p<u->num_params;++p)if(u->params[p].src==source){const int at=frame.route_count++;if(at<2){char name[23];lower(name,sizeof name,u->params[p].name);auto& r=frame.routes[at];std::snprintf(r.name,sizeof r.name,"%c:%c %.8s / %.22s",'1'+c,'1'+s,m::module_name(u->kind),name);r.depth=u->params[p].mod_depth;r.enabled=u->params[p].mod_enabled;}}
    }
    frame.dragging=gesture.kind==GestureKind::Cable||gesture.kind==GestureKind::End;frame.drag_x=gesture.last_x;frame.drag_y=gesture.last_y;
    frame.reconnecting=gesture.kind==GestureKind::End;frame.drag_depth=frame.reconnecting?gesture.depth:q15(16384);
    frame.drag_enabled=!frame.reconnecting||gesture.enabled;
    std::snprintf(frame.status,sizeof frame.status,"%s",status);
    }
    capture_wire(); // fixed UI geometry, no engine/audio access or draw-time animation
}
void patchbay_draw(){
    m::paper(320);if(!frame.valid)return;char text[64];
    m::heading(14,13,frame.page==Page::Bay?"sigilbay":"source",m::ink());
    if(frame.page==Page::Source){
        std::snprintf(text,sizeof text,"< %s >",source_name(ModSource(frame.source)));label(199,24,text,m::ghost());
        if(frame.source==uint8_t(ModSource::EnvFollow)){label(14,55,"post-module envelope follower",m::ghost());std::snprintf(text,sizeof text,"chain %u    >",frame.env_chain+1);label(20,95,text,m::spot());
            const char* responses[]={"fast","body","slow"};std::snprintf(text,sizeof text,"response / %s  >",responses[clamp(frame.env_response,0,2)]);label(20,142,text,m::ink());}
        else label(20,115,"edit sigil >",m::spot());
        label(126,226,"cables",m::spot());label(264,226,"b back",m::ghost());return;
    }
    std::snprintf(text,sizeof text,"< chain %u >",frame.chain+1);label(200,24,text,m::ghost());
    std::snprintf(text,sizeof text,"< src %u/%u >",frame.src_page+1,(frame.src_count+3)/4);label(14,49,text,m::ghost());
    std::snprintf(text,sizeof text,"< params %u/%u >",frame.dst_page+1,std::max(1,(frame.target_count+3)/4));label(211,49,text,m::ghost());
    // Quiet routes first; selected/held route and its annotation stay on top.
    for(int pass=0;pass<2;++pass)for(int j=0;j<ROWS;++j){const auto& r=frame.rows[j];
        if(r.id.slot<0||bool(j==frame.selected_row)!=bool(pass)||(frame.dragging&&frame.reconnecting&&j==frame.selected_row))continue;
        for(int i=0;i<ROWS;++i)if(frame.sources[i]!=ModSource::None&&frame.sources[i]==r.src)
            cable(FIRST+i*STEP,FIRST+j*STEP,pass?m::spot():(r.enabled?m::ghost():m::branch()),pass?2:1);
    }
    if(frame.dragging){
        for(int i=1;i<=WIRE_STEPS;++i)ui::line(frame.wire_x[i-1],frame.wire_y[i-1],frame.wire_x[i],frame.wire_y[i],m::spot(),2);
        wire_label(frame.wire_x[WIRE_STEPS/2],frame.wire_y[WIRE_STEPS/2],frame.drag_depth,frame.drag_enabled);
    }else if(frame.selected_row<ROWS){const auto& r=frame.rows[frame.selected_row];
        for(int i=0;i<ROWS;++i)if(frame.sources[i]!=ModSource::None&&frame.sources[i]==r.src)
            wire_label(156,cable_y(156,FIRST+i*STEP,FIRST+frame.selected_row*STEP),r.depth,r.enabled);
    }
    for(int i=0;i<ROWS;++i){const int y=FIRST+i*STEP;const bool sel=uint8_t(frame.sources[i])==frame.source;
        if(frame.sources[i]!=ModSource::None){const bool hover=i==frame.hover_source;
            label(19,y-4,source_name(frame.sources[i]),sel||hover?m::spot():m::ink());socket(LEFT,y,sel||hover?m::spot():m::ghost(),true,hover);
            if(sel||hover)ui::line(19,y+8,19+ui::text_w(source_name(frame.sources[i]))-2,y+8,m::spot());
        }
        const auto& r=frame.rows[i];if(r.id.slot>=0){const bool chosen=i==frame.selected_row,hover=i==frame.hover_row;
            const uint32_t color=chosen||(hover&&!frame.blocked)?m::spot():m::ink();
            std::snprintf(text,sizeof text,"%u %s",r.id.slot+1,r.module);label(220,y-10,text,m::ghost());
            char shortname[16];std::snprintf(shortname,sizeof shortname,"%.15s",r.param_name);label(220,y+3,shortname,color);
            if(chosen||hover)ui::line(220,y+13,244,y+13,hover&&frame.blocked?m::ghost():m::spot());
            socket(RIGHT,y,chosen||(hover&&!frame.blocked)?m::spot():m::ghost(),r.src!=ModSource::None,hover&&!frame.blocked);
            if(r.src!=ModSource::None&&!source_visible(r.src)){source_tag(text,sizeof text,r.src);label(RIGHT-7-ui::text_w(text),y-11,text,m::ghost());}
            if(r.src!=ModSource::None&&!r.enabled)label(184,y+5,"off",m::ghost());
        }
    }
    if(!frame.target_count&&!frame.dragging)label(126,124,"no modules",m::ghost());
    ui::line(14,189,306,189,m::branch());label(14,200,frame.selected?(frame.enabled?"on / y":"off / y"):"amount",m::ghost());
    // Keep the old hslider's snapped float mapping and native axis at y204.
    const int depth_x=int(std::lround(80.f+(float(frame.selected?frame.depth:0)+32767.f)*174.f/65534.f));
    module_fader::horizontal(80,254,204,depth_x,depth_x,{false,!frame.selected||!frame.enabled,false},167);
    std::snprintf(text,sizeof text,frame.selected?"%+d%%":"---",percent(frame.depth));label(268,200,text,m::spot());
    label(14,226,"edit source",m::ink());label(132,226,"invert",frame.selected?m::ink():m::ghost());label(266,226,"remove",frame.selected?m::ink():m::ghost());
}
void patchbay_draw_top(){
    m::paper(400);if(!frame.valid)return;
    if(frame.page==Page::Source){source_detail_top();return;}
    char text[64];m::heading(20,14,"modulation",m::ink());std::snprintf(text,sizeof text,"chain %u / 04",frame.chain+1);label(283,24,text,m::ghost());
    label(20,54,frame.selected?"selected cable":"sigilbay / control signals",m::spot());
    if(frame.selected){
        std::snprintf(text,sizeof text,"slot %u",frame.selected_slot+1);label(332,54,text,m::ghost());
        source_tag(text,sizeof text,ModSource(frame.source));
        const int end=sigilbay_type::draw(20,96,text,60,m::spot());
        ui::line(end+10,81,end+16,86,m::branch());ui::line(end+16,86,end+10,91,m::branch());
        sigilbay_type::draw(end+28,96,frame.selected_name,380-(end+28),m::ink());
    }else label(20,80,"drag a source socket to a parameter",m::ink());
    ui::line(20,105,380,105,m::branch());label(20,118,"source",m::ghost());label(226,118,"destination",m::ghost());
    wave(20,138,170,40,true);
    if(frame.selected){label(226,139,"base",m::ghost());std::snprintf(text,sizeof text,"%+d%%",percent(frame.base));label(320,139,text,m::ink());
        label(226,155,"live",m::ghost());std::snprintf(text,sizeof text,"%+d%%",percent(frame.effective));label(320,155,text,m::ink());
        label(226,172,"amount",m::ghost());std::snprintf(text,sizeof text,"%+d%%",percent(frame.depth));label(320,172,text,m::spot());
        label(226,192,frame.enabled?"on / y to mute":"off / y to enable",m::ghost());}
    label(20,192,source_name(ModSource(frame.source)),m::ink());
    const char* hint=frame.status[0]?frame.status:"drag socket / tap source to edit";
    if(frame.dragging)hint=frame.blocked?"input occupied - choose a free port":frame.hover_source>=0?"release to change source":frame.hover_row>=0?"release to connect":"drag to an input / lift outside to cancel";
    ui::line(20,211,380,211,m::branch());label(20,225,hint,frame.status[0]||frame.dragging?m::spot():m::ghost());
    label(338,225,"b close",m::ghost());
}
} // namespace seance::platform::ui3ds
