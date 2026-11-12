#include "ui_module_focus_internal.h"
#include "strn_focus.h"
#include "swarm_layout.h"
#include "ui_param_lens.h"
#include "ui_state.h"
#include "ui_sequencer.h"
#include "ui_theme.h"
#include "editor_style.h"
#include "module_heading.h"
#include "module_fader.h"
#include "note_display_data.h"
#include "../../core/ui/draw.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <type_traits>

namespace seance::platform::ui3ds {
namespace {
namespace m = editor_style;
namespace l = swarm_layout;
using S = StranularUnit;
constexpr const char* names[12]={"position","size","rate","pitch","freeze","mix",
                               "spray","reverse","pitch env","hold","sync","cluster"};
constexpr const char* divisions[4]={"1/16","1/8","1/4","1/2"};
constexpr int performance[4]={S::P_DENS,S::P_SPRAY,S::P_PITCH,S::P_MIX};
constexpr int settings[2][2]={{S::P_PITCH_ENV,S::P_REVERSE},{S::P_SYNC,S::P_CLUSTER}};
int selected=S::P_SIZE, bound_chain=-1, bound_slot=-1;
uintptr_t identity=0; // comparison token, never dereferenced
bool setup=false, fine=false;
struct Gesture {
    Engine* engine=nullptr;
    uintptr_t unit=0;
    View view=View::Chains;
    int chain=-1,slot=-1,bank=-1,param=-1;
    int start=0,last=0;
    q15 value=0;
    bool vertical=false,setup=false,fine=false;
} gesture;
struct Frame {
    ParamInspector param={};
    q15 edit[12]={},live[12]={},wave[S::VIZ_POINTS]={};
    uint8_t heads[S::MAX_GRAINS]={},kinds[UNITS_PER_CHAIN]={};
    bool bypassed[UNITS_PER_CHAIN]={};
    uint16_t mod_links=0,mask=0;
    uint32_t span=0;
    int bpm=120;
    int8_t chain=-1,slot=-1,selected=S::P_SIZE;
    uint8_t window=0,division=0;
    bool valid=false,ready=false,frozen=false,longer=false,bypass=false,mute=false;
    bool setup=false,fine=false,lens=false;
} frame;
static_assert(sizeof(Frame)<=512 && std::is_trivially_copyable<Frame>::value,
              "swarm draw uses a bounded pointer-free capture, not audio access");
int clamp(int v,int lo=0,int hi=32767) { return std::max(lo,std::min(hi,v)); }
bool bipolar(int p) { return p==S::P_PITCH||p==S::P_PITCH_ENV; }
q15 legal(int p,int v) { return q15(clamp(v,bipolar(p)?-32767:0)); }
bool context() {
    return g_eng&&g_audio&&g_overlay==Overlay::Editor&&
        g_view==View::Chains&&
        g_edit_chain>=0&&g_edit_chain<NUM_CHAINS&&g_edit_slot>=0&&g_edit_slot<UNITS_PER_CHAIN;
}
bool ours() { return g_drag==Drag::StrnPitch; }
void release() { gesture={};if(ours())g_drag=Drag::None; }
void bind(const S& u) {
    if(bound_chain!=g_edit_chain||bound_slot!=g_edit_slot||identity!=uintptr_t(&u)) {
        release();setup=false;selected=S::P_SIZE;
        bound_chain=g_edit_chain;bound_slot=g_edit_slot;identity=uintptr_t(&u);
    }
}
q15 edit_value(const S& u,int p) {
    return u.params[p].base;
}
void set_value(S& u,int p,q15 v) { // caller holds guard
    if(edit_value(u,p)==v)return;
    u.params[p].base=v;
    mark_project_mutated();
}
void begin(S& u,int p,int x,int y,bool vertical) {
    selected=p;
    gesture={};gesture.engine=g_eng;gesture.unit=uintptr_t(&u);gesture.view=g_view;
    gesture.chain=g_edit_chain;gesture.slot=g_edit_slot;gesture.bank=g_eng->active_pattern;
    gesture.param=p;gesture.start=gesture.last=vertical?y:x;gesture.value=edit_value(u,p);
    gesture.vertical=vertical;gesture.setup=setup;gesture.fine=fine;g_drag=Drag::StrnPitch;
}
bool owns(const S& u) {
    return gesture.engine==g_eng&&gesture.unit==uintptr_t(&u)&&gesture.view==g_view&&
        gesture.chain==g_edit_chain&&gesture.slot==g_edit_slot&&gesture.bank==g_eng->active_pattern&&
        gesture.param==selected&&gesture.setup==setup&&gesture.fine==fine&&
        g_eng->chains[g_edit_chain].units[g_edit_slot]==&u;
}
uint32_t length(q15 value,bool longer) {
    return 128u+uint32_t(uint64_t(clamp(value))*(longer?32640u:8064u)/32767u);
}
uint32_t lookback(q15 value,q15 size,const Frame& f) {
    if(!f.span)return 0;
    const uint32_t n=length(size,f.longer),maximum=f.span>n+4?f.span-n-4:0;
    return std::min(uint32_t((uint64_t(clamp(value))*maximum)>>15)+(f.frozen?0:64),f.span-1);
}
int milliseconds(uint32_t frames) { return int((uint64_t(frames)*1000+ENGINE_SR/2)/ENGINE_SR); }
int cents(q15 v) { const int n=clamp(v,-32767)*2400;return (n+(n<0?-16383:16383))/32767; }
int rate_tenths(q15 v,const Frame& f) {
    uint32_t interval=32+uint32_t(uint64_t(32767-clamp(v))*2016/32767);
    if(f.live[S::P_SYNC]>16384) {
        static const int den[4]={4,2,1,1},mul[4]={1,1,1,2};
        interval=uint32_t(uint64_t(ENGINE_SR)*60*mul[f.division]/(clamp(f.bpm,20,300)*den[f.division]));
    }
    return int((uint64_t(ENGINE_SR)*10+(interval+1)/2)/(interval+1));
}
void value_text(char* out,size_t n,int p,q15 v,const Frame& f,bool live=false) {
    switch(p) {
    case S::P_POS:std::snprintf(out,n,"%dms",milliseconds(lookback(v,live?f.live[1]:f.edit[1],f)));break;
    case S::P_SIZE:std::snprintf(out,n,"%dms",milliseconds(length(v,f.longer)));break;
    case S::P_DENS:
        if(f.live[S::P_SYNC]>16384)std::snprintf(out,n,"%s sync",divisions[f.division]);
        else {const int r=rate_tenths(v,f);std::snprintf(out,n,"%d.%d/s",r/10,r%10);}break;
    case S::P_PITCH:case S::P_PITCH_ENV:{const int c=cents(v);
        std::snprintf(out,n,"%c%d.%02dst",c<0?'-':'+',std::abs(c)/100,std::abs(c)%100);break;}
    case S::P_SPRAY:std::snprintf(out,n,"%dms",milliseconds(uint32_t(uint64_t(clamp(v))*8192/32767)));break;
    case S::P_FREEZE:case S::P_HOLD:std::snprintf(out,n,"%s",v>16384?"on":"off");break;
    case S::P_SYNC:std::snprintf(out,n,"%s",v>16384?divisions[f.division]:"off");break;
    case S::P_CLUSTER:std::snprintf(out,n,"x%d",S::cluster_count(q15(clamp(v))));break;
    default:std::snprintf(out,n,"%d%%",(clamp(v)*100+16383)/32767);break;
    }
}
void text(int x,int y,const char* s,u32 color) {ui::text(x,y,s,color);}
void right(int x,int y,const char* s,u32 color) {text(x-ui::text_w(s),y,s,color);}
void center(int x,int y,const char* s,u32 color) {text(x-ui::text_w(s)/2,y,s,color);}
void line(int x,int y,int x1,int y1,u32 c) {ui::line(x,y,x1,y1,c);}
module_fader::State fader_state(int p,const Frame& f) {
    return {bool(f.mod_links&(1u<<p)),p==S::P_DENS&&f.live[S::P_SYNC]>16384,f.bypass||f.mute};
}
int fraction(int p,q15 value,int pixels) {
    const int lo=bipolar(p)?-32767:0;return (clamp(value,lo)-lo)*pixels/(32767-lo);
}
int numeral(int x,int baseline,const char* s) {
    for(;*s;++s)for(const auto& g:note_display::glyphs)if(g.character==*s&&!g.small) {
        const int stride=(g.width+7)/8;
        for(int y=0;y<g.height;++y) {
            int start=-1;
            for(int col=0;col<=g.width;++col) {
                const bool on=col<g.width&&(note_display::bitmap[g.offset+y*stride+col/8]&(128>>(col&7)));
                if(on&&start<0)start=col;
                if(!on&&start>=0){ui::rect(x+start,baseline+g.top+y,col-start,1,m::ink());start=-1;}
            }
        }
        x+=g.width+4;break;
    }
    return x;
}
void time_control(int col,const Frame& f) {
    const int p=col?S::P_SIZE:S::P_POS,x=l::time_left(col),end=l::time_right(col);
    text(x,65,names[p],f.selected==p?m::spot():m::ink());
    char value[12];const int ms=milliseconds(col?length(f.edit[p],f.longer):lookback(f.edit[p],f.edit[1],f));
    std::snprintf(value,sizeof value,"%d",ms);const int after=numeral(x,115,value);
    text(after+2,101,"ms",m::ghost());
    auto at=[&](q15 v){const int px=fraction(p,v,end-x);return col?x+px:end-px;};
    module_fader::horizontal(x,end,l::time_rail,at(f.edit[p]),at(f.live[p]),fader_state(p,f));
}
void performance_control(int slot,const Frame& f) {
    const int p=performance[slot],x=l::center(slot);
    const auto state=fader_state(p,f);
    center(x,148,p==S::P_PITCH&&f.fine?"pitch /f":names[p],f.selected==p?m::spot():m::ghost());
    auto at=[&](q15 v){return l::fader_bottom-fraction(p,v,l::fader_bottom-l::fader_top);};
    module_fader::vertical(x,l::fader_top,l::fader_bottom,at(f.edit[p]),at(f.live[p]),state,bipolar(p)?at(0):-1);
    char value[24];value_text(value,sizeof value,p,f.edit[p],f);
    center(x,208,value,state.disabled?m::ghost():m::ink());
}
void setting_control(int col,int row,const Frame& f) {
    const int p=settings[row][col],x=l::time_left(col),end=l::time_right(col),y=row?121:78;
    const u32 color=f.selected==p?m::spot():m::ink();
    text(x,y,p==S::P_PITCH_ENV?"p.env":names[p],color);
    char value[24];value_text(value,sizeof value,p,f.edit[p],f);right(end,y,value,color);
    auto at=[&](q15 v){return x+fraction(p,v,end-x);};
    module_fader::horizontal(x,end,y+20,at(f.edit[p]),at(f.live[p]),fader_state(p,f),bipolar(p)?at(0):-1);
}
void setup_controls(const Frame& f) {
    text(14,44,"settings",m::ghost());
    for(int row=0;row<2;++row)for(int col=0;col<2;++col)setting_control(col,row,f);
    char value[32];std::snprintf(value,sizeof value,"hold %s",f.edit[S::P_HOLD]>16384?"on":"off");
    text(14,167,value,f.edit[S::P_HOLD]>16384?m::spot():m::ink());
    std::snprintf(value,sizeof value,"shape %s",f.window==0?"tri":f.window==1?"hann":"trap");text(118,167,value,m::ink());
    text(230,167,f.longer?"span long":"span scan",m::ink());
    text(14,201,"pitch step",!f.fine?m::spot():m::ghost());
    text(118,201,"pitch fine",f.fine?m::spot():m::ghost());text(250,201,"zero",m::ink());
}
void memory_plot(const Frame& f) {
    constexpr int left=14,width=372,mid=132,amplitude=54,columns=125;
    bool signal=false;
    for(q15 value:f.wave)signal=signal||value!=0;
    const bool quiet=f.bypass||f.mute||!f.ready||!signal;
    const uint32_t span=std::max<uint32_t>(1u,f.span);
    const int nominal=left+width-int(uint64_t(lookback(f.live[0],f.live[1],f))*width/span);
    const int radius=int(uint64_t(clamp(f.live[S::P_SPRAY]))*8192*width/(32767ull*span));
    // This is the existing 40 raw history probes, magnitude-interpolated only.
    // Not min/max, an FFT, extra audio scanning, voice count or synthetic motion.
    for(int column=0;column<columns;++column) {
        const int phase=column*(S::VIZ_POINTS-1),i=phase/(columns-1),part=phase%(columns-1);
        const int a=std::abs(int(f.wave[i])),b=std::abs(int(f.wave[std::min(i+1,S::VIZ_POINTS-1)]));
        const int height=f.span?((a*(columns-1-part)+b*part)/(columns-1))*amplitude/32768:0;
        const int x=left+column*width/(columns-1);
        const u32 color=!quiet&&std::abs(x-nominal)<=radius?m::spot():m::branch();
        ui::rect(x,mid,1,1,color);
        for(int dy=4;dy<=height;dy+=4) {
            ui::rect(x,mid-dy,1,1,color);ui::rect(x,mid+dy,1,1,color);
        }
    }
    if(!quiet&&f.span&&f.live[S::P_MIX]>0)for(int i=0;i<S::MAX_GRAINS;++i)if(f.mask&(1u<<i)) {
        const int x=left+std::min(int(f.heads[i]),S::VIZ_POINTS-1)*width/(S::VIZ_POINTS-1);
        line(x,127,x,137,m::ink());
    }
}
} // namespace

void stranular_focus_release() {release();}
void stranular_focus_cancel() {
    release();frame={};identity=0;bound_chain=bound_slot=-1;setup=fine=false;selected=S::P_SIZE;
}
void stranular_focus_value_text(char* out,size_t size,int param,int16_t raw) {
    if(!out||!size)return;
    if(!frame.valid||param<0||param>=12){std::snprintf(out,size,"--");return;}
    value_text(out,size,param,raw,frame);
}
int stranular_focus_selected() {return selected;}
void stranular_focus_select(int param) {if(param>=0&&param<12){if(selected!=param)release();selected=param;}}
void stranular_focus_capture() {
    frame={};
    if(!context()){stranular_focus_cancel();return;}
    if(param_lens_is_open())release();
    // Another module's editor: no guard entry. Chain slots are written only by this UI thread.
    {const Unit* u=g_eng->chains[g_edit_chain].units[g_edit_slot];if(!u||u->kind!=UnitKind::Stranular){stranular_focus_cancel();return;}}
    platform::AudioOut::Guard guard(*g_audio);
    const Unit* u=g_eng->chains[g_edit_chain].units[g_edit_slot];
    if(!u||u->kind!=UnitKind::Stranular){stranular_focus_cancel();return;}
    const auto& st=*static_cast<const S*>(u);bind(st);
    if(ours()&&!owns(st))release();
    frame.valid=true;frame.chain=g_edit_chain;frame.slot=g_edit_slot;frame.selected=selected;
    frame.setup=setup;frame.fine=fine;frame.bypass=u->bypass;
    frame.mod_links=focus_mod_links(st);frame.mute=g_eng->chains[g_edit_chain].mute;frame.ready=st.ready();
    frame.longer=st.long_mode;frame.window=st.window_mode%3;frame.division=std::min(int(st.sync_division),3);
    frame.frozen=st.visual_frozen();frame.span=st.scan_span_frames();frame.bpm=g_eng->bpm;frame.lens=param_lens_is_open();
    for(int p=0;p<12;++p){frame.edit[p]=legal(p,edit_value(st,p));frame.live[p]=legal(p,st.params[p].effective);}
    frame.live[S::P_MIX]=st.visual_mix(); // preserve actual legacy freeze wet takeover
    std::memcpy(frame.wave,st.visual_wave(),sizeof frame.wave);
    std::memcpy(frame.heads,st.visual_positions(),sizeof frame.heads);frame.mask=st.visual_grains();
    g_eng->capture_param_inspector(g_edit_chain,g_edit_slot,selected,frame.param);
    for(int i=0;i<UNITS_PER_CHAIN;++i)if(const Unit* at=g_eng->chains[g_edit_chain].units[i]) {
        frame.kinds[i]=uint8_t(at->kind);frame.bypassed[i]=at->bypass;
    }
}
bool stranular_focus_touch_body(S& u,int x,int y) {
    if(!context()||param_lens_is_open())return false;
    platform::AudioOut::Guard guard(*g_audio);
    if(g_eng->chains[g_edit_chain].units[g_edit_slot]!=&u)return false;
    bind(u);release();
    if(l::contains(l::setup,x,y)){setup=!setup;return true;}
    if(y<24) {
        if(l::contains(l::bypass,x,y)){u.bypass=!u.bypass;mark_project_mutated();}
        return true; // physical X owns deletion
    }
    if(!setup) {
        if(l::contains(l::freeze,x,y)) {
            selected=S::P_FREEZE;set_value(u,selected,edit_value(u,selected)>16384?0:32767);return true;
        }
        for(int col=0;col<2;++col)if(l::contains(l::time(col),x,y)){begin(u,col?S::P_SIZE:S::P_POS,x,y,false);return true;}
        for(int slot=0;slot<4;++slot)if(l::contains(l::fader(slot),x,y)) {
            const int p=performance[slot];selected=p;
            if(p!=S::P_DENS||u.params[S::P_SYNC].effective<=16384)begin(u,p,x,y,true);
            return true;
        }
    } else {
        for(int row=0;row<2;++row)for(int col=0;col<2;++col)if(l::contains(l::extra(col,row),x,y)) {
            const int p=settings[row][col];selected=p;
            if(p==S::P_SYNC&&x>=90&&y<132) {
                {
                    if(u.params[p].base<=16384)set_value(u,p,32767);
                    else if(u.sync_division>=3){u.sync_division=0;set_value(u,p,0);}
                    else {++u.sync_division;mark_project_mutated();}
                    u.set_transport_bpm(g_eng->bpm);
                }
            } else begin(u,p,x,y,false);
            return true;
        }
        if(l::contains(l::hold,x,y)){selected=S::P_HOLD;set_value(u,selected,edit_value(u,selected)>16384?0:32767);}
        else if(l::contains(l::shape,x,y)){u.window_mode=(u.window_mode+1)%3;mark_project_mutated();}
        else if(l::contains(l::span,x,y)){u.long_mode=!u.long_mode;mark_project_mutated();}
        else if(l::contains(l::step,x,y))fine=false;
        else if(l::contains(l::fine,x,y))fine=true;
        else if(l::contains(l::zero,x,y)){selected=S::P_PITCH;set_value(u,selected,0);}
    }
    return true; // all empty space owned; no retired hidden gestures
}
void stranular_focus_drag(S& u,int x,int y) {
    if(!context()||param_lens_is_open()){release();return;}
    if(!ours()||gesture.param<0)return;
    platform::AudioOut::Guard guard(*g_audio);
    if(!owns(u)){release();return;}
    const int at=gesture.vertical?clamp(y,0,239):clamp(x,0,319);
    if(at==gesture.last)return;
    gesture.last=at;
    const int delta=gesture.vertical?gesture.start-at:at-gesture.start,p=gesture.param;
    if(p==S::P_DENS&&u.params[S::P_SYNC].effective>16384)return;
    int next=gesture.value;
    if(bipolar(p)) {
        if(gesture.fine)next+=delta*4;
        else if(const int steps=delta/6) {
            const int st=clamp((cents(gesture.value)+(gesture.value<0?-50:50))/100+steps,-24,24);
            next=(st*32767+(st<0?-12:12))/24;
        }
    } else if(p==S::P_POS)next-=delta*32767/296;
    else if(p==S::P_SIZE)next+=delta*32767/296;
    else if(p==S::P_CLUSTER) {
        if(const int steps=delta/24)next=(clamp(S::cluster_count(legal(p,gesture.value))+steps,1,4)-1)*32767/3;
    } else next+=delta*32767/(gesture.vertical?168:134);
    set_value(u,p,legal(p,next));
}
bool stranular_focus_draw_bottom() {
    const auto& f=frame;if(!f.valid)return false;
    m::paper(320);module_heading::header(UnitKind::Stranular,m::spot());
    char label[64];std::snprintf(label,sizeof label,"ch%02d / %02d",f.chain+1,f.slot+1);
    m::header_text(84,160,label,m::ghost());m::header_text(172,212,"setup",f.setup?m::spot():m::ghost());
    text(231,m::kHeaderTextY,"byp",f.bypass?m::spot():m::ghost());right(m::right_edge(320),m::kHeaderTextY,"X del",m::ghost());
    if(f.setup)setup_controls(f);
    else {
        std::snprintf(label,sizeof label,"freeze / %s",f.edit[S::P_FREEZE]>16384?"on":"off");
        const u32 color=f.edit[S::P_FREEZE]>16384&&!f.bypass&&!f.mute?m::spot():m::ghost();
        center(160,37,label,color);line(118,50,202,50,color);
        time_control(0,f);time_control(1,f);for(int slot=0;slot<4;++slot)performance_control(slot,f);
    }
    m::footer(320,"b back","l: mod >");
    return true;
}
void stranular_focus_draw_body(const S&) {stranular_focus_draw_bottom();}
bool stranular_focus_draw_top() {
    const auto& f=frame;if(!f.valid)return false;
    m::paper(400);module_heading::header(UnitKind::Stranular,m::spot());
    int x=14;
    for(int i=0;i<UNITS_PER_CHAIN;++i) {
        const auto kind=UnitKind(f.kinds[i]);if(kind==UnitKind::Empty)continue;
        const char* name=m::module_name(kind);const int w=ui::text_w(name);
        if(x>14){text(x,module_heading::kRouteY,"/",m::ghost());x+=14;}
        text(x,module_heading::kRouteY,name,i==f.slot?m::spot():m::ghost());
        if(i==f.slot)line(x,module_heading::kRouteUnderlineY,x+w-2,module_heading::kRouteUnderlineY,m::spot());
        if(f.bypassed[i])line(x,module_heading::kRouteCrossY,x+w-2,module_heading::kRouteCrossY,m::ghost());
        x+=w+8;
    }
    char label[64];std::snprintf(label,sizeof label,"ch%02d / %02d",f.chain+1,f.slot+1);
    right(m::right_edge(400),m::kHeaderTextY,label,m::ghost());
    const char* state=!f.ready?"unavailable":f.bypass?"bypassed":f.mute?"muted":!f.span?"waiting":f.frozen?"frozen":f.live[S::P_HOLD]>16384?"held":"live";
    right(386,64,state,f.frozen&&!f.bypass&&!f.mute?m::spot():m::ghost());
    if(f.lens) {
        char value[24];value_text(value,sizeof value,f.selected,f.live[f.selected],f,true);
        std::snprintf(label,sizeof label,"%s / %s",names[f.selected],value);text(14,64,label,m::spot());
    }
    memory_plot(f);
    std::snprintf(label,sizeof label,"-%dms",milliseconds(f.span));
    m::footer(400,label,f.frozen?"capture":"now");
    return true;
}
} // namespace seance::platform::ui3ds
