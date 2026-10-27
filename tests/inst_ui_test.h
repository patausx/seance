// Included by the actual main -> editor -> module integration harness.
// Host raster is not PICA/device timing; only --inst-only runs these cases.
namespace {
using Inst = InstUnit;
struct InstFixture : ModuleInspectorFixture {
    InstFixture():ModuleInspectorFixture(UnitKind::Inst) {
        Inst defaults;
        for(int p=0;p<Inst::P_COUNT;++p)module->params[p]=defaults.params[p];
        inst_focus_release();inst_focus_select(Inst::P_RELEASE);inst_focus_input(0,false);
        settle();unit().reset();capture();
    }
    Inst& unit(){return *static_cast<Inst*>(module);}
    void set(int p,q15 value){module->params[p].base=value;module->params[p].snap();}
    void settle(){for(int p=0;p<Inst::P_COUNT;++p)module->params[p].snap();unit().control_tick();}
    std::array<q15,Inst::P_COUNT> bases() const {
        std::array<q15,Inst::P_COUNT> v{};for(int p=0;p<Inst::P_COUNT;++p)v[p]=module->params[p].base;return v;
    }
    void feed(int n){q15 buffer[512];while(n){const int count=std::min(n,512);unit().process(buffer,count);n-=count;}}
    static std::pair<int,int> point(int p) {
        if(p==Inst::P_INSTRUMENT)return {130,75};
        const int i=p-1;return {70+(i%2)*152,127+(i/2)*38};
    }
};
q15 inst_lock(const Engine& e,int bank,int step,int param,q15 fallback=-1) {
    const auto& s=e.sequence_pattern(bank).steps[0][step];
    for(int i=0;i<s.lock_count;++i)if(s.locks[i].slot==0 && s.locks[i].param==param)return s.locks[i].value;
    return fallback;
}
void test_inst_controls_actions() {
    InstFixture f;auto& u=f.unit();
    for(int p=1;p<Inst::P_COUNT;++p) {
        f.set(p,p==Inst::P_NOTE?Inst::note_value(60):q15(14000));
        const auto xy=f.point(p);const auto before=f.bases();const int dirty=mutations;
        input::down(xy.first,xy.second);CHECK(g_drag==Drag::InstParam && inst_focus_selected()==p);
        input::move(xy.first,xy.second);CHECK(f.bases()==before && mutations==dirty);
        input::move(xy.first+12,xy.second);input::up();
        CHECK(u.params[p].base==(p==Inst::P_NOTE?Inst::note_value(63):q15(14000+12*32767/126)));
    }
    f.set(Inst::P_INSTRUMENT,Inst::instrument_value(0));
    input::down(299,74);CHECK(Inst::instrument_index(u.params[0].base)==0);input::up();CHECK(Inst::instrument_index(u.params[0].base)==1);
    input::down(299,74);input::move(275,74);input::move(299,74);input::up();CHECK(Inst::instrument_index(u.params[0].base)==1);
    input::down(20,74);input::up();CHECK(Inst::instrument_index(u.params[0].base)==0);
    input::down(130,75);input::move(210,75);input::up();CHECK(Inst::instrument_index(u.params[0].base)==2);
    input::down(286,20);CHECK(!u.bypass);input::up();CHECK(u.bypass);
    input::down(286,20);input::move(260,20);input::up();CHECK(u.bypass);
    input::down(286,20);input::up();CHECK(!u.bypass);
    // Touch arrows/labels cannot survive a button-owned frame or identity change.
    const q15 family=u.params[0].base;
    input::down(299,74);input::frame(KEY_A,KEY_A|KEY_TOUCH);input::frame(0,0);CHECK(u.params[0].base==family);
    input::down(299,74);g_overlay=Overlay::None;f.capture();g_overlay=Overlay::Editor;input::up();CHECK(u.params[0].base==family);
    f.set(Inst::P_INSTRUMENT,Inst::instrument_value(5));f.set(Inst::P_NOTE,Inst::note_value(36));
    const q15 length=u.params[Inst::P_LENGTH].base;
    input::down(222,127);input::move(252,127);input::up();CHECK(u.params[Inst::P_LENGTH].base==length);
    input::down(70,127);input::move(90,127);input::up();CHECK(Inst::note_midi(u.params[Inst::P_NOTE].base)==38);
    f.settle();const auto accepted=u.visual().strikes;const int dirty=mutations;
    input::down(150,231);CHECK(u.visual().strikes==accepted);input::move(210,231);input::up();CHECK(u.visual().strikes==accepted);
    input::down(150,231);input::up();CHECK(u.visual().strikes==accepted+1 && mutations==dirty);
    input::down(250,232);CHECK(!param_lens_is_open());input::move(205,232);input::up();CHECK(!param_lens_is_open());
    input::down(250,232);input::up();CHECK(param_lens_is_open());
    input::frame(KEY_B,KEY_B);input::frame(0,0);CHECK(!param_lens_is_open());
    input::down(40,231);input::move(95,231);input::up();CHECK(g_overlay==Overlay::Editor);
    input::down(40,231);input::up();CHECK(g_overlay==Overlay::None);
}
void test_inst_locks_banks_lens() {
    InstFixture f;auto& u=f.unit();const auto base=f.bases();
    g_overlay=Overlay::None;g_view=View::Sequencer;extent_bank(f,2);
    f.engine->request_pattern(7);CHECK(f.engine->active_pattern==7 && sequencer_edit_bank()==2);
    g_overlay=Overlay::Editor;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=5;
    auto& stored=f.engine->sequence_pattern(2).steps[0][5];
    stored.locks[0]={0,Inst::P_RELEASE,14000};stored.lock_count=1;
    input::down(222,165);input::move(234,165);input::up();
    CHECK(inst_lock(*f.engine,2,5,Inst::P_RELEASE)==14000+12*32767/126 && f.bases()==base);
    CHECK(f.engine->pattern.steps[0][5].lock_count==0);
    input::down(299,74);input::up();CHECK(Inst::instrument_index(inst_lock(*f.engine,2,5,Inst::P_INSTRUMENT))==1 && f.bases()==base);
    CHECK(stored.lock_count==2);
    stored.locks[2]={0,Inst::P_NOTE,Inst::note_value(69)};
    stored.locks[3]={0,Inst::P_LEVEL,27000};stored.lock_count=4;
    const int dirty=mutations;input::down(70,203);input::move(90,203);input::up();CHECK(g_seq_lock_flash>0 && mutations==dirty && f.bases()==base);
    const q15 release=inst_lock(*f.engine,2,5,Inst::P_RELEASE);
    input::down(174,146);CHECK(inst_lock(*f.engine,2,5,Inst::P_RELEASE)==release);input::up();CHECK(inst_lock(*f.engine,2,5,Inst::P_RELEASE)==-1);
    const auto strikes=u.visual().strikes;input::down(150,231);input::up();CHECK(u.visual().strikes==strikes+1);
    bool accepted=false;for(const auto& v:u.visual().voices)if(v.stage && v.note==69 && v.instrument==1)accepted=true;CHECK(accepted);
    input::down(286,20);input::up();CHECK(!u.bypass && f.bases()==base);
    // Capture context changes make later lift inert, including target replacement.
    input::down(299,74);g_seq_lock_step=6;input::up();CHECK(inst_lock(*f.engine,2,6,Inst::P_INSTRUMENT)==-1);
    g_seq_lock_armed=false;g_view=View::Chains;f.engine->cancel_sequence_audition();
    for(int p=0;p<Inst::P_COUNT;++p) {
        inst_focus_select(p);input::frame(0,0);input::frame(KEY_L,KEY_L);CHECK(param_lens_is_open());f.capture();
        const q15 before=u.params[p].base;
        input::down(180,129);input::move(180,129);CHECK(u.params[p].base==before);
        input::move(185,129);input::up();CHECK(u.params[p].base!=before);
        param_lens_touch_down(54,186);param_lens_touch_release();CHECK(u.params[p].src==ModSource::SigilA);
        input::frame(0,0,KEY_L);input::frame(0,0);CHECK(!param_lens_is_open());
    }
    CHECK(!seq_lock::supports_offset(UnitKind::Inst,0,Inst::P_INSTRUMENT));
    CHECK(!seq_lock::supports_offset(UnitKind::Inst,0,Inst::P_NOTE));
    CHECK(seq_lock::supports_offset(UnitKind::Inst,0,Inst::P_RELEASE));
    CHECK(Inst::instrument_index(seq_lock::nudge_value(UnitKind::Inst,0,Inst::P_INSTRUMENT,Inst::instrument_value(2),1,false))==3);
    CHECK(Inst::note_midi(seq_lock::nudge_value(UnitKind::Inst,0,Inst::P_NOTE,Inst::note_value(60),1,false))==61);
    char text[24];seq_lock::format_value(UnitKind::Inst,0,Inst::P_NOTE,Inst::note_value(61),text,sizeof text);CHECK(std::string(text)=="c#4");
    input::down(299,74);delete f.engine->replace_unit(0,0,new Inst);f.module=f.engine->chains[0].units[0];const q15 saved=f.module->params[0].base;
    input::up();CHECK(f.module->params[0].base==saved);
}
void test_inst_frozen_geometry() {
    InstFixture f;auto& u=f.unit();unsigned pairs=0;
    for(auto theme:{palette::Theme::Pink,palette::Theme::Graphite})for(int family=0;family<6;++family) {
        palette::set(theme);f.set(Inst::P_INSTRUMENT,Inst::instrument_value(family));
        f.set(Inst::P_NOTE,Inst::note_value(family==5?38:60));f.set(Inst::P_LENGTH,Inst::length_value(1800));
        f.set(Inst::P_RELEASE,Inst::release_value(640));f.set(Inst::P_ATTACK,Inst::attack_value(8));f.settle();u.reset();inst_focus_select(Inst::P_RELEASE);
        f.capture();const auto idle=cleanup_draw(true);host_time_ms+=123456;f.capture();CHECK(cleanup_draw(true)==idle);
        u.trigger();f.feed(6400);f.capture();const auto top=cleanup_draw(true);const int top_count=objects;
        CHECK(has_text("voice view") && top!=idle);
        const char* dump=std::getenv("SEANCE_INST_DUMP");
        if(dump && theme==palette::Theme::Graphite)dump_surface(dump,std::string(Inst::instrument_name(family))+"-top");
        const auto bottom=cleanup_draw(false);cleanup_pair_budget(top_count);CHECK(has_text("instrument") && has_text("attack") && has_text("release"));
        if(dump && theme==palette::Theme::Graphite)dump_surface(dump,std::string(Inst::instrument_name(family))+"-bottom");
        CHECK(!has_text("one-shot") || family==5);
        const auto before=u.visual();const int dirty=mutations;
        {ModuleInspectorNoEngine frozen;
            module_inspector_surface(false);inst_focus_draw(u);cleanup_geometry();CHECK(pixels==bottom);
            module_inspector_surface(true);CHECK(inst_focus_draw_top());cleanup_geometry();CHECK(pixels==top);
        }
        CHECK(mutations==dirty && u.visual().strikes==before.strikes);
        for(int i=0;i<Inst::VOICE_COUNT;++i)CHECK(u.visual().voices[i].age==before.voices[i].age);
        f.feed(2000);f.capture();CHECK(cleanup_draw(true)!=top); // actual voice time, not UI clock
        ++pairs;
        // Worst control values/large note glyphs/long route and every selected field.
        for(int p=0;p<Inst::P_COUNT;++p)for(q15 raw:{q15(0),q15(32767)}) {
            const q15 old=u.params[p].base;f.set(p,raw);inst_focus_select(p);f.capture();
            cleanup_draw(true);const int count=objects;cleanup_draw(false);cleanup_pair_budget(count);++pairs;f.set(p,old);
        }
    }
    palette::set(palette::Theme::Graphite);f.set(Inst::P_INSTRUMENT,0);u.reset();
    for(int note=48;note<72;++note) {f.set(Inst::P_NOTE,Inst::note_value(note));f.capture();cleanup_draw(true);++pairs;}
    for(int s=1;s<UNITS_PER_CHAIN;++s)delete f.engine->replace_unit(0,s,new Inst);
    f.capture();cleanup_draw(true);CHECK(has_text("inst / inst / inst / inst / inst / inst"));
    u.bypass=true;f.capture();cleanup_draw(true);CHECK(has_text("bypassed"));u.bypass=false;
    f.engine->chains[0].mute=true;f.capture();cleanup_draw(true);CHECK(has_text("chain muted"));
    theme_test::select();std::printf("inst: %u frozen/extreme/glyph dual cases; no wall-clock-driven figure\n",pairs);
}
void test_inst_engine_persistence_picker() {
    InstFixture f;
    // Catalog position is with the sources, not an undiscoverable secret unit.
    g_overlay=Overlay::Picker;picker_open();for(int i=0;i<4;++i)picker_buttons(KEY_DOWN);
    module_inspector_surface(false);picker_draw();cleanup_geometry();CHECK(has_text("inst") && has_text("instruments"));
    picker_buttons(KEY_A);f.module=f.engine->chains[0].units[0];CHECK(f.module && f.module->kind==UnitKind::Inst);
    auto& source=f.unit();g_overlay=Overlay::Editor;
    f.set(Inst::P_NOTE,Inst::note_value(60));source.params[Inst::P_TONE].src=ModSource::SigilH;
    source.params[Inst::P_TONE].mod_depth=7000;source.params[Inst::P_TONE].mod_enabled=false;
    f.settle();CHECK(f.engine->set_step_lock(0,0,0,Inst::P_NOTE,Inst::note_value(72)));
    CHECK(f.engine->set_step_lock(0,0,0,Inst::P_INSTRUMENT,Inst::instrument_value(2)));
    f.engine->capture_scene(0,0);f.engine->pattern.steps[0][0].flags=SeqStep::Trig;
    f.engine->set_sequencer_enabled(true);f.engine->transport_play();q15 audio[2048];
    // Lazy transport starts at its next sixteenth, not at frame zero.
    // Advance to the first real scheduled strike (bounded well below a bar).
    for(int i=0;i<32 && !source.visual().strikes;++i)f.engine->render(audio,512);
    CHECK(source.visual().strikes==1);
    bool accepted=false;for(const auto& v:source.visual().voices)if(v.stage && v.instrument==2 && v.note==72)accepted=true;CHECK(accepted);
    EngineUiSnapshot snap;f.engine->capture_ui_snapshot(snap);
    CHECK(snap.sequence_pitch_kind[0]==uint8_t(UnitKind::Inst) && snap.sequence_pitch_slot[0]==0 && snap.sequence_pitch_param[0]==Inst::P_NOTE);
    CHECK(snap.sequence_value_param[0]==Inst::P_LEVEL && snap.sequence_lifecycle[0]==uint8_t(SequenceLifecycle::Strike));
    CHECK(source.params[Inst::P_NOTE].base==Inst::note_value(60));
    std::unique_ptr<Unit> clone(clone_unit(&source));CHECK(clone && clone->kind==UnitKind::Inst && !static_cast<Inst*>(clone.get())->visual().active);
    char directory[]="/tmp/seance-inst-project-XXXXXX";CHECK(mkdtemp(directory));const std::string path=std::string(directory)+"/test.snc";
    auto loaded=std::make_unique<Engine>();CHECK(patchio::save(*f.engine,path.c_str()) && patchio::load(*loaded,path.c_str()));
    std::remove(path.c_str());rmdir(directory);
    auto* restored=static_cast<Inst*>(loaded->chains[0].units[0]);CHECK(restored && restored->kind==UnitKind::Inst && !restored->visual().active);
    for(int p=0;p<Inst::P_COUNT;++p) {
        CHECK(restored->params[p].base==source.params[p].base && restored->params[p].src==source.params[p].src &&
            restored->params[p].mod_depth==source.params[p].mod_depth && restored->params[p].mod_enabled==source.params[p].mod_enabled);
        CHECK(clone->params[p].base==source.params[p].base);
    }
    CHECK(loaded->scenes[0][0].kind[0]==UnitKind::Inst && inst_lock(*loaded,0,0,Inst::P_NOTE)==Inst::note_value(72));
    const auto base=f.bases();CHECK(f.engine->remove_unit_with_undo(0,0));CHECK(f.engine->undo_structural_edit());
    f.module=f.engine->chains[0].units[0];CHECK(f.module==&source && f.bases()==base);
    f.engine->transport_stop();CHECK(source.visual().active);f.engine->transport_reset();CHECK(!source.visual().active);
    // Actual score note lens and MIDI nudge target the INST note param, not program.
    g_view=View::Sequencer;g_overlay=Overlay::None;f.refresh();sequencer_view_enter();f.mode(1);
    char note[12];sequencer_selected_pitch_text(note,sizeof note);CHECK(std::string(note)=="c5");
    f.tap(KEY_A);CHECK(Inst::note_midi(inst_lock(*f.engine,0,0,Inst::P_NOTE))==73);
    sequencer_capture_inspector();cleanup_draw(true,false);
    auto arp_engine=std::make_unique<Engine>();auto* arp=new ArpUnit;auto* inst=new Inst;
    arp_engine->chains[0].units[0]=arp;arp_engine->chains[0].units[1]=inst;
    for(int p=ArpUnit::P_NOTE1;p<=ArpUnit::P_NOTE4;++p) {
        arp->params[p].base=ArpUnit::note_value(p==ArpUnit::P_NOTE1?7:ArpUnit::NOTE_OFF);arp->params[p].snap();
    }
    arp_engine->sequencer_enabled=false;arp_engine->transport_play();arp_engine->render(audio,128);
    CHECK(ArpUnit::supports(UnitKind::Inst) && inst->visual().strikes==1);
    accepted=false;for(const auto& v:inst->visual().voices)if(v.stage && v.note==67)accepted=true;CHECK(accepted);
}
int run_inst_ui() {
    int failed=0;
    const auto run=[&](const char* name,auto fn){try{fn();std::printf("ok: %s\n",name);}catch(const std::exception& e){++failed;std::printf("FAIL: %s: %s\n",name,e.what());}};
    run("inst actual main/editor controls / release-only buttons / cancellation / drum sound",test_inst_controls_actions);
    run("inst independent edit-bank locks / limit / ownership / all selected modulation",test_inst_locks_banks_lens);
    run("inst real voices / six v2 forms / frozen duals / extrema / note glyphs",test_inst_frozen_geometry);
    run("inst catalog / sequence note+level / arp / clone / SNC7 / structural undo",test_inst_engine_persistence_picker);
    std::printf("inst integration: %d/4; host shared peak %d/8192 objects (not hardware fps)\n",4-failed,maximum_shared_objects);
    return failed?1:0;
}
} // namespace
