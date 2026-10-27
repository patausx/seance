// Appended only to our temporary actual-C++ driver (old main renamed).
// Existing raster/engine fixtures are reused; no inherited test group is run.
#include "../platform/3ds/ghost_memory_visual.h"
namespace ghost_refresh_cases {
namespace {
namespace fixture=fader_lancet_cases;
namespace hid=osc_input_main;
using fixture::Scene;
using G=GhostUnit;
static std::string label;
static uint64_t geometry_cases=0,duals=0,detached=0,gestures=0,routes=0,ownership=0;
static int peak=0;
struct Pair {std::vector<u32> top,bottom;std::vector<TextRecord> top_text,bottom_text;int upper=0,lower=0;};
using Controls=std::array<Param,MAX_PARAMS>;
G& ghost(Scene& s){return *static_cast<G*>(s.unit);}
void close(){param_lens_cancel();hid::nav_opened=false;hid::frame(0,0);g_drag=Drag::None;}
Controls controls(const Scene& s){Controls r{};std::copy(std::begin(s.unit->params),std::end(s.unit->params),r.begin());return r;}
void same(const Scene& s,const Controls& old,bool include_base=true){for(int p=0;p<8;++p){const auto&a=old[p];const auto&b=s.unit->params[p];CHECK((!include_base||a.base==b.base)&&a.src==b.src&&a.mod_depth==b.mod_depth&&a.mod_enabled==b.mod_enabled);}}
void down(int x,int y,u32 mods=0){hid::touch={x,y};hid::frame(KEY_TOUCH,mods|KEY_TOUCH);}
void move(int x,int y,u32 mods=0){hid::touch={x,y};hid::frame(0,mods|KEY_TOUCH);}
void up(u32 mods=0){hid::frame(0,mods,KEY_TOUCH);}
void tap(int x,int y,u32 mods=0){down(x,y,mods);up(mods);}
void choose(Scene& s,int p){ghost_focus_select(p);s.capture();CHECK(ghost_focus_selected()==p);}
void open(Scene& s,bool held=true){
    hid::nav_opened=false;hid::frame(0,0);
    if(held){const auto r=hid::frame(KEY_L,KEY_L);CHECK(r.owned&&r.skip&&!r.remaining);hid::frame(0,KEY_L);}
    else tap(250,232);
    CHECK(param_lens_is_open()&&g_drag==Drag::None&&!s.engine->param_temp_active());s.capture();
}
void bounds(bool overlap=true){
    if(outside_rects||outside_lines||(overlap&&text_overlaps))std::fprintf(stderr,"ghost %s: w%d rect%d line%d text%d\n",label.c_str(),width,outside_rects,outside_lines,text_overlaps);
    CHECK(objects>0&&objects<=8192&&!outside_rects&&!outside_lines);if(overlap)CHECK(!text_overlaps);
    for(const auto&t:texts)CHECK(t.x>=0&&t.y>=0&&t.x+t.w<=width&&t.y+t.h<=240);
}
void overlay_text(){
    const auto first=std::find_if(texts.begin(),texts.end(),[](const TextRecord&t){return t.text=="link";});CHECK(first!=texts.end());
    bool base=false,depth=false;
    for(auto a=first;a!=texts.end();++a){if(a->text=="base")base=true;if(a->text=="depth")depth=true;
        for(auto b=first;b!=a;++b)CHECK(a->x>=b->x+b->w||a->x+a->w<=b->x||a->y>=b->y+b->h||a->y+a->h<=b->y);}
    CHECK(base&&depth);
}
Pair draw(Scene& s,bool frozen=true){
    Pair r;const int dirty=mutations;const auto clock=s.engine->transport_frame;
    fixture::surface(true);{fixture::AudioHidden hidden;top_draw();}bounds();r.top=pixels;r.top_text=texts;r.upper=objects;
    fixture::surface(false);{fixture::AudioHidden hidden;editor_draw();}bounds(!param_lens_is_open());if(param_lens_is_open())overlay_text();
    r.bottom=pixels;r.bottom_text=texts;r.lower=objects;CHECK(r.upper+r.lower<=8192);peak=std::max(peak,r.upper+r.lower);++duals;
    if(frozen){const auto again=[&](){
        fixture::surface(true);{ModuleInspectorNoEngine hidden;top_draw();}bounds();CHECK(pixels==r.top&&objects==r.upper);
        fixture::surface(false);{ModuleInspectorNoEngine hidden;ghost_focus_draw(ghost(s));param_lens_draw_bottom();}
        bounds(!param_lens_is_open());CHECK(pixels==r.bottom&&objects==r.lower);
    };again();{fixture::PoisonAfterCapture poison(s);again();}++detached;}
    CHECK(mutations==dirty&&s.engine->transport_frame==clock);return r;
}
bool has(const std::vector<TextRecord>& ts,const std::string& text){return std::any_of(ts.begin(),ts.end(),[&](const TextRecord&t){return t.text==text;});}
void set_note_mode(Scene& s,bool note){
    s.capture();fixture::surface(false);{fixture::AudioHidden hidden;editor_draw();}
    CHECK(has(texts,"note")||has(texts,"free"));if(has(texts,"note")!=note)tap(150,10);
}
std::pair<int,int> point(const Scene& s,int p){const int v=s.unit->params[p].base;
    switch(p){case G::P_TONE:return{82+v*150/32767,54};case G::P_DECAY:return{61,174-v*82/32767};
    case G::P_SMEAR:return{111+v*114/32767,183};case G::P_DRIFT:return{254,111-v*52/32767};
    case G::P_FREEZE:return{160,117};case G::P_MIX:return{294,181-v*65/32767};
    case G::P_FINE:return{150+v*60/32767,76};case G::P_RATE:return{64+v*186/32767,220};default:throw std::runtime_error("bad Ghost param");}
}
void active(Scene& s){
    const q15 values[8]={3500,18000,12000,13000,0,18000,0,4000};for(int p=0;p<8;++p)s.set(p,values[p]);
    auto* source=s.engine->chains[0].units[0];CHECK(source!=s.unit&&source->kind==UnitKind::Osc);
    source->params[OscUnit::P_GAIN].base=14000;source->params[OscUnit::P_GAIN].snap();s.render(8192);s.capture();
    CHECK(ghost(s).visual_energy()>0);
}
void helper_group(){
    namespace shape=ghost_memory_visual;
    static_assert(shape::WELLS==4&&shape::ECHOES==3&&shape::SEGMENTS==32&&shape::EXTENT==1024,"approved closed geometry");
    static_assert(sizeof(shape::Geometry)<=1600,"bounded private geometry");
    const auto equal=[](const shape::Geometry&a,const shape::Geometry&b){return a.strength==b.strength&&a.quiet==b.quiet&&a.sealed==b.sealed&&std::memcmp(a.points,b.points,sizeof a.points)==0;};
    for(int energy:{0,1,4000,16000,32767})for(int freeze:{0,1,16384,32766,32767})for(int drift:{-32767,-11000,0,11000,32767})for(bool quiet:{false,true}){
        shape::Geometry a,b;shape::project(energy,freeze,drift,quiet,a);shape::project(energy,freeze,drift,quiet,b);CHECK(equal(a,b));
        CHECK(a.sealed==(freeze==32767)&&a.quiet==(a.strength==0));if(quiet||!energy)CHECK(!a.strength);
        for(const auto& contour:a.points){CHECK(contour[0].x==contour[shape::SEGMENTS].x&&contour[0].y==contour[shape::SEGMENTS].y);
            int x0=shape::EXTENT,y0=shape::EXTENT,x1=0,y1=0;
            for(const auto&p:contour){CHECK(p.x>=0&&p.x<=shape::EXTENT&&p.y>=0&&p.y<=shape::EXTENT);x0=std::min(x0,int(p.x));x1=std::max(x1,int(p.x));y0=std::min(y0,int(p.y));y1=std::max(y1,int(p.y));}
            CHECK(x1-x0>40&&y1-y0>300);}
        if(a.sealed){shape::project(energy,freeze,-drift,quiet,b);CHECK(equal(a,b));}
        ++geometry_cases;
    }
    shape::Geometry a,b;shape::project(-1,-20,-40000,false,a);shape::project(0,0,-32767,false,b);CHECK(equal(a,b));
    shape::project(65535,50000,40000,false,a);shape::project(32767,32767,32767,false,b);CHECK(equal(a,b));
    unsigned last=0;for(int energy=0;energy<=32767;energy+=511){const auto strength=shape::strength(energy);CHECK(strength>=last);last=strength;}CHECK(shape::strength(0)==0&&shape::strength(32767)==255);
}
void presentation_group(){
    for(auto theme:{palette::Theme::Graphite,palette::Theme::Pink}){
        palette::set(theme);Scene s(UnitKind::Ghost);active(s);
        for(int p=0;p<8;++p){choose(s,p);label="param "+std::to_string(p);draw(s);
            const auto saved=controls(s);for(int edge:{0,1}){s.set(p,q15(edge?32767:(p==G::P_FINE?-32767:0)));s.capture();draw(s);}
            std::copy(saved.begin(),saved.end(),std::begin(s.unit->params));s.unit->control_tick();s.capture();}
        // Every rail with an actual ENV route at both ends. The approved lower
        // surface has no routing badges; all readouts are genuine current DTOs.
        for(int edge:{0,1}){for(int p=0;p<8;++p){auto&q=s.unit->params[p];q.base=q15(edge?32767:(p==G::P_FINE?-32767:0));q.src=ModSource::EnvFollow;q.mod_depth=-32767;q.mod_enabled=true;q.snap();}s.unit->control_tick();s.capture();label="all rails / env / extrema";draw(s);}
        s.restore();active(s);choose(s,G::P_DECAY);const auto ordinary=draw(s);open(s);label="held L native overlay";const auto lens=draw(s);
        CHECK(lens.top==ordinary.top&&lens.bottom!=ordinary.bottom);close();s.capture();CHECK(draw(s).bottom==ordinary.bottom);
        for(int state=0;state<7;++state){s.restore();active(s);choose(s,G::P_MIX);label="state "+std::to_string(state);
            if(state==0){ghost(s).reset();s.capture();CHECK(ghost(s).visual_energy()==0);}
            if(state==1)s.unit->bypass=true;
            if(state==2)s.engine->chains[0].mute=true;
            if(state==3){s.unit->params[G::P_MIX].src=ModSource::SigilH;s.unit->params[G::P_MIX].mod_depth=12000;s.unit->params[G::P_MIX].mod_enabled=false;}
            if(state>=4){s.set(G::P_FREEZE,32767);s.render(state==4?1:512);if(state==6){s.set(G::P_FREEZE,0);s.render(1);}}
            s.capture();draw(s);open(s);draw(s);close();}
        // The shared lens must use Ghost fine and percent, not the numeric id
        // aliases belonging to FM. Compare an independently formatted root.
        s.restore();s.set(G::P_TONE,12000);s.set(G::P_FINE,16384);s.capture();choose(s,G::P_TONE);
        const auto hz=G::root_millihz(12000,16384);char expected[32],value[32];std::snprintf(expected,sizeof expected,"%lu.%02luhz",(unsigned long)(hz/1000),(unsigned long)(hz%1000/10));
        ghost_focus_value_text(value,sizeof value,G::P_TONE,12000);CHECK(std::string(value)==expected);open(s);CHECK(has(draw(s).bottom_text,expected));close();
        s.set(G::P_DECAY,16384);choose(s,G::P_DECAY);open(s);CHECK(has(draw(s).bottom_text,"50%"));close();
        // A full real six-unit breadcrumb must remain within the top, including
        // similarly prefixed strain/strn. No old raw-inspector expectations.
        for(int slot=0;slot<UNITS_PER_CHAIN;++slot)if(slot!=s.slot)delete s.engine->replace_unit(0,slot,make_unit(slot%2?UnitKind::Stranular:UnitKind::Strain));
        s.capture();label="six unit path";draw(s);
    }
}
void ordinary_group(){
    Scene s(UnitKind::Ghost);set_note_mode(s,false);
    const int tested[]={G::P_TONE,G::P_DECAY,G::P_SMEAR,G::P_DRIFT,G::P_MIX,G::P_FINE,G::P_RATE};
    for(int p:tested){s.set(G::P_FREEZE,0);s.set(p,p==G::P_FINE?123:12000);s.capture();const auto at=point(s,p);const auto before=controls(s);label="ordinary relative "+std::to_string(p);
        const int dirty=mutations;down(at.first,at.second);CHECK(ghost_focus_selected()==p&&g_drag!=Drag::None);move(at.first,at.second);same(s,before);CHECK(mutations==dirty);
        const bool vertical=p==G::P_DECAY||p==G::P_DRIFT||p==G::P_MIX;
        if(vertical)move(at.first+9,at.second);else move(at.first,at.second+5);same(s,before);
        const int step=p==G::P_FINE?64:p==G::P_RATE?32767/186:p==G::P_TONE?32767/150:p==G::P_DECAY?32767/82:p==G::P_DRIFT?32767/52:p==G::P_MIX?32767/65:32767/114;
        move(at.first+(vertical?0:1),at.second-(vertical?1:0));CHECK(s.unit->params[p].base==before[p].base+step);
        for(int other=0;other<8;++other)if(other!=p)CHECK(s.unit->params[other].base==before[other].base);
        up();CHECK(g_drag==Drag::None);const auto stored=controls(s);move(at.first+20,at.second-20);up();same(s,stored);++gestures;
    }
    s.set(G::P_TONE,12000);s.capture();set_note_mode(s,true);const auto initial=s.unit->params[0].base;const auto at=point(s,0);
    down(at.first,at.second);move(at.first+5,at.second);CHECK(s.unit->params[0].base==initial);move(at.first+6,at.second);
    const double note=69+12*std::log2(G::root_millihz(s.unit->params[0].base,0)/440000.0);CHECK(std::abs(note-std::round(note))<0.003);up();
    const auto tuned=s.unit->params[0].base;tap(150,10);CHECK(s.unit->params[0].base==tuned);tap(150,10);CHECK(s.unit->params[0].base==tuned);
    s.set(G::P_FINE,-12000);tap(225,70);CHECK(s.unit->params[G::P_FINE].base==0);
    const bool mode=ghost(s).long_tail;tap(260,10);CHECK(ghost(s).long_tail!=mode);tap(260,10);CHECK(ghost(s).long_tail==mode);
    // Installed FREEZE remains immediate; no new release-only or R-fine law is
    // silently assigned to the ordinary bespoke controller.
    s.set(G::P_FREEZE,0);s.capture();down(160,117);CHECK(s.unit->params[G::P_FREEZE].base==32767);up();s.set(G::P_FREEZE,32767);s.render(512);s.capture();
    CHECK(ghost(s).visual_freeze()==32767);const auto held=controls(s);
    for(int p:{G::P_TONE,G::P_DECAY,G::P_SMEAR,G::P_DRIFT,G::P_FINE,G::P_RATE}){const auto pos=point(s,p);down(pos.first,pos.second);move(pos.first+1,pos.second-1);up();same(s,held);}
    tap(180,30);tap(225,70);tap(260,10);same(s,held);CHECK(ghost(s).long_tail==mode);
    const auto mix=point(s,G::P_MIX);down(mix.first,mix.second);move(mix.first,mix.second-1);up();CHECK(s.unit->params[G::P_MIX].base==held[G::P_MIX].base+32767/65);
    tap(160,117);CHECK(s.unit->params[G::P_FREEZE].base==0);s.set(G::P_FREEZE,0);s.render(512);s.capture();CHECK(!ghost(s).visual_freeze());++gestures;
}
void lens_gesture(Scene& s,int p,bool depth){
    choose(s,p);auto&q=s.unit->params[p];q.base=p==G::P_FINE?0:12000;q.mod_depth=3000;q.src=ModSource::SigilC;q.mod_enabled=false;q.snap();s.capture();open(s);
    const int y=depth?147:102,span=depth||q.bipolar?65534:32767,div=depth?8:p==G::P_TONE?16:8;const auto before=controls(s);
    const auto value=[&](){return depth?q.mod_depth:q.base;};const q15 start=value();const int dirty=mutations;
    down(137,y,KEY_L);move(137,y-18,KEY_L);CHECK(value()==start&&mutations==dirty);move(144,y,KEY_L);CHECK(value()==start+7*span/271);
    const q15 coarse=value();hid::frame(KEY_R,KEY_L|KEY_R|KEY_TOUCH);move(144,y,KEY_L|KEY_R);CHECK(value()==coarse);move(152,y,KEY_L|KEY_R);CHECK(value()==coarse+8*span/(271*div));
    const q15 fine=value();hid::frame(0,KEY_L|KEY_TOUCH,KEY_R);move(152,y,KEY_L);CHECK(value()==fine);move(147,y,KEY_L);CHECK(value()==fine-5*span/271);up(KEY_L);
    CHECK(q.src==ModSource::SigilC&&!q.mod_enabled);CHECK(depth?q.base==before[p].base:q.mod_depth==before[p].mod_depth);CHECK(!s.engine->param_temp_active());close();++gestures;
}
void lens_group(){
    Scene s(UnitKind::Ghost);for(int p=0;p<8;++p){label="lens relative "+std::to_string(p);lens_gesture(s,p,false);lens_gesture(s,p,true);}
    choose(s,G::P_DECAY);auto&q=s.unit->params[G::P_DECAY];q.base=12000;q.mod_depth=-7890;q.snap();open(s);label="all ten sources";
    for(int i=0;i<10;++i){q.mod_enabled=false;const ModSource expected=i==0?ModSource::None:i==9?ModSource::EnvFollow:sigil_mod_source(i-1);tap(8+31*i+15,188,KEY_L);
        CHECK(q.src==expected&&q.mod_enabled&&q.base==12000&&q.mod_depth==-7890);const int dirty=mutations;tap(8+31*i+15,188,KEY_L);CHECK(mutations==dirty);++routes;}
    for(int chain=0;chain<4;++chain){tap(100+32*chain,214,KEY_L);CHECK(s.engine->env_follow_source()==chain);}
    for(int n=0;n<3;++n){const int old=int(s.engine->env_follow_response());tap(265,214,KEY_L);CHECK(int(s.engine->env_follow_response())==(old+1)%3);}
    tap(54,188,KEY_L);tap(284,59,KEY_L);CHECK(!q.mod_enabled&&q.src==ModSource::SigilA&&q.mod_depth==-7890);tap(54,188,KEY_L);CHECK(q.mod_enabled&&q.mod_depth==-7890);
    tap(23,188,KEY_L);const int dirty=mutations;tap(284,59,KEY_L);CHECK(q.src==ModSource::None&&q.mod_depth==-7890&&mutations==dirty);close();
}
void ownership_group(){
    for(bool same_edge:{false,true}){Scene s(UnitKind::Ghost);label="held L quarantine";const auto before=controls(s);const auto at=point(s,G::P_TONE);
        hid::touch={at.first,at.second};if(!same_edge)hid::frame(KEY_TOUCH,KEY_TOUCH);
        const auto enter=hid::frame(KEY_L|KEY_A|KEY_X|(same_edge?u32(KEY_TOUCH):0u),KEY_L|KEY_TOUCH);CHECK(enter.owned&&enter.skip&&!enter.remaining&&param_lens_is_open());
        move(295,102,KEY_L);same(s,before);up(KEY_L);down(200,102,KEY_L);move(207,102,KEY_L);CHECK(s.unit->params[0].base!=before[0].base);
        const auto exit=hid::frame(KEY_X|KEY_A|KEY_R,KEY_TOUCH,KEY_L);CHECK(exit.skip&&!exit.owned&&!exit.remaining&&!param_lens_is_open());const auto saved=controls(s);
        move(at.first+20,at.second);same(s,saved);up();++ownership;}
    {Scene s(UnitKind::Ghost);choose(s,G::P_DECAY);open(s,false);label="latched footer / keyboard / B";down(140,102);hid::frame(KEY_RIGHT,KEY_TOUCH);
        CHECK(param_lens_is_open()&&ghost_focus_selected()==G::P_SMEAR);const auto saved=controls(s);move(220,102);same(s,saved);up();hid::frame(KEY_LEFT,0);CHECK(ghost_focus_selected()==G::P_DECAY);
        hid::frame(KEY_L,KEY_L);hid::frame(0,0,KEY_L);CHECK(param_lens_is_open());tap(250,232);CHECK(param_lens_is_open());
        const auto result=hid::frame(KEY_B,0);CHECK(result.skip&&!result.remaining&&!param_lens_is_open()&&g_overlay==Overlay::Editor);++ownership;}
    for(bool lens:{false,true}){Scene s(UnitKind::Ghost);label="replacement cancels";if(lens){open(s);down(150,102,KEY_L);}else down(150,76);
        Unit* retained=s.engine->replace_unit(0,s.slot,new GhostUnit);auto* replacement=s.engine->chains[0].units[s.slot];const auto base=replacement->params[G::P_FINE].base;
        move(200,lens?102:76,lens?KEY_L:0);CHECK(replacement->params[G::P_FINE].base==base&&g_drag==Drag::None&&!param_lens_is_open());
        delete s.engine->replace_unit(0,s.slot,retained);up();hid::frame(0,0);++ownership;}
    {Scene s(UnitKind::Ghost);label="retired inline routing";for(int p=0;p<8;++p){auto&q=s.unit->params[p];q.src=ModSource::SigilD;q.mod_depth=-4321;q.mod_enabled=false;}const auto before=controls(s);
        for(int p=0;p<8;++p){choose(s,p);for(auto xy:{std::pair<int,int>{274,211},{295,211},{317,224}}){tap(xy.first,xy.second);same(s,before);CHECK(!param_lens_is_open()&&g_drag==Drag::None);}
            for(int source=0;source<8;++source){tap(10,43+14*source);same(s,before);CHECK(!param_lens_is_open());}}
        // Former bottom routing rail/off now belongs only to ordinary cycle.
        s.set(G::P_RATE,12000);const auto cycle=s.unit->params[G::P_RATE].base;down(150,220);move(151,220);up();CHECK(s.unit->params[G::P_RATE].base==cycle+32767/186);same(s,before,false);
        const auto ordinary=controls(s);g_drag=Drag::SigCanvas;ghost_focus_drag(ghost(s),200,80);same(s,ordinary);ghost_focus_release();++ownership;
    }
    {Scene s(UnitKind::Ghost);label="p-lock / full bank / context cancellation";set_note_mode(s,false);s.set(G::P_FINE,500);s.set(G::P_FREEZE,0);s.capture();
        g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=5;CHECK(s.engine->set_step_lock(0,5,s.slot,G::P_FINE,1000));
        down(150,76);move(150,76);CHECK(seq_display_value(s.slot,G::P_FINE,0)==1000);move(151,76);up();CHECK(seq_display_value(s.slot,G::P_FINE,0)==1064&&s.unit->params[G::P_FINE].base==500);
        down(150,76);g_seq_lock_step=6;move(160,76);up();CHECK(seq_display_value(s.slot,G::P_FINE,500)==500&&s.unit->params[G::P_FINE].base==500);g_seq_lock_step=5;
        s.engine->clear_step_locks(0,5);for(int p=0;p<4;++p)CHECK(s.engine->set_step_lock(0,5,s.slot,p,1000));const auto old=pattern_hash(s.engine->pattern);down(150,76);move(151,76);up();CHECK(pattern_hash(s.engine->pattern)==old&&g_seq_lock_flash>0);
        const auto saved=controls(s);tap(250,232);hid::frame(KEY_L,KEY_L);CHECK(!param_lens_is_open());same(s,saved);hid::frame(0,0);g_seq_lock_armed=false;g_view=View::Chains;
        down(150,76);g_overlay=Overlay::None;s.capture();g_overlay=Overlay::Editor;move(160,76);up();same(s,saved);++ownership;
    }
}
int run(){int count=0,failed=0;const auto group=[&](const char* title,auto fn){++count;try{fn();std::printf("ok: ghost refresh %s\n",title);}catch(const std::exception&e){++failed;close();std::fprintf(stderr,"FAIL: ghost refresh %s [%s]: %s\n",title,label.c_str(),e.what());}};
    group("pure closed contour geometry / bounds / determinism",helper_group);
    group("all8 values / states / themes / actual duals / poisoned detached draw",presentation_group);
    group("installed root-note-free / relative fine-cycle / freeze / mix",ordinary_group);
    group("shared minimal L / R-fine / no jump / sources / retained mute",lens_group);
    group("actual main ownership / latched / identity / retired hits / p-lock",ownership_group);
    std::printf("ghost refresh UI: %d/%d groups; %llu projections; %llu duals; %llu detached pairs; %llu gestures; %llu sources; %llu ownership cases; shared peak %d/8192\n",count-failed,count,(unsigned long long)geometry_cases,(unsigned long long)duals,(unsigned long long)detached,(unsigned long long)gestures,(unsigned long long)routes,(unsigned long long)ownership,peak);
    std::puts("focused actual C++ host UI only; zero inherited regression groups; no hardware/FPS/audio/deployment claims");return failed?1:0;
}
void save(const std::filesystem::path& out,std::ofstream& manifest,Scene&s,const std::string& name){label=name;s.capture();const auto r=draw(s,false);
    width=400;pixels=r.top;texts=r.top_text;dump_surface(out,name+"_top");width=320;pixels=r.bottom;texts=r.bottom_text;dump_surface(out,name+"_bottom");
    const int p=ghost_focus_selected();const auto&q=s.unit->params[p];manifest<<name<<'\t'<<p<<'\t'<<q.base<<'\t'<<q.effective<<'\t'<<q.mod_depth<<'\t'<<int(q.src)<<'\t'<<q.mod_enabled<<'\t'<<ghost(s).visual_energy()<<'\t'<<ghost(s).visual_freeze()<<'\t'<<ghost(s).visual_drift()<<'\t'<<s.engine->transport_frame<<'\t'<<r.upper<<'\t'<<r.lower<<'\t'<<r.upper+r.lower<<'\n';
}
int capture(const char* directory){try{const std::filesystem::path out(directory);CHECK(!std::filesystem::exists(out));std::filesystem::create_directories(out);host_time_ms=1000;
    std::ofstream manifest(out/"manifest.tsv");manifest<<"frame\tparam\tbase\teffective\tdepth\tsource\tenabled\tenergy\tfreeze\tdrift\ttransport_frame\ttop\tbottom\tshared\n";
    for(auto theme:{palette::Theme::Graphite,palette::Theme::Pink}){palette::set(theme);const std::string prefix=theme==palette::Theme::Graphite?"graphite_":"pink_";
        Scene s(UnitKind::Ghost);active(s);choose(s,G::P_DECAY);save(out,manifest,s,prefix+"decay_active");
        s.set(G::P_FREEZE,32767);s.render(512);choose(s,G::P_FREEZE);save(out,manifest,s,prefix+"freeze_held");
        s.set(G::P_FREEZE,0);s.render(512);s.set(G::P_FINE,16384);choose(s,G::P_TONE);s.unit->params[0].src=ModSource::SigilB;s.unit->params[0].mod_depth=11000;s.unit->params[0].mod_enabled=true;s.capture();open(s);
        save(out,manifest,s,prefix+"root_fine_l");close();
        choose(s,G::P_FINE);s.set(G::P_FINE,-32767);s.set(G::P_RATE,32767);s.capture();save(out,manifest,s,prefix+"fine_cycle_extrema");
    }manifest.close();std::ofstream receipt(out/"capture.json");receipt<<"{\n  \"pairs\": 8, \"regression_groups_run\": 0, \"shared_peak\": "<<peak<<", \"budget\": 8192,\n  \"provenance\": \"fresh current actual C++ editor/top/engine through native host raster; no authored UI or reused PNG pixels\",\n  \"hardware\": false, \"audio\": \"private engine scalars only; PCM discarded\"\n}\n";
    std::printf("ghost refresh captures: 8 fresh native pairs; 0 regression groups; shared peak %d/8192\n",peak);return 0;
    }catch(const std::exception&e){std::fprintf(stderr,"ghost capture [%s]: %s\n",label.c_str(),e.what());return 1;}}
} // namespace
} // namespace ghost_refresh_cases
int main(int argc,char**argv){if(argc==2&&std::string(argv[1])=="--proof-only")return ghost_refresh_cases::run();if(argc==3&&std::string(argv[1])=="--capture-only")return ghost_refresh_cases::capture(argv[2]);std::fprintf(stderr,"usage: ghost-refresh-ui --proof-only | --capture-only fresh-output-directory\n");return 2;}
