// New focused swarm controllers and actual native raster. No inherited suite runs.
#include "../platform/3ds/swarm_layout.h"
namespace swarm_minimal_cases {
namespace h=module_headings_cases;
namespace f=fader_lancet_cases;
namespace l=swarm_layout;
using S=StranularUnit;
int gestures=0,frozen=0,motion_frames=0,needle_checks=0,glyph_checks=0,bank_checks=0,wave_checks=0,peak_dots=0;
unsigned bank_mask=0,lens_mask=0;
std::ofstream motion;
std::string active_case;
const int main_params[]={S::P_POS,S::P_SIZE,S::P_DENS,S::P_SPRAY,S::P_PITCH,S::P_MIX};
struct Scene:h::Scene {
    Scene():h::Scene(UnitKind::Stranular){
        // Historical fixture selection is not this redesign's product default.
        stranular_focus_cancel();capture();CHECK(stranular_focus_selected()==S::P_SIZE);
    }
    void select(int p){stranular_focus_select(p);}
    ~Scene(){editor_cancel_input();param_lens_cancel();stranular_focus_cancel();}
};
S& unit(Scene&s){return *static_cast<S*>(s.unit);}
std::array<q15,12> bases(const S&u){std::array<q15,12>a{};for(int p=0;p<12;++p)a[p]=u.params[p].base;return a;}
void down(int x,int y){input::down(x,y);}
void drag(int x,int y){input::move(x,y);}
void lift(){input::up();small_press::cancel();}
void cancel(){editor_cancel_input();param_lens_cancel();small_press::cancel();}
void tap(int x,int y){down(x,y);lift();}
void setup(){tap(188,12);}
bool bi(int p){return p==S::P_PITCH||p==S::P_PITCH_ENV;}
struct Target{int p,x,y,w,h;bool vertical;};
constexpr Target main_targets[]={
    {S::P_POS,8,56,146,81,false},{S::P_SIZE,166,56,146,81,false},
    {S::P_DENS,12,140,72,80,true},{S::P_SPRAY,87,140,72,80,true},
    {S::P_PITCH,162,140,72,80,true},{S::P_MIX,237,140,72,80,true}};
constexpr Target extra_targets[]={
    {S::P_PITCH_ENV,8,66,146,43,false},{S::P_REVERSE,166,66,146,43,false},
    {S::P_SYNC,8,109,146,43,false},{S::P_CLUSTER,166,109,146,43,false}};
void geometry(){
    if(outside_rects||outside_lines||text_overlaps)
        std::fprintf(stderr,"swarm %s width=%d outside=%d/%d overlaps=%d\n",active_case.c_str(),width,outside_rects,outside_lines,text_overlaps);
    CHECK(objects>0&&objects<=8192&&!outside_rects&&!outside_lines&&!text_overlaps);
    for(const auto&t:texts)CHECK(t.x>=0&&t.y>=0&&t.x+t.w<=width&&t.y+t.h<=240);
}
struct Pair{std::vector<u32>top,bottom;};
void memory_oracle(Scene&s,const Pair&pair);
Pair take(Scene&s,const std::string&id,const std::string&variant){
    active_case=id;s.capture();editor_style::capture();editor_gpu::begin();
    const auto clock=s.engine->transport_frame,time=host_time_ms;
    const auto alloc=editor_gpu::inits,uploads=editor_gpu::flushes;
    const int dirty=mutations;const auto ring=ring_process_calls,strain=strain_process_calls,rev=rev_process_calls;
    Pair result;int sum=0;
    for(bool top:{true,false}){
        module_inspector_surface(top);
        const bool glyphs=!top&&!param_lens_is_open();
        f::glyph_mask.fill(false);f::glyph_ink.fill(0);f::record_glyphs=glyphs;
        {f::AudioHidden hidden;if(top)top_draw();else editor_draw();}
        f::record_glyphs=false;geometry();const auto image=pixels;const int count=objects;
        if(glyphs){int checked=0;for(int y=56;y<224;++y)for(int x=0;x<320;++x){const int at=y*320+x;
            if(f::glyph_mask[at]){CHECK(pixels[at]==f::glyph_ink[at]);++checked;}}CHECK(checked>0);++glyph_checks;}
        if(top)result.top=image;else result.bottom=image;sum+=count;
        dump_surface(h::output,id+(top?"_top":"_bottom"));
        h::manifest<<id<<'\t'<<int(UnitKind::Stranular)<<"\tswarm\t"<<variant<<'\t'<<(top?"top":"bottom")
            <<'\t'<<count<<'\t'<<outside_rects<<'\t'<<outside_lines<<'\t'<<text_overlaps<<'\t'<<h::raster_hash(image)<<'\n';
        const auto redraw=[&]{module_inspector_surface(top);{ModuleInspectorNoEngine hidden;if(top)top_draw();else editor_draw();}
            geometry();CHECK(pixels==image&&objects==count);};
        redraw();{f::PoisonAfterCapture poison(s);redraw();}
    }
    CHECK(sum<=8192);h::peak=std::max(h::peak,sum);++h::duals;++frozen;
    CHECK(s.engine->transport_frame==clock&&host_time_ms==time&&mutations==dirty&&editor_gpu::inits==alloc&&editor_gpu::flushes==uploads);
    CHECK(ring_process_calls==ring&&strain_process_calls==strain&&rev_process_calls==rev);
    memory_oracle(s,result);
    h::manifest.flush();std::printf("swarm: %s objects=%d\n",id.c_str(),sum);std::fflush(stdout);return result;
}
void layout_contract(){
    CHECK(l::setup.x==164&&l::setup.y==0&&l::setup.w==48&&l::setup.h==24);
    CHECK(l::freeze.x==108&&l::freeze.y==28&&l::freeze.w==104&&l::freeze.h==28);
    for(int col=0;col<2;++col){CHECK(l::time(col).x==(col?166:8)&&l::time(col).y==56&&l::time(col).w==146&&l::time(col).h==81);
        CHECK(l::time_left(col)==(col?172:14)&&l::time_right(col)==(col?306:148));}
    for(int i=0;i<4;++i)CHECK(l::fader(i).x==12+75*i&&l::fader(i).y==140&&l::fader(i).w==72&&l::fader(i).h==80);
    CHECK(l::time_rail==126&&l::fader_top==164&&l::fader_bottom==198);
}
int expected_delta(int p,int raw,int distance,bool vertical,bool fine=false){
    int next=raw;
    if(bi(p)){
        if(fine)next+=distance*4;
        else if(distance/6){const int product=raw*2400,cents=(product+(product<0?-16383:16383))/32767;
            const int semitone=std::max(-24,std::min(24,(cents+(raw<0?-50:50))/100+distance/6));
            next=(semitone*32767+(semitone<0?-12:12))/24;}
    }else if(p==S::P_POS)next-=distance*32767/296;
    else if(p==S::P_SIZE)next+=distance*32767/296;
    else if(p==S::P_CLUSTER){const int count=std::max(1,std::min(4,int(S::cluster_count(raw))+distance/24));if(distance/24)next=(count-1)*32767/3;}
    else next+=distance*32767/(vertical?168:134);
    return std::max(bi(p)?-32767:0,std::min(32767,next));
}
void control_target(const Target&t,bool extra){
    active_case=std::string(extra?"setup-control-":"main-control-")+std::to_string(t.p);
    for(int raw:{bi(t.p)?-32767:0,12000,32767})for(int corner=0;corner<4;++corner){
        Scene s;auto&u=unit(s);if(extra)setup();s.set(t.p,raw);const auto before=bases(u);const int dirty=mutations;
        // SYNC's upper-right caption is a distinct tap-cycle action; its bottom
        // corners plus the upper-left corner still belong to the drag target.
        if(extra&&t.p==S::P_SYNC&&corner==1)continue;
        const int x=t.x+((corner&1)?t.w-1:0),y=t.y+((corner&2)?t.h-1:0);
        down(x,y);CHECK(stranular_focus_selected()==t.p&&bases(u)==before&&mutations==dirty);
        drag(x,y);CHECK(bases(u)==before&&mutations==dirty);lift();++gestures;
    }
    Scene s;auto&u=unit(s);if(extra)setup();s.set(t.p,bi(t.p)?0:12000);
    const int x=t.x+t.w/2,y=t.y+t.h-9;const int old=u.params[t.p].base,d=t.p==S::P_CLUSTER?24:12;
    down(x,y);drag(x+(t.vertical?0:d),y-(t.vertical?d:0));CHECK(u.params[t.p].base==expected_delta(t.p,old,d,t.vertical));
    const int dirty=mutations;drag(x+(t.vertical?0:d),y-(t.vertical?d:0));CHECK(mutations==dirty);lift();++gestures;
    down(x,y);u.params[t.p].base=2222;drag(x,y);CHECK(u.params[t.p].base==2222);cancel();++gestures;
    down(x,y);const auto before=bases(u);s.select(t.p==S::P_SIZE?S::P_POS:S::P_SIZE);
    drag(x+(t.vertical?0:d),y-(t.vertical?d:0));CHECK(bases(u)==before);cancel();++gestures;
}
void controls(){
    for(const auto&t:main_targets)control_target(t,false);
    for(const auto&t:extra_targets)control_target(t,true);
    // Orthogonal travel does not alter a value. Crossing another target never
    // retargets an already-owned drag; visual 34px is not the sensitivity scale.
    for(const auto&t:main_targets){Scene s;auto&u=unit(s);s.set(t.p,bi(t.p)?0:14000);const auto before=bases(u);
        const int x=t.x+t.w/2,y=t.y+t.h/2;down(x,y);drag(x+(t.vertical?10:0),y+(t.vertical?0:10));CHECK(bases(u)==before);lift();++gestures;}
    {Scene s;auto&u=unit(s);s.set(S::P_DENS,12000);down(48,208);drag(48,174);lift();CHECK(u.params[S::P_DENS].base==12000+34*32767/168);++gestures;}
    {Scene s;auto&u=unit(s);s.set(S::P_PITCH,0);setup();tap(155,205);setup();down(198,184);drag(198,172);lift();CHECK(u.params[S::P_PITCH].base==48);++gestures;
        setup();tap(45,205);setup();down(198,184);drag(198,172);lift();CHECK(u.params[S::P_PITCH].base==expected_delta(S::P_PITCH,48,12,true));++gestures;}
    for(auto point:{std::pair<int,int>{0,180},{319,180},{160,90},{160,138},{80,40},{250,40},{155,155}}){Scene s;const auto before=bases(unit(s));
        tap(point.first,point.second);CHECK(bases(unit(s))==before);++gestures;}
    {Scene s;auto&u=unit(s);s.set(S::P_SYNC,32767);const auto before=bases(u);down(48,180);drag(48,156);lift();CHECK(bases(u)==before);++gestures;}
}
void actions(const std::string&prefix){
    active_case="header and setup actions";
    for(auto point:{std::pair<int,int>{108,28},{211,28},{108,55},{211,55}}){Scene s;auto&u=unit(s);const q15 old=u.params[S::P_FREEZE].base;
        down(point.first,point.second);CHECK(u.params[S::P_FREEZE].base==(old>16384?0:32767));const auto after=bases(u);
        drag(point.first,point.second);lift();CHECK(bases(u)==after);++gestures;}
    for(auto point:{std::pair<int,int>{107,42},{212,42},{160,27},{160,56}}){Scene s;const auto before=bases(unit(s));tap(point.first,point.second);CHECK(bases(unit(s))==before);++gestures;}
    for(auto point:{std::pair<int,int>{224,0},{256,0},{224,21},{256,21}}){Scene s;auto&u=unit(s);tap(point.first,point.second);CHECK(u.bypass);++gestures;}
    for(auto point:{std::pair<int,int>{223,12},{257,12},{240,22},{304,12}}){Scene s;auto&u=unit(s);const auto before=bases(u);tap(point.first,point.second);CHECK(!u.bypass&&bases(u)==before);++gestures;}
    for(auto point:{std::pair<int,int>{164,0},{211,0},{164,23},{211,23}}){Scene s;const auto before=bases(unit(s));tap(point.first,point.second);
        down(50,98);CHECK(stranular_focus_selected()==S::P_PITCH_ENV);lift();CHECK(bases(unit(s))==before);++gestures;}
    {Scene s;auto&u=unit(s);setup();for(int i=0;i<6;++i){tap(160,171);CHECK(u.window_mode==(i+1)%3);++gestures;}
        tap(260,171);CHECK(u.long_mode);tap(260,171);CHECK(!u.long_mode);gestures+=2;
        tap(40,171);CHECK(u.params[S::P_HOLD].base==32767);tap(40,171);CHECK(u.params[S::P_HOLD].base==0);gestures+=2;
        s.set(S::P_PITCH,22000);tap(275,205);CHECK(u.params[S::P_PITCH].base==0);++gestures;
        for(int i=0;i<5;++i){tap(125,121);CHECK(u.params[S::P_SYNC].base==(i==4?0:32767));CHECK(u.sync_division==(i==4?0:i));++gestures;}
        take(s,prefix+"swarm_setup","setup");}
}
void cancellation(){
    active_case="gesture cancellation";
    for(int why=0;why<7;++why){Scene s;auto&u=unit(s);s.set(S::P_MIX,12000);down(273,184);const auto before=bases(u);
        switch(why){
            case 0:g_overlay=Overlay::None;s.capture();g_overlay=Overlay::Editor;break;
            case 1:g_edit_slot=0;s.capture();g_edit_slot=s.slot;break;
            case 2:g_edit_chain=1;s.capture();g_edit_chain=0;break;
            case 3:g_view=View::Sequencer;s.capture();g_view=View::Chains;break;
            case 4:s.engine->request_pattern(7,false);break;
            case 5:{u32 key=KEY_L;CHECK(param_lens_input(key,KEY_L|KEY_TOUCH,0,false));CHECK(param_lens_is_open());break;}
            case 6:cancel();break;
        }
        // Direct continuation is deliberate: stale gestures must reject even
        // if callers omit a GPU frame or the main dispatcher discards a touch.
        stranular_focus_drag(u,273,160);CHECK(bases(u)==before);cancel();++gestures;
    }
    {Scene s;auto&u=unit(s);down(273,184);const auto before=bases(u);auto alternate=std::make_unique<Engine>();
        g_eng=alternate.get();stranular_focus_drag(u,273,160);g_eng=s.engine.get();lift();CHECK(bases(u)==before);++gestures;}
    {Scene s;down(273,184);auto*replacement=new S;CHECK(replacement->ready());delete s.engine->replace_unit(0,s.slot,replacement);s.unit=replacement;
        const auto before=bases(*replacement);stranular_focus_drag(*replacement,273,160);lift();CHECK(bases(*replacement)==before);++gestures;}
}
void edit_bank(Scene&s,int bank){
    cancel();g_seq_lock_armed=false;g_overlay=Overlay::None;g_view=View::Sequencer;
    s.engine->request_pattern(bank,false);sequencer_forget_pins();sequencer_view_enter();s.capture();
    CHECK(sequencer_edit_bank()==bank);s.engine->request_pattern((bank+3)%NUM_PATTERNS,false);
    CHECK(sequencer_edit_bank()==bank&&s.engine->active_pattern==(bank+3)%NUM_PATTERNS);
    g_overlay=Overlay::Editor;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=5;s.capture();
}
int lock_value(Scene&s,int bank,int step,int p){const auto&seq=s.engine->sequence_pattern(bank).steps[0][step];
    for(int i=0;i<seq.lock_count;++i)if(seq.locks[i].slot==s.slot&&seq.locks[i].param==p)return seq.locks[i].value;return -40000;}
void locks(const std::string&prefix){
    active_case="all eight edit banks";
    for(int bank=0;bank<NUM_PATTERNS;++bank){Scene s;auto&u=unit(s);edit_bank(s,bank);bank_mask|=1u<<bank;
        auto&step=s.engine->sequence_pattern(bank).steps[0][5];const auto before=bases(u);
        for(const auto&t:main_targets){const int raw=bi(t.p)?0:12000;step.lock_count=1;step.locks[0]={uint8_t(s.slot),uint8_t(t.p),q15(raw)};
            const int x=t.x+t.w/2,y=t.y+t.h/2;down(x,y);drag(x+(t.vertical?0:12),y-(t.vertical?12:0));lift();
            CHECK(lock_value(s,bank,5,t.p)==expected_delta(t.p,raw,12,t.vertical)&&bases(u)==before);
            CHECK(s.engine->pattern.steps[0][5].lock_count==0);++bank_checks;++gestures;}
        step.lock_count=1;step.locks[0]={uint8_t(s.slot),uint8_t(S::P_MIX),12000};
        down(273,184);s.engine->request_pattern((bank+4)%NUM_PATTERNS,false);drag(273,172);lift();
        CHECK(lock_value(s,bank,5,S::P_MIX)==12000+12*32767/168&&bases(u)==before);++bank_checks;++gestures;
        step.lock_count=0;tap(160,42);CHECK(lock_value(s,bank,5,S::P_FREEZE)==32767&&bases(u)==before);++gestures;
        if(bank==2)take(s,prefix+"swarm_lock","lock");
        setup();const int shape=u.window_mode,division=u.sync_division;const bool longer=u.long_mode,bypass=u.bypass;
        tap(160,171);tap(260,171);tap(125,121);tap(240,10);
        CHECK(u.window_mode==shape&&u.long_mode==longer&&u.sync_division==division&&u.bypass==bypass&&bases(u)==before);gestures+=4;
        for(const auto&t:extra_targets){const int raw=bi(t.p)?0:t.p==S::P_CLUSTER?0:12000;
            step.lock_count=1;step.locks[0]={uint8_t(s.slot),uint8_t(t.p),q15(raw)};const int x=t.x+50,y=t.y+t.h-9,d=t.p==S::P_CLUSTER?24:12;
            down(x,y);drag(x+d,y);lift();CHECK(lock_value(s,bank,5,t.p)==expected_delta(t.p,raw,d,false)&&bases(u)==before);++bank_checks;++gestures;}
        step.lock_count=0;tap(40,171);CHECK(lock_value(s,bank,5,S::P_HOLD)==32767&&bases(u)==before);++gestures;
        step.lock_count=1;step.locks[0]={uint8_t(s.slot),uint8_t(S::P_PITCH),12000};tap(275,205);
        CHECK(lock_value(s,bank,5,S::P_PITCH)==0&&bases(u)==before);++gestures;setup();
        step.lock_count=SEQ_LOCKS;for(int i=0;i<SEQ_LOCKS;++i)step.locks[i]={0,uint8_t(i),1234};
        down(273,184);drag(273,160);lift();CHECK(g_seq_lock_flash>0&&lock_value(s,bank,5,S::P_MIX)==-40000&&bases(u)==before);++gestures;
        if(bank==2)take(s,prefix+"swarm_lock_full","lock-full");
        step.lock_count=0;down(273,184);g_seq_lock_step=6;drag(273,160);lift();
        CHECK(lock_value(s,bank,6,S::P_MIX)==-40000&&step.lock_count==0&&bases(u)==before);++gestures;
        g_seq_lock_step=5;down(273,184);g_seq_lock_chain=1;stranular_focus_drag(u,273,160);lift();CHECK(step.lock_count==0&&bases(u)==before);++gestures;
        g_seq_lock_chain=0;down(273,184);
        const int next_bank=(bank+1)%NUM_PATTERNS;s.engine->request_pattern(next_bank,false);sequencer_forget_pins();sequencer_capture_inspector();
        CHECK(sequencer_edit_bank()==next_bank);stranular_focus_drag(u,273,160);lift();
        CHECK(lock_value(s,next_bank,5,S::P_MIX)==-40000&&step.lock_count==0&&bases(u)==before);++gestures;
        g_seq_lock_armed=false;g_view=View::Chains;cancel();
    }
}
// Independent pixel oracle for the confirmed lancet (not the retired needle).
// Expected geometry is declarative and never calls the production helpers.
void needle(const std::vector<u32>&actual,const std::vector<u32>&paper,bool vertical,int x,int y,int length,int edit,int live,bool linked,bool quiet,bool disabled,int neutral=-1){
    auto expected=paper;const bool show=linked||edit!=live;
    const auto rail=palette::subdued(),accent=quiet||disabled?palette::menu_ghost():palette::spot();
    const auto ink=disabled?palette::menu_ghost():palette::ink();
    const auto point=[&](int u,int v,u32 color){const int px=vertical?x+v:x+u,py=vertical?y-u:y+v;CHECK(px>=0&&px<320&&py>=0&&py<240);expected[py*320+px]=color;};
    const auto half=[](int v,bool small){const int full[]={3,2,2,1,1,0},compact[]={2,1,1,0};return small?compact[std::abs(v)]:full[std::abs(v)];};
    const auto axis=[&](int a,int b,u32 color){for(int u=std::min(a,b);u<=std::max(a,b);++u)if(std::abs(u-edit)>2)point(u,0,color);};
    const auto tick=[&](int u,int v){if(std::abs(u-edit)>half(v,false))point(u,v,rail);};
    axis(0,length,rail);
    for(int side:{-1,1})for(int v=-2;v<=2;++v)tick((side<0?0:length)+side*(2-std::abs(v)),v);
    if(neutral>=0)for(int v=-3;v<=3;++v)tick(neutral,v);
    if(show&&edit!=live)axis(edit,live,accent);
    for(int v=-5;v<=5;++v){point(edit-half(v,false),v,ink);point(edit+half(v,false),v,ink);}
    if(show){if(std::abs(live-edit)<=4){for(int v=-2;v<=2;++v)point(live,v,accent);}
        else for(int v=-3;v<=3;++v)for(int u=-half(v,true);u<=half(v,true);++u)point(live+u,v,accent);}
    const int x0=vertical?x-5:x-3,x1=vertical?x+5:x+length+3;
    const int y0=vertical?y-length-3:y-5,y1=vertical?y+3:y+5;
    for(int yy=y0;yy<=y1;++yy)for(int xx=x0;xx<=x1;++xx)if(actual[yy*320+xx]!=expected[yy*320+xx]){
        std::fprintf(stderr,"swarm needle %s vertical=%d xy=%d,%d length=%d edit=%d live=%d mismatch=%d,%d\n",active_case.c_str(),vertical,x,y,length,edit,live,xx,yy);CHECK(false);}
    ++needle_checks;
}
void memory_oracle(Scene&s,const Pair&pair){
    module_inspector_surface(true);{ModuleInspectorNoEngine hidden;editor_style::paper(400);}auto expected=pixels;
    const auto&u=unit(s);std::array<int,40> probes{};bool nonzero=false;
    for(int i=0;i<40;++i){probes[i]=std::abs(int(u.visual_wave()[i]));nonzero=nonzero||probes[i]!=0;}
    const bool quiet=u.bypass||s.engine->chains[0].mute||!u.ready()||!nonzero;
    const unsigned span=u.scan_span_frames();
    const auto positive=[](q15 raw){return std::max(0,int(raw));};
    const unsigned duration=128+uint64_t(positive(u.params[S::P_SIZE].effective))*(u.long_mode?32640:8064)/32767;
    const unsigned maximum=span>duration+4?span-duration-4:0;
    const unsigned lookback=span?std::min(unsigned((uint64_t(positive(u.params[S::P_POS].effective))*maximum)>>15)+(u.visual_frozen()?0:64),span-1):0;
    const int nominal=386-uint64_t(lookback)*372/std::max(1u,span);
    const int radius=uint64_t(positive(u.params[S::P_SPRAY].effective))*8192*372/(32767ull*std::max(1u,span));
    int dots=0;
    for(int col=0;col<125;++col){const int offset=col*39,index=offset/124,weight=offset%124;
        const int magnitude=(probes[index]*(124-weight)+probes[std::min(index+1,39)]*weight)/124;
        const int height=span?magnitude*54/32768:0,x=14+col*3;
        const auto color=!quiet&&std::abs(x-nominal)<=radius?palette::spot():editor_style::branch();
        // Equal paired offsets keep even the smallest nonzero magnitude honest:
        // always one center dot and no one-sided four-pixel-grid phase shift.
        expected[132*400+x]=color;++dots;
        for(int dy=4;dy<=height;dy+=4){expected[(132-dy)*400+x]=expected[(132+dy)*400+x]=color;dots+=2;}
    }
    if(!quiet&&span&&u.visual_mix()>0)for(int head=0;head<8;++head)if(u.visual_grains()&(1u<<head)){
        const int x=14+std::min(39,int(u.visual_positions()[head]))*372/39;
        for(int y=127;y<=137;++y)expected[y*400+x]=palette::ink();
    }
    CHECK(dots>=125&&dots<=3500);peak_dots=std::max(peak_dots,dots);
    for(int y=76;y<=188;++y)for(int x=14;x<=386;++x)if(pair.top[y*400+x]!=expected[y*400+x]){
        std::fprintf(stderr,"swarm actual40-probe stipple %s mismatch=%d,%d\n",active_case.c_str(),x,y);CHECK(false);}
    ++wave_checks;
}
void rails(Scene&s,const Pair&pair){
    module_inspector_surface(false);{ModuleInspectorNoEngine hidden;editor_style::paper(320);}const auto paper=pixels;auto&u=unit(s);
    for(int n=0;n<6;++n){const int p=main_params[n],lo=bi(p)?-32767:0,len=n<2?134:34;
        const auto map=[&](int raw){raw=std::max(lo,std::min(32767,raw));const int v=(raw-lo)*len/(32767-lo);return p==S::P_POS?len-v:v;};
        const int edit=map(u.params[p].base),live=map(p==S::P_MIX?u.visual_mix():u.params[p].effective);
        const bool linked=u.params[p].src!=ModSource::None&&u.params[p].mod_enabled,quiet=u.bypass||s.engine->chains[0].mute;
        needle(pair.bottom,paper,n>=2,n<2?(n?172:14):48+75*(n-2),n<2?126:198,len,edit,live,linked,quiet,p==S::P_DENS&&u.params[S::P_SYNC].effective>16384,bi(p)?map(0):-1);
    }
}
void lens(const std::string&prefix){
    for(int p=0;p<12;++p){Scene s;auto&u=unit(s);s.select(p);s.set(p,bi(p)?0:12000);s.capture();const auto before=bases(u);
        u32 key=KEY_L;CHECK(param_lens_input(key,KEY_L,0,false)&&param_lens_is_open());param_lens_touch_release();
        CHECK(stranular_focus_selected()==p&&bases(u)==before);lens_mask|=1u<<p;
        down(140,96);drag(146,96);lift();CHECK(u.params[p].base==before[p]+6*(bi(p)?65534:32767)/271);++gestures;
        tap(54,185);CHECK(u.params[p].src==ModSource::SigilA);const int depth=u.params[p].mod_depth;
        down(140,129);drag(146,129);lift();CHECK(u.params[p].mod_depth==std::min(32767,depth+6*65534/271));gestures+=2;
        take(s,prefix+"swarm_lens_"+std::to_string(p),"lens");
        for(int q=0;q<12;++q)if(q!=p)CHECK(u.params[q].base==before[q]);
        const auto after=bases(u);CHECK(!stranular_focus_touch_body(u,160,42));stranular_focus_drag(u,273,160);CHECK(bases(u)==after);
        param_lens_cancel();++gestures;
    }
    // Current persistent footer entry, then actual keyboard navigation. Retired
    // touch arrows are intentionally not controls in the shared minimal lens.
    Scene s;s.select(0);CHECK(param_lens_touch_down(280,232));param_lens_touch_release();
    for(int n=0;n<12;++n){CHECK(stranular_focus_selected()==n);u32 key=KEY_RIGHT;CHECK(param_lens_input(key,KEY_RIGHT,0,false));}
    CHECK(stranular_focus_selected()==0);param_lens_cancel();gestures+=12;
}
void signal(S&u,int mode,int frames){
    uint32_t random=0x9415aca3;int at=0;
    while(frames){q15 buf[256];const int n=std::min(frames,256);for(int i=0;i<n;++i,++at){random=random*1664525u+1013904223u;
        buf[i]=mode==0?0:mode==1?32767:mode==2?-32768:q15((int(random>>16)-32768)*3/4);}u.process(buf,n);frames-=n;}
}
void states(const std::string&prefix){
    for(const std::string v:{"default","waiting","silence","maximum","negative","bypass","mute","frozen","hold","long6","holes","idle_first","idle_later","minimum","sync"}){
        Scene s;auto&u=unit(s);
        if(v=="waiting")u.reset();
        if(v=="silence"||v=="maximum"||v=="negative"){u.reset();signal(u,v=="silence"?0:v=="maximum"?1:2,32768);}
        if(v=="bypass")u.bypass=true;if(v=="mute")s.engine->chains[0].mute=true;
        if(v=="frozen"){s.set(S::P_MIX,7000);signal(u,3,8192);s.set(S::P_FREEZE,32767);signal(u,0,1024);CHECK(u.visual_frozen()&&u.visual_mix()==32767&&u.params[S::P_MIX].base==7000);}
        if(v=="hold"){s.set(S::P_HOLD,32767);signal(u,0,32768);CHECK(u.visual_grains()==0);}
        if(v=="long6"||v=="holes")s.path(v=="holes");
        if(v=="idle_later"){host_time_ms+=5000;g_ui_frame+=137;}
        if(v=="minimum")for(int p:main_params)s.set(p,bi(p)?-32767:0);
        if(v=="sync")s.set(S::P_SYNC,32767);
        const auto pair=take(s,prefix+"swarm_"+v,v);rails(s,pair);
    }
    for(int value:{0,16384,32767}){Scene s;auto&u=unit(s);for(int p:main_params){auto&q=u.params[p];q.base=bi(p)?q15(value*2-32767):q15(value);
        q.src=ModSource::SigilA;q.mod_enabled=true;q.effective=q.target=bi(p)?q15(32767-value*2):q15(32767-value);}
        // MIX's live marker is captured from actual visual_mix(), not the raw Param.
        const auto pair=take(s,prefix+"swarm_rail_"+std::to_string(value),"rail-edge");rails(s,pair);}
}
uint64_t wave_hash(const S&u){uint64_t hash=1469598103934665603ull;for(int i=0;i<S::VIZ_POINTS;++i){const uint16_t raw=uint16_t(u.visual_wave()[i]);for(int byte=0;byte<2;++byte){hash^=(raw>>(8*byte))&255;hash*=1099511628211ull;}}return hash;}
void live_motion(const std::string&prefix){
    Scene s;auto&u=unit(s);u.reset();s.set(S::P_SIZE,12000);s.set(S::P_DENS,23000);s.set(S::P_MIX,16000);
    for(int n=0;n<20;++n){if(n==11)s.set(S::P_FREEZE,32767);if(n==16)s.set(S::P_FREEZE,0);
        signal(u,n<2||n>=16?0:3,2560);const std::string id=prefix+"swarm_motion_"+(n<10?"0":"")+std::to_string(n);
        const auto pair=take(s,id,"actual-core-input");rails(s,pair);
        motion<<prefix<<'\t'<<n<<'\t'<<id<<'\t'<<wave_hash(u)<<'\t'<<u.scan_span_frames()<<'\t'<<u.visual_grains()<<'\t'<<u.visual_frozen()<<'\t'<<u.visual_mix();
        for(int i=0;i<S::VIZ_POINTS;++i)motion<<'\t'<<u.visual_wave()[i];motion<<'\n';++motion_frames;
    }
}
int run(const std::filesystem::path&dir){
    CHECK(!std::filesystem::exists(dir));std::filesystem::create_directories(dir);h::output=dir;h::capture_files=true;
    h::manifest.open(dir/"surfaces.tsv");CHECK(h::manifest.good());h::manifest<<"id\tkind\tmodule\tvariant\tsurface\tobjects\toutside_rects\toutside_lines\ttext_overlaps\traster_fnv64\n";
    motion.open(dir/"motion.tsv");motion<<"theme\tframe\tid\twave_fnv64\tspan\tgrains\tfreeze\tlive_mix";for(int i=0;i<S::VIZ_POINTS;++i)motion<<"\twave"<<i;motion<<'\n';
    f::sample_fixture();CHECK(editor_style::init());layout_contract();
    for(auto theme:{palette::Theme::Graphite,palette::Theme::Pink}){palette::set(theme);const std::string prefix=theme==palette::Theme::Graphite?"graphite_":"pink_";
        controls();actions(prefix);cancellation();locks(prefix);lens(prefix);states(prefix);live_motion(prefix);}
    CHECK(bank_mask==255&&lens_mask==4095);editor_style::shutdown();CHECK(editor_gpu::textures.empty());h::manifest.close();motion.close();
    std::ofstream out(dir/"behavior.json");out<<"{\"status\":\"passed\",\"gestures\":"<<gestures<<",\"frozen_pairs\":"<<frozen
        <<",\"motion_frames\":"<<motion_frames<<",\"needle_checks\":"<<needle_checks<<",\"glyph_checks\":"<<glyph_checks
        <<",\"bank_parameter_checks\":"<<bank_checks<<",\"raw_probe_raster_checks\":"<<wave_checks<<",\"peak_stipple_dots\":"<<peak_dots<<",\"edit_banks_tested\":8,\"lens_params_tested\":12,\"duals\":"<<h::duals
        <<",\"peak_objects\":"<<h::peak<<",\"old_suites_run\":0}\n";
    std::printf("swarm complete: %d gestures; %d frozen pairs; %d motion frames; %d needle oracles; %d glyph poses; %d bank edits; peak %d/8192 host objects; no inherited suites/device/FPS/audio claim\n",
        gestures,frozen,motion_frames,needle_checks,glyph_checks,bank_checks,h::peak);return 0;
}
}
int main(int argc,char**argv){try{CHECK(argc==3);return swarm_minimal_cases::run(argv[2]);}
catch(const std::exception&e){fader_lancet_cases::record_glyphs=false;std::fprintf(stderr,"swarm minimal failed [%s]: %s\n",swarm_minimal_cases::active_case.c_str(),e.what());return 1;}}
