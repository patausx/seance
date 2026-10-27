// host-only reactive breakbeat: SAME real engine for gestures, native UI and PCM.
// build/run: python3 tools/capture_promo_reactive.py
// New capture only; original promo scripts/samples/recordings remain untouched.
#define main seance_unused_regression_main
#include "sequencer_test.cpp"
#undef main
#include "../core/synth/wav_writer.h"

namespace {
constexpr int RATE=32000,FPS=30,VIDEO_FRAMES=936,AUDIO_FRAMES=1152000;
void setp(Unit& u,int p,int v){u.params[p].base=q15(v);u.params[p].snap();u.control_tick();}
q15 midi(int n){return q15(((n-24)*32767+36)/72);}
struct VideoStream {
    FILE* pipe=nullptr;
    VideoStream(const std::filesystem::path& p,int w){
        CHECK(!std::filesystem::exists(p));CHECK(p.string().find('\'')==std::string::npos);
        std::string cmd="ffmpeg -nostdin -hide_banner -loglevel error -n -f rawvideo -pixel_format rgba -video_size "+std::to_string(w)+"x240 -framerate 30 -i pipe:0 -an -c:v ffv1 -level 3 -pix_fmt bgr0 '"+p.string()+"'";
        pipe=popen(cmd.c_str(),"w");CHECK(pipe);
    }
    ~VideoStream(){if(pipe)pclose(pipe);}
    void frame(){CHECK(fwrite(pixels.data(),sizeof(u32),pixels.size(),pipe)==pixels.size());}
    void finish(){auto*p=pipe;pipe=nullptr;CHECK(pclose(p)==0);}
};
void empty_engine(Engine& e){for(auto&c:e.chains)for(auto*&u:c.units){delete u;u=nullptr;}}
struct StemInfo { const char* name; int slot,peak; double gain; };
std::array<StemInfo,3> stems{};
void drum_sources(){
    // The liked beat's actual hit vocabulary, gains, swing and pitched replies.
    // Four-bar, mono stem sources made in memory, never installed WAVs. Each
    // source voice is monophonic like the audio-only original (no piled kick tails).
    constexpr int N=153600,STEP=2400,BAR=38400;
    const char* names[]={"kick_punch.wav","kick_deep.wav","snare_x0x.wav","snare_synth.wav","hat_closed.wav","hat_bright.wav","rimshot.wav","conga.wav"};
    std::array<trackr::synth::Sample,8> hits;
    for(int i=0;i<8;++i){const std::string path=std::string("sample_library/starter_cc0/wav/")+names[i];
        CHECK(trackr::synth::load_wav_to_sample(path.c_str(),hits[i],RATE,320000)==trackr::synth::WavLoadResult::Ok);CHECK(hits[i].channels==1);}
    struct Hit {int frame,voice,source,gain,speed;};std::vector<Hit> score;
    auto add=[&](int b,int s,int v,int source,int gain,int offset=0,int speed=8192){score.push_back({b*BAR+s*STEP+offset,v,source,gain,speed});};
    for(int b=0;b<4;++b){
        const std::vector<int> kicks=b==3?std::vector<int>{0,6,9,11,14}:b%2?std::vector<int>{0,3,8,10,14}:std::vector<int>{0,6,8,11};
        for(int s:kicks)add(b,s,0,s==0?1:0,s==0?29000:24000);
        for(int s:{4,12})add(b,s,1,2,30000,64);
        add(b,3,2,2,6500,-80,7900);add(b,b%2?9:10,2,2,10000,120,8400);
        if(b%2)add(b,15,2,3,8500,-60);
        if(b==3){add(b,13,2,2,13000,0,8000);add(b,14,2,2,19000,0,8400);add(b,15,2,2,23000,0,9000);add(b,15,2,2,12500,STEP/2,9800);}
        for(int s=0;s<16;++s){
            if(s%8==5)continue;
            int source=s%8==6?5:4,gain=s%4==0?16000:s%2?7500:11500;
            if(s==7&&b%2){source=6;gain=15500;}if(s==15&&b==2){source=7;gain=18000;}
            add(b,s,3,source,gain,s%2?160:0,8192+(s%3-1)*160);
        }
    }
    std::stable_sort(score.begin(),score.end(),[](const Hit&a,const Hit&b){return a.frame<b.frame;});
    std::array<std::vector<int>,4> voices;for(auto&v:voices)v.resize(N);
    for(size_t j=0;j<score.size();++j){const auto&h=score[j];int end=N;
        for(size_t k=j+1;k<score.size();++k)if(score[k].voice==h.voice){end=score[k].frame;break;}
        const auto&hit=hits[h.source].data;
        for(int f=h.frame;f<end;++f){const double pos=double(f-h.frame)*h.speed/8192;const size_t i=size_t(pos);if(i+1>=hit.size())break;
            const double value=hit[i]+(hit[i+1]-hit[i])*(pos-i);voices[h.voice][f]=int(value*h.gain/32768);}
    }
    const int slots[]={61,59,60};const char* labels[]={"reactive_cc0_break_4bar.wav","reactive_cc0_ghosts_4bar.wav","reactive_cc0_hats_4bar.wav"};
    for(int stem=0;stem<3;++stem){std::vector<int> mix(N);int peak=0;
        for(int f=0;f<N;++f){mix[f]=stem==0?voices[0][f]+voices[1][f]:voices[stem+1][f];peak=std::max(peak,std::abs(mix[f]));}
        // One constant safety attenuation per prepared source; never time-varying.
        const double gain=std::min(1.,26000./peak);trackr::synth::Sample sample;sample.data.resize(N);
        for(int f=0;f<N;++f)sample.data[f]=q15(mix[f]*gain);
        for(int cut=0;cut<16;++cut)sample.chops[cut]=cut*9600;
        CHECK(trackr::synth::SampleBank::instance().replace_sample(slots[stem],std::move(sample),trackr::synth::SampleOrigin::Runtime,labels[stem])==trackr::synth::SampleEditResult::Ok);
        stems[stem]={labels[stem],slots[stem],peak,gain};
    }
}
struct Reactive : Fixture {
    PlayUnit* play=nullptr;PlayUnit* reply=nullptr;OscUnit* bass=nullptr;FilterUnit* filter=nullptr;DelayUnit* delay=nullptr;
    Reactive(){
        empty_engine(*engine);drum_sources();
        play=new PlayUnit;play->sample_slot=61;engine->chains[0].units[0]=play;
        setp(*play,PlayUnit::P_MODE,0);setp(*play,PlayUnit::P_GAIN,27000);engine->chains[0].volume=24500;
        bass=new OscUnit;engine->chains[1].units[0]=bass;
        setp(*bass,OscUnit::P_PITCH,midi(31));setp(*bass,OscUnit::P_SHAPE,OscUnit::shape_anchor(OscUnit::Wave::Tri));
        setp(*bass,OscUnit::P_SUB,7000);setp(*bass,OscUnit::P_GAIN,12500);setp(*bass,OscUnit::P_DRIFT,0);
        filter=new FilterUnit;engine->chains[1].units[1]=filter;
        setp(*filter,FilterUnit::P_CUTOFF,7000);setp(*filter,FilterUnit::P_RES,9000);setp(*filter,FilterUnit::P_DRIVE,4500);
        auto*vca=new VcaUnit;engine->chains[1].units[2]=vca;
        setp(*vca,VcaUnit::P_ATTACK,vca_envelope::time_value(80));setp(*vca,VcaUnit::P_DECAY,vca_envelope::time_value(5200));
        engine->chains[1].volume=18000;
        reply=new PlayUnit;reply->sample_slot=59;engine->chains[2].units[0]=reply;
        setp(*reply,PlayUnit::P_MODE,0);setp(*reply,PlayUnit::P_GAIN,31000);
        // Unlocked manual pad launches the prepared final pitched-roll cut.
        setp(*reply,PlayUnit::P_SLICE,q15((16*32768+16)/17));
        // Base morph and Sigil routes are intentionally never overwritten by step locks.
        filter->params[FilterUnit::P_CUTOFF].src=ModSource::SigilA;filter->params[FilterUnit::P_CUTOFF].mod_depth=11500;
        delay=new DelayUnit;engine->chains[2].units[1]=delay;
        setp(*delay,DelayUnit::P_TIME,delay_time::free_value(7200));setp(*delay,DelayUnit::P_MIX,3000);setp(*delay,DelayUnit::P_FEEDBACK,4500);
        setp(*delay,DelayUnit::P_WOW,0);setp(*delay,DelayUnit::P_SAT,0);engine->chains[2].volume=23000;engine->chains[2].pan=1600;
        delay->params[DelayUnit::P_MIX].src=ModSource::SigilA;delay->params[DelayUnit::P_MIX].mod_depth=11000;
        auto*support=new PlayUnit;support->sample_slot=60;engine->chains[3].units[0]=support;
        setp(*support,PlayUnit::P_MODE,0);setp(*support,PlayUnit::P_GAIN,31000);engine->chains[3].volume=15500;engine->chains[3].pan=-3500;
        for(int bank=0;bank<3;++bank){
            Pattern& p=engine->pattern;p=Pattern{};for(auto&len:p.length)len=64;
            auto cut=[&](int chain,int step,int id){p.steps[chain][step].flags=SeqStep::Trig;CHECK(engine->set_step_lock(chain,step,0,PlayUnit::P_SLICE,q15(((id+1)*32768+16)/17)));};
            for(int bar=0;bar<4;++bar){
                if(bank==2){
                    // Prepared sparse half-time arrangement: no sampler/source swaps.
                    cut(0,bar*16,0);cut(0,bar*16+8,3);cut(0,bar*16+10,0);
                    cut(2,bar*16+12,15);cut(3,bar*16,bar*4);cut(3,bar*16+8,bar*4+2);
                }else for(int beat=0;beat<4;++beat){
                    const int step=bar*16+beat*4,id=bar*4+beat;
                    const int break_id=bank==1&&bar%2?(beat==1?15:beat==3?9:id):id;
                    cut(0,step,break_id);cut(2,step,bank==1?(id+8)%16:id);cut(3,step,bank==1?(id+4)%16:id);
                }
                const int root=bank==1?std::array<int,4>{29,27,34,31}[bar]:std::array<int,4>{31,34,29,27}[bar];
                const std::vector<int> notes=bank==2?std::vector<int>{0,10}:bank==1?std::vector<int>{0,2,6,9,11,14,15}:std::vector<int>{0,3,6,8,11,14};
                for(int step:notes){const int s=bar*16+step;p.steps[1][s].flags=SeqStep::Trig;
                    CHECK(engine->set_step_lock(1,s,0,OscUnit::P_PITCH,midi(root+((step==6||step==14)?7:0))));
                    p.microtiming[1][s]=step%2?3:0;
                }
            }
            if(bank==1){p.steps[2][60].set_retrig_count(2);p.steps[3][60].set_retrig_count(2);}
            engine->pattern_banks[bank]=p;
        }
        engine->pattern=engine->pattern_banks[0];
        for(int i=0;i<SIGIL_POINTS;++i)engine->sigils[0].set_point(i,q15(std::sin(i*6.283185307/SIGIL_POINTS)*14000));
        engine->set_sigil_sync(0,true);engine->set_sigil_div(0,0);
        g_view=View::Performance;g_overlay=Overlay::None;g_drag=Drag::None;performance_view_reset();
        auto save=[&](int side){const int x=side?285:25;performance_view_touch_down(x,210);g_ui_frame+=24;performance_view_touch_hold(x,210);performance_view_touch_release(true);};
        save(0);setp(*filter,FilterUnit::P_CUTOFF,17000);setp(*filter,FilterUnit::P_RES,13000);
        setp(*delay,DelayUnit::P_MIX,13000);setp(*delay,DelayUnit::P_FEEDBACK,13000);save(1);
        performance_view_touch_down(60,211);performance_view_touch_release(true);CHECK(engine->global_scene_topology_compatible());
        performance_view_leave();g_view=View::Sequencer;g_sel_chain=g_sel_slot=g_edit_chain=g_edit_slot=0;g_overlay=Overlay::None;
        engine->set_bpm(200);engine->set_sequencer_enabled(true);engine->transport_reset();engine->transport_play();refresh();sequencer_view_enter();
    }
    void play_capture(){engine->capture_ui_snapshot(g_ui_snapshot,0,0);play_focus_capture();param_lens_capture();}
    void play_down(int x,int y){CHECK(play_focus_touch_body(*play,x,y));}
    void play_up(){play_focus_touch_release(*play);g_drag=Drag::None;}
    void play_tap(int x,int y){play_down(x,y);play_up();}
};
struct CatalogEntry{UnitKind kind;const char*name;};
const CatalogEntry CATALOG[]={
    {UnitKind::Osc,"osc"},{UnitKind::Noise,"noise"},{UnitKind::Fm,"fm"},{UnitKind::Motor,"motor"},
    {UnitKind::SamplePlayer,"play"},{UnitKind::Grain,"gran"},{UnitKind::MicIn,"mic"},{UnitKind::Recorder,"rec"},
    {UnitKind::Filter,"filter"},{UnitKind::Vca,"vca"},{UnitKind::Delay,"delay"},{UnitKind::Fold,"fold"},
    {UnitKind::Bitcrush,"bit"},{UnitKind::Chorus,"chorus"},{UnitKind::Stranular,"strn"},{UnitKind::Reverb,"reverb"},
    {UnitKind::Ghost,"ghost"},{UnitKind::Ring,"ring"},{UnitKind::Tilt,"tilt"},{UnitKind::Strain,"strain"},
    {UnitKind::Shift,"shift"},{UnitKind::Bird,"bird"}
};
int catalogue_at(int n){if(n>=144&&n<216)return(n-144)/9;if(n>=360&&n<432)return 8+(n-360)/9;if(n>=540&&n<594)return 16+(n-540)/9;return -1;}
const char* scene_at(int n){if(n<36)return"intro";if(n<144)return"sequencer_open";if(n<216)return"catalogue_sources";if(n<360)return"performance_drop";if(n<432)return"catalogue_processors";if(n<540)return"sigils";if(n<594)return"catalogue_character";if(n<720)return"play_cuts_scratch";if(n<756)return"rhythm_bank_transition";if(n<828)return"sequencer_banks_locks";if(n<936)return"performance_finale";return"splash_music_tail";}
void capture_focus(){
    // Same bounded pre-GPU capture path as main; invalidate stale module pages too.
    fm_context_capture(fm_focus_selected());stranular_focus_capture();play_focus_capture();
    osc_focus_capture();noise_focus_capture();fm_focus_capture();motor_focus_capture();grain_focus_capture();vca_focus_capture();
    filter_focus_capture();fold_focus_capture();bit_focus_capture();tilt_focus_capture();ring_focus_capture();strain_focus_capture();
    mic_focus_capture();recorder_focus_capture();bird_focus_capture();shift_focus_capture();param_lens_capture();
}
void draw_focus(const Unit& u){
#define FOCUS(K,T,F) case UnitKind::K:F(static_cast<const T&>(u));break
    switch(u.kind){
        FOCUS(Osc,OscUnit,osc_focus_draw);FOCUS(Noise,NoiseUnit,noise_focus_draw);FOCUS(Fm,FmUnit,fm_focus_draw);FOCUS(Motor,MotorUnit,motor_focus_draw);
        FOCUS(SamplePlayer,PlayUnit,play_focus_draw);FOCUS(Grain,GrainUnit,grain_focus_draw);FOCUS(MicIn,MicUnit,mic_focus_draw);
        case UnitKind::Recorder:recorder_focus_draw(static_cast<const RecUnit&>(u),"");break;
        FOCUS(Filter,FilterUnit,filter_focus_draw);FOCUS(Vca,VcaUnit,vca_focus_draw);FOCUS(Delay,DelayUnit,delay_focus_draw);FOCUS(Fold,FoldUnit,fold_focus_draw);
        FOCUS(Bitcrush,BitUnit,bit_focus_draw);FOCUS(Chorus,ChorusUnit,chorus_focus_draw);case UnitKind::Stranular:CHECK(stranular_focus_draw_bottom());break;
        FOCUS(Reverb,ReverbUnit,reverb_focus_draw);FOCUS(Ghost,GhostUnit,ghost_focus_draw);FOCUS(Ring,RingUnit,ring_focus_draw);FOCUS(Tilt,TiltUnit,tilt_focus_draw);
        FOCUS(Strain,StrainUnit,strain_focus_draw);FOCUS(Shift,ShiftUnit,shift_focus_draw);FOCUS(Bird,BirdUnit,bird_focus_draw);default:CHECK(false);
    }
#undef FOCUS
}
void perform_tap(int x,int y){performance_view_touch_down(x,y);performance_view_touch_release(true);}
}
int main(int argc,char**argv){
    try{
        CHECK(argc==2);const std::filesystem::path out=argv[1];CHECK(!std::filesystem::exists(out));std::filesystem::create_directories(out);
        palette::set(palette::Theme::Pink);host_time_ms=1000;Reactive f;
        const auto pitch_lock=[&](){const auto&s=f.engine->sequence_pattern(1).steps[1][6];for(int i=0;i<s.lock_count;++i)if(s.locks[i].slot==0&&s.locks[i].param==OscUnit::P_PITCH)return int(s.locks[i].value);return -1;};
        int lock_before=-1,lock_after=-1,volume_before=-1,volume_low=-1,volume_restored=-1,morph_cutoff_before=-1,morph_cutoff_after=-1;
        int sigil_points_changed=0,preview_frames=0,scratch_moves=0,manual_verified=0;uint32_t last_preview_frame=0,manual_before=0;
        std::array<q15,SIGIL_POINTS> original_sigil;std::copy(f.engine->sigils[0].table,f.engine->sigils[0].table+SIGIL_POINTS,original_sigil.begin());
        auto catalogue=std::make_unique<Engine>();empty_engine(*catalogue);int previous_catalogue=-1;
        VideoStream bottom(out/"bottom.mkv",320),top(out/"top.mkv",400);
        std::ofstream timeline(out/"timeline.tsv");timeline<<"frame\tseconds\tscene\tmodule\tbank\tseq_step\tlive_source_frame\tsigil_a\tvolume0\tvolume1\tvolume2\tvolume3\tmutes\taction\tpreview_token\tpreview_source_frame\tcut_rate_q8\tmorph_q15\tcutoff_base\tcutoff_effective\tdelay_mix_base\tdelay_mix_effective\tbass_step6_b_lock\tframe_rms_pcm16\treply_source_frame\n";
        trackr::synth::Sample audio;audio.channels=2;audio.data.resize(AUDIO_FRAMES*2);
        uint32_t done=0;int max_objects=0,max_single=0;std::array<int,22> seen{};std::vector<std::pair<int,std::string>> actions;
        for(int n=0;n<1080;++n){
            host_time_ms=1000+uint64_t(n)*1000/FPS;g_ui_frame=200+2*n;g_eng=f.engine.get();g_audio=&f.audio;
            std::string action;auto log=[&](const std::string&s){if(!action.empty())action+="; ";action+=s;};
            const int cat=catalogue_at(n);
            if(n<144||(n>=756&&n<828&&!(n>=762&&n<=777)&&!(n>=780&&n<792)))sequencer_view_buttons(0,0,0);
            if(n==216||n==828){play_focus_release();stranular_focus_cancel();g_view=View::Performance;g_overlay=Overlay::None;g_drag=Drag::None;performance_view_buttons(0,0,0);log("enter real performance mixer");}
            if(n>=216&&n<360){
                if(n==225){volume_before=f.engine->chains[1].volume;performance_view_touch_down(118,110);log("bass fader pickup "+std::to_string(volume_before));}
                if(n>225&&n<=243)performance_view_touch_drag(118,110+(n-225)*2);
                if(n==244){performance_view_touch_release(true);volume_low=f.engine->chains[1].volume;CHECK(volume_low<volume_before/2);log("bass fader low "+std::to_string(volume_low));}
                if(n==252){perform_tap(40,182);CHECK(f.engine->chains[0].mute);log("mute main break; support percussion continues");}
                if(n==270){perform_tap(40,182);CHECK(!f.engine->chains[0].mute);performance_view_touch_down(118,146);log("break returns; bass fader rise");}
                if(n>270&&n<=288)performance_view_touch_drag(118,146-(n-270)*2);
                if(n==289){performance_view_touch_release(true);volume_restored=f.engine->chains[1].volume;CHECK(volume_restored>volume_low*2);log("bass fader restored "+std::to_string(volume_restored));}
                if(n==297){morph_cutoff_before=f.filter->params[FilterUnit::P_CUTOFF].base;performance_view_touch_down(60,211);log("a/b sound morph sweep: bass cutoff and ghost delay");}
                if(n>297&&n<=342)performance_view_touch_drag(60+(n-297)*4,211);
                if(n==343){performance_view_touch_release(true);morph_cutoff_after=f.filter->params[FilterUnit::P_CUTOFF].base;CHECK(morph_cutoff_after>morph_cutoff_before+6000);log("morph cutoff "+std::to_string(morph_cutoff_before)+" -> "+std::to_string(morph_cutoff_after));}
                if(n==356){CHECK(f.engine->sequence_step(2)%4!=0);CHECK(!f.reply->params[PlayUnit::P_SLICE].step_lock_override);manual_before=f.reply->visual_source_frame();perform_tap(196,36);CHECK(f.reply->visual_source_frame()==144000&&manual_before!=144000);++manual_verified;log("manual ghost roll off sequencer beat, cursor "+std::to_string(manual_before)+" -> 144000");}
            }
            if(n==432){performance_view_leave();g_view=View::Sigils;g_overlay=Overlay::None;g_sel_sigil=0;g_drag=Drag::None;sigils_forget_undo();CHECK(f.filter->params[FilterUnit::P_CUTOFF].src==ModSource::SigilA&&f.delay->params[DelayUnit::P_MIX].src==ModSource::SigilA);log("sigil a routes: bass filter cutoff depth11500; ghost delay wet depth11000");}
            if(n==441){sigils_view_touch_down(20,108);log("draw actual sigil curve");}
            if(n>441&&n<=522){const double t=(n-441)/81.;sigils_view_touch_drag(20+int(280*t),108-int(43*std::sin(t*18.84955592)+12*std::sin(t*37.69911184)));}
            if(n==523){sigils_view_touch_release();for(int i=0;i<SIGIL_POINTS;++i)sigil_points_changed+=original_sigil[i]!=f.engine->sigils[0].table[i];CHECK(sigil_points_changed>80);log("draw changed sigil points "+std::to_string(sigil_points_changed));}
            if(n==576){f.engine->request_pattern(2,false);CHECK(f.engine->active_pattern==2);log("scripted score section: sparse half-time bank c, no source swap");}
            if(n==594){
                g_view=View::Chains;g_overlay=Overlay::Editor;g_edit_chain=g_edit_slot=g_sel_chain=g_sel_slot=0;param_lens_cancel();g_drag=Drag::None;
                f.play_capture();f.play_tap(90,72);for(int i=0;i<20;++i)f.play_capture();log("real play slices mode, source waveform");
            }
            if(n==600){f.play_tap(120,230);log("repeat audition on through actual play control");}
            if(n==603){f.play_tap(42,128);log("select and repeat-audition stable cut index1");}
            if(n==612){f.play_down(50,46);log("shared cut rate drag");}
            if(n>612&&n<=627)play_focus_drag(*f.play,50+(n-612),46);
            if(n==628){f.play_up();CHECK(trackr::synth::SampleBank::instance().slot(61).slice_speed_q8[1]==316);log("shared stable cut index1 rate 256 -> 316, repeating audition live");}
            if(n==636){f.play_tap(150,72);log("switch to real source scratch");}
            if(n==639){f.play_down(55,128);log("silent scratch touchdown");}
            if(n==640)CHECK(g_drag==Drag::PlaySliceSeam);
            if(n>639&&n<708){const double t=(n-639)/69.;play_focus_drag(*f.play,55+int(205*t+26*std::sin(t*25.13274123)),128);}
            if(n==708){f.play_up();log("scratch release; sequencer source resumes");}
            if(n==720){f.engine->request_pattern(0,false);CHECK(f.engine->active_pattern==0);log("scripted half-time score ends; bank a restored before manual rail");play_focus_release();g_view=View::Performance;g_overlay=Overlay::None;g_drag=Drag::None;performance_view_buttons(0,0,0);perform_tap(260,10);CHECK(transition::active());log("open actual rhythm transition a to b");}
            if(n==723){performance_view_touch_down(20,135);log("rhythm rail pickup");}
            if(n>723&&n<=735)performance_view_touch_drag(20+(n-723)*24,135);
            if(n==736){performance_view_touch_release(true);CHECK(f.engine->pattern_transition().position==32767);log("rhythm rail at b endpoint");}
            if(n==747){perform_tap(280,228);CHECK(f.engine->active_pattern==1);log("keep bank b through actual controller");}
            if(n==756){transition::close();performance_view_leave();g_view=View::Sequencer;g_overlay=Overlay::None;g_drag=Drag::None;f.refresh();sequencer_view_enter();log("return sequencer, audible bank b");}
            if(n==762){sequencer_view_buttons(KEY_B,KEY_B,0);log("hold b edit-bank selector");}
            if(n>762&&n<774)sequencer_view_buttons(0,KEY_B,0);
            if(n==774)sequencer_view_buttons(KEY_DOWN,KEY_B|KEY_DOWN,0);
            if(n==775)sequencer_view_buttons(0,KEY_B,KEY_DOWN);
            if(n==776)sequencer_view_buttons(0,KEY_B,0);
            if(n==777){sequencer_view_buttons(0,0,KEY_B);CHECK(sequencer_edit_bank()==1);log("release edit-bank selector on audible b");}
            if(n>=780&&n<792){if(n%2==0)sequencer_view_buttons(KEY_R,KEY_R,0);else sequencer_view_buttons(0,0,KEY_R);}
            if(n==792){CHECK(sequencer_selected_layer()==6);log("actual lock mode (six r taps)");}
            if(n==795){sequencer_view_touch_down(layout::X0+6*layout::PITCH+layout::CELL_W/2,layout::ROWS[1]+layout::CELL_H/2);sequencer_view_touch_release(true);log("select bass step in lock mode");}
            if(n==804){lock_before=pitch_lock();CHECK(lock_before==midi(36));sequencer_view_touch_down(186,218);log("real bass pitch lock rail pickup y218, before "+std::to_string(lock_before));}
            if(n>804&&n<=813)sequencer_view_touch_hold(186+(n-804),218);
            if(n==814){sequencer_view_touch_release(true);lock_after=pitch_lock();CHECK(lock_after==35*32767/155&&lock_after!=lock_before);log("bank b step6 pitch lock changed "+std::to_string(lock_before)+" -> "+std::to_string(lock_after));}
            if(n>=828&&n<936){
                if(n==837){performance_view_touch_down(240,211);log("finale a/b morph return");}
                if(n>837&&n<=864)performance_view_touch_drag(240-(n-837)*5,211);
                if(n==865)performance_view_touch_release(true);
                if(n==873){perform_tap(196,182);CHECK(f.engine->chains[2].mute);log("one beat ghost snare mute");}
                if(n==882){perform_tap(196,182);performance_view_touch_down(100,211);CHECK(!f.engine->chains[2].mute);log("ghost snare return and bright morph climax");}
                if(n>882&&n<=918)performance_view_touch_drag(100+(n-882)*4,211);
                if(n==919)performance_view_touch_release(true);
                if(n==932){CHECK(f.engine->sequence_step(2)%4!=0);CHECK(!f.reply->params[PlayUnit::P_SLICE].step_lock_override);manual_before=f.reply->visual_source_frame();perform_tap(196,36);CHECK(f.reply->visual_source_frame()==144000&&manual_before!=144000);++manual_verified;log("final manual ghost roll off sequencer beat, cursor "+std::to_string(manual_before)+" -> 144000");}
            }
            if(n==1044){f.engine->transport_stop();log("real transport stop at34.8s; natural dsp tail");}
            if(!action.empty())actions.emplace_back(n,action);
            // Never more than128frames, matched to exact rational 30fps boundaries.
            const uint32_t begin=done,end=uint32_t(uint64_t(n+1)*RATE/FPS);
            while(done<end){const int count=std::min<int>(128,end-done);f.engine->render(audio.data.data()+done*2,count);done+=count;}
            if(n<VIDEO_FRAMES){
                const Unit* shown=nullptr;
                if(cat>=0){
                    g_eng=catalogue.get();g_view=View::Chains;g_overlay=Overlay::Editor;g_edit_chain=g_edit_slot=g_sel_chain=g_sel_slot=0;
                    if(cat!=previous_catalogue){
                        empty_engine(*catalogue);Unit*u=make_unit(CATALOG[cat].kind);CHECK(u);catalogue->chains[0].units[0]=u;
                        if(u->kind==UnitKind::SamplePlayer)static_cast<PlayUnit*>(u)->sample_slot=61;
                        if(u->kind==UnitKind::Grain)static_cast<GrainUnit*>(u)->sample_slot=61;
                        u->control_tick();u->trigger();param_lens_cancel();g_drag=Drag::None;previous_catalogue=cat;
                    }
                    shown=catalogue->chains[0].units[0];std::array<q15,256> discard{};
                    for(int d=0;d<1024;d+=128)catalogue->render(discard.data(),128);
                    catalogue->capture_ui_snapshot(g_ui_snapshot,0,0);capture_focus();++seen[cat];
                }else{
                    f.engine->capture_ui_snapshot(g_ui_snapshot,0,0);
                    if(n>=594&&n<720){shown=f.play;capture_focus();}
                    else if(n>=432&&n<540)sigils_capture();
                    else if((n>=216&&n<360)||(n>=720&&n<756)||n>=828)performance_view_tick();
                    else{g_view=View::Sequencer;g_overlay=Overlay::None;sequencer_capture_inspector();}
                }
                int shared=0;
                for(bool is_top:{true,false}){
                    module_inspector_surface(is_top);
                    {ModuleInspectorNoEngine frozen;if(is_top)top_draw();else if(shown)draw_focus(*shown);else if(g_view==View::Sigils)sigils_view_draw();else if(g_view==View::Performance)performance_view_draw();else sequencer_view_draw();}
                    CHECK(objects>0&&objects<8192);shared+=objects;max_single=std::max(max_single,objects);(is_top?top:bottom).frame();
                }
                CHECK(shared<8192);max_objects=std::max(max_objects,shared);
            }
            const auto& e=*f.engine;int mutes=0;for(int c=0;c<4;++c)if(e.chains[c].mute)mutes|=1<<c;
            timeline<<n<<'\t'<<double(n)/FPS<<'\t'<<scene_at(n)<<'\t'<<(cat>=0?CATALOG[cat].name:"")<<'\t'<<int(e.active_pattern)<<'\t'<<int(e.sequence_step(0))<<'\t'<<f.play->visual_source_frame()<<'\t'<<e.sigils[0].value;
            for(int c=0;c<4;++c)timeline<<'\t'<<e.chains[c].volume;
            timeline<<'\t'<<mutes<<'\t'<<action;
            uint32_t preview_token=0,preview_frame=0;
            for(uint32_t token=1;token<=64;++token)if(e.play_preview_active(token)){preview_token=token;preview_frame=e.play_preview_position(token);break;}
            if(preview_token)++preview_frames;
            if(n>=640&&n<708&&preview_token){if(preview_frame!=last_preview_frame)++scratch_moves;last_preview_frame=preview_frame;}
            if(n==628)CHECK(preview_token!=0);
            if(n==708)CHECK(preview_token==0);
            const auto&cutoff=f.filter->params[FilterUnit::P_CUTOFF];const auto&wet=f.delay->params[DelayUnit::P_MIX];
            CHECK(!cutoff.step_lock_override&&!wet.step_lock_override);
            long double frame_square=0;for(uint32_t i=begin*2;i<end*2;++i)frame_square+=int64_t(audio.data[i])*audio.data[i];
            timeline<<'\t'<<preview_token<<'\t'<<preview_frame<<'\t'<<trackr::synth::SampleBank::instance().slot(61).slice_speed_q8[1]
                <<'\t'<<e.scene_morph[1]<<'\t'<<cutoff.base<<'\t'<<cutoff.effective<<'\t'<<wet.base<<'\t'<<wet.effective<<'\t'<<pitch_lock()<<'\t'<<double(std::sqrt(frame_square/((end-begin)*2)))<<'\t'<<f.reply->visual_source_frame()<<'\n';
            g_eng=f.engine.get();g_audio=&f.audio;
        }
        bottom.finish();top.finish();CHECK(done==AUDIO_FRAMES);for(int x:seen)CHECK(x==9);
        CHECK(lock_before!=lock_after&&preview_frames>80&&scratch_moves>50&&manual_verified==2);
        int peak=0,clipped=0;long double square=0;for(q15 x:audio.data){peak=std::max(peak,std::abs(int(x)));clipped+=x==32767||x==-32768;square+=int64_t(x)*x;}
        CHECK(peak>0&&clipped==0);CHECK(trackr::synth::save_sample_wav((out/"engine.wav").c_str(),audio,RATE,done)==trackr::synth::WavSaveResult::Ok);
        std::ofstream meta(out/"capture.json");
        meta<<"{\n  \"provenance\":\"actual current C++ UI/controllers and Engine; deterministic host software raster, not console recording\",\n  \"bpm\":200,\"fps\":30,\"frames\":936,\"duration_seconds\":31.2,\"audio_duration_seconds\":36,\"audio_rate\":32000,\"channels\":2,\"audio_frames\":"<<done<<",\n  \"audio_peak_pcm16\":"<<peak<<",\"audio_rms_pcm16\":"<<double(std::sqrt(square/audio.data.size()))<<",\"clipped_samples\":"<<clipped<<",\"max_shared_objects\":"<<max_objects<<",\"max_single_surface_objects\":"<<max_single<<",\n";
        meta<<"  \"sound\":\"200bpm broken-drum and triangle/sub bass arrangement adapted from the NEW liked tools/render_breakbeat_audio.cpp, not the old FM/acid fixture. Three four-bar in-memory CC0 stems with16 stable beat cuts. Chain0 kick/backbeat PLAY; chain1 triangle/sub OSC-filter-VCA; chain2 ghost/pitched-roll snare PLAY-delay; chain3 swung hats/rim/conga PLAY. Real sequencer triggers all audible material. Bank b changes cuts, bass notes and rhythm. Scripted sparse half-time bank c19.2..24s. Transport stops34.8s; natural tail to36s. Every visible gesture affects this SAME Engine before its next PCM render. No external soundtrack, post-processing or time-varying normalization.\",\n";
        meta<<"  \"sample_license\":\"sample_library/starter_cc0/README.md and CC0-1.0.txt; kick_punch,kick_deep,snare_x0x,snare_synth,hat_closed,hat_bright,rimshot,conga. Existing files read only.\",\n";
        meta<<"  \"causal_assertions\":{\"basis\":\"actual controller writes and live DSP scalars, not meter/audio-correlation claims\",\"bass_volume_before\":"<<volume_before<<",\"bass_volume_low\":"<<volume_low<<",\"bass_volume_return\":"<<volume_restored<<",\"morph_cutoff_before\":"<<morph_cutoff_before<<",\"morph_cutoff_after\":"<<morph_cutoff_after<<",\"sigil_points_changed\":"<<sigil_points_changed<<",\"preview_active_frames\":"<<preview_frames<<",\"scratch_cursor_moves\":"<<scratch_moves<<",\"cut_index\":1,\"cut_rate_before\":256,\"cut_rate_after\":316,\"cut_rate_audition_live\":true,\"audible_bank_after_keep\":1,\"edited_bank\":1,\"edited_chain\":1,\"edited_step\":6,\"edited_slot\":0,\"edited_param\":0,\"pitch_lock_before\":"<<lock_before<<",\"pitch_lock_after\":"<<lock_after<<",\"lock_rail_y\":218,\"off_beat_manual_cursor_restarts\":"<<manual_verified<<",\"manual_frames\":[356,932],\"no_cutoff_or_delay_step_overrides\":true},\n";
        meta<<"  \"memory_sources\":[";for(size_t i=0;i<stems.size();++i){if(i)meta<<',';const auto&s=stems[i];meta<<"{\"name\":\""<<s.name<<"\",\"slot\":"<<s.slot<<",\"frames\":153600,\"cuts\":16,\"original_peak\":"<<s.peak<<",\"constant_source_gain\":"<<s.gain<<"}";}meta<<"],\n";
        meta<<"  \"audio_sections\":[";const double ranges[][2]={{1.2,4.8},{7.2,8.1},{8.4,9.0},{9.0,9.9},{14.4,18},{19.2,21.3},{21.3,23.6},{25.2,27.6},{27.6,31.2},{34.8,36}};
        for(size_t j=0;j<sizeof(ranges)/sizeof(ranges[0]);++j){if(j)meta<<',';long double sum=0;int pk=0;const int a=int(ranges[j][0]*RATE)*2,b=int(ranges[j][1]*RATE)*2;
            for(int i=a;i<b;++i){sum+=int64_t(audio.data[i])*audio.data[i];pk=std::max(pk,std::abs(int(audio.data[i])));}meta<<"{\"from\":"<<ranges[j][0]<<",\"to\":"<<ranges[j][1]<<",\"peak\":"<<pk<<",\"rms\":"<<double(std::sqrt(sum/(b-a)))<<"}";}
        meta<<"],\n";
        meta<<"  \"catalogue_boundary\":\"22 actual focus renders,9frames each; separate prepared Engine, its rendered audio discarded while main performance Engine continues. Catalogue is not the audible patch. MIC host stub has no hardware input; REC remains idle and writes no recording.\",\n";
        meta<<"  \"caveats\":\"GPU line/triangle coverage and living-paper texture are host approximations. Native snapshot capture before drawing; Engine/AudioOut pointers unavailable during draws. Initial fixtures and view changes scripted directly; visible performance edits use actual controllers. Native sequencer lookahead is retained; gesture frame timestamps are not onset claims. Seq hold-b selects edit bank only; actual audible bank-b adoption uses transition rail/keep-bank24..25.2s. No hardware fps/audio claims, production edits, generations, networking or deployment.\",\n  \"modules\":[";
        for(int i=0;i<22;++i){if(i)meta<<',';const int start=i<8?144+i*9:i<16?360+(i-8)*9:540+(i-16)*9;meta<<"{\"name\":\""<<CATALOG[i].name<<"\",\"start_frame\":"<<start<<",\"frames\":9}";}
        meta<<"],\n  \"scenes\":[";const int boundaries[]={0,36,144,216,360,432,540,594,720,756,828,936,1080};
        for(int i=0;i<12;++i){if(i)meta<<',';meta<<"{\"from\":"<<double(boundaries[i])/FPS<<",\"to\":"<<double(boundaries[i+1])/FPS<<",\"name\":\""<<scene_at(boundaries[i])<<"\"}";}
        meta<<"],\n  \"actions\":[";for(size_t i=0;i<actions.size();++i){if(i)meta<<',';meta<<"{\"frame\":"<<actions[i].first<<",\"action\":\""<<actions[i].second<<"\"}";}meta<<"]\n}\n";
        std::printf("reactive936frames /1152000stereo frames: peak=%d clipped=%d shared_objects=%d\n",peak,clipped,max_objects);return 0;
    }catch(const std::exception&e){std::fprintf(stderr,"reactive capture: %s\n",e.what());return 1;}
}
