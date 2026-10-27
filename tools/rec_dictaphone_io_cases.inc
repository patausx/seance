// Additional real REC WAV/project protection on the established actual-main fixture.
namespace project_audio_test {
namespace rec_disk_cases {
void wav_errors_and_retry(){AudioFixture f;f.take();CHECK(!g_project_session.request(DestructiveAction::ExitApp));
    const char*directory=RECORDING_DIR;RECORDING_DIR="missing/recordings";CHECK(!save_current_patch(10)&&f.rec->take_needs_save()&&g_project_session.dirty());RECORDING_DIR=directory;
    fail_patch_path=true;CHECK(!save_current_patch(10)&&!f.rec->take_needs_save()&&files(RECORDING_DIR)==1&&g_project_session.dirty());
    fail_patch_path=false;CHECK(save_current_patch(10)&&!g_project_session.dirty()&&g_project_session.active_slot()==10&&files(RECORDING_DIR)==1);
    CHECK(remembered==0&&g_project_session.has_pending());
    std::puts("pass WAV failure preserves take, WAV-success/SNC-failure partial result, retry reuses WAV; SAVE is not OPEN history");
}
void paused_export_project(){AudioFixture f;g_edit_slot=1;auto&u=*f.rec;
    CHECK(f.engine->request_recorder_command(0,1,RecorderCommand::Start,LaunchQuantize::Immediate)==Result::Ok);
    CHECK(u.pause()&&u.writer_open()&&!u.is_recording());
    g_view=View::Chains;g_overlay=Overlay::Editor;g_sel_chain=0;g_sel_slot=g_edit_slot=1;
    CHECK(!chains_confirm_editor_delete()&&f.engine->chains[0].units[1]==&u&&u.is_paused());
    Unit*alias_before=f.engine->chains[0].units[0];const int source_before=sample_slot_for_unit(alias_before);set_sample_slot_for_unit(alias_before,31);CHECK(sample_slot_for_unit(alias_before)==source_before);
    export_current_sample();CHECK(!g_export_ok&&files(RECORDING_DIR)==0);CHECK(!save_current_patch(20));
    CHECK(!std::filesystem::exists("project_20.snc"));SampleCommitToken token;
    CHECK(!capture_sample_token(&u,31,token));auto preview=sample_browser_test::source(64);f.engine->swap_sample_preview(preview);CHECK(!f.engine->sample_preview_active());
    CHECK(u.resume());q15 first[257];for(int i=0;i<257;++i)first[i]=q15(1000+i*7);u.capture_input(first,257);CHECK(u.pause());
    const auto session=u.session_token();q15 ignored[111];std::fill(std::begin(ignored),std::end(ignored),q15(-17000));u.capture_input(ignored,111);
    CHECK(u.recorded_frames()==257&&u.session_token()==session);export_current_sample();CHECK(!g_export_ok&&files(RECORDING_DIR)==0);
    CHECK(!save_current_patch(20));CHECK(u.resume());q15 last[131];for(int i=0;i<131;++i)last[i]=q15(-9000+i*11);u.capture_input(last,131);u.stop();
    CHECK(u.recorded_frames()==388);CHECK(save_current_patch(20));CHECK(files(RECORDING_DIR)==1&&!u.take_needs_save());
    const int old=u.sample_slot;const std::string wav=Bank::instance().provenance(old).path;const auto original=Bank::instance().slot(old).data;
    trackr::synth::Sample decoded;CHECK(trackr::synth::load_wav_to_sample(wav.c_str(),decoded,ENGINE_SR,ENGINE_SR*20,{0,true})==trackr::synth::WavLoadResult::Ok);
    CHECK(decoded.playable_frames()==388);for(int i=0;i<388;++i)CHECK(decoded.data[i]==(i<257?first[i]:last[i-257]));
    auto*play=static_cast<PlayUnit*>(f.engine->chains[0].units[0]);play->sample_slot=old;play->set_pitch_mode(PlayUnit::PitchMode::Normal);
    RecWorkspacePlan plan;CHECK(f.engine->plan_recorder(u,true,plan)==Result::Ok);CHECK(Engine::stage_recorder(plan)==Result::Ok);
    CHECK(f.engine->commit_recorder_restart(plan,0,1,LaunchQuantize::Immediate)==Result::Ok);CHECK(u.sample_slot!=old&&Bank::instance().slot(old).data==original);
    CHECK(!transactional_patch_load("project_20.snc"));u.capture_input(last,131);CHECK(u.pause());CHECK(!transactional_patch_load("project_20.snc"));u.stop();
    CHECK(save_current_patch(21));CHECK(files(RECORDING_DIR)==2);CHECK(transactional_patch_load("project_21.snc"));
    auto*restored=static_cast<RecUnit*>(f.engine->chains[0].units[1]);auto*alias=static_cast<PlayUnit*>(f.engine->chains[0].units[0]);
    CHECK(alias->sample_slot==old&&restored->sample_slot!=old&&!restored->writer_open()&&restored->recorded_frames()==131);
    CHECK(Bank::instance().slot(old).playable_frames()==388);for(int i=0;i<388;++i)CHECK(Bank::instance().slot(old).data[i]==decoded.data[i]);
    CHECK(std::filesystem::exists(wav));std::puts("pass paused prefix blocked from WAV/SNC/replace/preview, append exact PCM, two-take alias COW real WAV+SNC round trip");
}
}
}
int main(){try{theme_test::select();project_audio_test::rec_disk_cases::wav_errors_and_retry();project_audio_test::pending_recording();project_audio_test::late_recording();project_audio_test::restore_shared_cuts();project_audio_test::rec_disk_cases::paused_export_project();
    std::puts("rec disk: 5/5 real WAV/SNC groups passed / no device claims");return 0;
}catch(const std::exception&e){std::fprintf(stderr,"rec disk FAIL: %s\n",e.what());return 1;}}
