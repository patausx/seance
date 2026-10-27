// Focused COMP checks through exact main touch routing, real engine and frozen UI.
#include "../platform/3ds/sequencer_lock_format.h"
namespace {
struct CompFixture : ModuleInspectorFixture {
    CompFixture():ModuleInspectorFixture(UnitKind::Comp) {
        CompUnit defaults;
        for(int p=0;p<CompUnit::P_COUNT;++p){module->params[p]=defaults.params[p];module->params[p].snap();}
        comp_focus_release();comp_focus_select(0);unit().control_tick();feed(6400);capture();
    }
    CompUnit& unit(){return *static_cast<CompUnit*>(module);}
    void feed(int frames) {
        q15 b[64];
        while(frames>0){const int n=std::min(frames,64);std::fill(b,b+n,q15(18000));unit().process(b,n);frames-=n;}
    }
    static std::pair<int,int> point(int p) {
        if(!p)return {180,97};
        if(p<6)return {(p&1)?100:250,143+((p-1)/2)*30};
        return {220,80+(p-6)*48};
    }
};
void test_comp_controls_mod() {
    CompFixture f;auto& u=f.unit();
    for(int p=0;p<CompUnit::P_CLOCK;++p) {
        comp_focus_select(p);u.params[p].base=15000;u.params[p].snap();f.capture();
        const auto point=CompFixture::point(p);const int dirty=mutations;
        input::down(point.first,point.second);CHECK(comp_focus_selected()==p && g_drag==Drag::CompParam);
        input::move(point.first,point.second);CHECK(u.params[p].base==15000 && mutations==dirty);
        input::move(point.first+10,point.second);input::up();CHECK(u.params[p].base>15000);
    }
    comp_focus_select(CompUnit::P_CLOCK);f.capture();input::down(220,176);input::up();CHECK(u.params[CompUnit::P_CLOCK].base==32767);
    comp_focus_select(CompUnit::P_RELEASE);u.params[CompUnit::P_RELEASE].base=CompUnit::release_value(2);f.capture();
    input::down(100,173);input::move(112,173);input::up();CHECK(CompUnit::release_index(u.params[CompUnit::P_RELEASE].base)==3);
    CHECK(!u.bypass);input::down(287,231);input::up();CHECK(u.bypass && !param_lens_is_open());
    input::down(287,231);input::up();CHECK(!u.bypass);
    input::down(205,190);input::up();f.capture();CHECK(comp_focus_selected()==CompUnit::P_KNEE);
    CHECK(editor_back());f.capture();CHECK(comp_focus_selected()==CompUnit::P_THRESHOLD);
    // Existing held-L route assignment, signed depth and relative base ownership.
    input::frame(KEY_L,KEY_L);CHECK(param_lens_is_open());f.capture();
    param_lens_touch_down(76,186);param_lens_touch_release();
    CHECK(u.params[0].src==ModSource::SigilA);
    param_lens_touch_down(200,158);param_lens_touch_drag(150,158);param_lens_touch_release();
    CHECK(u.params[0].mod_depth<0);
    const q15 saved=u.params[0].base;
    input::frame(0,0,KEY_L);input::frame(0,0);CHECK(!param_lens_is_open());
    for(int s=0;s<4;++s){u.params[s].src=ModSource(int(ModSource::SigilA)+s);u.params[s].mod_depth=6000;}
    q15 stereo[128];for(int i=0;i<32;++i)f.engine->render(stereo,64);f.capture();
    for(int s=0;s<4;++s) {
        ParamInspector d;f.engine->capture_param_inspector(0,0,s,d);
        CHECK(d.source==ModSource(int(ModSource::SigilA)+s) && d.range_min<d.range_max);
    }
    CHECK(u.params[0].base==saved);
    input::down(180,97);g_overlay=Overlay::None;f.capture();g_overlay=Overlay::Editor;
    input::move(220,97);input::up();CHECK(u.params[0].base==saved);
    // Armed lock edits do not mutate bases or bypass and cannot enter mod panel.
    g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=5;
    comp_focus_select(0);f.capture();CHECK(f.engine->set_step_lock(0,5,0,0,12000));
    input::down(180,97);input::move(200,97);input::up();CHECK(u.params[0].base==saved && seq_display_value(0,0,saved)>12000);
    input::down(18,43);input::up();CHECK(seq_display_value(0,0,saved)==saved);
    input::down(287,231);input::up();CHECK(!u.bypass);
    input::frame(KEY_L,KEY_L);CHECK(!param_lens_is_open());input::frame(0,0,KEY_L);
    const int dirty=mutations;g_seq_lock_chain=1;input::down(180,97);input::move(200,97);input::up();CHECK(mutations==dirty);
    g_seq_lock_armed=false;g_view=View::Chains;g_overlay=Overlay::Picker;
    picker_open();picker_buttons(KEY_UP);picker_buttons(KEY_UP);picker_buttons(KEY_UP);picker_buttons(KEY_A); // mouth and arp follow comp
    f.module=f.engine->chains[0].units[0];CHECK(f.module && f.module->kind==UnitKind::Comp && f.module->num_params==9);
}
void test_comp_stored_release() {
    using C=CompUnit;
    char out[40];seq_lock::format_value(UnitKind::Comp,0,C::P_RELEASE,16000,out,sizeof out);
    CHECK(std::string(out).find("f")!=std::string::npos && std::string(out).find(" s")!=std::string::npos);
    CHECK(!seq_lock::supports_offset(UnitKind::Comp,0,C::P_CLOCK));
    for(int i=0;i<C::RELEASE_DIVISIONS;++i) {
        const q15 v=C::release_value(i);
        CHECK(C::release_index(seq_lock::comp_release_nudge(v,1,true))==std::min(i+1,C::RELEASE_DIVISIONS-1));
        CHECK(C::release_index(seq_lock::comp_release_nudge(v,-1,true))==std::max(0,i-1));
    }
    Fixture f;auto* u=new C;delete f.engine->replace_unit(0,0,u);
    u->params[C::P_CLOCK].base=32767;u->params[C::P_RELEASE].base=C::release_value(2);
    f.mode(6);for(int i=0;i<3;++i)f.tap(KEY_A);
    sequencer_capture_inspector();CHECK(sequencer_inspector().param==C::P_RELEASE);
    const auto key=[&](u32 k){f.frame(KEY_L|k,KEY_L|k);f.frame(0,0,KEY_L|k);f.frame();};
    key(KEY_DUP);sequencer_capture_inspector();
    CHECK(C::release_index(sequencer_inspector().rows[C::P_RELEASE].focused)==3);
    CHECK(C::release_index(u->params[C::P_RELEASE].base)==2);
    key(KEY_Y);sequencer_capture_inspector();CHECK(!sequencer_inspector().rows[C::P_RELEASE].focused_locked);
    // Mixed stored clock refuses a SET nudge atomically, regardless of sounding mode.
    CHECK(f.engine->set_step_lock(0,1,0,C::P_CLOCK,0));
    key(KEY_DRIGHT);sequencer_capture_inspector();CHECK(sequencer_inspector().selected_count==2);
    const auto before=f.hash();key(KEY_DUP);CHECK(f.hash()==before);
    sequencer_capture_inspector();CHECK(std::string(sequencer_inspector().status).find("mixed clocks")!=std::string::npos);
    // Free-only nudge remains continuous; no forced musical buckets.
    CHECK(seq_lock::comp_release_nudge(10000,1,false)==10328);
}
void test_comp_frozen_surface() {
    CompFixture f;auto& u=f.unit();
    for(int p=0;p<CompUnit::P_COUNT;++p) {
        auto& v=u.params[p];v.src=ModSource(int(ModSource::SigilA)+(p%4));v.mod_depth=p%2?-8000:6000;
        v.resolve(12000);
    }
    u.control_tick();f.feed(68000);
    CHECK(u.visual().history_count==64 && u.visual().gr_db>0);
    for(auto theme:{palette::Theme::Pink,palette::Theme::Graphite}) {
        palette::set(theme);
        for(bool detail:{false,true}) {
            comp_focus_select(detail?CompUnit::P_HP:CompUnit::P_THRESHOLD);f.capture();
            const auto top=cleanup_draw(true);const int count=objects;CHECK(has_text("gain reduction"));
            const auto lower=cleanup_draw(false);cleanup_pair_budget(count);
            CHECK(has_text(detail?"detector hp":"threshold"));
            {ModuleInspectorNoEngine frozen;
                module_inspector_surface(false);comp_focus_draw(u);cleanup_geometry();CHECK(pixels==lower);
                module_inspector_surface(true);CHECK(comp_focus_draw_top());cleanup_geometry();CHECK(pixels==top);
            }
        }
    }
    comp_focus_select(CompUnit::P_THRESHOLD);f.capture();input::frame(KEY_L,KEY_L);f.capture();
    cleanup_draw(true);const int count=objects;cleanup_draw(false);cleanup_pair_budget(count);
    input::frame(0,0,KEY_L);input::frame(0,0);theme_test::select();
}
int run_comp_ui() {
    int failed=0;
    const auto run=[&](const char* name,auto test){try{test();std::printf("ok: %s\n",name);}catch(const std::exception& e){++failed;std::printf("FAIL: %s: %s\n",name,e.what());}};
    run("comp main controls / locks / sigil modulation / ownership / picker",test_comp_controls_mod);
    run("comp stored release formatting / sync detents / mixed group safety",test_comp_stored_release);
    run("comp real telemetry / frozen dual screens / detail / palettes / lens",test_comp_frozen_surface);
    std::printf("comp ui: %d/3 passed, host shared peak %d/8192 (not hardware fps)\n",3-failed,maximum_shared_objects);
    return failed?1:0;
}
} // namespace
