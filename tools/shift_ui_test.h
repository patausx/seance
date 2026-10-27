// Actual main input + editor/controller + scalar captures; included by integration harness.
#include "../platform/3ds/ui_picker.h"
namespace {
struct ShiftFixture : ModuleInspectorFixture {
    ShiftFixture():ModuleInspectorFixture(UnitKind::Shift) {shift_focus_release();shift_focus_select(0);}
    ShiftUnit& unit(){return *static_cast<ShiftUnit*>(module);}
    std::array<q15,4> bases() const {return {module->params[0].base,module->params[1].base,module->params[2].base,module->params[3].base};}
    static int rail_y(int p) {return p==0?106:p==1?150:p==2?206:178;}
    static int label_y(int p) {return p==0?35:p==1?137:p==2?193:165;}
};
void test_shift_input() {
    for(bool lock:{false,true})for(int p=0;p<ShiftUnit::P_COUNT;++p) {
        ShiftFixture f;auto& u=f.unit();auto& v=u.params[p];v.base=p?10000:-7000;v.snap();
        if(lock){g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;CHECK(f.engine->set_step_lock(0,7,0,p,12000));}
        const auto original=f.bases();const q15 start=lock?12000:v.base;const int y=ShiftFixture::rail_y(p),range=p?32767:65534;
        input::down(140,y);CHECK(g_drag==Drag::ShiftParam && shift_focus_selected()==p && f.bases()==original);
        f.capture();input::move(140,y);CHECK(f.bases()==original);
        for(int dx:{7,-7,21}) {
            input::move(140+dx,y);f.capture();const q15 expected=q15(start+dx*range/284);
            if(lock)CHECK(f.bases()==original && seq_display_value(0,p,v.base)==expected);
            else {auto values=original;values[p]=expected;CHECK(f.bases()==values);}
        }
        input::up();CHECK(g_drag==Drag::None);const q15 stopped=seq_display_value(0,p,v.base);
        input::move(190,y);CHECK(seq_display_value(0,p,v.base)==stopped);
        if(lock){input::down(22,ShiftFixture::label_y(p));input::up();CHECK(f.bases()==original && seq_display_value(0,p,v.base)==v.base);}
        // Both legal extremes, no absolute jump on touch-down.
        input::down(160,y);input::move(319,y);input::up();
        CHECK(seq_display_value(0,p,v.base)<=32767);
        input::down(300,y);input::move(0,y);input::up();
        CHECK(seq_display_value(0,p,v.base)==(p?0:-32767));
    }
}
void test_shift_zero_header_identity() {
    ShiftFixture f;auto& u=f.unit();input::down(280,35);input::up();CHECK(u.params[0].base==0 && shift_focus_selected()==0);
    input::down(276,10);input::up();CHECK(u.bypass);input::down(276,10);input::up();CHECK(!u.bypass);
    // Former shell targets and passive gutters cannot mutate or delete.
    const int dirty=mutations;for(auto xy:{std::pair<int,int>{210,10},{300,216},{150,87}}){input::down(xy.first,xy.second);input::up();}
    CHECK(mutations==dirty && f.engine->chains[0].units[0]==&u);
    g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=5;
    u.params[0].base=-10000;CHECK(f.engine->set_step_lock(0,5,0,0,9000));
    input::down(280,35);input::up();CHECK(u.params[0].base==-10000 && seq_display_value(0,0,-10000)==0);
    input::down(276,10);input::up();CHECK(!u.bypass);
    const int locked_dirty=mutations;g_seq_lock_chain=1;
    input::down(280,35);input::up();input::down(140,69);input::move(150,69);input::up();CHECK(mutations==locked_dirty);
    g_seq_lock_armed=false;g_view=View::Chains;
    // Fine changes sensitivity, not patch state; exact no-op touch and zero remain.
    const int fine_dirty=mutations;input::down(226,35);input::up();CHECK(mutations==fine_dirty);
    u.params[0].base=518;input::down(140,106);input::move(140,106);CHECK(u.params[0].base==518);
    input::move(141,106);input::up();CHECK(u.params[0].base==518+65534/(284*32));
    f.capture();cleanup_draw(false);CHECK(has_text("fine /32"));
    // Fine is equally safe for armed locks and is not another saved parameter.
    g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=5;
    CHECK(f.engine->set_step_lock(0,5,0,0,518));const auto fine_bases=f.bases();
    input::down(140,106);input::move(141,106);input::up();CHECK(f.bases()==fine_bases && seq_display_value(0,0,0)==525);
    input::down(140,106);f.engine->active_pattern=1;input::move(170,106);input::up();
    f.engine->active_pattern=0;CHECK(seq_display_value(0,0,0)==525);
    input::down(280,35);input::up();CHECK(seq_display_value(0,0,0)==0 && f.bases()==fine_bases);
    input::down(226,35);input::up(); // Restore wide for the other fixtures.
    g_seq_lock_armed=false;g_view=View::Chains;
    input::down(140,69);const q15 before=u.params[0].base;g_edit_slot=1;input::move(180,69);input::up();CHECK(u.params[0].base==before);g_edit_slot=0;
    input::down(140,69);g_overlay=Overlay::None;shift_focus_capture();g_overlay=Overlay::Editor;input::move(180,69);input::up();CHECK(u.params[0].base==before);
}
void test_shift_mod() {
    ShiftFixture f;auto& u=f.unit();
    for(int p=0;p<ShiftUnit::P_COUNT;++p) {
        shift_focus_select(p);input::frame(0,0);input::frame(KEY_L,KEY_L);CHECK(param_lens_is_open());f.capture();
        const q15 before=u.params[p].base;input::down(220,129);input::move(227,129);input::up();CHECK(u.params[p].base!=before);
        input::down(200,155);input::move(207,155);input::up();CHECK(u.params[p].mod_depth!=0);
        input::down(200,189);input::up();CHECK(u.params[p].src!=ModSource::None);
        f.capture();cleanup_draw(true);const int top=objects;cleanup_draw(false);cleanup_pair_budget(top);
        const q15 saved=u.params[p].base;input::frame(0,0,KEY_L);CHECK(!param_lens_is_open() && u.params[p].base==saved && !f.engine->param_temp_active());
    }
    input::frame(0,0);input::down(250,232);input::up();CHECK(param_lens_is_open());
    input::frame(KEY_B,KEY_B);CHECK(!param_lens_is_open());input::frame(0,0);
}
void test_shift_frames() {
    ShiftFixture f;auto& u=f.unit();int pairs=0;
    for(auto theme:{palette::Theme::Pink,palette::Theme::Graphite}) {
        palette::set(theme);
        for(int p=0;p<ShiftUnit::P_COUNT;++p) {
            shift_focus_select(p);u.bypass=p==1;f.engine->chains[0].mute=p==2;
            for(int n=0;n<ShiftUnit::P_COUNT;++n) {
                auto& v=u.params[n];v.base=n?24000:-32767;v.src=ModSource::SigilB;v.mod_depth=14000;v.snap();v.resolve(12000);
            }
            g_seq_lock_armed=p==3;g_seq_lock_chain=0;g_seq_lock_step=7;g_view=p==3?View::Sequencer:View::Chains;
            if(p==3)CHECK(f.engine->set_step_lock(0,7,0,p,7000));
            f.capture();const auto before=f.bases();const int dirty=mutations;
            const auto top=cleanup_draw(true);const int top_count=objects;CHECK(has_text("model / not audio") && has_text("reference tones / ideal loop"));
            const auto lower=cleanup_draw(false);const int lower_count=objects;cleanup_pair_budget(top_count);CHECK(has_text("damp") && has_text("wide"));
            const int y=ShiftFixture::rail_y(p),stored=p==3?7000:before[p];
            const int bx=18+(stored+(p?0:32767))*284/(p?32767:65534);
            const int ex=18+(int(u.params[p].effective)+(p?0:32767))*284/(p?32767:65534);
            CHECK(pixels[(y-4)*320+bx]==menu_style::spot());
            CHECK(pixels[(y+6)*320+ex]==(u.bypass||p==2?menu_style::ghost():menu_style::spot()));
            module_inspector_surface(false);shift_focus_draw(u);CHECK(pixels==lower && objects==lower_count);
            u.params[p].base=1234;g_ui_snapshot={};
            {ModuleInspectorNoEngine frozen;
                module_inspector_surface(false);shift_focus_draw(u);cleanup_geometry();CHECK(pixels==lower);
                module_inspector_surface(true);CHECK(module_inspector_draw_top());cleanup_geometry();CHECK(pixels==top);
            }
            u.params[p].base=before[p];CHECK(f.bases()==before && mutations==dirty);++pairs;
        }
    }
    theme_test::select();
    std::printf("shift: %d frozen actual upper/lower pairs; both palettes / stored-live markers\n",pairs);
}
void test_shift_values() {
    char text[24];module_inspector_value_text(text,sizeof text,UnitKind::Shift,0,-32767);CHECK(std::string(text)=="-2000hz");
    module_inspector_value_text(text,sizeof text,UnitKind::Shift,0,32767);CHECK(std::string(text)=="+2000hz");
    module_inspector_value_text(text,sizeof text,UnitKind::Shift,0,0);CHECK(std::string(text)=="0hz");
    module_inspector_value_text(text,sizeof text,UnitKind::Shift,1,32767);CHECK(std::string(text)=="85.0%");
    module_inspector_value_text(text,sizeof text,UnitKind::Shift,2,32767);CHECK(std::string(text)=="100%");
    module_inspector_value_text(text,sizeof text,UnitKind::Shift,3,0);CHECK(std::string(text)=="0%");
    module_inspector_value_text(text,sizeof text,UnitKind::Shift,3,32767);CHECK(std::string(text)=="100%");
    CHECK(std::string(unit_kind_name(UnitKind::Shift))=="SHIFT");
}
void test_shift_picker() {
    ShiftFixture f;g_overlay=Overlay::Picker;
    seance::platform::ui3ds::picker_open();for(int i=0;i<6;++i)seance::platform::ui3ds::picker_buttons(KEY_UP); // bird, fract, comp, mouth and arp follow shift
    module_inspector_surface(false);picker_draw();cleanup_geometry();CHECK(has_text("shift") && has_text("frequency shift"));
    seance::platform::ui3ds::picker_buttons(KEY_A);CHECK(g_overlay==Overlay::None);
    auto* unit=f.engine->chains[0].units[0];CHECK(unit && unit->kind==UnitKind::Shift && unit->num_params==ShiftUnit::P_COUNT);
    f.module=unit; // picker legitimately replaced fixture pointer
    g_overlay=Overlay::Picker;seance::platform::ui3ds::picker_open();
    seance::platform::ui3ds::picker_touch_down(303,204);seance::platform::ui3ds::picker_touch_release();
    module_inspector_surface(false);picker_draw();int shift_y=-1;
    for(const auto& text:texts)if(text.text=="shift")shift_y=text.y+3;
    CHECK(shift_y>=59 && shift_y<215);
    seance::platform::ui3ds::picker_touch_down(90,shift_y);seance::platform::ui3ds::picker_touch_release();
    CHECK(g_overlay==Overlay::None && f.engine->chains[0].units[0]->kind==UnitKind::Shift);
    f.module=f.engine->chains[0].units[0];
}
void dump_shift(const std::filesystem::path& dir) {
    std::filesystem::create_directories(dir);std::ofstream manifest(dir/"manifest.tsv");manifest<<"frame\ttop\tbottom\tshared\tclipped\toverlaps\n";
    ShiftFixture f;auto& u=f.unit();
    u.params[0].base=3663;u.params[1].base=0;u.params[2].base=32767;for(auto& p:u.params)p.snap();
    f.capture();cleanup_dump_pair(dir,manifest,"01_shift_small");
    u.params[0].base=-14000;u.params[0].src=ModSource::SigilA;u.params[0].mod_depth=12000;u.params[0].snap();u.params[0].effective=-7000;
    u.params[1].base=24000;u.params[1].snap();u.params[2].base=20000;u.params[2].snap();f.capture();cleanup_dump_pair(dir,manifest,"02_shift_modulated");
    shift_focus_select(1);f.capture();cleanup_dump_pair(dir,manifest,"03_shift_feedback");
}
int run_shift_ui(int argc,char** argv) {
    int failed=0,total=0;
    const auto run=[&](const char* name,auto test){++total;try{test();std::printf("ok: %s\n",name);}catch(const std::exception& e){++failed;std::printf("FAIL: %s: %s\n",name,e.what());}};
    run("shift actual main / all base+lock rails",test_shift_input);
    run("shift zero/header/identity/cancellation",test_shift_zero_header_identity);
    run("shift held+latch modulation / release",test_shift_mod);
    run("shift frozen composed pairs / bounds / budget",test_shift_frames);
    run("shift physical readouts / metadata",test_shift_values);
    run("shift real picker reachable / constructs",test_shift_picker);
    if(argc==3 && std::string(argv[1])=="--dump-shift")run("shift three actual dual captures",[&]{dump_shift(argv[2]);});
    std::printf("shift ui: %d/%d passed, shared peak %d/8192\n",total-failed,total,maximum_shared_objects);return failed?1:0;
}
} // namespace
