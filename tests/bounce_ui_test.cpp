// Real bounce page, core, WAV writer/loader, navigation and main routing excerpts.
// Reuse the existing native raster; only 3DS/HID/workspace-camera boundaries shimmed.
#include "../platform/3ds/ui_bounce.h"
#include "../platform/3ds/ui_nav.h"
#define main inherited_sequencer_main
#include "sequencer_test.cpp"
#undef main

namespace seance::platform::ui3ds {
bool g_has_dsp=true;
// Clipboard and recording directory are provided by the shared sequencer harness.
int bounce_workspace_resets=0;
void workspace_reset_chain(int) {++bounce_workspace_resets;}
}
namespace bounce_main {
struct touchPosition {int px=0,py=0;} pen;
void hidTouchRead(touchPosition* t){*t=pen;}
int fallthrough=0;
void touch_frame(u32 down,u32 held) {
#include <touch.inc>
    ++fallthrough;
}
bool buttons(u32 down,u32 held) {
    bool stage_skip_touch=false;
#include <buttons.inc>
    CHECK(!(down & ~(KEY_TOUCH|KEY_ZL)));
    return stage_skip_touch;
}
void patch_browser_draw(){CHECK(false);}
void confirm_discard_draw(){CHECK(false);}
void picker_draw(){CHECK(false);}
void chains_view_draw(){CHECK(false);}
void editor_draw(){CHECK(false);}
void sample_browser_draw(){CHECK(false);}
void sequencer_view_draw(){CHECK(false);}
void performance_view_draw(){CHECK(false);}
void sigils_view_draw(){CHECK(false);}
void patchbay_draw(){CHECK(false);}
#include <bottom.inc>
}
namespace {
std::string output;
int pair_peak=0,pairs=0,frozen_pairs=0;
std::vector<u32> draw_bounce(bool top) {
    width=top?400:320;objects=outside_rects=outside_lines=text_overlaps=0;texts.clear();pixels.assign(width*240,0);
    if(top){bounce_draw_top();bounce_draw_indicator();}else bounce_main::draw_bottom_surface(View::Bounce,Overlay::None);
    CHECK(outside_rects==0 && outside_lines==0);
    if(text_overlaps) {for(const auto& t:texts)std::fprintf(stderr,"text %d,%d %s\n",t.x,t.y,t.text.c_str());}
    CHECK(text_overlaps==0);return pixels;
}
void ppm(const std::string& file,const std::vector<u32>& p,int w) {
    std::ofstream out(file,std::ios::binary);out<<"P6\n"<<w<<" 240\n255\n";
    for(u32 c:p){const char rgb[]={char(c&255),char((c>>8)&255),char((c>>16)&255)};out.write(rgb,3);}
}
void pair(const char* name=nullptr) {
    bounce_capture();const auto top=draw_bounce(true);const int topcount=objects;
    const auto bottom=draw_bounce(false);pair_peak=std::max(pair_peak,topcount+objects);CHECK(topcount+objects<=8192);++pairs;
    auto* e=g_eng;auto* a=g_audio;g_eng=nullptr;g_audio=nullptr;
    CHECK(draw_bounce(true)==top);CHECK(draw_bounce(false)==bottom);++frozen_pairs;
    g_eng=e;g_audio=a;
    if(name && !output.empty()){ppm(output+"/"+name+"_top.ppm",top,400);ppm(output+"/"+name+"_bottom.ppm",bottom,320);}
}
void click(int x,int y) {
    bounce_main::pen={x,y};bounce_main::touch_frame(KEY_TOUCH,KEY_TOUCH);bounce_main::touch_frame(0,0);
}
void key(u32 down,u32 held=0){CHECK(bounce_main::buttons(down,held?held:down));}
void render_frames(Engine& e,uint64_t frames) {q15 out[512*2];while(frames){const int n=int(std::min<uint64_t>(frames,511));e.render(out,n);frames-=n;}}
struct BounceFixture : Fixture {
    BounceFixture(){nav_close();g_view=View::Bounce;g_overlay=Overlay::None;g_has_dsp=true;bounce_enter();bounce_workspace_resets=0;click(115,56);}
    ~BounceFixture(){bounce_cancel_input();trackr::synth::Sample old;engine->discard_bounce(old);}
    void arm(){click(50,90);key(KEY_A);CHECK(engine->bounce_snapshot().status==BounceStatus::Armed && engine->sequencer_enabled);}
    void complete(){arm();auto t=engine->bounce_snapshot();render_frames(*engine,t.end_frame-engine->transport_frame+100);CHECK(engine->bounce_snapshot().status==BounceStatus::Complete);}
};
void clear_bank(){auto& b=trackr::synth::SampleBank::instance();for(int i=0;i<64;++i)b.clear_slot(i);b.set_pcm_budget_for_test(b.DEFAULT_PCM_BUDGET_BYTES);}
void input_and_recording() {
    clear_bank();BounceFixture f;pair("01_ready");
    // Simultaneous action+touch cannot leak a second activation from main.
    key(KEY_A|KEY_TOUCH);CHECK(f.engine->bounce_snapshot().active());
    const auto armed=f.engine->bounce_snapshot();pair("02_waiting");
    bounce_main::touch_frame(0,0);CHECK(f.engine->bounce_snapshot().generation==armed.generation);
    key(KEY_B);CHECK(g_view==View::Chains && !nav_is_open());
    nav_open();NavChoice choice;nav_buttons(0,choice);nav_touch_down(220,110,choice);
    CHECK(choice==NavChoice::Bounce && !nav_is_open());g_view=View::Bounce;bounce_enter();
    // The first input actually selected four bars; active recording survives views.
    CHECK(f.engine->transport_playing && armed.start_frame==0);
    render_frames(*f.engine,armed.start_frame+16001);CHECK(f.engine->bounce_snapshot().status==BounceStatus::Recording);
    pair("03_recording");
    g_view=View::Performance;bounce_capture();auto* e=g_eng;g_eng=nullptr;
    width=400;objects=outside_rects=outside_lines=0;pixels.assign(400*240,0);bounce_draw_indicator();CHECK(objects==1);g_eng=e;
    render_frames(*f.engine,armed.target_frames);CHECK(f.engine->bounce_snapshot().status==BounceStatus::Complete);
    g_view=View::Bounce;bounce_enter();pair("04_take");
    const auto complete=f.engine->bounce_snapshot();key(KEY_A);CHECK(f.engine->bounce_snapshot().generation==complete.generation);
    CHECK(!f.engine->transport_playing && f.engine->bounce_preview_active());
    CHECK(bounce_main::fallthrough==0);
}
void save_send_discard() {
    clear_bank();BounceFixture f;f.complete();key(KEY_START);
    CHECK(!f.engine->transport_playing);
    Unit* others[3]={f.engine->chains[1].units[0],f.engine->chains[2].units[0],f.engine->chains[3].units[0]};
    const char* valid=RECORDING_DIR;RECORDING_DIR="/not-present/seance-bounce-test";key(KEY_Y);
    CHECK(!f.engine->bounce_snapshot().saved && f.engine->chains[0].units[0]->kind==UnitKind::Fm);
    RECORDING_DIR=valid;key(KEY_Y);CHECK(f.engine->bounce_snapshot().saved);pair("05_send");
    const auto t=f.engine->bounce_snapshot();const auto& bank=trackr::synth::SampleBank::instance();
    const std::string path=bank.provenance(t.slot).path;CHECK(std::filesystem::exists(path));
    trackr::synth::Sample decoded;trackr::synth::WavDecodeLimits limits;limits.workspace_bytes=trackr::synth::WAV_DECODE_WORKSPACE_BYTES;
    CHECK(trackr::synth::load_wav_to_sample(path.c_str(),decoded,ENGINE_SR,ENGINE_SR*20,limits)==trackr::synth::WavLoadResult::Ok);
    CHECK(decoded.channels==2 && decoded.num_frames()==t.frames);
    // Existing generic same-rate decoder leaves the final frame zero; raw WAV is
    // checked independently below, and all earlier decoded PCM must match.
    CHECK(std::equal(decoded.data.begin(),decoded.data.end()-2,bank.slot(t.slot).data.begin()));
    std::ifstream wav(path,std::ios::binary);std::vector<char> bytes((std::istreambuf_iterator<char>(wav)),{});
    CHECK(bytes.size()==44+size_t(t.frames)*4);
    for(size_t i=0;i<size_t(t.frames)*2;++i){uint16_t v=uint8_t(bytes[44+i*2])|(uint16_t(uint8_t(bytes[45+i*2]))<<8);CHECK(q15(v)==bank.slot(t.slot).data[i]);}
    // Modal B cancels without replacing; repeated save uses same immutable WAV.
    key(KEY_B);key(KEY_X);CHECK(bank.provenance(t.slot).path==path);
    key(KEY_Y);key(KEY_A);CHECK(f.engine->chains[0].units[0]->kind==UnitKind::SamplePlayer && bounce_workspace_resets==1);
    for(int i=1;i<4;++i)CHECK(f.engine->chains[i].units[0]==others[i-1]);
    const auto* play=static_cast<PlayUnit*>(f.engine->chains[0].units[0]);CHECK(play->sample_slot==t.slot && play->params[PlayUnit::P_MODE].base==0);
    // A saved take needs no extra confirmation. Shared PLAY PCM and WAV survive.
    click(160,198);CHECK(f.engine->bounce_snapshot().slot<0 && bank.slot(t.slot).playable_frames()==t.frames);
    CHECK(std::filesystem::exists(path) && !bounce_guard_project_change());
}
void cancellation_and_limits() {
    clear_bank();BounceFixture f;
    // Move away, navigation, wrong engine and held modifier all quarantine touch.
    bounce_main::pen={40,168};bounce_main::touch_frame(KEY_TOUCH,KEY_TOUCH);bounce_main::pen={90,168};bounce_main::touch_frame(0,KEY_TOUCH);bounce_main::touch_frame(0,0);CHECK(f.engine->bounce_snapshot().slot<0);
    bounce_touch_down(40,168);bounce_cancel_input();bounce_touch_release();CHECK(f.engine->bounce_snapshot().slot<0);
    key(KEY_A,KEY_A|KEY_L);CHECK(f.engine->bounce_snapshot().slot<0);
    g_has_dsp=false;key(KEY_A);CHECK(f.engine->bounce_snapshot().slot<0);g_has_dsp=true;
    f.engine->set_clock_mod(ClockTarget::Tempo,ModSource::SigilA,10);key(KEY_A);CHECK(f.engine->bounce_snapshot().slot<0);
    f.engine->set_clock_mod(ClockTarget::Tempo,ModSource::None,0);render_frames(*f.engine,32000);
    // Reset removes the clock route's settling history but doesn't alter test data.
    f.engine->transport_reset();f.arm();
    auto t=f.engine->bounce_snapshot();render_frames(*f.engine,t.start_frame+1234);
    f.engine->set_bpm(130);render_frames(*f.engine,64);t=f.engine->bounce_snapshot();CHECK(t.status==BounceStatus::Partial && t.reason==BounceReason::TempoChanged);
    pair("07_partial");for(const auto& text:texts)CHECK(text.text.find("armed /")==std::string::npos);
    key(KEY_Y);CHECK(!f.engine->bounce_snapshot().saved);
    key(KEY_START);click(274,207);key(KEY_A);CHECK(f.engine->bounce_snapshot().slot<0);
    auto& bank=trackr::synth::SampleBank::instance();bank.set_pcm_budget_for_test(1);key(KEY_A);CHECK(f.engine->bounce_snapshot().slot<0);
    bank.set_pcm_budget_for_test(bank.DEFAULT_PCM_BUDGET_BYTES);
    f.engine->set_bpm(20);render_frames(*f.engine,32000);f.engine->transport_reset();click(270,90);pair();
}
void stale_confirmations() {
    clear_bank();BounceFixture f;f.complete();key(KEY_START);key(KEY_Y);
    auto* before=f.engine->chains[0].units[0];f.engine->chains[0].units[0]=new OscUnit;
    key(KEY_A);CHECK(f.engine->chains[0].units[0]->kind==UnitKind::Osc);delete before;
    // A new unsaved take generation cannot inherit a held release confirmation.
    click(160,198);CHECK(f.engine->bounce_snapshot().slot<0);f.complete();
    click(160,198);bounce_touch_down(40,186);trackr::synth::Sample retired;f.engine->discard_bounce(retired);bounce_touch_release();
    CHECK(f.engine->bounce_snapshot().slot<0);
}
void repeat_bounce_cache() {
    clear_bank();BounceFixture f;std::vector<std::string> paths;
    int previous=-1;
    for(int generation=0;generation<12;++generation) {
        f.arm();const auto armed=f.engine->bounce_snapshot();
        render_frames(*f.engine,armed.end_frame-f.engine->transport_frame+100);key(KEY_START);
        const auto take=f.engine->bounce_snapshot();CHECK(take.status==BounceStatus::Complete);
        key(KEY_Y);key(KEY_A);CHECK(f.engine->chains[0].units[0]->kind==UnitKind::SamplePlayer);
        auto& bank=trackr::synth::SampleBank::instance();paths.push_back(bank.provenance(take.slot).path);
        if(previous>=0)CHECK(!bank.slot(previous).has_storage());
        click(160,198);CHECK(f.engine->bounce_snapshot().slot<0);
        CHECK(bank.pcm_bytes_used()==size_t(take.target_frames)*4);
        for(const auto& p:paths)CHECK(std::filesystem::exists(p));
        previous=take.slot;
    }
    // A clipboard alias is a real hold; replacement must not evict its PCM.
    g_clip_unit=new PlayUnit;static_cast<PlayUnit*>(g_clip_unit)->sample_slot=previous;
    f.arm();const auto armed=f.engine->bounce_snapshot();
    render_frames(*f.engine,armed.end_frame-f.engine->transport_frame+100);key(KEY_START);key(KEY_Y);key(KEY_A);
    CHECK(trackr::synth::SampleBank::instance().slot(previous).has_storage());
    delete g_clip_unit;g_clip_unit=nullptr;
}
bool has_text(const char* text) {
    return std::any_of(texts.begin(),texts.end(),[&](const auto& t){return t.text==text;});
}
void chain_source_and_mono() {
    clear_bank();BounceFixture f;
    // A dragged/cancelled source touch must not change the default master.
    bounce_touch_down(166,56);bounce_touch_hold(206,56);bounce_touch_release();
    bounce_capture();draw_bounce(true);CHECK(has_text("full mix"));
    bounce_touch_down(166,56);bounce_cancel_input();bounce_touch_release();
    bounce_capture();draw_bounce(true);CHECK(has_text("full mix"));
    for(int source=1;source<=4;++source) {
        key(KEY_DOWN);pair();draw_bounce(true);
        const std::string label="chain "+std::to_string(source);CHECK(has_text(label.c_str()));
        f.arm();auto t=f.engine->bounce_snapshot();CHECK(int(t.source)==source);
        CHECK(trackr::synth::SampleBank::instance().slot(t.slot).channels==1);
        click(115,56);key(KEY_UP);CHECK(f.engine->bounce_snapshot().source==t.source);
        CHECK(f.engine->bounce_snapshot().generation==t.generation);
        key(KEY_A);CHECK(f.engine->bounce_snapshot().slot<0); // empty cancellation releases immediately
    }
    key(KEY_DOWN); // clamp to chain 4
    f.engine->chains[3].mute=true;pair();draw_bounce(true);CHECK(has_text("chain muted / records silence"));
    f.engine->chains[3].mute=false;
    // Source 4 can also be chosen directly by touch; other controls stay separate.
    click(115,56);click(286,56);f.engine->chains[3].units[0]->trigger();f.complete();key(KEY_START);
    const auto take=f.engine->bounce_snapshot();CHECK(take.source==BounceSource::Chain4);
    CHECK(take.status==BounceStatus::Complete);click(115,56);bounce_enter();
    const auto theme=palette::current();
    for(auto p:{palette::Theme::Pink,palette::Theme::Graphite}){palette::set(p);pair();draw_bounce(true);CHECK(has_text("chain 4") && has_text("mono"));}
    palette::set(theme);
    const char* valid=RECORDING_DIR;RECORDING_DIR="/not-present/seance-bounce-test";key(KEY_X);
    CHECK(!f.engine->bounce_snapshot().saved && f.engine->bounce_snapshot().source==BounceSource::Chain4);
    RECORDING_DIR=valid;key(KEY_X);CHECK(f.engine->bounce_snapshot().saved);
    auto& bank=trackr::synth::SampleBank::instance();const std::string path=bank.provenance(take.slot).path;
    CHECK(path.find("_chain4.wav")!=std::string::npos);
    std::ifstream wav(path,std::ios::binary);std::vector<char> bytes((std::istreambuf_iterator<char>(wav)),{});
    CHECK(bytes.size()==44+size_t(take.frames)*2);CHECK(uint8_t(bytes[22])==1 && uint8_t(bytes[23])==0);
    for(size_t i=0;i<take.frames;++i){uint16_t v=uint8_t(bytes[44+i*2])|(uint16_t(uint8_t(bytes[45+i*2]))<<8);CHECK(q15(v)==bank.slot(take.slot).data[i]);}
    trackr::synth::Sample decoded;trackr::synth::WavDecodeLimits limits;limits.workspace_bytes=trackr::synth::WAV_DECODE_WORKSPACE_BYTES;
    CHECK(trackr::synth::load_wav_to_sample(path.c_str(),decoded,ENGINE_SR,ENGINE_SR*20,limits)==trackr::synth::WavLoadResult::Ok);
    CHECK(decoded.channels==1 && decoded.num_frames()==take.frames);
    CHECK(std::equal(decoded.data.begin(),decoded.data.end()-1,bank.slot(take.slot).data.begin()));
    // Existing send remains explicit: source chain intact when destination differs.
    Unit* original=f.engine->chains[3].units[0];key(KEY_Y);click(123,90);key(KEY_A);
    CHECK(f.engine->chains[3].units[0]==original);
    CHECK(f.engine->chains[1].units[0]->kind==UnitKind::SamplePlayer);
    CHECK(static_cast<PlayUnit*>(f.engine->chains[1].units[0])->sample_slot==take.slot);
    CHECK(f.engine->bounce_snapshot().source==BounceSource::Chain4);
    click(160,198);CHECK(std::filesystem::exists(path));
    CHECK(bank.slot(take.slot).channels==1 && bank.slot(take.slot).playable_frames()==take.frames);
    click(115,56);bounce_capture();draw_bounce(true);CHECK(has_text("full mix"));
}
void simple_states_and_listen() {
    clear_bank();BounceFixture f;pair();
    CHECK(has_text("source") && has_text("record") && !has_text("save wav") && !has_text("listen"));
    f.arm();CHECK(f.engine->transport_playing && f.engine->bounce_snapshot().start_frame==0);
    pair();CHECK(has_text("stop early") && !has_text("source") && !has_text("save wav"));
    click(24,204);CHECK(f.engine->bounce_snapshot().active()); // old actions are gone
    render_frames(*f.engine,1234);click(160,110);
    const auto take=f.engine->bounce_snapshot();CHECK(take.status==BounceStatus::Partial);
    pair();CHECK(has_text("listen") && has_text("save wav") && !has_text("use in chain"));
    const auto* original=f.engine->chains[0].units[0];
    click(160,70);CHECK(f.engine->bounce_preview_active() && !f.engine->transport_playing);
    render_frames(*f.engine,137);CHECK(f.engine->bounce_preview_position()==137);
    pair();CHECK(has_text("stop listening"));
    CHECK(f.engine->chains[0].units[0]==original && f.engine->bounce_snapshot().generation==take.generation);
    key(KEY_A);CHECK(!f.engine->bounce_preview_active());
    bounce_touch_down(160,70);bounce_touch_hold(190,70);bounce_touch_release();CHECK(!f.engine->bounce_preview_active());
    key(KEY_A);CHECK(f.engine->bounce_preview_active());
    nav_open();bounce_cancel_input();CHECK(!f.engine->bounce_preview_active());nav_close();
    key(KEY_A);render_frames(*f.engine,take.frames+16);CHECK(!f.engine->bounce_preview_active());
    CHECK(f.engine->bounce_preview_position()==take.frames);pair();CHECK(has_text("listen"));
    key(KEY_A);key(KEY_B);CHECK(g_view==View::Chains && !f.engine->bounce_preview_active());
    g_view=View::Bounce;bounce_enter();CHECK(bounce_guard_project_change());
    click(160,198);pair();CHECK(has_text("record again?") && has_text("release take"));
    key(KEY_B);CHECK(f.engine->bounce_snapshot().generation==take.generation);
    click(160,198);key(KEY_A);CHECK(f.engine->bounce_snapshot().slot<0 && !f.engine->transport_playing);
    pair();CHECK(has_text("source") && has_text("record"));
    // Restarting transport also stops listen; saved takes can leave directly.
    f.complete();key(KEY_A);CHECK(f.engine->bounce_preview_active());
    key(KEY_START);CHECK(!f.engine->bounce_preview_active() && f.engine->transport_playing);
    key(KEY_X);CHECK(f.engine->bounce_snapshot().saved);
    CHECK(!bounce_guard_project_change() && f.engine->bounce_snapshot().slot<0);
}
void fast_send_and_saved_exit() {
    clear_bank();BounceFixture f;
    for(auto*& u:f.engine->chains[1].units){delete u;u=nullptr;}
    f.complete();CHECK(f.engine->transport_playing);
    Unit* original=f.engine->chains[0].units[0];
    const char* directory=RECORDING_DIR;RECORDING_DIR="/not-present/seance-bounce-test";
    key(KEY_Y);CHECK(f.engine->transport_playing && !f.engine->bounce_snapshot().saved);
    RECORDING_DIR=directory;key(KEY_Y);CHECK(!f.engine->transport_playing && f.engine->bounce_snapshot().saved);
    pair();CHECK(has_text("use chain") && has_text("empty"));
    const auto take=f.engine->bounce_snapshot();
    const std::string path=trackr::synth::SampleBank::instance().provenance(take.slot).path;
    key(KEY_A);CHECK(f.engine->chains[1].units[0]->kind==UnitKind::SamplePlayer);
    CHECK(f.engine->chains[0].units[0]==original && f.engine->transport_frame==0 && !f.engine->transport_playing);
    CHECK(!bounce_guard_project_change() && f.engine->bounce_snapshot().slot<0);
    CHECK(std::filesystem::exists(path));
    CHECK(trackr::synth::SampleBank::instance().slot(take.slot).playable_frames()==take.frames);
}
void navigation_targets() {
    clear_bank();BounceFixture f;const NavChoice choices[]={NavChoice::Chains,NavChoice::Sigils,NavChoice::Patchbay,NavChoice::Sequencer,NavChoice::Performance,NavChoice::Bounce,NavChoice::Patch,NavChoice::Exit};
    constexpr int choice_count=sizeof(choices)/sizeof(choices[0]);
    for(int i=0;i<choice_count;++i){nav_open();NavChoice o;nav_buttons(0,o);nav_touch_down(i<4?80:220,64+(i%4)*42,o);CHECK(o==choices[i] && !nav_is_open());}
    nav_open();nav_capture();width=320;objects=outside_rects=outside_lines=text_overlaps=0;texts.clear();pixels.assign(320*240,0);nav_draw();CHECK(outside_rects==0 && outside_lines==0 && text_overlaps==0);nav_close();
}
}
int main(int argc,char** argv) {
    if(argc>1 && std::string(argv[1])!="--dump-bounce")return inherited_sequencer_main(argc,argv);
    if(argc==3)output=argv[2];
    if(!output.empty())std::filesystem::create_directories(output);
    const std::string dir="/tmp/seance-bounce-ui-"+std::to_string(getpid());std::filesystem::create_directories(dir);RECORDING_DIR=dir.c_str();
    int passed=0;try {
        for(auto t:{input_and_recording,save_send_discard,cancellation_and_limits,stale_confirmations,repeat_bounce_cache,navigation_targets,chain_source_and_mono,simple_states_and_listen,fast_send_and_saved_exit}){t();++passed;}
        std::printf("pass bounce-ui: %d/9; %d frozen pairs / %d draws; peak %d/8192; real main input, simple states, audition, first-bar startup, safe send, mono/stereo WAV IO and navigation\n",passed,frozen_pairs,pairs,pair_peak);
        clear_bank();std::filesystem::remove_all(dir);return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"bounce-ui failed after %d cases: %s\n",passed,e.what());return 1;}
}
