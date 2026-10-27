// Fresh native host UI for the 192-bpm promo. The soundtrack is separate.
// Reuse the native raster only; do not call the inherited regression main.
#define main unused_tek_regression_main
#define chains_context_draw_top unused_tek_chains_top
#define workspace_point unused_tek_workspace_point
#define workspace_camera_x unused_tek_camera_x
#define workspace_camera_y unused_tek_camera_y
#define workspace_shift_slots_right unused_tek_shift_slots
#include "sequencer_test.cpp"
#undef main
#undef chains_context_draw_top
#undef workspace_point
#undef workspace_camera_x
#undef workspace_camera_y
#undef workspace_shift_slots_right
#include "../platform/3ds/ui_chains.h"
#include "../platform/3ds/ui_patchbay.h"
#include "../platform/3ds/ui_bounce.h"

namespace seance::platform::ui3ds {
bool g_has_dsp = true;
void mark_project_structural_mutated() { ++mutations; }
}
namespace tek_promo {
constexpr int RATE=32000, FPS=32; // ten native frames per 192-bpm beat
const char* names[]={"osc","noise","fm","motor","play","gran","mic","rec","filter","vca","delay","fold","bit","chorus","strn","reverb","ghost","ring","tilt","strain","shift","bird","fract","comp","mouth","arp","glitch"};
const UnitKind kinds[]={UnitKind::Osc,UnitKind::Noise,UnitKind::Fm,UnitKind::Motor,UnitKind::SamplePlayer,UnitKind::Grain,UnitKind::MicIn,UnitKind::Recorder,UnitKind::Filter,UnitKind::Vca,UnitKind::Delay,UnitKind::Fold,UnitKind::Bitcrush,UnitKind::Chorus,UnitKind::Stranular,UnitKind::Reverb,UnitKind::Ghost,UnitKind::Ring,UnitKind::Tilt,UnitKind::Strain,UnitKind::Shift,UnitKind::Bird,UnitKind::Fract,UnitKind::Comp,UnitKind::Mouth,UnitKind::Arp,UnitKind::Glitch};
static_assert(sizeof(kinds)/sizeof(kinds[0])==int(UnitKind::Count)-1,"all current visible modules");
void value(Unit& u,int p,int v) {u.params[p].base=q15(v);u.params[p].snap();u.control_tick();}
q15 note(int n) {return q15((n-24)*32767/72);}
void clear(Engine& e) {for(auto& c:e.chains)for(auto*& u:c.units){delete u;u=nullptr;}}
void run(Engine& e,int count) {q15 pcm[256];while(count>0){const int n=std::min(count,128);e.render(pcm,n);count-=n;}}
void focus_capture() {
    fm_context_capture(fm_focus_selected());stranular_focus_capture();play_focus_capture();
    osc_focus_capture();noise_focus_capture();fm_focus_capture();motor_focus_capture();grain_focus_capture();
    vca_focus_capture();filter_focus_capture();delay_focus_capture();fold_focus_capture();bit_focus_capture();
    chorus_focus_capture();reverb_focus_capture();ghost_focus_capture();tilt_focus_capture();ring_focus_capture();
    strain_focus_capture();mic_focus_capture();recorder_focus_capture();bird_focus_capture();shift_focus_capture();
    fract_focus_capture();comp_focus_capture();mouth_focus_capture();arp_focus_capture();glitch_focus_capture();param_lens_capture();
}
void lower(const Unit& u) {
#define D(K,T,F) case UnitKind::K:F(static_cast<const T&>(u));break
    switch(u.kind){
    D(Osc,OscUnit,osc_focus_draw);D(Noise,NoiseUnit,noise_focus_draw);D(Fm,FmUnit,fm_focus_draw);
    D(Motor,MotorUnit,motor_focus_draw);D(SamplePlayer,PlayUnit,play_focus_draw);D(Grain,GrainUnit,grain_focus_draw);
    D(MicIn,MicUnit,mic_focus_draw);case UnitKind::Recorder:recorder_focus_draw(static_cast<const RecUnit&>(u),"");break;
    D(Filter,FilterUnit,filter_focus_draw);D(Vca,VcaUnit,vca_focus_draw);D(Delay,DelayUnit,delay_focus_draw);
    D(Fold,FoldUnit,fold_focus_draw);D(Bitcrush,BitUnit,bit_focus_draw);D(Chorus,ChorusUnit,chorus_focus_draw);
    case UnitKind::Stranular:CHECK(stranular_focus_draw_bottom());break;
    D(Reverb,ReverbUnit,reverb_focus_draw);D(Ghost,GhostUnit,ghost_focus_draw);D(Ring,RingUnit,ring_focus_draw);
    D(Tilt,TiltUnit,tilt_focus_draw);D(Strain,StrainUnit,strain_focus_draw);D(Shift,ShiftUnit,shift_focus_draw);
    D(Bird,BirdUnit,bird_focus_draw);D(Fract,FractUnit,fract_focus_draw);D(Comp,CompUnit,comp_focus_draw);
    D(Mouth,MouthUnit,mouth_focus_draw);D(Arp,ArpUnit,arp_focus_draw);D(Glitch,GlitchUnit,glitch_focus_draw);
    default:CHECK(false);}
#undef D
}
struct Stream {
    FILE* pipe=nullptr;
    Stream(const std::filesystem::path& path,int w){
        CHECK(!std::filesystem::exists(path));CHECK(path.string().find('\'')==std::string::npos);
        const std::string cmd="ffmpeg -nostdin -v error -n -f rawvideo -pix_fmt rgba -s "+std::to_string(w)+"x240 -r 32 -i pipe:0 -an -c:v ffv1 -level 3 -threads 2 -pix_fmt bgr0 '"+path.string()+"'";
        pipe=popen(cmd.c_str(),"w");CHECK(pipe);
    }
    ~Stream(){if(pipe)pclose(pipe);}
    void append(){CHECK(fwrite(pixels.data(),sizeof(u32),pixels.size(),pipe)==pixels.size());}
    void close(){FILE* p=pipe;pipe=nullptr;CHECK(pclose(p)==0);}
};
void pair(Stream& top,Stream& bottom,const std::function<void(bool)>& draw){
    int sum=0;
    for(bool upper:{true,false}){
        module_inspector_surface(upper);{ModuleInspectorNoEngine frozen;draw(upper);}
        CHECK(objects>0);sum+=objects;(upper?top:bottom).append();
    }
    maximum_shared_objects=std::max(maximum_shared_objects,sum);
}
void input_sample(const char* path,int slot){
    trackr::synth::Sample s;
    CHECK(trackr::synth::load_wav_to_sample(path,s,RATE,RATE*20)==trackr::synth::WavLoadResult::Ok);
    if(s.channels==2){for(size_t i=0;i<s.data.size()/2;++i)s.data[i]=q15((int(s.data[i*2])+int(s.data[i*2+1]))/2);s.data.resize(s.data.size()/2);s.channels=1;}
    CHECK(trackr::synth::SampleBank::instance().replace_sample(slot,std::move(s),trackr::synth::SampleOrigin::Runtime,"user_kick.wav")==trackr::synth::SampleEditResult::Ok);
}
void loop_sample(){
    const auto& kick=trackr::synth::SampleBank::instance().slot(61);
    const auto& alt=trackr::synth::SampleBank::instance().slot(60);
    trackr::synth::Sample s;s.channels=1;s.data.assign(80000,0);
    for(int beat=0;beat<8;++beat){const auto& a=beat%4==3?alt:kick;
        for(int i=0;i<10000&&i<int(a.data.size());++i)s.data[beat*10000+i]=q15(int(a.data[i])*3/4);
        s.chops[beat]=beat*10000;
    }
    CHECK(trackr::synth::SampleBank::instance().replace_sample(59,std::move(s),trackr::synth::SampleOrigin::Runtime,"tek_cuts.wav")==trackr::synth::SampleEditResult::Ok);
}
void catalogue(const std::filesystem::path& out){
    Fixture f;clear(*f.engine);g_view=View::Chains;g_overlay=Overlay::Editor;
    Stream top(out/"modules-top.mkv",400),bottom(out/"modules-bottom.mkv",320);
    for(int k=0;k<27;++k){
        clear(*f.engine);param_lens_cancel();g_drag=Drag::None;g_seq_lock_armed=false;
        g_sel_chain=g_edit_chain=0;g_sel_slot=g_edit_slot=0;g_view=View::Chains;g_overlay=Overlay::Editor;
        Unit* u=make_unit(kinds[k]);CHECK(u);int slot=0;
        const bool source=k<7||kinds[k]==UnitKind::Bird||kinds[k]==UnitKind::Arp;
        if(!source){
            auto* p=new PlayUnit;p->sample_slot=59;f.engine->chains[0].units[0]=p;
            value(*p,PlayUnit::P_MODE,0);value(*p,PlayUnit::P_GAIN,24000);p->trigger();slot=1;
        }
        f.engine->chains[0].units[slot]=u;g_sel_slot=g_edit_slot=slot;
        if(kinds[k]==UnitKind::SamplePlayer)static_cast<PlayUnit*>(u)->sample_slot=59;
        if(kinds[k]==UnitKind::Grain)static_cast<GrainUnit*>(u)->sample_slot=59;
        if(kinds[k]==UnitKind::Arp){f.engine->chains[0].units[1]=new OscUnit;value(*u,ArpUnit::P_NOTE2,ArpUnit::note_value(3));value(*u,ArpUnit::P_NOTE3,ArpUnit::note_value(7));value(*u,ArpUnit::P_NOTE4,ArpUnit::note_value(12));}
        if(kinds[k]==UnitKind::Recorder){
            auto* r=static_cast<RecUnit*>(u);RecWorkspacePlan plan;
            CHECK(f.engine->plan_recorder(*r,true,plan)==trackr::synth::SampleEditResult::Ok);
            CHECK(Engine::stage_recorder(plan)==trackr::synth::SampleEditResult::Ok);
            CHECK(f.engine->commit_recorder(plan)==trackr::synth::SampleEditResult::Ok);r->trigger();
        }
        if(kinds[k]==UnitKind::Osc){value(*u,OscUnit::P_PITCH,note(43));value(*u,OscUnit::P_SHAPE,18000);}
        if(kinds[k]==UnitKind::Filter)value(*u,FilterUnit::P_RES,21000);
        if(kinds[k]==UnitKind::Fm)value(*u,FmUnit::P_INDEX,19000);
        f.engine->pattern=Pattern{};f.engine->set_bpm(192);f.engine->set_sequencer_enabled(false);f.engine->transport_reset();f.engine->transport_play();
        u->control_tick();u->trigger();run(*f.engine,44000);
        if(kinds[k]==UnitKind::Fract)static_cast<FractUnit*>(u)->set_manual_grab(true);
        for(int n=0;n<40;++n){
            host_time_ms+=1000/FPS;g_ui_frame+=2;
            if(n%10==0&&kinds[k]!=UnitKind::Recorder)f.engine->trigger_chain(0);
            if(kinds[k]==UnitKind::Recorder&&n==16)static_cast<RecUnit*>(u)->stop();
            run(*f.engine,1000);f.engine->capture_ui_snapshot(g_ui_snapshot,0,slot);focus_capture();chains_capture();
            pair(top,bottom,[&](bool upper){if(upper)top_draw();else lower(*u);});
        }
        std::printf("module %02d %s\n",k+1,names[k]);std::fflush(stdout);
    }
    top.close();bottom.close();
}
void prepare(Engine& e){
    clear(e);e.pattern=Pattern{};
    auto* p=new PlayUnit;p->sample_slot=61;e.chains[0].units[0]=p;value(*p,PlayUnit::P_MODE,0);value(*p,PlayUnit::P_GAIN,23000);
    e.chains[0].units[1]=new CompUnit;e.chains[0].volume=26000;
    auto* osc=new OscUnit;e.chains[1].units[0]=osc;value(*osc,OscUnit::P_PITCH,note(36));value(*osc,OscUnit::P_SHAPE,19000);value(*osc,OscUnit::P_GAIN,12000);
    auto* filt=new FilterUnit;e.chains[1].units[1]=filt;value(*filt,FilterUnit::P_CUTOFF,14500);value(*filt,FilterUnit::P_RES,21000);
    e.chains[1].units[2]=new FoldUnit;e.chains[1].units[3]=new VcaUnit;e.chains[1].units[4]=new ChorusUnit;e.chains[1].units[5]=new DelayUnit;
    e.chains[1].volume=19000;
    auto* fm=new FmUnit;e.chains[2].units[0]=fm;value(*fm,FmUnit::P_PITCH,note(60));value(*fm,FmUnit::P_INDEX,16000);value(*fm,FmUnit::P_GAIN,8000);
    e.chains[2].units[1]=new ReverbUnit;e.chains[2].volume=17000;
    auto* gran=new GrainUnit;gran->sample_slot=59;e.chains[3].units[0]=gran;e.chains[3].units[1]=new GlitchUnit;e.chains[3].volume=16000;
    for(int c=0;c<4;++c){e.pattern.length[c]=64;for(int s=0;s<64;++s){
        if(c==0?s%4==0:c==1?(s%4==2||s%16==15):c==2?(s%8==3||s%16==13):s%8==6)e.pattern.steps[c][s].flags=SeqStep::Trig;
        if(c==1&&e.pattern.steps[c][s].flags)CHECK(e.set_step_lock(c,s,0,OscUnit::P_PITCH,note(36+(s%16==15?7:s%16==10?3:0))));
        if(c==2&&e.pattern.steps[c][s].flags)CHECK(e.set_step_lock(c,s,0,FmUnit::P_PITCH,note(60+(s%16==13?7:0))));
        if(s%16==15){e.pattern.steps[c][s].set_retrig_count(2);e.pattern.microtiming[c][s]=-4;}
    }}
    for(int i=0;i<8;++i){for(int n=0;n<SIGIL_POINTS;++n)e.sigils[i].set_point(n,q15(std::sin(n*6.283185307/SIGIL_POINTS*(i%3+1))*22000));
        SigilConfig config;config.legacy=false;config.rate_millihz=1600;e.set_sigil_config(i,config);e.set_sigil_sync(i,true);e.set_sigil_div(i,0);}
    filt->params[FilterUnit::P_CUTOFF].src=ModSource::SigilA;filt->params[FilterUnit::P_CUTOFF].mod_depth=9000;
    fm->params[FmUnit::P_INDEX].src=ModSource::SigilB;fm->params[FmUnit::P_INDEX].mod_depth=8000;
    e.pattern_banks[0]=e.pattern; e.pattern_banks[1]=e.pattern;
    for(int s=0;s<64;++s)e.pattern_banks[1].steps[3][s].flags=s%3==0?SeqStep::Trig:0;
    e.set_bpm(192);e.set_sequencer_enabled(true);e.transport_reset();e.transport_play();
}
void workflows(const std::filesystem::path& out){
    Fixture f;prepare(*f.engine);g_view=View::Chains;g_overlay=Overlay::None;g_sel_chain=g_edit_chain=1;g_sel_slot=g_edit_slot=1;
    Stream top(out/"workflows-top.mkv",400),bottom(out/"workflows-bottom.mkv",320);
    std::ofstream timeline(out/"workflows.tsv");timeline<<"frame\tscene\tselected_layer\tengine_frame\n";
    std::string scene;uint64_t clock=30000;
    for(int n=0;n<720;++n){
        host_time_ms=clock+uint64_t(n)*1000/FPS;g_ui_frame=200+2*n;
        if(n==0){scene="chains";chains_capture();}
        if(n<80&&n%20==0){g_sel_slot=g_edit_slot=(n/20)%6;chains_capture();}
        if(n==80){scene="sequence";g_view=View::Sequencer;g_overlay=Overlay::None;g_sel_chain=1;f.refresh();sequencer_view_enter();f.mode(1);f.touch(1,2);}
        if(n==120)f.mode(6);
        if(n==160)f.mode(4);
        if(n==200){scene="sigils";g_view=View::Sigils;g_overlay=Overlay::None;g_sel_sigil=0;sigils_close_mode_menu();sigils_forget_undo();}
        if(n==208)sigils_view_touch_down(20,104);
        if(n>208&&n<286){const double p=(n-208)/77.;sigils_view_touch_drag(20+int(p*278),108-int(42*std::sin(p*18.8495559)));}
        if(n==286)sigils_view_touch_release();
        if(n==300){scene="sigilbay";g_view=View::Patchbay;g_overlay=Overlay::None;patchbay_enter_sigil(0);}
        if(n==340){scene="perform";g_view=View::Performance;g_overlay=Overlay::None;performance_view_reset();
            f.refresh();for(int side=0;side<2;++side){if(side)value(*f.engine->chains[1].units[1],FilterUnit::P_RES,28000);const int x=side?285:25;performance_view_touch_down(x,210);g_ui_frame+=24;performance_view_touch_hold(x,210);performance_view_touch_release(true);}
        }
        if(n==354)performance_view_touch_down(62,211);
        if(n>354&&n<418)performance_view_touch_drag(62+(n-354)*3,211);
        if(n==418)performance_view_touch_release(true);
        if(n==420){performance_view_touch_down(196,182);performance_view_touch_release(true);}
        if(n==440){performance_view_touch_down(196,182);performance_view_touch_release(true);}
        if(n==450){scene="transition";performance_view_touch_down(265,10);performance_view_touch_release(true);}
        if(n==458)performance_view_touch_down(20,135);
        if(n>458&&n<498)performance_view_touch_drag(20+(n-458)*7,135);
        if(n==498)performance_view_touch_release(true);
        if(n==510){scene="bounce";transition::close();performance_view_leave();g_view=View::Bounce;g_overlay=Overlay::None;bounce_enter();
            f.engine->transport_stop();f.engine->transport_reset();bounce_buttons(KEY_A,KEY_A);
        }
        run(*f.engine,1000);
        f.engine->capture_ui_snapshot(g_ui_snapshot,g_sel_chain,g_sel_slot,g_sel_sigil);
        // As in current main: this owner must run on EVERY page so leaving Chains
        // completes the real 240ms fade back to quiet paper, including Perform.
        chains_capture();
        if(g_view==View::Sequencer){sequencer_view_buttons(0,0,0);sequencer_capture_inspector();}
        else if(g_view==View::Sigils)sigils_capture();
        else if(g_view==View::Patchbay)patchbay_capture();
        else if(g_view==View::Performance){performance_view_tick();performance_view_capture();}
        else if(g_view==View::Bounce)bounce_capture();
        pair(top,bottom,[&](bool upper){
            if(g_view==View::Bounce){if(upper)bounce_draw_top();else bounce_draw();}
            else if(upper)top_draw();
            else if(g_view==View::Chains)chains_view_draw();
            else if(g_view==View::Sequencer)sequencer_view_draw();
            else if(g_view==View::Sigils)sigils_view_draw();
            else if(g_view==View::Patchbay)patchbay_draw();
            else performance_view_draw();
        });
        timeline<<n<<'\t'<<scene<<'\t'<<sequencer_selected_layer()<<'\t'<<f.engine->transport_frame<<'\n';
    }
    top.close();bottom.close();bounce_cancel_input();performance_view_leave();
}
}
int main(int argc,char** argv){
    try{
        CHECK(argc==4);const std::filesystem::path out=argv[1];CHECK(!std::filesystem::exists(out));std::filesystem::create_directories(out);
        palette::set(palette::Theme::Graphite);host_time_ms=1000;
        tek_promo::input_sample(argv[2],61);tek_promo::input_sample(argv[3],60);tek_promo::loop_sample();
        tek_promo::catalogue(out);tek_promo::workflows(out);
        std::ofstream info(out/"capture.json");info<<"{\n  \"provenance\":\"fresh current C++ UI renderers and Engine, host raster, not hardware footage\",\n  \"native_fps\":32, \"bpm\":192, \"modules\":27, \"module_frames_each\":40, \"workflow_frames\":720,\n  \"audio\":\"engine renders feed real UI telemetry only; audio discarded, editorial soundtrack generated separately from user kicks\",\n  \"mic\":\"no host microphone input, idle source shown\",\n  \"boundaries\":\"scripted prepared fixtures and current controllers; no software tests, deployment, project writes or recording exports\",\n  \"max_shared_objects\":"<<maximum_shared_objects<<"\n}\n";
        std::printf("fresh capture complete: 27 modules / current workflows / shared peak %d (not hardware fps)\n",maximum_shared_objects);return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"tek capture: %s\n",e.what());return 1;}
}
