// Exact main save/load functions + real REC/patchio/WAV paths in the existing
// host harness. Only platform lifecycle/recents are shims; no device/FPS claim.
#define SEANCE_SAMPLE_BROWSER_TEST
#define SEANCE_PATCH_MENU_TEST
#define main inherited_menu_main
#include "menu_stage1_test.cpp"
#undef main

namespace project_audio_test {
using namespace seance::platform::ui3ds;
using Bank = trackr::synth::SampleBank;
using Result = trackr::synth::SampleEditResult;
ProjectSession g_project_session;
const char* g_patch_save_error = nullptr;
int remembered = 0, g_loaded_samples = 0, g_sample_flash = 0;
bool fail_patch_path = false;
RecUnit* during_write = nullptr;
const char* during_suffix = nullptr;
bool finish_during_write = false;
void patch_path(int slot, char* out, size_t n) {
    std::snprintf(out,n,fail_patch_path ? "missing/project_%d.snc" : "project_%d.snc",slot);
}
void remember_project(int) { ++remembered; }
void performance_view_reset() {}
namespace transition { void forget_launch_tracking() {} } // lifecycle shim for exact main load
void sequencer_forget_pins() {}
void patchbay_forget() {}
void sigils_forget_undo() {}
void nav_close() { seance::platform::ui3ds::nav_close(); }
#include <audit_project_main.inc>

struct AudioFixture : sample_browser_test::BrowserFixture {
    RecUnit* rec = nullptr;
    AudioFixture() {
        remembered=0;fail_patch_path=false;g_patch_save_error=nullptr;
        during_write=nullptr;during_suffix=nullptr;finish_during_write=false;
        g_project_session.reset_new();g_project_session.mark_mutated();
        rec=static_cast<RecUnit*>(make_unit(UnitKind::Recorder));
        engine->chains[0].units[1]=rec;
        CHECK(engine->prepare_recorder(*rec,true)==Result::Ok);
    }
    ~AudioFixture() { during_write=nullptr;during_suffix=nullptr; }
    void take() {
        rec->trigger();q15 pcm[64];std::fill(std::begin(pcm),std::end(pcm),q15(2468));
        rec->capture_input(pcm,64);rec->stop();CHECK(rec->take_needs_save());
    }
};
size_t files(const char* path) {
    return size_t(std::distance(std::filesystem::directory_iterator(path),std::filesystem::directory_iterator()));
}
void save_recording() {
    AudioFixture f;
    CHECK(!g_project_session.request(DestructiveAction::ExitApp));
    f.rec->trigger(); // even zero recorded frames must not be reported saved
    CHECK(f.rec->is_recording() && !save_current_patch(10));
    CHECK(!std::filesystem::exists("project_10.snc") && files(RECORDING_DIR)==0);
    CHECK(g_project_session.dirty() && g_project_session.has_pending() && remembered==0);
    CHECK(std::string(g_patch_save_error)=="stop rec before saving");
    q15 pcm[64];std::fill(std::begin(pcm),std::end(pcm),q15(2468));f.rec->capture_input(pcm,64);
    const auto take=f.rec->take_generation();
    CHECK(!save_current_patch(10) && f.rec->is_recording() && f.rec->take_generation()==take);
    f.rec->stop();
    const char* original_dir=RECORDING_DIR;RECORDING_DIR="missing/recordings";
    CHECK(!save_current_patch(10) && f.rec->take_needs_save() && g_project_session.dirty());
    CHECK(g_project_session.has_pending() && g_edit_chain==0 && g_edit_slot==0 && remembered==0);
    RECORDING_DIR=original_dir;
    fail_patch_path=true;CHECK(!save_current_patch(10)); // WAV committed, SNC rejected
    CHECK(!f.rec->take_needs_save() && files(RECORDING_DIR)==1 && g_project_session.dirty());
    CHECK(g_project_session.has_pending() && remembered==0);
    fail_patch_path=false;CHECK(save_current_patch(10));
    CHECK(!g_project_session.dirty() && g_project_session.active_slot()==10 && remembered==1);
    CHECK(files(RECORDING_DIR)==1 && g_edit_chain==0 && g_edit_slot==0);
    auto restored=std::make_unique<Engine>();patchio::SampleManifest manifest;
    CHECK(patchio::load(*restored,"project_10.snc",&manifest));
    CHECK(std::filesystem::exists(manifest.slots[f.rec->sample_slot].path));
    std::puts("pass save REC: zero/partial active blocked, export/SNC failures stay dirty, pending leave retained, retry reuses exported WAV");
}
void pending_recording() {
    AudioFixture f;f.engine->transport_play();
    CHECK(f.engine->request_recorder_record(0,1,true,LaunchQuantize::Bar)==Result::Ok);
    CHECK(f.engine->recorder_record_pending(0,1) && !save_current_patch(11));
    CHECK(g_project_session.dirty() && files(RECORDING_DIR)==0 && remembered==0);
    f.engine->transport_stop();f.engine->transport_play();
    CHECK(f.engine->request_chain_record(0,true,LaunchQuantize::Bar)==Result::Ok);
    CHECK(f.engine->pending_rec_start && !save_current_patch(11));
    CHECK(!std::filesystem::exists("project_11.snc") && remembered==0);
    f.engine->transport_stop();CHECK(save_current_patch(11));
    std::puts("pass save pending: quantized unit/chain REC blocked; cancelled command permits save");
}
void late_recording() {
    // Real writer rename boundary injects a new take while save has released its
    // audio guard. No fake success result or alternate save algorithm.
    for(bool finish:{false,true}) {
        AudioFixture f;g_project_session.reset_loaded(1);
        during_write=f.rec;during_suffix=".snc";finish_during_write=finish;
        CHECK(!save_current_patch(12));
        CHECK(!during_write && remembered==0 && g_project_session.dirty());
        CHECK(g_project_session.active_slot()==1);
        CHECK(finish ? f.rec->take_needs_save() : f.rec->is_recording());
        CHECK(std::string(g_patch_save_error)=="rec changed / retry save");
    }
    {
        AudioFixture f;f.take();
        auto* second=static_cast<RecUnit*>(make_unit(UnitKind::Recorder));
        f.engine->chains[1].units[0]=second;CHECK(f.engine->prepare_recorder(*second,true)==Result::Ok);
        during_write=second;during_suffix=".wav";
        CHECK(!save_current_patch(13) && !during_write);
        CHECK(!std::filesystem::exists("project_13.snc") && g_project_session.dirty() && remembered==0);
        CHECK(std::string(g_patch_save_error)=="finish rec / retry save");
    }
    std::puts("pass save revalidation: starts during WAV export or SNC write, including completed new take, never mark clean");
}
void restore_shared_cuts() {
    AudioFixture f;f.wav("shared.wav",512);
    const std::string wav=std::string(f.directory)+"/shared.wav";
    auto sample=sample_browser_test::source(512);sample.exact_native_endpoint=true;
    const int slot=f.rec->sample_slot;
    CHECK(Bank::instance().replace_sample(slot,std::move(sample),trackr::synth::SampleOrigin::Recording,wav.c_str())==Result::Ok);
    auto* play=static_cast<PlayUnit*>(f.engine->chains[0].units[0]);play->sample_slot=slot;
    f.rec->adopt_take(512);
    auto& s=Bank::instance().slot(slot);
    s.loop_start=23;s.loop_end=470;s.reversed=true;
    s.chops[0]=64;s.chops[1]=192;s.chops[2]=400;s.slice_reverse_mask=5;
    s.slice_speed_q8[0]=640;s.slice_speed_q8[1]=128;s.slice_speed_q8[2]=1024;
    const auto before=s;const auto generation=Bank::instance().generation(slot);
    f.rec->restore_take(99999);
    CHECK(f.rec->recorded_frames()==512 && !f.rec->is_recording() && !f.rec->take_needs_save());
    CHECK(s.data==before.data && s.loop_start==23 && s.loop_end==470 && s.reversed);
    CHECK(s.logical_frames==before.logical_frames && s.exact_native_endpoint==before.exact_native_endpoint);
    CHECK(!std::memcmp(s.chops,before.chops,sizeof s.chops) && s.slice_reverse_mask==5);
    CHECK(!std::memcmp(s.slice_speed_q8,before.slice_speed_q8,sizeof s.slice_speed_q8));
    CHECK(Bank::instance().generation(slot)==generation && std::string(Bank::instance().provenance(slot).path)==wav);
    CHECK(save_current_patch(14));
    CHECK(transactional_patch_load("project_14.snc"));
    const auto& loaded=Bank::instance().slot(slot);
    auto* loaded_rec=static_cast<RecUnit*>(f.engine->chains[0].units[1]);
    CHECK(loaded_rec->sample_slot==slot && loaded_rec->recorded_frames()==512 && !loaded_rec->take_needs_save());
    CHECK(sample_slot_for_unit(f.engine->chains[0].units[0])==slot && g_loaded_samples==1);
    CHECK(loaded.data==before.data && !std::memcmp(loaded.chops,before.chops,sizeof loaded.chops));
    CHECK(loaded.slice_reverse_mask==5 && !std::memcmp(loaded.slice_speed_q8,before.slice_speed_q8,sizeof loaded.slice_speed_q8));
    CHECK(save_current_patch(15) && transactional_patch_load("project_15.snc"));
    CHECK(Bank::instance().slot(slot).chops[0]==64 && Bank::instance().slot(slot).slice_reverse_mask==5);
    std::puts("pass shared REC/PLAY restore: runtime-only adoption preserves metadata, real SNC/WAV load-resave keeps first cut/reverse/rates/PCM");
}
} // namespace project_audio_test

extern "C" int __real_rename(const char*,const char*);
extern "C" int __wrap_rename(const char* from,const char* to) {
    const int result=__real_rename(from,to);
    using namespace project_audio_test;
    if(!result && during_write && during_suffix) {
        const size_t n=std::strlen(to),suffix=std::strlen(during_suffix);
        if(n>=suffix && !std::strcmp(to+n-suffix,during_suffix)) {
            RecUnit* rec=during_write;during_write=nullptr;rec->trigger();
            if(finish_during_write){q15 pcm[16]={1234};rec->capture_input(pcm,16);rec->stop();}
        }
    }
    return result;
}

int main(){try{
    theme_test::select();
    project_audio_test::save_recording();project_audio_test::pending_recording();
    project_audio_test::late_recording();project_audio_test::restore_shared_cuts();
    test_sample_browser(nullptr);test_patch_menu();
    std::puts("audit audio/projects: 4 core + 5 browser + 6 project groups passed; no media/device claims");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"audit FAIL: %s\n",e.what());return 1;}}
