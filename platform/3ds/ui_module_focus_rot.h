// Included once by ui_module_focus.cpp. UI owns copies/resources, never DSP clocks.
#pragma once
#include "ui_module_focus_internal.h"
#include "rot_ui.h"
#include "rot_motion.h"
#include "ui_state.h"
#include "ui_param_lens.h"
#include "ui_sample_library.h"
#include "ui_sequencer.h"
#include "sigil_surface.h"
#include "audio_out.h"
#include <cstring>
namespace seance::platform::ui3ds {
namespace rot_private {
namespace g=rot_ui;namespace m=editor_style;namespace v=rot_visual;using C=RotUnit;using PB=trackr::synth::SampleBank;
g::Frame frame;
// Off-stack lifetime: software Resource alone owns 2MiB. Never copy it in a frame.
v::Resource resource;C::Artwork captured,admitted;v::Artwork film;C::Visual pose;
rot_motion::State lease;word_menu::State word_motion;
uint32_t spark_age[372]={};uint64_t source_serial=0,artwork_serial=0,spark_serial=0;
int selected=0,menu=0,focus=0;bool wait_lift=false,blocked=false,dirty=false,fine=false,retire=false;
struct Owner {
    Engine* engine=nullptr;platform::AudioOut* audio=nullptr;uintptr_t unit=0,pcm=0;size_t elements=0;
    int chain=-1,slot=-1,sample=-1,bank=-1,selection=-1;
    uint32_t generation=0,frames=0,epoch=0,seed=11;uint16_t overrides=0,passes=14;uint8_t channels=0,mode=0;
    View view=View::Chains;bool bypass=false,mute=false,busy=false,overwritten=false;uint64_t palette=0;
};
struct Touch {Owner owner{};g::Box box{};int action=-1,param=-1,x=0,y=0,last=0,divisor=1;q15 origin=0;} touch;
Owner menu_owner{},view_owner{},motion_owner{};
enum Action {Source=20,Setup,Back,Mode=30};
bool context(){return g_eng&&g_audio&&g_overlay==Overlay::Editor&&
    g_view==View::Chains&&unsigned(g_edit_chain)<NUM_CHAINS&&unsigned(g_edit_slot)<UNITS_PER_CHAIN;}
C* current(){if(!context())return nullptr;auto* u=g_eng->chains[g_edit_chain].units[g_edit_slot];return u&&u->kind==UnitKind::Rot?static_cast<C*>(u):nullptr;}
uint16_t overrides(const C& u){uint16_t mask=0;for(int p=0;p<12;++p){const auto& d=u.params[p];if(d.temp_override||d.performance_override||d.audition_override||d.step_lock_override)mask|=uint16_t(1u<<p);}return mask;}
Owner owner(const C& u){Owner o;o.engine=g_eng;o.audio=g_audio;o.unit=uintptr_t(&u);o.chain=g_edit_chain;o.slot=g_edit_slot;o.sample=u.sample_slot;
    if(unsigned(o.sample)<trackr::synth::SAMPLE_BANK_SIZE){o.generation=PB::instance().generation(o.sample);const auto& s=PB::instance().slot(o.sample);o.frames=s.playable_frames();o.pcm=uintptr_t(s.data.data());o.elements=s.data.size();o.channels=s.channels;}
    o.bank=g_eng->active_pattern;o.selection=selected;o.view=g_view;o.bypass=u.bypass;o.mute=g_eng->chains[g_edit_chain].mute;
    o.busy=g_eng->recording_active_or_pending();o.epoch=g_audio->lifecycle_epoch();o.overrides=overrides(u);const auto c=u.config();o.seed=c.seed;o.passes=c.passes;o.mode=uint8_t(c.mode);o.palette=palette::revision();
    for(int s=g_edit_slot+1;s<UNITS_PER_CHAIN;++s){const auto* at=g_eng->chains[g_edit_chain].units[s];if(at&&!at->bypass&&at->sequencer_properties().source)o.overwritten=true;}return o;}
bool physical(const Owner& a,const Owner& b){return a.engine==b.engine&&a.audio==b.audio&&a.unit==b.unit&&a.chain==b.chain&&a.slot==b.slot&&a.sample==b.sample&&a.generation==b.generation&&a.frames==b.frames&&a.pcm==b.pcm&&a.elements==b.elements&&a.channels==b.channels&&a.seed==b.seed&&a.passes==b.passes;}
bool equal(const Owner& a,const Owner& b){return physical(a,b)&&a.bank==b.bank&&a.selection==b.selection&&a.view==b.view&&a.bypass==b.bypass&&a.mute==b.mute&&a.busy==b.busy&&a.epoch==b.epoch&&a.overrides==b.overrides&&a.mode==b.mode&&a.palette==b.palette&&a.overwritten==b.overwritten;}
bool matches(const Owner& o,const C& u){return context()&&!param_lens_is_open()&&equal(o,owner(u));}
void clear_touch(){if(touch.owner.unit)wait_lift=true;touch={};if(g_drag==Drag::RotParam)g_drag=Drag::None;}
void cancel(){clear_touch();if(menu)wait_lift=true;menu=0;menu_owner={};word_motion={};dirty=false;}
bool allowed(const C& u,int p){return unsigned(p)<12&&!g_eng->recording_active_or_pending()&&!(overrides(u)&(1u<<p));}
bool config_allowed(const C& u){return !g_eng->recording_active_or_pending()&&!overrides(u);}
q15 edit(const C& u,int p){return u.params[p].base;}
bool write(C& u,int p,q15 value){if(!allowed(u,p)||value==edit(u,p))return false;
    u.params[p].base=value;mark_project_mutated();return true;}
void begin(C& u,int x,int y,int action,g::Box box){touch={};touch.owner=owner(u);touch.x=touch.last=x;touch.y=y;touch.action=action;touch.box=box;touch.divisor=fine?8:1;g_drag=Drag::RotParam;}
void select(int p){if(unsigned(p)>=12)return;if(p!=selected){clear_touch();selected=p;}if(menu)menu_owner.selection=selected;}
void open(C& u,int which){clear_touch();menu=which;focus=0;menu_owner=owner(u);dirty=true;}
void rows(g::Frame& f){f.count=0;const auto row=[&](const char* s){std::snprintf(f.rows[f.count++],20,"%.19s",s);};char text[24];
    if(menu==1){row("passes >");row("seed >");std::snprintf(f.anchor,16,"setup");}
    else if(menu==2){std::snprintf(text,24,"passes %u",unsigned(f.passes));row(text);row("passes -");row("passes +");std::snprintf(f.anchor,16,"setup");}
    else if(menu==3){std::snprintf(text,24,"seed %u",unsigned(f.seed));row(text);row("seed -");row("seed +");std::snprintf(f.anchor,16,"setup");}
    f.layout=word_menu::Layout{174,194,50,24,174,306};f.focus=uint8_t(std::clamp(focus,0,std::max(0,int(f.count)-1)));}
void activate(C& u,int row){if(row<0||row>=frame.count)return;
    if(!config_allowed(u)){return;}if(menu==1){menu=row==0?2:3;focus=0;dirty=true;return;}
    auto c=u.config();const auto old=c;if(menu==2){if(row==1)c.passes=std::max(4,int(c.passes)-1);if(row==2)c.passes=std::min(100,int(c.passes)+1);}
    else if(menu==3){if(row==1&&c.seed)c.seed--;if(row==2&&c.seed<UINT32_MAX)c.seed++;}
    if((old.seed!=c.seed||old.passes!=c.passes)&&u.set_config(c)){mark_project_mutated();retire=true;std::memset(static_cast<void*>(&admitted),0,sizeof admitted);pose={};std::memset(static_cast<void*>(&film),0,sizeof film);lease.reset();++source_serial;}
    menu_owner=owner(u);dirty=true;}
void title(const sigil_surface::Painter& pen){const auto* l=module_heading::label(UnitKind::Rot);if(!l)return;const auto* bits=module_heading_data::bitmap+l->offset;const int stride=(l->width+7)/8;
    for(int y=0;y<l->height;++y){int start=-1;for(int x=0;x<=l->width;++x){const bool on=x<l->width&&(bits[y*stride+x/8]&(128>>(x&7)));if(on&&start<0)start=x;if(!on&&start>=0){pen.rect(m::kLeft+start,module_frame_layout::kTopInset+y,x-start,1,m::spot());start=-1;}}}}
void closed(const sigil_surface::Painter& p,const g::Frame& f){title(p);v::bottom_content(p,f.art,film,resource.ready);}
void tree(const g::Frame& f){const sigil_surface::Painter p;p.text(305-ui::text_w(f.anchor),m::kHeaderTextY,f.anchor,m::spot());
    word_menu::draw(p,f.layout,f.motion,f.count,f.focus,[&](int i){return f.rows[i];},[](int,int,unsigned,bool){},m::ink(),m::spot(),m::branch());m::footer(320,nullptr,"b / outside closes");}
void copy_film(){std::memcpy(film.env,admitted.env,sizeof film.env);std::memcpy(film.mask,admitted.mask,sizeof film.mask);std::memcpy(film.scar,admitted.scar,sizeof film.scar);std::memcpy(film.source_env,admitted.source_env,sizeof film.source_env);}
// Spark bins are real tape hits. Only an actual main-reader crossing arms them;
// age is accumulated from captured SOURCE travel, never from UI wall time.
void sparks(const C::Visual& before,const C::Visual& next){
    const bool same=before.serial==next.serial&&before.pass==next.pass&&before.loop_start==next.loop_start&&before.loop_span==next.loop_span;
    const uint32_t span=next.loop_span;if(!span)return;uint32_t travel=0;
    const bool edge=before.loaded&&next.pass==before.pass+1&&next.serial==before.serial+1&&before.loop_start==next.loop_start&&before.loop_span==span;
    if(!same)std::fill(std::begin(spark_age),std::end(spark_age),0u);
    if(same)travel=next.phase_frames>=before.phase_frames?next.phase_frames-before.phase_frames:span-before.phase_frames+next.phase_frames;
    else if(before.loaded&&next.serial==before.serial+1&&before.loop_span==span)travel=span-std::min(span,before.phase_frames)+next.phase_frames;
    bool changed=false;for(unsigned c=0;c<372;++c){if(spark_age[c])spark_age[c]=travel>=spark_age[c]?0:spark_age[c]-travel;
        const uint32_t bin=uint32_t(uint64_t(c)*span/372);
        const bool crossed=edge?bin<next.phase_frames:same&&travel&&(before.phase_frames<=next.phase_frames?(bin>=before.phase_frames&&bin<next.phase_frames):(bin>=before.phase_frames||bin<next.phase_frames));
        if(crossed&&admitted.spark[c])spark_age[c]=8000;
        const uint8_t level=uint8_t(uint32_t(admitted.spark[c])*spark_age[c]/8000);if(film.spark[c]!=level){film.spark[c]=level;changed=true;}}
    if(changed)++spark_serial;
}
} // rot_private
namespace rot_ui {const Frame& frozen(){return rot_private::frame;}const rot_visual::Artwork& artwork(){return rot_private::film;}
bool draw_bottom(){using namespace rot_private;const auto& f=frame;if(!f.valid)return false;
    if(!f.menu){v::rot_draw_bottom(f.art,film,resource);
        if(f.pending){const sigil_surface::Painter p;for(int x:{f.edit_start,f.edit_end-1})p.line(14+std::clamp(x,0,291),60,14+std::clamp(x,0,291),92,m::ghost());}return true;}
    if(!sigil_surface::bottom_cache.draw()){m::paper(320);closed(sigil_surface::Painter{},f);m::paper_rect(0,0,320,240,185);}tree(f);return true;}}
int rot_focus_selected(){return rot_private::selected;}void rot_focus_select(int p){rot_private::select(p);}
void rot_focus_release(){rot_private::cancel();}
bool rot_focus_back(){const bool had=rot_private::menu!=0;rot_focus_release();return had;}
bool rot_focus_menu_modal(){return rot_private::menu!=0;}
bool rot_focus_menu_input(u32& down,u32 held,u32 up,bool is_blocked){using namespace rot_private;const bool had=menu!=0,tail=wait_lift;fine=bool(held&KEY_ZL);
    blocked=is_blocked||bool((down|held)&~u32(KEY_TOUCH|KEY_ZL|KEY_R));const auto revoke=[&](){cancel();if(had)down&=~u32(KEY_A|KEY_X);};
    // No owner and no ROT editor: the guarded branch below has nothing to do, skip its entry.
    if(g_audio&&(touch.owner.unit||had||current())){platform::AudioOut::Guard guard(*g_audio);auto* u=current();
        if(!u||is_blocked||param_lens_is_open()){if(touch.owner.unit||had)revoke();}
        else if((touch.owner.unit&&(g_drag!=Drag::RotParam||!matches(touch.owner,*u)))||(had&&!matches(menu_owner,*u)))revoke();
        else if(down&~u32(KEY_TOUCH|KEY_ZL|KEY_R)){
            if(touch.owner.unit){cancel();if(held&KEY_TOUCH)wait_lift=true;}
            if(down&(KEY_L|KEY_SELECT|KEY_START|KEY_Y)){if(had)cancel();}
            else if(had){if(down&(KEY_B|KEY_X))cancel();else if(down&KEY_UP)focus=(focus+std::max(1,int(frame.count))-1)%std::max(1,int(frame.count));else if(down&KEY_DOWN)focus=(focus+1)%std::max(1,int(frame.count));else if(down&KEY_A)activate(*u,focus);down&=KEY_TOUCH|KEY_ZL;}
            else if(!(held&(KEY_L|KEY_R))&&(down&(KEY_UP|KEY_DOWN))){select((selected+12+(down&KEY_UP?-1:1))%12);down&=~u32(KEY_UP|KEY_DOWN);}
        }}
    if(wait_lift&&((up&KEY_TOUCH)||!(held&KEY_TOUCH))){wait_lift=false;}return had||tail||wait_lift;}
bool rot_focus_touch_body(RotUnit& u,int x,int y){using namespace rot_private;if(wait_lift||blocked||!context()||param_lens_is_open())return true;
    platform::AudioOut::Guard guard(*g_audio);if(current()!=&u||touch.owner.unit)return true;
    if(menu){if(!matches(menu_owner,u)){cancel();return true;}const auto& l=frame.layout;const int row=(y-l.y+4)/std::max(1,l.step);
        if(x<l.left||x>=l.right||y<l.y-4||row<0||row>=frame.count){cancel();return true;}focus=row;begin(u,x,y,row,{l.left,l.y-4+row*l.step,l.right-l.left,l.step});return true;}
    if(g::back.has(x,y)){begin(u,x,y,Back,g::back);return true;}
    if(g::source.has(x,y)){if(config_allowed(u))begin(u,x,y,Source,g::source);return true;}
    if(g::setup.has(x,y)){if(config_allowed(u))begin(u,x,y,Setup,g::setup);return true;}
    for(int i=0;i<3;++i)if(g::modes[i].has(x,y)){if(config_allowed(u))begin(u,x,y,Mode+i,g::modes[i]);return true;}
    if(y>=60&&y<93&&x>=8&&x<312&&frame.loaded){const int left=14+frame.edit_start,right=14+frame.edit_end-1;const int p=std::abs(x-left)<=std::abs(x-right)?C::P_START:C::P_LENGTH;
        if(std::min(std::abs(x-left),std::abs(x-right))<=8){select(p);if(allowed(u,p)){begin(u,x,y,-1,{8,60,304,33});touch.param=p;touch.origin=edit(u,p);}}return true;}
    for(int p=0;p<10;++p){const auto b=g::parameter(p);if(!b.has(x,y))continue;select(p);if(!allowed(u,p))return true;
        begin(u,x,y,-1,b);touch.param=p;touch.origin=edit(u,p);return true;}return true;}
void rot_focus_drag(RotUnit& u,int x,int y){using namespace rot_private;if(!touch.owner.unit)return;
    if(blocked||!context()||param_lens_is_open()){cancel();return;}platform::AudioOut::Guard guard(*g_audio);if(g_drag!=Drag::RotParam||!matches(touch.owner,u)){cancel();return;}
    if(touch.action>=0){const int64_t dx=int64_t(x)-touch.x,dy=int64_t(y)-touch.y;if(!touch.box.has(x,y)||dx*dx+dy*dy>64)cancel();return;}
    if(!allowed(u,touch.param)){cancel();return;}const int divisor=fine?8:1;if(divisor!=touch.divisor){touch.x=touch.last;touch.origin=edit(u,touch.param);touch.divisor=divisor;}if(x==touch.last)return;touch.last=x;
    const bool bi=u.params[touch.param].bipolar;const auto raw=q15(std::clamp<int64_t>(int64_t(touch.origin)+(int64_t(x)-touch.x)*(bi?65534:32767)/(128*divisor),bi?-32767:0,32767));write(u,touch.param,raw);}
void rot_focus_touch_release(){using namespace rot_private;bool browser=false,back=false;
    if(touch.owner.unit&&context()&&!blocked&&!param_lens_is_open()){platform::AudioOut::Guard guard(*g_audio);auto* u=current();if(u&&g_drag==Drag::RotParam&&matches(touch.owner,*u)){const int a=touch.action;
        if(a==Source&&config_allowed(*u))browser=true;else if(a==Back)back=true;else if(a==Setup&&config_allowed(*u))open(*u,1);
        else if(a>=Mode&&a<Mode+3&&config_allowed(*u)){const auto next=C::Mode(a-Mode);if(u->config().mode!=next&&u->set_mode(next))mark_project_mutated();}
        else if(a>=0&&a<6)activate(*u,a);}}
    clear_touch();wait_lift=false;
    if(browser){scan_sample_files();g_overlay=Overlay::SampleBrowser;}if(back){rot_focus_release();g_overlay=Overlay::None;}}
void rot_focus_menu_touch(u32 down,u32 held,int x,int y){using namespace rot_private;if(!menu||wait_lift)return;
    if(held&KEY_TOUCH){C* u=nullptr;{platform::AudioOut::Guard guard(*g_audio);u=current();}if(u){if(down&KEY_TOUCH)rot_focus_touch_body(*u,x,y);else if(touch.owner.unit)rot_focus_drag(*u,x,y);}}else if(touch.owner.unit)rot_focus_touch_release();}
void rot_focus_value_text(char* out,size_t n,int p,q15 value){rot_ui::value_text(out,n,p,value);}
void rot_focus_capture(){using namespace rot_private;frame={};const uint64_t now=osGetTime();bool reset=false;
    if(!context()){cancel();if(view_owner.unit)retire=true;view_owner=motion_owner={};lease.reset();return;}
    {platform::AudioOut::Guard guard(*g_audio);auto* u=current();if(!u){cancel();if(view_owner.unit)retire=true;view_owner=motion_owner={};lease.reset();return;}
        const auto o=owner(*u);reset=!physical(view_owner,o);view_owner=o;
        if((touch.owner.unit&&(g_drag!=Drag::RotParam||!matches(touch.owner,*u)))||(menu&&!matches(menu_owner,*u)))cancel();
        auto& f=frame;f.valid=true;f.chain=o.chain;f.slot=o.slot;f.sample=o.sample;f.generation=o.generation;f.frames=o.frames;f.bank=o.bank;f.lens=param_lens_is_open();
        f.bypass=o.bypass;f.mute=o.mute;f.busy=o.busy;f.overwritten=o.overwritten;f.passes=o.passes;f.seed=o.seed;f.config_mode=o.mode;
        g_eng->capture_rot_visual(o.chain,o.slot,f.visual);g_eng->capture_rot_artwork(o.chain,o.slot,captured);f.loaded=f.visual.loaded&&f.frames>1;
        if(unsigned(f.sample)<trackr::synth::SAMPLE_BANK_SIZE){const char* path=PB::instance().provenance(f.sample).path;const char* name=std::strrchr(path,'/');name=name?name+1:path;const auto n=strnlen(name,sizeof f.source_leaf-1);std::memcpy(f.source_leaf,name,n);f.source_leaf[n]=0;}
        for(int p=0;p<12;++p){f.edit[p]=edit(*u,p);f.live[p]=f.visual.effective[p];if(!allowed(*u,p))f.disabled|=uint16_t(1u<<p);}f.linked=focus_mod_links(*u);
        f.eligible=f.loaded&&f.visual.active&&!f.bypass&&!f.mute&&!f.busy&&!f.overwritten&&!o.overrides&&!f.visual.paused&&!f.visual.fault&&!f.visual.preparing&&f.visual.sample_slot==f.sample&&f.visual.source_generation==f.generation&&f.visual.source_frames==f.frames;
        f.menu=uint8_t(menu);
    }
    // Guard ends above: film projection, source-aged sparks, lease and cache
    // decisions consume only the consistent copied scalar/array snapshot.
    auto& f=frame;
    reset=reset||(captured.valid&&admitted.valid&&(captured.pass[0]<admitted.pass[0]||captured.history<admitted.history));
    if(reset||(!captured.valid&&admitted.valid)){retire=true;std::memset(static_cast<void*>(&admitted),0,sizeof admitted);pose={};std::memset(static_cast<void*>(&film),0,sizeof film);std::fill(std::begin(spark_age),std::end(spark_age),0u);++source_serial;++artwork_serial;++spark_serial;lease.reset();}
    if(!equal(motion_owner,view_owner)){lease.reset();motion_owner=view_owner;}
    const auto movement=lease.capture(f.visual.serial,f.visual.phase_frames,f.visual.drop_q8,now,f.eligible);
    if(f.eligible&&captured.valid&&(reset||!admitted.valid||(movement.fresh&&movement.live))){
        if(captured.serial==f.visual.serial&&captured.source_generation==f.generation&&captured.source_frames==f.frames){
            const bool changed=!admitted.valid||admitted.serial!=captured.serial||admitted.history!=captured.history||admitted.pass[0]!=captured.pass[0]||admitted.loop_start!=captured.loop_start||admitted.loop_span!=captured.loop_span||admitted.source_env_complete!=captured.source_env_complete||std::memcmp(admitted.source_env,captured.source_env,sizeof captured.source_env)||std::memcmp(admitted.env,captured.env,sizeof captured.env)||std::memcmp(admitted.mask,captured.mask,sizeof captured.mask)||std::memcmp(admitted.scar,captured.scar,sizeof captured.scar);
            admitted=captured;if(changed){copy_film();++artwork_serial;}sparks(pose,f.visual);pose=f.visual;
        }}
    f.eligible=f.eligible&&movement.live;
    auto& a=f.art;a.valid=true;a.selected=selected;a.source_serial=source_serial;a.artwork_serial=artwork_serial;a.spark_serial=spark_serial;a.material_revision=palette::revision();
    a.pass=int(pose.pass);a.passes=int(f.visual.serial?pose.pass_count:f.passes);a.law=pose.law;a.rot_percent=int(pose.rot_mean)*100/32767;a.loop_ms=int(pose.loop_span/32);a.mode=v::RotFrame::Mode(pose.mode);
    a.status=!f.loaded?v::RotFrame::Empty:f.visual.fault?v::RotFrame::Fault:f.visual.preparing?v::RotFrame::Preparing:f.bypass?v::RotFrame::Bypass:!f.eligible?v::RotFrame::Paused:v::RotFrame::Ready;
    a.history=admitted.valid?std::min(12,int(admitted.history)):0;a.drop_q8=pose.drop_q8;
    if(pose.loop_span&&pose.source_frames){a.head=int(uint64_t(pose.head-pose.loop_start)*372/pose.loop_span);a.strip_head=int(uint64_t(pose.head)*292/pose.source_frames);a.loop_x0=int(uint64_t(pose.loop_start)*292/pose.source_frames);a.loop_x1=int(uint64_t(pose.loop_start+pose.loop_span)*292/pose.source_frames);}
    const auto target=C::loop_projection(f.frames,f.edit[C::P_START],f.edit[C::P_LENGTH]);if(target.valid){f.edit_start=int(uint64_t(target.start)*292/f.frames);f.edit_end=std::max(f.edit_start+1,int(uint64_t(target.start+target.span)*292/f.frames));f.pending=admitted.valid&&(target.start!=pose.loop_start||target.span!=pose.loop_span);}
    a.window_pending=f.pending;a.lens=f.lens;
    std::snprintf(a.source,sizeof a.source,"%.26s",f.source_leaf[0]?f.source_leaf:f.loaded?"source >":"no source >");
    std::snprintf(a.detail,sizeof a.detail,"%s",!f.loaded?"empty source":f.visual.fault?"tape unavailable":f.visual.preparing?"preparing new region":f.mute?"muted":f.busy?"recording":f.overwritten?"overwritten source":f.pending?"window pending pass / trigger":!f.eligible?"paused source":"");
    for(int p=0;p<10;++p){auto& k=a.knobs[p];g::value_text(k.value,16,p,f.edit[p]);k.edit_q8=g::q8(f.edit[p],p==C::P_PITCH);k.live_q8=g::q8(f.live[p],p==C::P_PITCH);k.linked=bool(f.linked&(1u<<p));k.disabled=bool(f.disabled&(1u<<p));k.quiet=f.bypass||f.mute;}
    resource.prepare();rows(f);if(menu&&dirty){auto& cache=sigil_surface::bottom_cache;
        for(int y=0;y<sigil_surface::H;++y){for(int x=0;x<sigil_surface::W;++x){uint32_t c=0xff000000u;for(unsigned shift:{0u,8u,16u}){unsigned sum=0;for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx)sum+=(m::sample(false,float(x*2+dx),float(y*2+dy))>>shift)&255;c|=((sum+2)/4)<<shift;}cache.pixels[y*sigil_surface::W+x]=c;}}closed(sigil_surface::Painter{&cache},f);cache.finish();}
    f.motion=word_menu::capture(word_motion,now,unsigned(menu),f.focus,f.layout,[&](int i){return f.rows[std::clamp(i,0,std::max(0,int(f.count)-1))];},dirty);dirty=false;
}
void rot_focus_draw(const RotUnit&){rot_ui::draw_bottom();}
bool rot_focus_draw_top(){using namespace rot_private;if(!frame.valid)return false;v::rot_draw_top(frame.art,film,resource);return true;}
void rot_focus_gpu_frame(bool synchronized){using namespace rot_private;if(!synchronized)return;if(retire){resource.shutdown_after_fence(true);retire=false;}if(frame.valid){resource.prepare();resource.upload_after_fence(frame.art,film,true);}}
void rot_focus_shutdown(bool synchronized){rot_private::resource.shutdown_after_fence(synchronized);}
} // seance::platform::ui3ds
