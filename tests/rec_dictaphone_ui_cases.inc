// Current native main/editor/REC/PLAY + real crop service. REC WAV send uses the
// inherited explicit success/failure shim; disk/session proof is separate.
#include "../platform/3ds/rec_ui.h"
#include "../core/synth/wav_loader.h"
extern "C" void __real__ZN6seance2ui4lineEiiiiji(int,int,int,int,u32,int);
extern "C" void __wrap__ZN6seance2ui4lineEiiiiji(int a,int b,int c,int d,u32 colour,int width){__real__ZN6seance2ui4lineEiiiiji(a,b,c,d,colour,width);}
namespace rec_dictaphone_cases {
using C=RecUnit;using Bank=trackr::synth::SampleBank;using Result=trackr::synth::SampleEditResult;
int duals=0,peak=0,gestures=0;
void down(int x,int y){input::touch={x,y};input::frame(KEY_TOUCH,KEY_TOUCH);++gestures;}
void move(int x,int y){input::touch={x,y};input::frame(0,KEY_TOUCH);}
void up(){input::frame(0,0,KEY_TOUCH);}
void tap(int x,int y){down(x,y);up();}
struct Scene:RecFixture {
    Scene(){input::nav_opened=false;input::frame(0,0);g_clip_unit=nullptr;engine->set_external_sample_slot(-1);
        for(int c=0;c<NUM_CHAINS;++c)for(int p=0;p<UNITS_PER_CHAIN;++p)if(c||p)delete engine->replace_unit(c,p,nullptr);
        for(int i=0;i<trackr::synth::SAMPLE_BANK_SIZE;++i)Bank::instance().clear_slot(i);
        capture();}
    ~Scene(){delete g_clip_unit;g_clip_unit=nullptr;engine->interrupt_recorders();recorder_focus_release();param_lens_cancel();}
    void record(){rec_dictaphone_cases::tap(237,82);CHECK(rec().is_recording());feed();}
    void stop(){rec_dictaphone_cases::tap(83,82);CHECK(!rec().writer_open());}
    void back_to_rec(){editor_cancel_input();g_sel_chain=g_edit_chain=0;g_sel_slot=g_edit_slot=0;g_overlay=Overlay::Editor;g_view=View::Chains;g_seq_lock_armed=false;capture();input::frame(0,0);}
};
void bounds(){if(outside_rects||outside_lines||text_overlaps){std::fprintf(stderr,"rec width%d bounds%d/%d overlaps%d\n",width,outside_rects,outside_lines,text_overlaps);
    for(const auto&t:texts)std::fprintf(stderr," %d,%d %dx%d %s\n",t.x,t.y,t.w,t.h,t.text.c_str());}
    CHECK(objects>0&&objects<=8192&&!outside_rects&&!outside_lines&&!text_overlaps);
    for(const auto&t:texts)CHECK(t.x>=0&&t.y>=0&&t.x+t.w<=width&&t.y+t.h<=240);}
void surface(Scene&s,bool top){module_inspector_surface(top);{ModuleInspectorNoEngine hidden;if(top)CHECK(module_inspector_draw_top());else recorder_focus_draw(s.rec(),nullptr);}bounds();}
void pair(Scene&s){s.capture();editor_fullscreen_cases::prepare();const int dirty=mutations;
    surface(s,true);const auto a=pixels;const int n=objects;surface(s,false);const auto b=pixels;
    peak=std::max(peak,n+objects);CHECK(peak<=8192);++duals;auto saved=g_ui_snapshot;g_ui_snapshot={};host_time_ms+=123;
    surface(s,true);CHECK(pixels==a);surface(s,false);CHECK(pixels==b);g_ui_snapshot=saved;CHECK(mutations==dirty);
    if(!param_lens_is_open()){module_inspector_surface(false);editor_draw();CHECK(pixels==b);}}
void transport(){Scene s;auto&u=s.rec();down(237,82);CHECK(!u.writer_open());up();CHECK(u.is_recording());s.feed();auto frames=u.recorded_frames();auto session=u.session_token();
    CHECK(recorder_focus_action_at(80,200)==RecorderFocusAction::None);down(237,82);CHECK(u.is_recording());up();CHECK(u.is_paused()&&!u.is_recording());
    std::array<q15,512> noise;noise.fill(4444);u.capture_input(noise.data(),noise.size());CHECK(u.recorded_frames()==frames&&u.session_token()==session);pair(s);
    CHECK(recorder_focus_action_at(237,200)==RecorderFocusAction::None);tap(237,82);CHECK(u.is_recording()&&u.session_token()==session);u.capture_input(noise.data(),512);
    CHECK(u.recorded_frames()==frames+512&&Bank::instance().slot(u.sample_slot).data[frames]==4444);s.stop();CHECK(u.take_needs_save());
    down(237,82);CHECK(!recorder_focus_confirming());up();CHECK(recorder_focus_confirming()&&!u.writer_open());tap(260,166);CHECK(!recorder_focus_confirming()&&u.recorded_frames()==frames+512);
    down(237,82);move(160,30);move(237,82);up();CHECK(!recorder_focus_confirming());
    // Stop while paused finalizes, and a fresh dot must ask rather than append.
    tap(237,82);tap(160,166);CHECK(u.is_recording()&&u.recorded_frames()==0);s.feed();tap(237,82);CHECK(u.is_paused());s.stop();CHECK(!u.is_paused());
    // Automatic limit is a real stopped take, not resumable paused state.
    s.engine->interrupt_recorders();u.mark_take_saved(u.take_generation());tap(237,82);CHECK(u.is_recording());
    while(u.is_recording())u.capture_input(noise.data(),512);CHECK(u.recorded_frames()==u.active_target_frames());pair(s);
}
void settings_locks(){Scene s;auto&u=s.rec();const auto original=s.values();tap(269,10);down(130,111);CHECK(s.values()==original);move(137,111);up();CHECK(u.params[1].base==8192+7*32767/280);
    tap(260,81);CHECK(u.params[2].base==0);for(int q=1;q<=4;++q){tap(260,136);CHECK(int(s.engine->launch_quantize)==q%4);}pair(s);
    CHECK(editor_back());s.record();tap(237,82);CHECK(u.is_paused());tap(269,10);auto values=s.values();down(130,111);move(190,111);up();tap(260,81);tap(260,136);tap(260,162);tap(260,187);
    CHECK(s.values()==values&&!recorder_focus_confirming()&&rec_export_calls==0);CHECK(editor_back());s.stop();
    g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;CHECK(s.engine->set_step_lock(0,7,0,1,6000));
    down(80,54);move(86,54);up();CHECK(seq_display_value(0,1,0)==6000+6*32767/170&&s.values()==values);tap(14,32);CHECK(seq_display_value(0,1,123)==123);
    tap(230,50);CHECK(u.params[2].base==values[2]);tap(230,32);for(auto xy:{std::pair<int,int>{237,82},{83,82},{237,200},{269,10}})tap(xy.first,xy.second);
    CHECK(!u.writer_open()&&rec_export_calls==0);pair(s);
    s.engine->clear_track(0);delete s.engine->replace_unit(0,1,new FmUnit);for(int p=0;p<SEQ_LOCKS;++p)CHECK(s.engine->set_step_lock(0,7,1,p,100));
    const auto hash=s.hash();down(80,54);move(95,54);up();CHECK(s.hash()==hash&&s.values()==values&&g_seq_lock_flash>0);pair(s);
    g_seq_lock_armed=false;g_view=View::Chains;recorder_focus_select(C::P_LEN);s.open();CHECK(param_lens_is_open());pair(s);param_lens_cancel();
}
void send_retake(){Scene s;auto&u=s.rec();s.record();s.stop();const auto source=Bank::instance().slot(16).data;
    rec_export_succeeds=false;tap(237,200);CHECK(rec_export_calls==1&&u.take_needs_save()&&!s.engine->chains[0].units[1]);s.capture();surface(s,true);CHECK(module_inspector_text("wav save failed"));
    rec_export_succeeds=true;tap(237,200);CHECK(rec_export_calls==2&&!u.take_needs_save()&&g_edit_slot==1&&g_edit_chain==0&&g_drag==Drag::None);
    auto*play=static_cast<PlayUnit*>(s.engine->chains[0].units[1]);CHECK(play->kind==UnitKind::SamplePlayer&&play->pitch_mode()==PlayUnit::PitchMode::Normal&&play->modern());
    s.back_to_rec();s.engine->chains[1].mute=true;s.engine->chains[1].volume=12345;tap(83,200);CHECK(recorder_focus_confirming()&&rec_export_calls==2&&!s.engine->chains[1].units[0]);pair(s);
    tap(100,161);CHECK(!recorder_focus_confirming()&&g_edit_chain==1&&g_edit_slot==0&&s.engine->chains[0].units[1]==play);
    CHECK(s.engine->chains[1].mute&&s.engine->chains[1].volume==12345&&s.engine->chains[1].units[0]->kind==UnitKind::Grain);
    s.back_to_rec();g_clip_unit=clone_unit(play);delete s.engine->replace_unit(0,1,nullptr);delete s.engine->replace_unit(1,0,nullptr);
    tap(237,82);CHECK(u.is_recording()&&u.sample_slot!=16&&Bank::instance().slot(16).data==source);
    CHECK(static_cast<PlayUnit*>(g_clip_unit)->sample_slot==16);s.feed();s.stop();tap(269,10);tap(260,187);CHECK(recorder_focus_confirming());tap(85,161);CHECK(!u.recorded_frames()&&!u.writer_open()&&Bank::instance().slot(16).data==source);
}
void failures(){
    {Scene s;s.record();s.stop();for(int c=0;c<NUM_CHAINS;++c)for(int p=(c?0:1);p<UNITS_PER_CHAIN;++p)delete s.engine->replace_unit(c,p,new OscUnit);
        tap(83,200);CHECK(recorder_focus_confirming()&&rec_export_calls==0);tap(100,161);CHECK(rec_export_calls==0&&s.rec().recorded_frames()>0);pair(s);tap(265,161);}
    {Scene s;s.record();s.stop();tap(237,82);rec_export_succeeds=false;tap(58,166);CHECK(recorder_focus_confirming()&&s.rec().take_needs_save());rec_export_succeeds=true;
        auto*alias=new PlayUnit;alias->sample_slot=s.rec().sample_slot;delete s.engine->replace_unit(0,1,alias);
        const auto budget=Bank::instance().pcm_budget_bytes();Bank::instance().set_pcm_budget_for_test(1);tap(58,166);Bank::instance().set_pcm_budget_for_test(budget);CHECK(!s.rec().writer_open()&&s.rec().recorded_frames()>0);tap(262,166);}
    for(int reason=0;reason<8;++reason){Scene s;down(237,82);
        if(reason==0)g_overlay=Overlay::None;if(reason==1)g_view=View::Sigils;if(reason==2)g_edit_slot=1;if(reason==3)g_seq_lock_armed=true;
        if(reason==4)Bank::instance().clear_slot(16);if(reason==5)s.rec().restore_take(0);if(reason==6)recorder_focus_release();if(reason==7)input::frame(KEY_L,KEY_L|KEY_TOUCH);
        up();CHECK(!s.rec().writer_open());param_lens_cancel();g_seq_lock_armed=false;s.back_to_rec();}
    {Scene s;s.engine->transport_play();s.engine->launch_quantize=LaunchQuantize::Beat;tap(237,82);CHECK(s.engine->recorder_pending_command(0,0)==RecorderCommand::Start);pair(s);tap(83,82);CHECK(!s.engine->recorder_record_pending(0,0)&&!s.rec().writer_open());
        s.engine->launch_quantize=LaunchQuantize::Immediate;s.record();s.engine->launch_quantize=LaunchQuantize::Beat;tap(237,82);CHECK(s.rec().is_recording()&&s.engine->recorder_pending_command(0,0)==RecorderCommand::Pause);pair(s);}
}
void mic_return(){Scene s;Unit*rec=s.engine->replace_unit(0,0,new MicUnit);CHECK(s.engine->replace_unit(0,1,rec)==nullptr);g_sel_slot=g_edit_slot=1;s.capture();tap(269,10);tap(260,56);
    CHECK(g_edit_slot==0&&g_overlay==Overlay::Editor);input::frame(0,0);down(203,10);CHECK(g_edit_slot==0);up();CHECK(g_edit_slot==1);
    tap(269,10);tap(260,56);CHECK(g_edit_slot==0);delete s.engine->replace_unit(0,1,new RecUnit);s.module=s.engine->chains[0].units[1];input::frame(0,0);tap(203,10);CHECK(g_edit_slot==0);
}
void crop(){Scene s;s.record();s.stop();rec_export_succeeds=true;tap(237,200);auto*p=static_cast<PlayUnit*>(s.engine->chains[0].units[1]);
    char dir[]="/tmp/seance-rec-crop-XXXXXX";CHECK(mkdtemp(dir));RECORDING_DIR=dir;
    const int original=p->sample_slot;const auto pcm=Bank::instance().slot(original).data;const auto frames=Bank::instance().slot(original).playable_frames();
    for(int i=0;i<8;++i)s.capture();const auto before=p->edit_state();down(100,140);move(120,140);up();CHECK(play_edit_equal(before,p->edit_state()));
    down(14,126);move(44,126);up();CHECK(p->edit_state().start==uint64_t(30)*frames/207);const uint32_t start=p->edit_state().start;
    down(221,150);move(201,150);up();const uint32_t end=p->edit_state().end;CHECK(end==frames-uint64_t(20)*frames/207);const auto edit=p->edit_state();
    down(40,201);CHECK(p->sample_slot==original);move(90,201);move(40,201);up();CHECK(p->sample_slot==original);
    tap(40,201);CHECK(p->sample_slot!=original&&Bank::instance().slot(p->sample_slot).playable_frames()==end-start);const int cropped=p->sample_slot;
    CHECK(Bank::instance().slot(original).data==pcm);const auto&out=Bank::instance().slot(cropped).data;
    for(uint32_t i=0;i<end-start;++i)CHECK(out[i]==pcm[start+i]);CHECK(std::filesystem::exists(Bank::instance().provenance(cropped).path));
    s.capture();tap(116,228);CHECK(p->sample_slot==original&&play_edit_equal(p->edit_state(),edit));
    tap(116,228);tap(190,101);CHECK(p->sample_slot==cropped); // source redo returns directly to trim
    // Narrow overlap: top is IN, lower is OUT; empty middle isn't a hidden trim.
    auto narrow=p->edit_state();narrow.start=8000;narrow.end=8004;narrow.loop_start=8000;narrow.loop_end=8004;narrow.fade_in=narrow.fade_out=narrow.crossfade=0;CHECK(p->set_edit_state(narrow,Bank::instance().generation(cropped)));s.capture();
    const int x=14+int(uint64_t(8000)*207/(end-start));down(x,125);move(x-1,125);up();CHECK(p->edit_state().end==8004&&p->edit_state().start<8000);
    const uint32_t a=p->edit_state().start;down(x,157);move(x+2,157);up();CHECK(p->edit_state().start==a&&p->edit_state().end>8004);
    std::filesystem::remove_all(dir);RECORDING_DIR="/tmp/seance-play-ui-unused-export";
}
void frames(){for(auto theme:{palette::Theme::Graphite,palette::Theme::Pink,palette::Theme::Custom}){palette::set(theme);Scene s;pair(s);s.record();pair(s);tap(237,82);pair(s);tap(269,10);pair(s);CHECK(editor_back());s.stop();pair(s);tap(237,82);pair(s);tap(262,166);s.rec().mark_take_saved(s.rec().take_generation());pair(s);tap(269,10);pair(s);tap(260,187);pair(s);tap(237,161);}}
void capture(const std::filesystem::path&dir){CHECK(!std::filesystem::exists(dir));std::filesystem::create_directories(dir);
    for(auto theme:{palette::Theme::Graphite,palette::Theme::Pink}){palette::set(theme);Scene s;
        auto shot=[&](const char*name){s.capture();editor_fullscreen_cases::prepare();surface(s,true);dump_surface(dir,std::string(theme==palette::Theme::Graphite?"graphite_":"pink_")+name+"_top");surface(s,false);dump_surface(dir,std::string(theme==palette::Theme::Graphite?"graphite_":"pink_")+name+"_bottom");};
        shot("empty");s.record();shot("recording");tap(237,82);shot("paused");s.stop();shot("stopped");tap(269,10);shot("setup");CHECK(editor_back());tap(237,82);shot("retake");tap(262,166);
        rec_export_succeeds=true;tap(237,200);auto*p=static_cast<PlayUnit*>(s.engine->chains[0].units[1]);
        for(int i=0;i<8;++i)s.capture();down(14,126);move(44,126);up();down(221,150);move(201,150);up();
        auto play_shot=[&](const char*name){for(int i=0;i<8;++i)s.capture();editor_fullscreen_cases::prepare();
            for(bool top:{true,false}){module_inspector_surface(top);{ModuleInspectorNoEngine hidden;if(top)CHECK(module_inspector_draw_top());else play_focus_draw(*p);}bounds();
                dump_surface(dir,std::string(theme==palette::Theme::Graphite?"graphite_":"pink_")+name+(top?"_top":"_bottom"));}};
        play_shot("play_trim");char scratch[]="/tmp/seance-rec-capture-crop-XXXXXX";CHECK(mkdtemp(scratch));RECORDING_DIR=scratch;
        tap(40,201);play_shot("play_crop");std::filesystem::remove_all(scratch);RECORDING_DIR="/tmp/seance-play-ui-unused-export";
    }std::printf("rec capture: 16 actual C++ duals / synthetic recorded input + real crop, not MIC device\n");}
int proof(){int total=0,failed=0;auto run=[&](const char*name,auto fn){++total;try{fn();std::printf("ok: %s\n",name);}catch(const std::exception&e){++failed;std::fprintf(stderr,"FAIL %s: %s\n",name,e.what());}std::fflush(stdout);};
    run("owned record/pause/resume/stop/retake/limit",transport);run("setup, clock, parameter locks and lens",settings_locks);
    run("export failure, actual destination, repeat collision, clipboard-only COW",send_retake);run("capacity/staging/quantize/stale release",failures);
    run("validated mic round trip",mic_return);run("real direct crop WAV/PCM/undo/redo/narrow handles",crop);run("themes and frozen pointer-free native duals",frames);
    std::printf("rec dictaphone UI: %d/%d groups; %d duals; %d gestures; peak%d/8192 raw host objects; 0 inherited groups\n",total-failed,total,duals,gestures,peak);return failed?1:0;}
}
int main(int argc,char**argv){theme_test::select();editor_style::init();editor_fullscreen_cases::prepare();int result=2;
    if(argc==2&&std::string(argv[1])=="--proof-only")result=rec_dictaphone_cases::proof();
    if(argc==3&&std::string(argv[1])=="--capture-only"){rec_dictaphone_cases::capture(argv[2]);result=0;}
    editor_style::shutdown();return result;}
