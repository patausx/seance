// Included by the existing menu-stage1 harness. Actual runtime implementation,
// including private controller state for fixture setup; no copied UI or decoder.
#include "../platform/3ds/ui_sample_library.cpp"
#include <cstdlib>
namespace seance::platform::ui3ds {
int g_sample_page=0, g_sample_browser_mode=0, g_sample_file_count=0, g_sample_file_page=0;
char g_sample_files[MAX_SAMPLE_FILES][SAMPLE_NAME_LEN] = {};
const char* SAMPLE_DIR=""; const char* RECORDING_DIR="";
int g_export_flash=0; bool g_export_ok=false, g_export_is_load=false;
}
namespace {
namespace sample_browser_test {
using namespace seance::platform::ui3ds;
using trackr::synth::Sample;
using trackr::synth::SampleBank;
using trackr::synth::SampleOrigin;
int peak=0, pairs=0;
Sample source(int frames, int channels=1) {
    Sample s; s.channels=channels; s.data.resize(size_t(frames)*channels);
    for(int i=0;i<frames;++i) for(int ch=0;ch<channels;++ch) {
        // Deterministic real PCM fixture, not a UI wave drawing.
        const double env=std::exp(-double(i)/std::max(1,frames)*4.0);
        s.data[i*channels+ch]=q15(30000*env*std::sin(i*0.19+ch*0.6));
    }
    return s;
}
void settle() { for(int i=0;i<8;++i) sample_browser_tick(); }
void tap(int x,int y,bool commit=true) {
    sample_browser_touch_down(x,y);sample_browser_touch_release(commit);settle();
}
void button(uint32_t key) { sample_browser_buttons(key);settle(); }
int index_named(const char* name) {
    for(int i=0;i<g_sample_file_count;++i) if(!std::strcmp(g_sample_files[i],name)) return i;
    return -1;
}
int index_slot(int slot) {
    for(int i=0;i<g_sample_file_count;++i) if(browser_entries[i].slot==slot) return i;
    return -1;
}
int row_y(int index) {
    const int y=56+index*24-g_sample_scroll_q8/256+12;
    CHECK(index>=0&&index<g_sample_file_count&&y>=56&&y<200);return y;
}
struct BrowserFixture : Fixture {
    char directory[64]="/tmp/seance_samples_XXXXXX",recordings[96]{};
    BrowserFixture() {
        CHECK(mkdtemp(directory));SAMPLE_DIR=directory;
        // Separate root without adding a visible library-directory fixture.
        std::snprintf(recordings,sizeof recordings,"%s/.recordings",directory);
        CHECK(std::filesystem::create_directory(recordings));RECORDING_DIR=recordings;
        for(int i=0;i<64;++i) SampleBank::instance().clear_slot(i);
        engine->chains[0].units[0]=make_unit(UnitKind::SamplePlayer);
        browser_owner=0;browser_store_loaded=false;browser_location_next=0;
        for(auto& location:browser_locations) location={};
        std::memset(browser_directories,0,sizeof browser_directories);
        browser_pending=0;browser_pending_play=false;browser_menu=0;
        g_overlay=Overlay::SampleBrowser;g_sample_page=0;g_sample_browser_mode=0;
        g_sample_relative_dir[0]=0;g_sample_slot_selected=-1;g_sample_slot_previewing=-1;
        scan_sample_files();
    }
    ~BrowserFixture() {
        sample_browser_touch_release(false);sample_browser_stop_preview();
        for(int i=0;i<64;++i) SampleBank::instance().clear_slot(i);
        std::filesystem::remove_all(directory);
        SAMPLE_DIR=RECORDING_DIR="";browser_owner=0;
    }
    void put(int slot, int frames=1024, int channels=1, bool missing=false) {
        Sample s=source(frames,channels);char path[192];
        std::snprintf(path,sizeof path,"%s/slot_%02d.wav",directory,slot);
        CHECK(SampleBank::instance().replace_sample(slot,std::move(s),SampleOrigin::SdWav,
              path,missing)==trackr::synth::SampleEditResult::Ok);
    }
    void wav(const char* name,int frames=32000,int channels=1,bool recording=false) {
        Sample s=source(frames,channels);
        CHECK(trackr::synth::save_sample_wav((std::string(recording?recordings:directory)+"/"+name).c_str(),s,ENGINE_SR)==trackr::synth::WavSaveResult::Ok);
    }
};
std::pair<Frame,Frame> pair(bool frozen=true) {
    sample_browser_capture();
    auto top=render(400,sample_browser_draw_top),bottom=render(320,sample_browser_draw);
    palette(top);palette(bottom);paper_edges(top);paper_edges(bottom);
    CHECK(top.objects+bottom.objects<8192);peak=std::max(peak,top.objects+bottom.objects);++pairs;
    if(frozen) {
        auto* engine=g_eng;auto* audio=g_audio;
        const int mode=g_sample_browser_mode,selected=g_sample_selected,count=g_sample_file_count;
        const int slot=g_sample_slot_selected,menu=browser_menu,scroll=g_sample_scroll_q8;
        const char name=g_sample_files[0][0],notice=browser_notice[0];
        const bool automatic=browser_store.auto_preview;
        const auto wave=g_sample_file_wave;
        g_eng=nullptr;g_audio=nullptr;g_sample_browser_mode=(mode+1)%4;
        g_sample_selected=127;g_sample_slot_selected=63;g_sample_file_count=0;
        browser_menu=3;g_sample_scroll_q8=12345;g_sample_files[0][0]='!';
        browser_notice[0]='!';browser_store.auto_preview=!automatic;g_sample_file_wave={};
        CHECK(render(400,sample_browser_draw_top).pixels==top.pixels);
        CHECK(render(320,sample_browser_draw).pixels==bottom.pixels);
        g_eng=engine;g_audio=audio;g_sample_browser_mode=mode;g_sample_selected=selected;
        g_sample_slot_selected=slot;g_sample_file_count=count;browser_menu=menu;
        g_sample_scroll_q8=scroll;g_sample_files[0][0]=name;browser_notice[0]=notice;
        browser_store.auto_preview=automatic;g_sample_file_wave=wave;
    }
    return {top,bottom};
}
void test_wave() {
    for(int frames:{1,2,17,351,352,353,4097,640000}) for(int channels:{1,2}) {
        auto s=source(frames,channels);SampleWaveView wave;sample_wave_capture(s,wave);
        CHECK(wave.frames==unsigned(frames)&&wave.channels==channels);
        for(int x=0;x<SAMPLE_WAVE_COLUMNS;++x) {
            const int first=uint64_t(x)*frames/SAMPLE_WAVE_COLUMNS;
            const int end=std::max(first+1,int(uint64_t(x+1)*frames/SAMPLE_WAVE_COLUMNS));
            int lo=32767,hi=-32768;
            for(int probe=0;probe<8;++probe) for(int ch=0;ch<channels;++ch) {
                const int v=s.data[(first+uint64_t(probe)*(end-first-1)/7)*channels+ch];
                lo=std::min(lo,v);hi=std::max(hi,v);
            }
            auto px=[](int v){return int(std::round(double(v)*50/32768));};
            CHECK(wave.low[x]==px(lo)&&wave.high[x]==px(hi));
        }
    }
    Sample s;s.channels=2;s.data.assign(704,0);SampleWaveView wave;
    sample_wave_capture(s,wave);for(int x=0;x<352;++x) CHECK(wave.low[x]==0&&wave.high[x]==0);
    for(int x=0;x<352;++x) {s.data[x*2]=32767;s.data[x*2+1]=-32768;}
    sample_wave_capture(s,wave);for(int x=0;x<352;++x) CHECK(wave.low[x]==-50&&wave.high[x]==50);
    s.logical_frames=1;sample_wave_capture(s,wave);CHECK(wave.frames==1);
    s.logical_frames=0;sample_wave_capture(s,wave);CHECK(wave.frames==0);
    std::puts("pass sample wave: 16 PCM probe oracles, stereo union, silence, full scale and logical length");
}
void test_slots() {
    BrowserFixture f;f.put(0);f.put(7,4096,2);f.put(63);
    SampleBank::instance().set_provenance(12,SampleOrigin::SdWav,"/missing.wav",true);
    browser_switch_source(1);settle();CHECK(g_sample_file_count==4);
    CHECK(index_slot(0)==0&&index_slot(7)==1&&index_slot(12)==2&&index_slot(63)==3);
    auto* play=static_cast<PlayUnit*>(edited_unit());const int assigned=play->sample_slot,before=mutations;
    sample_browser_touch_down(80,row_y(1));
    CHECK(g_sample_selected==-1&&!g_eng->sample_preview_active()&&play->sample_slot==assigned);
    sample_browser_touch_release(true);settle();
    CHECK(g_sample_selected==1&&g_sample_slot_previewing==7&&g_eng->sample_preview_active());
    CHECK(play->sample_slot==assigned&&mutations==before);
    const auto stopped_at_zero=pair();
    CHECK(sample_frame.assigned==assigned&&sample_frame.selected==1&&sample_frame.playing);
    CHECK(sample_frame.wave.frames==4096&&sample_frame.wave.channels==2&&sample_frame.position==0);
    std::array<q15,256> audio{};f.engine->render(audio.data(),128);
    // No recapture: advancing actual playback cannot mutate either frozen draw.
    CHECK(render(400,sample_browser_draw_top).pixels==stopped_at_zero.first.pixels);
    CHECK(render(320,sample_browser_draw).pixels==stopped_at_zero.second.pixels);
    pair();CHECK(sample_frame.position==128&&sample_frame.playing);
    { platform::AudioOut::Guard guard(f.audio);
      CHECK(f.engine->sample_preview_position()==128&&f.engine->sample_preview_frames()==4096); }
    button(KEY_Y);pair();CHECK(!sample_frame.playing&&sample_frame.wave.frames==4096);
    button(KEY_Y);CHECK(g_eng->sample_preview_active());
    tap(80,row_y(0),false);CHECK(g_sample_selected==1&&mutations==before);
    sample_browser_touch_down(80,row_y(0));sample_browser_touch_hold(80,row_y(0)+30);
    sample_browser_touch_release(true);settle();CHECK(g_sample_selected==1&&mutations==before);
    // Generation changed after selection: neither stale Use nor missing PCM may assign.
    f.put(7,2048);CHECK(!sample_browser_assign_selected_slot());
    CHECK(play->sample_slot==assigned&&mutations==before&&g_overlay==Overlay::SampleBrowser);
    browser_switch_source(1);browser_select(index_slot(12),false);settle();pair();
    CHECK(!sample_frame.wave.frames&&!sample_browser_assign_selected_slot());
    CHECK(play->sample_slot==assigned&&mutations==before);
    browser_select(index_slot(63),true);settle();CHECK(sample_browser_assign_selected_slot());
    CHECK(play->sample_slot==63&&g_overlay==Overlay::Editor&&!g_eng->sample_preview_active());
    g_overlay=Overlay::SampleBrowser;scan_sample_files();settle();
    auto* other=static_cast<PlayUnit*>(make_unit(UnitKind::SamplePlayer));other->sample_slot=63;
    f.engine->chains[1].units[0]=other;pair();CHECK(!sample_frame.can_delete);
    const int protected_before=mutations;
    CHECK(!delete_sample_slot_for_owner(63,play));
    CHECK(!SampleBank::instance().slot(63).empty()&&mutations==protected_before);
    delete f.engine->replace_unit(1,0,nullptr);pair();CHECK(sample_frame.can_delete);
    tap(162,214);CHECK(browser_menu==2);tap(80,64);CHECK(browser_menu==3&&browser_menu_selected==1);
    button(KEY_A);CHECK(!SampleBank::instance().slot(63).empty()); // confirm defaults to cancel
    tap(162,214);tap(80,64);CHECK(browser_menu==3);
    f.put(63,2048);tap(80,64);CHECK(!SampleBank::instance().slot(63).empty()); // stale confirmation
    scan_sample_files();browser_select(index_slot(63),false);settle();
    tap(162,214);tap(80,64);CHECK(browser_menu==3);tap(80,64);
    CHECK(SampleBank::instance().slot(63).empty());
    CHECK(g_sample_file_count==3&&index_slot(63)==-1);pair();
    tap(288,16,false);CHECK(g_overlay==Overlay::SampleBrowser);
    tap(288,16);CHECK(g_overlay==Overlay::Editor&&!g_eng->sample_preview_active());
    // REC imports into a private destination, never blanks a selected project
    // source. Also exercise shared current ownership and stereo logical length.
    f.put(7,4096,2);
    auto* rec=static_cast<RecUnit*>(make_unit(UnitKind::Recorder));rec->sample_slot=0;
    delete f.engine->replace_unit(0,0,rec);
    auto* shared=static_cast<PlayUnit*>(make_unit(UnitKind::SamplePlayer));shared->sample_slot=0;
    f.engine->chains[1].units[0]=shared;
    g_overlay=Overlay::SampleBrowser;scan_sample_files();
    auto& bank=SampleBank::instance();
    const auto source_pcm=bank.slot(7).data,original_pcm=bank.slot(0).data;
    const auto generation=bank.generation(7),original_generation=bank.generation(0);
    const std::string source_path=bank.provenance(7).path;
    browser_select(index_slot(7),false);settle();
    CHECK(sample_browser_assign_selected_slot()&&g_overlay==Overlay::Editor);
    CHECK(rec->sample_slot!=7&&rec->sample_slot!=0);
    CHECK(bank.slot(rec->sample_slot).data==source_pcm&&bank.slot(rec->sample_slot).channels==2);
    CHECK(bank.slot(rec->sample_slot).playable_frames()==4096);
    CHECK(bank.slot(7).data==source_pcm&&bank.generation(7)==generation);
    CHECK(bank.slot(0).data==original_pcm&&bank.generation(0)==original_generation);
    CHECK(std::string(bank.provenance(7).path)==source_path);
    const int after_copy=mutations;
    g_overlay=Overlay::SampleBrowser;scan_sample_files();
    browser_select(index_slot(rec->sample_slot),false);settle();
    const auto take_generation=rec->take_generation();
    CHECK(sample_browser_assign_selected_slot()&&mutations==after_copy&&rec->take_generation()==take_generation);
    auto* writer=static_cast<RecUnit*>(make_unit(UnitKind::Recorder));writer->sample_slot=7;
    f.engine->chains[2].units[0]=writer;
    g_overlay=Overlay::SampleBrowser;scan_sample_files();browser_select(index_slot(7),false);settle();
    CHECK(!sample_browser_assign_selected_slot()&&mutations==after_copy);
    CHECK(bank.slot(7).data==source_pcm&&bank.generation(7)==generation);
    std::puts("pass project: release-only use, real progress, stale/protected removal and non-destructive private REC copy");
}
void test_files() {
    BrowserFixture f;f.wav("kick.wav");f.wav("stereo.wav",2048,2);f.wav("take.wav",4096,1,true);
    std::ofstream(std::string(f.directory)+"/broken.wav")<<"invalid";
    CHECK(std::filesystem::create_directory(std::string(f.directory)+"/drums"));scan_sample_files();
    CHECK(g_sample_file_count==4&&g_sample_file_is_dir[0]);pair();const int before=mutations;
    // Cancel does not open directories; a committed row opens on release.
    tap(80,row_y(index_named("drums")),false);CHECK(!g_sample_relative_dir[0]);
    tap(80,row_y(index_named("drums")));CHECK(std::string(g_sample_relative_dir)=="drums");pair();
    button(KEY_B);CHECK(!g_sample_relative_dir[0]&&g_overlay==Overlay::SampleBrowser);
    CHECK(g_sample_selected==index_named("drums"));
    const int kick=index_named("kick.wav"),stereo=index_named("stereo.wav"),broken=index_named("broken.wav");
    sample_browser_touch_down(80,row_y(kick));CHECK(!g_eng->sample_preview_active());
    sample_browser_touch_release(true);settle();CHECK(g_sample_selected==kick&&g_eng->sample_preview_active());
    pair();CHECK(sample_frame.wave.frames==32000&&sample_frame.wave.channels==1&&mutations==before);
    tap(294,row_y(kick),false);CHECK(browser_store.count==0);
    tap(294,row_y(kick));CHECK(browser_store.count==1&&g_sample_selected==kick&&mutations==before);
    tap(294,row_y(index_named("drums")));CHECK(browser_store.count==2&&mutations==before);
    tap(60,233,false);CHECK(browser_store.auto_preview);
    tap(60,233);CHECK(!browser_store.auto_preview&&mutations==before);
    CHECK(std::filesystem::exists(browser_store_path));
    browser_store_loaded=false;scan_sample_files();settle();
    CHECK(browser_store.count==2&&!browser_store.auto_preview&&mutations==before);
    sample_browser_stop_preview();browser_select(kick,false);settle();button(KEY_DOWN);
    CHECK(g_sample_selected==stereo&&!g_eng->sample_preview_active());
    tap(80,row_y(kick));CHECK(g_eng->sample_preview_active()); // row tap ignores auto off
    button(KEY_Y);CHECK(!g_eng->sample_preview_active());
    tap(60,233);button(KEY_DOWN);CHECK(browser_store.auto_preview&&g_eng->sample_preview_active());
    pair();CHECK(sample_frame.wave.channels==2&&mutations==before);
    tap(80,row_y(broken));pair();CHECK(!sample_frame.wave.frames&&!sample_frame.playing);
    CHECK(std::string(browser_notice)=="invalid or incomplete wav");
    CHECK(!text_has(render(400,sample_browser_draw_top),"other preview"));
    CHECK(!sample_browser_load_selected()&&mutations==before&&g_overlay==Overlay::SampleBrowser);
    // App-side failed save cannot publish the draft or mark the project dirty.
    char saved_path[sizeof browser_store_path];std::memcpy(saved_path,browser_store_path,sizeof saved_path);
    std::snprintf(browser_store_path,sizeof browser_store_path,"%s/absent/preferences.bin",f.directory);
    const bool automatic=browser_store.auto_preview;const int stars=browser_store.count;
    tap(60,233);tap(294,row_y(stereo));
    CHECK(browser_store.auto_preview==automatic&&browser_store.count==stars&&mutations==before);
    std::memcpy(browser_store_path,saved_path,sizeof saved_path);
    // Source menu uses the approved order, not internal source enum ordering.
    tap(42,44,false);CHECK(browser_menu==0);
    tap(42,44);CHECK(browser_menu==1);pair();tap(80,92);
    CHECK(g_sample_browser_mode==2&&g_sample_file_count==2);pair();
    CHECK(std::filesystem::remove(std::string(f.directory)+"/kick.wav"));
    tap(80,row_y(index_named("kick.wav")));pair();
    CHECK(!sample_frame.wave.frames&&!sample_frame.playing&&browser_store.count==2);
    CHECK(std::string(browser_notice)=="file missing or unreadable");
    f.wav("kick.wav");browser_select(index_named("drums"),false);settle();
    CHECK(sample_browser_load_selected());CHECK(g_sample_browser_mode==0&&std::string(g_sample_relative_dir)=="drums");
    CHECK(g_overlay==Overlay::SampleBrowser);button(KEY_LEFT);CHECK(!g_sample_relative_dir[0]);
    browser_select(index_named("kick.wav"),false);settle();
    const int remembered=g_sample_selected;
    button(KEY_R);CHECK(browser_menu==1);tap(80,148);
    CHECK(g_sample_browser_mode==3&&g_sample_file_count==1&&index_named("take.wav")==0);
    tap(80,row_y(0));pair();CHECK(sample_frame.wave.frames==4096);
    browser_switch_source(0);settle();CHECK(g_sample_selected==remembered&&!g_eng->sample_preview_active());
    CHECK(mutations==before);
    // Interrupt a queued Use after its loading frame. A source menu must never
    // allow that queued load to commit behind it or restart on closing the menu.
    CHECK(sample_browser_buttons(KEY_A));sample_browser_tick();
    CHECK(browser_pending==1&&browser_pending_use);pair();
    CHECK(sample_browser_buttons(KEY_R));settle();
    CHECK(browser_menu==1&&!browser_pending&&!browser_pending_use&&!browser_pending_play);
    CHECK(g_overlay==Overlay::SampleBrowser&&mutations==before&&!g_eng->sample_preview_active());
    pair();CHECK(!sample_frame.pending&&!sample_frame.notice[0]);
    button(KEY_B);CHECK(!browser_menu&&g_overlay==Overlay::SampleBrowser&&mutations==before);
    sample_browser_touch_down(270,214);CHECK(g_overlay==Overlay::SampleBrowser&&mutations==before);
    sample_browser_touch_release(false);settle();CHECK(g_overlay==Overlay::SampleBrowser&&mutations==before);
    tap(270,214);CHECK(g_overlay==Overlay::Editor&&g_export_ok&&!g_eng->sample_preview_active());
    CHECK(sample_slot_frames(sample_slot_for_unit(edited_unit()))==32000&&mutations>before);
    std::puts("pass files/preferences: real WAV/folders/recordings, persistence, favorites missing paths, auto audition, failed saves and release-only use");
}
void test_clipboard() {
    BrowserFixture f; f.put(0,4096); f.wav("replacement.wav",512);
    auto& bank=SampleBank::instance();
    auto* original=static_cast<PlayUnit*>(edited_unit());
    const auto pcm=bank.slot(0).data;
    const auto generation=bank.generation(0);
    const std::string path=bank.provenance(0).path;
    // Use the real main controller for copy and paste, not just two synthetic owners.
    g_overlay=Overlay::None; chains_capture(); chains_workspace_buttons(KEY_R);
    chains_context_buttons(KEY_DOWN); chains_context_buttons(KEY_A);
    CHECK(g_clip_unit && sample_slot_for_unit(g_clip_unit)==0);
    g_overlay=Overlay::SampleBrowser; scan_sample_files();
    browser_select(index_named("replacement.wav"),false); settle(); tap(270,214);
    CHECK(g_overlay==Overlay::Editor && original->sample_slot!=0);
    CHECK(bank.slot(original->sample_slot).playable_frames()==512);
    CHECK(bank.slot(0).data==pcm && bank.generation(0)==generation && std::string(bank.provenance(0).path)==path);
    g_overlay=Overlay::SampleBrowser; browser_switch_source(kProject);
    browser_select(index_slot(0),false); settle(); sample_browser_capture();
    CHECK(!browser_can_remove() && !sample_frame.can_delete && sample_frame.references==1);
    CHECK(!delete_sample_slot_for_owner(0,original));
    g_overlay=Overlay::None; chains_capture(); chains_workspace_buttons(KEY_R);
    chains_context_buttons(KEY_DOWN); chains_context_buttons(KEY_DOWN); chains_context_buttons(KEY_A);
    CHECK(f.engine->chains[0].units[1] && sample_slot_for_unit(f.engine->chains[0].units[1])==0);
    CHECK(bank.slot(sample_slot_for_unit(f.engine->chains[0].units[1])).data==pcm);

    // Refuse a load when preserving a clipboard source leaves no free slot.
    delete f.engine->replace_unit(0,1,nullptr);
    original->sample_slot=0; g_sel_slot=g_edit_slot=0;
    for(int slot=1;slot<64;++slot) f.put(slot,16);
    g_overlay=Overlay::SampleBrowser; g_sample_browser_mode=kLibrary; scan_sample_files();
    browser_select(index_named("replacement.wav"),false); settle();
    expected_sample_result=trackr::synth::SampleEditResult::NoFreeSlot;
    CHECK(!sample_browser_load_selected() && original->sample_slot==0 && bank.slot(0).data==pcm);
    expected_sample_result=trackr::synth::SampleEditResult::Ok;
    CHECK(std::string(browser_notice)=="project sample bank full");
    delete g_clip_unit; g_clip_unit=nullptr;
    SampleCommitToken token;
    CHECK(capture_sample_token(original,0,token));
    for(UnitKind kind:{UnitKind::SamplePlayer,UnitKind::Grain,UnitKind::Recorder}) {
        g_clip_unit=make_unit(kind);
        if(kind==UnitKind::SamplePlayer) static_cast<PlayUnit*>(g_clip_unit)->sample_slot=0;
        else if(kind==UnitKind::Grain) static_cast<GrainUnit*>(g_clip_unit)->sample_slot=0;
        else static_cast<RecUnit*>(g_clip_unit)->sample_slot=0;
        CHECK(sample_ownership::clipboard_holds(0,g_clip_unit));
        CHECK(sample_slot_ref_count(0,original)==1 && !token_is_current(token));
        CHECK(!delete_sample_slot_for_owner(0,original));
        delete g_clip_unit; g_clip_unit=nullptr;
    }
    CHECK(token_is_current(token));
    CHECK(sample_browser_load_selected() && original->sample_slot==0); // sole owner may replace
    std::puts("pass clipboard: actual copy/WAV/paste keeps old PCM, deletion blocked, full-bank refusal, all sample owners and stale tokens");
}
void file_inventory() {
    g_sample_browser_mode=0;g_sample_file_count=128;g_sample_selected=3;browser_pending=0;browser_menu=0;
    std::snprintf(g_sample_relative_dir,sizeof g_sample_relative_dir,"one/two/three/four/very_long_folder_name");
    for(int i=0;i<128;++i) {
        std::snprintf(g_sample_files[i],SAMPLE_NAME_LEN,"%03d_a_very_long_inventory_sample_name_with_suffix.wav",i);
        std::snprintf(browser_entries[i].path,sizeof browser_entries[i].path,"/unavailable/%03d_sample.wav",i);
        browser_entries[i].slot=-1;browser_entries[i].generation=0;
        g_sample_file_is_dir[i]=i<2;g_sample_file_info[i]={uint32_t((i+1)*32000),32000,true,true};
    }
}
void test_layout() {
    BrowserFixture f;file_inventory();
    CHECK(sizeof(sample_frame.rows)/sizeof(sample_frame.rows[0])==7);
    for(int offset=0;offset<24;++offset) {sample_browser_set_scroll_q8((40+offset)*256);pair();}
    sample_browser_set_scroll_q8(-2147483647);CHECK(g_sample_scroll_q8==0);pair();
    sample_browser_set_scroll_q8(2147483647);CHECK(g_sample_scroll_q8==122*24*256);pair();
    for(int y:{59,127,196}) {tap(315,y);pair();}
    sample_browser_set_scroll_q8(0);g_sample_selected=3;
    sample_browser_touch_down(80,92);sample_browser_touch_hold(80,124);sample_browser_touch_release();settle();
    CHECK(g_sample_selected==3); // drag never selects or auditions another row
    CHECK(!g_eng->sample_preview_active()&&!browser_touch_action&&!g_sample_touch_list);
    sample_browser_set_scroll_q8(0);tap(80,92,false);CHECK(g_sample_selected==3);
    CHECK(!browser_touch_action&&!g_sample_touch_list&&mutations==0);
    // Source-specific remembered scroll/selection, then a genuinely empty project.
    browser_switch_source(1);settle();CHECK(g_sample_file_count==0&&g_sample_selected==-1);pair();
    CHECK(!sample_browser_assign_selected_slot());
    Engine* e=g_eng;g_eng=nullptr;sample_browser_capture();pair();g_eng=e;
    std::puts("pass layout: all24 row phases, bounded extreme scrollbars, long paths/names, empty context, frozen dual DTOs and cancelled gestures");
}
// Kept opt-in for the existing manual renderer; the test runner uses no output path.
void dump(const char* directory) {
    std::filesystem::path out(directory);std::filesystem::create_directories(out);
    std::ofstream report(out/"frames.tsv");report<<"name\ttop_objects\tbottom_objects\tout_of_bounds\n";
    BrowserFixture f;
    auto save=[&](const char* name) {auto frames=pair();ppm(out/(std::string(name)+"_top.ppm"),frames.first);
        ppm(out/(std::string(name)+"_bottom.ppm"),frames.second);
        report<<name<<'\t'<<frames.first.objects<<'\t'<<frames.second.objects<<'\t'<<frames.first.outside+frames.second.outside<<'\n';};
    f.wav("kick_dust.wav");f.wav("glass_stereo.wav",32000,2);scan_sample_files();
    tap(80,row_y(index_named("kick_dust.wav")));save("01_files_wave");
    for(int i=0;i<12;++i) f.put(i,32000,1);
    browser_switch_source(1);tap(80,row_y(3));save("02_slots_wave");
    button(KEY_Y);save("03_stopped_wave");
    file_inventory();sample_browser_set_scroll_q8(47*256);g_sample_selected=-1;save("04_scrolled_files");
}
} // namespace sample_browser_test
} // namespace
void test_sample_browser(const char* out) {
    using namespace sample_browser_test;
    test_wave();test_slots();test_files();test_clipboard();test_layout();if(out) dump(out);
    std::printf("pass sample-browser 5/5; pairs=%d, peak=%d/8192, frame=%zu bytes, bounds/collisions=0\n",pairs,peak,sizeof(sample_frame));
}
