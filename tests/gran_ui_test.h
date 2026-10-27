// Included by the existing sequencer raster harness: actual GRAN, not authored pixels.
void gran_geometry_pair(int top){
    CHECK(objects>0 && top+objects<=8192 && outside_rects==0 && outside_lines==0 && text_overlaps==0);
    for(const auto& t:texts)CHECK(t.x>=0&&t.y>=0&&t.x+t.w<=width&&t.y+t.h<=HEIGHT);
    // Alpha blending is required INSIDE the grain plot; do not use REV's opaque-only palette audit.
    maximum_objects=std::max(maximum_objects,objects);maximum_shared_objects=std::max(maximum_shared_objects,top+objects);
}
struct GranFixture : Fixture {
    static constexpr int SAMPLE=60;
    GrainUnit* gran=nullptr;
    trackr::synth::Sample saved;
    trackr::synth::SampleProvenance provenance;
    GranFixture(){
        auto& b=trackr::synth::SampleBank::instance();saved=std::move(b.slot(SAMPLE));provenance=b.provenance(SAMPLE);
        auto& s=b.slot(SAMPLE);s={};s.channels=1;s.data.resize(64000);
        for(int i=0;i<64000;++i)s.data[i]=q15(20000*std::sin(i*.053)*std::sin(i*.00011));
        b.set_provenance(SAMPLE,trackr::synth::SampleOrigin::SdWav,"test/gran_source.wav");
        gran=new GrainUnit;gran->clean=false;gran->sample_slot=SAMPLE;delete engine->replace_unit(0,0,gran);
        g_edit_chain=g_edit_slot=0;g_view=View::Chains;g_overlay=Overlay::Editor;param_lens_cancel();
        grain_focus_release();grain_focus_select(1);g_drag=Drag::None;capture();
    }
    ~GranFixture(){gran_lines=nullptr;gran_browser_enabled=false;g_overlay=Overlay::None;grain_focus_capture();
        trackr::synth::SampleBank::instance().swap_sample_unchecked(SAMPLE,saved,provenance.origin,provenance.path,provenance.missing);}
    void capture(){engine->capture_ui_snapshot(g_ui_snapshot,0,0);param_lens_capture();}
    void touch(int x,int y){CHECK(grain_focus_touch_body(*gran,x,y));}
    void drag(int x,int y){grain_focus_drag(*gran,x,y);}
    void up(){grain_focus_release();g_drag=Drag::None;}
    void pick(int p){grain_focus_select(p);}
    static int index(int p){static const int order[]={0,1,2,3,4,0,1,3,4,5,2};return order[p];}
    static std::pair<int,int> point(int p){const int i=index(p);return {14+(i%3)*104+42,165+(i/3)*34+25};}
    void snap(){for(int p=0;p<11;++p)gran->params[p].snap();gran->control_tick();}
    void process(int frames){q15 b[32];for(int n=0;n<frames;n+=32){gran->control_tick();gran->process(b,std::min(32,frames-n));}}
    std::array<q15,11> bases(){std::array<q15,11> v{};for(int p=0;p<11;++p)v[p]=gran->params[p].base;return v;}
    void lower(){module_inspector_surface(false);ModuleInspectorNoEngine frozen;grain_focus_draw(*gran);gran_geometry_pair(0);}
    std::vector<GranLine> lines(){std::vector<GranLine> result;gran_lines=&result;lower();gran_lines=nullptr;return result;}
    void pair(){capture();module_inspector_top(false,true);const int top=objects;lower();gran_geometry_pair(top);}
    void open(){CHECK(param_lens_touch_down(250,232));param_lens_touch_release();CHECK(param_lens_is_open());}
    void config(){const q15 values[]={16384,25800,6500,8192,32767,20480,0,16384,16384,0,32767};
        for(int p=0;p<11;++p){gran->params[p].base=values[p];gran->params[p].src=ModSource::None;gran->params[p].mod_depth=0;}snap();gran->reset();snap();}
};
void test_gran_controls(){
    GranFixture f;auto& u=*f.gran;CHECK(u.num_params==GrainUnit::P_COUNT);const auto defaults=f.bases();
    for(int p=0;p<11;++p){const int i=GranFixture::index(p),x=14+(i%3)*104,y=165+(i/3)*34;f.pick(p);
        for(int edge:{p==3?-32767:0,32767})for(int corner=0;corner<4;++corner){u.params[p].base=edge;const auto before=f.bases();
            f.touch(x-6+(corner&1?95:0),y-4+(corner&2?32:0));CHECK(grain_focus_selected()==p&&f.bases()==before);f.up();}
        const auto xy=f.point(p);u.params[p].base=10000;auto before=f.bases();const int dirty=mutations;
        f.touch(xy.first,xy.second);f.drag(xy.first,xy.second-30);CHECK(f.bases()==before&&mutations==dirty);
        f.drag(xy.first+6,xy.second);before[p]=10000+6*(p==3?65534:32767)/84;CHECK(f.bases()==before);
        f.drag(xy.first+2000,xy.second);CHECK(u.params[p].base==32767);f.drag(xy.first-2000,xy.second);CHECK(u.params[p].base==(p==3?-32767:0));f.up();
    }
    for(int p=0;p<11;++p)u.params[p].base=defaults[p];
    const auto before=f.bases();const int dirty=mutations;
    for(auto xy:{std::pair<int,int>{110,180},{215,200},{160,145},{20,145},{50,40}}){f.touch(xy.first,xy.second);f.drag(xy.first+40,xy.second);CHECK(f.bases()==before&&mutations==dirty);}
    f.touch(210,40);CHECK(grain_focus_selected()<=4);f.touch(275,40);CHECK(grain_focus_selected()==5);f.touch(210,40);
    for(int i=0;i<5;++i){f.touch(280,65);CHECK(u.clock_sync==(i<4));CHECK(u.sync_division==(i<4?i:0));CHECK(f.bases()==before);}
    f.touch(242,9);CHECK(u.bypass);f.touch(242,9);CHECK(!u.bypass);
    const int scans=gran_browser_scans;gran_browser_enabled=true;f.touch(100,65);CHECK(g_overlay==Overlay::SampleBrowser&&g_sample_page==60/16&&gran_browser_scans==scans+1);
    g_overlay=Overlay::Editor;f.pick(8);grain_focus_select(-1);grain_focus_select(12);CHECK(grain_focus_selected()==8);
}
void test_gran_locks_lens(){
    GranFixture f;auto& u=*f.gran;const auto bases=f.bases();g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;g_view=View::Sequencer;
    for(int p=0;p<11;++p){f.pick(p);const auto xy=f.point(p);CHECK(f.engine->set_step_lock(0,7,0,p,5000));const auto before=f.hash();const int dirty=mutations;
        f.touch(xy.first,xy.second);f.drag(xy.first,xy.second);CHECK(f.hash()==before&&mutations==dirty);
        f.drag(xy.first+6,xy.second);CHECK(seq_display_value(0,p,bases[p])==5000+6*(p==3?65534:32767)/84&&f.bases()==bases);
        const auto changed=f.hash();g_seq_lock_step=8;f.drag(xy.first+12,xy.second);CHECK(f.hash()==changed);g_seq_lock_step=7;
        f.touch(xy.first-42,xy.second-25);CHECK(seq_display_value(0,p,bases[p])==bases[p]);f.up();
    }
    for(int reason=0;reason<8;++reason){f.pick(3);const auto xy=f.point(3);f.touch(xy.first,xy.second);const auto before=f.hash();Unit* old=nullptr;
        if(reason==0)g_seq_lock_chain=1;
        if(reason==1)g_seq_lock_armed=false;
        if(reason==2)g_edit_chain=1;
        if(reason==3)g_edit_slot=1;
        if(reason==4)g_overlay=Overlay::None;
        if(reason==5)g_view=View::Sigils;
        if(reason==6)old=f.engine->replace_unit(0,0,new GrainUnit);
        if(reason==7)f.engine->active_pattern=1;
        f.drag(xy.first+40,xy.second);CHECK(f.hash()==before&&f.bases()==bases);
        if(old)delete f.engine->replace_unit(0,0,old);
        g_edit_chain=g_edit_slot=g_seq_lock_chain=0;g_seq_lock_armed=true;g_view=View::Sequencer;g_overlay=Overlay::Editor;f.engine->active_pattern=0;f.up();
    }
    delete f.engine->replace_unit(0,1,new FmUnit);for(int i=0;i<SEQ_LOCKS;++i)CHECK(f.engine->set_step_lock(0,7,1,i,1234));const auto full=f.hash();
    for(int p=0;p<11;++p){f.pick(p);const auto xy=f.point(p);f.touch(xy.first,xy.second);f.drag(xy.first+6,xy.second);CHECK(f.hash()==full&&g_seq_lock_flash>0&&f.bases()==bases);f.up();}
    f.touch(242,9);f.touch(100,65);f.touch(210,40);f.touch(280,65);CHECK(!u.bypass&&!u.clock_sync&&g_overlay==Overlay::Editor);CHECK(!param_lens_touch_down(250,232));f.pair();
    g_seq_lock_armed=false;g_view=View::Chains;
    for(int p=0;p<11;++p){u.params[p].base=13000;u.params[p].mod_depth=1000;u.params[p].snap();f.pick(p);f.open();CHECK(grain_focus_selected()==p);
        param_lens_touch_down(250,129);CHECK(u.params[p].base==13000);param_lens_touch_drag(256,129);param_lens_touch_release();CHECK(u.params[p].base==13000+6*(p==3?65534:32767)/271);
        param_lens_touch_down(100,166);CHECK(u.params[p].mod_depth==1000);param_lens_touch_drag(106,166);param_lens_touch_release();CHECK(u.params[p].mod_depth==1000+6*65534/271);
        param_lens_touch_down(70,185);param_lens_touch_release();CHECK(u.params[p].src==ModSource::SigilA);
        param_lens_touch_down(290,47);param_lens_touch_release();CHECK(grain_focus_selected()==(p+1)%11);
        u32 down=KEY_LEFT;param_lens_input(down,KEY_LEFT,0,false);CHECK(grain_focus_selected()==p);
        lens_permanent_probe(u,p);CHECK(grain_focus_selected()==p);f.open();
        f.capture();module_inspector_top(true);const int top=objects;module_inspector_surface(false);{ModuleInspectorNoEngine no;param_lens_draw_bottom();}gran_geometry_pair(top);
        CHECK(module_inspector_text("base ")&&!module_inspector_text("temp"));
        down=KEY_B;param_lens_input(down,0,0,false);CHECK(!param_lens_is_open()&&grain_focus_selected()==p);
    }
}
std::vector<u32> gran_crop(){std::vector<u32> out;for(int y=76;y<149;++y)for(int x=14;x<=306;++x)out.push_back(pixels[y*320+x]);return out;}
void test_gran_wave(){
    GranFixture f;auto& s=trackr::synth::SampleBank::instance().slot(60);auto& u=*f.gran;int pcm_cases=0,head_cases=0;
    for(int nf:{1,2,255,256,1024,64000})for(int ch:{1,2})for(int mode:{0,1,2}){
        s.channels=ch;s.logical_frames=nf;s.data.resize((nf+100)*ch);
        for(int i=0;i<nf+100;++i)for(int c=0;c<ch;++c)s.data[i*ch+c]=i>=nf?32767:mode==0?0:mode==1?(c?-32768:32767):q15((i*877+c*999)%65536-32768);
        u.reset();u.params[0].base=pcm_cases%3==0?0:pcm_cases%3==1?16384:32767;f.snap();f.capture();const auto lines=f.lines();
        std::vector<GranLine> wave;for(const auto& l:lines)if(l.x0==l.x1&&l.x0>=14&&l.x0<=306&&l.y0>=83&&l.y0<=143&&l.y1>=83&&l.y1<=143&&(l.color>>24)==255)wave.push_back(l);
        CHECK(wave.size()==293);const uint32_t span=(nf+1)/2,pos=uint64_t(u.params[0].effective)*nf/32768,start=pos>span/2?std::min<uint32_t>(pos-span/2,nf-span):0;
        for(int x=0;x<293;++x){const uint32_t a=start+uint64_t(x)*span/293,b=start+uint64_t(x+1)*span/293,n=std::min<uint32_t>(8,std::max<uint32_t>(1,b-a));int lo=32767,hi=-32768;
            for(uint32_t j=0;j<n;++j){const uint32_t at=std::min<uint32_t>(nf-1,a+(n>1?uint64_t(j)*(b-a-1)/(n-1):0));for(int c=0;c<ch;++c){lo=std::min(lo,int(s.data[at*ch+c]));hi=std::max(hi,int(s.data[at*ch+c]));}}
            CHECK(wave[x].x0==14+x&&wave[x].y0==113-hi*30/32768&&wave[x].y1==113-lo*30/32768);
        }++pcm_cases;
    }
    s.channels=1;s.logical_frames=UINT32_MAX;s.data.assign(64000,0);f.config();
    u.params[2].base=0;u.params[5].base=0;u.params[1].base=25800;f.snap();
    for(int speed:{8192,-8192,32767,-32767,0}){
        u.reset();u.params[3].base=speed;f.snap();
        for(int age:{1,1600,4800,9600,12800}){
            u.reset();u.params[7].base=u.params[8].base=16384;f.snap();f.process(age);GrainUnit::VisualGrain h[8];u.capture_visual_grains(h);f.capture();const auto lines=f.lines();
            std::vector<GranLine> cursors;for(const auto& l:lines)if(l.x0==l.x1&&l.y0==102&&l.y1==122&&(l.color>>24)<255)cursors.push_back(l);
            std::vector<std::pair<int,int>> expected;
            for(const auto& v:h)if(v.active){const int x=14+int((v.position_q16-16000ll*65536)*292/(32000ll*65536));const unsigned alpha=uint32_t(v.envelope)*190/32767;
                if(alpha&&x>=14&&x<=306)expected.push_back({x,alpha});}
            CHECK(cursors.size()==expected.size());for(size_t i=0;i<expected.size();++i)CHECK(cursors[i].x0==expected[i].first&&int(cursors[i].color>>24)==expected[i].second);
            const auto original=gran_crop();u.params[7].base=u.params[8].base=0;u.params[7].snap();u.params[8].snap();f.capture();f.lower();CHECK(gran_crop()==original);++head_cases;
        }
    }
    // An old latched slow head may still be valid after a tiny source replacement.
    s.data.assign(64000,0);u.params[0].base=0;u.params[3].base=0;u.params[1].base=32767;u.params[7].base=0;u.reset();f.snap();f.process(1);
    s.data.resize(2);f.capture();const auto tiny=f.lines();bool full_width=false;
    for(const auto& l:tiny)if((l.color>>24)<255){CHECK(l.x0>=14&&l.x0<=306&&l.x1>=14&&l.x1<=306);if(l.x0==14&&l.x1==306)full_width=true;}
    CHECK(full_width);
    s.data.assign(64000,0);f.config();f.process(11520);f.capture();f.lower();const auto grain=gran_crop();f.touch(275,40);f.capture();f.lower();CHECK(gran_crop()==grain);
    const auto frozen=pixels;s.data.clear();u.params[0].base=0;u.reset();g_ui_snapshot={};g_ui_frame+=900;f.lower();CHECK(pixels==frozen);
    f.capture();f.lower();CHECK(module_inspector_text("no source"));CHECK(gran_crop()!=grain);
    std::printf("gran wave: %d exact293-column PCM fixtures; %d actual head cursor/alpha pairs; latched knobs, page-invariant wave, no PCM draw/stale trail\n",pcm_cases,head_cases);
}
void test_gran_frames(){
    GranFixture f;auto& u=*f.gran;char text[32];
    for(int raw:{-32767,-8192,-1,0,1,8192,32767}){module_inspector_value_text(text,sizeof text,UnitKind::Grain,3,q15(raw));CHECK(std::abs(std::strtod(text,nullptr)-(raw<0?-1.:1.)*std::max(1024,std::abs(raw)*8)/65536.)<=.0051);}
    for(int raw:{1,8192,16384,32767}){module_inspector_value_text(text,sizeof text,UnitKind::Grain,4,q15(raw));CHECK(std::abs(std::strtod(text,nullptr)-20*std::log10(raw/32768.))<=.051);}
    module_inspector_value_text(text,sizeof text,UnitKind::Grain,4,0);CHECK(std::string(text)=="mute");
    int pairs=0;
    for(int p=0;p<11;++p)for(int edge:{0,32767}){f.config();u.params[p].base=edge;u.params[3].base=p==3?-32767:8192;f.snap();f.process(11520);f.pick(p);f.capture();
        const auto top=module_inspector_top(false,true);const int n=objects;f.lower();gran_geometry_pair(n);const auto bottom=pixels;
        const auto saved=f.bases();for(auto& param:u.params)param.base=12345;g_ui_snapshot={};f.lower();CHECK(pixels==bottom&&module_inspector_top()==top);
        for(int i=0;i<11;++i)u.params[i].base=saved[i];
        ++pairs;
    }
    for(int state=0;state<2;++state){u.bypass=state==0;f.engine->chains[0].mute=state==1;f.capture();const auto lines=f.lines();CHECK(module_inspector_text(state==0?"paused":"muted"));for(const auto& l:lines)CHECK((l.color>>24)==255);f.pair();}
    g_overlay=Overlay::None;f.capture();module_inspector_surface(true);CHECK(!module_inspector_draw_top());f.lower();CHECK(texts.empty());
    std::printf("gran frames: %d frozen actual dual extrema, bypass/mute suppression; exact speed and gain units\n",pairs);
}
void dump_gran_runtime(const std::filesystem::path& directory){
    std::filesystem::create_directories(directory);std::ofstream manifest(directory/"manifest.tsv");
    manifest<<"frame\tselected\tactive\ttop_objects\tbottom_objects\tshared_objects\tbottom_clipped\tbottom_overlaps\n";
    GranFixture f;auto& u=*f.gran;auto& s=trackr::synth::SampleBank::instance().slot(60);
    CHECK(trackr::synth::load_wav_to_sample("local_sample_library/sdcard/3ds/seance/samples/granular/gran_dark_atmos_01.wav",s,32000,640000)==trackr::synth::WavLoadResult::Ok);
    trackr::synth::SampleBank::instance().set_provenance(60,trackr::synth::SampleOrigin::SdWav,"gran_dark_atmos_01.wav");
    f.config();u.params[2].base=q15(uint64_t(12800)*65536/s.playable_frames());f.snap();f.process(11520);
    auto save=[&](const char* name){f.capture();module_inspector_top(false,true);const int top=objects;dump_surface(directory,std::string(name)+"_top");f.lower();gran_geometry_pair(top);dump_surface(directory,std::string(name)+"_bottom");
        GrainUnit::VisualGrain h[8];u.capture_visual_grains(h);int active=0;for(const auto& v:h)active+=v.active;
        manifest<<name<<'\t'<<grain_focus_selected()<<'\t'<<active<<'\t'<<top<<'\t'<<objects<<'\t'<<top+objects<<'\t'<<outside_lines+outside_rects<<'\t'<<text_overlaps<<'\n';};
    save("01_grain");f.touch(275,40);save("02_cloud");
    u.reset();u.params[3].base=-8192;f.snap();f.process(11520);f.pick(3);save("03_reverse");
    f.pick(7);g_seq_lock_armed=true;g_seq_lock_step=7;CHECK(f.engine->set_step_lock(0,7,0,7,0));save("04_lock_envelope");g_seq_lock_armed=false;
}
