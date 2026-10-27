// Focused current MOTOR native host cases. No historical runners execute.
#include "../platform/3ds/motor_editor_visual.h"
#include "../core/engine/musical_note.h"
namespace motor_extended_cases {
namespace h=module_headings_cases;
using M=MotorUnit;
int gesture_checks=0,frozen_checks=0,model_checks=0;
struct Scene:h::Scene {
    Scene():h::Scene(UnitKind::Motor){module_inspector_capture();motor_focus_select(M::P_RPM);capture();}
    ~Scene(){editor_cancel_input();param_lens_cancel();}
};
M& unit(Scene&s){return *static_cast<M*>(s.unit);}
void tap(int x,int y){editor_touch_down(x,y);editor_touch_release();++gesture_checks;}
void take(Scene&s,const std::string&id,const char*variant){
    s.capture();module_inspector_capture();
    h::take(s,id,variant);
    s.capture();module_inspector_capture();editor_style::capture();editor_gpu::begin();
    module_inspector_surface(true);{fader_lancet_cases::AudioHidden hidden;top_draw();}
    CHECK(!outside_rects&&!outside_lines&&!text_overlaps);auto top=pixels;
    module_inspector_surface(false);{fader_lancet_cases::AudioHidden hidden;editor_draw();}
    CHECK(!outside_rects&&!outside_lines&&!text_overlaps);auto bottom=pixels;
    // A changed engine pointer, without recapture, must not change either raster.
    module_inspector_surface(true);{ModuleInspectorNoEngine hidden;top_draw();}CHECK(pixels==top);
    module_inspector_surface(false);{ModuleInspectorNoEngine hidden;motor_focus_draw(unit(s));
        if(param_lens_is_open())param_lens_draw_bottom();}
    CHECK(pixels==bottom);++frozen_checks;
}
void set(Scene&s,int p,q15 v){s.set(p,v);}
void tuning(Scene&s,int midi){set(s,M::P_MODE,32767);set(s,M::P_NOTE,musical_note::encode(midi));}
void styles(const std::string& prefix){
    {
        Scene s;auto&u=unit(s);CHECK(u.modern());tuning(s,45);
        // A real preceding ARP and trailing processor make the six-slot route visible.
        auto& chain=s.engine->chains[0];chain.units[0]=new ArpUnit;
        chain.units[1]=&u;chain.units[2]=new FilterUnit;
        s.slot=g_edit_slot=g_sel_slot=1;
        CHECK(ArpUnit::accepts(&u));
        set(s,M::P_SOURCE,32767);set(s,M::P_WIDTH,10500);set(s,M::P_SYNC,14400);
        set(s,M::P_LOAD,12500);set(s,M::P_JITTER,3000);set(s,M::P_JITTER_SPEED,17500);
        // Accepted note event belongs to the real core; private reference uses its semitone offset.
        u.arp_voice.owner=0;u.arp_voice.semitones=7;u.arp_voice.gate=true;
        s.capture();module_inspector_capture();motor_focus_select(M::P_SYNC);take(s,prefix+"motor_sync_arp","note-sync-arp");
        CHECK(motor_focus_selected()==M::P_SYNC);
        const uint32_t hz=u.nominal_millihz();CHECK(hz>160000&&hz<166000);++model_checks;
        tuning(s,96);u.arp_voice.semitones=48;
        s.capture();module_inspector_capture();editor_style::capture();editor_gpu::begin();
        module_inspector_surface(true);{ModuleInspectorNoEngine hidden;top_draw();}
        bool capped_hz=false;
        for(const auto&t:texts)if(t.text=="14000.0hz nominal")capped_hz=true;
        CHECK(capped_hz&&!outside_rects&&!outside_lines&&!text_overlaps);++model_checks;
    }
    {
        Scene s;set(s,M::P_SOURCE,16384);set(s,M::P_WIDTH,8200);set(s,M::P_SYNC,0);
        motor_focus_select(M::P_WIDTH);take(s,prefix+"motor_pulse_width","pulse-width");
    }
    {
        Scene s;set(s,M::P_SOURCE,0);set(s,M::P_JITTER,23000);set(s,M::P_JITTER_SPEED,22000);
        motor_focus_select(M::P_JITTER_SPEED);const int before=unit(s).params[M::P_WIDTH].base;
        tap(47,126);CHECK(unit(s).params[M::P_WIDTH].base==before);
        take(s,prefix+"motor_saw_drift","saw-width-inactive");
    }
    {
        Scene s;auto&u=unit(s);u.set_modern(false);s.capture();CHECK(u.num_params==6);
        auto& scene=s.engine->scenes[0][0];scene.kind[s.slot]=UnitKind::Motor;
        for(int p=0;p<M::P_COUNT;++p)scene.base[s.slot][p]=q15(2000+p);
        const int neighbor=(s.slot+1)%UNITS_PER_CHAIN;
        scene.kind[neighbor]=UnitKind::Motor;scene.base[neighbor][M::P_WIDTH]=3456;
        const auto old=u.params[M::P_RPM].base;tap(198,43);CHECK(u.modern()&&u.params[M::P_RPM].base==old);
        M defaults;
        for(int p=0;p<M::P_WIDTH;++p)CHECK(scene.base[s.slot][p]==q15(2000+p));
        for(int p=M::P_WIDTH;p<M::P_COUNT;++p){
            CHECK(scene.base[s.slot][p]==defaults.params[p].base);
            CHECK(u.params[p].base==defaults.params[p].base);
        }
        CHECK(scene.base[neighbor][M::P_WIDTH]==3456);
        take(s,prefix+"motor_extended","explicit-extend");
    }
    {
        Scene s;auto&u=unit(s);u.set_modern(false);s.capture();take(s,prefix+"motor_legacy","legacy");
        const auto before=u.params[M::P_RPM].base;tap(185,42);CHECK(u.modern()&&u.params[M::P_RPM].base==before);
    }
    {
        Scene s;unit(s).bypass=true;take(s,prefix+"motor_bypass","bypass-reference");
    }
    {
        Scene s;g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=5;
        CHECK(s.engine->set_step_lock(0,5,s.slot,M::P_WIDTH,10000));
        motor_focus_select(M::P_WIDTH);set(s,M::P_SOURCE,16384);
        take(s,prefix+"motor_lock","lock-reference");g_seq_lock_armed=false;g_view=View::Chains;
    }
    {
        Scene s;motor_focus_select(M::P_NOTE);tuning(s,45);s.capture();u32 down=KEY_L;
        CHECK(param_lens_input(down,KEY_L,0,false)&&param_lens_is_open());
        take(s,prefix+"motor_mod","mod-lens");param_lens_cancel();
    }
}
void gestures(){
    {Scene s;auto&u=unit(s);set(s,M::P_SOURCE,16384);set(s,M::P_WIDTH,14000);
        editor_touch_down(56,133);CHECK(u.params[M::P_WIDTH].base==14000);
        editor_touch_drag(86,133);CHECK(u.params[M::P_WIDTH].base>14000);
        editor_touch_release();++gesture_checks;}
    {Scene s;auto&u=unit(s);tuning(s,45);s.capture();const int before=musical_note::decode(u.params[M::P_NOTE].base);
        editor_touch_down(60,93);editor_touch_drag(76,93);
        CHECK(musical_note::decode(u.params[M::P_NOTE].base)==before+4);
        editor_touch_drag(60,93);CHECK(u.params[M::P_NOTE].base==musical_note::encode(before));
        editor_touch_drag(76,93);editor_touch_release();
        CHECK(musical_note::decode(u.params[M::P_NOTE].base)==before+4);++gesture_checks;}
    {Scene s;auto&u=unit(s);set(s,M::P_SYNC,11000);editor_touch_down(200,93);
        const auto before=u.params[M::P_SYNC].base;motor_focus_select(M::P_GAIN);
        editor_touch_drag(245,93);editor_touch_release();CHECK(u.params[M::P_SYNC].base==before);++gesture_checks;}
    {Scene s;auto&u=unit(s);u.set_modern(false);s.capture();
        tap(181,43);CHECK(!u.modern()); // source right edge is not extend
        g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=5;
        tap(198,43);CHECK(!u.modern());g_seq_lock_armed=false;g_view=View::Chains;}
}
void banks(){
    Scene s;auto&u=unit(s);set(s,M::P_SOURCE,16384);set(s,M::P_MODE,32767);
    const q15 base=u.params[M::P_WIDTH].base;
    g_overlay=Overlay::None;g_view=View::Sequencer;sequencer_view_enter();s.frame();
    for(int b=0;b<NUM_PATTERNS;++b){
        if(b<4){
            const int old=sequencer_edit_bank();extent_test_open(s);
            for(int i=0;i<(b-old+4)%4;++i)extent_hold_key(s,KEY_DOWN);
            extent_test_close(s);
        }else{
            // Legacy B picker displays four slots; validate nonvisible 4..7 through
            // the real sequencer active-bank adoption path, never a fabricated bank.
            s.engine->request_pattern(b,false);sequencer_forget_pins();sequencer_view_enter();s.frame();
        }
        CHECK(sequencer_edit_bank()==b);
        s.engine->request_pattern((b+3)%NUM_PATTERNS,false);
        CHECK(s.engine->active_pattern!=(unsigned)b);
        auto&step=s.engine->sequence_pattern(b).steps[0][5];step.lock_count=0;
        const int active_locks=s.engine->sequence_pattern(s.engine->active_pattern).steps[0][5].lock_count;
        g_overlay=Overlay::Editor;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=5;
        editor_touch_down(64,133);editor_touch_drag(88,133);editor_touch_release();
        CHECK(step.lock_count==1&&step.locks[0].slot==s.slot&&step.locks[0].param==M::P_WIDTH);
        CHECK(step.locks[0].value==q15(base+24*32767/296));
        CHECK(u.params[M::P_WIDTH].base==base);++gesture_checks;
        tap(20,112);CHECK(step.lock_count==0&&u.params[M::P_WIDTH].base==base);
        step.lock_count=2;
        step.locks[0]={uint8_t(s.slot),M::P_SOURCE,32767};
        step.locks[1]={uint8_t(s.slot),M::P_MODE,32767};
        tap(30,38); // visible saw, not a name/remove affordance
        CHECK(step.lock_count==2&&step.locks[0].value==0&&u.params[M::P_SOURCE].base==16384);
        tap(235,38); // visible free: keep explicit zero despite note-mode base
        CHECK(step.lock_count==2&&step.locks[1].value==0&&u.params[M::P_MODE].base==32767);
        step.lock_count=0;
        for(int i=0;i<SEQ_LOCKS;++i)step.locks[i]={uint8_t(s.slot),uint8_t(i),q15(1234)};
        step.lock_count=SEQ_LOCKS;g_seq_lock_flash=0;
        editor_touch_down(64,133);editor_touch_drag(88,133);editor_touch_release();
        CHECK(g_seq_lock_flash>0&&step.lock_count==SEQ_LOCKS&&u.params[M::P_WIDTH].base==base);
        CHECK(s.engine->sequence_pattern(s.engine->active_pattern).steps[0][5].lock_count==active_locks);
        ++gesture_checks;
        g_seq_lock_armed=false;g_overlay=Overlay::None;g_seq_lock_flash=0;
    }
    g_view=View::Chains;g_overlay=Overlay::None;
}
int run(const std::filesystem::path& dir,bool save){
    CHECK(!std::filesystem::exists(dir));std::filesystem::create_directories(dir);
    h::output=dir;h::capture_files=save;h::manifest.open(dir/"surfaces.tsv");
    CHECK(h::manifest.good());h::manifest<<"id\tkind\tmodule\tvariant\tsurface\tobjects\toutside_rects\toutside_lines\ttext_overlaps\traster_fnv64\n";
    fader_lancet_cases::sample_fixture();CHECK(editor_style::init());
    for(const auto theme:{palette::Theme::Graphite,palette::Theme::Pink}){
        palette::set(theme);styles(theme==palette::Theme::Graphite?"graphite_":"pink_");
        gestures();banks();
    }
    editor_style::shutdown();CHECK(editor_gpu::textures.empty());h::manifest.close();
    std::printf("motor extended: %d duals, %d gestures, %d frozen, %d model, native host only\n",
        h::duals,gesture_checks,frozen_checks,model_checks);return 0;
}
}
int main(int argc,char**argv){try{CHECK(argc==3);return motor_extended_cases::run(argv[2],std::string(argv[1])=="--capture");}
catch(const std::exception&e){std::fprintf(stderr,"motor extended failed: %s\n",e.what());return 1;}}
