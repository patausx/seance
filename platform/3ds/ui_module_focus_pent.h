// Included ONCE by ui_module_focus.cpp. UI/controller only; no DSP ownership.
#pragma once
#include "ui_module_focus_internal.h"
#include "pent_ui.h"
#include "pent_readout.h"
#include "pent_visual.h"
#include "pent_motion.h"
#include "ui_state.h"
#include "ui_theme.h"
#include "ui_param_lens.h"
#include "ui_sequencer.h"
#include "editor_style.h"
#include "module_heading.h"
#include "module_fader_painter.h"
#include "sigil_surface.h"
#include "audio_out.h"
#include <cstdio>
#include <cstring>
namespace seance::platform::ui3ds {
namespace pent_private {
namespace m=editor_style;namespace g=pent_ui;using C=PentUnit;
g::Frame frame;
int selected=C::P_ROOT,voice=0,focus=0;g::Menu menu=g::Menu::None;
word_menu::State motion;
bool wait_lift=false,blocked=false,dirty=false;
int anchor_x=14,anchor_y=228;char anchor[16]={};
struct Owner {
    Engine* engine=nullptr;platform::AudioOut* audio=nullptr;uintptr_t unit=0;
    int chain=-1,slot=-1,bank=-1,selection=-1,voice=-1;
    View view=View::Chains;bool bypass=false,mute=false,busy=false;
    uint8_t width=0;uint16_t overrides=0;uint64_t palette=0;uint32_t lifecycle=0;
};
struct Touch {Owner owner{};g::Box box{};int action=-1,param=-1,x=0,y=0,last_x=0,left=0,right=0;q15 origin=0;bool selected_voice=false;} touch;
Owner menu_owner{},view_owner{},motion_owner{},feedback_owner{};pent_motion::State ornament;
pent_reactive::State response;pent_reactive::Feedback feedback;
enum Action {OpenRatios=20,OpenWidth,ChooseVoice=30,Back=80};
bool context(){return g_eng&&g_audio&&g_overlay==Overlay::Editor&&
    g_view==View::Chains&&
    g_edit_chain>=0&&g_edit_chain<NUM_CHAINS&&g_edit_slot>=0&&g_edit_slot<UNITS_PER_CHAIN;}
C* current(){if(!context())return nullptr;auto* u=g_eng->chains[g_edit_chain].units[g_edit_slot];return u&&u->kind==UnitKind::Pent?static_cast<C*>(u):nullptr;}
uint16_t overrides(const C& u){uint16_t mask=0;for(int p=0;p<C::P_COUNT;++p){const auto& d=u.params[p];if(d.temp_override||d.performance_override||d.audition_override||d.step_lock_override)mask|=uint16_t(1u<<p);}return mask;}
Owner owner(const C& u){return {g_eng,g_audio,uintptr_t(&u),g_edit_chain,g_edit_slot,g_eng->active_pattern,
    selected,voice,g_view,u.bypass,g_eng->chains[g_edit_chain].mute,g_eng->recording_active_or_pending(),uint8_t(u.width),overrides(u),palette::revision(),g_audio->lifecycle_epoch()};}
bool matches(const Owner& o,const C& u){return context()&&!param_lens_is_open()&&o.engine==g_eng&&o.audio==g_audio&&o.unit==uintptr_t(&u)&&
    g_eng->chains[g_edit_chain].units[g_edit_slot]==&u&&o.chain==g_edit_chain&&o.slot==g_edit_slot&&o.view==g_view&&
    o.bank==g_eng->active_pattern&&
    o.width==uint8_t(u.width)&&o.overrides==overrides(u)&&o.bypass==u.bypass&&o.mute==g_eng->chains[g_edit_chain].mute&&
    o.busy==g_eng->recording_active_or_pending()&&o.palette==palette::revision()&&o.lifecycle==g_audio->lifecycle_epoch()&&o.selection==selected&&o.voice==voice;}
void cancel_touch(){if(touch.owner.unit)wait_lift=true;touch={};if(g_drag==Drag::PentParam)g_drag=Drag::None;}
void cancel(){cancel_touch();if(menu!=g::Menu::None)wait_lift=true;menu=g::Menu::None;menu_owner={};dirty=false;motion={};}
void identify(const C& u){if(view_owner.engine!=g_eng||view_owner.audio!=g_audio||view_owner.unit!=uintptr_t(&u)||view_owner.chain!=g_edit_chain||view_owner.slot!=g_edit_slot){
    cancel();view_owner=owner(u);}}
void motion_context(const C& u){const auto next=owner(u);
    if(motion_owner.engine!=next.engine||motion_owner.audio!=next.audio||motion_owner.unit!=next.unit||
        motion_owner.chain!=next.chain||motion_owner.slot!=next.slot||motion_owner.width!=next.width||
        motion_owner.lifecycle!=next.lifecycle||motion_owner.view!=next.view||motion_owner.bank!=next.bank)
        {ornament.reset();response.reset();}
    motion_owner=next;
}
void feedback_context(const C& u){const auto next=owner(u);
    if(feedback_owner.engine!=next.engine||feedback_owner.audio!=next.audio||feedback_owner.unit!=next.unit||
        feedback_owner.chain!=next.chain||feedback_owner.slot!=next.slot||feedback_owner.width!=next.width||
        feedback_owner.lifecycle!=next.lifecycle||feedback_owner.view!=next.view||feedback_owner.bank!=next.bank||
        feedback_owner.overrides!=next.overrides||feedback_owner.bypass!=next.bypass||feedback_owner.mute!=next.mute||feedback_owner.busy!=next.busy)
        feedback.cancel();
    feedback_owner=next;
}
void acknowledge(const C& u,uint8_t mask){feedback_context(u);feedback.acknowledge(mask,osGetTime());}
bool allowed(const C& u,int p){return p>=0&&p<C::P_COUNT&&!g_eng->recording_active_or_pending()&&
    !(overrides(u)&(1u<<p));}
bool width_allowed(const C& u){return !g_eng->recording_active_or_pending()&&!overrides(u);}
q15 edit(const C& u,int p){
    return u.params[p].base;
}
bool write(C& u,int p,q15 v){if(!allowed(u,p)||v==edit(u,p))return false;
    u.params[p].base=v;
    mark_project_mutated();acknowledge(u,pent_reactive::parameter_mask(p));return true;
}
void begin(C& u,int x,int y,int action,g::Box box){touch={};touch.owner=owner(u);touch.x=touch.last_x=x;touch.y=y;touch.action=action;touch.box=box;g_drag=Drag::PentParam;}
void open(C& u,g::Menu value){
    cancel_touch();menu=value;focus=0;menu_owner=owner(u);dirty=true;
    const char* label=value==g::Menu::Width?(u.width==C::Width::Stereo?"stereo >":"mono >"):"ratios >";
    anchor_x=value==g::Menu::Width?306-ui::text_w(label):14;
    anchor_y=value==g::Menu::Width?m::kHeaderTextY:m::kTextY;
    std::snprintf(anchor,sizeof anchor,"%s",label);
    acknowledge(u,value==g::Menu::Ratio?uint8_t(1u<<voice):uint8_t(31));
}
bool menu_back(){if(menu==g::Menu::None)return false;cancel();return true;}
void select(int p){if(p<0||p>=C::P_COUNT)return;if(p!=selected){cancel_touch();selected=p;if(p>=C::P_RATIO_A&&p<=C::P_RATIO_E)voice=p-C::P_RATIO_A;}
    // Selection is UI-only. An open disclosure follows the new selection without
    // retaining a stale action or performing a base write.
    if(menu!=g::Menu::None)menu_owner.selection=selected,menu_owner.voice=voice;
}
void build_rows(g::Frame& f){
    const auto row=[&](const char* text,g::Op op,int p=-1,q15 value=0){auto& r=f.rows[f.count++];std::snprintf(r.label,sizeof r.label,"%s",text);r.op=op;r.param=int8_t(p);r.value=value;
        r.enabled=op==g::Op::Preset?!(f.disabled&(1u<<p)):!f.width_disabled;};
    if(f.menu==g::Menu::Ratio)for(const auto& r:pent_readout::ratios){char text[16];std::snprintf(text,sizeof text,"%u/%u",unsigned(r.n),unsigned(r.d));row(text,g::Op::Preset,C::P_RATIO_A+f.voice,C::ratio_value(r.n,r.d));}
    else if(f.menu==g::Menu::Width){row("mono",g::Op::Mono);row("stereo",g::Op::Stereo);}
    f.anchor_x=int16_t(anchor_x);f.anchor_y=int16_t(anchor_y);std::memcpy(f.anchor,anchor,sizeof anchor);
    // One optional palette, not a disclosure hierarchy for every letter.
    f.layout=f.menu==g::Menu::Ratio?word_menu::Layout{14,34,64,16,14,306}:word_menu::Layout{230,250,44,26,230,306};
    f.menu_focus=uint8_t(std::clamp(focus,0,std::max(0,int(f.count)-1)));
}
void activate(C& u,int row){if(row<0||row>=frame.count||!frame.rows[row].enabled)return;const auto r=frame.rows[row];
    if(r.op==g::Op::Preset){if(allowed(u,r.param)){select(r.param);write(u,r.param,r.value);menu_owner=owner(u);}}
    else if(width_allowed(u)){const auto next=r.op==g::Op::Mono?C::Width::Mono:C::Width::Stereo;
        if(u.width!=next&&u.set_width(next)){mark_project_mutated();acknowledge(u,31);}
        cancel();}
}
void title(const sigil_surface::Painter& pen){const auto* l=module_heading::label(UnitKind::Pent);if(!l)return;const int stride=(l->width+7)/8;
    const auto* bits=module_heading_data::bitmap+l->offset;
    for(int y=0;y<l->height;++y){int start=-1;for(int x=0;x<=l->width;++x){const bool on=x<l->width&&(bits[y*stride+x/8]&(0x80>>(x&7)));
        if(on&&start<0)start=x;
        if(!on&&start>=0){pen.rect(m::kLeft+start,module_frame_layout::kTopInset+y,x-start,1,m::spot());start=-1;}}}}
struct FadePen {const sigil_surface::Painter& pen;unsigned alpha;
    void rect(int x,int y,int w,int h,uint32_t c)const{pen.rect(x,y,w,h,word_menu::faded(c,alpha));}};
template<class Pen> void slider(const Pen& pen,const g::Frame& f,int p,int left,int right,int y){
    module_fader::horizontal(pen,left,right,y,g::parameter_x(f.edit[p],left,right),g::parameter_x(f.live[p],left,right),
        {bool(f.linked&(1u<<p)),bool(f.disabled&(1u<<p)),f.bypass||f.mute||!f.eligible});
}
void closed(const sigil_surface::Painter& pen,const g::Frame& f){
    title(pen);pen.text(83,m::kHeaderTextY,f.context,m::ghost());
    pen.right(m::right_edge(320),m::kHeaderTextY,f.width?"stereo >":"mono >",m::ghost());
    for(int p:{C::P_ROOT,C::P_GAIN,C::P_SPREAD,C::P_BOND,C::P_DRIFT,C::P_DRIFT_DEPTH,C::P_DRIVE}){const auto b=g::main_parameter(p);
        pen.text(b.x,b.y+5,pent_readout::names[p],f.selected==p?m::spot():m::ink());
        pen.right(b.x+b.w,b.y+(p==C::P_DRIFT||p==C::P_DRIFT_DEPTH?17:5),f.values[p],m::ghost());
        slider(pen,f,p,g::rail_left(p),g::rail_right(p),g::rail_y(p));}
    for(int i=0;i<5;++i){const int x=14+i*58,p=C::P_RATIO_A+i;const char letter[]={char('a'+i),0};
        pen.text(x,76,letter,i==f.voice?m::spot():m::ghost());
        pen.text(x,92,f.values[p],i==f.voice?m::ink():m::ghost());
        slider(pen,f,p,g::rail_left(p),g::rail_right(p),g::rail_y(p));}
    pen.text(m::kLeft,m::kTextY,"ratios > / l mod",m::ghost());
    pen.right(m::right_edge(320),m::kTextY,"b back",m::ghost());
}
struct TreePen {const sigil_surface::Painter& pen;const g::Frame& f;
    void line(int x,int y,int xx,int yy,uint32_t c)const{pen.line(x,y,xx,yy,c);}
    void text(int x,int y,const char* s,uint32_t c)const{
        const int row=(y-f.layout.y)/std::max(1,f.layout.step);
        if(x==f.layout.x&&row>=0&&row<f.count&&!f.rows[row].enabled)c=(m::ghost()&0xffffffu)|(c&0xff000000u);
        pen.text(x,y,s,c);
    }
};
void tree(const sigil_surface::Painter& pen,const g::Frame& f){
    pen.text(f.anchor_x,f.anchor_y,f.anchor,m::spot());
    // The shared palette follows the chosen voice immediately, not the blurred
    // parent's old selection; changing a letter never creates another menu.
    pen.text(83,m::kHeaderTextY,f.context,m::ghost());
    word_menu::draw(TreePen{pen,f},f.layout,f.motion,f.count,f.menu_focus,[&](int i){return f.rows[i].label;},[](int,int,unsigned,bool){},m::ink(),m::spot(),m::branch());
}
} // namespace pent_private
namespace pent_ui {const Frame& frozen(){return pent_private::frame;}
bool draw_bottom(){using namespace pent_private;const auto& f=frame;if(!f.valid)return false;
    if(f.menu==Menu::None){m::paper(320);module_heading::header(UnitKind::Pent,m::spot());closed(sigil_surface::Painter{},f);return true;}
    if(!sigil_surface::bottom_cache.draw()){m::paper(320);closed(sigil_surface::Painter{},f);m::paper_rect(0,0,320,240,185);}
    tree(sigil_surface::Painter{},f);
    if(f.menu==Menu::Width)m::footer(320,"b closes",f.width_disabled?"width locked":f.unsupported?"unsupported chain":"explicit width");
    else{m::footer(320,nullptr,"b / outside closes");}return true;
}}
int pent_focus_selected(){return pent_private::selected;}
void pent_focus_select(int p){pent_private::select(p);}
void pent_focus_release(){pent_private::cancel();pent_private::feedback.cancel();}
// Called under the existing main audio guard AFTER a genuine chain-trigger
// request. This acknowledges a request (possibly quantized), not DAC activity.
void pent_focus_chain_trigger_requested(int chain){using namespace pent_private;
    if(chain!=g_edit_chain||menu!=g::Menu::None||param_lens_is_open())return;
    if(auto* u=current())acknowledge(*u,31);
}
bool pent_focus_back(){return pent_private::menu_back();}
bool pent_focus_menu_modal(){return pent_private::menu!=pent_ui::Menu::None;}
bool pent_focus_menu_input(u32& down,u32 held,u32 up,bool is_blocked){using namespace pent_private;
    const bool had=menu!=g::Menu::None,tail=wait_lift;blocked=is_blocked||bool((down|held)&~u32(KEY_TOUCH|KEY_ZL));
    if(blocked&&(held&KEY_TOUCH)&&touch.owner.unit)cancel_touch();
    // A/X belonged to the visible disclosure. Revoking its owner must not
    // reinterpret that same edge as a global chain trigger/delete action.
    const auto revoke=[&](){cancel();feedback.cancel();if(had)down&=~u32(KEY_A|KEY_X);};
    if(!context()||is_blocked||param_lens_is_open()){if(touch.owner.unit||had)revoke();}
    // Another module's editor and no owner: the guarded branch has nothing to do, skip its entry.
    else if(touch.owner.unit||had||current()){platform::AudioOut::Guard guard(*g_audio);auto* u=current();
        if(!u){if(touch.owner.unit||had)revoke();}
        else if((touch.owner.unit&&(g_drag!=Drag::PentParam||!matches(touch.owner,*u)))||(had&&!matches(menu_owner,*u)))revoke();
        else if(down&~u32(KEY_TOUCH|KEY_ZL)){
            if(touch.owner.unit)cancel_touch();
            if(held&KEY_TOUCH)wait_lift=true;
            if(down&(KEY_L|KEY_SELECT|KEY_START|KEY_Y)){if(had)cancel();}
            else if(had){if(down&KEY_B)menu_back();
                else if(down&KEY_UP)focus=(focus+std::max(1,int(frame.count))-1)%std::max(1,int(frame.count));
                else if(down&KEY_DOWN)focus=(focus+1)%std::max(1,int(frame.count));
                else if(menu==g::Menu::Ratio&&(down&(KEY_LEFT|KEY_RIGHT))){voice=(voice+5+(down&KEY_LEFT?-1:1))%5;select(C::P_RATIO_A+voice);menu_owner=owner(*u);acknowledge(*u,uint8_t(1u<<voice));}
                else if(down&KEY_A)activate(*u,focus);
                else if(down&KEY_X)menu_back();
                down&=KEY_TOUCH|KEY_ZL;
            }else if(!(held&(KEY_L|KEY_R))){
                if(down&(KEY_UP|KEY_DOWN)){select((selected+C::P_COUNT+(down&KEY_UP?-1:1))%C::P_COUNT);acknowledge(*u,pent_reactive::parameter_mask(selected));down&=~u32(KEY_UP|KEY_DOWN);}
                else if(down&(KEY_LEFT|KEY_RIGHT)){voice=(voice+5+(down&KEY_LEFT?-1:1))%5;select(C::P_RATIO_A+voice);acknowledge(*u,uint8_t(1u<<voice));down&=~u32(KEY_LEFT|KEY_RIGHT);}
                // A retains its real chain action; X opens the one rational
                // palette for the retained voice, never another parameter page.
                else if(down&KEY_X){select(C::P_RATIO_A+voice);open(*u,g::Menu::Ratio);down&=~u32(KEY_X);}
            }
        }
    }
    if(wait_lift&&((up&KEY_TOUCH)||!(held&KEY_TOUCH)))wait_lift=false;
    return had||tail||wait_lift;
}
bool pent_focus_touch_body(PentUnit& u,int x,int y){using namespace pent_private;
    if(wait_lift||blocked||!context()||param_lens_is_open())return true;
    platform::AudioOut::Guard guard(*g_audio);if(current()!=&u||touch.owner.unit)return true;identify(u);if(wait_lift)return true;
    if(menu!=g::Menu::None){if(!matches(menu_owner,u)){cancel();return true;}
        const auto& l=frame.layout;const int row=g::menu_row(frame,x,y);
        if(x<l.left||x>=l.right||y<l.y-4||row<0||row>=frame.count){cancel();return true;}
        focus=row;const auto r=frame.rows[row];const g::Box box{l.left,l.y-4+row*l.step,l.right-l.left,l.step};
        if(r.enabled)begin(u,x,y,row,box);
        return true;
    }
    if(g::presets.has(x,y)){begin(u,x,y,OpenRatios,g::presets);return true;}
    if(g::back.has(x,y)){begin(u,x,y,Back,g::back);return true;}
    if(g::width.has(x,y)){if(width_allowed(u))begin(u,x,y,OpenWidth,g::width);return true;}
    for(int i=0;i<5;++i)if(g::voice(i).has(x,y)){begin(u,x,y,ChooseVoice+i,g::voice(i));return true;}
    for(int p=0;p<C::P_COUNT;++p){const auto b=g::main_parameter(p);if(!b.has(x,y))continue;
        const bool voice_changed=g::is_ratio(p)&&selected!=p;select(p);if(!allowed(u,p))return true;
        begin(u,x,y,-1,b);touch.param=p;touch.origin=edit(u,p);touch.left=0;touch.right=g::gesture_span(p);touch.selected_voice=voice_changed;return true;}
    return true;
}
void pent_focus_drag(PentUnit& u,int x,int y){using namespace pent_private;if(!touch.owner.unit)return;
    if(blocked||!context()||param_lens_is_open()){cancel();return;}
    platform::AudioOut::Guard guard(*g_audio);if(g_drag!=Drag::PentParam||!matches(touch.owner,u)){cancel();return;}
    if(touch.action>=0){const int64_t dx=int64_t(x)-touch.x,dy=int64_t(y)-touch.y;if(!touch.box.has(x,y)||dx*dx+dy*dy>64)cancel_touch();return;}
    if(!allowed(u,touch.param)){cancel_touch();return;}if(x==touch.last_x)return;touch.last_x=x;
    q15 next=q15(std::clamp<int64_t>(int64_t(touch.origin)+(int64_t(x)-touch.x)*32767/(touch.right-touch.left),0,32767));
    if(touch.param==C::P_ROOT)next=C::root_value(C::root_note(next));
    write(u,touch.param,next);
}
void pent_focus_touch_release(){using namespace pent_private;bool back=false;
    if(touch.owner.unit&&context()&&!blocked&&!param_lens_is_open()){platform::AudioOut::Guard guard(*g_audio);auto* u=current();
        if(u&&g_drag==Drag::PentParam&&matches(touch.owner,*u)){const int a=touch.action;
            if(a==Back)back=true;
            else if(a==OpenRatios){select(C::P_RATIO_A+voice);open(*u,g::Menu::Ratio);}
            else if(a==OpenWidth){if(width_allowed(*u))open(*u,g::Menu::Width);}
            else if(a>=ChooseVoice&&a<ChooseVoice+5){voice=a-ChooseVoice;select(C::P_RATIO_A+voice);acknowledge(*u,uint8_t(1u<<voice));}
            else if(a>=0&&a<10)activate(*u,a);
            else if(a==-1&&touch.selected_voice&&edit(*u,touch.param)==touch.origin)acknowledge(*u,pent_reactive::parameter_mask(touch.param));
        }}
    cancel_touch();wait_lift=false;
    if(back){if(!pent_focus_back())g_overlay=Overlay::None;}
}
void pent_focus_menu_touch(u32 down,u32 held,int x,int y){using namespace pent_private;if(menu==g::Menu::None||wait_lift)return;
    if(held&KEY_TOUCH){C* u=nullptr;{platform::AudioOut::Guard guard(*g_audio);u=current();}if(u){if(down&KEY_TOUCH)pent_focus_touch_body(*u,x,y);else if(touch.owner.unit)pent_focus_drag(*u,x,y);}}
    else if(touch.owner.unit)pent_focus_touch_release();
}
void pent_focus_value_text(char* out,size_t n,int p,q15 v){pent_readout::value_text(out,n,p,v);}
void pent_focus_capture(){using namespace pent_private;frame={};if(!context()){if(touch.owner.unit||menu!=g::Menu::None)cancel();view_owner=motion_owner=feedback_owner={};ornament.reset();response.reset();feedback.cancel();return;}
    uint8_t held_feedback=0;const uint64_t now=osGetTime();
    {platform::AudioOut::Guard guard(*g_audio);auto* u=current();if(!u){if(touch.owner.unit||menu!=g::Menu::None)cancel();view_owner=motion_owner=feedback_owner={};ornament.reset();response.reset();feedback.cancel();return;}identify(*u);
        if((touch.owner.unit&&(g_drag!=Drag::PentParam||!matches(touch.owner,*u)))||(menu!=g::Menu::None&&!matches(menu_owner,*u)))cancel();
        auto& f=frame;f.valid=true;f.chain=g_edit_chain;f.slot=g_edit_slot;f.bank=g_eng->active_pattern;f.selected=selected;f.voice=voice;f.width=uint8_t(u->width);
        f.bypass=u->bypass;f.mute=g_eng->chains[g_edit_chain].mute;f.busy=g_eng->recording_active_or_pending();f.lens=param_lens_is_open();f.playing=g_eng->transport_playing;
        f.width_disabled=!width_allowed(*u);g_eng->capture_pent_visual(g_edit_chain,g_edit_slot,f.visual);motion_context(*u);feedback_context(*u);
        // motion_context resets response.frames at a physical owner boundary;
        // counters from two different PENTs must never erase the incoming input.
        if(f.visual.frames<response.frames)feedback.cancel();
        if(touch.owner.unit&&touch.action==-1&&matches(touch.owner,*u)&&allowed(*u,touch.param))
            held_feedback=pent_reactive::parameter_mask(touch.param);
        unsigned channels=uint8_t(u->width)==1?2:1;
        for(int i=g_edit_slot+1;i<UNITS_PER_CHAIN;++i){const auto* at=g_eng->chains[g_edit_chain].units[i];if(!at||at->bypass)continue;
            if(at->sequencer_properties().source)f.overwritten=true;
            if(channels==2&&!at->supports_stereo())f.unsupported=true;
            channels=at->output_channels(uint8_t(channels));}
        f.unsupported=f.unsupported||(f.visual.frames&&g_eng->chains[g_edit_chain].rendered_channels==0);
        const bool accepted=f.visual.ready&&f.visual.audible&&!f.bypass&&!f.mute&&!f.overwritten&&!f.unsupported;
        const auto movement=ornament.capture(f.visual.frames,now,accepted);
        f.fresh=movement.fresh;f.eligible=movement.live;f.art=movement.art;
        for(int p=0;p<C::P_COUNT;++p){f.edit[p]=edit(*u,p);f.live[p]=f.visual.ready?f.visual.effective[p]:u->params[p].effective;
            if(!allowed(*u,p))f.disabled|=uint16_t(1u<<p);
            if((u->params[p].src!=ModSource::None||u->params[p].relic_source)&&u->params[p].mod_enabled)f.linked|=uint16_t(1u<<p);}
        g_eng->capture_param_inspector(g_edit_chain,g_edit_slot,selected,f.route);f.menu=menu;
    }
    auto& f=frame;
    uint8_t acknowledgement[5]{};
    for(unsigned i=0;i<5;++i)acknowledgement[i]=std::max(feedback.capture(i,now),uint8_t(held_feedback&(1u<<i)?128:0));
    f.response=response.capture(f.visual,f.fresh,f.eligible,acknowledgement);
    for(int p=0;p<C::P_COUNT;++p)pent_readout::value_text(f.values[p],sizeof f.values[p],p,f.edit[p]);
    std::snprintf(f.context,sizeof f.context,"voice %c / %s",'a'+voice,f.route.temp?"temp":"edit");
    const auto tune=[&](int p){return !std::strcmp(f.values[p],"1/1")?"1":!std::strcmp(f.values[p],"2/1")?"2":f.values[p];};
    // The context occupies only the approved 98px to the left of the hero.
    char tuning[40];std::snprintf(tuning,sizeof tuning,"%.5s : %.5s : %.5s",tune(C::P_RATIO_A),tune(C::P_RATIO_B),tune(C::P_RATIO_C));
    std::snprintf(f.tuning[0],sizeof f.tuning[0],"%.21s",tuning);
    std::snprintf(f.tuning[1],sizeof f.tuning[1],"%.5s : %.5s",tune(C::P_RATIO_D),tune(C::P_RATIO_E));
    std::snprintf(f.status,sizeof f.status,"%s",f.bypass?"bypassed":f.mute?"muted":f.unsupported?"unsupported stereo chain":f.overwritten?"overwritten source":!f.visual.ready?"unprocessed":!f.visual.audible?"silent":!f.eligible?"unprocessed":f.visual.limited?"frequency limited":"five coupled voices");
    // Five heads consume the accepted source and genuine owned input. Immutable
    // art uploads once at the main fence; repeated captures never upload/synthesize.
    pent_visual::prepare();build_rows(f);
    if(menu!=g::Menu::None&&dirty){auto& cache=sigil_surface::bottom_cache;
        for(int y=0;y<sigil_surface::H;++y)for(int x=0;x<sigil_surface::W;++x){uint32_t c=0xff000000u;for(unsigned s:{0u,8u,16u}){unsigned sum=0;
            for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx)sum+=(m::sample(false,float(x*2+dx),float(y*2+dy))>>s)&255;
            c|=((sum+2)/4)<<s;}cache.pixels[y*sigil_surface::W+x]=c;}
        closed(sigil_surface::Painter{&cache},f);cache.finish();}
    f.motion=word_menu::capture(motion,osGetTime(),unsigned(menu),f.menu_focus,f.layout,[&](int i){return f.rows[std::clamp(i,0,std::max(0,int(f.count)-1))].label;},dirty);dirty=false;
}
void pent_focus_draw(const PentUnit&){pent_ui::draw_bottom();}
bool pent_focus_draw_top(){using namespace pent_private;const auto& f=frame;if(!f.valid)return false;
    m::paper(400);module_heading::header(UnitKind::Pent,m::spot());m::header_text(83,286,"five coupled voices",m::ghost());
    const sigil_surface::TopPainter pen;pen.right(m::right_edge(400),m::kHeaderTextY,f.width?"stereo":"mono",m::ghost());
    pen.text(14,50,"root",m::ghost());pen.text(14,66,f.values[C::P_ROOT],m::ink());
    pen.text(14,98,"tuning",m::ghost());m::caption(14,116,98,f.tuning[0],m::ink());m::caption(14,132,98,f.tuning[1],m::ink());
    pen.text(14,170,"five voices",m::ghost());pen.text(14,186,"one body",m::ink());
    pent_visual::draw(f.response);
    bool input_feedback=false;for(const auto& h:f.response.heads)input_feedback=input_feedback||h.feedback;
    m::footer(400,f.status,f.lens?"mod / temp":f.eligible?"source response":input_feedback?"input feedback":"quiescent");return true;
}
void pent_focus_gpu_frame(){pent_visual::upload();}
void pent_focus_shutdown(){pent_visual::shutdown();}
} // namespace seance::platform::ui3ds
