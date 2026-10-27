// Scoped current REV field/controllers. Reuse existing host boundary, not old tests.
#include "../platform/3ds/reverb_field.h"
#include "../platform/3ds/rev_readout.h"
namespace reverb_field_cases {
namespace h=module_headings_cases;
namespace f=fader_lancet_cases;
using R=ReverbUnit;
const int main_params[]={0,1,3,2},extra_params[]={5,6,7};
int gestures=0,frozen=0,motion_frames=0,route_frames=0,field_checks=0;
std::ofstream motion,numbers;
struct Scene:h::Scene {
    Scene():h::Scene(UnitKind::Reverb){reverb_focus_input(0,0,false);select(R::P_DECAY);capture();}
    void select(int p){reverb_focus_select(p);}
    ~Scene(){editor_cancel_input();param_lens_cancel();reverb_focus_input(0,0,false);}
};
R& unit(Scene&s){return *static_cast<R*>(s.unit);}
std::array<q15,8> bases(const R&u){std::array<q15,8>a{};for(int p=0;p<8;++p)a[p]=u.params[p].base;return a;}
void down(int x,int y){input::down(x,y);}
void drag(int x,int y){input::move(x,y);}
void lift(){input::up();small_press::cancel();}
void cancel(){editor_cancel_input();param_lens_cancel();small_press::cancel();reverb_focus_input(0,0,false);}
void tap(int x,int y){down(x,y);lift();}
void take(Scene&s,const std::string&id,const std::string&variant){
    h::take(s,id,variant);s.capture();editor_style::capture();editor_gpu::begin();
    module_inspector_surface(true);{f::AudioHidden hidden;top_draw();}
    CHECK(!outside_rects&&!outside_lines&&!text_overlaps);const auto top=pixels;const int upper=objects;
    module_inspector_surface(false);{f::AudioHidden hidden;editor_draw();}
    CHECK(!outside_rects&&!outside_lines&&!text_overlaps);const auto bottom=pixels;const int lower=objects;
    const auto redraw=[&]{
        module_inspector_surface(true);{ModuleInspectorNoEngine hidden;top_draw();}
        CHECK(pixels==top&&objects==upper&&!text_overlaps);
        module_inspector_surface(false);{ModuleInspectorNoEngine hidden;
            reverb_focus_draw(unit(s));recorder_focus_draw_return();if(param_lens_is_open())param_lens_draw_bottom();}
        CHECK(pixels==bottom&&objects==lower&&!text_overlaps);
    };
    redraw();{f::PoisonAfterCapture poison(s);redraw();}++frozen;
}
void geometry(){
    namespace field=reverb_field;
    for(int size:{0,18000,32767})for(int damp:{0,16000,32767})for(int wet:{0,1000,32767})
        for(int bits=0;bits<16;++bits) {
            q15 returns[4];for(int i=0;i<4;++i)returns[i]=(bits&(1<<i))?24000:0;
            field::Plot a{},b{};field::build(size,damp,wet,returns,true,false,a);
            field::build(size,damp,wet,returns,true,false,b);CHECK(!std::memcmp(&a,&b,sizeof a));
            for(const auto&row:a.points)for(auto p:row)CHECK(p.x>=24&&p.x<=376&&p.y>=48&&p.y<=169);
            field::build(size,damp,wet,returns,true,true,b);CHECK(b.wet_strength==0);
            for(int row=0;row<field::ROWS;++row){CHECK(b.row_strength[row]==0);
                for(auto p:b.points[row])CHECK(p.y==101+(row*61+9)/18);}
            ++field_checks;
        }
    q15 left[4]={32767,0,0,0},right[4]={0,32767,0,0};field::Plot a{},b{};
    field::build(18000,9000,0,left,true,false,a);field::build(18000,9000,0,right,true,false,b);
    CHECK(std::memcmp(a.points,b.points,sizeof a.points));CHECK(a.wet_strength==0&&b.wet_strength==0);++field_checks;
}
void controls(){
    for(int p:{0,1,3,2,5,6,7})for(int v:{0,12000,32767})for(int corner=0;corner<4;++corner){
        Scene s;auto&u=unit(s);s.set(p,v);s.select(p);s.capture();const auto before=bases(u);const int dirty=mutations;
        const int row=p>=5?p-5:p==3?2:p==2?3:p;
        const int x=corner&1?311:8,y=39+37*row+((corner&2)?36:0);
        down(x,y);CHECK(reverb_focus_selected()==p&&bases(u)==before&&mutations==dirty);
        drag(x,y);CHECK(bases(u)==before);lift();++gestures;
    }
    for(int p:{0,1,3,2,5,6,7}) {
        Scene s;auto&u=unit(s);s.select(p);s.set(p,12000);s.capture();const int row=p>=5?p-5:p==3?2:p==2?3:p,y=66+37*row;
        down(170,y);drag(194,y);CHECK(u.params[p].base==15276);const int dirty=mutations;
        drag(194,y);CHECK(mutations==dirty);lift();++gestures;
        down(170,y);u.params[p].base=2222;drag(170,y);CHECK(u.params[p].base==2222);cancel();++gestures;
        s.set(p,12000);down(170,y);const auto before=bases(u);s.select(p==7?0:p+1);drag(194,y);CHECK(bases(u)==before);cancel();++gestures;
    }
    {Scene s;auto&u=unit(s);s.select(5);s.set(5,12000);s.capture();down(170,66);drag(194,66);CHECK(u.params[5].base==15276);
        input::touch={194,66};input::frame(KEY_R,KEY_R|KEY_TOUCH);CHECK(u.params[5].base==15276);
        drag(218,66);CHECK(u.params[5].base==15276+24*32767/(240*8));const q15 v=u.params[5].base;
        input::touch={218,66};input::frame(0,KEY_TOUCH,KEY_R);CHECK(u.params[5].base==v);
        drag(242,66);lift();CHECK(u.params[5].base==v+3276);gestures+=3;}
    {Scene s;auto&u=unit(s);const auto before=bases(u);const int dirty=mutations;
        tap(275,202);CHECK(reverb_focus_selected()==5&&bases(u)==before&&mutations==dirty);
        tap(275,202);CHECK(reverb_focus_selected()==1&&bases(u)==before&&mutations==dirty);gestures+=2;}
}
void actions(){
    for(int action=0;action<3;++action)for(int reason=0;reason<9;++reason){
        Scene s;auto&u=unit(s);const auto before=bases(u);const int x=action==0?30:action==1?258:275,y=action==1?10:202;
        const int dirty=mutations;down(x,y);CHECK(bases(u)==before&&!u.bypass&&mutations==dirty);
        if(reason==0){lift();if(action==0)CHECK(u.params[4].base==32767);if(action==1)CHECK(u.bypass);if(action==2)CHECK(reverb_focus_selected()==5);}
        else {
            if(reason==1){drag(x+50,y);drag(x,y);}
            if(reason==2){input::frame(KEY_R,KEY_R|KEY_TOUCH);}
            if(reason==3){g_overlay=Overlay::None;s.capture();g_overlay=Overlay::Editor;}
            if(reason==4){input::nav_opened=true;input::frame(0,KEY_TOUCH);input::nav_opened=false;}
            if(reason==5){g_edit_chain=1;s.capture();g_edit_chain=0;}
            if(reason==6){g_edit_slot=0;s.capture();g_edit_slot=s.slot;}
            if(reason==7){s.select(action==0?2:3);}
            if(reason==8){u.bypass=true;s.capture();u.bypass=false;}
            lift();CHECK(bases(u)==before&&!u.bypass&&mutations==dirty);
        }cancel();++gestures;
    }
    for(u32 key:{u32(KEY_A),u32(KEY_B),u32(KEY_SELECT),u32(KEY_L)}) {
        Scene s;const auto before=bases(unit(s));down(30,202);
        input::frame(key,key|KEY_TOUCH);input::frame(0,0,key);CHECK(bases(unit(s))==before);cancel();++gestures;
    }
    {Scene s;down(30,202);auto* replacement=new R;CHECK(replacement->ready());delete s.engine->replace_unit(0,s.slot,replacement);s.unit=replacement;
        lift();CHECK(replacement->params[4].base==0);++gestures;}
    {Scene s;auto&u=unit(s);tap(30,202);CHECK(u.params[4].base==32767);s.render(2048);
        for(int p:{0,1,3}){s.select(p);const auto before=bases(u);const int row=p==3?2:p;down(170,66+37*row);drag(194,66+37*row);lift();CHECK(bases(u)==before);++gestures;}
        s.select(2);const int old=u.params[2].base;down(170,177);drag(194,177);lift();CHECK(u.params[2].base==old+3276);++gestures;
        tap(30,202);CHECK(u.params[4].base==0);}
}
q15 locked(Scene&s,int bank,int step,int p){const auto&st=s.engine->sequence_pattern(bank).steps[0][step];
    for(int i=0;i<st.lock_count;++i)if(st.locks[i].slot==s.slot&&st.locks[i].param==p)return st.locks[i].value;
    return -1;}
void locks(const std::string&prefix){
    Scene s;auto&u=unit(s);g_overlay=Overlay::None;g_view=View::Sequencer;
    sequencer_view_enter();s.frame();extent_bank(s,2);s.engine->request_pattern(7,false);
    CHECK(s.engine->active_pattern==7&&sequencer_edit_bank()==2);
    g_overlay=Overlay::Editor;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=5;
    const auto before=bases(u);auto&step=s.engine->sequence_pattern(2).steps[0][5];step.lock_count=0;
    for(int p:{0,1,3,2,5,6,7}){
        step.lock_count=1;step.locks[0]={uint8_t(s.slot),uint8_t(p),10000};s.select(p);s.capture();
        const int row=p>=5?p-5:p==3?2:p==2?3:p;
        down(170,66+37*row);drag(194,66+37*row);lift();CHECK(locked(s,2,5,p)==13276&&bases(u)==before);
        CHECK(s.engine->pattern.steps[0][5].lock_count==0);
        tap(24,47+37*row);CHECK(locked(s,2,5,p)==-1&&bases(u)==before);gestures+=2;
    }
    // Launching another audio bank does not redirect a gesture from edit bank2.
    step.lock_count=1;step.locks[0]={uint8_t(s.slot),5,10000};s.select(5);s.capture();
    down(170,66);s.engine->request_pattern(3,false);drag(194,66);lift();
    CHECK(locked(s,2,5,5)==13276&&s.engine->active_pattern==3&&bases(u)==before);++gestures;
    step.lock_count=0;tap(30,202);CHECK(locked(s,2,5,4)==32767&&bases(u)==before);++gestures;
    s.select(5);take(s,prefix+"rev_lock","lock");
    step.lock_count=4;for(int i=0;i<4;++i)step.locks[i]={uint8_t(s.slot),uint8_t(i),1234};
    down(170,66);drag(194,66);lift();CHECK(g_seq_lock_flash>0&&bases(u)==before&&locked(s,2,5,5)==-1);++gestures;
    take(s,prefix+"rev_lock_full","lock-full");
    const bool bypass=u.bypass;tap(258,10);CHECK(u.bypass==bypass);++gestures;
    step.lock_count=0;down(170,66);g_seq_lock_step=6;drag(194,66);lift();CHECK(locked(s,2,6,5)==-1&&bases(u)==before);++gestures;
    g_seq_lock_armed=false;g_view=View::Chains;cancel();
}
void states(const std::string&prefix){
    // Same address/mode setup, FREEZE selected on extras used to leave a hidden
    // more=true with four primary rows and index beyond the three-extra table.
    for(bool capture_first:{false,true})for(int row=0;row<4;++row) {
        Scene s;auto&u=unit(s);s.select(5);s.select(4);CHECK(u.set_modern(false));
        if(capture_first)s.capture();
        const int p=main_params[row];s.set(p,12000);down(170,66+37*row);drag(194,66+37*row);lift();
        CHECK(reverb_focus_selected()==p&&u.params[p].base==15276);++gestures;
    }
    for(int phase=0;phase<4;++phase) {
        Scene s;auto&u=unit(s);s.select(7);s.set(7,32767);q15 burst[4096];std::fill(std::begin(burst),std::end(burst),q15(20000));u.process(burst,4096);
        if(phase==0)u.params[4].base=32767; // request only; effective remains off
        else {s.set(4,32767);std::fill(std::begin(burst),std::end(burst),q15(20000));u.process(burst,phase==1?16:128);
            if(phase==3){s.set(4,0);u.process(burst,16);}}
        take(s,prefix+"rev_duck_freeze_"+std::to_string(phase),"duck-freeze");
        CHECK(module_inspector_text(phase==2?"freeze: open":phase==3?"returning":"freeze pending"));
        if(phase==2)CHECK(u.visual_duck_gain()==32767);else CHECK(u.visual_duck_gain()<32767);
    }
    for(const std::string v:{"default","more","waiting","legacy","legacy-more","bypass","mute","dry","held","empty-held","releasing","settling","long6","holes","lens","more-lens","idle-first","idle-later"}){
        Scene s;auto&u=unit(s);
        if(v=="more"||v=="more-lens"){s.select(5);s.set(5,18000);s.set(6,12000);s.set(7,21000);s.render(4096);}
        if(v=="legacy"||v=="legacy-more"){CHECK(u.set_modern(false));s.render(4096);if(v=="legacy-more"){tap(275,202);CHECK(reverb_focus_selected()<5&&u.num_params==5);++gestures;}}
        if(v=="waiting")u.reset();
        if(v=="bypass")u.bypass=true;
        if(v=="mute")s.engine->chains[0].mute=true;
        if(v=="dry"){s.set(2,0);s.render(4096);}
        if(v=="held"||v=="empty-held"||v=="releasing"){
            if(v=="empty-held")u.reset();else s.render(8192);
            s.set(4,32767);
            if(v=="empty-held"){q15 silence[1024]{};u.process(silence,1024);CHECK(u.visual_energy()==0);for(int lane=0;lane<4;++lane)CHECK(u.visual_return(lane)==0);}
            else s.render(1024);
            if(v=="releasing"){s.set(4,0);q15 buf[16]{};u.process(buf,16);}
        }
        if(v=="settling"){s.set(0,32767);q15 buf[32]{};u.process(buf,32);s.set(4,32767);u.process(buf,32);CHECK(u.visual_transition());}
        if(v=="long6"||v=="holes")s.path(v=="holes");
        if(v=="lens"||v=="more-lens"){u32 k=KEY_L;CHECK(param_lens_input(k,KEY_L,0,false));param_lens_touch_release();}
        if(v=="idle-later"){host_time_ms+=5000;g_ui_frame+=137;}
        take(s,prefix+"rev_"+v,v);
    }
    for(int p=0;p<8;++p)for(int raw:{0,32767}){
        Scene s;s.set(p,raw);s.select(p);s.render(1024);take(s,prefix+"rev_p"+std::to_string(p)+"_"+std::to_string(raw),"edge");
    }
    for(int p:{5,6,7}){
        Scene s;s.select(p);u32 key=KEY_L;CHECK(param_lens_input(key,KEY_L,0,false));param_lens_touch_release();
        const auto old=unit(s).params[p].base;down(140,96);drag(146,96);lift();CHECK(unit(s).params[p].base==old+6*32767/271);++gestures;
        take(s,prefix+"rev_lens_edit_"+std::to_string(p),"lens-edit");param_lens_cancel();
    }
}
void numeric(Scene&s,const std::string&id,bool extra){
    for(int row=0;row<(extra?3:4);++row){const int p=extra?extra_params[row]:main_params[row];const auto&u=unit(s);
        numbers<<id<<'\t'<<row<<'\t'<<p<<'\t'<<u.params[p].base<<'\t'<<u.visual_effective(p)<<'\n';}
}
void routes(const std::string&prefix){
    Scene s;auto&u=unit(s);
    const int base[]={18000,18000,16000,16000,0,14000,14000,16000};
    for(int p:{0,1,2,3,5,6,7}){auto&q=u.params[p];q.base=base[p];q.src=ModSource::SigilA;q.mod_depth=10000;q.snap();}
    auto&sig=s.engine->sigils[0];sig.depth=32767;
    for(int n=0;n<SIGIL_POINTS;++n)sig.set_point(n,q15(std::lround(std::sin(double(n)*6.283185307179586/SIGIL_POINTS)*30000)));
    auto cfg=s.engine->sigil_config[0];cfg.enabled=true;cfg.legacy=true;s.engine->set_sigil_config(0,cfg);
    s.engine->set_sigil_sync(0,false);s.engine->set_sigil_rate(0,16000);s.engine->reset_sigil(0);
    for(int n=0;n<24;++n){s.render(1024);
        for(bool extra:{false,true}){s.select(extra?5:1);const std::string id=prefix+"rev_route_"+(extra?"extra_":"main_")+std::to_string(n);
            take(s,id,"engine-sigil");numeric(s,id,extra);++route_frames;}
        for(int p:{0,1,2,3,5,6,7})CHECK(u.params[p].base==base[p]);
    }
}
void live_motion(const std::string&prefix){
    Scene s;auto&u=unit(s);u.reset();uint32_t rng=0x915aca3;
    for(int n=0;n<100;++n){
        if(n==39)s.set(4,32767);
        if(n==66)s.set(4,0);
        for(int block=0;block<10;++block){q15 buf[256];for(int j=0;j<256;++j){rng=rng*1664525u+1013904223u;
            buf[j]=(n>=8&&n<=10)||(n>=36&&n<=38)?q15((int(rng>>16)-32768)*3/4):0;}
            u.process(buf,256);}
        const std::string id=prefix+"rev_motion_"+(n<10?"0":"")+std::to_string(n);
        take(s,id,"real-core-tail");motion<<prefix<<'\t'<<n<<'\t'<<id<<'\t'<<u.visual_energy()<<'\t'<<u.visual_freeze();
        for(int lane=0;lane<4;++lane)motion<<'\t'<<u.visual_return(lane);
        motion<<'\n';++motion_frames;
    }
}
int run(const std::filesystem::path&dir){
    CHECK(!std::filesystem::exists(dir));std::filesystem::create_directories(dir);h::output=dir;h::capture_files=true;
    h::manifest.open(dir/"surfaces.tsv");h::manifest<<"id\tkind\tmodule\tvariant\tsurface\tobjects\toutside_rects\toutside_lines\ttext_overlaps\traster_fnv64\n";
    motion.open(dir/"motion.tsv");motion<<"theme\tframe\tid\twet\tfreeze\treturn0\treturn1\treturn2\treturn3\n";
    numbers.open(dir/"numbers.tsv");numbers<<"id\trow\tparam\tbase\tlive\n";
    f::sample_fixture();CHECK(editor_style::init());geometry();
    for(auto theme:{palette::Theme::Graphite,palette::Theme::Pink}){palette::set(theme);const std::string prefix=theme==palette::Theme::Graphite?"graphite_":"pink_";
        controls();actions();locks(prefix);states(prefix);routes(prefix);live_motion(prefix);
    }
    editor_style::shutdown();CHECK(editor_gpu::textures.empty());h::manifest.close();motion.close();numbers.close();
    std::ofstream out(dir/"behavior.json");out<<"{\"status\":\"passed\",\"gestures\":"<<gestures<<",\"frozen_pairs\":"<<frozen
        <<",\"motion_frames\":"<<motion_frames<<",\"route_frames\":"<<route_frames<<",\"field_cases\":"<<field_checks
        <<",\"duals\":"<<h::duals<<",\"peak_objects\":"<<h::peak<<",\"old_suites_run\":0}\n";
    std::printf("rev field: %d gestures, %d frozen pairs, %d real-core frames, %d engine-sigil pairs, %d geometry cases; peak %d/8192 software objects\n",
        gestures,frozen,motion_frames,route_frames,field_checks,h::peak);return 0;
}
}
int main(int argc,char**argv){try{CHECK(argc==3);return reverb_field_cases::run(argv[2]);}
catch(const std::exception&e){std::fprintf(stderr,"rev field failed: %s\n",e.what());return 1;}}
