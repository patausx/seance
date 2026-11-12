#include "ui_module_focus_internal.h"
#include "ui_state.h"
#include "ui_sequencer.h"
#include "ui_theme.h"
#include "ui_param_lens.h"
#include "module_inspector.h"
#include "editor_style.h"
#include "module_heading.h"
#include "bit_reference.h"
#include "bit_readout.h"
#include "bit_editor_visual.h"
#include "audio_out.h"
#include "../../core/ui/draw.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <type_traits>

namespace seance::platform::ui3ds {
namespace {
namespace m=editor_style;
namespace model=bit_reference;
namespace l=bit_editor_visual;
using C=BitUnit;
union Visual {
    model::Plot legacy;l::Plot modern;
    Visual():modern{}{}
};
struct Frame {
    uint16_t mod_links=0;
    bool valid=false,modern=false,setup=false,bypass=false,mute=false;
    int8_t chain=0,slot=0,selected=0;
    q15 edit[C::P_COUNT]={},live[C::P_COUNT]={};
    char values[C::P_COUNT][18]={},drop_length[18]={},model_summary[40]={};
    uint8_t drop_percent=0;
    uint8_t kinds[UNITS_PER_CHAIN]={};bool bypassed[UNITS_PER_CHAIN]={};
    Visual plot;
} frame;
static_assert(sizeof(Frame)<=2048 && std::is_trivially_copyable<Frame>::value,
              "bounded pointer-free Bit frame, no engine reads or model work in draw");
struct Gesture {
    Engine* engine=nullptr;uintptr_t unit=0;View view=View::Chains;
    int chain=-1,slot=-1,bank=-1,param=-1,start=0;
    bool modern=false,setup=false;q15 value=0;
} gesture;
struct Key {q15 values[C::P_COUNT]={};bool modern=false,bypass=false;};
Key cached_key;Visual cached_plot;bool cache_valid=false;
int selected=C::P_BITS,bound_chain=-1,bound_slot=-1;
Engine* bound_engine=nullptr;uintptr_t bound_unit=0;bool setup=false,bound_modern=false;
struct Control { int x,y,w,h,label_x,label_y,rail_y,extent; };
constexpr Control CONTROLS[C::LEGACY_COUNT]={
    {8,27,96,46,14,32,65,82}, {112,27,96,46,118,32,65,82},
    {216,27,96,46,222,32,65,82},
    {8,172,72,48,14,177,213,60}, {84,172,72,48,90,177,213,60},
    {160,172,72,48,166,177,213,60}, {236,172,76,48,242,177,213,62}
};
constexpr const char* NAMES[C::LEGACY_COUNT]={"bits","rate","mix","jitter","drop","drift","comp"};
constexpr int main_params[4][2]={{C::P_BITS,C::P_RATE},{C::P_DRIVE,C::P_TONE},
    {C::P_JITTER,C::P_DROPOUT},{C::P_MIX,C::P_GAIN}};
constexpr const char* main_names[4][2]={{"bits","rate"},{"drive","tone"},{"jitter","dropout"},{"mix","wet gain"}};
constexpr int setup_params[4]={C::P_DRIFT,C::P_COMP,C::P_DITHER,C::P_DROP_TIME};
constexpr const char* setup_names[4]={"drift","comp curve","dither","drop length"};
constexpr const char* setup_help[4]={"0 = neutral / signed sweep direction","static curve / not a compressor",
    "tpdf / 0 off / 100 one lsb peak","fixed event / 100% drop = wet mute"};
bool capture_context() {
    return g_eng && g_audio && g_overlay==Overlay::Editor &&
        g_view==View::Chains &&
        g_edit_chain>=0 && g_edit_chain<NUM_CHAINS && g_edit_slot>=0 && g_edit_slot<UNITS_PER_CHAIN;
}
bool context() { return !param_lens_is_open() && capture_context(); }
void release(){gesture={};if(g_drag==Drag::BitBits)g_drag=Drag::None;}
void bind(const C& u){
    if(bound_engine!=g_eng||bound_unit!=uintptr_t(&u)||bound_chain!=g_edit_chain||
       bound_slot!=g_edit_slot||bound_modern!=u.modern()){
        release();setup=false;selected=C::P_BITS;bound_engine=g_eng;bound_unit=uintptr_t(&u);
        bound_chain=g_edit_chain;bound_slot=g_edit_slot;bound_modern=u.modern();cache_valid=false;
    }
}
q15 edit_value(const C& u,int p) {
    return u.params[p].base;
}
void set_value(C& u,int p,q15 value){
    if(p<0||p>=u.num_params)return;
    if(u.params[p].base==value)return;
    u.params[p].base=value;
    mark_project_mutated();cache_valid=false;
}
int clamp(int v,int p) { return std::max(p==C::P_DRIFT?-32767:0,std::min(32767,v)); }
u32 color(int p) { return frame.selected==p?m::spot():m::ink(); }
int position(q15 value,int p,int left,int right) {
    const bool bi=p==C::P_DRIFT;
    return left+(clamp(value,p)+(bi?32767:0))*(right-left)/(bi?65534:32767);
}
void begin(C& u,int p,int x){
    selected=p;gesture={};gesture.engine=g_eng;gesture.unit=uintptr_t(&u);gesture.view=g_view;
    gesture.chain=g_edit_chain;gesture.slot=g_edit_slot;gesture.bank=g_eng->active_pattern;
    gesture.param=p;gesture.start=x;gesture.value=edit_value(u,p);gesture.setup=setup;
    gesture.modern=u.modern();g_drag=Drag::BitBits;g_drag_param=p;
}
bool owns(const C& u){return gesture.engine==g_eng&&gesture.unit==uintptr_t(&u)&&gesture.view==g_view&&
    gesture.chain==g_edit_chain&&gesture.slot==g_edit_slot&&gesture.bank==g_eng->active_pattern&&
    gesture.param==selected&&gesture.setup==setup&&
    gesture.modern==u.modern()&&g_eng->chains[g_edit_chain].units[g_edit_slot]==&u;}
void value_text(char* out,size_t size,int p,q15 value,bool modern){
    bit_readout::value_text(out,size,p,value,modern);
}
void capture_plot(){
    Key key;key.modern=frame.modern;key.bypass=frame.bypass;
    std::copy(frame.live,frame.live+C::P_COUNT,key.values);
    if(!cache_valid||key.modern!=cached_key.modern||key.bypass!=cached_key.bypass||
       !std::equal(std::begin(key.values),std::end(key.values),std::begin(cached_key.values))){
        // Placement construction only activates the bounded union member; no allocation.
        if(key.modern){new(&cached_plot.modern) l::Plot{};l::calculate(key.values,key.bypass,cached_plot.modern);}
        else {new(&cached_plot.legacy) model::Plot{};model::plot(key.values,key.bypass,cached_plot.legacy);}
        cached_key=key;cache_valid=true;
    }
    frame.plot=cached_plot;
}
void right(int x,int y,const char* text,u32 c){ui::text(x-ui::text_w(text),y,text,c);}
void control(int p,int left,int end,int y,const char* name){
    ui::text(left,y,name,color(p));right(end,y,frame.values[p],color(p));
    module_fader::horizontal(left+1,end-1,y+20,
        position(frame.edit[p],p,left+1,end-1),position(frame.live[p],p,left+1,end-1),
        {bool(frame.mod_links&(1u<<p)),false,frame.bypass||frame.mute},
        p==C::P_DRIFT?position(0,p,left+1,end-1):-1);
}
} // namespace
int bit_focus_selected(){return selected;}
void bit_focus_select(int p){if(p>=0&&p<C::P_COUNT){if(p!=selected)release();selected=p;}}
void bit_focus_release(){release();}
bool bit_focus_modern(){return frame.valid&&frame.modern;}
void bit_focus_value_text(char* out,size_t size,int p,q15 v){value_text(out,size,p,v,bit_focus_modern());}
bool bit_focus_back(){if(!context()||!setup)return false;release();setup=false;return true;}
bool bit_focus_touch_body(C& u,int tx,int ty){
    if(!context()){release();return false;}
    platform::AudioOut::Guard guard(*g_audio);
    if(g_eng->chains[g_edit_chain].units[g_edit_slot]!=&u)return false;
    bind(u);release();
    if(u.modern()&&hit(tx,ty,198,0,48,26)){setup=!setup;return true;}
    if(!u.modern()&&hit(tx,ty,164,0,76,26)){
        {C defaults;
            for(int p=C::LEGACY_COUNT;p<C::P_COUNT;++p)u.params[p]=defaults.params[p];
            for(auto& scene:g_eng->scenes[g_edit_chain])if(scene.kind[g_edit_slot]==UnitKind::Bitcrush)
                for(int p=C::LEGACY_COUNT;p<C::P_COUNT;++p)scene.base[g_edit_slot][p]=defaults.params[p].base;
            u.set_modern(true);mark_project_mutated();bind(u);}
        return true;
    }
    if(ty<26){if(hit(tx,ty,246,0,38,25)){u.bypass=!u.bypass;mark_project_mutated();}return true;}
    if(u.modern()){
        if(setup){for(int row=0;row<4;++row){const int p=setup_params[row],y=l::kSetupY[row];
            if(!hit(tx,ty,8,y-5,304,35))continue;
            selected=p;begin(u,p,tx);return true;}}
        else for(int row=0;row<4;++row)for(int col=0;col<2;++col){
            const int p=main_params[row][col],x=l::kControlLeft[col],end=l::kControlRight[col],y=l::kMainY[row];
            if(!hit(tx,ty,x-6,y-5,end-x+12,35))continue;
            selected=p;begin(u,p,tx);return true;}
        return true;
    }
    for(int p=0;p<C::LEGACY_COUNT;++p){const auto& c=CONTROLS[p];if(!hit(tx,ty,c.x,c.y,c.w,c.h))continue;
        selected=p;
        begin(u,p,tx);return true;}
    return true;
}
void bit_focus_drag(C& u,int tx,int){
    if(gesture.param<0)return;
    if(!context()||g_drag!=Drag::BitBits){release();return;}
    platform::AudioOut::Guard guard(*g_audio);
    if(!owns(u)){release();return;}
    const int p=gesture.param,range=p==C::P_DRIFT?65534:32767;
    const int extent=u.modern()?296:CONTROLS[p].extent;
    set_value(u,p,q15(clamp(int(gesture.value)+(tx-gesture.start)*range/extent,p)));
}
void bit_focus_capture(){
    frame={};if(!capture_context()){release();bound_engine=nullptr;bound_unit=0;setup=false;selected=C::P_BITS;cache_valid=false;return;}
    {
        platform::AudioOut::Guard guard(*g_audio);
        const auto& chain=g_eng->chains[g_edit_chain];const Unit* unit=chain.units[g_edit_slot];
        if(!unit||unit->kind!=UnitKind::Bitcrush){release();bound_engine=nullptr;bound_unit=0;setup=false;selected=C::P_BITS;cache_valid=false;return;}
        const auto& u=*static_cast<const C*>(unit);bind(u);
        if(gesture.param>=0&&!owns(u))release();
        if(selected>=u.num_params){selected=C::P_BITS;release();}
        frame.valid=true;frame.modern=u.modern();frame.setup=setup;frame.bypass=u.bypass;frame.mute=chain.mute;
        frame.chain=g_edit_chain;frame.slot=g_edit_slot;frame.selected=selected;frame.mod_links=focus_mod_links(u);
        for(int i=0;i<UNITS_PER_CHAIN;++i){frame.kinds[i]=chain.units[i]?uint8_t(chain.units[i]->kind):uint8_t(UnitKind::Empty);
            frame.bypassed[i]=chain.units[i]&&chain.units[i]->bypass;}
        for(int p=0;p<u.num_params;++p){frame.edit[p]=edit_value(u,p);frame.live[p]=u.modern()?u.control_value(p):u.params[p].effective;}
    }
    for(int p=0;p<(frame.modern?C::P_COUNT:C::LEGACY_COUNT);++p){
        if(!frame.modern&&(p==C::P_JITTER||p==C::P_DRIFT))
            std::snprintf(frame.values[p],sizeof frame.values[p],p==C::P_DRIFT?"%+d%%":"%d%%",int(frame.edit[p])*100/32767);
        else value_text(frame.values[p],sizeof frame.values[p],p,frame.edit[p],frame.modern);
    }
    if(frame.modern){const q15* s=frame.live;
        char bits[18],rate[18];value_text(bits,sizeof bits,C::P_BITS,s[C::P_BITS],true);
        value_text(rate,sizeof rate,C::P_RATE,s[C::P_RATE],true);
        std::snprintf(frame.model_summary,sizeof frame.model_summary,"%s / %s",bits,rate);
        value_text(frame.drop_length,sizeof frame.drop_length,C::P_DROP_TIME,s[C::P_DROP_TIME],true);
        frame.drop_percent=uint8_t((uint32_t(std::max(0,int(s[C::P_DROPOUT])))*100+16383)/32767);}
    capture_plot(); // actual private processing outside audio guard / before GPU
}
void bit_focus_draw(const C&){
    m::paper(320);if(!frame.valid)return;
    module_heading::header(UnitKind::Bitcrush,m::spot());
    char text[24];std::snprintf(text,sizeof text,"ch%02d / %02d",frame.chain+1,frame.slot+1);
    ui::text(64,m::kHeaderTextY,text,m::ghost());
    if(frame.modern)ui::text(206,m::kHeaderTextY,frame.setup?"main":"setup",m::spot());
    else ui::text(170,m::kHeaderTextY,"extend",m::spot());
    ui::text(258,m::kHeaderTextY,"byp",frame.bypass?m::spot():m::ghost());
    ui::text(m::right_edge(320)-ui::text_w("x"),m::kHeaderTextY,"x",m::ghost());
    if(frame.bypass)ui::line(258,m::kHeaderMarkY,275,m::kHeaderMarkY,m::spot());
    if(param_lens_is_open()){m::footer(320,"b back","l: mod >");return;}
    if(frame.modern){
        if(frame.setup){for(int row=0;row<4;++row){const int y=l::kSetupY[row];
            control(setup_params[row],14,306,y,setup_names[row]);ui::text(14,y+30,setup_help[row],m::ghost());}}
        else for(int row=0;row<4;++row)for(int col=0;col<2;++col)
            control(main_params[row][col],l::kControlLeft[col],l::kControlRight[col],l::kMainY[row],main_names[row][col]);
        m::footer(320,frame.setup?"b main":"b back","l: mod >");
        return;
    }
    for(int p=0;p<C::LEGACY_COUNT;++p){const auto& c=CONTROLS[p];
        ui::text(c.label_x,c.label_y,NAMES[p],color(p));ui::text(c.label_x,c.label_y+15,frame.values[p],color(p));
        module_fader::horizontal(c.label_x,c.label_x+c.extent,c.rail_y,
            position(frame.edit[p],p,c.label_x,c.label_x+c.extent),position(frame.live[p],p,c.label_x,c.label_x+c.extent),
            {bool(frame.mod_links&(1u<<p)),false,frame.bypass||frame.mute},
            p==C::P_DRIFT?position(0,p,c.label_x,c.label_x+c.extent):-1);
    }
    ui::line(14,83,24,83,m::ghost());ui::text(30,79,"test triangle",m::ghost());
    const u32 out=frame.mute?m::ghost():m::spot();ui::line(212,83,222,83,out);
    ui::text(228,79,frame.bypass?"byp dry":frame.mute?"muted":"bit model",out);
    ui::line(model::X0,model::ZERO,303,model::ZERO,m::branch());
    for(int i=1;i<model::COLUMNS;++i){const int x=model::X0+i;
        ui::line(x-1,frame.plot.legacy.source[i-1],x,frame.plot.legacy.source[i],m::ghost());}
    for(int i=1;i<model::COLUMNS;++i){const int x=model::X0+i;
        ui::line(x-1,frame.plot.legacy.shaped[i-1],x,frame.plot.legacy.shaped[i-1],out);
        if(frame.plot.legacy.shaped[i]!=frame.plot.legacy.shaped[i-1])
            ui::line(x,frame.plot.legacy.shaped[i-1],x,frame.plot.legacy.shaped[i],out);}
    ui::text(14,158,"9ms / fixed seed / not audio",m::ghost());ui::line(14,223,306,223,m::branch());
    m::footer(320,"b back","l: mod >");
}
bool bit_focus_draw_top(){
    if(!frame.valid||!frame.modern||param_lens_is_open())return false;
    m::paper(400);module_heading::header(UnitKind::Bitcrush,m::spot());
    char text[48];std::snprintf(text,sizeof text,"ch%02d / %02d",frame.chain+1,frame.slot+1);
    right(m::right_edge(400),m::kHeaderTextY,text,m::ghost());
    m::header_text(64,306,frame.model_summary,m::ink());
    int rx=14;for(int i=0;i<UNITS_PER_CHAIN;++i){const auto kind=UnitKind(frame.kinds[i]);if(kind==UnitKind::Empty)continue;
        if(rx>14){ui::text(rx,module_heading::kRouteY,"/",m::ghost());rx+=14;}
        const char* name=m::module_name(kind);const int w=ui::text_w(name);if(rx+w>386)break;
        ui::text(rx,module_heading::kRouteY,name,i==frame.slot?m::spot():m::ghost());
        if(i==frame.slot)ui::line(rx,module_heading::kRouteUnderlineY,rx+w-2,module_heading::kRouteUnderlineY,m::spot());
        if(frame.bypassed[i])ui::line(rx,module_heading::kRouteCrossY,rx+w-2,module_heading::kRouteCrossY,m::ghost());
        rx+=w+8;
    }
    const u32 out=frame.mute?m::ghost():m::spot();
    ui::line(14,59,25,59,m::ghost());ui::text(31,55,"in",m::ghost());
    ui::line(62,59,73,59,out);ui::text(79,55,frame.bypass?"dry":"out",out);
    right(386,55,"125hz test triangle",m::ghost());
    for(int y:{77,113,149})ui::line(l::kLeft,y,l::kRight,y,palette::shadow());
    ui::text(14,77,"+1",m::ghost());ui::text(14,110,"0",m::ghost());ui::text(14,142,"-1",m::ghost());
    for(int i=0;i<=4;++i){const int x=l::kLeft+i*(l::kRight-l::kLeft)/4;
        ui::line(x,77,x,149,palette::shadow());std::snprintf(text,sizeof text,i==4?"%dms":"%d",i*8);
        if(i==4)right(x,157,text,m::ghost());else ui::text(i?x-ui::text_w(text)/2:x,157,text,m::ghost());}
    const auto& plot=frame.plot.modern;
    for(int i=0;i<l::kColumns;++i){const int x=l::kLeft+i;const auto& c=l::source.columns[i];
        if(i)ui::line(x-1,l::kZero-l::source.columns[i-1].last,x,l::kZero-c.first,m::ghost());
        ui::line(x,l::kZero-c.high,x,l::kZero-c.low,m::ghost());}
    for(int i=0;i<l::kColumns;++i){const int x=l::kLeft+i;const auto& c=plot.out[i];
        if(i)ui::line(x-1,l::kZero-plot.out[i-1].last,x,l::kZero-c.first,out);
        ui::line(x,l::kZero-c.high,x,l::kZero-c.low,out);}
    ui::text(14,175,"clock / dropout",m::ghost());
    std::snprintf(text,sizeof text,"%u%% / %s",unsigned(frame.drop_percent),frame.drop_length);
    right(386,175,text,m::ghost());
    ui::line(l::kLeft,193,l::kRight,193,palette::shadow());ui::line(l::kLeft,211,l::kRight,211,palette::shadow());
    for(int i=0;i<l::kColumns;++i){const int x=l::kLeft+i;
        if(l::event(plot.capture,i))ui::line(x,190,x,196,m::ghost());
        if(l::event(plot.drop,i))ui::line(x,207,x,211,out);}
    m::footer(400,frame.mute?"muted / reset ref / not audio":
        frame.bypass?"bypass / reset ref / not audio":"reset reference / not live audio","out --");
    return true;
}
} // namespace seance::platform::ui3ds
