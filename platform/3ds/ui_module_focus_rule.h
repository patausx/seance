// Included once by ui_module_focus.cpp; RULE UI only, no audio clock synthesis.
#pragma once
#include "ui_module_focus_internal.h"
#include "rule_ui.h"
#include "pent_motion.h"
#include "ui_state.h"
#include "ui_param_lens.h"
#include "ui_sample_library.h"
#include "ui_sequencer.h"
#include "sigil_surface.h"
#include "module_fader_painter.h"
#include "audio_out.h"
#include <cstring>
namespace seance::platform::ui3ds {
namespace rule_private {
namespace g=rule_ui;namespace m=editor_style;using C=RuleUnit;using PB=trackr::synth::SampleBank;
g::Frame frame;
int selected=0,menu=0,focus=0;bool wait_lift=false,blocked=false,dirty=false;
word_menu::State word_motion;pent_motion::State lease;
C::Visual admitted_pose; // last real source-admitted pose; never a future score
uint16_t frozen_push=256,frozen_phase=0;
struct Owner {
    Engine* engine=nullptr;platform::AudioOut* audio=nullptr;uintptr_t unit=0;
    int chain=-1,slot=-1,sample=-1,bank=-1,selected=-1;
    uint32_t generation=0,frames=0,epoch=0;uint16_t overrides=0;
    uintptr_t pcm=0;size_t elements=0;uint8_t channels=0;
    View view=View::Chains;bool bypass=false,mute=false,busy=false;
    uint8_t phrase=1,root=60;uint64_t palette=0;
};
struct Touch {Owner owner{};g::Box box{};int action=-1,param=-1,x=0,y=0,last=0; q15 origin=0;} touch;
Owner menu_owner{},view_owner{},motion_owner{};
enum Action {Source=20,Setup,Scale,Back,Table=40,Cell=60};
bool context(){return g_eng&&g_audio&&g_overlay==Overlay::Editor&&
    g_view==View::Chains&&
    unsigned(g_edit_chain)<NUM_CHAINS&&unsigned(g_edit_slot)<UNITS_PER_CHAIN;}
C* current(){if(!context())return nullptr;auto* u=g_eng->chains[g_edit_chain].units[g_edit_slot];return u&&u->kind==UnitKind::Rule?static_cast<C*>(u):nullptr;}
uint16_t overrides(const C& u){uint16_t mask=0;for(int p=0;p<12;++p){const auto& d=u.params[p];if(d.temp_override||d.performance_override||d.audition_override||d.step_lock_override)mask|=uint16_t(1u<<p);}return mask;}
Owner owner(const C& u){Owner o;o.engine=g_eng;o.audio=g_audio;o.unit=uintptr_t(&u);o.chain=g_edit_chain;o.slot=g_edit_slot;o.sample=u.sample_slot;
    if(unsigned(o.sample)<trackr::synth::SAMPLE_BANK_SIZE){o.generation=PB::instance().generation(o.sample);const auto& s=PB::instance().slot(o.sample);o.frames=s.playable_frames();o.pcm=uintptr_t(s.data.data());o.elements=s.data.size();o.channels=s.channels;}
    o.bank=g_eng->active_pattern;o.selected=selected;o.view=g_view;
    o.bypass=u.bypass;o.mute=g_eng->chains[g_edit_chain].mute;o.busy=g_eng->recording_active_or_pending();o.epoch=g_audio->lifecycle_epoch();
    o.overrides=overrides(u);o.phrase=u.config().phrase;o.root=u.config().source_root;o.palette=palette::revision();return o;}
bool equal(const Owner& a,const Owner& b){return a.engine==b.engine&&a.audio==b.audio&&a.unit==b.unit&&a.chain==b.chain&&a.slot==b.slot&&
    a.sample==b.sample&&a.generation==b.generation&&a.frames==b.frames&&a.pcm==b.pcm&&a.elements==b.elements&&a.channels==b.channels&&a.bank==b.bank&&a.selected==b.selected&&
    a.view==b.view&&a.bypass==b.bypass&&a.mute==b.mute&&a.busy==b.busy&&a.epoch==b.epoch&&a.overrides==b.overrides&&a.phrase==b.phrase&&a.root==b.root&&a.palette==b.palette;}
bool matches(const Owner& o,const C& u){return context()&&!param_lens_is_open()&&equal(o,owner(u));}
// Never dereference a retired address. Only an installed same Engine/slot may lose transient pending edits.
void cancel_pending(const Owner& o){if(g_eng&&o.engine==g_eng&&unsigned(o.chain)<NUM_CHAINS&&unsigned(o.slot)<UNITS_PER_CHAIN){auto* u=g_eng->chains[o.chain].units[o.slot];
    if(u&&uintptr_t(u)==o.unit&&u->kind==UnitKind::Rule)static_cast<C*>(u)->cancel_live_edits();}}
void clear_touch(){if(touch.owner.unit)wait_lift=true;touch={};if(g_drag==Drag::RuleParam)g_drag=Drag::None;}
void cancel(){cancel_pending(touch.owner.unit?touch.owner:view_owner);clear_touch();if(menu)wait_lift=true;menu=0;menu_owner={};word_motion={};dirty=false;}
bool allowed(const C& u,int p){return unsigned(p)<12&&!g_eng->recording_active_or_pending()&&
    (p!=C::P_RULE||!u.config().phrase)&&!(overrides(u)&(1u<<p));}
bool config_allowed(const C& u){return !g_eng->recording_active_or_pending()&&!overrides(u);}
q15 edit(const C& u,int p){return u.params[p].base;}
bool write(C& u,int p,q15 v){if(!allowed(u,p)||v==edit(u,p))return false;
    if(p==C::P_RULE){if(!u.edit_rule(C::rule_number(v)))return false;}else u.params[p].base=v;
    mark_project_mutated();return true;}
void begin(C& u,int x,int y,int a,g::Box b){touch={};touch.owner=owner(u);touch.x=touch.last=x;touch.y=y;touch.action=a;touch.box=b;g_drag=Drag::RuleParam;}
void select(int p){if(unsigned(p)>=12)return;if(p!=selected){clear_touch();selected=p;}if(menu)menu_owner.selected=selected;}
void open(C& u,int value){clear_touch();menu=value;focus=0;menu_owner=owner(u);dirty=true;}
void rows(g::Frame& f){f.count=0;const auto row=[&](const char* s){std::snprintf(f.rows[f.count++],20,"%.19s",s);};
    if(menu==1){for(const char* s:g::scales)row(s);std::snprintf(f.anchor,16,"scale >");f.layout={14,34,56,22,14,306};}
    else if(menu==2){row("phrase 90/30/110");row("manual");row("source root >");std::snprintf(f.anchor,16,"setup");f.layout={174,194,50,24,174,306};}
    else if(menu==3){char s[24];std::snprintf(s,24,"root %u",unsigned(f.root));row(s);row("root -");row("root +");row("sample metadata");std::snprintf(f.anchor,16,"setup");f.layout={154,174,50,24,154,306};}
    f.focus=uint8_t(std::clamp(focus,0,std::max(0,int(f.count)-1)));}
void activate(C& u,int row){if(row<0||row>=frame.count)return;
    if(menu==1){select(C::P_SCALE);if(row<5){write(u,C::P_SCALE,q15((row*32768+16384)/5));}
        menu_owner=owner(u);}
    else if(config_allowed(u)){auto c=u.config();if(menu==2){if(row<2){const uint8_t next=row==0?1:0;if(c.phrase!=next){c.phrase=next;u.set_config(c);mark_project_mutated();}menu=0;}
        else{menu=3;focus=0;dirty=true;}}
        else if(menu==3){int root=c.source_root;if(row==1)root=std::max(0,root-1);if(row==2)root=std::min(120,root+1);
            if(row==3&&unsigned(u.sample_slot)<trackr::synth::SAMPLE_BANK_SIZE){const int known=PB::instance().slot(u.sample_slot).root_note;if(known>=0&&known<=120)root=known;}
            if(root!=c.source_root){c.source_root=uint8_t(root);u.set_config(c);mark_project_mutated();}}
        menu_owner=owner(u);}}
void title(const sigil_surface::Painter& pen){const auto* l=module_heading::label(UnitKind::Rule);if(!l)return;const auto* bits=module_heading_data::bitmap+l->offset;const int stride=(l->width+7)/8;
    for(int y=0;y<l->height;++y){int start=-1;for(int x=0;x<=l->width;++x){const bool on=x<l->width&&(bits[y*stride+x/8]&(128>>(x&7)));if(on&&start<0)start=x;
        if(!on&&start>=0){pen.rect(m::kLeft+start,module_frame_layout::kTopInset+y,x-start,1,m::spot());start=-1;}}}}
void closed(const sigil_surface::Painter& p,const g::Frame& f){title(p);rule_visual::bottom_content(p,f.art);}
void tree(const g::Frame& f){const sigil_surface::Painter p;p.text(f.menu==1?14:262,f.menu==1?m::kTextY:m::kHeaderTextY,f.anchor,m::spot());
    word_menu::draw(p,f.layout,f.motion,f.count,f.focus,[&](int i){return f.rows[i];},[](int,int,unsigned,bool){},m::ink(),m::spot(),m::branch());
    m::footer(320,nullptr,"b / outside closes");}
} // namespace rule_private
namespace rule_ui {const Frame& frozen(){return rule_private::frame;}
bool draw_bottom(){using namespace rule_private;const auto& f=frame;if(!f.valid)return false;
    if(!f.menu){m::paper(320);module_heading::header(UnitKind::Rule,m::spot());rule_visual::bottom_content(sigil_surface::Painter{},f.art);return true;}
    if(!sigil_surface::bottom_cache.draw()){m::paper(320);closed(sigil_surface::Painter{},f);m::paper_rect(0,0,320,240,185);}tree(f);return true;}}
int rule_focus_selected(){return rule_private::selected;}
void rule_focus_select(int p){rule_private::select(p);}
void rule_focus_release(){using namespace rule_private;if(g_audio){platform::AudioOut::Guard guard(*g_audio);cancel();}else{clear_touch();menu=0;}}
bool rule_focus_back(){using namespace rule_private;const bool had=menu!=0;rule_focus_release();return had;}
bool rule_focus_menu_modal(){return rule_private::menu!=0;}
bool rule_focus_menu_input(u32& down,u32 held,u32 up,bool is_blocked){using namespace rule_private;const bool had=menu!=0,tail=wait_lift;
    blocked=is_blocked||bool((down|held)&~u32(KEY_TOUCH|KEY_ZL|KEY_R));
    const auto revoke=[&](){cancel();if(had)down&=~u32(KEY_A|KEY_X);};
    // No owner and no RULE editor: the guarded branch below has nothing to do, skip its entry.
    if(g_audio&&(touch.owner.unit||had||current())){platform::AudioOut::Guard guard(*g_audio);auto* u=current();
        if(!u||is_blocked||param_lens_is_open()){if(touch.owner.unit||had)revoke();}
        else if((touch.owner.unit&&(g_drag!=Drag::RuleParam||!matches(touch.owner,*u)))||(had&&!matches(menu_owner,*u)))revoke();
        else if(down&~u32(KEY_TOUCH|KEY_ZL|KEY_R)){
            if(touch.owner.unit){cancel();if(held&KEY_TOUCH)wait_lift=true;}
            if(down&(KEY_L|KEY_SELECT|KEY_START|KEY_Y)){if(had)cancel();}
            else if(had){if(down&(KEY_B|KEY_X))cancel();else if(down&KEY_UP)focus=(focus+std::max(1,int(frame.count))-1)%std::max(1,int(frame.count));
                else if(down&KEY_DOWN){focus=(focus+1)%std::max(1,int(frame.count));}
                else if(down&KEY_A){activate(*u,focus);}
                down&=KEY_TOUCH|KEY_ZL;}
            else if(!(held&(KEY_L|KEY_R))&&(down&(KEY_UP|KEY_DOWN))){select((selected+12+(down&KEY_UP?-1:1))%12);down&=~u32(KEY_UP|KEY_DOWN);}
        }}
    if(wait_lift&&((up&KEY_TOUCH)||!(held&KEY_TOUCH))){wait_lift=false;}
    return had||tail||wait_lift;}
bool rule_focus_touch_body(RuleUnit& u,int x,int y){using namespace rule_private;if(wait_lift||blocked||!context()||param_lens_is_open())return true;
    platform::AudioOut::Guard guard(*g_audio);if(current()!=&u||touch.owner.unit)return true;
    if(menu){if(!matches(menu_owner,u)){cancel();return true;}const auto& l=frame.layout;const int row=(y-l.y+4)/std::max(1,l.step);
        if(x<l.left||x>=l.right||y<l.y-4||row<0||row>=frame.count){cancel();return true;}focus=row;begin(u,x,y,row,{l.left,l.y-4+row*l.step,l.right-l.left,l.step});return true;}
    if(g::back.has(x,y)){begin(u,x,y,Back,g::back);return true;}
    if(g::source.has(x,y)){if(config_allowed(u))begin(u,x,y,Source,g::source);return true;}
    if(g::setup.has(x,y)){if(config_allowed(u))begin(u,x,y,Setup,g::setup);return true;}
    if(g::scale.has(x,y)){select(C::P_SCALE);if(allowed(u,C::P_SCALE))begin(u,x,y,Scale,g::scale);return true;}
    for(int i=0;i<8;++i)if(g::table(i).has(x,y)){select(C::P_RULE);if(config_allowed(u))begin(u,x,y,Table+i,g::table(i));return true;}
    for(int i=0;i<15;++i)if(g::cell(i).has(x,y)){if(config_allowed(u))begin(u,x,y,Cell+i,g::cell(i));return true;}
    for(int p=0;p<10;++p){const auto b=g::parameter(p);if(!b.has(x,y))continue;select(p);if(!allowed(u,p))return true;
        begin(u,x,y,-1,b);touch.param=p;touch.origin=edit(u,p);return true;}return true;}
void rule_focus_drag(RuleUnit& u,int x,int y){using namespace rule_private;if(!touch.owner.unit)return;
    if(blocked||!context()||param_lens_is_open()){rule_focus_release();return;}platform::AudioOut::Guard guard(*g_audio);
    if(g_drag!=Drag::RuleParam||!matches(touch.owner,u)){cancel();return;}
    if(touch.action>=0){const int64_t dx=int64_t(x)-touch.x,dy=int64_t(y)-touch.y;if(!touch.box.has(x,y)||dx*dx+dy*dy>64)cancel();return;}
    if(!allowed(u,touch.param)){cancel();return;}if(x==touch.last)return;touch.last=x;const bool bi=u.params[touch.param].bipolar;
    const q15 value=q15(std::clamp<int64_t>(int64_t(touch.origin)+(int64_t(x)-touch.x)*(bi?65534:32767)/128,bi?-32767:0,32767));write(u,touch.param,value);}
void rule_focus_touch_release(){using namespace rule_private;bool browser=false,back=false;
    if(touch.owner.unit&&context()&&!blocked&&!param_lens_is_open()){platform::AudioOut::Guard guard(*g_audio);auto* u=current();
        if(u&&g_drag==Drag::RuleParam&&matches(touch.owner,*u)){const int a=touch.action;
            if(a==Source&&config_allowed(*u))browser=true;else if(a==Back)back=true;else if(a==Setup&&config_allowed(*u))open(*u,2);
            else if(a==Scale&&allowed(*u,C::P_SCALE))open(*u,1);else if(a>=0&&a<8)activate(*u,a);
            else if(a>=Table&&a<Table+8){const int bit=7-(a-Table);C::Visual actual{};g_eng->capture_rule_visual(g_edit_chain,g_edit_slot,actual);
                if(config_allowed(*u)){const uint8_t from=u->config().phrase&&actual.serial?actual.rule:C::rule_number(u->params[C::P_RULE].base);
                    if(u->edit_rule(uint16_t(from^(1u<<bit))))mark_project_mutated();}}
            else if(a>=Cell&&a<Cell+15&&config_allowed(*u)){C::Visual actual{};g_eng->capture_rule_visual(g_edit_chain,g_edit_slot,actual);uint16_t row=0;
                if(!u->pending_row(row))row=actual.serial?actual.rows[0]:u->config().seed_row;
                // Leaving the automatic phrase by seeding must retain its
                // CURRENT rule (30/110 as well as 90), not the dormant base90.
                bool changed=false;
                if(u->config().phrase&&actual.serial)changed=u->edit_rule(actual.rule);
                if(u->edit_row(uint16_t(row^(1u<<(a-Cell)))))changed=true;
                if(changed)mark_project_mutated();}
        }else cancel();}
    // Successful physical lift clears ONLY contact authority. It must retain the
    // newly committed next-generation row; cancellation is a separate path.
    clear_touch();wait_lift=false;
    if(browser){scan_sample_files();g_overlay=Overlay::SampleBrowser;}
    if(back){rule_focus_release();g_overlay=Overlay::None;}}
void rule_focus_menu_touch(u32 down,u32 held,int x,int y){using namespace rule_private;if(!menu||wait_lift)return;
    if(held&KEY_TOUCH){C* u=nullptr;{platform::AudioOut::Guard guard(*g_audio);u=current();}if(u){if(down&KEY_TOUCH)rule_focus_touch_body(*u,x,y);else if(touch.owner.unit)rule_focus_drag(*u,x,y);}}
    else if(touch.owner.unit)rule_focus_touch_release();}
void rule_focus_value_text(char* out,size_t n,int p,q15 v){rule_ui::value_text(out,n,p,v);}
void rule_focus_capture(){using namespace rule_private;frame={};if(!context()){rule_focus_release();view_owner=motion_owner={};lease.reset();return;}
    const uint64_t now=osGetTime();
    {platform::AudioOut::Guard guard(*g_audio);auto* u=current();if(!u){cancel();view_owner=motion_owner={};lease.reset();return;}
        const auto o=owner(*u);
        const bool lost=view_owner.unit&&(view_owner.engine!=o.engine||view_owner.audio!=o.audio||view_owner.unit!=o.unit||view_owner.chain!=o.chain||view_owner.slot!=o.slot||view_owner.sample!=o.sample||view_owner.generation!=o.generation||view_owner.frames!=o.frames||view_owner.pcm!=o.pcm||view_owner.elements!=o.elements||view_owner.channels!=o.channels||view_owner.bank!=o.bank||view_owner.view!=o.view||view_owner.epoch!=o.epoch||view_owner.bypass!=o.bypass||view_owner.mute!=o.mute||view_owner.busy!=o.busy||view_owner.overrides!=o.overrides);
        if(lost)cancel();
        view_owner=o;if((touch.owner.unit&&(g_drag!=Drag::RuleParam||!matches(touch.owner,*u)))||(menu&&!matches(menu_owner,*u)))cancel();
        auto& f=frame;f.valid=true;f.chain=g_edit_chain;f.slot=g_edit_slot;f.sample=o.sample;f.generation=o.generation;f.frames=o.frames;f.bank=o.bank;
        f.lens=param_lens_is_open();f.bypass=o.bypass;f.mute=o.mute;f.busy=o.busy;f.root=o.root;f.phrase=o.phrase;f.beat=g_eng->frames_per_beat();
        g_eng->capture_rule_visual(g_edit_chain,g_edit_slot,f.visual);f.loaded=f.visual.loaded&&f.frames>1;
        if(unsigned(f.sample)<trackr::synth::SAMPLE_BANK_SIZE){const char* path=PB::instance().provenance(f.sample).path;const char* name=std::strrchr(path,'/');name=name?name+1:path;
            const size_t n=strnlen(name,sizeof f.source_leaf-1);std::memcpy(f.source_leaf,name,n);f.source_leaf[n]=0;}
        for(int p=0;p<12;++p){f.edit[p]=edit(*u,p);f.live[p]=f.visual.serial?f.visual.effective[p]:u->params[p].effective;if(!allowed(*u,p))f.disabled|=uint16_t(1u<<p);}
        f.linked=focus_mod_links(*u);
        for(int s=g_edit_slot+1;s<UNITS_PER_CHAIN;++s){const auto* at=g_eng->chains[g_edit_chain].units[s];if(at&&!at->bypass&&at->sequencer_properties().source)f.overwritten=true;}
        f.eligible=f.loaded&&!f.bypass&&!f.mute&&!f.busy&&!f.overwritten&&!f.visual.paused&&!f.visual.fault&&
            f.visual.sample_slot==f.sample&&f.visual.source_generation==f.generation&&f.visual.source_frames==f.frames;
        if(!equal(motion_owner,o)){lease.reset();admitted_pose=f.visual;frozen_push=f.visual.serial?f.visual.push_q8:256;frozen_phase=f.visual.serial?f.visual.phase_q8:0;motion_owner=o;}
        const auto movement=lease.capture((uint64_t(f.visual.serial)<<32)|f.visual.phase_frames,now,f.eligible);
        if(movement.fresh&&movement.live&&f.visual.serial){admitted_pose=f.visual;frozen_push=f.visual.push_q8;frozen_phase=f.visual.phase_q8;}
        if(!f.visual.serial){admitted_pose=f.visual;frozen_push=256;frozen_phase=0;}
        const auto& pose=admitted_pose;
        auto& a=f.art;a.valid=true;a.rule=pose.rule;a.gen=int(pose.generation);a.rings=pose.rings;a.period_ms=int(pose.serial?pose.period_ms:C::period_frames(f.live[C::P_RATE],f.beat)*1000u/32000u);
        std::copy(std::begin(pose.rows),std::end(pose.rows),a.rows);std::copy(std::begin(pose.born),std::end(pose.born),a.born);
        if(f.eligible){std::copy(std::begin(pose.spoke),std::end(pose.spoke),a.spoke);a.sounding=__builtin_popcount(unsigned(pose.sounding));}
        a.push_q8=frozen_push;a.phase_q8=frozen_phase;a.selected=selected;a.lens=f.lens;
        a.edit_rule=f.phrase&&f.visual.serial?f.visual.rule:C::rule_number(f.edit[C::P_RULE]);
        a.pending=u->pending_row(a.edit_row);a.seed=!f.visual.serial||!g_eng->transport_playing;
        if(!a.pending)a.edit_row=a.seed?u->config().seed_row:f.visual.rows[0];
        f.menu=uint8_t(menu);
    }
    auto& f=frame;auto& a=f.art;std::snprintf(a.source,sizeof a.source,"%.26s",f.source_leaf[0]?f.source_leaf:f.loaded?"source >":"no source >");
    std::snprintf(a.scale,sizeof a.scale,"%s / src %u",g::scales[std::min(4,int(f.visual.scale))],unsigned(f.root));
    if(f.bypass||f.mute||f.busy||!f.loaded||f.overwritten||f.visual.paused||f.visual.fault||!f.visual.serial)std::snprintf(a.status,sizeof a.status,"%s",f.bypass?"bypassed":f.mute?"muted":f.busy?"recording":!f.loaded?"empty source":f.overwritten?"overwritten source":f.visual.fault?"score fault":f.visual.paused?"paused":"unprocessed");
    for(int p=0;p<10;++p){auto& k=a.knobs[p];g::value_text(k.value,16,p,f.edit[p]);k.edit_q8=g::q8(f.edit[p],p==C::P_ROOT);k.live_q8=g::q8(f.live[p],p==C::P_ROOT);k.linked=bool(f.linked&(1u<<p));k.disabled=bool(f.disabled&(1u<<p));k.quiet=f.bypass||f.mute;}
    rows(f);if(menu&&dirty){auto& cache=sigil_surface::bottom_cache;
        // Same underlying editor sheet as the native closed surface, averaged
        // into the existing shared sigil cache; no new panel/material or timer.
        for(int y=0;y<sigil_surface::H;++y)for(int x=0;x<sigil_surface::W;++x){uint32_t c=0xff000000u;for(unsigned shift:{0u,8u,16u}){unsigned sum=0;
            for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx)sum+=(m::sample(false,float(x*2+dx),float(y*2+dy))>>shift)&255;
            c|=((sum+2)/4)<<shift;}cache.pixels[y*sigil_surface::W+x]=c;}
        closed(sigil_surface::Painter{&cache},f);cache.finish();}
    f.motion=word_menu::capture(word_motion,now,unsigned(menu),f.focus,f.layout,[&](int i){return f.rows[std::clamp(i,0,std::max(0,int(f.count)-1))];},dirty);dirty=false;}
void rule_focus_draw(const RuleUnit&){rule_ui::draw_bottom();}
bool rule_focus_draw_top(){const auto& f=rule_private::frame;if(!f.valid)return false;rule_visual::rule_draw_top(f.art);return true;}
} // namespace seance::platform::ui3ds
