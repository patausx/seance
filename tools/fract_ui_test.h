// Narrow FRACT checks through the exact main touch dispatcher + real editor.
namespace {
struct FractFixture : ModuleInspectorFixture {
    FractFixture():ModuleInspectorFixture(UnitKind::Fract) {
        FractUnit defaults;
        for(int p=0;p<FractUnit::P_COUNT;++p){module->params[p].base=defaults.params[p].base;module->params[p].snap();}
        fract_focus_release();fract_focus_select(0);unit().control_tick();feed(4096);capture();
    }
    FractUnit& unit(){return *static_cast<FractUnit*>(module);}
    void feed(int frames) {
        q15 block[64];
        while(frames>0) {
            const int n=std::min(frames,64);
            for(int i=0;i<n;++i)block[i]=q15((i%32)*700-11000);
            unit().process(block,n);frames-=n;
        }
    }
};
void test_fract_gesture() {
    FractFixture f;auto& u=f.unit();
    input::down(170,108);CHECK(g_drag==Drag::FractGrab);
    f.feed(128);CHECK(u.visual().manual && u.visual().active);
    const q15 damage=u.params[FractUnit::P_DAMAGE].base;
    input::move(170,108);CHECK(u.params[FractUnit::P_DAMAGE].base==damage);
    input::move(100,108);CHECK(u.params[FractUnit::P_DAMAGE].base>damage);
    input::up();f.feed(256);CHECK(!u.visual().manual && !u.visual().active);
    input::down(170,108);f.feed(128);g_overlay=Overlay::None;f.capture();
    CHECK(!u.visual().manual);g_overlay=Overlay::Editor;input::up();
    input::down(170,108);f.feed(128);input::frame(KEY_L,KEY_L);CHECK(param_lens_is_open() && !u.visual().manual);
    input::frame(0,0,KEY_L);input::up();CHECK(!param_lens_is_open());
    input::down(260,105);input::up();CHECK(u.params[FractUnit::P_HOLD].base==32767);
    u.params[FractUnit::P_HOLD].snap();u.control_tick();f.feed(128);CHECK(u.visual().held);
    input::down(260,105);input::up();u.params[FractUnit::P_HOLD].snap();u.control_tick();f.feed(256);CHECK(!u.visual().held);
}
void test_fract_controls() {
    FractFixture f;auto& u=f.unit();
    for(int p=0;p<6;++p) {
        const int x=(p&1)?240:90,y=151+(p/2)*27;
        const q15 before=u.params[p].base;
        input::down(x,y);CHECK(g_drag==Drag::FractParam && fract_focus_selected()==p);
        input::move(x-10,y);input::up();CHECK(u.params[p].base<=before);
    }
    g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=5;
    const int p=FractUnit::P_DAMAGE;const q15 base=u.params[p].base;
    CHECK(f.engine->set_step_lock(0,5,0,p,10000));
    input::down(240,178);input::move(260,178);input::up();
    CHECK(u.params[p].base==base && seq_display_value(0,p,base)>10000);
    input::down(170,166);input::up();CHECK(seq_display_value(0,p,base)==base);
    input::down(170,108);f.feed(128);CHECK(!u.visual().manual);input::up();
    const q15 sync=u.params[FractUnit::P_SYNC].base;
    input::down(204,10);input::up();CHECK(u.params[FractUnit::P_SYNC].base==sync);
    CHECK(seq_display_value(0,FractUnit::P_SYNC,sync)==0);
    input::down(260,105);input::up();CHECK(u.params[FractUnit::P_HOLD].base==0);
    CHECK(seq_display_value(0,FractUnit::P_HOLD,0)==32767);
    const int dirty=mutations;g_seq_lock_chain=1;input::down(204,10);input::up();CHECK(mutations==dirty);
    g_seq_lock_armed=false;g_view=View::Chains;g_overlay=Overlay::Picker;
    seance::platform::ui3ds::picker_open();seance::platform::ui3ds::picker_buttons(KEY_UP);
    seance::platform::ui3ds::picker_buttons(KEY_UP); // comp, mouth and arp follow fract.
    seance::platform::ui3ds::picker_buttons(KEY_UP);
    seance::platform::ui3ds::picker_buttons(KEY_UP);
    seance::platform::ui3ds::picker_buttons(KEY_A);
    f.module=f.engine->chains[0].units[0];
    CHECK(g_overlay==Overlay::None && f.module && f.module->kind==UnitKind::Fract && f.module->num_params==8);
}
void test_fract_bypass_mute() {
    for(bool muted:{false,true}) {
        FractFixture f;auto& u=f.unit();auto& chain=f.engine->chains[0];
        auto disable=[&]{u.bypass=!muted;chain.mute=muted;};
        disable();
        // Capture occurs between actual main touch frames: it must not revoke a parameter drag.
        for(int p=0;p<6;++p) {
            const int x=(p&1)?240:90,y=151+(p/2)*27,range=p==FractUnit::P_SPEED?65534:32767;
            const q15 origin=p==FractUnit::P_SPEED?-5000:10000;u.params[p].base=origin;
            const int dirty=mutations;input::down(x,y);f.capture();input::move(x,y);
            CHECK(g_drag==Drag::FractParam && u.params[p].base==origin && mutations==dirty);
            input::move(x+11,y);f.capture();CHECK(u.params[p].base==origin+11*range/132);
            input::move(x+22,y);input::up();CHECK(u.params[p].base==origin+22*range/132);
        }
        const int p=FractUnit::P_DAMAGE;u.params[p].base=10000;
        // Becoming bypassed/muted during the same ordinary drag must also preserve its origin.
        u.bypass=chain.mute=false;input::down(240,178);disable();f.capture();
        input::move(251,178);input::up();CHECK(u.params[p].base==10000+11*32767/132);
        // A fresh grab is inert, and the existing target explains why without looking pressed.
        const q15 damage=u.params[p].base;const int dirty=mutations;
        input::down(170,108);f.capture();input::move(100,108);input::up();
        CHECK(!u.visual().manual && u.params[p].base==damage && mutations==dirty);
        f.capture();cleanup_draw(false);
        bool reason=false;
        for(const auto& t:texts)if(t.call_x==100 && t.call_y==97 && t.text==(muted?"chain muted":"bypassed"))reason=true;
        CHECK(reason && module_inspector_text("edit below") && !module_inspector_text("to squeeze"));
        int dim_pixels=0;
        for(int y=98;y<114;++y)for(int x=26;x<74;++x)if(pixels[y*width+x]!=m::paper_color(y)) {
            CHECK(pixels[y*width+x]==m::ghost());++dim_pixels;
        }
        CHECK(dim_pixels>0 && pixels[90*width+14]==m::paper_color(90));
        // Unlike parameter edits, an already-held grab is canceled immediately by capture.
        u.bypass=chain.mute=false;input::down(170,108);f.feed(128);CHECK(u.visual().manual);
        disable();f.capture();CHECK(!u.visual().manual);
        input::move(100,108);input::up();CHECK(u.params[p].base==damage);
        // Bypass/mute do not divert lock edits to base; a step change still invalidates ownership.
        g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=5;
        CHECK(f.engine->set_step_lock(0,5,0,p,8000));input::down(240,178);f.capture();
        input::move(251,178);input::up();CHECK(seq_display_value(0,p,damage)==8000+11*32767/132 && u.params[p].base==damage);
        input::down(240,178);const auto locks=f.hash();g_seq_lock_step=6;f.capture();g_seq_lock_step=5;
        input::move(262,178);input::up();CHECK(f.hash()==locks && u.params[p].base==damage);
        // An ordinary bypassed/muted drag cannot survive leaving the editor either.
        g_seq_lock_armed=false;g_view=View::Chains;input::down(240,178);g_overlay=Overlay::None;f.capture();
        g_overlay=Overlay::Editor;input::move(262,178);input::up();CHECK(u.params[p].base==damage);
    }
}
void test_fract_frozen_ui() {
    FractFixture f;auto& u=f.unit();
    u.params[FractUnit::P_DAMAGE].base=32767;u.params[FractUnit::P_DAMAGE].snap();u.control_tick();
    input::down(170,108);f.feed(50000);
    for(auto theme:{palette::Theme::Pink,palette::Theme::Graphite}) {
        palette::set(theme);f.capture();
        const auto top=cleanup_draw(true);const int top_count=objects;
        const auto lower=cleanup_draw(false);cleanup_pair_budget(top_count);
        {
            ModuleInspectorNoEngine frozen;
            module_inspector_surface(false);fract_focus_draw(u);cleanup_geometry();CHECK(pixels==lower);
            module_inspector_surface(true);CHECK(fract_focus_draw_top());cleanup_geometry();CHECK(pixels==top);
        }
    }
    input::up();theme_test::select();
}
int run_fract_ui() {
    int failed=0;
    const auto run=[&](const char* name,auto test){try{test();std::printf("ok: %s\n",name);}catch(const std::exception& e){++failed;std::printf("FAIL: %s: %s\n",name,e.what());}};
    run("fract main grab / squeeze / release / context / lens / hold",test_fract_gesture);
    run("fract six rails / step locks / clock / hold / input ownership",test_fract_controls);
    run("fract bypass/mute editable rails / disabled grab / capture ownership",test_fract_bypass_mute);
    run("fract bounded frozen tape / two palettes / composed draw",test_fract_frozen_ui);
    std::printf("fract ui: %d/4 passed, shared peak %d/8192\n",4-failed,maximum_shared_objects);
    return failed?1:0;
}
} // namespace
