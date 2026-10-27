// Focused real main/editor ARP workflow; no screenshots or full-suite invocation.
namespace {
using Arp = ArpUnit;
struct ArpFixture : ModuleInspectorFixture {
    ArpFixture():ModuleInspectorFixture(UnitKind::Arp) {
        Arp defaults;
        for(int p=0;p<Arp::P_COUNT;++p) {module->params[p]=defaults.params[p];module->params[p].snap();}
        delete engine->replace_unit(0,1,new OscUnit);
        arp_focus_release();arp_focus_select(0);unit().reset();capture();
    }
    Arp& unit() {return *static_cast<Arp*>(module);}
    std::array<q15,Arp::P_COUNT> bases() const {
        std::array<q15,Arp::P_COUNT> v{};for(int p=0;p<Arp::P_COUNT;++p)v[p]=module->params[p].base;return v;
    }
    static std::pair<int,int> point(int p) {
        return p>=Arp::P_NOTE1?std::pair<int,int>{35+76*(p-Arp::P_NOTE1),94}:
            std::pair<int,int>{35+104*(p%3),164+43*(p/3)};
    }
    void feed(int frames=256) {std::array<q15,512> pcm{};while(frames>0){int n=std::min(256,frames);engine->render(pcm.data(),n);frames-=n;}}
};
void test_arp_input_locks() {
    for(bool lock:{false,true})for(int p=0;p<Arp::P_COUNT;++p) {
        ArpFixture f;auto& u=f.unit();
        const q15 start=p>=Arp::P_NOTE1?Arp::note_value(0):q15(0);
        u.params[p].base=start;u.params[p].snap();
        if(lock) {g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;CHECK(f.engine->set_step_lock(0,7,0,p,start));}
        const auto before=f.bases();const int dirty=mutations;const auto xy=ArpFixture::point(p);
        input::down(xy.first,xy.second);CHECK(g_drag==Drag::ArpParam && arp_focus_selected()==p);
        input::move(xy.first,xy.second);CHECK(f.bases()==before && mutations==dirty);
        input::move(xy.first+12,xy.second);input::up();
        q15 expected=0;
        switch(p) {
            case Arp::P_RATE:expected=Arp::rate_value(1);break;
            case Arp::P_GATE:expected=q15(12*32767/192);break;
            case Arp::P_ORDER:expected=Arp::order_value(1);break;
            case Arp::P_OCTAVES:expected=Arp::octaves_value(2);break;
            case Arp::P_RESET:case Arp::P_PLAY_MODE:expected=32767;break;
            default:expected=Arp::note_value(3);break;
        }
        if(lock) {
            CHECK(f.bases()==before && seq_display_value(0,p,start)==expected);
            const int x=p>=Arp::P_NOTE1?14+76*(p-Arp::P_NOTE1):14+104*(p%3);
            const int y=p>=Arp::P_NOTE1?46:135+43*(p/3);
            input::down(x,y);input::up();CHECK(seq_display_value(0,p,start)==start);
        } else CHECK(u.params[p].base==expected);
        const auto released=f.bases();input::move(xy.first+24,xy.second);CHECK(f.bases()==released);
    }
    ArpFixture f;auto& u=f.unit();
    input::down(28,110);input::up();CHECK(Arp::note_semitones(u.params[Arp::P_NOTE1].base)==Arp::NOTE_OFF);
    input::down(28,110);input::up();CHECK(Arp::note_semitones(u.params[Arp::P_NOTE1].base)==0);
    input::down(280,18);input::up();CHECK(u.bypass && !param_lens_is_open());
    input::down(280,18);input::up();CHECK(!u.bypass);
    auto base=f.bases();input::down(35,164);f.engine->active_pattern=1;input::move(59,164);input::up();CHECK(f.bases()==base);f.engine->active_pattern=0;
    input::down(35,164);g_overlay=Overlay::None;f.capture();g_overlay=Overlay::Editor;input::move(59,164);input::up();CHECK(f.bases()==base);
    g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=5;
    for(int p=0;p<4;++p)CHECK(f.engine->set_step_lock(0,5,0,p,8000));
    input::down(263,94);input::move(279,94);input::up();CHECK(f.bases()==base && seq_display_value(0,Arp::P_NOTE4,base[Arp::P_NOTE4])==base[Arp::P_NOTE4]);
    input::down(280,18);input::up();CHECK(!u.bypass);
    input::frame(KEY_L,KEY_L);CHECK(!param_lens_is_open());input::frame(0,0,KEY_L);
    const int dirty=mutations;g_seq_lock_chain=1;input::down(35,164);input::move(59,164);input::up();CHECK(mutations==dirty);
}
void test_arp_lens_picker_domains() {
    ArpFixture f;auto& u=f.unit();
    for(int p=0;p<Arp::P_COUNT;++p) {
        u.params[p].base=14000;u.params[p].snap();arp_focus_select(p);
        input::frame(0,0);input::frame(KEY_L,KEY_L);CHECK(param_lens_is_open());f.capture();
        const q15 before=u.params[p].base;
        input::down(190,129);input::move(190,129);CHECK(u.params[p].base==before);
        input::move(202,129);input::up();CHECK(u.params[p].base!=before);
        param_lens_touch_down(54,186);param_lens_touch_release();CHECK(u.params[p].src==ModSource::SigilA);
        input::down(200,158);input::move(190,158);input::up();CHECK(u.params[p].mod_depth<0);
        const q15 value=u.params[p].base;input::frame(0,0,KEY_L);input::frame(0,0);
        CHECK(!param_lens_is_open() && arp_focus_selected()==p && u.params[p].base==value && !f.engine->param_temp_active());
    }
    input::down(250,232);input::up();CHECK(param_lens_is_open());input::frame(KEY_B,KEY_B);input::frame(0,0);CHECK(!param_lens_is_open());
    g_overlay=Overlay::Picker;picker_open();picker_buttons(KEY_UP);
    module_inspector_surface(false);picker_draw();cleanup_geometry();CHECK(has_text("arp") && has_text("note arpeggiator"));
    picker_buttons(KEY_A);f.module=f.engine->chains[0].units[0];CHECK(f.module && f.module->kind==UnitKind::Arp && f.module->num_params==10);
    CHECK(menu_style::heading_width("arp")>ui::text_w("arp"));
    char text[32];module_inspector_value_text(text,sizeof text,UnitKind::Arp,Arp::P_NOTE2,Arp::note_value(7));CHECK(std::string(text)=="+7st");
    CHECK(!seq_lock::supports_offset(UnitKind::Arp,0,Arp::P_NOTE1));
    CHECK(seq_lock::supports_offset(UnitKind::Arp,0,Arp::P_GATE));
    const q15 next=seq_lock::nudge_value(UnitKind::Arp,0,Arp::P_NOTE1,Arp::note_value(0),1,false);
    CHECK(Arp::note_semitones(next)==1);
    const q15 first=seq_lock::nudge_value(UnitKind::Arp,0,Arp::P_NOTE1,Arp::note_value(Arp::NOTE_OFF),1,false);
    CHECK(Arp::note_semitones(first)==-24);
    seq_lock::format_value(UnitKind::Arp,0,Arp::P_RATE,Arp::rate_value(6),text,sizeof text);CHECK(std::string(text)=="1/16t");
}
void test_arp_frozen_cycle() {
    ArpFixture f;auto& u=f.unit();
    u.params[Arp::P_ORDER].base=Arp::order_value(2);
    u.params[Arp::P_OCTAVES].base=Arp::octaves_value(3);
    u.params[Arp::P_NOTE4].base=Arp::note_value(12);
    for(auto& p:u.params)p.snap();
    f.engine->set_sequencer_enabled(false);f.engine->transport_play();f.feed();CHECK(u.visual().active && u.visual().count==22);
    for(auto theme:{palette::Theme::Pink,palette::Theme::Graphite}) {
        palette::set(theme);
        for(int p:{Arp::P_RATE,Arp::P_GATE,Arp::P_NOTE4}) {
            arp_focus_select(p);u.params[p].src=ModSource::SigilH;u.params[p].mod_depth=6000;
            f.capture();const auto base=f.bases();const int dirty=mutations;
            const auto top=cleanup_draw(true);const int top_count=objects;
            CHECK(has_text("current cycle / semitones from source") && has_text("source gate / effects keep tails"));
            const auto bottom=cleanup_draw(false);cleanup_pair_budget(top_count);CHECK(has_text("note 4") && has_text("octaves"));
            u.params[p].base=30000;
            {ModuleInspectorNoEngine frozen;
                module_inspector_surface(false);arp_focus_draw(u);cleanup_geometry();CHECK(pixels==bottom);
                module_inspector_surface(true);CHECK(arp_focus_draw_top());cleanup_geometry();CHECK(pixels==top);
            }
            u.params[p].base=base[p];CHECK(f.bases()==base && mutations==dirty);
        }
    }
    // Destination diagnostics are truthful even before the first audio tick.
    delete f.engine->replace_unit(0,1,nullptr);f.capture();cleanup_draw(true);CHECK(has_text("place arp before osc / fm / bird / play"));
    u.bypass=true;f.capture();cleanup_draw(true);CHECK(has_text("bypassed / source independent"));
    u.bypass=false;f.engine->chains[0].mute=true;f.capture();cleanup_draw(true);CHECK(has_text("chain muted"));
    theme_test::select();
}
int run_arp_ui() {
    int failed=0;
    const auto run=[&](const char* name,auto test) {try{test();std::printf("ok: %s\n",name);}catch(const std::exception& e){++failed;std::printf("FAIL: %s: %s\n",name,e.what());}};
    run("arp real main / musical drags / note enables / locks / ownership",test_arp_input_locks);
    run("arp mod lens / picker / physical formatting / discrete lock nudges",test_arp_lens_picker_domains);
    run("arp real ordered cycle / frozen duals / palettes / destination states",test_arp_frozen_cycle);
    std::printf("arp ui: %d/3 passed; host shared peak %d/8192 (not hardware fps)\n",3-failed,maximum_shared_objects);
    return failed?1:0;
}
} // namespace
