// host-only expanded promo; real engine/controllers/renderers, no regression suites.
// build/run: python3 tools/capture_promo_showcase.py
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
void drum_source(int slot,bool support){
    constexpr int N=76800,STEP=2400;std::vector<int32_t> mix(N);
    auto add=[&](const char*name,const std::vector<int>&steps,double gain){
        trackr::synth::Sample hit;const std::string path=std::string("sample_library/starter_cc0/wav/")+name;
        CHECK(trackr::synth::load_wav_to_sample(path.c_str(),hit,RATE,320000)==trackr::synth::WavLoadResult::Ok);CHECK(hit.channels==1);
        for(int s:steps)for(size_t i=0;i<hit.data.size()&&i+s*STEP<mix.size();++i)mix[i+s*STEP]+=int(hit.data[i]*gain);
    };
    if(!support){
        add("kick_punch.wav",{0,6,8,11,16,19,22,24,30},.45);
        add("snare_x0x.wav",{4,12,20,28},.34);add("snare_synth.wav",{10,15,26,31},.09);
        add("hat_closed.wav",{0,2,4,6,8,10,12,14,16,18,20,22,24,26,28,30},.13);
        add("hat_bright.wav",{7,15,23,31},.06);add("rimshot.wav",{3,14,21,29},.12);
    }else{
        add("kick_deep.wav",{0,8,16,24},.29);add("clap_x0x.wav",{4,12,20,28},.15);
        add("maracas.wav",{1,3,5,7,9,11,13,15,17,19,21,23,25,27,29,31},.11);
        add("conga.wav",{3,10,15,18,23,29,31},.16);add("cowbell.wav",{6,22,27},.10);
    }
    trackr::synth::Sample sample;sample.data.resize(N);
    for(int i=0;i<N;++i){CHECK(std::abs(mix[i])<32767);sample.data[i]=q15(mix[i]);}
    for(int i=0;i<8;++i)sample.chops[i]=i*9600;
    CHECK(trackr::synth::SampleBank::instance().replace_sample(slot,std::move(sample),trackr::synth::SampleOrigin::Runtime,support?"cc0_200_support.wav":"cc0_200_break.wav")==trackr::synth::SampleEditResult::Ok);
}
struct Showcase : Fixture {
    PlayUnit* play=nullptr;OscUnit* bass=nullptr;FilterUnit* filter=nullptr;FmUnit* fm=nullptr;DelayUnit* delay=nullptr;
    Showcase(){
        empty_engine(*engine);drum_source(61,false);drum_source(60,true);
        play=new PlayUnit;play->sample_slot=61;engine->chains[0].units[0]=play;
        setp(*play,PlayUnit::P_MODE,0);setp(*play,PlayUnit::P_GAIN,23000);engine->chains[0].volume=24500;
        bass=new OscUnit;engine->chains[1].units[0]=bass;
        setp(*bass,OscUnit::P_PITCH,midi(36));setp(*bass,OscUnit::P_SHAPE,OscUnit::shape_anchor(OscUnit::Wave::Saw));
        setp(*bass,OscUnit::P_GAIN,12500);setp(*bass,OscUnit::P_DRIFT,0);
        filter=new FilterUnit;engine->chains[1].units[1]=filter;
        setp(*filter,FilterUnit::P_CUTOFF,10500);setp(*filter,FilterUnit::P_RES,12500);setp(*filter,FilterUnit::P_DRIVE,3500);
        auto*vca=new VcaUnit;engine->chains[1].units[2]=vca;
        setp(*vca,VcaUnit::P_ATTACK,vca_envelope::time_value(48));setp(*vca,VcaUnit::P_DECAY,vca_envelope::time_value(4400));
        engine->chains[1].volume=19500;
        fm=new FmUnit;engine->chains[2].units[0]=fm;
        setp(*fm,FmUnit::P_PITCH,midi(60));setp(*fm,FmUnit::P_RATIO,6553);setp(*fm,FmUnit::P_INDEX,3500);
        setp(*fm,FmUnit::P_FEEDBACK,800);setp(*fm,FmUnit::P_DECAY,27800);setp(*fm,FmUnit::P_GAIN,9300);
        setp(*fm,FmUnit::P_ATTACK,2500);setp(*fm,FmUnit::P_INDEX_FALL,13000);
        fm->params[FmUnit::P_INDEX].src=ModSource::SigilA;fm->params[FmUnit::P_INDEX].mod_depth=5100;
        filter->params[FilterUnit::P_CUTOFF].src=ModSource::SigilA;filter->params[FilterUnit::P_CUTOFF].mod_depth=3900;
        delay=new DelayUnit;engine->chains[2].units[1]=delay;
        setp(*delay,DelayUnit::P_TIME,delay_time::free_value(7200));setp(*delay,DelayUnit::P_MIX,8500);setp(*delay,DelayUnit::P_FEEDBACK,12000);
        setp(*delay,DelayUnit::P_WOW,0);setp(*delay,DelayUnit::P_SAT,0);engine->chains[2].volume=18000;engine->chains[2].pan=4500;
        auto*support=new PlayUnit;support->sample_slot=60;engine->chains[3].units[0]=support;
        setp(*support,PlayUnit::P_MODE,0);setp(*support,PlayUnit::P_GAIN,19000);engine->chains[3].volume=18000;engine->chains[3].pan=-3000;
        engine->pattern=Pattern{};
        for(int bank=0;bank<4;++bank){
            Pattern& p=engine->pattern;p=Pattern{};for(auto&len:p.length)len=32;
            for(int step=0;step<32;step+=4){
                p.steps[0][step].flags=SeqStep::Trig;p.steps[3][step].flags=SeqStep::Trig;
                int cut=step/4;if(bank==1&&step>=16)cut=(step/4==5)?7:(step/4==7)?5:cut;
                CHECK(engine->set_step_lock(0,step,0,PlayUnit::P_SLICE,q15(((cut+1)*32768+16)/17)));
                CHECK(engine->set_step_lock(3,step,0,PlayUnit::P_SLICE,q15(((step/4+1)*32768+16)/17)));
            }
            const int bs[]={0,2,3,6,8,10,11,14,16,18,19,22,24,26,27,30,31};
            const int bn[]={36,36,48,43,36,39,36,34,36,48,46,43,39,39,43,34,46};
            for(int i=0;i<17;++i){const int s=bs[i];p.steps[1][s].flags=SeqStep::Trig;
                CHECK(engine->set_step_lock(1,s,0,OscUnit::P_PITCH,midi(bn[i]+(bank==1?(i%3==0?7:0):0))));
                // Pulse-like cutoff accents, independent of the drawn Sigil contribution.
                CHECK(engine->set_step_lock(1,s,1,FilterUnit::P_CUTOFF,q15(8500+(i%4)*1800+(bank==1?2000:0))));
            }
            const int fs[]={2,5,10,13,17,21,25,29,31},fn[]={60,67,63,70,72,67,75,70,79};
            for(int i=0;i<9;++i){p.steps[2][fs[i]].flags=SeqStep::Trig;
                CHECK(engine->set_step_lock(2,fs[i],0,FmUnit::P_PITCH,midi(fn[i]+(bank==1?-5:0))));
            }
            if(bank==1){p.steps[2][29].set_retrig_count(2);p.steps[3][28].set_retrig_count(2);}
            engine->pattern_banks[bank]=p;
        }
        engine->pattern=engine->pattern_banks[0];
        for(int i=0;i<SIGIL_POINTS;++i)engine->sigils[0].set_point(i,q15(std::sin(i*6.283185307/SIGIL_POINTS)*14000));
        engine->set_sigil_sync(0,true);engine->set_sigil_div(0,0);
        g_view=View::Performance;g_overlay=Overlay::None;g_drag=Drag::None;performance_view_reset();
        auto save=[&](int side){const int x=side?285:25;performance_view_touch_down(x,210);g_ui_frame+=24;performance_view_touch_hold(x,210);performance_view_touch_release(true);};
        save(0);setp(*filter,FilterUnit::P_RES,21000);setp(*fm,FmUnit::P_INDEX,6300);setp(*fm,FmUnit::P_RATIO,8500);setp(*delay,DelayUnit::P_MIX,10500);save(1);
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
        palette::set(palette::Theme::Pink);host_time_ms=1000;Showcase f;
        auto catalogue=std::make_unique<Engine>();empty_engine(*catalogue);int previous_catalogue=-1;
        VideoStream bottom(out/"bottom.mkv",320),top(out/"top.mkv",400);
        std::ofstream timeline(out/"timeline.tsv");timeline<<"frame\tseconds\tscene\tmodule\tbank\tseq_step\tlive_source_frame\tsigil_a\tvolume0\tvolume1\tvolume2\tvolume3\tmutes\taction\tpreview_token\tpreview_source_frame\tcut_rate_q8\n";
        trackr::synth::Sample audio;audio.channels=2;audio.data.resize(AUDIO_FRAMES*2);
        uint32_t done=0;int max_objects=0,max_single=0;std::array<int,22> seen{};std::vector<std::pair<int,std::string>> actions;
        for(int n=0;n<1080;++n){
            host_time_ms=1000+uint64_t(n)*1000/FPS;g_ui_frame=200+2*n;g_eng=f.engine.get();g_audio=&f.audio;
            std::string action;auto log=[&](const char*s){if(!action.empty())action+="; ";action+=s;};
            const int cat=catalogue_at(n);
            if(n<144||(n>=756&&n<828&&!(n>=762&&n<=777)&&!(n>=780&&n<792)))sequencer_view_buttons(0,0,0);
            if(n==216||n==828){play_focus_release();stranular_focus_cancel();g_view=View::Performance;g_overlay=Overlay::None;g_drag=Drag::None;performance_view_buttons(0,0,0);log("enter real performance mixer");}
            if(n>=216&&n<360){
                if(n==225){performance_view_touch_down(118,110);log("bass fader pickup");}
                if(n>225&&n<=243)performance_view_touch_drag(118,110+(n-225)*2);
                if(n==244){performance_view_touch_release(true);log("bass fader low");}
                if(n==252){perform_tap(40,182);CHECK(f.engine->chains[0].mute);log("mute main break; support percussion continues");}
                if(n==270){perform_tap(40,182);CHECK(!f.engine->chains[0].mute);performance_view_touch_down(118,146);log("break returns; bass fader rise");}
                if(n>270&&n<=288)performance_view_touch_drag(118,146-(n-270)*2);
                if(n==289)performance_view_touch_release(true);
                if(n==297){performance_view_touch_down(60,211);log("a/b sound morph sweep");}
                if(n>297&&n<=342)performance_view_touch_drag(60+(n-297)*4,211);
                if(n==343)performance_view_touch_release(true);
                if(n==351){perform_tap(196,36);log("manual fm chain trigger");}
            }
            if(n==432){performance_view_leave();g_view=View::Sigils;g_overlay=Overlay::None;g_sel_sigil=0;g_drag=Drag::None;sigils_forget_undo();log("sigil a routed to fm index and filter cutoff");}
            if(n==441){sigils_view_touch_down(20,108);log("draw actual sigil curve");}
            if(n>441&&n<=522){const double t=(n-441)/81.;sigils_view_touch_drag(20+int(280*t),108-int(43*std::sin(t*18.84955592)+12*std::sin(t*37.69911184)));}
            if(n==523)sigils_view_touch_release();
            if(n==594){
                g_view=View::Chains;g_overlay=Overlay::Editor;g_edit_chain=g_edit_slot=g_sel_chain=g_sel_slot=0;param_lens_cancel();g_drag=Drag::None;
                f.play_capture();f.play_tap(90,72);for(int i=0;i<20;++i)f.play_capture();log("real play slices mode, source waveform");
            }
            if(n==603){f.play_tap(70,128);log("select and audition stable cut");}
            if(n==612){f.play_down(50,46);log("shared cut rate drag");}
            if(n>612&&n<=627)play_focus_drag(*f.play,50+(n-612),46);
            if(n==628)f.play_up();
            if(n==636){f.play_tap(150,72);log("switch to real source scratch");}
            if(n==639){f.play_down(55,128);log("silent scratch touchdown");}
            if(n==640)CHECK(g_drag==Drag::PlaySliceSeam);
            if(n>639&&n<708){const double t=(n-639)/69.;play_focus_drag(*f.play,55+int(205*t+26*std::sin(t*25.13274123)),128);}
            if(n==708){f.play_up();log("scratch release; sequencer source resumes");}
            if(n==720){play_focus_release();g_view=View::Performance;g_overlay=Overlay::None;g_drag=Drag::None;performance_view_buttons(0,0,0);perform_tap(260,10);CHECK(transition::active());log("open actual rhythm transition a to b");}
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
            if(n==804){sequencer_view_touch_down(220,205);log("actual lock value gesture");}
            if(n>804&&n<=813)sequencer_view_touch_hold(220+(n-804)*4,205);
            if(n==814)sequencer_view_touch_release(true);
            if(n>=828&&n<936){
                if(n==837){performance_view_touch_down(240,211);log("finale a/b morph return");}
                if(n>837&&n<=864)performance_view_touch_drag(240-(n-837)*5,211);
                if(n==865)performance_view_touch_release(true);
                if(n==873){perform_tap(196,182);log("one beat fm mute");}
                if(n==882){perform_tap(196,182);performance_view_touch_down(100,211);log("fm return and bright morph climax");}
                if(n>882&&n<=918)performance_view_touch_drag(100+(n-882)*4,211);
                if(n==919)performance_view_touch_release(true);
                if(n==927){perform_tap(196,36);log("final manual fm accent");}
            }
            if(n==1044){f.engine->transport_stop();log("real transport stop at34.8s; natural dsp tail");}
            if(!action.empty())actions.emplace_back(n,action);
            // Never more than128frames, matched to exact rational 30fps boundaries.
            const uint32_t end=uint32_t(uint64_t(n+1)*RATE/FPS);
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
            timeline<<'\t'<<preview_token<<'\t'<<preview_frame<<'\t'<<trackr::synth::SampleBank::instance().slot(61).slice_speed_q8[1]<<'\n';
            g_eng=f.engine.get();g_audio=&f.audio;
        }
        bottom.finish();top.finish();CHECK(done==AUDIO_FRAMES);for(int x:seen)CHECK(x==9);
        int peak=0,clipped=0;long double square=0;for(q15 x:audio.data){peak=std::max(peak,std::abs(int(x)));clipped+=x==32767||x==-32768;square+=int64_t(x)*x;}
        CHECK(peak>0&&clipped==0);CHECK(trackr::synth::save_sample_wav((out/"engine.wav").c_str(),audio,RATE,done)==trackr::synth::WavSaveResult::Ok);
        std::ofstream meta(out/"capture.json");
        meta<<"{\n  \"provenance\":\"actual current C++ UI/controllers and Engine; deterministic host software raster, not console recording\",\n  \"bpm\":200,\"fps\":30,\"frames\":936,\"duration_seconds\":31.2,\"audio_duration_seconds\":36,\"audio_rate\":32000,\"channels\":2,\"audio_frames\":"<<done<<",\n  \"audio_peak_pcm16\":"<<peak<<",\"audio_rms_pcm16\":"<<double(std::sqrt(square/audio.data.size()))<<",\"clipped_samples\":"<<clipped<<",\"max_shared_objects\":"<<max_objects<<",\"max_single_surface_objects\":"<<max_single<<",\n";
        meta<<"  \"sound\":\"200bpm original two-bar breakbeat/acid riff; eight stable beat cuts in two in-memory CC0 drum composites, real OSC saw/filter/VCA bass, FM/dotted-eighth delay. Prepared a/b timbre scenes, independent pattern-b pitch/cut/retrigger variation, audible real faders/mutes/morph/sigil/scratch/transition gestures. Transport stops34.8s, effects tail continues to36s. No external soundtrack or post-normalization.\",\n";
        meta<<"  \"sample_license\":\"sample_library/starter_cc0/README.md and CC0-1.0.txt; kick_punch,kick_deep,snare_x0x,snare_synth,hat_closed,hat_bright,rimshot,clap_x0x,maracas,conga,cowbell. Existing files read only.\",\n";
        meta<<"  \"catalogue_boundary\":\"22 actual focus renders,9frames each; separate prepared Engine, its rendered audio discarded while main performance Engine continues. Catalogue is not the audible patch. MIC host stub has no hardware input; REC remains idle and writes no recording.\",\n";
        meta<<"  \"caveats\":\"GPU line/triangle coverage and living-paper texture are host approximations. Native snapshot capture before drawing; Engine/AudioOut pointers unavailable during draws. Initial fixtures and view changes scripted directly; visible performance edits use actual controllers. Seq hold-b selects edit bank only; actual audible bank-b adoption uses transition rail/keep-bank24..25.2s. No hardware fps/audio claims, production edits, generations, networking or deployment.\",\n  \"modules\":[";
        for(int i=0;i<22;++i){if(i)meta<<',';const int start=i<8?144+i*9:i<16?360+(i-8)*9:540+(i-16)*9;meta<<"{\"name\":\""<<CATALOG[i].name<<"\",\"start_frame\":"<<start<<",\"frames\":9}";}
        meta<<"],\n  \"scenes\":[";const int boundaries[]={0,36,144,216,360,432,540,594,720,756,828,936,1080};
        for(int i=0;i<12;++i){if(i)meta<<',';meta<<"{\"from\":"<<double(boundaries[i])/FPS<<",\"to\":"<<double(boundaries[i+1])/FPS<<",\"name\":\""<<scene_at(boundaries[i])<<"\"}";}
        meta<<"],\n  \"actions\":[";for(size_t i=0;i<actions.size();++i){if(i)meta<<',';meta<<"{\"frame\":"<<actions[i].first<<",\"action\":\""<<actions[i].second<<"\"}";}meta<<"]\n}\n";
        std::printf("showcase936frames /1152000stereo frames: peak=%d clipped=%d shared_objects=%d\n",peak,clipped,max_objects);return 0;
    }catch(const std::exception&e){std::fprintf(stderr,"showcase capture: %s\n",e.what());return 1;}
}
