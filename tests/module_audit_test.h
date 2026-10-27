// Included by the existing real-main/editor harness. No media or full suite.
// WAV IO remains the existing explicit export-outcome shim; engine/PCM are real.
namespace {
using AuditResult=trackr::synth::SampleEditResult;
struct AuditRecFixture : RecFixture {
    trackr::synth::Sample saved_target;
    AuditRecFixture() {
        std::swap(saved_target,trackr::synth::SampleBank::instance().slot(17));
        input::nav_opened=false;input::frame(0,0);
    }
    ~AuditRecFixture() {
        u32 down=0;recorder_focus_input(down,0,true);
        std::swap(saved_target,trackr::synth::SampleBank::instance().slot(17));
        input::nav_opened=false;
    }
    void take() {
        input::down(24,170);input::up();feed();input::down(118,170);input::up();
        CHECK(rec().sample_slot==16 && rec().take_needs_save() && !rec().is_recording());
    }
    void prompt() {
        input::down(296,76);input::move(140,166);input::up();
        CHECK(recorder_focus_confirming() && rec().sample_slot==16 && rec().recorded_frames()==32768);
    }
};
void audit_tap(int x,int y) {input::down(x,y);input::up();}
void test_audit_rec_choices() {
    AuditRecFixture f;auto& u=f.rec();auto& bank=trackr::synth::SampleBank::instance();f.take();
    const auto pcm=bank.slot(16).data;const auto take=u.take_generation();
    f.prompt();f.capture();const auto frozen=f.bottom();
    CHECK(module_inspector_text("unsaved take") && module_inspector_text("discard") && module_inspector_text("cancel"));
    CHECK(f.bottom()==frozen && rec_export_calls==0 && u.take_generation()==take);
    // A choice is a real press + lift; moving outside permanently cancels that press.
    input::down(40,166);CHECK(rec_export_calls==0);input::move(180,210);input::move(40,166);input::up();
    CHECK(rec_export_calls==0 && recorder_focus_confirming());
    audit_tap(40,166);CHECK(rec_export_calls==1 && u.sample_slot==16 && u.take_needs_save());
    f.capture();f.bottom();CHECK(module_inspector_text("wav save failed"));
    audit_tap(260,166);CHECK(!recorder_focus_confirming() && u.take_generation()==take && u.take_needs_save());
    CHECK(bank.slot(16).data==pcm);
    f.prompt();rec_export_succeeds=true;
    input::down(40,166);CHECK(rec_export_calls==1 && u.sample_slot==16);input::up();
    CHECK(rec_export_calls==2 && !recorder_focus_confirming() && u.sample_slot==17 && u.recorded_frames()==0);
    CHECK(bank.slot(16).data==pcm && bank.provenance(16).origin==trackr::synth::SampleOrigin::Recording);
    CHECK(!u.take_needs_save());
}
void test_audit_rec_discard_failure() {
    AuditRecFixture f;auto& u=f.rec();auto& bank=trackr::synth::SampleBank::instance();f.take();f.prompt();
    const auto pcm=bank.slot(16).data;const auto take=u.take_generation();
    auto* play=new PlayUnit;play->sample_slot=17;delete f.engine->replace_unit(0,1,play);
    audit_tap(140,166); // target ownership failure must not discard first
    CHECK(u.sample_slot==16 && u.recorded_frames()==32768 && u.take_generation()==take && u.take_needs_save());
    CHECK(recorder_focus_confirming() && bank.slot(16).data==pcm && rec_export_calls==0);
    delete f.engine->replace_unit(0,1,nullptr);
    input::down(140,166);CHECK(u.sample_slot==16);input::up();
    CHECK(u.sample_slot==17 && u.recorded_frames()==0 && !recorder_focus_confirming() && rec_export_calls==0);
    CHECK(bank.slot(16).data==pcm && bank.provenance(16).origin==trackr::synth::SampleOrigin::Runtime);
}
void test_audit_rec_cancel_ownership() {
    AuditRecFixture f;auto& u=f.rec();f.take();const auto take=u.take_generation();
    for(u32 key:{u32(KEY_B),u32(KEY_A|KEY_R|KEY_TOUCH),u32(KEY_L|KEY_TOUCH),u32(KEY_SELECT),u32(KEY_START)}) {
        f.prompt();input::down(140,166);const auto result=input::frame(key,key|KEY_TOUCH);
        CHECK(result.skip && !recorder_focus_confirming() && !param_lens_is_open());
        CHECK((result.remaining & ~(KEY_SELECT|KEY_START|KEY_Y|KEY_ZL))==0);
        input::frame(0,KEY_TOUCH);input::frame(0,0);input::up();
        CHECK(u.sample_slot==16 && u.take_generation()==take && u.take_needs_save() && rec_export_calls==0);
    }
    f.prompt();input::down(140,166);input::nav_opened=true;
    CHECK(input::frame(0,KEY_TOUCH).skip && !recorder_focus_confirming());input::nav_opened=false;input::frame(0,0);
    f.prompt();input::down(140,166);g_edit_slot=1;
    CHECK(input::frame(0,0).skip && !recorder_focus_confirming());g_edit_slot=0;
    CHECK(u.sample_slot==16 && u.take_needs_save());
    f.prompt();input::down(140,166);
    Unit* previous=f.engine->replace_unit(0,0,new RecUnit);
    CHECK(input::frame(0,0).skip && !recorder_focus_confirming());
    delete f.engine->replace_unit(0,0,previous);CHECK(u.take_generation()==take && u.take_needs_save());
    f.prompt();input::down(140,166);u.clear_take();u.trigger();q15 data[4]={11,22,33,44};u.capture_input(data,4);u.stop();
    CHECK(input::frame(0,0).skip && !recorder_focus_confirming());
    CHECK(u.sample_slot==16 && u.recorded_frames()==4 && u.take_needs_save());
}
void test_audit_rec_core_tokens() {
    AuditRecFixture f;auto& u=f.rec();f.take();auto& bank=trackr::synth::SampleBank::instance();
    const auto pcm=bank.slot(16).data;
    RecWorkspacePlan plan;CHECK(f.engine->plan_recorder(u,false,plan,17)==AuditResult::Ok);
    CHECK(Engine::stage_recorder(plan)==AuditResult::Ok);
    CHECK(f.engine->commit_recorder(plan)==AuditResult::Busy && u.take_needs_save() && u.sample_slot==16);
    CHECK(bank.slot(16).data==pcm && f.engine->retarget_recorder(u,17)==AuditResult::Busy);
    // Consent is tied to take+sample tokens, not merely the same REC pointer.
    plan.discard_unsaved=true;u.clear_take();u.trigger();q15 data[4]={111,222,333,444};u.capture_input(data,4);u.stop();
    CHECK(f.engine->commit_recorder(plan)==AuditResult::Busy && u.recorded_frames()==4 && u.sample_slot==16);
    CHECK(f.engine->plan_recorder(u,false,plan,17)==AuditResult::Ok);CHECK(Engine::stage_recorder(plan)==AuditResult::Ok);
    u.mark_take_saved(u.take_generation());CHECK(f.engine->commit_recorder(plan)==AuditResult::Ok && u.sample_slot==17);
    // A quantized start arriving after staging prevents the swap; planning while
    // queued is refused as well. No controller may silently abandon queued audio.
    CHECK(f.engine->plan_recorder(u,false,plan,16)==AuditResult::Ok);CHECK(Engine::stage_recorder(plan)==AuditResult::Ok);
    f.engine->transport_play();
    CHECK(f.engine->request_recorder_record(0,0,true,LaunchQuantize::Beat)==AuditResult::Ok);
    CHECK(f.engine->recorder_record_pending(0,0));
    CHECK(f.engine->commit_recorder(plan)==AuditResult::Busy && u.sample_slot==17);
    RecWorkspacePlan queued;CHECK(f.engine->plan_recorder(u,false,queued,16)==AuditResult::Busy);
    CHECK(f.engine->request_recorder_record(0,0,false,LaunchQuantize::Immediate)==AuditResult::Ok);
    f.engine->transport_stop();
}
void audit_select(UnitKind kind,int p) {
    if(kind==UnitKind::Fm)fm_focus_select(p);
    else if(kind==UnitKind::Reverb) {
        CHECK(p==0 || p==1);auto& u=*static_cast<ReverbUnit*>(edited_unit());
        CHECK(reverb_focus_touch_body(u,p?180:24,42));reverb_focus_release();CHECK(reverb_focus_selected()==p);
    } else if(kind==UnitKind::Ghost) {
        CHECK(p==0 || p==1);auto& u=*static_cast<GhostUnit*>(edited_unit());const int v=u.params[p].base;
        CHECK(ghost_focus_touch_body(u,p?61:82+v*150/32767,p?174-v*82/32767:54));
        ghost_focus_release();CHECK(ghost_focus_selected()==p);
    } else if(kind==UnitKind::Stranular) {
        CHECK(p==0 || p==1);auto& u=*static_cast<StranularUnit*>(edited_unit());
        CHECK(stranular_focus_touch_body(u,p?200:50,122));stranular_focus_release();CHECK(stranular_focus_selected()==p);
    } else module_inspector_select(kind,p);
}
void audit_open(UnitKind kind,int p) {
    param_lens_cancel();audit_select(kind,p);
    u32 down=KEY_L;CHECK(param_lens_input(down,KEY_L,0,false) && param_lens_is_open());
    param_lens_touch_release();
}
void audit_lens_tap(int x,int y) {CHECK(param_lens_touch_down(x,y));param_lens_touch_release();}
std::vector<u32> audit_lens_bottom() {
    module_inspector_surface(false);
    {ModuleInspectorNoEngine frozen;param_lens_draw_bottom();}
    module_inspector_geometry();return pixels;
}
void test_audit_lens_relative_muted() {
    for(UnitKind kind:{UnitKind::Fm,UnitKind::Reverb,UnitKind::Ghost,UnitKind::Chorus,UnitKind::Vca,UnitKind::Stranular}) {
        ModuleInspectorFixture f(kind);auto& p=f.module->params[1];p.base=13000;p.mod_depth=-1234;p.src=ModSource::SigilB;p.mod_enabled=false;p.snap();
        audit_open(kind,1);const int dirty=mutations;
        param_lens_touch_down(280,129);param_lens_touch_drag(280,129);CHECK(p.base==13000 && mutations==dirty);
        param_lens_touch_drag(286,129);param_lens_touch_release();CHECK(p.base==13000+6*32767/271 && !p.mod_enabled);
        param_lens_touch_down(120,166);param_lens_touch_drag(120,166);CHECK(p.mod_depth==-1234);
        param_lens_touch_drag(126,166);param_lens_touch_release();CHECK(p.mod_depth==-1234+6*65534/271 && !p.mod_enabled);
        f.capture();const auto muted=audit_lens_bottom();CHECK(module_inspector_text("link muted"));
        p.mod_enabled=true;CHECK(audit_lens_bottom()==muted);p.mod_enabled=false;
        const q15 base=p.base,depth=p.mod_depth;
        audit_lens_tap(60,88);CHECK(p.mod_enabled && p.src==ModSource::SigilB && p.mod_depth==depth && p.base==base);
        audit_lens_tap(60,88);CHECK(!p.mod_enabled);
        audit_lens_tap(84,185);CHECK(p.src==ModSource::SigilB && p.mod_enabled); // same source explicitly enables
        p.mod_enabled=false;audit_lens_tap(53,185);CHECK(p.src==ModSource::SigilA && p.mod_enabled && p.mod_depth==depth);
        p.mod_enabled=false;audit_lens_tap(288,185);CHECK(p.src==ModSource::EnvFollow && p.mod_enabled);
        p.mod_enabled=false;f.capture();audit_lens_bottom();CHECK(module_inspector_text("link muted") && module_inspector_text("env all"));
        audit_lens_tap(18,185);CHECK(p.src==ModSource::None && p.mod_enabled && p.mod_depth==depth);
        param_lens_cancel();
    }
}
void test_audit_lens_fine_neutral() {
    struct Neutral {UnitKind kind;int param;int value;const char* label;};
    for(const auto& item:{Neutral{UnitKind::SamplePlayer,PlayUnit::P_SPEED,8192,"unity"},
        Neutral{UnitKind::Grain,GrainUnit::P_PITCH,8192,"unity"},Neutral{UnitKind::Fract,FractUnit::P_SPEED,8192,"unity"},
        Neutral{UnitKind::MicIn,MicUnit::P_GAIN,8192,"unity"},Neutral{UnitKind::Vca,VcaUnit::P_GAIN,32767,"unity"},
        Neutral{UnitKind::Fm,FmUnit::P_RATIO_FINE,0,"zero"},Neutral{UnitKind::Filter,FilterUnit::P_CUTOFF,0,"min"}}) {
        ModuleInspectorFixture f(item.kind);auto& p=f.module->params[item.param];p.base=12000;p.mod_depth=3456;p.src=ModSource::SigilH;p.mod_enabled=false;
        audit_open(item.kind,item.param);f.capture();audit_lens_bottom();CHECK(module_inspector_text(item.label));
        audit_lens_tap(286,108);CHECK(p.base==item.value && p.src==ModSource::SigilH && !p.mod_enabled && p.mod_depth==3456);
        audit_lens_tap(286,146);CHECK(p.mod_depth==0 && p.src==ModSource::SigilH && !p.mod_enabled);
        param_lens_cancel();
    }
    ModuleInspectorFixture f(UnitKind::Shift);auto& p=f.module->params[ShiftUnit::P_SHIFT];p.base=1000;p.mod_depth=0;
    f.module->bypass=true;f.engine->chains[0].mute=true;audit_open(UnitKind::Shift,ShiftUnit::P_SHIFT);
    // Coarse -> held-R fine -> coarse rebases at the last real coordinate.
    param_lens_touch_down(150,129);param_lens_touch_drag(160,129);const q15 coarse=p.base;
    u32 down=KEY_R;param_lens_input(down,KEY_L|KEY_R|KEY_TOUCH,0,false);
    param_lens_touch_drag(160,129);CHECK(p.base==coarse);
    param_lens_touch_drag(176,129);CHECK(p.base==coarse+16*65534/(271*32));const q15 precise=p.base;
    down=0;param_lens_input(down,KEY_L|KEY_TOUCH,KEY_R,false);param_lens_touch_drag(176,129);CHECK(p.base==precise);
    param_lens_touch_drag(182,129);CHECK(p.base==precise+6*65534/271);param_lens_touch_release();
    audit_lens_tap(224,108); // same fine mode is also available by stylus
    param_lens_touch_down(150,166);param_lens_touch_drag(166,166);param_lens_touch_release();CHECK(p.mod_depth==16*65534/(271*8));
    f.capture();audit_lens_bottom();CHECK(module_inspector_text("fine / tap link"));param_lens_cancel();
    // Musical unity is the actual source-rate law, not the old 2x GRAN default.
    CHECK(play_window(2,nullptr,0,8192,0,32767,0,false,true).increment==65536);
    CHECK(FractUnit::speed_q16(8192)==65536);
}
void test_audit_rec_lens_busy() {
    AuditRecFixture f;auto& u=f.rec();input::down(24,170);input::up();CHECK(u.is_recording());
    audit_open(UnitKind::Recorder,RecUnit::P_LEN);const q15 len=u.params[RecUnit::P_LEN].base;
    audit_lens_tap(286,108);param_lens_touch_down(150,129);param_lens_touch_drag(280,129);param_lens_touch_release();
    CHECK(u.params[RecUnit::P_LEN].base==len);f.capture();audit_lens_bottom();CHECK(module_inspector_text("busy"));
    param_lens_touch_down(150,166);param_lens_touch_drag(160,166);param_lens_touch_release();
    CHECK(u.params[RecUnit::P_LEN].mod_depth==10*65534/271);param_lens_cancel();input::down(118,170);input::up();
}
void test_audit_muted_inspectors() {
    for(UnitKind kind:{UnitKind::Filter,UnitKind::Fm,UnitKind::Reverb,UnitKind::Ghost,UnitKind::Stranular,
        UnitKind::Vca,UnitKind::Fract,UnitKind::Osc,UnitKind::Motor,UnitKind::Shift,UnitKind::Bird,
        UnitKind::Comp,UnitKind::Mouth,UnitKind::Arp,UnitKind::Glitch}) {
        ModuleInspectorFixture f(kind);audit_select(kind,0);auto& p=f.module->params[0];
        p.src=ModSource::SigilH;p.mod_depth=-32767;p.mod_enabled=false;p.snap();
        f.capture();if(kind==UnitKind::Fm)fm_context_capture(0);
        const auto draw_route=[&]() {
            module_inspector_surface(kind!=UnitKind::Comp);
            {ModuleInspectorNoEngine no;
                if(kind==UnitKind::Comp)comp_focus_draw(*static_cast<CompUnit*>(f.module));
                else top_draw();
            }
            // Modern module models use intentional tint/AA colours outside the
            // old REV-only palette oracle; retain actual bounds/overlap checks.
            cleanup_geometry();return pixels;
        };
        const auto frozen=draw_route();
        if(!module_inspector_text("muted"))std::fprintf(stderr,"missing muted inspector: %s\n",unit_kind_name(kind));
        CHECK(module_inspector_text("muted"));p.mod_enabled=true;
        CHECK(draw_route()==frozen);
        param_lens_cancel();
    }
}
int run_module_audit() {
    int total=0,failed=0;
    const auto run=[&](const char* name,auto test){++total;try{test();std::printf("ok: %s\n",name);}catch(const std::exception& e){++failed;std::printf("FAIL: %s: %s\n",name,e.what());}};
    run("REC save/failure/cancel + frozen choice + real lift",test_audit_rec_choices);
    run("REC discard only after target commit succeeds",test_audit_rec_discard_failure);
    run("REC keys/navigation/context/take identity cancel",test_audit_rec_cancel_ownership);
    run("REC core no implicit unsaved reset + stale/pending tokens",test_audit_rec_core_tokens);
    run("MOD six formerly absolute editors: no jump + muted/re-enable",test_audit_lens_relative_muted);
    run("MOD musical fine + exact zero/unity/min + no bypass lockout",test_audit_lens_fine_neutral);
    run("REC busy base is disabled, route remains editable",test_audit_rec_lens_busy);
    run("muted selected inspectors / frozen status",test_audit_muted_inspectors);
    run("FRACT inactive rails stay editable, grab unavailable",test_fract_bypass_mute);
    run("FOLD hollow edit / solid live",test_fold_marker_layers);
    run("RING hollow edit / solid live",test_ring_marker_layers);
    std::printf("module audit: %d/%d passed (host only; no device sound/fps claim)\n",total-failed,total);
    return failed?1:0;
}
} // namespace
