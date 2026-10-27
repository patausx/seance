// Host-only promo capture: real Engine + current UI controllers/renderers.
// Includes the existing native-font/Citro2D software-raster harness; never runs its suite.
// Build with Makefile SEQUENCER_TEST_FLAGS/SOURCES, replacing sequencer_test.cpp with this file.
// Run from repository root: /tmp/seance_promo_capture promo/draft-01/capture [--audio-only]
// --audio-only preserves capture outputs and writes 15s engine audio with natural continuation.
// Recipe extraction: make NEEDS_DEVKIT= -nB /tmp/seance_chorus_ui_test (compile line only).
#define main seance_unused_regression_main
#include "sequencer_test.cpp"
#undef main
#include "../core/synth/wav_writer.h"

namespace {
constexpr int promo_rate=32000, promo_fps=30, promo_frames=330;
void promo_param(Unit& u,int p,int value){u.params[p].base=q15(value);u.params[p].snap();u.control_tick();}
q15 promo_note(int midi){return q15(((midi-24)*32767+36)/72);}
struct PromoVideo {
    FILE* pipe=nullptr;
    PromoVideo(const std::filesystem::path& path,int w){
        CHECK(path.string().find('\'')==std::string::npos);
        const std::string cmd="ffmpeg -nostdin -hide_banner -loglevel error -n -f rawvideo -pixel_format rgba -video_size "+
            std::to_string(w)+"x240 -framerate 30 -i pipe:0 -an -c:v ffv1 -level 3 -pix_fmt bgr0 '"+path.string()+"'";
        pipe=popen(cmd.c_str(),"w");CHECK(pipe);
    }
    ~PromoVideo(){if(pipe)pclose(pipe);}
    void frame(){CHECK(fwrite(pixels.data(),sizeof(u32),pixels.size(),pipe)==pixels.size());}
    void finish(){FILE*p=pipe;pipe=nullptr;CHECK(pclose(p)==0);}
};
struct PromoFixture : Fixture {
    PlayUnit* play=nullptr;
    PromoFixture(){
        for(auto& chain:engine->chains)for(auto*&u:chain.units){delete u;u=nullptr;}
        auto&bank=trackr::synth::SampleBank::instance();
        trackr::synth::Sample break_sample;break_sample.data.assign(64000,0);
        std::vector<int32_t> mix(64000);
        auto add=[&](const char*name,const std::vector<int>&steps,double gain){
            trackr::synth::Sample hit;
            std::string path=std::string("sample_library/starter_cc0/wav/")+name;
            CHECK(trackr::synth::load_wav_to_sample(path.c_str(),hit,32000,320000)==trackr::synth::WavLoadResult::Ok);
            CHECK(hit.channels==1);
            for(int step:steps)for(size_t i=0;i<hit.data.size()&&i+step*4000<mix.size();++i)
                mix[i+step*4000]+=int(hit.data[i]*gain);
        };
        add("kick_deep.wav",{0,6,8,11},.57);
        add("snare_x0x.wav",{4,12},.34);
        add("hat_closed.wav",{0,2,4,6,8,10,12,14},.14);
        add("rimshot.wav",{7,15},.15);
        for(size_t i=0;i<mix.size();++i){CHECK(std::abs(mix[i])<32767);break_sample.data[i]=q15(mix[i]);}
        for(int c=0;c<4;++c)break_sample.chops[c]=c*16000;
        CHECK(bank.replace_sample(61,std::move(break_sample),trackr::synth::SampleOrigin::Runtime,"cc0_break_120.wav")==trackr::synth::SampleEditResult::Ok);
        play=new PlayUnit;play->sample_slot=61;engine->chains[0].units[0]=play;
        promo_param(*play,PlayUnit::P_MODE,0);promo_param(*play,PlayUnit::P_GAIN,27000);
        engine->chains[0].volume=26000;
        auto*bass=new OscUnit;engine->chains[1].units[0]=bass;
        promo_param(*bass,OscUnit::P_PITCH,promo_note(36));
        promo_param(*bass,OscUnit::P_SHAPE,OscUnit::shape_anchor(OscUnit::Wave::Tri));
        promo_param(*bass,OscUnit::P_GAIN,13000);promo_param(*bass,OscUnit::P_DRIFT,0);
        auto*env=new VcaUnit;engine->chains[1].units[1]=env;
        promo_param(*env,VcaUnit::P_ATTACK,vca_envelope::time_value(128));
        promo_param(*env,VcaUnit::P_DECAY,vca_envelope::time_value(6600));
        engine->chains[1].volume=19000;
        auto*fm=new FmUnit;engine->chains[2].units[0]=fm;
        promo_param(*fm,FmUnit::P_PITCH,promo_note(60));promo_param(*fm,FmUnit::P_RATIO,6553);
        promo_param(*fm,FmUnit::P_INDEX,3400);promo_param(*fm,FmUnit::P_FEEDBACK,0);
        promo_param(*fm,FmUnit::P_DECAY,28000);promo_param(*fm,FmUnit::P_GAIN,10500);
        promo_param(*fm,FmUnit::P_ATTACK,3500);promo_param(*fm,FmUnit::P_INDEX_FALL,14000);
        fm->params[FmUnit::P_INDEX].src=ModSource::SigilA;fm->params[FmUnit::P_INDEX].mod_depth=1400;
        auto*delay=new DelayUnit;engine->chains[2].units[1]=delay;
        promo_param(*delay,DelayUnit::P_TIME,delay_time::free_value(12000));
        promo_param(*delay,DelayUnit::P_MIX,7500);promo_param(*delay,DelayUnit::P_FEEDBACK,8000);
        promo_param(*delay,DelayUnit::P_WOW,0);promo_param(*delay,DelayUnit::P_SAT,0);
        engine->chains[2].volume=19000;engine->chains[2].pan=5000;
        trackr::synth::Sample conga;
        CHECK(trackr::synth::load_wav_to_sample("sample_library/starter_cc0/wav/conga.wav",conga,32000,320000)==trackr::synth::WavLoadResult::Ok);
        CHECK(bank.replace_sample(60,std::move(conga),trackr::synth::SampleOrigin::SdWav,"conga.wav")==trackr::synth::SampleEditResult::Ok);
        auto*perc=new PlayUnit;perc->sample_slot=60;engine->chains[3].units[0]=perc;
        promo_param(*perc,PlayUnit::P_MODE,0);promo_param(*perc,PlayUnit::P_GAIN,7000);
        engine->chains[3].volume=15000;engine->chains[3].pan=-5000;
        engine->pattern=Pattern{};
        for(int c=0;c<4;++c)engine->pattern.length[c]=16;
        for(int step:{0,4,8,12}){
            engine->pattern.steps[0][step].flags=SeqStep::Trig;
            CHECK(engine->set_step_lock(0,step,0,PlayUnit::P_SLICE,q15(((step/4+1)*32768+16)/17)));
        }
        const int bass_steps[]={0,3,6,8,11,14},bass_notes[]={36,36,43,36,39,34};
        for(int i=0;i<6;++i){engine->pattern.steps[1][bass_steps[i]].flags=SeqStep::Trig;
            CHECK(engine->set_step_lock(1,bass_steps[i],0,OscUnit::P_PITCH,promo_note(bass_notes[i])));}
        const int fm_steps[]={2,5,10,13},fm_notes[]={60,67,63,70};
        for(int i=0;i<4;++i){engine->pattern.steps[2][fm_steps[i]].flags=SeqStep::Trig;
            CHECK(engine->set_step_lock(2,fm_steps[i],0,FmUnit::P_PITCH,promo_note(fm_notes[i])));}
        for(int step:{3,10,15})engine->pattern.steps[3][step].flags=SeqStep::Trig;
        for(int i=0;i<SIGIL_POINTS;++i)engine->sigils[0].set_point(i,q15(std::sin(i*6.283185307/SIGIL_POINTS)*19000));
        engine->set_sigil_sync(0,true);engine->set_sigil_div(0,0);
        engine->set_bpm(120);engine->set_sequencer_enabled(true);engine->transport_reset();engine->transport_play();
        g_edit_chain=g_edit_slot=g_sel_chain=g_sel_slot=0;g_view=View::Chains;g_overlay=Overlay::Editor;
        param_lens_cancel();g_drag=Drag::None;
        capture();tap(90,72);for(int i=0;i<20;++i)capture();
    }
    void capture(){engine->capture_ui_snapshot(g_ui_snapshot,0,0);play_focus_capture();param_lens_capture();}
    void touch(int x,int y){CHECK(play_focus_touch_body(*play,x,y));}
    void drag(int x,int y){play_focus_drag(*play,x,y);}
    void up(){play_focus_touch_release(*play);g_drag=Drag::None;}
    void tap(int x,int y){touch(x,y);up();}
};
}
int main(int argc,char**argv){
    try{
        CHECK(argc==2||(argc==3&&std::strcmp(argv[2],"--audio-only")==0));
        const bool audio_only=argc==3;
        const std::filesystem::path out=argv[1];std::filesystem::create_directories(out);
        if(audio_only)CHECK(!std::filesystem::exists(out/"engine-15s.wav")&&!std::filesystem::exists(out/"audio-15s.json"));
        palette::set(palette::Theme::Pink);host_time_ms=1000;
        PromoFixture f;
        std::unique_ptr<PromoVideo> bottom,top;
        std::ofstream timeline;
        if(!audio_only){
            bottom=std::make_unique<PromoVideo>(out/"bottom.mkv",320);top=std::make_unique<PromoVideo>(out/"top.mkv",400);
            timeline.open(out/"timeline.tsv");timeline<<"frame\tseconds\tscene\tseq_step\tlive_source_frame\tsigil_value\n";
        }
        trackr::synth::Sample audio;audio.channels=2;audio.data.resize(size_t(audio_only?15:11)*promo_rate*2);
        uint32_t done=0;int peak=0,clipped=0;long double square=0;
        for(int n=0;n<promo_frames;++n){
            host_time_ms=1000+uint64_t(n)*1000/promo_fps;g_ui_frame=2*n;
            if(n==18)f.tap(35,94); // actual add tool
            if(n==30)f.tap(123,128); // actual new stable cut, around 0.75s
            if(n==50)f.tap(136,128); // select/audition the new cut
            if(n==65)f.touch(50,46);
            if(n>65&&n<=77)f.drag(50+(n-65),46); // real shared cut rate
            if(n==78)f.up();
            if(n==90)f.tap(150,72); // source scratch surface
            if(n==99)f.touch(60,128); // silent touchdown, no synthetic playback cursor
            if(n>99&&n<185){
                const double phase=(n-99)/85.0;
                f.drag(60+int(176*phase+21*std::sin(phase*18.84955592)),128);
            }
            if(n==185)f.up();
            if(n==190)f.touch(50,176);
            if(n>190&&n<202)f.drag(50+(n-190)*4,176); // persistent position through actual control
            if(n==202)f.up();
            if(n==210){f.tap(270,232);CHECK(g_view==View::Sequencer&&g_overlay==Overlay::None);}
            if(n>=210&&n<270)sequencer_view_buttons(0,0,0);
            if(n==270){sequencer_view_touch_release();play_focus_release();g_view=View::Sigils;g_overlay=Overlay::None;g_sel_sigil=0;g_drag=Drag::None;sigils_forget_undo();}
            if(n==282)sigils_view_touch_down(20,108);
            if(n>282&&n<=312){const double t=(n-282)/30.0;sigils_view_touch_drag(20+int(280*t),108-int(31*std::sin(t*9.42477796)));}
            if(n==313)sigils_view_touch_release();
            const uint32_t end=uint32_t(uint64_t(n+1)*promo_rate/promo_fps);
            while(done<end){const int count=std::min<int>(128,end-done);f.engine->render(audio.data.data()+done*2,count);done+=count;}
            f.engine->capture_ui_snapshot(g_ui_snapshot,0,0);
            if(n<210){play_focus_capture();param_lens_capture();}
            else if(n<270)sequencer_capture_inspector();else sigils_capture();
            if(!audio_only)for(bool is_top:{true,false}){
                module_inspector_surface(is_top);
                {ModuleInspectorNoEngine frozen;
                    if(is_top)top_draw();else if(n<210)play_focus_draw(*f.play);
                    else if(n<270)sequencer_view_draw();else sigils_view_draw();}
                CHECK(objects>0&&objects<8192);
                (is_top?top:bottom)->frame();
            }
            if(!audio_only)timeline<<n<<'\t'<<double(n)/30<<'\t'<<(n<90?"play_cuts":n<210?"play_scrub":n<270?"sequencer":"sigils")
                <<'\t'<<int(g_ui_snapshot.sequence_step[0])<<'\t'<<f.play->visual_source_frame()
                <<'\t'<<f.engine->sigils[0].value<<'\n';
        }
        CHECK(done==11*promo_rate);
        if(!audio_only){bottom->finish();top->finish();}
        else while(done<15*promo_rate){
            const int count=std::min<int>(128,15*promo_rate-done);
            f.engine->render(audio.data.data()+done*2,count);done+=count;
        }
        for(q15 x:audio.data){peak=std::max(peak,std::abs(int(x)));clipped+=x==32767||x==-32768;square+=int64_t(x)*x;}
        CHECK(peak>0&&clipped==0);
        CHECK(trackr::synth::save_sample_wav((out/(audio_only?"engine-15s.wav":"engine.wav")).c_str(),audio,32000,done)==trackr::synth::WavSaveResult::Ok);
        if(audio_only){
            CHECK(done==480000);
            std::ofstream meta(out/"audio-15s.json");
            meta<<"{\"provenance\":\"real Engine; same first 11s scripted controls, then 4s natural continuation; no fade\","
                <<"\"duration_seconds\":15,\"audio_rate\":32000,\"channels\":2,\"audio_frames\":"<<done
                <<",\"audio_peak_pcm16\":"<<peak<<",\"audio_rms_pcm16\":"<<double(std::sqrt(square/audio.data.size()))
                <<",\"clipped_samples\":"<<clipped<<"}\n";
            std::printf("audio-only: %u stereo frames; peak %d; clipped %d\n",done,peak,clipped);
            return 0;
        }
        std::ofstream meta(out/"capture.json");
        meta<<"{\n  \"provenance\": \"current C++ UI and Engine, deterministic host software Citro2D raster; not hardware\",\n"
            <<"  \"frames\": 330, \"fps\": 30, \"duration_seconds\": 11, \"audio_rate\": 32000, \"audio_frames\": "<<done<<",\n"
            <<"  \"audio_peak_pcm16\": "<<peak<<", \"audio_rms_pcm16\": "<<double(std::sqrt(square/audio.data.size()))<<", \"clipped_samples\": "<<clipped<<",\n"
            <<"  \"scenes\": [{\"from\":0,\"to\":3,\"name\":\"play cuts, insert and select, cut speed\"},{\"from\":3,\"to\":7,\"name\":\"play private source scratch and position\"},{\"from\":7,\"to\":9,\"name\":\"running sequencer, trigger mode\"},{\"from\":9,\"to\":11,\"name\":\"sigil a, actual drawn modulation of fm index\"}],\n"
            <<"  \"sound\": \"120 bpm, 16 steps; memory-only break from shipped CC0 kick_deep/snare_x0x/hat_closed/rimshot; conga CC0; actual OSC triangle bass/VCA, FM pluck and 375ms delay\",\n"
            <<"  \"sample_license\": \"sample_library/starter_cc0/README.md and CC0-1.0.txt (Stargate Sample Pack)\",\n"
            <<"  \"caveats\": \"GPU line coverage and paper texture are host approximations. UI inputs scripted through actual controls except initial scene setup and Sigils navigation. Live snapshots/audio advance together in <=128-frame Engine blocks; no fake pixels or external soundtrack. No hardware, installation, production source or sample-file edits.\"\n}\n";
        std::printf("capture: 330 frames, 352000 stereo audio frames; peak %d; clipped %d\n",peak,clipped);
        return 0;
    }catch(const std::exception&e){std::fprintf(stderr,"promo capture: %s\n",e.what());return 1;}
}
