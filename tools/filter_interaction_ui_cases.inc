// Appended to exact current main/editor harness. No inherited test groups run.
#include "../platform/3ds/filter_interaction.h"
#include "../platform/3ds/filter_type_data.h"
namespace filter_interaction_cases {
namespace fi=filter_interaction;
using C=FilterUnit;using Mode=trackr::dsp::FilterType;
int duals=0,gestures=0,peak=0,fullraw=0;
struct Scene:ModuleInspectorFixture {
    C& u;
    Scene():ModuleInspectorFixture(UnitKind::Filter),u(*static_cast<C*>(module)) {
        input::nav_opened=false;g_sel_chain=g_sel_slot=0;input::frame(0,0);filter_focus_select(0);set();
    }
    ~Scene(){filter_focus_release();g_drag=Drag::None;}
    void set(int raw=5142,int res=10485,int drive=5898,int morph=0,Mode mode=Mode::LPF){
        u.type=mode;u.bypass=false;engine->chains[0].mute=false;
        const int v[]={raw,res,drive,morph};
        for(int p=0;p<4;++p){u.params[p].base=q15(v[p]);u.params[p].src=ModSource::None;u.params[p].mod_depth=0;u.params[p].snap();}
        u.reset();capture();
    }
    std::array<q15,4> bases(){return filter_test_bases(u);}
};
constexpr fi::Box boxes[]={fi::cutoff,fi::resonance,fi::drive,fi::morph};
constexpr Drag drags[]={Drag::FilterCutoff,Drag::FilterResonance,Drag::FilterDrive,Drag::FilterMorph};
const std::pair<int,int> points[]={{160,130},{295,130},{80,211},{235,211}};
void tap(int x,int y){input::down(x,y);input::up();++gestures;}
void move_param(int p,int delta){auto xy=points[p];input::move(xy.first+(p==1?0:delta),xy.second-(p==1?delta:0));}
long double hz(int raw){return 32000.L/acosl(-1.L)*asinl(std::clamp(raw,1,27851)/65536.L);}
long double screen(int raw){return 228.L*logl(hz(raw)/20.L)/logl(800.L);}
// Independent long-double oracle, explicitly including the UI-only tails.
int oracle(int raw,int dx){
    if(!dx)return raw;
    const long double a=screen(129),b=screen(27851);
    const long double origin=raw<129?a-1+raw/129.L:raw>27851?b+(raw-27851)/4916.L:screen(raw);
    const long double x=origin+dx;
    if(x<=a-1)return 0;
    if(x>=b+1)return 32767;
    const long double v=x<a?(x-a+1)*129:x>b?27851+(x-b)*4916:65536*sinl(acosl(-1.L)*20*powl(800.L,x/228)/32000);
    return std::clamp(int(llroundl(v)),0,32767);
}
void mapping(){
    Scene s;auto original=s.bases();double previous=-1.e10;
    for(int raw=0;raw<=32767;++raw){
        const double x=fi::cutoff_coordinate(raw);CHECK(x>previous);previous=x;
        CHECK(fi::cutoff_from_coordinate(x)==raw);CHECK(fi::cutoff_drag(q15(raw),0)==raw);
        s.u.params[0].base=q15(raw);const int dirty=mutations;
        input::down(160,130);input::move(160,151);CHECK(s.u.params[0].base==raw&&mutations==dirty);
        input::move(161,130);const int right=s.u.params[0].base;CHECK(right>=raw&&std::abs(right-oracle(raw,1))<=1);
        input::move(159,130);const int left=s.u.params[0].base;CHECK(left<=raw&&std::abs(left-oracle(raw,-1))<=1);
        input::move(160,130);CHECK(s.u.params[0].base==raw);input::up();++fullraw;
        for(int p=1;p<4;++p)CHECK(s.u.params[p].base==original[p]);
    }
    for(int raw:{0,1,2,127,128,129,643,5142,19024,27850,27851,27852,30000,32767}){
        s.u.params[0].base=q15(raw);input::down(160,130);
        for(int dx:{-150,-70,-15,-1,0,1,15,70,150}){
            input::move(160+dx,130);CHECK(std::abs(int(s.u.params[0].base)-oracle(raw,dx))<=1);
        }
        input::move(160,130);CHECK(s.u.params[0].base==raw);input::up();++gestures;
        s.u.params[0].base=q15(raw);input::down(8,130);input::move(319,130);CHECK(s.u.params[0].base==32767);input::up();
        s.u.params[0].base=q15(raw);input::down(273,130);input::move(0,130);CHECK(s.u.params[0].base==0);input::up();
    }
    CHECK(fi::cutoff_drag(32767,-2)<27851);CHECK(fi::cutoff_drag(0,2)>129);
    // Quantized nominal marker follows physical travel in the visible interior.
    for(int raw:{643,5142,13000,19024})for(int dx:{-10,-1,1,10}) {
        const int next=fi::cutoff_drag(q15(raw),dx);
        CHECK(fabsl(screen(next)-screen(raw)-dx)<.06L);
    }
}
void hit_and_other_controls(){
    Scene s;
    for(int p=0;p<4;++p)for(int raw:{0,17000,32767})for(int corner=0;corner<4;++corner){
        s.u.params[p].base=q15(raw);const auto before=s.bases();const int dirty=mutations;
        const auto b=boxes[p];const int x=corner&1?b.x+b.w-1:b.x,y=corner&2?b.y+b.h-1:b.y;
        input::down(x,y);CHECK(filter_focus_selected()==p&&g_drag==drags[p]);input::move(x,y);input::up();
        CHECK(s.bases()==before&&mutations==dirty);++gestures;
    }
    s.set(13000,13000,13000,13000);
    for(int p=1;p<4;++p){
        const auto before=s.bases();const auto xy=points[p];input::down(xy.first,xy.second);
        move_param(p,6);CHECK(s.u.params[p].base==13000+6*32767/(p==1?70:126));
        move_param(p,0);CHECK(s.bases()==before);input::up();++gestures;
    }
    const auto before=s.bases();const int dirty=mutations;
    for(auto xy:{std::pair<int,int>{275,130},{159,211},{20,180},{150,60},{65,40},{284,10}}){tap(xy.first,xy.second);CHECK(s.bases()==before&&mutations==dirty);}
    for(int mode=0;mode<5;++mode){
        const int count=mutations;const Mode old=s.u.type;tap(36+61*mode,40);
        CHECK(s.u.type==Mode(mode)&&mutations==count+(old!=Mode(mode)));const int after=mutations;
        tap(36+61*mode,40);CHECK(mutations==after);
    }
    tap(258,10);CHECK(s.u.bypass);tap(258,10);CHECK(!s.u.bypass);
    // Label names are not secretly still drawn in mode keys.
    s.capture();module_inspector_surface(false);filter_focus_draw(s.u);
    for(const auto&t:texts)CHECK(!(t.y>=28&&t.y<59));
    // Live external changes are not overwritten by repeated stationary reports.
    for(int p=0;p<4;++p){const auto xy=points[p];s.u.params[p].base=12000;input::down(xy.first,xy.second);
        s.u.params[p].base=6000;const int count=mutations;input::move(xy.first,xy.second);CHECK(s.u.params[p].base==6000&&mutations==count);
        move_param(p,7);const q15 moved=s.u.params[p].base;s.u.params[p].base=7000;move_param(p,7);CHECK(s.u.params[p].base==7000);
        move_param(p,0);CHECK(s.u.params[p].base==12000&&moved!=7000);input::up();++gestures;}
}
void ownership(){
    for(bool capture_cancel:{false,true})for(int reason=0;reason<10;++reason){
        Scene s;s.set();const auto before=s.bases();input::down(160,130);
        Unit* parked=nullptr;std::unique_ptr<Engine> other;
        if(reason==0)g_edit_chain=1;
        if(reason==1)g_edit_slot=1;
        if(reason==2)g_overlay=Overlay::None;
        if(reason==3)g_view=View::Sigils;
        if(reason==4)g_drag=Drag::None;
        if(reason==5)g_seq_lock_armed=true;
        if(reason==6)s.engine->request_pattern(1,false);
        if(reason==7){parked=s.engine->replace_unit(0,0,new C);}
        if(reason==8){other=std::make_unique<Engine>();std::swap(other->chains[0].units[0],s.engine->chains[0].units[0]);g_eng=other.get();}
        if(reason==9)filter_focus_select(1);
        if(capture_cancel)filter_focus_capture();else filter_focus_drag(s.u,168,130);
        if(reason==7)delete s.engine->replace_unit(0,0,parked);
        if(reason==8){g_eng=s.engine.get();std::swap(other->chains[0].units[0],s.engine->chains[0].units[0]);}
        g_edit_chain=g_edit_slot=0;g_overlay=Overlay::Editor;g_view=View::Chains;g_seq_lock_armed=false;
        if(reason==6)s.engine->request_pattern(0,false);
        g_drag=Drag::FilterCutoff;filter_focus_select(0);input::move(176,130);input::up();CHECK(s.bases()==before);++gestures;
    }
    Scene s;input::down(160,130);input::move(165,130);const auto committed=s.bases();filter_focus_release();input::move(170,130);input::up();CHECK(s.bases()==committed);
}
void locks(){
    Scene s;s.set(13000,13000,13000,13000);const auto bases=s.bases();
    g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;g_view=View::Sequencer;
    const fi::Box names[]={fi::cutoff_name,fi::resonance_name,fi::drive_name,fi::morph_name};
    for(int p=0;p<4;++p){
        const int start=p==0?32767:4000;CHECK(s.engine->set_step_lock(0,7,0,p,q15(start)));const auto xy=points[p];
        const auto hash=s.hash();const int dirty=mutations;input::down(xy.first,xy.second);move_param(p,0);CHECK(s.hash()==hash&&mutations==dirty);
        move_param(p,-6);const int expected=p==0?oracle(start,-6):start-6*32767/(p==1?70:126);
        CHECK(std::abs(int(seq_display_value(0,p,0))-expected)<=1&&s.bases()==bases);
        move_param(p,0);CHECK(seq_display_value(0,p,0)==start);input::up();
        tap(names[p].x+2,names[p].y+2);CHECK(seq_display_value(0,p,bases[p])==bases[p]&&s.bases()==bases);++gestures;
    }
    for(int reason=0;reason<4;++reason){
        CHECK(s.engine->set_step_lock(0,7,0,0,5142));input::down(160,130);const auto hash=s.hash();
        if(reason==0)g_seq_lock_step=8;
        if(reason==1)g_seq_lock_chain=1;
        if(reason==2)g_seq_lock_armed=false;
        if(reason==3)s.engine->request_pattern(1,false);
        filter_focus_capture();g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;
        if(reason==3)s.engine->request_pattern(0,false);
        input::move(168,130);input::up();CHECK(s.hash()==hash&&s.bases()==bases);
    }
    const Mode old=s.u.type;const bool bypass=s.u.bypass;
    for(int i=0;i<5;++i)tap(36+61*i,40);
    tap(258,10);CHECK(s.u.type==old&&s.u.bypass==bypass);
    s.engine->clear_track(0);delete s.engine->replace_unit(0,1,new FmUnit);
    for(int p=0;p<SEQ_LOCKS;++p)CHECK(s.engine->set_step_lock(0,7,1,p,1000));
    const auto full=s.hash();for(int p=0;p<4;++p){auto xy=points[p];input::down(xy.first,xy.second);move_param(p,7);input::up();CHECK(s.hash()==full&&s.bases()==bases&&g_seq_lock_flash>0);}
    s.capture();module_inspector_surface(false);filter_focus_draw(s.u);CHECK(module_inspector_text("lock full"));
    g_seq_lock_step=-1;input::down(160,130);input::move(168,130);input::up();CHECK(s.hash()==full&&s.bases()==bases);
    s.capture();module_inspector_surface(false);filter_focus_draw(s.u);CHECK(module_inspector_text("unavailable"));
}
void lens(){
    for(int p=0;p<4;++p){
        Scene s;s.set(13000,13000,13000,13000);filter_focus_select(p);auto&v=s.u.params[p];const auto before=s.bases();
        input::frame(0,0);auto xy=points[p];input::touch={xy.first,xy.second};input::frame(KEY_TOUCH,KEY_TOUCH);
        input::frame(KEY_L,KEY_L|KEY_TOUCH);CHECK(param_lens_is_open()&&g_drag==Drag::None);
        input::touch={200,102};input::frame(0,KEY_L|KEY_TOUCH);CHECK(s.bases()==before);
        input::frame(0,KEY_L,KEY_TOUCH);
        input::touch={137,102};input::frame(KEY_TOUCH,KEY_L|KEY_TOUCH);CHECK(v.base==13000);
        input::touch={144,102};input::frame(0,KEY_L|KEY_TOUCH);CHECK(v.base==13000+7*32767/271);
        const q15 coarse=v.base;input::frame(KEY_R,KEY_L|KEY_R|KEY_TOUCH);CHECK(v.base==coarse);
        input::touch={152,102};input::frame(0,KEY_L|KEY_R|KEY_TOUCH);CHECK(v.base==coarse+8*32767/(271*(p==0?16:8)));
        input::frame(0,KEY_L,KEY_TOUCH|KEY_R);const q15 saved=v.base;CHECK(!s.engine->param_temp_active());
        // Current lens depth and cable targets, not the retired historical coordinates.
        input::touch={140,147};input::frame(KEY_TOUCH,KEY_L|KEY_TOUCH);input::touch={146,147};input::frame(0,KEY_L|KEY_TOUCH);
        CHECK(v.mod_depth==6*65534/271&&v.base==saved);input::frame(0,KEY_TOUCH,KEY_L);CHECK(!param_lens_is_open());
        input::touch={xy.first+12,xy.second};input::frame(0,KEY_TOUCH);CHECK(v.base==saved);
        input::frame(0,0,KEY_TOUCH);input::frame(0,0);CHECK(filter_focus_selected()==p&&!s.engine->param_temp_active());++gestures;
    }
}
void bounds(){
    if(outside_rects||outside_lines||text_overlaps){std::fprintf(stderr,"filter bounds w%d rect%d lines%d overlap%d\n",width,outside_rects,outside_lines,text_overlaps);
        for(const auto&t:texts)std::fprintf(stderr," %d,%d %dx%d %s\n",t.x,t.y,t.w,t.h,t.text.c_str());}
    CHECK(objects>0&&objects<=8192&&!outside_rects&&!outside_lines&&!text_overlaps);
    for(const auto&t:texts)CHECK(t.x>=0&&t.y>=0&&t.x+t.w<=width&&t.y+t.h<=240);
}
void surface(Scene&s,bool top,bool dispatch=false){module_inspector_surface(top);{ModuleInspectorNoEngine hidden;
    if(top){if(dispatch)CHECK(module_inspector_draw_top());else CHECK(filter_focus_draw_top());}
    else {
        filter_focus_draw(s.u);bounds();
        // The real editor paints this underlay before the translucent MOD layer.
        // Validate text per layer; crossing underlay/overlay labels is intentional.
        if(param_lens_is_open()){texts.clear();text_overlaps=0;param_lens_draw_bottom();}
    }}bounds();}
void pair(Scene&s){s.capture();surface(s,true);const int top=objects;const auto upper=pixels;surface(s,true,true);CHECK(pixels==upper);
    surface(s,false);const int bottom=objects;const auto lower=pixels;CHECK(top+bottom<=8192);peak=std::max(peak,top+bottom);++duals;
    // Also compare the actual editor facade composition (which legitimately uses
    // the current unit to dispatch); frozen render functions above do not.
    module_inspector_surface(false);editor_draw();CHECK(pixels==lower&&objects==bottom&&!outside_rects&&!outside_lines);
    if(param_lens_is_open()){module_inspector_surface(true);{ModuleInspectorNoEngine hidden;CHECK(param_lens_draw_top());}CHECK(pixels==upper&&objects==top);}
    const auto snapshot=g_ui_snapshot;g_ui_snapshot={};surface(s,true);CHECK(pixels==upper);surface(s,false);CHECK(pixels==lower);g_ui_snapshot=snapshot;
    if(!param_lens_is_open()){C decoy;decoy.type=Mode::Off;decoy.params[0].base=32767;module_inspector_surface(false);{ModuleInspectorNoEngine hidden;filter_focus_draw(decoy);}CHECK(pixels==lower);}
}
void frames(){
    for(auto theme:{palette::Theme::Graphite,palette::Theme::Pink}){palette::set(theme);Scene s;
        for(int type=0;type<5;++type)for(int raw:{0,1,128,129,5142,27851,27852,32767})for(int res:{0,32767})for(int morph:{0,16384,32767}){
            s.set(raw,res,32767,morph,Mode(type));filter_focus_select((raw+type+res)%4);pair(s);
        }
        for(int p=0;p<4;++p){s.set();filter_focus_select(p);s.u.bypass=true;pair(s);s.u.bypass=false;s.engine->chains[0].mute=true;pair(s);
            s.engine->chains[0].mute=false;s.u.params[p].src=ModSource::SigilA;s.u.params[p].mod_depth=6000;s.u.params[p].effective=12000;s.u.reset();pair(s);
            input::frame(0,0);input::frame(KEY_L,KEY_L);input::frame(0,KEY_L);CHECK(param_lens_is_open());pair(s);
            input::frame(0,0,KEY_L);input::frame(0,0);}
        s.set();g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;g_view=View::Sequencer;
        CHECK(s.engine->set_step_lock(0,7,0,0,0));pair(s);g_seq_lock_flash=90;pair(s);g_seq_lock_step=-1;pair(s);
        g_seq_lock_armed=false;g_view=View::Chains;g_overlay=Overlay::None;s.capture();module_inspector_surface(true);CHECK(!filter_focus_draw_top());
    }
}
void truthful_and_type(){
    Scene s;s.set();s.capture();surface(s,true);const auto original=pixels;
    // Changing just the DRIVE amount must leave the actual linear plot unchanged.
    s.u.params[2].base=30000;s.u.params[2].snap();s.capture();surface(s,true);
    for(int y=100;y<208;++y)for(int x=0;x<400;++x)CHECK(pixels[y*400+x]==original[y*400+x]);
    for(int mode=0;mode<4;++mode){s.set(5142,10485,5898,16384,Mode(mode));surface(s,true);
        const u32 accent=menu_style::spot();const int yy=110+int((12+6.020599913)*75/48+.5);
        for(int x=34;x<379;++x)if(std::abs(x-(32+int(screen(5142)*348/228+.5)))>2)CHECK(pixels[yy*400+x]==accent);}
    // Native masks are explicitly bounded and fit the intended hero/title slots.
    for(const auto&label:filter_type::labels)CHECK(label.width>0&&label.height>0&&label.height<40&&size_t(label.offset+(label.width+7)/8*label.height)<=sizeof(filter_type::bitmap));
    for(const auto&g:filter_type::glyphs)CHECK(g.width&&g.height&&g.advance&&size_t(g.offset+(g.width+7)/8*g.height)<=sizeof(filter_type::bitmap));
    const fi::Box all[]={fi::bypass,fi::mode(0),fi::mode(1),fi::mode(2),fi::mode(3),fi::mode(4),fi::cutoff,fi::resonance,fi::drive,fi::morph};
    for(size_t i=0;i<std::size(all);++i){auto a=all[i];CHECK(a.w>=32&&a.h>=24&&a.x>=0&&a.y>=0&&a.x+a.w<=320&&a.y+a.h<=240);
        for(size_t j=0;j<i;++j){auto b=all[j];CHECK(a.x+a.w<=b.x||b.x+b.w<=a.x||a.y+a.h<=b.y||b.y+b.h<=a.y);}}
    s.set();s.u.params[0].effective=10000;s.u.reset();s.capture();surface(s,true);CHECK(module_inspector_text("live"));
    // Scalar smoothed-live, not fabricated frequency movement: no engine tick in draw.
    const auto a=pixels;const q15 live=s.u.visual_cutoff();s.u.params[0].effective=20000;surface(s,true);CHECK(pixels==a&&s.u.visual_cutoff()==live);
    s.u.control_tick();s.capture();surface(s,true);CHECK(pixels!=a);
}
int proof(){int total=0,failed=0;auto run=[&](const char*name,auto test){++total;try{test();std::printf("ok: %s\n",name);}catch(const std::exception&e){++failed;std::printf("FAIL: %s: %s\n",name,e.what());}};
    run("allraw reversible tails and physical log travel",mapping);
    run("actual main hits, lift, no-jump and unchanged other laws",hit_and_other_controls);
    run("engine/bank/owner/token/context sticky cancellation",ownership);
    run("actual main lock adapters, capacity and base isolation",locks);
    run("current permanent MOD, fine and held-touch quarantine",lens);
    run("frozen two-theme extrema, states and top dispatch",frames);
    run("truthful model motion, native type and target boundaries",truthful_and_type);
    std::printf("filter interaction UI: %d/%d groups; %d frozen duals; %d gestures; %d fullraw main traces; peak %d/8192; 0 inherited groups\n",total-failed,total,duals,gestures,fullraw,peak);
    return failed?1:0;
}
void capture(const std::filesystem::path&dir){CHECK(!std::filesystem::exists(dir));std::filesystem::create_directories(dir);
    std::ofstream manifest(dir/"manifest.tsv");manifest<<"frame\ttop\tbottom\tshared\n";int pairs=0;
    for(auto theme:{palette::Theme::Graphite,palette::Theme::Pink}){palette::set(theme);Scene s;const std::string prefix=theme==palette::Theme::Graphite?"graphite_":"pink_";
        auto shot=[&](const char*name){s.capture();surface(s,true);const int top=objects;dump_surface(dir,prefix+name+"_top");surface(s,false);CHECK(top+objects<=8192);
            dump_surface(dir,prefix+name+"_bottom");manifest<<prefix+name<<'\t'<<top<<'\t'<<objects<<'\t'<<top+objects<<'\n';++pairs;};
        for(int type=0;type<5;++type){s.set(5142,10485,5898,0,Mode(type));shot((const char*[]){"low","high","band","notch","off"}[type]);}
        s.set(5142,26869);filter_focus_select(1);shot("resonance");s.set(5142,10485,5898,16384);filter_focus_select(3);shot("morph_half");
        s.set(5142,10485,32767);filter_focus_select(2);shot("drive");
        filter_focus_select(0);s.set(0);shot("below_axis");s.set(32767);shot("ceiling");
        s.set();s.u.params[0].src=ModSource::SigilA;s.u.params[0].mod_depth=6000;s.u.params[0].effective=9600;s.u.reset();shot("modulation");
        input::frame(0,0);input::frame(KEY_L,KEY_L);input::frame(0,KEY_L);shot("mod_lens");input::frame(0,0,KEY_L);input::frame(0,0);
        s.set();s.u.bypass=true;shot("bypass");s.u.bypass=false;s.engine->chains[0].mute=true;shot("muted");
        s.set();g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;g_view=View::Sequencer;CHECK(s.engine->set_step_lock(0,7,0,0,643));shot("lock");
        g_seq_lock_flash=90;shot("lock_full");g_seq_lock_armed=false;g_view=View::Chains;
    }
    manifest.close();std::ofstream receipt(dir/"capture.json");receipt<<"{\"pairs\":"<<pairs<<",\"regression_groups_run\":0,\"origin\":\"actual C++ filter capture/raster, configured engine parameters, host static paper; not hardware/audio spectrum\"}\n";
    std::printf("filter capture: %d pairs; 0 regression groups\n",pairs);
}
}
int main(int argc,char**argv){theme_test::select();if(argc==3&&std::string(argv[1])=="--capture-only"){filter_interaction_cases::capture(argv[2]);return 0;}
    if(argc==2&&std::string(argv[1])=="--proof-only")return filter_interaction_cases::proof();
    return 2;}
