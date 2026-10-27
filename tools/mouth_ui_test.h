// Focused real main/editor input and frozen Mouth surfaces; no screenshot exports.
namespace {
struct MouthFixture : ModuleInspectorFixture {
    MouthFixture():ModuleInspectorFixture(UnitKind::Mouth){
        MouthUnit defaults;
        for(int p=0;p<MouthUnit::P_COUNT;++p){module->params[p]=defaults.params[p];module->params[p].snap();}
        mouth_focus_release();mouth_focus_select(0);unit().reset();capture();
    }
    MouthUnit& unit(){return *static_cast<MouthUnit*>(module);}
    std::array<q15,5> bases(){return {module->params[0].base,module->params[1].base,module->params[2].base,module->params[3].base,module->params[4].base};}
    static std::pair<int,int> point(int p){return p?std::pair<int,int>{40+(p-1)*76,194}:std::pair<int,int>{140,103};}
    void feed(){q15 samples[256]={12000};unit().control_tick();unit().process(samples,256);}
};
void test_mouth_input_locks(){
    for(bool lock:{false,true})for(int p=0;p<MouthUnit::P_COUNT;++p){
        MouthFixture f;auto& u=f.unit();u.params[p].base=12000;u.params[p].snap();
        if(lock){g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;CHECK(f.engine->set_step_lock(0,7,0,p,16000));}
        const auto base=f.bases();const int dirty=mutations;const auto xy=MouthFixture::point(p);
        input::down(xy.first,xy.second);CHECK(g_drag==Drag::MouthParam && mouth_focus_selected()==p);
        input::move(xy.first,xy.second);CHECK(f.bases()==base && mutations==dirty);
        input::move(xy.first+7,xy.second);input::up();
        const q15 expect=q15((lock?16000:12000)+7*32767/(p?62:256));
        if(lock){CHECK(f.bases()==base && seq_display_value(0,p,base[p])==expect);
            input::down(p?16+(p-1)*76:100,p?155:18);input::up();CHECK(seq_display_value(0,p,base[p])==base[p]);}
        else CHECK(u.params[p].base==expect);
        const q15 stopped=seq_display_value(0,p,u.params[p].base);
        input::move(xy.first+20,xy.second);CHECK(seq_display_value(0,p,u.params[p].base)==stopped);
    }
    MouthFixture f;auto& u=f.unit();
    input::down(280,214);input::up();CHECK(u.bypass && !param_lens_is_open());
    input::down(280,214);input::up();CHECK(!u.bypass);
    const auto base=f.bases();input::down(140,103);f.engine->active_pattern=1;input::move(190,103);input::up();CHECK(f.bases()==base);f.engine->active_pattern=0;
    input::down(140,103);g_overlay=Overlay::None;f.capture();g_overlay=Overlay::Editor;input::move(190,103);input::up();CHECK(f.bases()==base);
    g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=5;
    for(int p=0;p<4;++p)CHECK(f.engine->set_step_lock(0,5,0,p,8000));
    input::down(270,194);input::move(285,194);input::up();CHECK(f.bases()==base && seq_display_value(0,4,base[4])==base[4]);
    input::down(280,214);input::up();CHECK(!u.bypass);
    input::frame(KEY_L,KEY_L);CHECK(!param_lens_is_open());input::frame(0,0,KEY_L);
    const int dirty=mutations;g_seq_lock_chain=1;input::down(140,103);input::move(190,103);input::up();CHECK(mutations==dirty);
}
void test_mouth_mod_and_picker(){
    MouthFixture f;auto& u=f.unit();
    for(int p=0;p<MouthUnit::P_COUNT;++p){
        u.params[p].base=14000;u.params[p].snap();
        mouth_focus_select(p);input::frame(0,0);input::frame(KEY_L,KEY_L);CHECK(param_lens_is_open());f.capture();
        const q15 before=u.params[p].base;
        input::down(190,129);input::move(190,129);CHECK(u.params[p].base==before);
        input::move(193,129);input::up();CHECK(u.params[p].base!=before);
        param_lens_touch_down(54,186);param_lens_touch_release();CHECK(u.params[p].src==ModSource::SigilA);
        input::down(200,158);input::move(190,158);input::up();CHECK(u.params[p].mod_depth<0);
        f.capture();cleanup_draw(true);const int top=objects;cleanup_draw(false);cleanup_pair_budget(top);
        const q15 saved=u.params[p].base;input::frame(0,0,KEY_L);input::frame(0,0);
        CHECK(!param_lens_is_open() && mouth_focus_selected()==p && u.params[p].base==saved && !f.engine->param_temp_active());
    }
    input::down(250,232);input::up();CHECK(param_lens_is_open());input::frame(KEY_B,KEY_B);input::frame(0,0);CHECK(!param_lens_is_open());
    g_overlay=Overlay::Picker;picker_open();picker_buttons(KEY_UP);picker_buttons(KEY_UP); // arp follows mouth
    module_inspector_surface(false);picker_draw();cleanup_geometry();CHECK(has_text("mouth") && has_text("formant filter"));
    picker_buttons(KEY_A);f.module=f.engine->chains[0].units[0];CHECK(f.module && f.module->kind==UnitKind::Mouth && f.module->num_params==5);
    char text[24];module_inspector_value_text(text,sizeof text,UnitKind::Mouth,0,32767);CHECK(std::string(text)=="u");
    module_inspector_value_text(text,sizeof text,UnitKind::Mouth,1,16384);CHECK(std::string(text)=="1.00x");
    module_inspector_value_text(text,sizeof text,UnitKind::Mouth,4,16384);CHECK(std::string(text)=="+0.0db");
    CHECK(menu_style::heading_width("mouth")>30 && menu_style::heading_width("mouth-a")==18);
}
void test_mouth_frozen_model(){
    MouthFixture f;auto& u=f.unit();
    for(auto theme:{palette::Theme::Pink,palette::Theme::Graphite}){
        palette::set(theme);
        for(int p=0;p<MouthUnit::P_COUNT;++p){
            mouth_focus_select(p);u.bypass=p==1;f.engine->chains[0].mute=p==2;
            for(int n=0;n<MouthUnit::P_COUNT;++n){auto& v=u.params[n];v.base=n%2?32767:10000;v.src=ModSource::SigilH;v.mod_depth=9000;v.snap();v.resolve(24000);}
            g_view=p==4?View::Sequencer:View::Chains;g_seq_lock_armed=p==4;g_seq_lock_chain=0;g_seq_lock_step=7;
            if(p==4)CHECK(f.engine->set_step_lock(0,7,0,p,0));
            f.feed();f.capture();const int dirty=mutations;const auto base=f.bases();
            const auto top=cleanup_draw(true);const int top_count=objects;CHECK(has_text("model / not audio") && has_text(p==4?"lock draft response / db":"applied response / db"));
            const auto bottom=cleanup_draw(false);cleanup_pair_budget(top_count);CHECK(has_text("resonance") && has_text("out"));
            const int x=p?14+(p-1)*76:32,extent=p?62:256,y=p?194:103;
            const int stored=p==4?0:base[p],handle=x+stored*extent/32767,live=x+int(u.params[p].effective)*extent/32767;
            CHECK(pixels[(y-4)*320+handle]==menu_style::spot());
            CHECK(pixels[(y+6)*320+live]==(u.bypass||p==2?menu_style::ghost():menu_style::spot()));
            u.params[p].base=20000;
            {ModuleInspectorNoEngine frozen;
                module_inspector_surface(false);mouth_focus_draw(u);cleanup_geometry();CHECK(pixels==bottom);
                module_inspector_surface(true);CHECK(mouth_focus_draw_top());cleanup_geometry();CHECK(pixels==top);
            }
            u.params[p].base=base[p];CHECK(f.bases()==base && mutations==dirty);
        }
    }
    theme_test::select();
}
int run_mouth_ui(){
    int failed=0;
    const auto run=[&](const char* name,auto test){try{test();std::printf("ok: %s\n",name);}catch(const std::exception& e){++failed;std::printf("FAIL: %s: %s\n",name,e.what());}};
    run("mouth real main / five relative controls / locks / full bank / ownership",test_mouth_input_locks);
    run("mouth selected mod / relative lens / picker / physical values",test_mouth_mod_and_picker);
    run("mouth applied+draft response / frozen duals / palettes / sigil markers",test_mouth_frozen_model);
    std::printf("mouth ui: %d/3 passed; host shared peak %d/8192 (not hardware fps)\n",3-failed,maximum_shared_objects);
    return failed?1:0;
}
} // namespace
