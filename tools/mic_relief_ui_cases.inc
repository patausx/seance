// Focused current MIC only. Actual main slices/editor/inspector/lens/AudioOut boundary.
// Synthetic raw PCM reaches the real MicUnit push/process APIs; no test is a device claim.
#include "../platform/3ds/mic_relief.h"
namespace mic_relief_cases {
using C=MicUnit;
int duals=0,peak=0,gestures=0,pcm_cases=0;
struct Line {int x0,y0,x1,y1;bool operator==(const Line&o)const{return x0==o.x0&&y0==o.y0&&x1==o.x1&&y1==o.y1;}};
std::vector<Line> relief_lines;
bool observe_lines=false;
void down(int x,int y){input::touch={x,y};input::frame(KEY_TOUCH,KEY_TOUCH);++gestures;}
void move(int x,int y){input::touch={x,y};input::frame(0,KEY_TOUCH);}
void up(){input::frame(0,0,KEY_TOUCH);}
void tap(int x,int y){down(x,y);up();}
struct Scene:ModuleInspectorFixture {
    Scene():ModuleInspectorFixture(UnitKind::MicIn){
        input::nav_opened=false;input::frame(0,0);g_sel_chain=g_sel_slot=0;
        for(int c=0;c<NUM_CHAINS;++c)for(int p=0;p<UNITS_PER_CHAIN;++p)if(c||p)delete engine->replace_unit(c,p,nullptr);
        host_time_ms+=1000;g_mic_ok=true;C::reset_input();mic_focus_release();mic_focus_select(0);set();entry();
    }
    ~Scene(){param_lens_cancel();mic_focus_release();g_drag=Drag::None;g_mic_ok=false;C::reset_input();g_overlay=Overlay::None;mic_focus_capture();}
    C& u(){return *static_cast<C*>(module);}
    void set(q15 gain=8192,q15 gate=32767,q15 cut=0){const q15 v[]={gain,gate,cut};for(int p=0;p<C::P_COUNT;++p){u().params[p].base=v[p];u().params[p].src=ModSource::None;u().params[p].mod_depth=0;u().params[p].snap();}u().bypass=false;engine->chains[0].mute=false;}
    void entry(){g_overlay=Overlay::None;capture();g_overlay=Overlay::Editor;capture();}
    std::array<q15,3> bases(){return {u().params[0].base,u().params[1].base,u().params[2].base};}
    void push(const q15*raw,int n,unsigned dt=32,bool process=true){host_time_ms+=dt;C::push(raw,n);if(process){std::array<q15,256> out{};u().process(out.data(),out.size());}++g_ui_frame;capture();++pcm_cases;}
    void feed(int variant=0,unsigned dt=32){std::array<q15,C::INPUT_VISUAL_SAMPLES> raw{};
        for(int i=0;i<int(raw.size());++i){const double x=double(i)/(raw.size()-1);const double env=.13+.87*std::pow(std::sin(3.141592653589793*x),2);
            raw[i]=q15(std::lround((10500+variant*670)*env*std::sin(i*.17+variant*.41)));}push(raw.data(),raw.size(),dt);}
    void fill(int v,unsigned dt=32,int n=C::INPUT_VISUAL_SAMPLES){std::array<q15,C::INPUT_VISUAL_SAMPLES>raw{};raw.fill(q15(v));push(raw.data(),n,dt);}
    void history(){for(int i=0;i<15;++i)feed(i%7);}
};
void bounds(bool lens=false){
    if(lens){texts.clear();text_overlaps=0;} // inherited translucent MOD overlay intentionally overlaps paper words
    if(outside_rects||outside_lines||text_overlaps){std::fprintf(stderr,"mic w%d bounds%d/%d overlap%d\n",width,outside_rects,outside_lines,text_overlaps);for(const auto&t:texts)std::fprintf(stderr," %d,%d %dx%d %s\n",t.x,t.y,t.w,t.h,t.text.c_str());}
    CHECK(objects>0&&objects<8192&&!outside_rects&&!outside_lines&&!text_overlaps);
    for(const auto&t:texts)CHECK(t.x>=0&&t.y>=0&&t.x+t.w<=width&&t.y+t.h<=240);
}
void surface(Scene&,bool top,bool integrated=true){
    module_inspector_surface(top);relief_lines.clear();observe_lines=top;
    if(integrated){fader_lancet_cases::AudioHidden hidden;if(top)top_draw();else editor_draw();}
    else {ModuleInspectorNoEngine hidden;if(top)top_draw();else {C decoy;mic_focus_draw(decoy);recorder_focus_draw_return();if(param_lens_is_open())param_lens_draw_bottom();}}
    observe_lines=false;bounds(!top&&param_lens_is_open());
}
struct Pair {std::vector<u32>top,bottom;std::vector<Line>lines;int upper=0,lower=0;};
Pair pair(Scene&s,bool recapture=true){if(recapture)s.capture();editor_fullscreen_cases::prepare();const auto time=host_time_ms;const auto clock=s.engine->transport_frame;const int dirty=mutations;const auto process=s.u().visual_process_serial(),serial=C::input_serial(),allocations=editor_gpu::inits;
    Pair result;surface(s,true);result.top=pixels;result.lines=relief_lines;result.upper=objects;surface(s,false);result.bottom=pixels;result.lower=objects;
    peak=std::max(peak,result.upper+result.lower);CHECK(peak<8192);++duals;
    const auto saved=g_ui_snapshot;g_ui_snapshot={};host_time_ms+=100000;
    surface(s,true,false);CHECK(pixels==result.top&&objects==result.upper);surface(s,false,false);CHECK(pixels==result.bottom&&objects==result.lower);
    host_time_ms=time;g_ui_snapshot=saved;CHECK(s.engine->transport_frame==clock&&mutations==dirty&&C::input_serial()==serial&&s.u().visual_process_serial()==process&&editor_gpu::inits==allocations);
    return result;
}
std::vector<u32> art(const std::vector<u32>&p){std::vector<u32>r;for(int y=70;y<194;++y)for(int x=20;x<380;++x)r.push_back(p[y*400+x]);return r;}
bool has(const char*text){return module_inspector_text(text);}
void status(Scene&s,const char*text){surface(s,true);CHECK(has(text));}
void relative_gain(){Scene s;
    for(int raw:{0,1,2,8191,8192,16384,30000,32766,32767})for(auto origin:{std::pair<int,int>{110,53},{200,53},{130,94}}){
        s.set(q15(raw));const auto base=s.bases();const int dirty=mutations;down(origin.first,origin.second);CHECK(mic_focus_selected()==C::P_GAIN&&s.bases()==base);
        move(origin.first,origin.second+4);CHECK(s.bases()==base&&mutations==dirty);
        for(int dx:{-90,-17,-1,0,1,17,90}){move(origin.first+dx,origin.second);CHECK(s.u().params[C::P_GAIN].base==std::clamp(raw+dx*32767/279,0,32767));}
        move(origin.first,origin.second);up();CHECK(s.bases()==base&&g_drag==Drag::None);
    }
    // Both gain value and rail own every native edge; gap/name are not a hidden slider.
    for(auto b:{std::array<int,4>{104,32,204,41},{12,74,296,39}})for(int corner=0;corner<4;++corner){s.set();int x=b[0]+((corner&1)?b[2]-1:0),y=b[1]+((corner&2)?b[3]-1:0);down(x,y);CHECK(g_drag==Drag::MicGain);up();CHECK(s.u().params[0].base==8192);}
    for(auto xy:{std::pair<int,int>{20,53},{102,53},{150,73},{100,118},{208,140},{240,190}}){const auto before=s.bases();tap(xy.first,xy.second);CHECK(s.bases()==before);}
    pair(s);
}
struct Choice {int x,y,w,h,param,value;};
constexpr Choice choices[]={{110,122,97,42,C::P_MON,32767},{213,122,95,42,C::P_MON,0},{108,176,62,41,C::P_LOW_CUT,0},{176,176,62,41,C::P_LOW_CUT,16384},{244,176,64,41,C::P_LOW_CUT,32767}};
void owned_choices(){Scene s;
    for(const auto&b:choices)for(int corner=0;corner<4;++corner){s.set(8192,q15(b.param==C::P_MON&&b.value==32767?0:32767),q15(b.param==C::P_LOW_CUT&&b.value==0?32767:0));const auto old=s.bases();const int x=b.x+((corner&1)?b.w-1:0),y=b.y+((corner&2)?b.h-1:0);
        down(x,y);CHECK(s.bases()==old);move(x,y);CHECK(s.bases()==old);up();CHECK(s.u().params[b.param].base==b.value);const int dirty=mutations;tap(x,y);CHECK(mutations==dirty);
        s.set(old[0],old[1],old[2]);down(x,y);move(319,28);move(x,y);up();CHECK(s.bases()==old);
    }
    for(int corner=0;corner<4;++corner){const int x=238+((corner&1)?31:0),y=(corner&2)?27:0;s.u().bypass=false;down(x,y);CHECK(!s.u().bypass);up();CHECK(s.u().bypass);down(x,y);move(220,29);move(x,y);up();CHECK(s.u().bypass);tap(x,y);CHECK(!s.u().bypass);}
    pair(s);
}
void cancellation(){
    for(int reason=0;reason<11;++reason){Scene s;const auto before=s.bases();down(260,143);
        if(reason==0)g_overlay=Overlay::None;
        if(reason==1)g_view=View::Sigils;
        if(reason==2)g_edit_slot=1;
        if(reason==3)g_seq_lock_armed=true;
        if(reason==4)s.engine->active_pattern=1;
        if(reason==5)input::nav_opened=true;
        if(reason==6)input::frame(KEY_A,KEY_A|KEY_TOUCH);
        if(reason==7)input::frame(KEY_X,KEY_X|KEY_TOUCH);
        if(reason==8)input::frame(KEY_B,KEY_B|KEY_TOUCH);
        if(reason==9)input::frame(KEY_L,KEY_L|KEY_TOUCH);
        if(reason==10)editor_cancel_input();
        s.capture();up();CHECK(s.bases()==before);input::nav_opened=false;param_lens_cancel();g_overlay=Overlay::Editor;g_view=View::Chains;g_edit_slot=0;g_seq_lock_armed=false;input::frame(0,0);
    }
    {Scene s;down(260,143);Unit* old=s.engine->replace_unit(0,0,new C);s.capture();up();CHECK(static_cast<C*>(old)->params[C::P_MON].base==32767&&static_cast<C*>(s.engine->chains[0].units[0])->params[C::P_MON].base==32767);delete s.engine->replace_unit(0,0,old);}
    // A gesture cannot resurrect after a capture observes a wrong context.
    {Scene s;down(260,143);g_overlay=Overlay::None;s.capture();g_overlay=Overlay::Editor;move(260,143);up();CHECK(s.u().params[C::P_MON].base==32767);}
    {Scene s;down(160,94);s.engine->active_pattern=1;move(200,94);up();CHECK(s.u().params[0].base==8192);}
}
void locks_lens(){Scene s;const auto before=s.bases();g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;
    CHECK(s.engine->set_step_lock(0,7,0,C::P_GAIN,6000));down(140,94);move(147,94);up();CHECK(seq_display_value(0,0,0)==6000+7*32767/279&&s.bases()==before);
    tap(260,143);CHECK(seq_display_value(0,1,32767)==0&&s.bases()==before);tap(200,195);CHECK(seq_display_value(0,2,0)==16384&&s.bases()==before);pair(s);
    for(auto xy:{std::pair<int,int>{20,53},{20,141},{20,195}})tap(xy.first,xy.second);
    for(int p=0;p<3;++p)CHECK(seq_display_value(0,p,12345)==12345);
    tap(240,10);CHECK(!s.u().bypass);
    // Pattern ownership matters even when the same chain/step/slot stays selected.
    down(260,143);s.engine->active_pattern=1;up();CHECK(seq_display_value(0,1,32767)==32767);s.engine->active_pattern=0;
    s.engine->clear_track(0);delete s.engine->replace_unit(0,1,new FmUnit);for(int p=0;p<SEQ_LOCKS;++p)CHECK(s.engine->set_step_lock(0,7,1,p,1000));
    const auto hash=s.hash();down(140,94);move(150,94);up();tap(260,143);tap(200,195);CHECK(s.hash()==hash&&s.bases()==before&&g_seq_lock_flash>0);pair(s);
    g_seq_lock_armed=false;g_view=View::Chains;
    for(int p=0;p<3;++p){mic_focus_select(p);input::frame(0,0);input::frame(KEY_L,KEY_L);CHECK(param_lens_is_open());pair(s);input::frame(0,0,KEY_L);CHECK(!param_lens_is_open());input::frame(0,0);CHECK(mic_focus_selected()==p);}
    // Base/live remain distinct; no audio/parameter mutation is performed by draw.
    s.u().params[C::P_GAIN].base=8192;s.u().params[C::P_GAIN].effective=16384;s.u().params[C::P_GAIN].src=ModSource::SigilA;s.u().params[C::P_GAIN].mod_enabled=true;
    pair(s);CHECK(s.u().params[C::P_GAIN].base==8192&&s.u().params[C::P_GAIN].effective==16384);
}
void freshness(){Scene s;status(s,"waiting");auto idle=pair(s);host_time_ms+=1000;s.capture();CHECK(art(pair(s,false).top)==art(idle.top));
    s.feed();auto active=pair(s);CHECK(!active.lines.empty());const auto serial=C::input_serial();host_time_ms+=31;s.capture();CHECK(pair(s,false).lines==active.lines&&C::input_serial()==serial);
    // Capture at 32ms without any new serial must not shift/decay a layer.
    host_time_ms+=1;s.capture();CHECK(pair(s,false).lines==active.lines);
    host_time_ms+=117;s.capture();status(s,"live input");CHECK(pair(s,false).lines==active.lines);
    host_time_ms+=1;s.capture();status(s,"no new input");pair(s);
    s.feed();CHECK(!pair(s).lines.empty());g_mic_ok=false;s.capture();status(s,"mic unavailable");pair(s);
    g_mic_ok=true;s.capture();status(s,"waiting");s.feed();s.entry();status(s,"waiting"); // old ring at reentry isn't live
    host_time_ms+=40;s.capture();status(s,"waiting");s.feed();CHECK(!pair(s).lines.empty());
    C::reset_input();s.capture();status(s,"waiting");s.fill(-32768,32,1);pair(s);s.fill(32767,32,17);pair(s);s.fill(0);pair(s);
}
void raw_and_output(){Scene s;s.history();const auto original=pair(s);const auto shape=art(original.top);
    for(int reason=0;reason<5;++reason){s.set();if(reason==0)s.u().params[0].base=s.u().params[0].effective=32767;if(reason==1)s.u().params[1].base=s.u().params[1].effective=0;if(reason==2)s.u().params[2].base=s.u().params[2].effective=32767;if(reason==3)s.u().bypass=true;if(reason==4)s.engine->chains[0].mute=true;
        s.capture();auto altered=pair(s,false);CHECK(altered.lines==original.lines&&art(altered.top)==shape);
        s.entry();s.history();auto arrivals=pair(s);CHECK(arrivals.lines==original.lines&&art(arrivals.top)==shape);}
    s.set(32767);s.fill(20000);CHECK(s.u().visual_clip()&&s.u().visual_output_peak()==32767);surface(s,true);CHECK(has("clip")&&has("0.0 dbfs"));pair(s);
    s.set(8192,0);s.fill(20000);CHECK(s.u().visual_output_peak()==0&&!s.u().visual_clip());surface(s,true);CHECK(has("silent")&&!has("clip"));pair(s);
    s.set();for(int i=0;i<15;++i)s.fill(0);const auto silence=pair(s);CHECK(silence.lines.size()==15);for(int i=0;i<4;++i){s.fill(0);CHECK(pair(s).lines==silence.lines);}
    // Fixed amplitude, no auto normalization: quiet and loud raw input cannot yield equal relief.
    s.entry();s.fill(2048);auto quiet=pair(s);s.entry();s.fill(30000);CHECK(pair(s).lines!=quiet.lines);
    // No pointer/PCM/clock read in draw: poison source and unit after capture, retain exact pixels.
    s.set();s.history();auto frozen=pair(s);s.set(0,0,32767);s.u().bypass=true;C::reset_input();g_mic_ok=false;
    editor_fullscreen_cases::prepare();surface(s,true,false);CHECK(pixels==frozen.top);surface(s,false,false);CHECK(pixels==frozen.bottom);
}
void output_freshness(){Scene s;s.set(32767);s.fill(20000);const auto processed=s.u().visual_process_serial();surface(s,true);CHECK(has("clip")&&has("0.0 dbfs"));
    std::array<q15,C::INPUT_VISUAL_SAMPLES> raw{};raw.fill(5000);
    s.push(raw.data(),raw.size(),149,false);surface(s,true);CHECK(has("clip")&&has("0.0 dbfs"));
    s.push(raw.data(),raw.size(),1,false);surface(s,true);CHECK(!has("clip")&&has("--")&&!has("no new input"));CHECK(s.u().visual_process_serial()==processed);pair(s);
    s.set(8192,0);s.capture();surface(s,true);CHECK(has("silent")&&!has("clip"));pair(s);
    s.set();s.push(raw.data(),raw.size(),32,true);surface(s,true);CHECK(!has("--"));pair(s);
    // Many distinct raw-magnitude bands and changing windows exercise coalesced draw cost.
    uint32_t rng=41;for(int row=0;row<15;++row){for(auto&v:raw){rng=rng*1664525u+1013904223u;v=q15(rng>>16);}s.push(raw.data(),raw.size());pair(s);}
}
void keys_and_return(){
    {Scene s;struct TriggerProbe:VcaUnit{int count=0;void trigger()override{++count;VcaUnit::trigger();}};
        auto*a=new TriggerProbe;auto*b=new TriggerProbe;auto*other=new TriggerProbe;auto*r=new RecUnit;
        delete s.engine->replace_unit(0,1,a);delete s.engine->replace_unit(0,2,b);delete s.engine->replace_unit(0,3,r);delete s.engine->replace_unit(1,0,other);
        s.engine->launch_quantize=LaunchQuantize::Immediate;input::trigger_frame(KEY_A,KEY_A);CHECK(a->count==1&&b->count==1&&other->count==0&&!r->writer_open());}
    {Scene s;CHECK(!editor_back());CHECK(g_overlay==Overlay::Editor); // facade leaves ordinary back to existing main path
        auto*r=new RecUnit;delete s.engine->replace_unit(0,1,r);g_edit_slot=g_sel_slot=1;s.capture();tap(269,10);tap(260,56);CHECK(g_edit_slot==0);input::frame(0,0);
        down(203,10);CHECK(g_edit_slot==0);move(185,30);move(203,10);up();CHECK(g_edit_slot==0);tap(203,10);CHECK(g_edit_slot==1);
        tap(269,10);tap(260,56);CHECK(g_edit_slot==0);delete s.engine->replace_unit(0,1,new RecUnit);input::frame(0,0);tap(203,10);CHECK(g_edit_slot==0);
    }
}
void relief_model(){
    namespace r=seance::platform::ui3ds::mic_relief;
    static_assert(r::ROWS==15&&r::POINTS==65&&r::SAMPLES==1048&&r::INTERVAL_MS==32&&r::STALE_MS==150);
    CHECK(sizeof(r::History)<2100&&sizeof(r::Plot)==5235);
    for(uint32_t v=0;v<=32768;++v)CHECK(r::root(v*32768u)==uint32_t(std::floor(std::sqrt(double(v*32768u)))));
    r::History h;std::array<int16_t,r::SAMPLES>pcm{};pcm.fill(32767);uint64_t time=100;uint32_t serial=91;
    h.update(pcm.data(),pcm.size(),serial,time,true);CHECK(h.status==r::Status::Waiting&&!h.received&&!h.has_row);
    h.update(pcm.data(),pcm.size(),serial,time+1000,true);CHECK(h.status==r::Status::Waiting);
    time+=1001;h.update(pcm.data(),pcm.size(),++serial,time,true);CHECK(h.status==r::Status::Live&&h.has_row);
    std::array<unsigned char,sizeof h.rows> rows{};std::memcpy(rows.data(),h.rows,sizeof h.rows);
    pcm.fill(0);h.update(pcm.data(),pcm.size(),++serial,time+31,true);CHECK(std::memcmp(rows.data(),h.rows,sizeof h.rows)==0);
    h.update(pcm.data(),pcm.size(),serial,time+32,true);CHECK(std::memcmp(rows.data(),h.rows,sizeof h.rows)==0);
    h.update(pcm.data(),pcm.size(),++serial,time+32,true);CHECK(h.rows[r::ROWS-2][32]>30000&&h.rows[r::ROWS-1][32]==0);
    time+=32;for(int n=0;n<14;++n){time+=32;h.update(pcm.data(),pcm.size(),++serial,time,true);}
    for(const auto&row:h.rows)for(auto v:row)CHECK(v==0);
    r::Plot plot;h.project(plot);int visible=0;
    for(int row=0;row<r::ROWS;++row){CHECK(r::width(row)==240+108*row/14&&r::baseline(row)==105+79*row/14);for(int x=0;x<r::COLUMNS;++x){CHECK(plot.y[row][x]==(x<=r::width(row)?r::baseline(row):r::HIDDEN));if(plot.y[row][x]!=r::HIDDEN)++visible;}}
    CHECK(visible>4300&&visible<4500);auto steady=plot;
    h.update(pcm.data(),pcm.size(),serial,time+149,true);h.project(plot);CHECK(std::memcmp(&steady,&plot,sizeof plot)==0&&h.status==r::Status::Live);
    h.update(pcm.data(),pcm.size(),serial,time+150,true);h.project(plot);CHECK(h.status==r::Status::Stale);for(const auto&row:plot.y)for(auto v:row)CHECK(v==r::HIDDEN);
    h.update(pcm.data(),pcm.size(),++serial,time+151,true);CHECK(h.status==r::Status::Live);
    h.update(pcm.data(),pcm.size(),++serial,time+152,false);CHECK(h.status==r::Status::Unavailable);
    h.update(pcm.data(),pcm.size(),serial,time+153,true);CHECK(h.status==r::Status::Waiting);
    h.update(pcm.data(),pcm.size(),++serial,time+154,true);CHECK(h.status==r::Status::Live);
    h.update(pcm.data(),pcm.size(),++serial,time,true);CHECK(h.status==r::Status::Waiting); // wall-clock rollback reseeds
    // Independent signed-magnitude/bin/smoothing/taper oracle, including missing prefix.
    for(int count:{-1,0,1,2,16,17,65,524,1047,1048,1049})for(int value:{-32768,-12345,0,2048,32767}){
        h.reset();h.update(nullptr,0,0,0,true);pcm.fill(int16_t(value));h.update(pcm.data(),count,1,32,true);const int n=std::clamp(count,0,1048);
        CHECK(h.status==(n?r::Status::Live:r::Status::Waiting));if(!n)continue;
        unsigned bins[65]{};for(int p=0;p<65;++p){const int a=p*1048/65,b=(p+1)*1048/65;const int present=std::max(0,b-std::max(a,1048-n));bins[p]=unsigned(std::abs(value))*present/(b-a);}
        for(int p=0;p<65;++p){unsigned weighted=0;for(int j=-2;j<=2;++j)weighted+=bins[std::clamp(p+j,0,64)]*(3-std::abs(j));const unsigned expected=unsigned(std::floor(std::sqrt(double(weighted/9)*32768)))*std::min(6,std::min(p,64-p))/6;CHECK(h.rows[14][p]==expected);}
        h.project(plot);for(int row=0;row<15;++row)for(int x=0;x<349;++x){const int y=plot.y[row][x];CHECK(y==255||(x<=r::width(row)&&y>=73&&y<=184));}
    }
    h.reset();h.update(nullptr,0,1,0,true);h.update(nullptr,1048,2,32,true);CHECK(h.status==r::Status::Waiting);h.update(pcm.data(),1048,0xffffffffu,64,true);CHECK(h.status==r::Status::Live);h.update(pcm.data(),1048,0,96,true);CHECK(h.status==r::Status::Live);
}
void themes(){for(auto theme:{palette::Theme::Graphite,palette::Theme::Pink,palette::Theme::Custom}){palette::set(theme);Scene s;pair(s);s.history();pair(s);s.set(32767);s.fill(-32768);pair(s);s.set(0,0,32767);pair(s);s.u().bypass=true;pair(s);}}
void capture(const std::filesystem::path&dir){CHECK(!std::filesystem::exists(dir));std::filesystem::create_directories(dir);int frames=0;
    for(auto theme:{palette::Theme::Graphite,palette::Theme::Pink}){palette::set(theme);Scene s;
        auto shot=[&](const char*name){s.capture();editor_fullscreen_cases::prepare();const std::string prefix=theme==palette::Theme::Graphite?"graphite_":"pink_";surface(s,true);dump_surface(dir,prefix+name+"_top");const int upper=objects;surface(s,false);dump_surface(dir,prefix+name+"_bottom");CHECK(upper+objects<8192);++frames;};
        if(theme==palette::Theme::Graphite)shot("waiting");
        s.history();shot("active");s.set(8192,0);shot("muted");
        if(theme==palette::Theme::Graphite){s.set(32767);s.fill(20000);shot("clip");s.set();for(int i=0;i<15;++i)s.fill(0);shot("silence");host_time_ms+=150;shot("stale");g_mic_ok=false;shot("unavailable");g_mic_ok=true;s.feed();g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;CHECK(s.engine->set_step_lock(0,7,0,C::P_GAIN,12000));shot("lock");g_seq_lock_armed=false;g_view=View::Chains;mic_focus_select(C::P_GAIN);s.open();shot("lens");param_lens_cancel();}else{s.set(32767);s.fill(20000);shot("clip");}
    }CHECK(frames==12);std::printf("mic relief capture: %d actual C++ duals; synthetic real MicUnit input; no device, FFT or regression groups\n",frames);
}
int proof(){int total=0,failed=0;auto run=[&](const char*name,auto fn){++total;try{fn();std::printf("ok: %s\n",name);}catch(const std::exception&e){++failed;std::fprintf(stderr,"FAIL %s: %s\n",name,e.what());}std::fflush(stdout);};
    run("pure bounded 15-layer history, independent raw mapping, no idle/pacing/settling",relief_model);
    run("gain raw-linear relative span279, no tap jump, native hit edges",relative_gain);
    run("explicit source/cut/bypass owned lift and permanent leave cancellation",owned_choices);
    run("key/nav/lens/unit/active-pattern/context cancellation",cancellation);
    run("three p-locks/name removal/full refusal/base-live/lens",locks_lens);
    run("entry waiting, serial pacing, 150ms stale, unavailable, partial input",freshness);
    run("raw geometry independence, actual clip telemetry, silence settling/frozen draw",raw_and_output);
    run("actual output serial freshness independent of fresh raw arrivals and hostile geometry",output_freshness);
    run("whole-chain A, existing back facade and validated REC return",keys_and_return);
    run("three themes, detached/frozen native dual bounds and resource ceiling",themes);
    std::printf("mic relief UI: %d/%d groups; %d duals; %d gestures; %d PCM cases; peak%d/8192 raw host objects; 0 inherited groups\n",total-failed,total,duals,gestures,pcm_cases,peak);return failed?1:0;
}
}
extern "C" void __real__ZN6seance2ui4lineEiiiiji(int,int,int,int,u32,int);
extern "C" void __wrap__ZN6seance2ui4lineEiiiiji(int a,int b,int c,int d,u32 colour,int pen){
    if(mic_relief_cases::observe_lines&&width==400&&b>=70&&b<194&&d>=70&&d<194)mic_relief_cases::relief_lines.push_back({a,b,c,d});
    __real__ZN6seance2ui4lineEiiiiji(a,b,c,d,colour,pen);
}
int main(int argc,char**argv){theme_test::select();editor_style::init();editor_fullscreen_cases::prepare();int result=2;
    if(argc==2&&std::string(argv[1])=="--proof-only")result=mic_relief_cases::proof();
    if(argc==3&&std::string(argv[1])=="--capture-only"){mic_relief_cases::capture(argv[2]);result=0;}
    editor_style::shutdown();return result;
}
