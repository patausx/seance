// Actual patchbay/nav/menu UI + actual portable engine. In-memory raster only.
#include "../platform/3ds/ui_patchbay.h"
#include "../platform/3ds/ui_sigils.h"
#include "../platform/3ds/sigilbay_type.h"
#include "../platform/3ds/sigil_surface.h"
#define main inherited_menu_stage1_main
#include "menu_stage1_test.cpp"
#undef main
namespace seance::platform::ui3ds {
q15 g_sigil_undo[NUM_SIGILS][SIGIL_POINTS]={},g_sigil_clip[SIGIL_POINTS]={};
bool g_sigil_undo_valid[NUM_SIGILS]={},g_sigil_clip_valid=false,g_sig_canvas_mutated=false,g_smooth_active=false;
int g_smooth_py=0;u32 g_ui_rng=0x12345678;
}
namespace {
void assert_palette_frame(const Frame& f){
    std::set<u32> allowed={m::ink(),m::spot(),m::ghost(),m::branch(),theme_test::p::cell_dim(),theme_test::p::inverse(),theme_test::p::subdued()};
    for(int y=0;y<240;++y){
        const u32 bg=paper_color(y),spot=m::spot();allowed.insert(bg);
        // Approved source/target focus wash: exact 20/255 accent over paper.
        u32 wash=0xff000000u;
        for(unsigned shift:{0u,8u,16u})wash|=((((spot>>shift)&255)*20+((bg>>shift)&255)*235+127)/255)<<shift;
        allowed.insert(wash);
    }
    for(size_t i=0;i<f.pixels.size();++i)if(!allowed.count(f.pixels[i])){
        std::fprintf(stderr,"palette mismatch %dx240 at %zu,%zu color=%08x paper=%08x\n",f.width,i%f.width,i/f.width,f.pixels[i],paper_color(int(i/f.width)));
        CHECK(false);
    }
}
}

namespace patchbay_main {
struct touchPosition { int px=0,py=0; } pen;
int reads=0,fallthrough=0;
void hidTouchRead(touchPosition* out){++reads;*out=pen;}
void touch(u32 down,u32 held){
#include <patchbay_touch.inc>
    ++fallthrough;
}
struct ButtonResult {u32 remaining;bool owns;};
ButtonResult buttons(u32 down,u32 held){
    bool stage_skip_touch=false;
#include <sigils_buttons.inc>
#include <patchbay_buttons.inc>
    return {down,stage_skip_touch};
}
}
namespace patchbay_test {
int pairs=0,peak=0;
const char* current_case="";
struct Pair {Frame top,bottom;};
bool has(const Frame& f,const std::string& needle){for(const auto& t:f.texts)if(t.text.find(needle)!=std::string::npos)return true;return false;}
struct RestoreGlobals {
    Engine* engine=g_eng;platform::AudioOut* audio=g_audio;
    ~RestoreGlobals(){g_eng=engine;g_audio=audio;}
};
Pair frozen(bool edges=true){
    patchbay_capture(); RestoreGlobals restore;g_eng=nullptr;g_audio=nullptr;
    const auto top=render(400,patchbay_draw_top);const auto bottom=render(320,patchbay_draw);
    CHECK(top.objects+bottom.objects<=8192);peak=std::max(peak,top.objects+bottom.objects);++pairs;
    ++g_ui_frame;host_time_ms+=17;
    CHECK(top.pixels==render(400,patchbay_draw_top).pixels);
    CHECK(bottom.pixels==render(320,patchbay_draw).pixels);
    assert_palette_frame(top);assert_palette_frame(bottom);if(edges){paper_edges(top);paper_edges(bottom);}
    return {top,bottom};
}
void key(u32 down,u32 held=0){patchbay_buttons(down,held|down);}
void touch_down(int x,int y){patchbay_main::pen={x,y};patchbay_main::touch(KEY_TOUCH,KEY_TOUCH);}
void hold(int x,int y){patchbay_main::pen={x,y};patchbay_main::touch(0,KEY_TOUCH);}
void lift(){patchbay_main::touch(0,0);}
void tap(int x,int y){touch_down(x,y);lift();}
void drag(int x0,int y0,int x1,int y1){touch_down(x0,y0);hold(x1,y1);lift();}
struct Bay : Fixture {
    Bay(){g_view=View::Patchbay;patchbay_forget();patchbay_enter();}
    ~Bay(){patchbay_forget();nav_close();}
    Unit* add(int slot=0,UnitKind kind=UnitKind::Comp,int chain=0){auto* u=make_unit(kind);CHECK(u);delete engine->replace_unit(chain,slot,u);return u;}
};
class FullBank final : public Unit {
public:
    FullBank(){num_params=PARAM_BANK_SIZE;for(auto& p:params){p.name="longest_param";p.base=14000;}}
    void process(q15*,int) override {}
};
void render_ticks(Engine& engine,int count){q15 out[64];while(count-->0)engine.render(out,32);}
void navigation_and_main(){
    static_assert(int(View::Bounce)==4&&int(View::Patchbay)==5,"append-only view");
    Bay f;auto*u=f.add();NavChoice out=NavChoice::None;
    constexpr NavChoice choices[]={NavChoice::Chains,NavChoice::Sigils,NavChoice::Patchbay,NavChoice::Sequencer,NavChoice::Performance,NavChoice::Bounce,NavChoice::Patch,NavChoice::Exit};
    for(int i=0;i<8;++i){nav_open();nav_buttons(0,out);nav_touch_down(80,64+i*20,out);CHECK(out==choices[i]&&!nav_is_open());}
    CHECK(mutations==0);
    nav_open();nav_capture();{RestoreGlobals restore;g_eng=nullptr;g_audio=nullptr;auto menu=render(320,nav_draw);CHECK(has(menu,"sigilbay")&&has(menu,"exit")&&!has(menu,"stress"));assert_palette_frame(menu);paper_edges(menu);}nav_close();
    patchbay_main::reads=patchbay_main::fallthrough=0;
    drag(102,72,210,72);CHECK(u->params[0].src==ModSource::SigilA&&patchbay_main::reads==2&&patchbay_main::fallthrough==0);
    g_overlay=Overlay::Editor;patchbay_main::touch(KEY_TOUCH,KEY_TOUCH);CHECK(patchbay_main::fallthrough==1);g_overlay=Overlay::None;
    touch_down(102,104);auto r=patchbay_main::buttons(KEY_RIGHT|KEY_TOUCH,KEY_RIGHT|KEY_TOUCH);
    CHECK(r.owns&&r.remaining==KEY_TOUCH);hold(210,104);lift();CHECK(u->params[1].src==ModSource::None);
    // Local controller strips A/Y/etc; START survives for the global browser.
    touch_down(102,104);r=patchbay_main::buttons(KEY_START|KEY_X|KEY_TOUCH,KEY_START|KEY_X|KEY_TOUCH);
    CHECK(r.owns&&r.remaining==KEY_START);hold(210,104);lift();CHECK(u->params[1].src==ModSource::None);
    CHECK(mutations==1);
    patchbay_forget();patchbay_enter();key(KEY_B);CHECK(nav_is_open());nav_close();
    std::puts("ok 1: real eight-row navigation, exact-main touch/local button ownership and START tail cancellation");
}
void cables(){
    Bay f;auto*u=f.add();auto&p0=u->params[0];auto&p1=u->params[1];auto&p2=u->params[2];
    drag(102,72,210,72);CHECK(mutations==1&&p0.src==ModSource::SigilA&&p0.mod_depth==16384&&p0.mod_enabled);
    drag(102,104,210,72);CHECK(mutations==1&&p0.src==ModSource::SigilA);CHECK(has(frozen().top,"input occupied"));
    p0.mod_depth=-12345;p0.mod_enabled=false;
    drag(210,72,210,104);CHECK(mutations==2&&p0.src==ModSource::None&&p0.mod_depth==0&&p0.mod_enabled);
    CHECK(p1.src==ModSource::SigilA&&p1.mod_depth==-12345&&!p1.mod_enabled);
    drag(210,104,210,104);CHECK(mutations==2); // same endpoint = no-op
    drag(210,104,102,136);CHECK(mutations==3&&p1.src==ModSource::SigilC&&p1.mod_depth==-12345&&!p1.mod_enabled);
    drag(210,104,102,136);CHECK(mutations==3); // same source = no-op
    p2.src=ModSource::SigilD;p2.mod_depth=777;
    drag(210,104,210,136);CHECK(mutations==3&&p1.src==ModSource::SigilC&&p2.src==ModSource::SigilD&&p2.mod_depth==777);
    tap(156,132);CHECK(mutations==3); // actual bowed cable c->row1 midpoint
    touch_down(80,201);CHECK(p1.mod_depth==-32767&&mutations==4);hold(80,201);CHECK(mutations==4);lift();
    tap(151,226);CHECK(p1.mod_depth==32767&&mutations==5);tap(30,201);CHECK(p1.mod_enabled&&mutations==6);
    tap(167,201);CHECK(p1.mod_depth==0&&mutations==7);tap(151,226);CHECK(mutations==7);
    tap(254,201);CHECK(p1.mod_depth==32767&&mutations==8);key(KEY_Y);CHECK(!p1.mod_enabled&&mutations==9);
    auto image=frozen();CHECK(has(image.bottom,"off / y")&&has(image.bottom,"+100%"));
    tap(280,226);CHECK(p1.src==ModSource::None&&p1.mod_depth==0&&p1.mod_enabled&&mutations==10);
    tap(280,226);tap(151,226);key(KEY_Y);CHECK(mutations==10);
    // Signed INT16_MIN imported depth inverts safely without wraparound.
    p1.src=ModSource::SigilA;p1.mod_depth=-32768;tap(250,104);tap(151,226);CHECK(p1.mod_depth==32767&&mutations==11);
    frozen();
    std::puts("ok 2: connect/occupied, endpoint move/source swap, signed amount/mute/invert/remove and dirty no-ops");
}
void all_targets(){
    Bay f;
    for(int chain=0;chain<NUM_CHAINS;++chain)for(int slot=0;slot<UNITS_PER_CHAIN;++slot)delete f.engine->replace_unit(chain,slot,new FullBank);
    for(int chain=0;chain<NUM_CHAINS;++chain){
        CHECK(g_sel_chain==chain);
        for(int page=0;page<18;++page){
            for(int row=0;row<4;++row){const int index=page*4+row;drag(102,72,210,72+row*32);auto& p=f.engine->chains[chain].units[index/12]->params[index%12];CHECK(p.src==ModSource::SigilA&&p.mod_depth==16384);}
            if(page==17){auto image=frozen();CHECK(has(image.bottom,"params 18/18")&&has(image.top,"chain "+std::to_string(chain+1)));}
            key(KEY_RIGHT);
        }
        CHECK(has(frozen().bottom,"params 1/18"));key(KEY_RIGHT,KEY_L);
    }
    CHECK(mutations==288&&g_sel_chain==0);
    // Last page is reachable in the other direction, including chain wrap.
    key(KEY_LEFT);CHECK(has(frozen().bottom,"params 18/18"));key(KEY_LEFT,KEY_L);CHECK(g_sel_chain==3);
    std::puts("ok 3: real UI fanout to all 288 parameter slots, 18-page wrap and independent chain navigation");
}
void sig_tap(int x,int y){sigils_view_touch_down(x,y);sigils_view_touch_release();}
void sig_drag(int x,int y,int xx,int yy){sigils_view_touch_down(x,y);sigils_view_touch_drag(xx,yy);sigils_view_touch_release();}
void sig_key(u32 down,u32 held=0){CHECK(sigils_view_buttons(down,held|down));}
void choose(int sigil){sig_tap(158,24);sig_tap(167,55+sigil*18);CHECK(g_sel_sigil==sigil);}
void shape(int index){sig_tap(80,47);sig_tap(95,78+index*20);}
void mode(int index){sig_tap(265,47);sig_tap(264,78+index*20);}
Pair frozen_sigil(){
    sigils_capture();sigils_gpu_frame();RestoreGlobals restore;g_eng=nullptr;g_audio=nullptr;
    auto top=render(400,sigils_top_draw),bottom=render(320,sigils_view_draw);++pairs;
    CHECK(top.pixels==render(400,sigils_top_draw).pixels);CHECK(bottom.pixels==render(320,sigils_view_draw).pixels);
    CHECK(!has(top,"lfo")&&!has(bottom,"lfo"));
    // Host blur materializes texture pixels as rects; production is one quad.
    // No combined GPU count/fps claim for disclosure frames.
    return {top,bottom};
}
void universal_editor(){
    Bay f;f.add();g_view=View::Sigils;g_sel_sigil=0;sigils_forget_undo();
    for(int i=0;i<8;++i){choose(i);CHECK(mutations==0);}
    sig_drag(20,110,300,70);CHECK(f.engine->sigils[7].drawn);q15 drawing[128];std::memcpy(drawing,f.engine->sigils[7].table,sizeof drawing);
    const int edited=mutations;sig_tap(280,23);CHECK(!f.engine->sigils[7].drawn&&mutations==edited+1);sig_tap(280,23);CHECK(!std::memcmp(drawing,f.engine->sigils[7].table,sizeof drawing));
    for(int i=1;i<7;++i){shape(i);CHECK(f.engine->sigil_config[7].shape==SigilShape(i));CHECK(!std::memcmp(drawing,f.engine->sigils[7].table,sizeof drawing));frozen_sigil();}
    shape(0);CHECK(!std::memcmp(drawing,f.engine->sigils[7].table,sizeof drawing));
    auto c=f.engine->sigil_config[7];c.rate_millihz=1000;c.legacy=false;c.legacy_rate=false;f.engine->set_sigil_config(7,c);
    int before=mutations;sig_tap(30,138);CHECK(mutations==before);sig_drag(112,140,132,140);CHECK(f.engine->sigil_config[7].rate_millihz==2000);
    sig_tap(63,138);CHECK(f.engine->sigil_sync[7]);sig_drag(112,140,192,140);CHECK(f.engine->sigil_div[7]==5&&f.engine->sigil_config[7].rate_millihz==2000);
    before=mutations;sig_tap(87,211);sig_drag(220,220,270,220);CHECK(mutations==before); // paused rate mod
    sig_tap(30,138);sig_drag(110,173,300,173);CHECK(f.engine->sigil_config[7].phase_degrees==359);
    sig_tap(40,190);sig_tap(52,161);CHECK(f.engine->sigil_config[7].unipolar);
    sig_tap(209,190);sig_tap(216,161);CHECK(f.engine->sigil_config[7].restart==SigilRestart::Transport);
    sig_tap(85,211);sig_tap(99,38+7*18);CHECK(f.engine->sigil_mods[7].src==ModSource::SigilG);
    sig_drag(300,220,190,220);CHECK(f.engine->sigil_mods[7].depth==-32767);
    sig_drag(208,153,300,153);CHECK(f.engine->sigils[7].depth==32767);
    before=mutations;render_ticks(*f.engine,10);sig_key(KEY_A);CHECK(f.engine->sigils[7].phase==sigil_phase_offset(359)&&mutations==before);
    for(int i=0;i<6;++i){mode(i);CHECK(f.engine->sigils[7].mode==SigilMode(i));frozen_sigil();}
    before=mutations;sig_drag(110,173,250,173);sig_drag(112,140,152,140);CHECK(mutations==before); // Follow-owned rate/phase
    sig_tap(214,47);CHECK(f.engine->step_dest[7]==0);
    mode(3);before=mutations;sig_tap(80,47);sig_tap(40,190);sig_drag(110,173,250,173);CHECK(mutations==before);
    mode(0);sig_tap(140,231);CHECK(g_view==View::Patchbay);CHECK(has(frozen().bottom,"sigil h"));key(KEY_B);CHECK(g_view==View::Sigils);
    std::puts("ok 4: a-h editor, independent drawing/undo, all seven shapes, full settings, legacy/follow disabled controls and sigilbay return");
}
void sigils_env_and_lifecycle(){
    Bay f;auto*u=f.add();sigils_forget_undo();tap(30,72);CHECK(g_view==View::Sigils&&g_sel_sigil==0);
    sig_tap(158,24);auto r=patchbay_main::buttons(KEY_B|KEY_TOUCH,KEY_B|KEY_TOUCH);CHECK(g_view==View::Sigils&&r.owns&&r.remaining==0);
    r=patchbay_main::buttons(KEY_B,KEY_B);CHECK(g_view==View::Patchbay&&r.owns&&mutations==0);
    key(KEY_DOWN);CHECK(has(frozen().bottom,"sigil e"));drag(102,72,210,72);CHECK(u->params[0].src==ModSource::SigilE);tap(280,226);CHECK(u->params[0].src==ModSource::None);
    key(KEY_DOWN);tap(30,72);CHECK(has(frozen().bottom,"post-module envelope follower"));
    const int base=mutations;tap(30,96);CHECK(f.engine->env_follow_source()==1&&mutations==base+1);tap(30,141);CHECK(f.engine->env_follow_response()==EnvResponse::Body&&mutations==base+2);frozen();key(KEY_B);
    for(int reason=0;reason<7;++reason){
        g_view=View::Patchbay;g_overlay=Overlay::None;g_sel_chain=0;nav_close();patchbay_forget();patchbay_enter();
        touch_down(102,72);hold(210,72);const int before=mutations;std::unique_ptr<Engine> other;platform::AudioOut alternate_audio;
        if(reason==0)patchbay_cancel_input();
        if(reason==1){g_sel_chain=1;patchbay_capture();g_sel_chain=0;}
        if(reason==2){g_overlay=Overlay::Editor;patchbay_capture();g_overlay=Overlay::None;}
        if(reason==3){nav_open();patchbay_capture();nav_close();}
        if(reason==4){g_view=View::Performance;patchbay_capture();g_view=View::Patchbay;}
        if(reason==5){other=std::make_unique<Engine>();g_eng=other.get();patchbay_capture();g_eng=f.engine.get();}
        if(reason==6){g_audio=&alternate_audio;patchbay_capture();g_audio=&f.audio;}
        lift();CHECK(u->params[0].src==ModSource::None&&mutations==before);
    }
    drag(102,72,210,72);touch_down(210,72);hold(210,104);const int before=mutations;auto* replaced=f.add();patchbay_capture();lift();CHECK(replaced->params[0].src==ModSource::None&&replaced->params[1].src==ModSource::None&&mutations==before);
    g_view=View::Sigils;g_sel_sigil=0;shape(1);sigils_view_touch_down(110,140);sigils_view_touch_drag(120,140);const auto rate=f.engine->sigil_config[0].rate_millihz;const int dirty=mutations;
    nav_open();sigils_capture();nav_close();sigils_view_touch_drag(220,140);sigils_view_touch_release();CHECK(f.engine->sigil_config[0].rate_millihz==rate&&mutations==dirty);
    sig_tap(158,24);r=patchbay_main::buttons(KEY_START|KEY_X|KEY_TOUCH,KEY_START|KEY_X|KEY_TOUCH);CHECK(r.owns&&r.remaining==KEY_START);
    sig_tap(158,24);auto fresh=std::make_unique<Engine>();f.engine->swap_patch_state(*fresh);sigils_forget_undo();sigils_view_touch_drag(200,90);sigils_view_touch_release();CHECK(!f.engine->sigils[0].drawn);
    std::puts("ok 5: exact-main modal button ownership, env and e-h routing, changed context/stroke cancellation and in-place load forget");
}
void sigil_motion(){
    Bay f;g_view=View::Sigils;g_sel_sigil=0;sigils_forget_undo();host_time_ms=1000;
    shape(1);mode(0);frozen_sigil();const int dirty=mutations;
    sig_tap(80,47);const auto opening=frozen_sigil();
    auto& cache=sigil_surface::bottom_cache;
    const std::vector<uint32_t> background(cache.pixels,cache.pixels+sigil_surface::W*sigil_surface::H);
    host_time_ms+=70;const auto middle=frozen_sigil();
    host_time_ms+=70;const auto settled=frozen_sigil();
    CHECK(opening.bottom.pixels!=middle.bottom.pixels&&middle.bottom.pixels!=settled.bottom.pixels);
    CHECK(std::equal(background.begin(),background.end(),cache.pixels));
    sig_key(KEY_DOWN);const auto start_focus=frozen_sigil();
    host_time_ms+=50;const auto moving=frozen_sigil();
    sig_key(KEY_UP);const auto retarget=frozen_sigil();
    // Words stay fixed and logical selection is immediate; only the underline travels.
    CHECK(moving.bottom.texts.size()==retarget.bottom.texts.size());
    for(size_t i=0;i<moving.bottom.texts.size();++i){
        const auto& t=moving.bottom.texts[i];const auto& r=retarget.bottom.texts[i];
        CHECK(t.text==r.text&&t.x==r.x&&t.y==r.y);
    }
    CHECK(start_focus.bottom.pixels!=moving.bottom.pixels);
    host_time_ms+=100;frozen_sigil();CHECK(mutations==dirty);
    CHECK(std::equal(background.begin(),background.end(),cache.pixels));
    sig_key(KEY_B);sig_tap(80,47);sig_tap(95,118); // triangle, before a reveal frame
    CHECK(f.engine->sigil_config[0].shape==SigilShape::Triangle);
    frozen_sigil();sig_drag(112,140,132,140);const auto rate=frozen_sigil();
    host_time_ms+=150;const auto rate_rest=frozen_sigil();CHECK(rate.bottom.pixels!=rate_rest.bottom.pixels);
    const int edited=mutations;sig_drag(112,140,112,140);CHECK(mutations==edited);
    CHECK(frozen_sigil().bottom.pixels==rate_rest.bottom.pixels); // no-op has no pulse
    sig_key(KEY_A);const auto trigger=frozen_sigil();CHECK(mutations==edited);
    host_time_ms+=150;CHECK(trigger.bottom.pixels!=frozen_sigil().bottom.pixels);
    shape(0);sig_drag(80,90,160,70);const auto ink=frozen_sigil();
    host_time_ms+=190;CHECK(ink.bottom.pixels!=frozen_sigil().bottom.pixels);
    sig_drag(80,90,160,70);frozen_sigil();sigils_forget_undo();const auto forgotten=frozen_sigil();
    host_time_ms+=190;CHECK(forgotten.bottom.pixels==frozen_sigil().bottom.pixels);
    // Real audio advances the phase. Stopping capture history removes only the tail.
    shape(1);frozen_sigil();render_ticks(*f.engine,10);host_time_ms+=17;frozen_sigil();
    render_ticks(*f.engine,10);host_time_ms+=17;const auto trail=frozen_sigil();
    host_time_ms+=100;const auto expired=frozen_sigil();CHECK(trail.top.pixels!=expired.top.pixels);
    sigils_forget_undo();CHECK(expired.top.pixels==frozen_sigil().top.pixels);
    render_ticks(*f.engine,10);host_time_ms+=17;frozen_sigil();
    host_time_ms=0;const auto backwards=frozen_sigil();sigils_forget_undo();
    CHECK(backwards.top.pixels==frozen_sigil().top.pixels);
    std::puts("ok 8: sigil reveal/focus, fixed blur and text, instant input, feedback/no-op, bounded real trail and lifecycle reset");
}
void sigil_details(){
    Bay f;g_view=View::Sigils;g_sel_sigil=0;sigils_forget_undo();host_time_ms=1000;
    shape(5);mode(0);auto& s=f.engine->sigils[0];s.value=16384;
    const auto first=frozen_sigil();CHECK(has(first.top,"history / 3s / ui sampled"));
    const auto rng=s.random_state;const int dirty=mutations;
    host_time_ms+=50;s.value=-16384;const auto stepped=frozen_sigil();
    CHECK(stepped.top.pixels[92*400+374]==m::ink());
    CHECK(stepped.top.pixels[110*400+377]==m::branch());
    CHECK(s.random_state==rng&&s.value==-16384&&mutations==dirty);
    // Enough captures to wrap the fixed ring; old values disappear after 3s.
    for(int i=0;i<70;++i){host_time_ms+=50;sigils_capture();}
    const auto full=frozen_sigil();CHECK(full.top.pixels[128*400+20]==m::ink());
    host_time_ms+=201;const auto gap=frozen_sigil();CHECK(gap.top.pixels[128*400+20]==paper_color(128));
    host_time_ms=0;CHECK(frozen_sigil().top.pixels==gap.top.pixels);
    shape(6);s.value=16384;frozen_sigil();host_time_ms+=50;s.value=-16384;
    CHECK(frozen_sigil().top.pixels[110*400+377]==m::ink()); // sampled smooth, not held steps
    mode(3);s.value=16384;frozen_sigil();host_time_ms+=50;s.value=-16384;
    CHECK(frozen_sigil().top.pixels[110*400+377]==m::branch()); // legacy mode owns shape
    mode(0);choose(1);shape(6);f.engine->sigils[1].value=-16384;
    CHECK(frozen_sigil().top.pixels[128*400+374]==paper_color(128)); // no cross-sigil history
    choose(0);for(auto& point:s.table)point=32767;s.drawn=true;
    sig_tap(80,47);frozen_sigil();host_time_ms+=140;const auto choices=frozen_sigil();
    for(int row=0;row<7;++row){bool visible=false;
        for(int y=78+row*20;y<=86+row*20;++y)for(int x=136;x<=172;++x)
            visible|=choices.bottom.pixels[y*320+x]==(row==6?m::spot():m::ghost());
        CHECK(visible);
    }
    CHECK(choices.bottom.pixels[78*320+150]==m::ghost()); // actual saved drawn contour
    sig_tap(160,98);CHECK(f.engine->sigil_config[0].shape==SigilShape::Sine&&s.table[0]==32767);
    CHECK(!has(frozen_sigil().top,"history /")); // thumbnail keeps existing row hitbox
    // Counts include muted/zero routes, clock and paused rate links; ENV is not a sigil.
    for(int c=0;c<NUM_CHAINS;++c)for(int slot=0;slot<UNITS_PER_CHAIN;++slot){
        auto* bank=new FullBank;for(auto& p:bank->params){p.src=ModSource::SigilA;p.mod_depth=0;p.mod_enabled=false;}
        delete f.engine->replace_unit(c,slot,bank);
    }
    auto* bank=f.engine->chains[0].units[0];bank->params[0].src=ModSource::EnvFollow;
    bank->params[1].src=ModSource::SigilE;bank->params[2].src=ModSource::SigilH;
    f.engine->set_clock_mod(ClockTarget::Tempo,ModSource::SigilA,0);
    f.engine->set_clock_mod(ClockTarget::Swing,ModSource::SigilH,0);
    for(int i=1;i<NUM_SIGILS;++i){f.engine->set_sigil_mod(i,ModSource::SigilA,0);f.engine->set_sigil_sync(i,true);}
    const int before=mutations;sig_tap(158,24);const auto counts=frozen_sigil();
    const char* expected[]={"293","0","0","0","1","0","0","2"};
    CHECK(has(counts.bottom,"links")&&has(counts.top,"destinations / 293"));
    for(int row=0;row<8;++row){bool found=false;
        for(const auto& t:counts.bottom.texts)if(t.y==55+row*18&&t.x>210&&t.text==expected[row])found=true;
        CHECK(found);
    }
    CHECK(mutations==before);sig_key(KEY_B);
    std::puts("ok 9: sampled 3s history/reset/ring, static shape choices and complete a-h assignment counts");
}
void sigilbay_polish(){
    Bay f;auto* u=f.add();auto& p0=u->params[0];auto& p1=u->params[1];
    auto image=frozen();CHECK(image.bottom.pixels[72*320+210]==paper_color(72));
    // Occupied-but-off and zero-depth remain occupied even on another source page.
    p0.src=ModSource::SigilH;p0.mod_depth=0;p0.mod_enabled=false;
    image=frozen();CHECK(has(image.bottom,"h")&&has(image.bottom,"off"));
    CHECK(image.bottom.pixels[72*320+210]==m::ghost());
    CHECK(image.bottom.pixels[72*320+109]==paper_color(72)); // no old outer source ring
    touch_down(102,104);hold(290,104);image=frozen();
    CHECK(has(image.top,"release to connect")&&has(image.bottom,"+50%"));
    CHECK(p1.src==ModSource::None&&mutations==0); // preview never commits
    CHECK(image.bottom.pixels[117*320+156]==m::spot()); // actual bowed middle, not a straight line
    hold(170,150);const auto first=frozen().bottom;
    for(int i=0;i<5;++i){host_time_ms+=16;image=frozen();}
    CHECK(first.pixels!=image.bottom.pixels&&p1.src==ModSource::None&&mutations==0);
    // Boundary positions never draw into chrome/outside the display; long pauses reset.
    for(const auto xy:{std::pair<int,int>{0,0},{319,239},{102,104},{210,72}}){
        hold(xy.first,xy.second);host_time_ms+=250;frozen();
    }
    image=frozen();CHECK(has(image.top,"input occupied"));lift();
    CHECK(p0.src==ModSource::SigilH&&!p0.mod_enabled&&mutations==0);
    touch_down(102,104);hold(210,104);frozen();patchbay_cancel_input();lift();
    CHECK(p1.src==ModSource::None&&mutations==0&&!has(frozen().top,"release to connect"));
    // Reconnect-to-source preview anchors at the existing right input, preserves off/zero.
    touch_down(210,72);hold(102,104);image=frozen();CHECK(has(image.top,"release to change source"));
    CHECK(p0.src==ModSource::SigilH&&mutations==0);lift();
    CHECK(p0.src==ModSource::SigilF&&p0.mod_depth==0&&!p0.mod_enabled&&mutations==1);
    image=frozen();CHECK(has(image.top,"live")&&has(image.bottom,"off / y"));
    for(const auto& t:image.top.raw_texts)CHECK(t.y<70||t.y>104); // hero uses native Gothic masks
    // Small fallback is separately baked, fits its column, never enlarged bitmap text.
    namespace gt=seance::platform::ui3ds::sigilbay_type;
    CHECK(gt::width("comp / threshold")>0&&gt::width("comp / threshold",true)<gt::width("comp / threshold"));
    const auto letters=render(400,[]{m::paper(400);gt::draw(20,96,"a / vca / gain",360,m::ink());
        CHECK(gt::draw(20,150,"a very long parameter / longest_param",150,m::ink())<=170);});
    CHECK(letters.raw_texts.empty());paper_edges(letters);
    // Existing one-tap name -> editor and full-row drop hit areas are unchanged.
    tap(30,104);CHECK(g_view==View::Sigils&&g_sel_sigil==5);
    std::puts("ok 7: minimal/occupied ports, annotated inertial wire, hover/cancel/source-swap, Gothic fit and direct editor entry");
}
void both_palette_frozen(){
    for(auto theme:{theme_test::p::Theme::Pink,theme_test::p::Theme::Graphite}){
        theme_test::p::set(theme);Bay f;auto*u=f.add();u->params[0].src=ModSource::SigilH;u->params[0].mod_depth=-32767;u->params[0].mod_enabled=false;
        tap(250,72);frozen();touch_down(210,72);hold(170,170);frozen(false);patchbay_cancel_input();
        g_view=View::Sigils;g_sel_sigil=7;sigils_forget_undo();sig_drag(20,111,300,73);
        for(int shape_index=0;shape_index<7;++shape_index){shape(shape_index);frozen_sigil();}
        shape(0);const auto clear=frozen_sigil();
        sig_tap(158,24);auto menu=frozen_sigil();CHECK(has(menu.bottom,"sigil h")&&!has(menu.bottom,"shape"));CHECK(menu.bottom.pixels!=clear.bottom.pixels);
        sig_key(KEY_B);sig_tap(80,47);menu=frozen_sigil();CHECK(has(menu.bottom,"smooth")&&has(menu.bottom,"random"));
        sig_key(KEY_B);sig_tap(265,47);frozen_sigil();sig_key(KEY_B);
        sig_tap(40,190);frozen_sigil();sig_key(KEY_B);sig_tap(209,190);frozen_sigil();sig_key(KEY_B);sig_tap(85,211);frozen_sigil();sig_key(KEY_B);
        // All nine clock choices, both fields: UI ordering skips persisted ENV=5.
        NavChoice out=NavChoice::None;
        for(int field=0;field<2;++field){nav_close();nav_open();nav_buttons(KEY_X,out);if(field)nav_buttons(KEY_X,out);nav_buttons(KEY_Y,out);
            for(int i=0;i<=8;++i){nav_buttons(0,out);const int x=i?126+(i-1)*25:99;nav_touch_down(x,143,out);nav_touch_release();CHECK(f.engine->clock_mod(field?ClockTarget::Swing:ClockTarget::Tempo).src==(i?sigil_mod_source(i-1):ModSource::None));}
            nav_capture();RestoreGlobals restore;g_eng=nullptr;g_audio=nullptr;auto clock=render(320,nav_draw);CHECK(has(clock,"h")&&has(clock,"none"));render(400,nav_clock_draw_top);
        }
        nav_close();
    }
    std::puts("ok 6: real native dual-screen render in both themes, blur disclosures/anchors and frozen draw without engine/audio");
}
}
int main(){
    try{
        theme_test::p::set(theme_test::p::Theme::Pink);using namespace patchbay_test;
        if(std::getenv("SEANCE_UI_SIGILS_ONLY")){
            current_case="universal editor";universal_editor();current_case="sigil motion";sigil_motion();current_case="sigil details";sigil_details();
            std::printf("sigils-only UI: 3/3 passed; no exports/device/FPS claim\n");return 0;
        }
        current_case="navigation/main";navigation_and_main();current_case="cables";cables();current_case="all targets";all_targets();
        current_case="universal editor";universal_editor();current_case="lifecycle";sigils_env_and_lifecycle();current_case="frozen palettes";both_palette_frozen();
        current_case="sigilbay polish";sigilbay_polish();current_case="sigil motion";sigil_motion();current_case="sigil details";sigil_details();
        std::printf("universal sigils UI: 9/9 passed; %d frozen dual pairs; no exports/device/FPS claim\n",pairs);return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"universal sigils UI FAIL (%s): %s\n",patchbay_test::current_case,e.what());return 1;}
}
