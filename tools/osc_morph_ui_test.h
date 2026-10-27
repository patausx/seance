// Included by the real native raster harness: actual OSC morph controller and draws.
void osc_geometry(int top=0){CHECK(objects>0&&top+objects<=8192&&outside_rects==0&&outside_lines==0&&text_overlaps==0);for(const auto&t:texts)CHECK(t.x>=0&&t.y>=0&&t.x+t.w<=width&&t.y+t.h<=HEIGHT);maximum_objects=std::max(maximum_objects,objects);maximum_shared_objects=std::max(maximum_shared_objects,top+objects);}
struct OscFixture:Fixture{
    OscUnit*osc=nullptr;
    OscFixture(){osc=new OscUnit;osc->clean=false;delete engine->replace_unit(0,0,osc);g_edit_chain=g_edit_slot=0;g_view=View::Chains;g_overlay=Overlay::Editor;param_lens_cancel();osc_focus_release();osc_focus_select(OscUnit::P_SHAPE);capture();}
    ~OscFixture(){g_overlay=Overlay::None;osc_focus_capture();}
    void capture(){engine->capture_ui_snapshot(g_ui_snapshot,0,0);param_lens_capture();}
    void lower(){module_inspector_surface(false);{ModuleInspectorNoEngine frozen;osc_focus_draw(*osc);}osc_geometry();}
    void pair(){capture();module_inspector_top(false,true);const int top=objects;lower();osc_geometry(top);}
    void touch(int x,int y){CHECK(osc_focus_touch_body(*osc,x,y));}
    void drag(int x,int y){osc_focus_drag(*osc,x,y);}void up(){osc_focus_release();g_drag=Drag::None;}
    void pick(int p){osc_focus_select(p);}void snap(){for(int p=0;p<OscUnit::P_COUNT;++p)osc->params[p].snap();osc->control_tick();}
    std::array<q15,OscUnit::P_COUNT>bases(){std::array<q15,OscUnit::P_COUNT>v{};for(int p=0;p<OscUnit::P_COUNT;++p)v[p]=osc->params[p].base;return v;}
    static int index(int p){static const int params[]={OscUnit::P_PITCH,OscUnit::P_PW,OscUnit::P_FOLD,OscUnit::P_SUB,OscUnit::P_DRIFT,OscUnit::P_GAIN};for(int i=0;i<6;++i)if(params[i]==p)return i;return -1;}
    static std::pair<int,int>point(int p){if(p==OscUnit::P_SHAPE)return {153,148};const int i=index(p);return {14+(i%3)*104+42,166+(i/3)*31+18};}
    void open(){CHECK(param_lens_touch_down(250,232));param_lens_touch_release();CHECK(param_lens_is_open());}
};
void test_osc_controls_conversion(){OscFixture f;auto&u=*f.osc;CHECK(u.num_params==OscUnit::P_COUNT&&u.shape_morph);const auto original=f.bases();
    for(int p=0;p<OscUnit::P_COUNT;++p){f.pick(p);const auto xy=OscFixture::point(p);u.params[p].base=10000;auto before=f.bases();f.touch(xy.first,xy.second);CHECK(f.bases()==before);f.drag(xy.first+7,xy.second);before[p]=q15(10000+7*32767/(p==OscUnit::P_SHAPE?278:84));CHECK(f.bases()==before);f.drag(xy.first+2000,xy.second);CHECK(u.params[p].base==32767);f.drag(xy.first-2000,xy.second);CHECK(u.params[p].base==0);f.up();}
    for(int p=0;p<OscUnit::P_COUNT;++p)u.params[p].base=original[p];
    u.shape_morph=false;u.wave=OscUnit::Wave::Saw;u.params[OscUnit::P_SHAPE].base=31337;f.touch(80,145);CHECK(u.shape_morph&&u.params[OscUnit::P_SHAPE].base==21845);f.up();
    f.touch(230,9);CHECK(u.bypass);f.touch(230,9);CHECK(!u.bypass);f.touch(305,9);CHECK(u.pitch_quantize);f.touch(305,9);CHECK(!u.pitch_quantize);
    const auto before=f.bases();const int dirty=mutations;for(auto xy:{std::pair<int,int>{160,100},{250,100},{160,127}}){f.touch(xy.first,xy.second);f.drag(xy.first+20,xy.second);f.up();CHECK(f.bases()==before&&mutations==dirty);}
}
void test_osc_locks_lens(){OscFixture f;auto&u=*f.osc;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;g_view=View::Sequencer;const auto base=f.bases();
    for(int p=0;p<OscUnit::P_COUNT;++p){g_seq_lock_step=p;f.pick(p);const auto xy=OscFixture::point(p);CHECK(f.engine->set_step_lock(0,p,0,p,5000));f.touch(xy.first,xy.second);f.drag(xy.first+7,xy.second);CHECK(seq_display_value(0,p,base[p])==5000+7*32767/(p==OscUnit::P_SHAPE?278:84)&&f.bases()==base);f.up();}
    g_seq_lock_step=7;
    u.shape_morph=false;u.wave=OscUnit::Wave::Pulse;const q15 dormant=u.params[OscUnit::P_SHAPE].base;f.pick(OscUnit::P_SHAPE);f.touch(100,145);CHECK(!u.shape_morph&&u.params[OscUnit::P_SHAPE].base==dormant);f.up();
    g_seq_lock_armed=false;g_view=View::Chains;u.shape_morph=true;for(int p=0;p<OscUnit::P_COUNT;++p){u.params[p].base=13000;u.params[p].mod_depth=1000;u.params[p].snap();f.pick(p);f.open();CHECK(osc_focus_selected()==p);param_lens_touch_down(250,129);param_lens_touch_drag(256,129);param_lens_touch_release();CHECK(u.params[p].base==13000+6*32767/271);param_lens_touch_down(100,166);param_lens_touch_drag(106,166);param_lens_touch_release();CHECK(u.params[p].mod_depth==1000+6*65534/271);param_lens_touch_down(70,185);param_lens_touch_release();CHECK(u.params[p].src==ModSource::SigilA);lens_permanent_probe(u,p);CHECK(osc_focus_selected()==p);f.open();f.capture();module_inspector_top(true);const int top=objects;module_inspector_surface(false);{ModuleInspectorNoEngine frozen;param_lens_draw_bottom();}osc_geometry(top);CHECK(module_inspector_text("base ")&&!module_inspector_text("temp"));u32 down=KEY_B;param_lens_input(down,0,0,false);CHECK(!param_lens_is_open()&&osc_focus_selected()==p);}
}
std::vector<u32>osc_graph_crop(){std::vector<u32>v;for(int y=60;y<=124;++y)for(int x=8;x<=307;++x)v.push_back(pixels[y*320+x]);return v;}
void test_osc_model_frames(){OscFixture f;auto&u=*f.osc;char text[32];
    module_inspector_value_text(text,sizeof text,UnitKind::Osc,OscUnit::P_SHAPE,16384);CHECK(std::string(text)=="tri > saw 50%");module_inspector_value_text(text,sizeof text,UnitKind::Osc,OscUnit::P_PW,32767);CHECK(std::string(text)=="75.0%");module_inspector_value_text(text,sizeof text,UnitKind::Osc,OscUnit::P_FOLD,32767);CHECK(std::string(text)=="7.00x");
    u.params[OscUnit::P_SHAPE].base=21845;u.params[OscUnit::P_PW].base=0;f.snap();f.capture();f.lower();const auto saw=osc_graph_crop();u.params[OscUnit::P_PW].base=32767;f.snap();f.capture();f.lower();CHECK(osc_graph_crop()==saw);u.params[OscUnit::P_SHAPE].base=30000;f.snap();f.capture();f.lower();const auto pulse_a=osc_graph_crop();u.params[OscUnit::P_PW].base=0;f.snap();f.capture();f.lower();CHECK(osc_graph_crop()!=pulse_a);
    int pairs=0;for(int shape:{0,5431,10922,16384,21845,27306,32767}){u.params[OscUnit::P_SHAPE].base=shape;u.params[OscUnit::P_FOLD].base=q15((shape*3)%32768);u.params[OscUnit::P_SUB].base=q15(shape/2);f.snap();f.pick(OscUnit::P_SHAPE);f.capture();const auto top=module_inspector_top(false,true);const int n=objects;f.lower();osc_geometry(n);const auto bottom=pixels;for(auto&p:u.params)p.base=12345;g_ui_snapshot={};module_inspector_surface(false);{ModuleInspectorNoEngine frozen;osc_focus_draw(u);}CHECK(pixels==bottom&&module_inspector_top()==top);++pairs;}
    g_overlay=Overlay::None;f.capture();module_inspector_surface(false);osc_focus_draw(u);CHECK(texts.empty());std::printf("osc frames: %d frozen actual duals / width relevance / fixed reset model / budget\n",pairs);
}
void test_osc_clear_presentation(){
    OscFixture f;auto& u=*f.osc;char value[32];
    const int anchors[]={0,10922,21845,32767};const char* names[]={"sine","tri","saw","pulse"};
    for(int i=0;i<4;++i){module_inspector_value_text(value,sizeof value,UnitKind::Osc,OscUnit::P_SHAPE,anchors[i]);CHECK(std::string(value)==names[i]);}
    for(int p=0;p<OscUnit::P_COUNT;++p)for(int raw:{0,13000,32767}){
        u.params[p].base=raw;f.pick(p);f.snap();f.capture();module_inspector_top(false,true);const int top=objects;
        for(const char* retired:{"stored base","control ","target ","potential range","effective"})CHECK(!module_inspector_text(retired));
        CHECK(!module_inspector_text("live  "));
        for(const auto& t:texts)if(t.y>=70&&t.y<134)CHECK(t.scale==1); // no chunky 2x fallback
        f.lower();osc_geometry(top);CHECK(!module_inspector_text("partials"));
    }
    f.pick(OscUnit::P_SHAPE);u.params[OscUnit::P_SHAPE].base=18700;f.snap();
    u.params[OscUnit::P_SHAPE].src=ModSource::SigilA;u.params[OscUnit::P_SHAPE].mod_depth=18000;
    u.params[OscUnit::P_SHAPE].effective=23600;f.capture();module_inspector_top();
    module_inspector_value_text(value,sizeof value,UnitKind::Osc,OscUnit::P_SHAPE,18700);
    CHECK(std::string(value)=="tri > saw 71%"&&module_inspector_text("live  saw > pulse 16%")&&module_inspector_text("sg.a > shape  +54%"));
    const auto bases=f.bases();u.shape_morph=false;u.wave=OscUnit::Wave::Tri;f.capture();module_inspector_top();
    CHECK(module_inspector_text("legacy")&&module_inspector_text("morph off")&&!module_inspector_text("71%")&&f.bases()==bases&&!u.shape_morph);
    u.shape_morph=true;u.params[OscUnit::P_SHAPE].src=ModSource::None;u.params[OscUnit::P_SHAPE].base=0;
    f.pick(OscUnit::P_PITCH);f.snap();f.capture();f.lower();
    CHECK(module_inspector_text("width")); // all controls stay visibly editable even before pulse enters
    bool ink=false;for(int y=166;y<174;++y)for(int x=118;x<148;++x)ink|=pixels[y*320+x]==m::ink();CHECK(ink);
    std::printf("osc clear: 21 extrema pairs / semantic anchors / live route / legacy isolation\n");
}
void test_osc_clear_wave(){
    OscFixture f;auto& u=*f.osc;u.params[OscUnit::P_SHAPE].base=21845;
    u.params[OscUnit::P_FOLD].base=u.params[OscUnit::P_SUB].base=0;u.params[OscUnit::P_GAIN].base=32767;f.snap();f.capture();
    struct Capture{std::vector<GranLine> lines;Capture(){gran_lines=&lines;}~Capture(){gran_lines=nullptr;}} capture;
    f.lower();gran_lines=nullptr;
    int checked=0;
    for(int col=0;col<273;++col){int lo=32767,hi=-32768,outlo=32767,outhi=-32768;
        for(int probe=0;probe<8;++probe){const uint32_t phase=uint32_t(uint64_t(col*8+probe)*(uint64_t(1)<<33)/(273*8));
            const int raw=int(phase>>16)-32768;const int output=int((int64_t(int((int64_t(raw)*32767)>>15))*32767)>>15);
            lo=std::min(lo,raw);hi=std::max(hi,raw);outlo=std::min(outlo,output);outhi=std::max(outhi,output);}
        for(bool final:{false,true}){int found=0;for(const auto& line:capture.lines)if(line.x0==30+col&&line.x1==30+col&&line.y0>=64&&line.y1<=121&&line.color==(final?m::spot():m::ghost())){
            const int low=92-(final?outhi:hi)*28/32768,high=92-(final?outlo:lo)*28/32768;
            CHECK(line.y0==low&&line.y1==std::max(low+1,high));++found;}
            CHECK(found==1);++checked;}
    }
    std::printf("osc clear wave: %d exact saw extrema columns / full-width fixed-scale reset model\n",checked);
}
void test_osc_live_rails(){
    OscFixture f;auto& u=*f.osc;int cases=0;
    auto verify=[&](int p,int base,int live){
        const int i=OscFixture::index(p),x=p==OscUnit::P_SHAPE?14:14+(i%3)*104;
        const int y=OscFixture::point(p).second,w=p==OscUnit::P_SHAPE?278:84;
        const int bx=x+base*w/32767,lx=x+live*w/32767;
        // A solid live diamond even at rest/endpoints, independent of target or selection.
        for(int dy=-2;dy<=2;++dy)for(int dx=-(2-std::abs(dy));dx<=2-std::abs(dy);++dx)
            CHECK(pixels[(y+dy)*320+lx+dx]==m::spot());
        const u32 outline=osc_focus_selected()==p?m::spot():m::ink();
        CHECK(pixels[(y-4)*320+bx]==outline);
        for(int dx:{-4,-3,3,4})if(std::abs(bx+dx-lx)>2)CHECK(pixels[y*320+bx+dx]==outline);
        // Ordinary one-pixel track/ticks; the moving marker stays bold.
        int rail_pixels=0;for(int dx=0;dx<=w;++dx){const int at=x+dx;
            if(std::abs(at-bx)<=4||std::abs(at-lx)<=4)continue;
            CHECK(pixels[y*320+at]==m::branch());
            bool tick=dx==0||dx==w;
            if(p==OscUnit::P_SHAPE)for(int k=0;k<4;++k)tick|=dx==k*w/3;
            CHECK(pixels[(y-1)*320+at]==(tick?m::branch():m::paper_color(y-1)));++rail_pixels;}
        CHECK(rail_pixels>50);++cases;
    };
    for(int p=0;p<OscUnit::P_COUNT;++p)for(int base:{0,16384,32767})for(int live:{0,6000,16384,27000,32767}){
        auto& param=u.params[p];param.base=base;param.effective=live;param.target=q15(32767-live);
        f.pick((p+1)%OscUnit::P_COUNT);f.capture();f.lower();verify(p,base,live);
        const auto bottom=pixels;const int dirty=mutations;
        param.base=1234;param.effective=2345;g_ui_snapshot={};f.lower();CHECK(pixels==bottom&&mutations==dirty);
    }
    // Real parameter slew: fill follows effective, not unsmoothed target or a UI timer.
    for(int p=0;p<OscUnit::P_COUNT;++p){auto& param=u.params[p];param.base=16000;param.src=ModSource::SigilA;param.mod_depth=12000;
        param.snap();param.resolve(0);f.pick(p);const auto base=f.bases();const int dirty=mutations;
        for(int source:{-32767,-22000,0,12000,32767,0}){param.resolve(q15(source));
            CHECK(param.effective!=param.target);f.capture();f.lower();verify(p,16000,param.effective);
            CHECK(f.bases()==base&&mutations==dirty);}
    }
    // Lock outline remains the edited step, fill remains published live playback.
    g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;
    CHECK(f.engine->set_step_lock(0,7,0,OscUnit::P_GAIN,9000));u.params[OscUnit::P_GAIN].effective=26000;
    f.capture();f.lower();verify(OscUnit::P_GAIN,9000,26000);
    g_view=View::Chains;g_seq_lock_armed=false;
    u.shape_morph=false;u.wave=OscUnit::Wave::Saw;u.params[OscUnit::P_SHAPE].base=31337;u.params[OscUnit::P_SHAPE].effective=1234;
    const auto base=f.bases();f.capture();f.lower();verify(OscUnit::P_SHAPE,21845,21845);CHECK(!u.shape_morph&&f.bases()==base);
    // All 546 trace segments remain drawable even for flat pulse and zero gain.
    u.shape_morph=true;u.params[OscUnit::P_SHAPE].base=32767;u.params[OscUnit::P_GAIN].base=0;f.snap();f.capture();
    struct Capture{std::vector<GranLine> lines;Capture(){gran_lines=&lines;}~Capture(){gran_lines=nullptr;}} capture;
    f.lower();gran_lines=nullptr;int columns=0;
    for(const auto& line:capture.lines)if(line.x0==line.x1&&line.x0>=30&&line.x0<=302&&line.y0>=64&&line.y1<=121){CHECK(line.y1>line.y0);++columns;}
    CHECK(columns==546);
    // Violet silent output is visibly at least two pixels thick across the plot.
    for(int x=30;x<=302;++x)CHECK(pixels[92*320+x]==m::spot()&&pixels[93*320+x]==m::spot());
    f.pair();std::printf("osc live rails: %d positions / 7 smoothed params / frozen / lock / legacy / bold nonzero traces\n",cases);
}
void test_osc_gothic_hero(){
    OscFixture f;auto& u=*f.osc;int words=0;
    auto word_mask=[&](const std::vector<u32>& actual,const char* word,int x){
        CHECK(m::heading_width(word)>ui::text_w(word)); // baked hero, not native fallback
        module_inspector_surface(true);m::paper(400);m::heading(x,84,word,m::ink());
        for(int y=84;y<132;++y)for(int at=x;at<x+m::heading_width(word);++at)CHECK(actual[y*400+at]==pixels[y*400+at]);
        ++words;
    };
    const int anchors[]={0,10922,21845,32767};const char* names[]={"sine","tri","saw","pulse"};
    for(int i=0;i<4;++i){u.params[OscUnit::P_SHAPE].base=anchors[i];f.snap();f.capture();
        auto actual=module_inspector_top(false,true);word_mask(actual,names[i],14);}
    for(int raw:{1,10921,10923,18700,21844,21846,32766}){
        const int segment=raw<10922?0:raw<21845?1:2;u.params[OscUnit::P_SHAPE].base=raw;f.snap();f.capture();
        const auto actual=module_inspector_top(false,true);const int top=objects;f.lower();osc_geometry(top);
        word_mask(actual,names[segment],14);word_mask(actual,names[segment+1],14+m::heading_width(names[segment])+34);
        u.params[OscUnit::P_SHAPE].base=12000;u.params[OscUnit::P_SHAPE].effective=13000;g_ui_snapshot={};CHECK(module_inspector_top()==actual);
    }
    // Mute and melodic note identity no longer fall back to chunky 6x8 either.
    f.pick(OscUnit::P_GAIN);u.params[OscUnit::P_GAIN].base=0;f.snap();f.capture();
    word_mask(module_inspector_top(),"mute",14);
    f.pick(OscUnit::P_PITCH);
    for(int raw:{0,500,13000,32767}){u.params[OscUnit::P_PITCH].base=raw;f.snap();f.capture();const auto actual=module_inspector_top();
        char value[32],note[4]={};osc_focus_value_text(value,sizeof value,OscUnit::P_PITCH,raw);
        int n=0;while(value[n]!=' '&&n<3){note[n]=value[n];++n;}
        module_inspector_surface(true);m::paper(400);CHECK(note_display::melodic(note,m::ink(),m::spot(),4,20));
        for(int y=70;y<132;++y)for(int x=14;x<104;++x)CHECK(actual[y*400+x]==pixels[y*400+x]);
    }
    std::printf("osc gothic hero: %d baked word masks / 4 note identities / frozen transitions / no 2x native fallback\n",words);
}
void dump_osc_runtime(const std::filesystem::path&directory){std::filesystem::create_directories(directory);std::ofstream manifest(directory/"manifest.tsv");manifest<<"frame\tshape\ttop\tbottom\tshared\tclipped\toverlaps\n";OscFixture f;auto&u=*f.osc;delete f.engine->replace_unit(0,1,new FilterUnit);delete f.engine->replace_unit(0,2,new VcaUnit);
    auto save=[&](const char*name){f.capture();module_inspector_top(false,true);const int top=objects;dump_surface(directory,std::string(name)+"_top");f.lower();osc_geometry(top);dump_surface(directory,std::string(name)+"_bottom");manifest<<name<<'\t'<<u.params[OscUnit::P_SHAPE].base<<'\t'<<top<<'\t'<<objects<<'\t'<<top+objects<<'\t'<<outside_lines+outside_rects<<'\t'<<text_overlaps<<'\n';};
    u.params[OscUnit::P_SHAPE].base=6500;u.params[OscUnit::P_FOLD].base=0;u.params[OscUnit::P_SUB].base=0;f.snap();save("01_sine_tri");
    u.params[OscUnit::P_SHAPE].base=18700;u.params[OscUnit::P_FOLD].base=12000;f.snap();u.params[OscUnit::P_SHAPE].src=ModSource::SigilA;u.params[OscUnit::P_SHAPE].mod_depth=18000;u.params[OscUnit::P_SHAPE].effective=23600;u.params[OscUnit::P_PW].effective=28000;u.params[OscUnit::P_PW].src=ModSource::SigilB;u.params[OscUnit::P_PW].mod_depth=16000;u.params[OscUnit::P_GAIN].effective=12000;u.params[OscUnit::P_GAIN].src=ModSource::SigilA;u.params[OscUnit::P_GAIN].mod_depth=-10000;save("02_tri_saw_mod");
    u.params[OscUnit::P_PW].src=u.params[OscUnit::P_GAIN].src=ModSource::None;u.params[OscUnit::P_PW].mod_depth=u.params[OscUnit::P_GAIN].mod_depth=0;u.params[OscUnit::P_SHAPE].src=ModSource::None;u.params[OscUnit::P_SHAPE].base=30000;u.params[OscUnit::P_PW].base=6500;u.params[OscUnit::P_FOLD].base=20000;u.params[OscUnit::P_SUB].base=13000;f.snap();f.pick(OscUnit::P_PITCH);save("03_saw_pulse");}
