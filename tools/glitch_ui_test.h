// Narrow actual main -> editor -> glitch controller checks; no media export.
namespace {
using Glitch = GlitchUnit;
struct GlitchFixture : ModuleInspectorFixture {
    GlitchFixture():ModuleInspectorFixture(UnitKind::Glitch) {
        Glitch defaults;
        for(int p=0;p<Glitch::P_COUNT;++p) {module->params[p]=defaults.params[p];module->params[p].snap();}
        glitch_focus_release();glitch_focus_select(0);unit().reset();capture();
    }
    Glitch& unit() {return *static_cast<Glitch*>(module);}
    void settle() {for(int p=0;p<Glitch::P_COUNT;++p)module->params[p].snap();unit().control_tick();}
    std::array<q15,Glitch::P_COUNT> bases() const {
        std::array<q15,Glitch::P_COUNT> v{};for(int p=0;p<Glitch::P_COUNT;++p)v[p]=module->params[p].base;return v;
    }
    static std::pair<int,int> point(int p) {
        static const int order[]={0,2,3,4,5,6,1,7};const int i=order[p];
        return {65+156*(i%2),129+27*(i/2)};
    }
    void feed(int n) {
        q15 block[64];int at=0;
        while(n>0) {const int size=std::min(n,64);for(int i=0;i<size;++i)block[i]=q15(((at+i)%61-30)*800);
            unit().process(block,size);at+=size;n-=size;}
    }
    void start() {settle();unit().set_transport_clock(true,0,uint32_t((1ull<<32)/16000),0,16000);}
};
void test_glitch_controls() {
    GlitchFixture f;auto& u=f.unit();
    for(int p=0;p<Glitch::P_KEEP;++p) {
        u.params[p].base=14000;u.params[p].snap();const auto xy=GlitchFixture::point(p);
        const auto before=f.bases();const int dirty=mutations;
        input::down(xy.first,xy.second);CHECK(g_drag==Drag::GlitchParam && glitch_focus_selected()==p);
        input::move(xy.first,xy.second);CHECK(f.bases()==before && mutations==dirty);
        input::move(xy.first+12,xy.second);input::up();CHECK(u.params[p].base==14000+12*32767/192);
    }
    input::down(220,210);input::up();CHECK(u.params[Glitch::P_KEEP].base==32767);
    f.settle();const auto v=u.visual();CHECK(v.kept);
    input::down(220,18);input::up();CHECK(u.visual().revision!=v.revision); // stopped reroll is immediate
    f.start();const uint32_t rev=u.visual().revision;
    input::down(220,18);input::up();CHECK(u.visual().pending && u.visual().revision==rev);
    const auto before=f.bases();const int dirty=mutations;
    input::down(24,50);CHECK(g_drag==Drag::GlitchCell);input::move(24+37*6,82);input::up();
    CHECK(f.bases()==before && mutations==dirty); // figure touch is inspection, never destructive
    input::down(65,129);f.engine->active_pattern=1;input::move(89,129);input::up();CHECK(f.bases()==before);
    f.engine->active_pattern=0;
    input::down(65,129);g_overlay=Overlay::None;f.capture();g_overlay=Overlay::Editor;
    input::move(89,129);input::up();CHECK(f.bases()==before);
    g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=5;
    CHECK(f.engine->set_step_lock(0,5,0,Glitch::P_DENSITY,9000));
    input::down(65,129);input::move(77,129);input::up();
    CHECK(f.bases()==before && seq_display_value(0,Glitch::P_DENSITY,before[0])==9000+12*32767/192);
    input::down(16,114);input::up();CHECK(seq_display_value(0,Glitch::P_DENSITY,before[0])==before[0]);
    input::down(220,210);input::up();CHECK(u.params[Glitch::P_KEEP].base==32767 && seq_display_value(0,Glitch::P_KEEP,32767)==0);
    input::down(172,195);input::up();CHECK(seq_display_value(0,Glitch::P_KEEP,32767)==32767);
    const int lock_dirty=mutations;
    input::down(220,18);input::up();input::down(280,18);input::up();CHECK(mutations==lock_dirty && !u.bypass);
    g_seq_lock_chain=1;input::down(65,129);input::move(77,129);input::up();CHECK(mutations==lock_dirty);
}
void test_glitch_lens_picker() {
    GlitchFixture f;auto& u=f.unit();glitch_focus_select(Glitch::P_REVERSE);
    input::frame(KEY_L,KEY_L);CHECK(param_lens_is_open());f.capture();
    const q15 before=u.params[Glitch::P_REVERSE].base;
    input::down(190,129);input::move(190,129);CHECK(u.params[Glitch::P_REVERSE].base==before);
    input::move(202,129);input::up();CHECK(u.params[Glitch::P_REVERSE].base!=before);
    param_lens_touch_down(54,186);param_lens_touch_release();CHECK(u.params[Glitch::P_REVERSE].src==ModSource::SigilA);
    input::frame(0,0,KEY_L);input::frame(0,0);CHECK(!param_lens_is_open());
    char text[24];module_inspector_value_text(text,sizeof text,UnitKind::Glitch,Glitch::P_KEEP,32767);CHECK(std::string(text)=="on");
    CHECK(!seq_lock::supports_offset(UnitKind::Glitch,0,Glitch::P_KEEP));
    CHECK(seq_lock::supports_offset(UnitKind::Glitch,0,Glitch::P_JUMP));
    seq_lock::format_value(UnitKind::Glitch,0,Glitch::P_KEEP,32767,text,sizeof text);CHECK(std::string(text)=="keep");
    g_overlay=Overlay::Picker;picker_open();picker_buttons(KEY_UP);
    module_inspector_surface(false);picker_draw();cleanup_geometry();CHECK(has_text("glitch") && has_text("rhythmic rearranger"));
    picker_buttons(KEY_A);f.module=f.engine->chains[0].units[0];CHECK(f.module && f.module->kind==UnitKind::Glitch && f.module->num_params==8);
    CHECK(menu_style::heading_width("glitch")>ui::text_w("glitch"));
}
void test_glitch_frozen_map() {
    GlitchFixture f;auto& u=f.unit();
    u.params[Glitch::P_KEEP].base=32767;f.settle();
    auto pattern=u.pattern_state();
    for(int i=0;i<16;++i) {
        auto& c=pattern.cells[i];c={};c.op=Glitch::Op(i%6);c.source=uint8_t((15-i));
        c.repeats=c.op==Glitch::Op::Repeat?4:1;c.semitones=c.op==Glitch::Op::Pitch?-7:0;
    }
    CHECK(u.restore_pattern(pattern));f.start();f.feed(68000);CHECK(u.visual().ready && u.visual().running);
    for(auto theme:{palette::Theme::Pink,palette::Theme::Graphite}) {
        palette::set(theme);f.capture();
        const auto top=cleanup_draw(true);const int count=objects;
        CHECK(has_text("previous bar / source slices") && has_text("keep fixes the pattern, not the incoming sound"));
        const auto bottom=cleanup_draw(false);cleanup_pair_budget(count);CHECK(has_text("density") && has_text("reverse") && has_text("reroll"));
        const int dirty=mutations;glitch_focus_select(Glitch::P_PITCH);
        {ModuleInspectorNoEngine frozen;
            module_inspector_surface(false);glitch_focus_draw(u);cleanup_geometry();CHECK(pixels==bottom);
            module_inspector_surface(true);CHECK(glitch_focus_draw_top());cleanup_geometry();CHECK(pixels==top);
        }
        CHECK(mutations==dirty);glitch_focus_select(0);
        f.feed(600);f.capture();CHECK(cleanup_draw(true)!=top); // real phase ink moves without UI audio mutation
    }
    u.stop_transport();f.capture();cleanup_draw(true);CHECK(has_text("start transport / warm one complete bar"));
    u.bypass=true;f.capture();cleanup_draw(true);CHECK(has_text("bypassed / live input"));
    u.bypass=false;f.engine->chains[0].mute=true;f.capture();cleanup_draw(true);CHECK(has_text("chain muted"));
    theme_test::select();
}
int run_glitch_ui() {
    int failed=0;
    const auto run=[&](const char* name,auto test){try{test();std::printf("ok: %s\n",name);}catch(const std::exception& e){++failed;std::printf("FAIL: %s: %s\n",name,e.what());}};
    run("glitch actual touch / seven rails / keep-reroll / locks / ownership",test_glitch_controls);
    run("glitch selected mod lens / picker / discrete keep formatting",test_glitch_lens_picker);
    run("glitch real slice map / phase motion / frozen duals / bounded geometry",test_glitch_frozen_map);
    std::printf("glitch ui: %d/3 passed; shared host peak %d/8192 (not hardware fps)\n",3-failed,maximum_shared_objects);
    return failed?1:0;
}
} // namespace
