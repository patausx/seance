// Included by sequencer_test.cpp: actual runtime controller, engine, bitmap text,
// menu material and C2D submissions. Authored test tables are NOT hardware data.
struct SigilsFixture : Fixture {
    SigilsFixture() {
        g_view=View::Sigils;g_overlay=Overlay::None;g_sel_chain=g_sel_slot=g_sel_sigil=0;
        g_drag=Drag::None;g_seq_lock_armed=false;param_lens_cancel();sigils_forget_undo();
        std::fill(std::begin(g_sigil_undo_valid),std::end(g_sigil_undo_valid),false);
        g_sigil_clip_valid=false;g_sig_canvas_mutated=false;
        delete engine->replace_unit(0,0,new FilterUnit);
        for(int i=0;i<NUM_SIGILS;++i) {
            engine->sigils[i]=Sigil{};engine->set_sigil_rate(i,8192);
            engine->set_sigil_sync(i,false);engine->set_sigil_div(i,4);
            engine->set_sigil_mod(i,ModSource::None,0);
        }
        authored();mutations=0;
    }
    ~SigilsFixture() {sigils_view_touch_release();sigils_close_mode_menu();}
    Sigil& s() {return engine->sigils[g_sel_sigil];}
    void down(int x,int y) {sigils_view_touch_down(x,y);}
    void drag(int x,int y) {sigils_view_touch_drag(x,y);}
    void lift() {sigils_view_touch_release();}
    void tap(int x,int y) {down(x,y);lift();}
    void capture() {sigils_capture();}
    void mode(int m) {static const int xs[]={34,82,130,178,230,282};tap(xs[m],43);CHECK((int)s().mode==m);}
    void tab(int i) {tap(130+31*i,18);CHECK(g_sel_sigil==i);}
    void audio(int blocks=4) {std::array<q15,1024> out{};for(int i=0;i<blocks;++i)engine->render(out.data(),512);}
    void authored() {
        // Fixed, asymmetric, piecewise-linear fixture. The real 128-point storage
        // is projected by C++, not a traced bitmap or a Python replacement UI.
        constexpr int anchors[][2]={{0,15000},{13,24000},{31,-18000},{49,11000},
            {68,-24500},{84,21000},{102,4000},{117,-14000},{127,-5000}};
        for(unsigned a=1;a<sizeof anchors/sizeof anchors[0];++a)
            s().stroke_to(anchors[a-1][0],(q15)anchors[a-1][1],anchors[a][0],(q15)anchors[a][1]);
        s().phase=0x9eb851ebu;s().depth=32767;s().running=true;s().follow_active=false;
        s().value=12345; // Explicit authored instantaneous value, independent of shape.
    }
};
std::array<q15,SIGIL_POINTS> sigils_table(const Sigil& s) {
    std::array<q15,SIGIL_POINTS> t{};std::copy(std::begin(s.table),std::end(s.table),t.begin());return t;
}
q15 sigils_percent(int value) {return (q15)std::lround(value*32767.0/99);}
void sigils_render(bool top,bool dispatch=false) {
    width=top?400:320;objects=outside_rects=outside_lines=text_overlaps=0;
    texts.clear();pixels.assign(width*HEIGHT,0xff000000u);
    // Deliberately remove the engine AND audio guard dependency before every draw.
    struct Restore {Engine* e=g_eng;platform::AudioOut* a=g_audio;
        Restore(){g_eng=nullptr;g_audio=nullptr;}~Restore(){g_eng=e;g_audio=a;}} restore;
    if(top) {if(dispatch)top_draw();else sigils_top_draw();}else sigils_view_draw();
    if(outside_rects||outside_lines||text_overlaps) {
        std::fprintf(stderr,"sigils %s: rect=%d line=%d text-overlap=%d\n",top?"top":"bottom",outside_rects,outside_lines,text_overlaps);
        for(const auto& t:texts)std::fprintf(stderr,"  %d,%d %dx%d %s\n",t.x,t.y,t.w,t.h,t.text.c_str());
    }
    CHECK(objects>0&&objects<C2D_DEFAULT_MAX_OBJECTS*2);
    CHECK(outside_rects==0&&outside_lines==0&&text_overlaps==0);
    for(const auto& t:texts)CHECK(t.x>=0&&t.y>=0&&t.x+t.w<=width&&t.y+t.h<=HEIGHT);
    strn_menu_palette_and_paper(); // Independently computed integer menu gradient.
    if(top)maximum_top_objects=std::max(maximum_top_objects,objects);
    else maximum_objects=std::max(maximum_objects,objects);
}
void sigils_dual() {
    sigils_render(true);const int top=objects;sigils_render(false);
    maximum_shared_objects=std::max(maximum_shared_objects,top+objects);CHECK(top+objects<8192);
}
int sigils_wave_count(u32 color) {
    CHECK(width==320);int n=0;
    for(int y=61;y<150;++y)for(int x=12;x<308;++x)n+=pixels[y*320+x]==color;
    return n;
}
std::vector<uint8_t> sigils_ink_mask() {
    std::vector<uint8_t> result;
    for(int y=61;y<150;++y)for(int x=12;x<308;++x)result.push_back(pixels[y*320+x]==m::ink());
    return result;
}
void test_sigils_modes_sources() {
    SigilsFixture f;
    for(int index=0;index<4;++index) {
        const int dirty=mutations;f.tab(index);CHECK(mutations==dirty&&g_overlay==Overlay::None);
        for(int mode=0;mode<6;++mode) {
            f.s().mode=(SigilMode)((mode+1)%6);const auto before=sigils_table(f.s());const int changed=mutations;
            f.mode(mode);CHECK(mutations==changed+1&&sigils_table(f.s())==before&&g_overlay==Overlay::None);
            f.mode(mode);CHECK(mutations==changed+1);f.capture();sigils_dual();
        }
        // None -> strictly earlier sources -> ENV -> None; self/forward never appear.
        f.engine->set_sigil_mod(index,ModSource::None,4321);
        std::vector<ModSource> expected;
        for(int earlier=0;earlier<index;++earlier)expected.push_back(sigil_source(earlier));
        expected.push_back(ModSource::EnvFollow);expected.push_back(ModSource::None);
        for(auto source:expected) {
            const int changed=mutations;f.tap(80,201);
            CHECK(f.engine->sigil_mods[index].src==source&&f.engine->sigil_mods[index].depth==4321);
            CHECK(mutations==changed+1&&g_overlay==Overlay::None);
        }
    }
    // Every native x along direct mode/tab edges, including the exclusive right
    // and bottom edges. No modal tools path survives in either header band.
    f.tab(0);
    for(int y:{33,34,43,58,59})for(int x=0;x<320;++x) {
        f.s().mode=SigilMode::Follow;const int before=mutations;
        int expected=5;
        if(y>=34&&y<59&&x>=10&&x<310)expected=x<58?0:x<106?1:x<154?2:x<202?3:x<258?4:5;
        f.tap(x,y);CHECK((int)f.s().mode==expected&&mutations==before+(expected!=5));
        CHECK(g_overlay==Overlay::None);
    }
    for(int y:{4,5,18,31,32})for(int x=0;x<320;++x) {
        g_sel_sigil=0;const int before=mutations;int expected=0;
        for(int i=0;i<4;++i)if(x>=119+31*i&&x<147+31*i&&y>=5&&y<32)expected=i;
        f.tap(x,y);CHECK(g_sel_sigil==expected&&mutations==before&&g_overlay==Overlay::None);
    }
}
void test_sigils_timing() {
    SigilsFixture f;f.mode(0);
    // Selecting each value edge is silent; origin is stored, not touched x.
    for(int x:{78,100,176,183})for(int y:{152,159,173}) {
        f.engine->set_sigil_rate(0,8192);const int dirty=mutations;
        f.down(x,y);f.drag(x,y);CHECK(f.engine->sigil_rate(0)==8192&&mutations==dirty);
        f.drag(x+10,y);CHECK(f.engine->sigil_rate(0)>8192);
        const int moved=mutations;f.drag(x+10,y);CHECK(mutations==moved);
        f.drag(x,y);CHECK(f.engine->sigil_rate(0)==8192);f.lift();
        f.drag(319,y);CHECK(f.engine->sigil_rate(0)==8192);
    }
    for(int x:{12,20,60,98,176,183}) {
        f.tap(x,182);const int expected=(int)std::lround(std::clamp(x-20,0,156)*32767.0/156);
        CHECK(std::abs((int)f.engine->sigil_rate(0)-expected)<=1);
    }
    f.down(98,182);f.drag(-999,182);CHECK(f.engine->sigil_rate(0)==0);
    f.drag(999,182);CHECK(f.engine->sigil_rate(0)==32767);f.lift();
    f.engine->set_sigil_rate(0,12345);f.tap(20,159);CHECK(f.engine->sigil_sync[0]);
    for(int div=0;div<6;++div) {
        f.tap(20+(int)std::lround(156.0*div/5),182);
        CHECK(f.engine->sigil_div[0]==div&&f.engine->sigil_rate(0)==12345);
        f.capture();sigils_dual();
    }
    f.engine->set_sigil_div(0,2);const int dirty=mutations;
    f.down(120,159);f.drag(120,159);CHECK(f.engine->sigil_div[0]==2&&mutations==dirty);
    f.drag(180,159);CHECK(f.engine->sigil_div[0]>2&&f.engine->sigil_rate(0)==12345);
    f.drag(120,159);CHECK(f.engine->sigil_div[0]==2);f.lift();
    f.down(98,182);f.drag(-999,182);CHECK(f.engine->sigil_div[0]==0);
    f.drag(999,182);CHECK(f.engine->sigil_div[0]==5&&f.engine->sigil_rate(0)==12345);f.lift();
    f.tap(20,159);CHECK(!f.engine->sigil_sync[0]&&f.engine->sigil_rate(0)==12345);
    // Toggle hit is exactly half-open. Adjacent numeric hit is not a toggle.
    for(int y:{151,152,159,173,174})for(int x:{11,12,75,76,77,78}) {
        f.engine->set_sigil_sync(0,false);f.tap(x,y);
        CHECK(f.engine->sigil_sync[0]==(x>=12&&x<76&&y>=152&&y<174));
    }
    f.engine->set_sigil_rate(0,12345);f.engine->set_sigil_div(0,3);f.mode(5);
    const bool sync=f.engine->sigil_sync[0];const int held=mutations;
    for(auto xy:{std::pair<int,int>{20,159},{120,159},{20,182},{176,182}}) {
        f.down(xy.first,xy.second);f.drag(319,182);f.lift();
    }
    CHECK(f.engine->sigil_sync[0]==sync&&f.engine->sigil_div[0]==3&&f.engine->sigil_rate(0)==12345&&mutations==held);
}
void test_sigils_bipolar() {
    SigilsFixture f;
    for(int y:{152,159,173})for(int x:{198,250,307}) {
        f.s().depth=1234;const int dirty=mutations;
        f.down(x,y);f.drag(x,y);CHECK(f.s().depth==1234&&mutations==dirty);
        f.drag(x+8,y);CHECK(f.s().depth>1234);f.drag(x,y);CHECK(f.s().depth==1234);f.lift();
    }
    for(int x:{208,254,300}) {f.tap(x,182);CHECK(f.s().depth==(x==208?-32767:x==254?0:32767));}
    f.down(254,182);f.drag(-999,182);CHECK(f.s().depth==-32767);
    f.drag(999,182);CHECK(f.s().depth==32767);f.lift();
    for(int mode:{0,5})for(bool sync:{false,true})for(auto source:{ModSource::None,ModSource::EnvFollow}) {
        f.mode(mode);f.engine->set_sigil_sync(0,sync);f.engine->set_sigil_mod(0,source,1234);
        const int dirty=mutations;f.down(145,207);f.drag(145,207);
        CHECK(f.engine->sigil_mods[0].depth==1234&&mutations==dirty);
        f.drag(153,207);CHECK(f.engine->sigil_mods[0].depth>1234);
        f.drag(145,207);CHECK(f.engine->sigil_mods[0].depth==1234);f.lift();
        for(int x:{190,245,300}) {
            f.tap(x,207);CHECK(f.engine->sigil_mods[0].depth==(x==190?-32767:x==245?0:32767));
            CHECK(f.engine->sigil_mods[0].src==source);f.capture();sigils_dual();
        }
        f.down(245,207);f.drag(-999,207);CHECK(f.engine->sigil_mods[0].depth==-32767);
        f.drag(999,207);CHECK(f.engine->sigil_mods[0].depth==32767);f.lift();
    }
    // Exact native rail center is not -1; repeated zero taps are true no-ops.
    f.tap(254,182);f.tap(245,207);const int dirty=mutations;
    f.tap(254,182);f.tap(245,207);CHECK(mutations==dirty&&f.s().depth==0&&f.engine->sigil_mods[0].depth==0);
}
void test_sigils_strokes() {
    SigilsFixture f;f.s().clear();const auto blank=sigils_table(f.s());const int dirty=mutations;
    f.down(20,67);CHECK(f.s().drawn&&f.s().table[0]==32767);
    f.drag(300,143);f.lift();
    Sigil expected;expected.set_point(0,32767);expected.stroke_to(0,32767,127,10922);
    CHECK(sigils_table(f.s())==sigils_table(expected));CHECK(mutations>dirty&&g_sigil_undo_valid[0]);
    const auto forward=sigils_table(f.s());
    f.tap(285,18);CHECK(sigils_table(f.s())==blank&&!f.s().drawn);
    f.tap(285,18);CHECK(sigils_table(f.s())==forward&&f.s().drawn);
    // Backward interpolation and per-stroke smoothing reset use the SAME real
    // engine primitive as the UI, but expected endpoints are independently known.
    f.down(300,143);f.drag(20,67);f.lift();
    expected=Sigil{};std::copy(forward.begin(),forward.end(),std::begin(expected.table));
    expected.set_point(127,-32767);expected.stroke_to(127,-32767,0,-10922);
    CHECK(sigils_table(f.s())==sigils_table(expected));
    const auto backward=sigils_table(f.s());f.tap(285,18);CHECK(sigils_table(f.s())==forward);
    f.tap(285,18);CHECK(sigils_table(f.s())==backward);
    // Captured canvas owns clamped excursions outside the screen; never overruns.
    f.down(12,61);f.drag(900,-900);f.drag(-900,900);f.lift();
    CHECK(f.s().drawn);for(q15 v:f.s().table)CHECK(v>=-32767&&v<=32767);
    f.s().clear();f.s().drawn=true;f.down(160,105);f.lift();const int flat_dirty=mutations;
    f.down(160,105);f.drag(160,105);f.lift();CHECK(mutations==flat_dirty);
    const auto stored=sigils_table(f.s());f.drag(319,0);CHECK(sigils_table(f.s())==stored);
    // Undo belongs to its selected source, including originally undrawn state.
    f.tab(1);f.s().clear();f.down(20,67);f.lift();f.tap(285,18);CHECK(!f.s().drawn);
    f.tab(0);CHECK(sigils_table(f.s())==stored);
    // Exercise the actual serializer on an isolated temporary path. The existing
    // format infers drawn from nonzero table points; it stores no separate flag.
    // Use nonzero drawn A and undone blank B without changing that legacy rule.
    f.down(20,67);f.lift();const auto persisted=sigils_table(f.s());
    struct TempPatch {
        char path[64]="/tmp/seance-sigils-flags-XXXXXX";
        TempPatch(){const int fd=mkstemp(path);CHECK(fd>=0);close(fd);}
        ~TempPatch(){std::remove(path);std::remove((std::string(path)+".tmp").c_str());}
    } saved;
    CHECK(patchio::save(*f.engine,saved.path));auto loaded=std::make_unique<Engine>();
    CHECK(patchio::load(*loaded,saved.path));
    CHECK(loaded->sigils[0].drawn&&!loaded->sigils[1].drawn);
    CHECK(sigils_table(loaded->sigils[0])==persisted);
    CHECK(sigils_table(loaded->sigils[1])==sigils_table(f.engine->sigils[1]));
}
void test_sigils_routing_follow() {
    SigilsFixture f;SampleFixture samples;
    // Empty selection is inert: neither route metadata nor dirty marker changes.
    g_sel_chain=3;g_sel_slot=5;const int dirty=mutations;const int old_dest=f.engine->step_dest[0];
    f.tap(40,230);CHECK(mutations==dirty&&f.engine->step_dest[0]==old_dest);
    for(int chain=0;chain<4;++chain)for(int index=0;index<4;++index) {
        f.tab(index);g_sel_chain=chain;g_sel_slot=0;
        auto& p=f.engine->chains[chain].units[0]->params[0];p.src=ModSource::None;p.mod_depth=0;
        f.tap(40,230);CHECK(p.src==sigil_source(index)&&p.mod_depth==(p.bipolar?8000:12000));
        CHECK(f.engine->step_dest[index]==chain);
        const int routed=mutations;f.tap(40,230);CHECK(mutations==routed);
        p.mod_depth=-4321;f.tap(40,230);CHECK(p.mod_depth==-4321&&mutations==routed);
    }
    f.tab(0);
    for(int mode=0;mode<5;++mode) {
        f.mode(mode);f.s().phase=0x70000000u;f.s().running=false;const int before=mutations;
        f.tap(285,230);CHECK(f.s().phase==0&&f.s().running&&mutations==before);
    }
    f.mode(5);f.engine->set_step_dest(0,2);f.engine->transport_playing=true;
    f.engine->launch_quantize=LaunchQuantize::Beat;f.engine->pending_triggers=0;
    const int before=mutations;f.tap(285,230);
    CHECK(f.engine->pending_triggers==(1u<<2)&&mutations==before);
    f.engine->transport_playing=false;f.engine->pending_triggers=0;
    f.engine->launch_quantize=LaunchQuantize::Immediate;
    auto* play=static_cast<PlayUnit*>(f.engine->chains[2].units[0]);play->sample_slot=63;
    for(auto& p:play->params)p.snap();
    f.tap(285,230);f.audio();
    CHECK(play->playback_active()&&f.s().follow_active);f.capture();sigils_render(false);CHECK(sigils_wave_count(m::spot())>0);
    // UI structural/mute/bypass edges can precede the next audio control tick.
    // The cached output stays truthful s.value, but no stale PLAY marker survives.
    play->bypass=true;CHECK(f.s().follow_active);f.capture();sigils_render(false);CHECK(sigils_wave_count(m::spot())==0);
    play->bypass=false;f.engine->chains[2].mute=true;f.capture();sigils_render(false);CHECK(sigils_wave_count(m::spot())==0);
    f.engine->chains[2].mute=false;f.capture();sigils_render(false);CHECK(sigils_wave_count(m::spot())>0);
    delete f.engine->replace_unit(2,0,nullptr);f.capture();sigils_render(false);CHECK(sigils_wave_count(m::spot())==0);
    play=new PlayUnit;play->sample_slot=63;delete f.engine->replace_unit(2,0,play);
    f.capture();sigils_render(false);CHECK(sigils_wave_count(m::spot())==0);
    f.tap(285,230);f.audio();CHECK(play->playback_active()&&f.s().follow_active);
    f.engine->chains[2].mute=true;f.audio();CHECK(!f.s().follow_active&&f.s().value==0);
    f.capture();sigils_render(false);CHECK(sigils_wave_count(m::spot())==0);
    f.engine->chains[2].mute=false;play->bypass=true;f.audio();CHECK(!f.s().follow_active);
    f.engine->set_step_dest(0,0);f.audio();CHECK(!f.s().follow_active&&f.s().value==0);
    f.capture();sigils_render(false);CHECK(sigils_wave_count(m::spot())==0);
    // A stopped first PLAY wins over a playing later one, not a search for a
    // convenient synthetic cursor. First non-bypassed PLAY follows core semantics.
    play=new PlayUnit;play->sample_slot=63;delete f.engine->replace_unit(2,0,play);
    auto* second=new PlayUnit;second->sample_slot=63;delete f.engine->replace_unit(2,1,second);
    for(auto& p:second->params)p.snap();
    second->control_tick();second->trigger();
    f.engine->set_step_dest(0,2);f.audio();CHECK(second->playback_active()&&!play->playback_active()&&!f.s().follow_active);
    f.capture();sigils_render(false);CHECK(sigils_wave_count(m::spot())==0);
}
void test_sigils_ownership() {
    SigilsFixture f;
    for(int kind=0;kind<4;++kind)for(int cancel=0;cancel<6;++cancel) {
        g_view=View::Sigils;g_overlay=Overlay::None;g_sel_sigil=0;f.s().depth=1234;
        f.engine->set_sigil_rate(0,8192);f.engine->set_sigil_mod(0,ModSource::None,1234);
        static const int xs[]={100,260,145,100},ys[]={159,159,207,100};
        f.down(xs[kind],ys[kind]);
        const auto first=sigils_table(f.s()),second=sigils_table(f.engine->sigils[1]);
        const q15 depth=f.s().depth,mod=f.engine->sigil_mods[0].depth,rate=f.engine->sigil_rate(0);
        if(cancel==0)f.lift();
        if(cancel==1)sigils_close_mode_menu();
        if(cancel==2)g_view=View::Chains;
        if(cancel==3)g_overlay=Overlay::Editor;
        if(cancel==4)g_sel_sigil=1;
        if(cancel==5){g_view=View::Chains;f.capture();g_view=View::Sigils;}
        const int dirty=mutations;f.drag(319,0);f.lift();
        CHECK(sigils_table(f.engine->sigils[0])==first&&sigils_table(f.engine->sigils[1])==second);
        CHECK(f.engine->sigils[0].depth==depth&&f.engine->sigil_mods[0].depth==mod&&f.engine->sigil_rate(0)==rate);
        CHECK(mutations==dirty&&g_drag==Drag::None);
    }
    g_view=View::Sigils;g_overlay=Overlay::None;g_sel_sigil=0;
    f.down(100,100);const auto before=sigils_table(f.s());f.down(190,230);
    CHECK(g_view==View::Sigils);f.lift();f.tap(190,230);CHECK(g_view==View::Chains);
    f.drag(319,0);CHECK(sigils_table(f.s())==before&&g_drag==Drag::None);
    // Touch-down while an overlay or another view owns input is entirely inert.
    for(bool overlay:{false,true}) {
        g_view=overlay?View::Sigils:View::Chains;g_overlay=overlay?Overlay::Editor:Overlay::None;
        const int dirty=mutations;const auto table=sigils_table(f.s());
        for(auto xy:{std::pair<int,int>{20,67},{82,43},{20,159},{254,182},{80,201},{245,207},{285,230}})f.tap(xy.first,xy.second);
        CHECK(mutations==dirty&&sigils_table(f.s())==table);
    }
    // Successful project replacement invalidates undo/gesture/frame without
    // touching the newly installed table or creating a spurious dirty revision.
    g_view=View::Sigils;g_overlay=Overlay::None;g_sel_sigil=0;
    f.down(100,100);const auto loaded=sigils_table(f.s());
    std::fill(std::begin(g_sigil_undo_valid),std::end(g_sigil_undo_valid),true);
    const int dirty=mutations;sigils_forget_undo();
    CHECK(g_drag==Drag::None&&mutations==dirty);
    CHECK(std::none_of(std::begin(g_sigil_undo_valid),std::end(g_sigil_undo_valid),[](bool valid){return valid;}));
    f.drag(319,0);f.tap(285,18);CHECK(sigils_table(f.s())==loaded&&mutations==dirty);
    sigils_render(true);CHECK(texts.empty());sigils_render(false);CHECK(texts.empty());
}
void test_sigils_thread() {
    SigilsFixture f;f.capture();sigils_render(false);const auto ink=sigils_ink_mask();
    CHECK(sigils_wave_count(m::ink())>250&&sigils_wave_count(m::spot())>24);
    for(int depth=-99;depth<=99;++depth) {
        f.s().depth=sigils_percent(depth);f.capture();sigils_render(false);
        CHECK(sigils_ink_mask()==ink); // stored full-scale INK never changes with output depth
    }
    f.s().depth=0;f.capture();sigils_render(false);int flat=0;
    for(int x=20;x<=300;++x)flat+=pixels[105*320+x]==m::branch();
    CHECK(flat>230);
    for(int y:{67,143})for(int x=20;x<=300;++x)CHECK(pixels[y*320+x]==m::branch());
    // Independently known constant table: one-pixel column intervals, hollow
    // radius-three comet ring, a 5x1 bottom tick, and no full-height playhead.
    for(auto& v:f.s().table)v=16000;
    f.s().depth=32767;f.s().phase=0x80000000u;
    f.capture();sigils_render(false);
    int wave_y=-1;for(int y=68;y<143;++y)if(pixels[y*320+30]==m::ink()) {CHECK(wave_y==-1);wave_y=y;}
    CHECK(wave_y>=68&&wave_y<105);
    for(int x=25;x<125;++x)for(int y=68;y<143;++y)CHECK((pixels[y*320+x]==m::ink())==(y==wave_y));
    int tick_center=-1,tick_count=0;
    for(int x=12;x<308;++x)if(pixels[147*320+x]==m::spot()){tick_center=x-2;++tick_count;}
    CHECK(tick_count==5&&tick_center>=158&&tick_center<=161);
    CHECK(pixels[(wave_y-3)*320+tick_center]==m::spot());
    CHECK(pixels[(wave_y-1)*320+tick_center]==m::paper_color(wave_y-1)); // hollow interior
    CHECK(pixels[(wave_y+1)*320+tick_center]==m::paper_color(wave_y+1));
    // Exactly 24 screen pixels of trailing intervals, excluding the hollow ring
    // which extends three pixels ahead. No widened trace masquerading as trail.
    int trail_start=320;
    for(int x=20;x<tick_center-3;++x)if(pixels[wave_y*320+x]==m::spot())trail_start=std::min(trail_start,x);
    CHECK(tick_center-trail_start==24);
    for(int x=20;x<=300;++x) {
        int count=0;for(int y=64;y<=147;++y)count+=pixels[y*320+x]==m::spot();CHECK(count<20);
    }
    // Real Steps source is exactly table[bucket*8+4], not resampling a preview.
    for(int i=0;i<SIGIL_POINTS;++i)f.s().table[i]=(q15)(i*400-24000);
    f.mode(2);f.s().phase_inc=0;
    for(int bucket=0;bucket<16;++bucket) {
        f.s().phase=(uint32_t)(((uint64_t)bucket<<32)/16+0x01000000u);
        CHECK(f.s().step()==(q15)(((int32_t)f.s().table[bucket*8+4]*f.s().depth)>>15));
    }
    f.s().phase=0x9eb851ebu;f.capture();sigils_render(false);
    for(int bucket=0;bucket<16;++bucket) {
        const int x=20+bucket*280/16+3;
        const int yy=105-(int)std::lround(f.s().table[bucket*8+4]*38.0/32767);
        CHECK(pixels[yy*320+x]==m::spot());
    }
    for(int mode:{3,4,5}) {
        f.mode(mode);f.s().follow_active=false;f.s().value=0;f.capture();sigils_render(false);
        CHECK(sigils_wave_count(m::spot())==0);
        if(mode!=5)CHECK(sigils_wave_count(m::ink())==0&&has_text("stored shape / random ignores it"));
    }
    f.mode(1);f.s().running=false;f.capture();sigils_render(false);CHECK(sigils_wave_count(m::spot())==0);
    f.mode(0);f.s().drawn=false;f.capture();sigils_render(false);CHECK(sigils_wave_count(m::spot())==0);
}
void test_sigils_capture_extrema() {
    SigilsFixture f;g_patch_slot=7;f.capture();sigils_render(true);CHECK(has_text("patch01")&&!has_text("patch08"));
    const auto top=pixels;sigils_render(true,true);CHECK(pixels==top);sigils_render(false);const auto bottom=pixels;
    // Freeze every pointer-bearing and mutable input; even remove and delete the
    // selected target. BOTH screens must remain byte-exact until recapture.
    f.s().value=-30000;f.s().phase=0;f.s().depth=-32767;f.s().table[0]=-12345;
    f.engine->set_sigil_rate(0,32767);g_ui_snapshot={};g_sel_sigil=3;g_sel_chain=3;g_sel_slot=5;g_patch_slot=12;
    delete f.engine->replace_unit(0,0,nullptr);
    sigils_render(true);CHECK(pixels==top);sigils_render(false);CHECK(pixels==bottom);
    g_sel_sigil=g_sel_chain=g_sel_slot=0;delete f.engine->replace_unit(0,0,new FilterUnit);
    f.s().value=16000;f.capture();sigils_render(true);const auto positive=pixels;
    f.s().value=-16000;f.capture();sigils_render(true);CHECK(pixels!=positive);
    // Modulated live increment is captured independently of stored free rate.
    f.down(120,159);f.s().phase_inc=1000;f.capture();sigils_render(true);const auto slow=pixels;
    f.s().phase_inc=0x40000000u;f.capture();sigils_render(true);CHECK(pixels!=slow);f.lift();
    // Every legal source/mode/sync tuple, all six divisions, signed endpoints,
    // and each phase edge fit the true shared object budget and label geometry.
    for(int index=0;index<4;++index) {
        f.tab(index);f.authored();
        std::vector<ModSource> sources={ModSource::None,ModSource::EnvFollow};
        for(int earlier=0;earlier<index;++earlier)sources.push_back(sigil_source(earlier));
        for(int mode=0;mode<6;++mode)for(bool sync:{false,true})for(auto src:sources) {
            f.mode(mode);f.engine->set_sigil_sync(index,sync);f.engine->set_sigil_mod(index,src,-32767);
            f.s().depth=-32767;f.s().value=-32768;f.capture();sigils_dual();
        }
        for(int div=0;div<6;++div) {
            f.mode(0);f.engine->set_sigil_sync(index,true);f.engine->set_sigil_div(index,div);
            f.down(120,159);f.capture();sigils_dual();f.lift();
        }
        for(int mode=0;mode<6;++mode)for(uint32_t phase:{0u,1u,0x028f5c28u,0x80000000u,0xfd70a3d7u,0xffffffffu}) {
            f.mode(mode);f.s().phase=phase;f.s().depth=32767;f.s().value=32767;f.capture();sigils_dual();
        }
    }
    // All installed kinds/last slot and destination overflow, not just FILTER.
    f.tab(3);
    for(int c=0;c<4;++c)for(int slot=0;slot<6;++slot) {
        delete f.engine->replace_unit(c,slot,new FmUnit);
        auto* u=f.engine->chains[c].units[slot];for(int p=0;p<u->num_params;++p){u->params[p].src=ModSource::SigilD;u->params[p].mod_depth=-32768;}
    }
    g_sel_chain=3;g_sel_slot=5;f.mode(0);f.capture();sigils_render(true);
    // FM gained two pitch-envelope controls after this fixture was authored.
    // Count the explicitly assigned current controls, not the old ten-param FM.
    const int assigned = NUM_CHAINS * UNITS_PER_CHAIN * FmUnit::P_COUNT;
    const auto has_routes = [](int count) {
        const std::string total = std::to_string(count);
        const std::string overflow = "+" + std::to_string(count - 3) + " / y chains";
        return has_text(total.c_str()) && has_text(overflow.c_str());
    };
    CHECK(has_text("destinations") && has_routes(assigned));
    CHECK(has_text("assign target / c4 fm ptch"));
    for(int p=0;p<3;++p) {
        std::string expected=f.engine->chains[0].units[0]->params[p].name;
        for(auto& c:expected)if(c>='A'&&c<='Z')c=char(c-'A'+'a');
        CHECK(std::any_of(texts.begin(),texts.end(),[&](const TextRecord& t){
            return t.text==expected&&t.call_x==217&&t.call_y==115+28*p;
        }));
    }
    sigils_dual();
    // Clock routes are real global destinations, prioritised without fabricating
    // a chain/module. Assigned zero depth remains discoverable as a saved route.
    f.engine->set_clock_mod(ClockTarget::Tempo,ModSource::SigilD,12);
    f.engine->set_clock_mod(ClockTarget::Swing,ModSource::SigilD,0);
    f.capture();sigils_render(true);
    CHECK(has_routes(assigned + 2)&&has_text("project clock")&&has_text("tempo")&&has_text("swing"));
    const auto clock_top=pixels;sigils_render(true,true);CHECK(pixels==clock_top);sigils_dual();
    f.engine->set_clock_mod(ClockTarget::Tempo,ModSource::None,0);
    f.engine->set_clock_mod(ClockTarget::Swing,ModSource::None,0);
    auto& hidden=f.engine->chains[0].units[0]->params[0];hidden.mod_depth=0;
    f.capture();sigils_render(true);CHECK(has_routes(assigned - 1));
    hidden.mod_depth=-32768;
    for(int kind=0;kind<(int)UnitKind::Count;++kind) {
        Unit* u=make_unit((UnitKind)kind);if(!u)continue;
        delete f.engine->replace_unit(3,5,u);f.capture();sigils_dual();
    }
    // Invalid UI selections and loss of engine/audio invalidate the next capture
    // to paper only; no dereference or accidental old-sigil inspector survives.
    for(int invalid:{-1,4,999}) {
        g_sel_sigil=invalid;f.capture();sigils_render(true);CHECK(texts.empty());
        sigils_render(false);CHECK(texts.empty());
    }
    g_sel_sigil=0;auto* engine=g_eng;auto* audio=g_audio;g_eng=nullptr;g_audio=nullptr;
    f.capture();sigils_render(true);CHECK(texts.empty());sigils_render(false);CHECK(texts.empty());
    g_eng=engine;g_audio=audio;
}
void dump_sigils_menu(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);SigilsFixture f;SampleFixture samples;
    std::ofstream manifest(directory/"manifest.tsv");
    manifest<<"frame\tsigil\tmode\tdepth_q15\tvalue_q15\tphase\ttop_objects\tbottom_objects\tshared_objects\n";
    auto save=[&](const std::string& name) {
        f.capture();sigils_render(true);const int top=objects;dump_surface(directory,name+"_top");
        sigils_render(false);const int bottom=objects;dump_surface(directory,name+"_bottom");
        CHECK(top+bottom<8192);maximum_shared_objects=std::max(maximum_shared_objects,top+bottom);
        manifest<<name<<'\t'<<g_sel_sigil<<'\t'<<(int)f.s().mode<<'\t'<<f.s().depth<<'\t'<<f.s().value<<'\t'<<f.s().phase<<'\t'<<top<<'\t'<<bottom<<'\t'<<top+bottom<<'\n';
    };
    f.tap(40,230);save("main");
    f.s().depth=sigils_percent(48);save("depth_pos48");f.s().depth=sigils_percent(-64);save("depth_neg64");
    f.s().depth=0;save("depth_zero");f.s().depth=32767;
    for(int mode=0;mode<6;++mode) {
        f.mode(mode);f.s().random_from=-6000;f.s().random_to=12000;
        const uint32_t inc=f.s().phase_inc;f.s().phase_inc=0;f.s().step();f.s().phase_inc=inc;
        static const char* names[]={"mode_loop","mode_shot","mode_step","mode_rand","mode_smooth","follow_no_play"};save(names[mode]);
    }
    f.mode(1);f.s().running=false;f.s().value=0;save("shot_stopped");
    f.mode(0);f.s().clear();f.s().value=0;save("undrawn");f.authored();
    for(int div=0;div<6;++div) {
        f.engine->set_sigil_sync(0,true);f.engine->set_sigil_div(0,div);f.down(120,159);save("sync_"+std::to_string(div));f.lift();
    }
    f.engine->set_sigil_sync(0,false);
    for(int raw:{0,8192,32767}) {f.engine->set_sigil_rate(0,(q15)raw);f.down(120,159);save("rate_"+std::to_string(raw));f.lift();}
    f.s().depth=sigils_percent(-64);f.down(275,159);save("focus_depth");f.lift();f.s().depth=32767;
    for(int index=0;index<4;++index) {
        f.tab(index);f.authored();f.engine->set_sigil_mod(index,index?ModSource::SigilA:ModSource::EnvFollow,sigils_percent(24));
        f.down(145,207);save("source_"+std::to_string(index));f.lift();
    }
    f.tab(3);f.engine->set_sigil_mod(3,ModSource::SigilC,-32767);f.down(145,207);save("mod_negative");f.lift();
    f.engine->set_sigil_sync(3,true);f.down(145,207);save("mod_paused_sync");f.lift();
    f.mode(5);f.down(145,207);save("mod_paused_follow");f.lift();
    f.tab(0);f.mode(5);auto* play=static_cast<PlayUnit*>(f.engine->chains[2].units[0]);play->sample_slot=63;
    f.engine->set_step_dest(0,2);for(auto& p:play->params)p.snap();f.tap(285,230);f.audio(12);save("follow_active");
    f.engine->chains[2].mute=true;f.audio();save("follow_muted");f.engine->chains[2].mute=false;
    f.mode(0);g_sel_chain=3;g_sel_slot=5;save("empty_target");
    for(int c=0;c<4;++c)for(int slot=0;slot<6;++slot) {
        delete f.engine->replace_unit(c,slot,new FmUnit);
        for(auto& p:static_cast<FmUnit*>(f.engine->chains[c].units[slot])->params){p.src=ModSource::SigilA;p.mod_depth=-32768;}
    }
    save("routes_overflow");
    for(int mode=0;mode<6;++mode)for(uint32_t phase:{0u,0xffffffffu}) {
        f.mode(mode);f.s().phase=phase;save("edge_"+std::to_string(mode)+(phase?"_end":"_start"));
    }
    std::ofstream notice(directory/"README.txt");
    notice<<"real c++ single-surface Sigils / thread-v2 runtime software captures.\n"
          <<"authored 128-point Q15 table and explicit value/phase test states, not live hardware telemetry.\n"
          <<"follow_active uses the real engine and a deterministic 32000-frame sample fixture.\n"
          <<"both draw paths run with g_eng/g_audio null after capture; bounds, native text overlap,\n"
          <<"integer paper/four inks and shared 8192-object budget asserted. no Python UI redraw.\n"
          <<"Citro2D line coverage is software approximate. no LCD, hardware timing/audio acceptance, FTP or CIA.\n";
}
