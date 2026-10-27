// Included after step_focus_ui_test.h inside sequencer_test.cpp's anonymous namespace.
// Native controller/raster proof. Synthetic snapshot tests and real engine tests are separate.
namespace playhead_press_cases {
using step_focus_cases::Clock;
using step_focus_cases::select;
using step_focus_cases::settle;
using Image = std::vector<u32>;
using Inject = std::function<void(EngineUiSnapshot&)>;
uint64_t checked_pixels=0, glyph_pixels=0, draw_frames=0, changed_pixels=0, real_hits=0;
Image surface(const Inject& inject={}, bool top=false) {
    if(g_view==View::Chains&&g_overlay==Overlay::Editor){CHECK(!inject);++draw_frames;return step_focus_cases::surface(top);}
    width=top?400:320;objects=outside_rects=outside_lines=text_overlaps=0;texts.clear();
    pixels.assign(width*HEIGHT,0xff000000u);CHECK(g_view==View::Sequencer);
    sequencer_capture_inspector();
    if(inject)inject(const_cast<EngineUiSnapshot&>(sequencer_snapshot())); // test only, never engine state
    if(top)top_draw();else sequencer_view_draw();
    CHECK(outside_rects==0&&outside_lines==0&&objects>0&&objects<8192);
    for(const auto& t:texts)CHECK(t.x>=0&&t.y>=0&&t.x+t.w<=width&&t.y+t.h<=240);
    if(top)maximum_top_objects=std::max(maximum_top_objects,objects);
    else maximum_objects=std::max(maximum_objects,objects);
    ++draw_frames;return pixels;
}
Inject hit(int age,int duration=140,int step=5,int bank=0,int chain=2) {
    return [=](EngineUiSnapshot& s){s.transport_playing=true;s.sequencer_enabled=true;
        for(auto& h:s.sequence_hit){h={};}s.sequence_hit[chain]={(uint16_t)age,(uint8_t)step,(uint8_t)bank,(uint16_t)duration};};
}
Inject quiet(){return [](EngineUiSnapshot& s){for(auto& h:s.sequence_hit)h={};};}
// Decimal envelope law independently evaluated in double precision; native color
// conversion is rounded per channel, not an approximate RGB-distance test.
double playback(int age,int duration=140){if(age<0||age>=duration||duration<=0)return 0;
    const double attack=std::min(30.,duration*.3);if(age<attack)return age/attack;
    const double left=(duration-age)/(duration-attack);return left*left;}
double press(int age){if(age<0||age>=110)return 0;const double left=(110-age)/110.;return left*left;}
u32 tint(u32 base,u32 accent,double power){u32 color=base&0xff000000u;
    for(unsigned shift=0;shift<24;shift+=8){const int a=(base>>shift)&255,b=(accent>>shift)&255;
        color|=u32(std::floor(a+(b-a)*power*.28+.5))<<shift;}return color;}
bool covered(int x,int y,int inset=0){const int size=18-2*inset,xx=x-inset,yy=y-inset;
    if(yy<0||yy>=size)return false;
    const int edge=(yy==0||yy==size-1)?2:(yy==1||yy==size-2)?1:0;
    return xx>=edge&&xx<size-edge;}
void oracle(const Image& actual,const Image& rest,int chain,int step,bool selected,double hp,double ep,bool full=true,bool idle_backing=true,const Image* native_underlay=nullptr) {
    CHECK(actual.size()==76800&&rest.size()==actual.size());const int x0=8+(step%16)*19,y0=layout::ROWS[chain];
    const int inset=(hp>=.16||ep>=.16)?1:0;
    // The selected face starts from the approved spot plate; ordinary backing
    // keeps its old cell color. Inverse native glyphs keep the unshrunk mask.
    const u32 base=selected?m::spot():col_cell(),face=tint(tint(base,palette::playhead(),hp),m::ink(),ep*.35);
    for(int y=0;y<240;++y)for(int x=0;x<320;++x){if(!full&&(x<x0||x>=x0+18||y<y0||y>=y0+18))continue;
        const int i=y*320+x,xx=x-x0,yy=y-y0;u32 expected=rest[i];
        if(xx>=0&&xx<18&&yy>=0&&yy<18&&covered(xx,yy)){
            const bool glyph=selected?rest[i]==col_text_i():rest[i]!=(idle_backing?col_cell():m::paper_color(y));
            if(glyph){++glyph_pixels;}
            else if(selected||idle_backing||hp>0||ep>0)expected=covered(xx,yy,inset)?face:(native_underlay?(*native_underlay)[i]:m::paper_color(y));
        }
        if(actual[i]!=expected)std::fprintf(stderr,"feedback oracle layer=%d selected=%d chain=%d step=%d xy=%d,%d hp=%.9f ep=%.9f base=%08x got=%08x want=%08x\n",sequencer_selected_layer(),selected,chain,step,x,y,hp,ep,rest[i],actual[i],expected);
        CHECK(actual[i]==expected);++checked_pixels;changed_pixels+=actual[i]!=rest[i];
    }
}
void prepare(slice_cases::Setup& f,int layer,bool selected=true){f.dense();for(int s=0;s<16;++s)f.stored(s+1,s);
    f.mode(layer);select(f,selected?2:3,selected?5:15);settle();}
void test_playback_oracle(){Clock clock;
    const int ages[]={0,1,4,5,10,20,29,30,31,50,60,80,90,100,109,110,120,139,140,141,65535};
    for(int layer=0;layer<8;++layer)for(bool selected:{false,true}){
        slice_cases::Setup f(16);prepare(f,layer,selected);const auto data=f.hash(),pcm=f.sample_hash();const int dirty=mutations;
        const auto rest=surface(hit(65535)),top=surface({},true);
        for(int duration:{16,42,85,140})for(int age:ages){const auto image=surface(hit(age,duration));
            oracle(image,rest,2,5,selected,playback(age,duration),0);
            CHECK(surface(hit(age,duration))==image);CHECK(f.hash()==data&&f.sample_hash()==pcm&&mutations==dirty);}
        const int bottom=objects;CHECK(surface({},true)==top);maximum_shared_objects=std::max(maximum_shared_objects,bottom+objects);CHECK(bottom+objects<8192);
    }
}
void test_identity_and_gates(){Clock clock;slice_cases::Setup f(16);prepare(f,1,false);
    const auto rest=surface(hit(65535));
    for(const auto& inject:{hit(30,140,5,1),hit(30,140,21,0),hit(30,140,255,0),hit(30,0)})CHECK(surface(inject)==rest);
    CHECK(surface(hit(30))!=rest);
    for(int gate=0;gate<4;++gate){Inject off=[=](EngineUiSnapshot& s){hit(30)(s);
        if(gate==0)s.transport_playing=false;
        if(gate==1)s.sequencer_enabled=false;
        if(gate==2)s.sequence_length[2]=5;
        if(gate==3)s.sequence_lifecycle[2]=(uint8_t)SequenceLifecycle::Flow;};
        Inject no=[=](EngineUiSnapshot& s){off(s);s.sequence_hit[2]={};};CHECK(surface(off)==surface(no));}
    // A visually moving old underbar is explicitly NOT an event detector.
    for(int step=0;step<16;++step){Inject move=[=](EngineUiSnapshot& s){hit(65535)(s);s.sequence_step[2]=step;};
        const auto image=surface(move);
        for(int chain=0;chain<4;++chain)for(int y=layout::ROWS[chain];y<layout::ROWS[chain]+layout::CELL_H;++y)for(int x=0;x<320;++x){
            CHECK(image[y*320+x]==rest[y*320+x]);++checked_pixels;}}
    for(int page=0;page<4;++page){if(page)f.tap(KEY_L|KEY_RIGHT);select(f,3,15);settle();
        auto event=hit(30,140,page*16+5);const auto image=surface(event),base=surface(hit(65535));
        oracle(image,base,2,page*16+5,false,1,0);}
    f.mode(0);static_cast<FmUnit*>(f.engine->chains[0].units[0])->drone=true;f.refresh();settle();
    CHECK(surface(hit(30,140,48+5,0,0))==surface(hit(65535))); // real continuous source, even deliberately corrupt test telemetry
    f.engine->sequence_pattern(2)=f.engine->pattern;extent_bank(f,2);select(f,3,15);settle();
    const int source_step=sequencer_current_page()*16+5;const auto bank_rest=surface(hit(65535));
    CHECK(surface(hit(30,140,source_step,0))==bank_rest);
    oracle(surface(hit(30,140,source_step,2)),bank_rest,2,source_step,false,1,0);
}
void test_empty_lock_and_flow(){Clock clock;
    // A previous accepted hit can outlive a subsequent edit. Clear real source
    // data, keep only injected published telemetry, and test ephemeral backing
    // without manufacturing a permanent filled cell or moving native dots.
    for(int layer=0;layer<8;++layer)for(bool selected:{false,true})for(bool locked:{false,true}){
        slice_cases::Setup f(16);prepare(f,layer,selected);f.engine->pattern.steps[2][5]={};f.engine->pattern.microtiming[2][5]=0;
        if(locked)CHECK(f.engine->set_step_lock(2,5,0,PlayUnit::P_SLICE,slice_cases::center(3)));
        f.refresh();settle();const auto rest=surface(hit(65535));
        for(int age:{0,1,30,60,90,140})oracle(surface(hit(age)),rest,2,5,selected,playback(age),0,true,locked);
    }
    // Flow has no trigger response and its 18x10 point-local plate stays static
    // during real edits. NOTE/LEVEL ordinary badges retain their press envelope.
    for(int layer:{0,1,2}){Fixture f;f.dense();static_cast<FmUnit*>(f.engine->chains[0].units[0])->drone=true;f.refresh();
        const auto original=f.hash();
        f.mode(layer);select(f,0,5);settle();f.tap(KEY_L|KEY_DOWN);CHECK(f.hash()!=original);
        const auto begin=step_focus_test_ms;const auto first=surface();
        step_focus_test_ms=begin+35;const auto mid=surface();step_focus_test_ms=begin+110;const auto rest=surface();
        if(layer==0) {
            // The current real value changes immediately, but plate geometry,
            // inverse curve and point do not pulse/squash at any edit age.
            CHECK(first==mid&&mid==rest);g_overlay=Overlay::Picker;const auto native=surface();g_overlay=Overlay::None;
            step_focus_cases::flow_oracle(first,native,0,5);
        } else {
            oracle(first,rest,0,5,true,0,1);oracle(mid,rest,0,5,true,0,press(35));CHECK(first!=rest);
        }
        CHECK(surface(hit(30,140,5,0,0))==surface(hit(65535,140,5,0,0)));
    }
}
void adjust(Fixture& f,int layer){f.tap(KEY_L|((layer==0||layer==2)?KEY_DOWN:KEY_UP));}
void test_edit_oracle(){Clock clock;
    for(int layer=0;layer<8;++layer){slice_cases::Setup f(16);prepare(f,layer);const auto original=f.hash();adjust(f,layer);
        CHECK(f.hash()!=original);CHECK(sequencer_selected_chain()==2&&sequencer_selected_step()==5);
        const auto edited=f.hash(),pcm=f.sample_hash();const auto begin=step_focus_test_ms;const auto top=surface({},true);
        const int times[]={0,1,10,25,35,50,65,67,80,100,109,110,111};std::vector<Image> frames;
        for(int age:times){step_focus_test_ms=begin+age;frames.push_back(surface());CHECK(surface()==frames.back());CHECK(surface({},true)==top);}
        const auto rest=frames.back();for(size_t i=0;i<std::size(times);++i)oracle(frames[i],rest,2,5,true,0,press(times[i]));
        CHECK(frames.front()!=rest);CHECK(f.hash()==edited&&f.sample_hash()==pcm);
    }
}
void test_noop_during_press(){Clock clock;
    // Editor repeat at the same exact position for every value layer. This is a
    // real touch, not a mocked setter; a no-op must neither restart nor cancel.
    for(int layer=1;layer<8;++layer){slice_cases::Setup f(16);prepare(f,layer);int x=layer==7?220:190;
        f.control(x);const auto begin=step_focus_test_ms;const auto original=f.hash();surface();
        step_focus_test_ms=begin+35;const auto before=surface();f.control(x);CHECK(f.hash()==original);CHECK(surface()==before);
        const auto mid=surface();step_focus_test_ms=begin+80;const auto late=surface();step_focus_test_ms=begin+110;const auto rest=surface();
        oracle(mid,rest,2,5,true,0,press(35));oracle(late,rest,2,5,true,0,press(80));CHECK(mid!=rest);
    }
    // Real keyboard clamps: reach each endpoint, then repeat during its pulse.
    for(int layer:{0,1,2,3,5,6,7}){slice_cases::Setup f(16);prepare(f,layer);
        // Begin below the upper endpoint even when the dense fixture is already
        // saturated (notably LEVEL=32767). Then the last changed UP owns a pulse.
        f.tap(KEY_L|KEY_DOWN);
        bool saturated=false;for(int n=0;n<300;++n){const auto old=f.hash();f.tap(KEY_L|KEY_UP);if(f.hash()==old){saturated=true;break;}}
        CHECK(saturated);const auto begin=step_focus_test_ms;surface();step_focus_test_ms+=35;const auto before=surface();
        const auto old=f.hash();f.tap(KEY_L|KEY_UP);CHECK(f.hash()==old);CHECK(surface()==before);
        step_focus_test_ms=begin+110;const auto rest=surface();oracle(before,rest,2,5,true,0,press(35));CHECK(before!=rest);
    }
}
void test_copy_clear_remove_paint(){Clock clock;
    {slice_cases::Setup f(16);prepare(f,1);const auto copied=f.hash();const auto image=surface();f.tap(KEY_L|KEY_X);CHECK(f.hash()==copied&&surface()==image);
        select(f,2,6);settle();f.tap(KEY_L|KEY_A);const auto begin=step_focus_test_ms,edited=f.hash();const auto first=surface();
        CHECK(std::memcmp(&f.engine->pattern.steps[2][5],&f.engine->pattern.steps[2][6],sizeof(SeqStep))==0);
        step_focus_test_ms=begin+35;const auto mid=surface();f.tap(KEY_L|KEY_A);CHECK(f.hash()==edited&&surface()==mid);
        step_focus_test_ms=begin+110;const auto rest=surface();oracle(first,rest,2,6,true,0,1);oracle(mid,rest,2,6,true,0,press(35));
        f.tap(KEY_X);CHECK(f.engine->pattern.steps[2][6].flags==0&&f.engine->pattern.steps[2][6].lock_count==0&&f.engine->pattern.microtiming[2][6]==0);
        const auto cleared=surface();const auto start=step_focus_test_ms;step_focus_test_ms+=35;const auto active=surface();f.tap(KEY_X);CHECK(surface()==active);
        step_focus_test_ms=start+110;oracle(cleared,surface(),2,6,true,0,1);
    }
    auto remove=[](Fixture& f){f.frame(KEY_L,KEY_L);surface();
        sequencer_view_touch_down(280,200);f.release();f.frame(0,0,KEY_L);};
    for(int layer:{6,7}){slice_cases::Setup f(16);prepare(f,layer);
        if(layer==6){surface();const auto& d=sequencer_inspector();CHECK(d.slot>=0&&d.rows[d.param].focused_locked);}
        settle();const auto old=f.hash();remove(f);CHECK(f.hash()!=old);const auto begin=step_focus_test_ms;const auto first=surface();
        step_focus_test_ms=begin+110;oracle(first,surface(),2,5,true,0,1);
        if(layer==7){const auto rest=surface();remove(f);CHECK(surface()==rest);}
    }
    {Fixture f;f.mode(0);select(f,0,5);settle();f.down(0,0);f.hold(0,5);CHECK(f.trig(0,0)&&f.trig(0,5));
        for(int s=0;s<=5;++s){CHECK(f.trig(0,s));}const auto begin=step_focus_test_ms;const auto first=surface();
        step_focus_test_ms+=35;const auto mid=surface();f.hold(0,5);CHECK(surface()==mid);f.release();
        step_focus_test_ms=begin+110;const auto rest=surface(); // only last actually changed painted cell owns the pulse
        oracle(first,rest,0,5,true,0,1,false);oracle(mid,rest,0,5,true,0,press(35),false);
    }
}
void test_context_and_focus(){Clock clock;
    for(int action=0;action<9;++action){slice_cases::Setup f(16);prepare(f,action==6?7:1);adjust(f,sequencer_selected_layer());
        const auto first=surface();step_focus_test_ms+=25;CHECK(surface()!=first);
        if(action==0){select(f,2,15);f.tap(KEY_RIGHT);CHECK(sequencer_current_page()==1);}
        if(action==1)f.mode(3);
        if(action==2)sequencer_view_cancel_input();
        if(action==3){g_view=View::Chains;g_overlay=Overlay::Picker;sequencer_capture_inspector();g_view=View::Sequencer;g_overlay=Overlay::None;sequencer_view_enter();}
        if(action==4){f.r_hold();surface();f.frame(0,0,KEY_R);}
        if(action==5)extent_bank(f,2);
        if(action==6){f.control(135,200);CHECK(g_view==View::Chains&&g_overlay==Overlay::Editor);surface();
            CHECK(play_editor_selected()==PlayUnit::P_SLICE);sequencer_view_cancel_input();CHECK(play_editor_back());
            CHECK(g_view==View::Sequencer&&g_overlay==Overlay::None);
            CHECK(sequencer_selected_chain()==2&&sequencer_selected_step()==5&&sequencer_selected_layer()==7);}
        if(action==7)step_focus_test_ms+=201;
        if(action==8)--step_focus_test_ms;
        const auto reset=surface();step_focus_test_ms+=120;CHECK(surface()==reset);
    }
    // Hits at the new logical destination cannot tint/shrink a travelling plate
    // at its old location. At arrival the selected face uses the spot base.
    {slice_cases::Setup f(16);prepare(f,1);select(f,2,14);surface();const auto begin=step_focus_test_ms;
        for(int age:{0,10,25,50}){step_focus_test_ms=begin+age;const auto base=surface(hit(65535)),active=surface(hit(30,140,14));
            const int tx=8+14*19,ty=layout::ROWS[2];for(int y=0;y<240;++y)for(int x=0;x<320;++x)
                if(x<tx||x>=tx+18||y<ty||y>=ty+18){CHECK(base[y*320+x]==active[y*320+x]);++checked_pixels;}}
        step_focus_test_ms=begin+100;oracle(surface(hit(30,140,14)),surface(hit(65535)),2,14,true,1,0);
    }
    // Repeated real edits retarget immediately, with no queued edits, stale
    // input destination, additive shrink or frozen detail surface.
    {slice_cases::Setup f(16);prepare(f,1);for(int target:{12,2,14,1,9}){select(f,2,target);surface();const auto old=f.hash();adjust(f,1);CHECK(f.hash()!=old);
        CHECK(sequencer_selected_step()==target);surface();step_focus_test_ms+=25;surface();}
        step_focus_test_ms+=120;const auto rest=surface();step_focus_test_ms+=120;CHECK(surface()==rest);
    }
}
void test_combination(){Clock clock;slice_cases::Setup f(16);prepare(f,1);adjust(f,1);const auto begin=step_focus_test_ms;
    const int times[]={0,10,30,50,80,110};std::vector<Image> frames;
    for(int ms:times){step_focus_test_ms=begin+ms;frames.push_back(surface(hit(ms)));}
    step_focus_test_ms=begin+140;const auto rest=surface(hit(65535));
    for(size_t n=0;n<std::size(times);++n)oracle(frames[n],rest,2,5,true,playback(times[n]),press(times[n]));
}
void test_engine_and_audio(){Clock clock;
    for(bool phase:{false,true}){slice_cases::Setup f(16);prepare(f,0,false);
        for(int c=0;c<4;++c){f.engine->pattern.length[c]=1;f.engine->chains[c].volume=4096;f.engine->pattern.microtiming[c][0]=0;
            auto& step=f.engine->pattern.steps[c][0];step.flags=SeqStep::Trig;step.set_retrig_count(c==2?4:1);step.set_condition(SeqStep::Condition::Always);}
        f.engine->set_bpm(120);if(phase)CHECK(f.engine->set_clock_mod(ClockTarget::Tempo,ModSource::SigilA,1));
        f.tap(KEY_START);std::array<q15,256> audio{};int last=65535,hit_count=0;const auto data=f.hash(),pcm=f.sample_hash();
        for(int block=0;block<1000;++block){f.engine->render(audio.data(),128);f.refresh();step_focus_test_ms+=4;
            const auto image=surface();const auto s=sequencer_snapshot();const auto& h=s.sequence_hit[2];
            if(h.age_ms<last){++hit_count;++real_hits;}last=h.age_ms;
            if(h.step!=255){CHECK(h.step==0&&h.bank==0&&h.duration_ms>0&&h.duration_ms<=140);const auto rest=surface(quiet());oracle(image,rest,2,0,false,playback(h.age_ms,h.duration_ms),0,false);}
            CHECK(sequencer_selected_step()==15&&sequencer_selected_chain()==3);
        }
        CHECK(hit_count>80);f.tap(KEY_START);surface();CHECK(sequencer_snapshot().sequence_hit[2].step==255);CHECK(f.hash()==data&&f.sample_hash()==pcm);
    }
    auto audio_run=[](bool visual){Clock clock;slice_cases::Setup f(16);prepare(f,1);for(auto& chain:f.engine->chains)chain.volume=4096;
        f.tap(KEY_START);std::array<q15,1024> pcm{};std::vector<q15> result;result.reserve(180*1024);
        for(int block=0;block<180;++block){if(block%7==0){select(f,2,(block/7*5)%16);adjust(f,1);}if(block%29==0)f.engine->set_bpm(90+block);
            step_focus_test_ms+=16;if(visual){surface();surface({},true);surface();}
            f.engine->render(pcm.data(),512);f.refresh();result.insert(result.end(),pcm.begin(),pcm.end());}
        return result;};const auto a=audio_run(false),b=audio_run(true);CHECK(a==b);CHECK(std::any_of(a.begin(),a.end(),[](q15 x){return x!=0;}));
    std::printf("playhead press PCM twins: %zu q15 samples byte-exact, same actual inputs with/without draws\n",a.size());
}
int run(){const std::pair<const char*,void(*)()> tests[]={
    {"native playback / all8 layers / both selections / exact envelope+chamfer+glyphs",test_playback_oracle},
    {"bank/page/length/flow/transport identity gates; no index-derived hit",test_identity_and_gates},
    {"empty/lock-only all8 layers; actual flow edit and native line preservation",test_empty_lock_and_flow},
    {"actual edit / all8 layers / immediate values /110ms exact fixed glyphs",test_edit_oracle},
    {"stationary touch and keyboard clamps do not restart/cancel active pulse",test_noop_during_press},
    {"copy/noop paste/clear/remove/paint on actual changed values",test_copy_clear_remove_paint},
    {"page/bank/layer/cancel/nav/ring/cuts/gap/backward resets; rapid focus coexistence",test_context_and_focus},
    {"simultaneous hit+press max shrink and ordered tint",test_combination},
    {"real legacy+phase retrigs; audio and source isolation",test_engine_and_audio}};
    int failed=0;for(const auto& test:tests)try{test.second();std::printf("ok: playhead press %s\n",test.first);}catch(const std::exception& e){++failed;std::fprintf(stderr,"FAIL: playhead press %s: %s\n",test.first,e.what());}
    std::printf("playhead press: %zu tests, %d failed; frames=%llu exact_pixels=%llu native_glyph_pixels=%llu changed_pixels=%llu real_hits=%llu; peak bottom=%d top=%d paired=%d /8192\n",std::size(tests),failed,(unsigned long long)draw_frames,(unsigned long long)checked_pixels,(unsigned long long)glyph_pixels,(unsigned long long)changed_pixels,(unsigned long long)real_hits,maximum_objects,maximum_top_objects,maximum_shared_objects);
    return failed?1:0;
}
void dump(const std::filesystem::path& directory){Clock clock;std::filesystem::create_directories(directory);std::ofstream manifest(directory/"manifest.tsv");
    manifest<<"frame\tscenario\tms\tlayer\tchain\tstep\tpage\tbank\ttop_objects\tbottom_objects\tshared_objects\tout_of_bounds\torigin\n";int number=0;
    auto shot=[&](const std::string& scenario,int ms,const Inject& inject=Inject{}){char id[16];std::snprintf(id,sizeof id,"%04d",number++);const std::string name=std::string(id)+"_"+scenario;
        surface({},true);const int top=objects;dump_surface(directory,name+"_top");surface(inject);const int bottom=objects;dump_surface(directory,name+"_bottom");CHECK(top+bottom<8192);
        manifest<<name<<'\t'<<scenario<<'\t'<<ms<<'\t'<<sequencer_selected_layer()<<'\t'<<sequencer_selected_chain()<<'\t'<<sequencer_selected_step()<<'\t'<<sequencer_current_page()<<'\t'<<sequencer_edit_bank()<<'\t'<<top<<'\t'<<bottom<<'\t'<<top+bottom<<"\t0\t"<<(inject?"synthetic snapshot / native draw":"actual input+engine / native draw")<<'\n';};
    for(int layer=0;layer<8;++layer)for(bool selected:{false,true}){slice_cases::Setup f(16);prepare(f,layer,selected);
        const auto scenario="play_"+std::to_string(layer)+(selected?"_selected":"_free");for(int ms=0;ms<=160;ms+=10)shot(scenario,ms,hit(ms));}
    for(int layer=0;layer<8;++layer){slice_cases::Setup f(16);prepare(f,layer);adjust(f,layer);const auto begin=step_focus_test_ms;
        for(int ms=0;ms<=140;ms+=10){step_focus_test_ms=begin+ms;shot("edit_"+std::to_string(layer),ms);}}
    {slice_cases::Setup f(16);prepare(f,1);int elapsed=0;for(int s:{12,2,14,1,9}){select(f,2,s);surface();adjust(f,1);
        for(int t=0;t<40;t+=10){step_focus_test_ms+=10;shot("rapid_focus_press",elapsed);elapsed+=10;}}
        for(int t=0;t<160;t+=10){step_focus_test_ms+=10;shot("rapid_focus_press",elapsed);elapsed+=10;}}
    {slice_cases::Setup f(16);prepare(f,0,false);for(int c=0;c<4;++c){f.engine->chains[c].volume=4096;f.engine->pattern.length[c]=1;auto& step=f.engine->pattern.steps[c][0];step.flags=SeqStep::Trig;step.set_retrig_count(4);step.set_condition(SeqStep::Condition::Always);f.engine->pattern.microtiming[c][0]=0;}
        f.tap(KEY_START);std::array<q15,640> audio{};for(int ms=0;ms<600;ms+=10){f.engine->render(audio.data(),320);f.refresh();step_focus_test_ms+=10;shot("real_retrigs",ms);}f.tap(KEY_START);}
    {Fixture f;f.dense();static_cast<FmUnit*>(f.engine->chains[0].units[0])->drone=true;f.refresh();f.mode(0);select(f,0,5);settle();for(int ms=0;ms<=160;ms+=10)shot("flow_no_hit",ms,hit(ms,140,5,0,0));
        f.tap(KEY_L|KEY_DOWN);const auto begin=step_focus_test_ms;for(int ms=0;ms<=140;ms+=10){step_focus_test_ms=begin+ms;shot("flow_edit",ms);}}
    {slice_cases::Setup f(16);prepare(f,1);
        delete f.engine->chains[3].units[0];auto* flow=new FmUnit;flow->drone=true;f.engine->chains[3].units[0]=flow;f.refresh();
        for(int c=0;c<4;++c){f.engine->chains[c].volume=4096;f.engine->pattern.length[c]=16;
            f.engine->pattern.direction[c]=c==1?SequenceDirection::Reverse:SequenceDirection::Forward;
            for(int s=0;s<64;++s){auto& step=f.engine->pattern.steps[c][s];f.engine->pattern.microtiming[c][s]=0;
                const bool on=c==0?(s%4==0):c==1?(s%4==2):c==2?(s%2==0):false;
                if(!on)step={};else{step.flags=SeqStep::Trig;step.set_retrig_count(1);step.set_condition(SeqStep::Condition::Always);}}}
        f.engine->set_bpm(150);f.refresh();select(f,2,4);settle();f.tap(KEY_START);std::array<q15,640> audio{};
        for(int ms=0;ms<1600;ms+=10){f.engine->render(audio.data(),320);f.refresh();step_focus_test_ms+=10;shot("real_sequence",ms);}f.tap(KEY_START);}
    {cuts_cases::Setup f(8);shot("cuts",0);}
    std::printf("playhead press captures: %d native duals / %d screens\n",number,number*2);
}
} // namespace playhead_press_cases
