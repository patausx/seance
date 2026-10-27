// Included at the end of sequencer_test.cpp's anonymous namespace.
// Real controller, engine, native glyph renderer and host software raster.
// This does not model the GPU or replace the runtime renderer with a mock.
namespace step_focus_cases {
struct Clock {
    Clock() { step_focus_test_ms = 10000; }
    ~Clock() { step_focus_test_ms = 0; }
};
uint64_t checked_pixels = 0;
int motion_frames = 0, exact_plate_masks = 0, exact_pip_masks = 0;
uint64_t inverse_pixels = 0;
#include "score_selection_oracle.h"
std::vector<u32> editor_surface(bool top=false,bool capture=true) {
    CHECK(g_view==View::Chains&&g_overlay==Overlay::Editor);
    if(capture)play_editor_capture();
    width=top?400:320;
    objects=outside_rects=outside_lines=text_overlaps=0;texts.clear();
    pixels.assign(width*HEIGHT,0xff000000u);
    if(top)CHECK(play_editor_top());else play_editor_draw();
    CHECK(objects>0&&objects<8192&&text_overlaps==0&&!outside_rects&&!outside_lines);
    if(top)maximum_top_objects=std::max(maximum_top_objects,objects);
    else maximum_objects=std::max(maximum_objects,objects);
    return pixels;
}
std::vector<u32> surface(bool top = false) {
    // Current CUTS owns the real shared PLAY editor, not the retired inline
    // panel. It may perform bounded analysis between captures; draw is frozen.
    if(g_view==View::Chains&&g_overlay==Overlay::Editor)return editor_surface(top);
    const auto result=draw(top);
    CHECK(outside_rects == 0 && outside_lines == 0);
    CHECK(objects < 8192);
    return result;
}
void settle() { surface(); step_focus_test_ms += 120; surface(); }
void select(Fixture& f, int chain, int local) {
    // Cancel rather than commit a trigger tap; selection itself is immediate.
    f.down(chain,local); f.release(false);
    CHECK(sequencer_selected_chain() == chain);
    CHECK(sequencer_selected_step() == sequencer_current_page()*16+local);
}
void same_band(const std::vector<u32>& a,const std::vector<u32>& b,int w,int y0,int y1) {
    CHECK(a.size() == b.size());
    for(int y=y0;y<y1;++y)for(int x=0;x<w;++x){CHECK(a[y*w+x]==b[y*w+x]);++checked_pixels;}
}
void stationary() {
    const auto now=surface(); step_focus_test_ms+=150; CHECK(surface()==now);
}
void test_layers_timing() {
    Clock clock;
    for(int layer=0;layer<8;++layer)for(bool occupied:{false,true}) {
        slice_cases::Setup f(16);
        if(occupied)f.dense();
        const int row=layer==7?slice_cases::CHAIN:0;
        if(layer==7 && occupied)for(int s=0;s<16;++s)f.stored(s+1,s);
        f.mode(layer); select(f,row,0); settle();
        const auto data=f.hash();const auto pcm=f.sample_hash();const int dirty=mutations;
        select(f,row,8);
        CHECK(f.hash()==data && mutations==dirty);
        sequencer_capture_inspector();CHECK(sequencer_inspector().step==8);
        const auto t0=surface(); const auto top=surface(true);
        const u64 begin=step_focus_test_ms;
        std::vector<std::vector<u32>> images;
        for(unsigned dt:{0u,1u,17u,33u,50u,67u,83u,99u,100u,101u,250u}) {
            step_focus_test_ms=begin+dt;
            const auto image=surface();++motion_frames;
            const int bottom=objects;CHECK(surface(true)==top);
            maximum_shared_objects=std::max(maximum_shared_objects,bottom+objects);CHECK(bottom+objects<8192);
            same_band(image,t0,320,0,30);same_band(image,t0,320,194,240);
            CHECK(f.hash()==data && f.sample_hash()==pcm && mutations==dirty);
            CHECK(sequencer_selected_chain()==row && sequencer_selected_step()==8);
            CHECK(surface()==image); // repeated draw at one timestamp is pure
            images.push_back(image);
        }
        CHECK(images[0]!=images[4]&&images[4]!=images[8]);
        CHECK(images[8]==images[9]&&images[9]==images[10]); // 100ms arrival, no deferred input
        // Inactive events retain their glyphs, length slash and selected plate.
        f.engine->pattern.length[row]=1;f.refresh();select(f,row,15);settle();
        const auto inactive=surface();CHECK(sequencer_selected_step()==15);
        CHECK(inactive[layout::ROWS[row]*320+layout::X0+15*layout::PITCH]!=m::spot()); // chamfer
        CHECK(inactive[(layout::ROWS[row]+8)*320+layout::X0+15*layout::PITCH+1]==m::spot());
        CHECK(f.engine->pattern.length[row]==1);
    }
}
std::vector<u32> hidden_focus() {
    CHECK(g_overlay==Overlay::None);g_overlay=Overlay::Picker;
    const auto result=surface();g_overlay=Overlay::None;return result;
}
void test_native_ink_oracle() {
    Clock clock;
    for(int layer=0;layer<8;++layer) {
        slice_cases::Setup f(16);f.dense();f.mode(layer);
        const int row=layer==7?slice_cases::CHAIN:0;
        if(layer==7)for(int s=0;s<16;++s)f.stored(s+1,s);
        select(f,row,8);const auto substrate=hidden_focus();
        select(f,row,0);settle();select(f,row,8);surface();const auto begin=step_focus_test_ms;
        for(unsigned ms=0;ms<=100;++ms) {
            step_focus_test_ms=begin+ms;const auto actual=surface();
            plate_oracle(actual,substrate,oracle_plate(row,0,row,8,ms),layer);
            CHECK(surface()==actual);++motion_frames;
        }
    }
}
// Literal dice origins transcribed from the final study, not runtime arrays.
// Exhaustive actual pixel assertions check no extra ink and exact half-pixel centre.
bool pip_pixel(int count,int x,int y) {
    const std::pair<int,int> one[]={{0,0}},two[]={{-3,-3},{3,3}},three[]={{-4,-4},{0,0},{4,4}},four[]={{-3,-3},{3,-3},{-3,3},{3,3}};
    const auto* centers=count==1?one:count==2?two:count==3?three:four;
    for(int p=0;p<count;++p)if(x>=centers[p].first&&x<=centers[p].first+1&&y>=centers[p].second&&y<=centers[p].second+1)return true;
    return false;
}
void test_square_pips() {
    Clock clock;Fixture f;f.dense();f.mode(3);
    for(int selected_count=1;selected_count<=4;++selected_count){select(f,0,selected_count-1);settle();const auto image=surface();
        for(int count=1;count<=4;++count){const int x0=8+(count-1)*19,y0=layout::ROWS[0];int ink=0,sx=0,sy=0;
            for(int y=0;y<18;++y)for(int x=0;x<18;++x){const bool pip=pip_pixel(count,x-8,y-8);
                const int inset=y==0||y==17?2:y==1||y==16?1:0;
                const bool selected=count==selected_count;
                u32 expected=x>=inset&&x<18-inset?(selected?theme_test::spot():col_cell()):theme_test::paper(y0+y);
                if(pip){expected=selected?col_text_i():theme_test::ink();++ink;sx+=x;sy+=y;}
                CHECK(image[(y0+y)*320+x0+x]==expected);++checked_pixels;
            }
            CHECK(ink==count*4&&sx*2==ink*17&&sy*2==ink*17);++exact_pip_masks;
            const int cx=100+(count-1)*60,cy=218;int selector_ink=0;
            for(int y=-7;y<=7;++y)for(int x=-7;x<=7;++x){const bool pip=pip_pixel(count,x+1,y+1);
                const u32 expected=pip?(count==selected_count?theme_test::spot():theme_test::ghost()):theme_test::paper(cy+y);
                CHECK(image[(cy+y)*320+cx+x]==expected);++checked_pixels;selector_ink+=pip;
            }
            CHECK(selector_ink==count*4);++exact_pip_masks;
        }
    }
}
void test_flow_extrema() {
    Clock clock;Fixture f;static_cast<FmUnit*>(f.engine->chains[0].units[0])->drone=true;f.refresh();f.mode(0);
    for(int chain:{0,3})for(int shape=0;shape<6;++shape) {
        const int slot=g_ui_snapshot.sequence_value_slot[chain],param=g_ui_snapshot.sequence_value_param[chain];CHECK(slot>=0&&param>=0);
        for(int s=0;s<16;++s) {
            const int raw=shape<3?(shape==0?0:shape==1?16384:32767):shape==3?s*32767/15:shape==4?(15-s)*32767/15:(s%2?32767:0);
            CHECK(f.engine->set_step_lock(chain,s,slot,param,q15(raw)));
        }
        f.refresh();const auto data=f.hash();const int dirty=mutations;
        for(int local:{0,1,8,14,15}) {
            select(f,chain,local);const auto native=hidden_focus(),actual=surface();
            CHECK((SequenceLifecycle)sequencer_snapshot().sequence_lifecycle[chain]==SequenceLifecycle::Flow);
            const int raw=shape<3?(shape==0?0:shape==1?16384:32767):shape==3?local*32767/15:shape==4?(15-local)*32767/15:(local%2?32767:0);
            CHECK(flow_y(chain,local)==layout::ROWS[chain]+26-((raw*100+16383)/32767)*23/100);
            flow_oracle(actual,native,chain,local);
            const int cy=flow_y(chain,local);const auto p=flow_plate(chain,local);
            CHECK(p.x==8+19*local&&p.y==cy-5&&p.w==18&&p.h==10);
            for(int x=15+19*local;x<=17+19*local;++x)CHECK(actual[cy*320+x]==col_text_i());
            const auto top=surface(true);
            for(int dt:{1,50,100,500}){step_focus_test_ms+=dt;CHECK(surface()==actual&&surface(true)==top);++motion_frames;}
            CHECK(f.hash()==data&&mutations==dirty);
        }
    }
}
void test_event_flow_transitions() {
    Clock clock;Fixture f;f.dense();static_cast<FmUnit*>(f.engine->chains[0].units[0])->drone=true;f.refresh();f.mode(0);
    const auto data=f.hash();const int dirty=mutations;
    for(int flow_row:{0,3})for(int local:{0,15}) {
        // Start an actual event move, then enter the flow surface before arrival.
        select(f,1,0);settle();select(f,2,12);const auto start=surface();step_focus_test_ms+=25;CHECK(surface()!=start);
        select(f,flow_row,local);const auto first=surface();step_focus_test_ms+=50;CHECK(surface()==first);
        // Re-enter event without hidden_focus/cancel/navigation in between:
        // flow itself must discard old motion, not an oracle setup reset.
        // Reset starts at the destination, with only the original self-settle
        // squash at50ms. It must never resume the interrupted event trajectory.
        select(f,2,15);const auto event=surface();step_focus_test_ms+=50;const auto event_mid=surface();
        step_focus_test_ms+=50;CHECK(surface()==event);const auto event_native=hidden_focus();
        plate_oracle(event,event_native,raster(target_pose(2,15)),0);
        plate_oracle(event_mid,event_native,oracle_plate(2,15,2,15,50),0);
        surface();step_focus_test_ms+=120;CHECK(surface()==event);
        select(f,flow_row,local);const auto native=hidden_focus();flow_oracle(first,native,flow_row,local);
        // A new event->event movement after flow uses the new event origin.
        select(f,2,0);const auto next_native=hidden_focus();select(f,2,15);settle();select(f,2,0);surface();
        step_focus_test_ms+=25;plate_oracle(surface(),next_native,oracle_plate(2,15,2,0,25),0);
        CHECK(f.hash()==data&&mutations==dirty);
    }
    // Flow source in NOTE/LEVEL is still an ordinary moving event badge;
    // returning to TRIG makes only the small point-local fill, never row-wide.
    for(int layer:{1,2}){f.mode(layer);select(f,0,0);settle();select(f,0,15);const auto begin=surface();
        step_focus_test_ms+=25;CHECK(surface()!=begin);f.mode(0);const auto first=surface();
        const auto native=hidden_focus();flow_oracle(first,native,0,15);surface();stationary();}
}
void cross_flow_setup(slice_cases::Setup& f,int shape) {
    delete f.engine->chains[1].units[1];f.engine->chains[1].units[1]=nullptr; // OSC without VCA is real flow
    f.refresh();f.dense();f.mode(0);
    CHECK((SequenceLifecycle)g_ui_snapshot.sequence_lifecycle[1]==SequenceLifecycle::Flow);
    CHECK((SequenceLifecycle)g_ui_snapshot.sequence_lifecycle[0]!=SequenceLifecycle::Flow);
    CHECK((SequenceLifecycle)g_ui_snapshot.sequence_lifecycle[2]!=SequenceLifecycle::Flow);
    const int slot=g_ui_snapshot.sequence_value_slot[1],param=g_ui_snapshot.sequence_value_param[1];CHECK(slot>=0&&param>=0);
    for(int s=0;s<16;++s){const int raw=shape<3?(shape==0?0:shape==1?16384:32767):shape==3?s*32767/15:shape==4?(15-s)*32767/15:(s%2?32767:0);
        CHECK(f.engine->set_step_lock(1,s,slot,param,q15(raw)));}f.refresh();
}
void test_event_crosses_flow() {
    Clock clock;
    for(int shape=0;shape<6;++shape){slice_cases::Setup f(16);cross_flow_setup(f,shape);
        const auto data=f.hash(),pcm=f.sample_hash();const int dirty=mutations;
        const int routes[][4]={{0,0,2,15},{2,15,0,0},{0,8,2,8},{2,8,0,8}};
        for(const auto& route:routes){const int from_row=route[0],from_col=route[1],to_row=route[2],to_col=route[3];
            select(f,to_row,to_col);const auto native=hidden_focus();select(f,from_row,from_col);settle();
            select(f,to_row,to_col);surface();const auto begin=step_focus_test_ms;const auto top=surface(true);int curve_pixels=0;
            for(unsigned ms=0;ms<=100;++ms){step_focus_test_ms=begin+ms;const auto actual=surface();
                const auto plate=oracle_plate(from_row,from_col,to_row,to_col,ms);plate_oracle(actual,native,plate,0);
                for(int y=83;y<114;++y)for(int x=plate.x;x<plate.x+plate.w;++x)
                    if(covered(plate,x,y)&&flow_inverse_pixel(1,x,y)){CHECK(actual[y*320+x]==col_text_i());++curve_pixels;}
                CHECK(surface()==actual&&surface(true)==top);CHECK(f.hash()==data&&f.sample_hash()==pcm&&mutations==dirty);
                CHECK(sequencer_selected_chain()==to_row&&sequencer_selected_step()==to_col);++motion_frames;}
            CHECK(curve_pixels>0); // each path really crosses native flow ink
        }
    }
}
void test_immediate_input() {
    Clock clock;Fixture f;f.dense();f.mode(1);select(f,0,0);settle();
    const auto original=f.hash();select(f,0,7);surface();step_focus_test_ms+=20;
    CHECK(f.hash()==original);const auto source=f.engine->pattern.steps[0][0];
    f.tap(KEY_A);CHECK(f.hash()!=original);
    CHECK(std::memcmp(&source,&f.engine->pattern.steps[0][0],sizeof source)==0);
    CHECK(sequencer_selected_step()==7);
    // Input edits the new logical destination before the visual plate arrives;
    // no edit waits for the 100ms focus clock.
    select(f,0,10);surface();f.tap(KEY_X);
    CHECK(f.engine->pattern.steps[0][10].flags==0 && f.engine->pattern.steps[0][10].lock_count==0);
    f.mode(0);select(f,0,11);surface();const bool before=f.trig(0,11);f.tap(KEY_A);CHECK(f.trig(0,11)!=before);
    select(f,0,12);surface();f.tap(KEY_L|KEY_X);select(f,0,13);f.tap(KEY_L|KEY_A);
    CHECK(std::memcmp(&f.engine->pattern.steps[0][12],&f.engine->pattern.steps[0][13],sizeof(SeqStep))==0);
}
void test_rapid_and_rows() {
    Clock clock;Fixture f;f.dense();f.mode(1);select(f,0,0);
    const auto native=hidden_focus();settle();const auto data=f.hash();const int dirty=mutations;
    OraclePose shown=target_pose(0,0);
    for(const auto target:{std::pair<int,int>{0,15},{0,2},{2,7},{3,15},{3,0},{0,0},{1,5}}) {
        const auto previous=shown;select(f,target.first,target.second);
        CHECK(sequencer_selected_chain()==target.first&&sequencer_selected_step()==target.second);
        const auto at=surface();plate_oracle(at,native,raster(previous),1,false);
        const auto top=surface(true);step_focus_test_ms+=25;
        shown=travel(previous,target_pose(target.first,target.second),25);
        const auto mid=surface();CHECK(mid!=at);plate_oracle(mid,native,raster(shown),1,false);
        same_band(mid,at,320,0,30);same_band(mid,at,320,194,240);CHECK(surface(true)==top);
        CHECK(f.hash()==data&&mutations==dirty);++motion_frames;
    }
    settle();stationary();
    // Page wrapping resets the origin to its destination (including original
    // self-settle squash); vertical wrapping keeps the previous row origin.
    select(f,0,0);settle();
    const u32 directions[]={KEY_LEFT,KEY_RIGHT,KEY_UP,KEY_DOWN};
    const int chains[]={0,0,3,0},steps[]={63,0,0,0};
    for(int n=0;n<4;++n){const int origin=sequencer_selected_chain();f.tap(directions[n]);
        CHECK(sequencer_selected_chain()==chains[n]&&sequencer_selected_step()==steps[n]);
        const auto immediate=surface();step_focus_test_ms+=50;const auto mid=surface();
        step_focus_test_ms+=50;const auto endpoint=surface();const auto substrate=hidden_focus();
        const int target_local=steps[n]%16;
        plate_oracle(endpoint,substrate,raster(target_pose(chains[n],target_local)),1);
        plate_oracle(immediate,substrate,raster(target_pose(n<2?chains[n]:origin,target_local)),1);
        plate_oracle(mid,substrate,oracle_plate(n<2?chains[n]:origin,target_local,chains[n],target_local,50),1);
        CHECK(f.hash()==data&&mutations==dirty);surface();}
    // Backwards wall clock cancels motion at the current logical destination.
    select(f,3,12);surface();step_focus_test_ms-=1;const auto reset=surface();
    const auto substrate=hidden_focus();plate_oracle(reset,substrate,raster(target_pose(3,12)),1);surface();stationary();
}
void test_context_resets() {
    Clock clock;Fixture f;f.dense();f.mode(1);select(f,0,0);settle();
    auto moving=[&] {
        const int local=sequencer_selected_step()%16==9?2:9;
        select(f,0,local);const auto initial=surface();step_focus_test_ms+=25;
        CHECK(surface()!=initial); // every lifecycle action interrupts actual motion
    };
    moving();select(f,0,15);f.tap(KEY_RIGHT);CHECK(sequencer_current_page()==1);surface();stationary();
    moving();f.mode(3);surface();stationary();
    moving();sequencer_view_cancel_input();surface();stationary();
    moving();g_view=View::Chains;g_overlay=Overlay::Picker;sequencer_capture_inspector();
    step_focus_test_ms+=20;g_view=View::Sequencer;g_overlay=Overlay::None;sequencer_view_enter();surface();stationary();
    moving();f.r_hold();const auto ring=surface();step_focus_test_ms+=300;CHECK(surface()==ring);
    f.frame(0,0,KEY_R);surface();stationary();
    moving();extent_bank(f,2);CHECK(sequencer_edit_bank()==2);surface();stationary();
    // A real cuts panel owns its frozen surface and returns to settled score focus.
    slice_cases::Setup slices(8);slices.stored(2);select(slices,2,0);settle();
    select(slices,2,10);surface();step_focus_test_ms+=25;
    slices.control(135,200);CHECK(g_view==View::Chains&&g_overlay==Overlay::Editor);const auto cuts=surface();
    CHECK(play_editor_selected()==PlayUnit::P_SLICE);CHECK(editor_surface(false,false)==cuts);
    const auto cuts_data=slices.hash(),cuts_pcm=slices.sample_hash();
    step_focus_test_ms+=200;const auto later=surface();CHECK(editor_surface(false,false)==later);
    CHECK(slices.hash()==cuts_data&&slices.sample_hash()==cuts_pcm);
    // Main's B route cancels score input before calling the shared editor back;
    // it does NOT enter/reset the score, so step/layer identity is preserved.
    sequencer_view_cancel_input();CHECK(play_editor_back());CHECK(g_view==View::Sequencer&&g_overlay==Overlay::None);
    CHECK(sequencer_selected_chain()==2&&sequencer_selected_step()==10&&sequencer_selected_layer()==7);
    surface();stationary();
}
void test_flow_playhead() {
    Clock clock;Fixture f;f.dense();static_cast<FmUnit*>(f.engine->chains[0].units[0])->drone=true;
    f.refresh();f.mode(0);select(f,0,0);settle();
    const auto data=f.hash();const int dirty=mutations;
    select(f,0,12);const auto first=surface();step_focus_test_ms+=50;const auto mid=surface();
    CHECK(first==mid);same_band(first,mid,320,194,240);settle();
    CHECK(f.hash()==data && mutations==dirty);
    // Publish an independently moving real-engine playhead; focus is NOT a
    // replacement for transport and never changes selected step or page.
    f.tap(KEY_START);std::array<q15,1024> audio{};
    // Stay below the pre-existing hot-master overflow path; this focused UI
    // test is not a sanitizer repair of unrelated master DSP.
    for(auto& chain:f.engine->chains)chain.volume=4096;
    for(int block=0;block<40;++block) {
        f.engine->render(audio.data(),512);f.refresh();step_focus_test_ms+=16;
        const auto image=surface();const auto& snap=sequencer_snapshot();
        CHECK(sequencer_selected_step()==12 && sequencer_current_page()==0);
        for(int chain=0;chain<4;++chain) {
            const int step=snap.sequence_step[chain];if(step>=16)continue;
            const int x=layout::X0+step*layout::PITCH+3,y=layout::ROWS[chain]+layout::CELL_H+1;
            CHECK(image[y*320+x]==palette::playhead());
        }
        ++motion_frames;
    }
    f.tap(KEY_START);CHECK(f.hash()==data);
}
void test_audio_draw_isolation() {
    auto run_audio=[](bool render_ui) {
        Clock clock;Fixture f;f.dense();f.mode(0);
        for(auto& chain:f.engine->chains)chain.volume=4096;
        f.tap(KEY_START);std::vector<q15> result;result.reserve(160*1024);
        std::array<q15,1024> audio{};
        for(int block=0;block<160;++block) {
            if(block%5==0)select(f,(block/5)%4,(block*7)%16);
            step_focus_test_ms+=16;
            if(render_ui){surface();surface(true);}
            f.engine->render(audio.data(),512);f.refresh();
            result.insert(result.end(),audio.begin(),audio.end());
        }
        f.tap(KEY_START);return result;
    };
    const auto without=run_audio(false),with=run_audio(true);CHECK(without==with);
    CHECK(std::any_of(with.begin(),with.end(),[](q15 sample){return sample!=0;}));
    std::printf("step focus PCM twin: %zu stereo q15 samples byte-exact (same real input, with/without draw)\n",with.size());
}
int run() {
    const std::pair<const char*,void(*)()> tests[]={
        {"all8 layers / occupied+empty / elapsed time / immediate inspector / stable wave+editor",test_layers_timing},
        {"independent100ms plate / native inverse glyphs+diagonal quads / every millisecond / zero exclusions",test_native_ink_oracle},
        {"exact2x2 dice / final 1,2,3,4 geometry / cell+selector / centre8.5",test_square_pips},
        {"flow18x10 / min-mid-max+rises-falls-zigzag / edge rows+steps / exact inverse curve+radius1",test_flow_extrema},
        {"event-flow-event motion reset / flow source NOTE-LEVEL vs TRIG",test_event_flow_transitions},
        {"event travel across unselectedflow /24paths /every millisecond /analytic inverse curve including gaps",test_event_crosses_flow},
        {"immediate edit / toggle / clear / copy",test_immediate_input},
        {"rapid retarget / rows / backwards clock",test_rapid_and_rows},
        {"page / mode / cancel / nav / R ring / bank / cuts lifecycle",test_context_resets},
        {"flow / independent real-engine playhead",test_flow_playhead},
        {"real engine PCM with / without UI draws",test_audio_draw_isolation}};
    int failed=0;
    for(const auto& test:tests)try {test.second();std::printf("ok: step focus %s\n",test.first);}
        catch(const std::exception& e){++failed;std::fprintf(stderr,"FAIL: step focus %s: %s\n",test.first,e.what());}
    std::printf("step focus: %zu tests, %d failed; %d timing frames, %llu fixed pixels, %d exact plate masks, %llu inverse pixels, %d exact pip masks; peak bottom %d top %d shared %d / 8192\n",
        std::size(tests),failed,motion_frames,(unsigned long long)checked_pixels,exact_plate_masks,(unsigned long long)inverse_pixels,exact_pip_masks,maximum_objects,maximum_top_objects,maximum_shared_objects);
    std::printf("raster oracle: zero pixel exclusions; native host raster, not PICA hardware coverage\n");
    return failed?1:0;
}
void dump(const std::filesystem::path& directory) {
    Clock clock;std::filesystem::create_directories(directory);
    std::ofstream manifest(directory/"manifest.tsv");
    manifest<<"frame\tscenario\tms\tlayer\tchain\tstep\tpage\tbank\ttop_objects\tbottom_objects\tshared_objects\tout_of_bounds\n";
    int number=0;
    auto shot=[&](const std::string& scenario,int ms) {
        char id[16];std::snprintf(id,sizeof id,"%04d",number++);const std::string name=std::string(id)+"_"+scenario;
        surface(true);const int top=objects;dump_surface(directory,name+"_top");
        surface();const int bottom=objects;dump_surface(directory,name+"_bottom");CHECK(top+bottom<8192);
        manifest<<name<<'\t'<<scenario<<'\t'<<ms<<'\t'<<sequencer_selected_layer()<<'\t'<<sequencer_selected_chain()<<'\t'
            <<sequencer_selected_step()<<'\t'<<sequencer_current_page()<<'\t'<<sequencer_edit_bank()<<'\t'<<top<<'\t'<<bottom<<'\t'<<top+bottom<<"\t0\n";
    };
    for(int layer=0;layer<8;++layer) {
        slice_cases::Setup f(16);f.dense();f.mode(layer);
        const int row=layer==7?slice_cases::CHAIN:0;select(f,row,0);settle();
        for(int c=0;c<4;++c)for(int s=0;s<16;++s)f.engine->pattern.steps[c][s].flags|=SeqStep::Trig;
        for(int s=0;s<16;++s)f.stored(s+1,s);
        f.refresh();surface();const auto data=f.hash();const auto pcm=f.sample_hash();
        const std::string name="layer_"+std::to_string(layer);shot(name,0);
        select(f,row,1);surface();const auto begin=step_focus_test_ms;
        for(int t=0;t<=120;t+=10){step_focus_test_ms=begin+t;shot(name,t);}
        CHECK(f.hash()==data && f.sample_hash()==pcm);
    }
    {
        slice_cases::Setup f(8);f.dense();f.mode(1);select(f,0,0);settle();
        const std::pair<int,int> targets[]={{0,1},{0,2},{0,1},{1,1},{1,10},{3,15},{2,9},{0,0}};
        int timeline=0;
        for(const auto target:targets){select(f,target.first,target.second);surface();
            for(int t=0;t<160;t+=20){step_focus_test_ms+=20;shot("note_journey",timeline);timeline+=20;}}
        for(const auto target:targets){select(f,target.first,target.second);surface();
            for(int t=0;t<40;t+=10){step_focus_test_ms+=10;shot("rapid",timeline);timeline+=10;}}
        settle();shot("rapid",timeline+100);
        f.engine->pattern.length[2]=1;f.refresh();select(f,2,15);settle();shot("out_of_length",0);
        select(f,2,15);f.tap(KEY_RIGHT);surface();shot("page",0);
        f.r_hold();surface();shot("ring",0);f.frame(0,0,KEY_R);
        extent_bank(f,2);surface();shot("bank",0);
    }
    {
        Fixture f;f.dense();static_cast<FmUnit*>(f.engine->chains[0].units[0])->drone=true;f.refresh();
        f.mode(0);select(f,0,0);settle();select(f,0,9);surface();
        for(int t=0;t<=120;t+=10){step_focus_test_ms+=10;shot("flow",t);}
    }
    {
        Fixture f;f.dense();f.mode(1);select(f,0,0);settle();select(f,0,12);surface();
        for(auto& chain:f.engine->chains)chain.volume=4096;
        f.tap(KEY_START);std::array<q15,1024> audio{};
        for(int t=0;t<32;++t) {
            f.engine->render(audio.data(),512);f.refresh();step_focus_test_ms+=16;shot("playhead",t*16);
        }
        f.tap(KEY_START);
    }
    {
        Fixture f;f.dense();static_cast<FmUnit*>(f.engine->chains[0].units[0])->drone=true;f.refresh();f.mode(0);
        for(int row:{0,3})for(int raw:{0,16384,32767}) {
            const int slot=g_ui_snapshot.sequence_value_slot[row],param=g_ui_snapshot.sequence_value_param[row];
            for(int s=0;s<16;++s)CHECK(f.engine->set_step_lock(row,s,slot,param,q15(raw)));f.refresh();
            for(int local:{0,8,15}){select(f,row,local);surface();shot("flow_extrema_r"+std::to_string(row)+"_v"+std::to_string(raw),local);}
        }
        for(int local:{0,15}) {
            select(f,1,0);settle();select(f,2,12);surface();step_focus_test_ms+=25;shot("event_flow_transitions",0);
            select(f,0,local);shot("event_flow_transitions",25);step_focus_test_ms+=50;shot("event_flow_transitions",75);
            select(f,2,15);shot("event_flow_transitions",76);step_focus_test_ms+=100;shot("event_flow_transitions",176);
        }
    }
    {
        slice_cases::Setup f(16);cross_flow_setup(f,5);
        for(int direction=0;direction<2;++direction){const int from=direction?2:0,to=direction?0:2;
            select(f,from,direction?15:0);settle();select(f,to,direction?0:15);surface();const auto begin=step_focus_test_ms;
            for(int ms=0;ms<=100;ms+=10){step_focus_test_ms=begin+ms;shot(direction?"cross_flow_up":"cross_flow_down",ms);}}
    }
    {cuts_cases::Setup f(8);surface();shot("cuts",0);}
    std::printf("step focus capture: %d native dual frames, %d screens\n",number,number*2);
}
} // namespace step_focus_cases
