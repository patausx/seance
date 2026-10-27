// Focused current-main/controller and frozen C++ raster proof; no inherited suite run.
#include "../platform/3ds/gran_ui.h"
#include "../core/synth/wav_loader.h"
// Observe the real semantic line call, then execute it unchanged. Axis lines are
// rectangles in today's pixel renderer, so the old C2D-line-only probe is stale.
namespace gran_line_probe {
std::vector<GranLine>* lines=nullptr;
struct Scope {explicit Scope(std::vector<GranLine>&out){lines=&out;} ~Scope(){lines=nullptr;}};
}
extern "C" void __real__ZN6seance2ui4lineEiiiiji(int,int,int,int,u32,int);
extern "C" void __wrap__ZN6seance2ui4lineEiiiiji(int x0,int y0,int x1,int y1,u32 c,int thickness){
    if(gran_line_probe::lines)gran_line_probe::lines->push_back({x0,y0,x1,y1,c});
    __real__ZN6seance2ui4lineEiiiiji(x0,y0,x1,y1,c,thickness);
}
namespace gran_perform_cases {
using C=GrainUnit;namespace geo=gran_ui;
int duals=0,peak=0,gestures=0,waves=0;
void down(int x,int y){input::touch={x,y};input::frame(KEY_TOUCH,KEY_TOUCH);++gestures;}
void move(int x,int y){input::touch={x,y};input::frame(0,KEY_TOUCH);}
void up(){input::frame(0,0,KEY_TOUCH);}
void tap(int x,int y){down(x,y);up();}
struct Scene:GranFixture {
    C&u;
    Scene():u(*gran){input::nav_opened=false;input::frame(0,0);u.clean=true;config();u.params[C::P_SCAN].base=0;snap_all();capture();}
    ~Scene(){grain_focus_release();param_lens_cancel();}
    void snap_all(){for(auto&p:u.params)p.snap();u.control_tick();}
    std::array<q15,12> all()const{std::array<q15,12>a{};for(int p=0;p<12;++p)a[p]=u.params[p].base;return a;}
    static int index(int p){const int ids[]={0,1,2,3,4,0,1,3,4,5,2,5};return ids[p];}
    static std::pair<int,int> point(int p){int i=index(p);return {56+104*(i%3),190+34*(i/3)};}
};
void bounds(){if(outside_rects||outside_lines||text_overlaps){std::fprintf(stderr,"gran w%d bounds %d/%d overlap%d\n",width,outside_rects,outside_lines,text_overlaps);
    for(const auto&t:texts)std::fprintf(stderr," %d,%d %dx%d %s\n",t.x,t.y,t.w,t.h,t.text.c_str());}
    CHECK(objects>0&&objects<=8192&&!outside_rects&&!outside_lines&&!text_overlaps);
    for(const auto&t:texts)CHECK(t.x>=0&&t.y>=0&&t.x+t.w<=width&&t.y+t.h<=240);}
void surface(Scene&s,bool top){module_inspector_surface(top);{ModuleInspectorNoEngine hidden;
    if(top)CHECK(module_inspector_draw_top());else grain_focus_draw(s.u);}bounds();}
void pair(Scene&s){s.capture();editor_fullscreen_cases::prepare();const int dirty=mutations;
    surface(s,true);const auto a=pixels;const int n=objects;surface(s,false);const auto b=pixels;
    CHECK(n+objects<=8192);peak=std::max(peak,n+objects);++duals;
    const auto snapshot=g_ui_snapshot;g_ui_snapshot={};host_time_ms+=10000;
    surface(s,true);CHECK(pixels==a);surface(s,false);CHECK(pixels==b);g_ui_snapshot=snapshot;CHECK(mutations==dirty);
    if(!param_lens_is_open()){module_inspector_surface(false);editor_draw();CHECK(pixels==b);}
}
void controls(){
    for(bool lock:{false,true})for(int p=0;p<12;++p){Scene s;s.pick(p);s.u.params[p].base=12000;s.snap_all();
        if(lock){g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;g_view=View::Sequencer;CHECK(s.engine->set_step_lock(0,7,0,p,9000));}
        const auto before=s.all();const auto xy=s.point(p);const int start=lock?9000:12000,range=(p==3||p==11)?65534:32767;
        down(xy.first,xy.second);CHECK(g_drag==Drag::GrainPosition&&s.all()==before);move(xy.first,xy.second);CHECK(s.all()==before);
        move(xy.first+6,xy.second);CHECK(seq_display_value(0,p,s.u.params[p].base)==start+6*range/84);if(lock)CHECK(s.all()==before);up();
        const int dirty=mutations;tap(180,9);CHECK(mutations==dirty);const int fine=seq_display_value(0,p,s.u.params[p].base);
        down(xy.first,xy.second);move(xy.first+1,xy.second);up();CHECK(seq_display_value(0,p,s.u.params[p].base)==fine+range/(84*32));
        if(lock){tap(xy.first-42,xy.second-25);CHECK(seq_display_value(0,p,before[p])==before[p]);}
    }
    // GRAN header scan moved right of the fixed title; use its visible action centre.
    Scene s;s.u.params[C::P_SCAN].base=12000;tap(148,9);CHECK(s.u.params[C::P_SCAN].base==0&&grain_focus_selected()==C::P_SCAN);
    const int dirty=mutations;tap(148,9);CHECK(mutations==dirty);
    s.pick(C::P_SIZE);s.u.params[C::P_SIZE].base=12000;down(160,190);s.u.params[C::P_SIZE].base=6000;move(160,190);CHECK(s.u.params[C::P_SIZE].base==6000);up();
    // Physical hit rectangles match approved separations, especially tab/wave seam.
    std::vector<geo::Box>b={geo::scan,geo::fine,geo::bypass,geo::pad,geo::source,geo::wave};
    for(int i=0;i<3;++i){b.push_back(geo::mode(i));b.push_back(geo::tab(i));}for(int i=0;i<6;++i)b.push_back(geo::param(i));
    for(size_t i=0;i<b.size();++i){const auto a=b[i];CHECK(a.w>=32&&a.h>=24&&a.x>=0&&a.y>=0&&a.x+a.w<=320&&a.y+a.h<=240);
        for(size_t j=0;j<i;++j){const auto q=b[j];CHECK(a.x+a.w<=q.x||q.x+q.w<=a.x||a.y+a.h<=q.y||q.y+q.h<=a.y);}}
}
void config_pad(){Scene s;auto old=s.all();int dirty=mutations;
    down(95,40);CHECK(s.u.launch_mode==C::LaunchMode::Flow);up();CHECK(s.u.launch_mode==C::LaunchMode::Gate&&mutations==dirty+1);
    dirty=mutations;down(260,40);CHECK(s.u.gate_open());s.process(6400);const auto n=s.u.visual_spawned();
    for(int i=0;i<5;++i)move(261,41);
    CHECK(s.u.visual_spawned()==n&&mutations==dirty);up();CHECK(!s.u.gate_open());
    s.process(64000);CHECK(s.u.visual_spawned()==n);CHECK(s.all()==old);
    down(260,40);CHECK(s.u.gate_open());move(210,40);CHECK(!s.u.gate_open());move(260,40);up();CHECK(!s.u.gate_open());
    down(160,40);move(200,40);move(160,40);up();CHECK(s.u.launch_mode==C::LaunchMode::Gate);
    down(160,40);grain_focus_release();up();CHECK(s.u.launch_mode==C::LaunchMode::Gate);
    tap(160,40);CHECK(s.u.launch_mode==C::LaunchMode::Burst);s.u.reset();down(260,40);s.process(32000);CHECK(s.u.visual_spawned()==4);move(260,40);s.process(32000);CHECK(s.u.visual_spawned()==4);up();
    dirty=mutations;tap(290,68);CHECK(mutations==dirty);tap(75,178);CHECK(s.u.window==C::Window::Hann);tap(240,178);CHECK(s.u.burst_count==5);
    tap(80,211);CHECK(mutations==dirty+2);tap(240,211);CHECK(s.u.clock_sync);tap(240,211);CHECK(s.u.sync_division==1);
    tap(242,68);s.pick(C::P_ATTACK);const q15 atk=s.u.params[C::P_ATTACK].base;down(56,224);move(80,224);up();CHECK(s.u.params[C::P_ATTACK].base==atk);
    s.capture();surface(s,false);CHECK(module_inspector_text("stored"));
    s.u.clean=false;s.u.reset();tap(95,40);CHECK(!s.u.clean);tap(148,9);CHECK(s.u.clean);CHECK(s.all()==old);
}
void ownership(){
    for(int reason=0;reason<11;++reason){Scene s;s.u.set_launch_mode(C::LaunchMode::Gate);down(260,40);CHECK(s.u.gate_open());
        Unit* parked=nullptr;auto&bank=trackr::synth::SampleBank::instance();auto&sample=bank.slot(Scene::SAMPLE);
        if(reason==0)g_edit_chain=1;
        if(reason==1)g_edit_slot=1;
        if(reason==2)g_overlay=Overlay::None;
        if(reason==3)g_view=View::Sigils;
        if(reason==4)g_seq_lock_armed=true;
        if(reason==5)g_drag=Drag::None;
        if(reason==6)sample.logical_frames=1000;
        if(reason==7){auto data=std::move(sample);bank.swap_sample_unchecked(Scene::SAMPLE,data,trackr::synth::SampleOrigin::SdWav,"replacement",false);}
        if(reason==8)parked=s.engine->replace_unit(0,0,new C);
        if(reason==9)s.u.bypass=true;
        if(reason==10)s.engine->chains[0].mute=true;
        grain_focus_input(0,KEY_TOUCH,false);CHECK(!s.u.gate_open());
        if(parked)delete s.engine->replace_unit(0,0,parked);
        g_edit_chain=g_edit_slot=0;g_overlay=Overlay::Editor;g_view=View::Chains;g_seq_lock_armed=false;s.u.bypass=false;s.engine->chains[0].mute=false;sample.logical_frames=UINT32_MAX;
        move(260,40);up();CHECK(!s.u.gate_open());
    }
    for(u32 key:{KEY_A,KEY_B,KEY_X,KEY_START,KEY_Y,KEY_SELECT,KEY_R,KEY_L}){Scene s;s.u.set_launch_mode(C::LaunchMode::Gate);down(260,40);
        input::frame(key,key|KEY_TOUCH);CHECK(!s.u.gate_open());input::frame(0,KEY_TOUCH,key);up();input::frame(0,0);}
    Scene s;s.u.set_launch_mode(C::LaunchMode::Gate);down(260,40);grain_focus_input(0,KEY_TOUCH,true);CHECK(!s.u.gate_open());up();
}
void locks_lens(){Scene s;const auto base=s.all();s.u.set_launch_mode(C::LaunchMode::Burst);
    g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;g_view=View::Sequencer;
    const auto count=s.u.burst_count;for(auto xy:{std::pair<int,int>{95,40},{260,40},{245,9},{80,65}})tap(xy.first,xy.second);
    CHECK(s.u.launch_mode==C::LaunchMode::Burst&&!s.u.bypass&&s.u.burst_remaining()==0&&g_overlay==Overlay::Editor);
    tap(290,68);tap(75,178);tap(240,178);tap(240,211);CHECK(s.u.window==C::Window::Linear&&s.u.burst_count==count&&!s.u.clock_sync);
    const int dirty=mutations;tap(80,211);tap(180,9);CHECK(mutations==dirty);
    CHECK(s.engine->set_step_lock(0,7,0,C::P_SCAN,12000));tap(148,9);CHECK(seq_display_value(0,C::P_SCAN,123)==0&&s.all()==base);
    s.engine->clear_track(0);delete s.engine->replace_unit(0,1,new FmUnit);for(int p=0;p<SEQ_LOCKS;++p)CHECK(s.engine->set_step_lock(0,7,1,p,100));
    s.pick(C::P_SIZE);const auto hash=s.hash();down(160,190);move(168,190);up();CHECK(s.hash()==hash&&s.all()==base&&g_seq_lock_flash>0);pair(s);
    g_seq_lock_armed=false;g_view=View::Chains;input::frame(0,0);s.u.set_launch_mode(C::LaunchMode::Gate);down(260,40);
    input::frame(KEY_L,KEY_L|KEY_TOUCH);CHECK(param_lens_is_open()&&!s.u.gate_open());s.capture();module_inspector_surface(true);CHECK(!grain_focus_draw_top());CHECK(module_inspector_draw_top());
    input::frame(0,KEY_TOUCH,KEY_L);move(270,40);up();input::frame(0,0);CHECK(!param_lens_is_open()&&!s.u.gate_open());
    s.u.set_window(C::Window::Hann);s.pick(C::P_ATTACK);input::frame(KEY_L,KEY_L);s.capture();module_inspector_surface(true);CHECK(module_inspector_draw_top());CHECK(module_inspector_text("full hann"));
    input::frame(0,0,KEY_L);input::frame(0,0);
}
void waveform(){Scene s;auto&sample=trackr::synth::SampleBank::instance().slot(Scene::SAMPLE);s.u.set_launch_mode(C::LaunchMode::Gate);
    for(int nf:{1,2,255,256,1024,64000})for(int ch:{1,2}){sample.channels=ch;sample.logical_frames=nf;sample.data.resize((nf+4)*ch);
        for(int i=0;i<nf+4;++i)for(int c=0;c<ch;++c)sample.data[i*ch+c]=q15((i*877+c*999)%65536-32768);
        s.u.reset();s.u.params[0].base=16384;s.snap_all();s.capture();editor_fullscreen_cases::prepare();
        for(bool top:{false,true}){std::vector<GranLine>lines;gran_line_probe::Scope probe(lines);surface(s,top);
            const int cols=top?373:293,span=top?nf:(nf+3)/4,center=nf/2,start=top?0:std::min(std::max(0,center-span/2),nf-span),amp=top?30:26,zero=top?112:115;
            for(int x=0;x<cols;++x){const int a=start+int64_t(x)*span/cols,b=start+int64_t(x+1)*span/cols,n=std::min(8,std::max(1,b-a));int lo=32767,hi=-32768;
                for(int j=0;j<n;++j){int at=std::min<int64_t>(nf-1,a+(n>1?int64_t(j)*(b-a-1)/(n-1):0));for(int c=0;c<ch;++c){lo=std::min(lo,int(sample.data[at*ch+c]));hi=std::max(hi,int(sample.data[at*ch+c]));}}
                bool found=false;for(const auto&l:lines)if(l.x0==14+x&&l.x1==l.x0&&l.y0==zero-hi*amp/32768&&l.y1==zero-lo*amp/32768)found=true;CHECK(found);}
            ++waves;
        }
    }
    sample.channels=1;sample.logical_frames=UINT32_MAX;sample.data.assign(64000,10000);s.u.reset();s.u.params[0].base=12000;s.snap_all();
    s.capture();const auto before=s.all();down(160,113);move(168,113);s.capture();move(168,113);up();auto expected=before;expected[0]+=int64_t(8)*32768*16000/(292ll*64000);CHECK(s.all()==expected);
    const auto old=s.all();down(160,113);sample.logical_frames=256;move(180,113);up();CHECK(s.all()==old);
    sample.data.clear();s.capture();surface(s,false);CHECK(module_inspector_text("no source"));down(260,40);CHECK(!s.u.gate_open());up();
}
void frames(){
    for(auto theme:{palette::Theme::Graphite,palette::Theme::Pink,palette::Theme::Custom}){palette::set(theme);Scene s;
        for(int mode=0;mode<3;++mode)for(int shape=0;shape<2;++shape){s.u.set_launch_mode(C::LaunchMode(mode));s.u.set_window(C::Window(shape));
            s.u.reset();s.snap_all();const auto token=s.u.pad_begin();s.process(11520);
            for(int tab=0;tab<3;++tab){tap(201+42*tab,68);pair(s);}
            s.u.pad_end(token);
        }
        for(int p=0;p<12;++p)for(int raw:{p==3||p==11?-32767:0,32767}){s.u.set_window(C::Window::Linear);s.u.params[p].base=q15(raw);s.snap_all();s.pick(p);pair(s);}
        for(int state=0;state<4;++state){s.u.bypass=state==0;s.engine->chains[0].mute=state==1;s.u.clean=state!=2;
            if(state==3){g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;g_view=View::Sequencer;}pair(s);
            g_seq_lock_armed=false;g_view=View::Chains;}
    }
}
void capture(const std::filesystem::path&dir){CHECK(!std::filesystem::exists(dir));std::filesystem::create_directories(dir);
    for(auto theme:{palette::Theme::Graphite,palette::Theme::Pink}){palette::set(theme);Scene s;auto&sample=trackr::synth::SampleBank::instance().slot(Scene::SAMPLE);
        CHECK(trackr::synth::load_wav_to_sample("local_sample_library/sdcard/3ds/seance/samples/granular/gran_dark_atmos_01.wav",sample,32000,320000,{0,true})==trackr::synth::WavLoadResult::Ok);
        trackr::synth::SampleBank::instance().set_provenance(Scene::SAMPLE,trackr::synth::SampleOrigin::SdWav,"dark atmos",false);
        const int values[]={16384,25795,1956,8192,32767,20479,0,16384,16384,0,32767,0};for(int p=0;p<12;++p)s.u.params[p].base=q15(values[p]);
        auto shot=[&](const char*name){s.capture();editor_fullscreen_cases::prepare();surface(s,true);dump_surface(dir,std::string(theme==palette::Theme::Graphite?"graphite_":"pink_")+name+"_top");
            surface(s,false);dump_surface(dir,std::string(theme==palette::Theme::Graphite?"graphite_":"pink_")+name+"_bottom");};
        s.u.reset();s.snap_all();s.pick(C::P_SIZE);s.process(11520);shot("flow");
        s.u.set_launch_mode(C::LaunchMode::Gate);s.u.reset();shot("gate_wait");down(260,40);s.process(11520);shot("gate_hold");up();
        s.u.set_launch_mode(C::LaunchMode::Burst);s.u.set_window(C::Window::Hann);s.u.reset();down(260,40);up();s.process(10240);tap(290,68);shot("burst_setup");
        tap(245,68);shot("hann_cloud");s.pick(C::P_SCAN);s.u.params[C::P_SCAN].base=-8192;s.snap_all();s.process(2048);shot("reverse_scan");
    }
    std::printf("gran capture: 12 actual C++ duals; real WAV; 0 regression groups\n");
}
int proof(){int total=0,failed=0;auto run=[&](const char*name,auto fn){++total;try{fn();std::printf("ok: %s\n",name);}catch(const std::exception&e){++failed;std::fprintf(stderr,"FAIL %s: %s\n",name,e.what());}std::fflush(stdout);};
    run("12 controls/base+lock/fine/hold/hit separation",controls);run("release-only mode/config and independent pad",config_pad);
    run("main keys/lift/out-back/stale source/unit/context gate cancellation",ownership);run("full locks and real held-L fallback",locks_lens);
    run("independent two-screen PCM probes + relative waveform ownership",waveform);run("native extrema/themes/pages/modes/windows frozen frames",frames);
    std::printf("gran perform UI: %d/%d groups; %d duals; %d gestures; %d waveform cases; peak%d/8192 raw host objects; 0 inherited groups\n",total-failed,total,duals,gestures,waves,peak);return failed?1:0;}
}
int main(int argc,char**argv){theme_test::select();editor_style::init();editor_fullscreen_cases::prepare();int result=2;
    if(argc==2&&std::string(argv[1])=="--proof-only")result=gran_perform_cases::proof();
    if(argc==3&&std::string(argv[1])=="--capture-only"){gran_perform_cases::capture(argv[2]);result=0;}
    editor_style::shutdown();return result;}
