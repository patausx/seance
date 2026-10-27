// Reuse existing engine, host audio boundary, raster and OSC fixtures. Unlike the
// direct module tests, this links the real ui_editor.cpp and ui_module_focus.cpp.
#include "../platform/3ds/ui_editor.h"
#include "../platform/3ds/ui_patchbay.h"
#include "../platform/3ds/small_button_motion.h"
#define main existing_sequencer_test_main
#include "sequencer_test.cpp"
#undef main

namespace seance::platform::ui3ds {
// Standalone picker boundary: this harness tests module replacement, not the
// main-column blurred background or spatial insertion (main_columns_test does).
bool chains_picker_background() { return false; }
void mark_project_structural_mutated() { mark_project_mutated(); }
void workspace_move_slot(int,int,int) { CHECK(false); }
}

namespace osc_input_main {
// Only HID coordinates and unrelated main-owned screens are shimmed. The exact
// dispatcher, editor facade, module controller, captures and params are real.
struct touchPosition { int px=0,py=0; } touch;
void hidTouchRead(touchPosition* out) { *out=touch; }
void bounce_touch_down(int,int) { CHECK(false); }
void bounce_touch_hold(int,int) { CHECK(false); }
void bounce_touch_release() { CHECK(false); }
void chains_view_touch_release(bool) {}
void picker_touch_release() { CHECK(false); }
void sample_browser_touch_release(bool) { CHECK(false); }
void chains_view_touch_down(int,int) { CHECK(false); }
void picker_touch_down(int,int) { CHECK(false); }
void sample_browser_touch_down(int,int) { CHECK(false); }
void patch_browser_touch_down(int,int) { CHECK(false); }
void mixer_touch_down(int,int) { CHECK(false); }
void confirm_discard_touch_down(int,int) { CHECK(false); }
void sigil_tools_touch_down(int,int) { CHECK(false); }
void chains_view_touch_hold(int,int) { CHECK(false); }
void picker_touch_hold(int,int) { CHECK(false); }
void sample_browser_touch_hold(int,int) { CHECK(false); }
void project_overlay_touch_down(int,int) { CHECK(false); }
void project_overlay_touch_hold(int,int) { CHECK(false); }
void project_overlay_touch_release(bool) { CHECK(false); }
void mixer_set_value(int) { CHECK(false); }
#include "main_touch.inc"
void down(int x,int y) { touch={x,y};handle_touch(KEY_TOUCH,KEY_TOUCH); }
void move(int x,int y) { touch={x,y};handle_touch(0,KEY_TOUCH); }
void up() { handle_touch(0,0); }
// Real main key ownership, old L+arrow branch and final touch gate. Only the
// unrelated menu/picker boundaries are shimmed, as above for touch.
bool nav_opened=false;
bool nav_is_open() { return nav_opened; }
void picker_open() {}
void sample_browser_load_selected() { CHECK(false); }
void sample_browser_assign_selected_slot() { CHECK(false); }
void trigger_frame(u32 down,u32 held) {
    auto& audio=*g_audio;
    const bool stage_modal_frame=false;
#include "main_trigger.inc"
}
struct KeyResult { u32 remaining; bool skip, owned; };
KeyResult frame(u32 down,u32 held,u32 up=0) {
    const bool stage_modal_frame=false,stage_skip_touch=false;
    const bool nav_had_frame=nav_is_open();
    const bool discard_modal_had_frame=g_overlay==Overlay::ConfirmDiscard;
#include "main_lens_keys.inc"
#include "main_lens_nav.inc"
#include "main_lens_touch.inc"
    return {down,param_skip_touch,param_owned_frame};
}
}

namespace {
namespace input=osc_input_main;
void input_control(int p,bool lock) {
    OscFixture f; auto& u=*f.osc;
    for(auto& param:u.params) { param.base=10000;param.snap(); }
    if(lock) {
        g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;
        CHECK(f.engine->set_step_lock(0,7,0,p,10000));
    }
    auto before=f.bases();const auto point=OscFixture::point(p);
    input::down(point.first,point.second);
    CHECK(g_drag==Drag::OscShape && osc_focus_selected()==p && f.bases()==before);
    f.capture();CHECK(g_drag==Drag::OscShape);
    input::move(point.first,point.second);CHECK(f.bases()==before);
    const int extent=p==OscUnit::P_SHAPE?278:84;
    for(int dx:{7,14,-7}) {
        input::move(point.first+dx,point.second);f.capture();
        const q15 expected=q15(10000+dx*32767/extent);
        if(lock) CHECK(seq_display_value(0,p,before[p])==expected && f.bases()==before);
        else { auto after=before;after[p]=expected;CHECK(f.bases()==after); }
    }
    input::up();CHECK(g_drag==Drag::None);
    const auto released=f.bases();const q15 stored=seq_display_value(0,p,released[p]);
    input::move(point.first+20,point.second);
    CHECK(f.bases()==released && seq_display_value(0,p,released[p])==stored);
    if(lock) {
        const int i=OscFixture::index(p);
        input::down(p==OscUnit::P_SHAPE?16:16+(i%3)*104,p==OscUnit::P_SHAPE?132:168+(i/3)*31);
        input::up();CHECK(seq_display_value(0,p,before[p])==before[p] && f.bases()==before);
    }
}
void input_fm_control(int p,bool lock){
    FmMenuFixture f;auto& u=f.fm;fm_focus_select(p);
    static const int x[]={50,154,50,154,154,258,50,50,258,154,258,258};
    static const int y[]={155,155,195,195,155,195,195,155,155,195,155,195};
    static const int extent[]={128,128,84,84,128,84,84,128,336,84,168,128};
    auto& v=u.params[p];v.base=v.bipolar?-5000:10000;v.snap();const q15 base=v.base;
    if(lock){g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=5;g_view=View::Sequencer;CHECK(f.engine->set_step_lock(0,5,0,p,12000));}
    input::down(x[p],y[p]);CHECK(fm_focus_selected()==p&&v.base==base);f.capture();
    CHECK(g_drag!=Drag::None);input::move(x[p]+7,y[p]);f.capture();const q15 expected=q15((lock?12000:base)+7*32767/extent[p]);
    CHECK(lock?(v.base==base&&seq_display_value(0,p,base)==expected):v.base==expected);
    input::up();CHECK(g_drag==Drag::None);input::move(x[p]+14,y[p]);CHECK(lock?v.base==base:v.base==expected);
}
void input_fm_policies(){
    FmMenuFixture f;auto& u=f.fm;
    input::down(210,10);input::up();CHECK(!u.bypass); // old common header must not steal passive amp meter
    input::down(258,10);input::up();CHECK(u.bypass);input::down(258,10);input::up();CHECK(!u.bypass);
    input::down(250,35);input::up();input::down(204,81);input::up();CHECK(u.carrier_wave==FmUnit::Wave::Saw);
    input::down(274,130);input::up();CHECK(u.mod_wave==FmUnit::Wave::Pulse);
    for(int p:{10,11}){
        fm_focus_select(p);input::frame(0,0);auto enter=input::frame(KEY_L,KEY_L);CHECK(enter.skip&&enter.owned&&param_lens_is_open());
        input::up();input::down(250,129);input::move(244,129);input::up();const q15 value=u.params[p].base;
        input::frame(0,0,KEY_L);CHECK(!param_lens_is_open()&&u.params[p].base==value&&!f.engine->param_temp_active());input::frame(0,0);
    }
    g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=5;g_view=View::Sequencer;
    input::down(250,35);input::up();const int dirty=mutations;input::down(84,81);input::up();input::down(258,10);input::up();
    CHECK(u.carrier_wave==FmUnit::Wave::Saw&&!u.bypass&&dirty==mutations);
}
void input_hit_grid() {
    OscFixture f;const auto before=f.bases();
    for(int p=0;p<OscUnit::P_COUNT;++p) {
        const int i=OscFixture::index(p);
        const int x=p==OscUnit::P_SHAPE?8:8+(i%3)*104;
        const int y=p==OscUnit::P_SHAPE?128:163+(i/3)*31;
        const int w=p==OscUnit::P_SHAPE?296:96,h=p==OscUnit::P_SHAPE?34:30;
        for(int dx:{0,w/2,w-1})for(int dy:{0,h/2,h-1}) {
            input::down(x+dx,y+dy);CHECK(g_drag==Drag::OscShape&&osc_focus_selected()==p);
            input::up();CHECK(f.bases()==before);
        }
    }
    for(auto point:{std::pair<int,int>{106,184},{210,215},{150,162},{150,193},{160,125}}) {
        input::down(point.first,point.second);CHECK(g_drag==Drag::None);input::up();CHECK(f.bases()==before);
    }
}
void input_header() {
    OscFixture f;auto& u=*f.osc;const int dirty=mutations;
    // Retired shell targets must not steal the new header (including del text).
    for(int x:{199,210,225,260,280,286,293}) {input::down(x,9);input::up();CHECK(!u.bypass&&!u.pitch_quantize&&mutations==dirty);}
    for(int x:{226,240,259}) {input::down(x,9);input::up();CHECK(u.bypass);input::down(x,9);input::up();CHECK(!u.bypass);}
    for(int x:{294,305,319}) {input::down(x,9);input::up();CHECK(u.pitch_quantize);input::down(x,9);input::up();CHECK(!u.pitch_quantize);}
    g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;
    const int saved=mutations;for(int y:{9,22,24})for(int x:{230,305}) {input::down(x,y);input::up();CHECK(!u.bypass&&!u.pitch_quantize&&mutations==saved);}
}
void input_held_mod(int p) {
    OscFixture f;g_sel_chain=g_edit_chain=0;g_sel_slot=g_edit_slot=0;
    auto& u=*f.osc;const auto point=OscFixture::point(p);
    input::down(point.first,point.second);input::up();
    const auto base=f.bases();const int dirty=mutations;
    auto keys=input::frame(KEY_L|KEY_X|KEY_A,KEY_L);
    CHECK(keys.owned&&keys.skip&&keys.remaining==0&&param_lens_is_open());
    CHECK(osc_focus_selected()==p&&f.bases()==base&&mutations==dirty&&!f.engine->param_temp_active());
    input::touch={130,185};input::frame(KEY_TOUCH,KEY_L|KEY_TOUCH);
    input::frame(0,KEY_L);CHECK(u.params[p].src==ModSource::SigilB);
    input::touch={170,166};input::frame(KEY_TOUCH,KEY_L|KEY_TOUCH);
    input::touch={150,166};input::frame(0,KEY_L|KEY_TOUCH);
    const q15 depth=-20*65534/271;
    CHECK(u.params[p].mod_depth==depth&&f.bases()==base);
    keys=input::frame(KEY_X|KEY_A|KEY_R,KEY_TOUCH,KEY_L);
    CHECK(!keys.owned&&keys.skip&&keys.remaining==0&&!param_lens_is_open());
    input::touch={point.first+14,point.second};
    for(int i=0;i<3;++i)CHECK(input::frame(i?0u:u32(KEY_TOUCH),KEY_TOUCH).skip);
    CHECK(f.bases()==base&&u.params[p].mod_depth==depth&&!f.engine->param_temp_active());
    input::frame(0,0,KEY_TOUCH);
    input::frame(KEY_L,KEY_L);CHECK(param_lens_is_open());
    input::frame(KEY_B|KEY_X,KEY_L);CHECK(!param_lens_is_open());
    input::frame(0,KEY_L);CHECK(!param_lens_is_open()); // no reopen until a fresh edge
    input::frame(0,0,KEY_L);
}
void input_mod_quarantine() {
    for(bool same_edge:{false,true}) {
        OscFixture f;const auto base=f.bases();
        input::touch={100,148};if(!same_edge)input::frame(KEY_TOUCH,KEY_TOUCH);
        input::frame(KEY_L|(same_edge?u32(KEY_TOUCH):0u),KEY_L|KEY_TOUCH);
        input::touch={295,129};input::frame(0,KEY_L|KEY_TOUCH);
        CHECK(param_lens_is_open()&&f.bases()==base&&g_drag==Drag::None);
        input::frame(0,KEY_L,KEY_TOUCH);
        input::touch={200,129};input::frame(KEY_TOUCH,KEY_L|KEY_TOUCH);
        input::touch={207,129};input::frame(0,KEY_L|KEY_TOUCH);
        CHECK(f.osc->params[OscUnit::P_SHAPE].base==base[OscUnit::P_SHAPE]+7*32767/271);
        const auto edited=f.bases();input::frame(0,KEY_TOUCH,KEY_L);
        input::touch={170,148};input::frame(0,KEY_TOUCH);CHECK(f.bases()==edited);
        input::frame(0,0,KEY_TOUCH);
    }
}
void input_mod_navigation() {
    for(u32 direction:{u32(KEY_UP),u32(KEY_DOWN),u32(KEY_LEFT),u32(KEY_RIGHT)})for(bool simultaneous:{false,true}) {
        OscFixture f;g_sel_chain=g_edit_chain=0;g_sel_slot=g_edit_slot=0;
        const int chain=direction==KEY_UP?NUM_CHAINS-1:direction==KEY_DOWN?1:0;
        const int slot=direction==KEY_LEFT?UNITS_PER_CHAIN-1:direction==KEY_RIGHT?1:0;
        delete f.engine->replace_unit(chain,slot,new OscUnit);
        if(!simultaneous)CHECK(input::frame(KEY_L,KEY_L).owned);
        const auto keys=input::frame(direction|(simultaneous?u32(KEY_L):0u),KEY_L|direction);
        CHECK(!keys.owned&&!param_lens_is_open()&&g_sel_chain==chain&&g_edit_chain==chain&&g_sel_slot==slot&&g_edit_slot==slot);
        input::frame(0,KEY_L);CHECK(!param_lens_is_open());
        input::frame(0,0,KEY_L);
        CHECK(input::frame(KEY_L,KEY_L).owned);input::frame(0,0,KEY_L);
    }
    for(u32 action:{u32(KEY_SELECT),u32(KEY_START),u32(KEY_Y)}) {
        OscFixture f;input::frame(KEY_L,KEY_L);
        const auto result=input::frame(action|KEY_X|KEY_A|KEY_TOUCH,KEY_L|KEY_TOUCH);
        CHECK(!param_lens_is_open()&&result.skip&&result.remaining==action);
        input::frame(0,0);
    }
}
void input_mod_contexts() {
    for(int reason=0;reason<7;++reason) {
        OscFixture f;
        if(reason==0)g_view=View::Performance;
        if(reason==1)g_view=View::Sigils;
        if(reason==2)g_view=View::Sequencer;
        if(reason==3)g_overlay=Overlay::None;
        if(reason==4)g_seq_lock_armed=true;
        if(reason==5)g_overlay=Overlay::ConfirmDiscard;
        if(reason==6)input::nav_opened=true;
        input::frame(KEY_L,KEY_L);CHECK(!param_lens_is_open());
        input::nav_opened=false;
    }
    OscFixture f;input::frame(KEY_L,KEY_L);
    delete f.engine->replace_unit(0,0,new FilterUnit);
    const auto result=input::frame(KEY_TOUCH,KEY_L|KEY_TOUCH);
    CHECK(result.skip&&!param_lens_is_open());input::frame(0,0);
}
void input_ownership_lens() {
    OscFixture f;auto& u=*f.osc;u.params[OscUnit::P_PITCH].base=10000;
    auto point=OscFixture::point(OscUnit::P_PITCH);
    input::down(point.first,point.second);g_overlay=Overlay::None;f.capture();
    g_overlay=Overlay::Editor;input::move(point.first+7,point.second);CHECK(u.params[OscUnit::P_PITCH].base==10000);input::up();
    input::down(point.first,point.second);input::move(point.first+7,point.second);input::up();
    CHECK(u.params[OscUnit::P_PITCH].base==10000+7*32767/84);
    input::down(250,232);input::up();CHECK(param_lens_is_open()&&osc_focus_selected()==OscUnit::P_PITCH);
    const q15 saved=u.params[OscUnit::P_PITCH].base;
    input::down(200,129);input::move(207,129);input::up();CHECK(u.params[OscUnit::P_PITCH].base==saved+7*32767/271);
    param_lens_cancel();input::move(point.first+14,point.second);CHECK(u.params[OscUnit::P_PITCH].base==saved+7*32767/271);
    // Legacy conversion must still begin at the stored anchor through the facade.
    u.shape_morph=false;u.wave=OscUnit::Wave::Saw;u.params[OscUnit::P_SHAPE].base=31337;
    input::down(100,148);CHECK(u.shape_morph&&u.params[OscUnit::P_SHAPE].base==21845);
    f.capture();input::move(107,148);CHECK(u.params[OscUnit::P_SHAPE].base==21845+7*32767/278);input::up();
}
}

#include "legacy_cleanup_test.h"
#include "shift_ui_test.h"
#include "bird_ui_test.h"
#include "fract_ui_test.h"
#include "mouth_ui_test.h"
#include "arp_ui_test.h"
#include "glitch_ui_test.h"
#include "inst_ui_test.h"
#include "reverb_dense_ui_test.h"
#include "comp_ui_test.h"
#include "osc_clean_ui_test.h"
#include "gran_clean_ui_test.h"
#include "small_button_visual_test.h"
#include "module_audit_test.h"
#include "fader_lancet_ui_test.h"
#ifdef SEANCE_GRAPHITE_UI
#include "graphite_ui_test.h"
#endif

int main(int argc,char** argv) {
    theme_test::select();
#ifdef SEANCE_GRAPHITE_UI
    if(argc>1 && std::string(argv[1])=="--dump-graphite-extra") return graphite_extra(argc,argv);
#endif
    if(argc==2 && std::string(argv[1])=="--fader-lancet-only") return fader_lancet_cases::run();
    if(argc==3 && std::string(argv[1])=="--capture-fader-lancet") return fader_lancet_cases::capture(argv[2]);
    if(argc>1 && std::string(argv[1])=="--module-audit-only") return run_module_audit();
    if(argc>1 && std::string(argv[1])=="--small-buttons-only") return run_small_button_visual();
    if(argc>1 && std::string(argv[1])=="--gran-clean-only") return run_gran_clean_ui();
    if(argc>1 && std::string(argv[1])=="--osc-clean-only") return run_osc_clean_ui();
    if(argc>1 && std::string(argv[1])=="--rev-dense-only") return run_rev_dense_ui();
    if(argc==3 && std::string(argv[1])=="--capture-rev-dense") {dump_rev_visual(argv[2]);return 0;}
    if(argc>1 && std::string(argv[1])=="--inst-only") return run_inst_ui();
    if(argc>1 && std::string(argv[1])=="--glitch-only") return run_glitch_ui();
    if(argc>1 && std::string(argv[1])=="--arp-only") return run_arp_ui();
    if(argc>1 && std::string(argv[1])=="--mouth-only") return run_mouth_ui();
    if(argc>1 && std::string(argv[1])=="--comp-only") return run_comp_ui();
    if(argc>1 && std::string(argv[1])=="--fract-only") return run_fract_ui();
    if(argc>1 && (std::string(argv[1])=="--bird-only" || std::string(argv[1])=="--dump-bird"))
        return run_bird_ui(argc,argv);
    if(argc>1 && (std::string(argv[1])=="--shift-only" || std::string(argv[1])=="--dump-shift"))
        return run_shift_ui(argc,argv);
    if(argc>1 && (std::string(argv[1])=="--legacy-cleanup-only" || std::string(argv[1])=="--dump-legacy-cleanup"))
        return run_cleanup(argc,argv);
    const bool mod_only=argc>1&&std::string(argv[1])=="--mod-shortcut-only";
    const bool fm_only=argc>1&&std::string(argv[1])=="--fm-input-only";
    if(argc>1&&!mod_only&&!fm_only)return existing_sequencer_test_main(argc,argv); // reuse binary, no second build
    int failed=0,total=0;
    const auto run=[&](const std::string& name,const std::function<void()>& test) {
        ++total;try {test();std::printf("ok: %s\n",name.c_str());}
        catch(const std::exception& e) {++failed;std::printf("FAIL: %s: %s\n",name.c_str(),e.what());}
    };
    if(fm_only){
        for(bool lock:{false,true})for(int p=0;p<12;++p)run(std::string(lock?"fm main lock ":"fm main base ")+std::to_string(p),[=]{input_fm_control(p,lock);});
        run("fm main policies / header / operators / held mod",input_fm_policies);
        std::printf("fm input: %d tests, %d failed\n",total,failed);return failed?1:0;
    }
    if(mod_only) {
        for(int p=0;p<OscUnit::P_COUNT;++p)run("held mod / actual main / selected "+std::to_string(p),[=]{input_held_mod(p);});
        run("held mod / same-frame + pre-existing touch quarantine",input_mod_quarantine);
        run("held mod / old L+arrows + global chords",input_mod_navigation);
        run("held mod / excluded views + locks + identity",input_mod_contexts);
        run("shared lens / controls + no temporary audition",test_param_lens_controls);
        run("shared lens / all modules / frozen bounds",test_param_lens_draw);
        // Only the affected lens cases, not neighboring DSP/model/control suites.
        run("fm / selected mod",test_fm_menu_lens_temp);
        run("ghost / selected mod",test_ghost_menu_lens_temp);
        run("strn / selected mod",test_strn_lens_routing);
        run("rev / locks + selected mod",test_rev_locks_lens);
        run("rev / mod frame",test_rev_menu_lens_draw);
        run("vca / locks + selected mod",test_vca_flexible_locks_lens);
        run("play / selected mod + preview ownership",test_play_ui_preview_lens);
        run("gran / locks + selected mod",test_gran_locks_lens);
        run("osc / locks + selected mod",test_osc_locks_lens);
        run("strain / selected mod",test_strain_lens);
        run("ring / selected mod",test_ring_lens);
        run("tilt / selected mod",test_tilt_lens);
        run("bit / selected mod",test_bit_lens);
        run("fold / selected mod",test_fold_lens_temp);
        run("filter / selected mod",test_filter_lens_temp);
        run("delay / selected mod",test_delay_lens_temp);
        std::printf("held mod: %d tests, %d failed; shared %d / 8192 objects\n",total,failed,maximum_shared_objects);
        return failed?1:0;
    }
    for(bool lock:{false,true})for(int p=0;p<OscUnit::P_COUNT;++p)
        run(std::string(lock?"osc main p-lock ":"osc main base ")+std::to_string(p),[=]{input_control(p,lock);});
    run("osc clear hit-grid edges / no overlap",input_hit_grid);
    run("osc clear inspector / semantic values / extrema",test_osc_clear_presentation);
    run("osc clear full-width model / exact extrema",test_osc_clear_wave);
    run("osc live faders / thin rails + drawable wave",test_osc_live_rails);
    run("osc upper gothic words / notes / frozen masks",test_osc_gothic_hero);
    run("osc main header ownership",input_header);
    run("osc main cancellation / lens / legacy conversion",input_ownership_lens);
    run("osc original relative controls / conversion",test_osc_controls_conversion);
    run("osc original locks / modulation / held return",test_osc_locks_lens);
    run("osc original frozen model / dual budget",test_osc_model_frames);
    std::printf("osc input: %d tests, %d failed; shared %d / 8192 objects\n",total,failed,maximum_shared_objects);
    const int clean_failed=run_osc_clean_ui();
    return failed||clean_failed?1:0;
}
