// Focused actual Ring controllers and native C++ draws; no authored UI pixels.
using RingValues=std::array<q15,6>;
constexpr RingValues RING_DEFAULTS={19000,24576,0,0,24576,0};
RingValues ring_bases(const Unit& u){RingValues a;for(int p=0;p<6;++p)a[p]=u.params[p].base;return a;}
void ring_config(Unit& u){for(int p=0;p<6;++p){u.params[p].base=RING_DEFAULTS[p];u.params[p].src=ModSource::None;u.params[p].mod_depth=0;u.params[p].snap();}}
int ring_span(int p){return p<2?128:62;}
int ring_range(int p){return p==2?65534:32767;}
void ring_release(){ring_focus_release();g_drag=Drag::None;}
void test_ring_controls(){
    ModuleInspectorFixture f(UnitKind::Ring);auto& u=*f.module;ring_config(u);
    struct Box{int x,y,w,h;};const Box boxes[]={{8,27,146,46},{168,27,146,46},{8,177,72,44},{84,177,72,44},{236,177,72,44},{160,177,72,44}};
    for(int p=0;p<6;++p){const auto b=boxes[p];const auto xy=f.point(p);
        for(int edge:{p==2?-32767:0,32767})for(int corner=0;corner<4;++corner){u.params[p].base=edge;const auto before=ring_bases(u);
            CHECK(f.down(b.x+(corner&1?b.w-1:0),b.y+(corner&2?b.h-1:0)));CHECK(ring_focus_selected()==p&&ring_bases(u)==before);ring_release();}
        u.params[p].base=10000;auto before=ring_bases(u);const int dirty=mutations;CHECK(f.down(xy.first,xy.second));
        f.drag(xy.first,xy.second-30);CHECK(ring_bases(u)==before&&mutations==dirty);
        f.drag(xy.first+6,xy.second);before[p]=10000+6*ring_range(p)/ring_span(p);CHECK(ring_bases(u)==before);
        f.drag(xy.first+1000,xy.second);CHECK(u.params[p].base==32767);f.drag(xy.first-1000,xy.second);CHECK(u.params[p].base==(p==2?-32767:0));ring_release();
    }
    ring_config(u);const auto before=ring_bases(u);const int dirty=mutations;
    for(auto xy:{std::pair<int,int>{155,118},{155,50},{80,190},{158,180},{232,200},{100,230}}){f.down(xy.first,xy.second);f.drag(xy.first+30,xy.second);CHECK(ring_bases(u)==before&&mutations==dirty);}
    f.down(260,10);CHECK(u.bypass);f.down(260,10);CHECK(!u.bypass);ring_release();
    ring_focus_select(5);ring_focus_select(-1);ring_focus_select(6);CHECK(ring_focus_selected()==5);
}
void test_ring_locks(){
    ModuleInspectorFixture f(UnitKind::Ring);auto& u=*f.module;ring_config(u);const auto bases=ring_bases(u);
    g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;g_view=View::Sequencer;
    const int labels[]={14,174,14,90,242,166};
    for(int p=0;p<6;++p){const auto xy=f.point(p);const q15 origin=p==2?-4000:4000;
        CHECK(f.engine->set_step_lock(0,7,0,p,origin));const auto saved=f.hash();const int dirty=mutations;
        f.down(xy.first,xy.second);f.drag(xy.first,xy.second);CHECK(f.hash()==saved&&mutations==dirty);
        f.drag(xy.first+6,xy.second);CHECK(seq_display_value(0,p,bases[p])==origin+6*ring_range(p)/ring_span(p)&&ring_bases(u)==bases);
        const auto locked=f.hash();g_seq_lock_step=8;f.drag(xy.first+12,xy.second);CHECK(f.hash()==locked);g_seq_lock_step=7;
        f.down(labels[p],p<2?32:182);CHECK(seq_display_value(0,p,bases[p])==bases[p]&&ring_focus_selected()==p);ring_release();
        const auto empty=f.hash();const int clean=mutations;f.down(xy.first,xy.second);f.drag(xy.first,xy.second);CHECK(f.hash()==empty&&mutations==clean);ring_release();
    }
    for(bool armed:{false,true})for(int reason=0;reason<9;++reason){
        if(!armed&&(reason==0||reason==6))continue;
        g_seq_lock_armed=armed;g_view=armed?View::Sequencer:View::Chains;CHECK(f.engine->set_step_lock(0,7,0,0,4000));
        const auto xy=f.point(0);f.down(xy.first,xy.second);const auto saved=f.hash();const int dirty=mutations;Unit* held=nullptr;
        if(reason==0)g_seq_lock_chain=1;
        if(reason==1)g_seq_lock_armed=!armed;
        if(reason==2)g_edit_chain=1;
        if(reason==3)g_edit_slot=1;
        if(reason==4)g_overlay=Overlay::None;
        if(reason==5)g_view=View::Sigils;
        if(reason==6)g_seq_lock_step=8;
        if(reason==7)ring_release();
        if(reason==8)held=f.engine->replace_unit(0,0,new RingUnit);
        f.drag(xy.first+30,xy.second);CHECK(f.hash()==saved&&ring_bases(u)==bases&&mutations==dirty);
        if(held)delete f.engine->replace_unit(0,0,held);
        g_seq_lock_chain=g_edit_chain=g_edit_slot=0;g_seq_lock_step=7;g_overlay=Overlay::Editor;ring_release();
    }
    g_seq_lock_armed=true;g_view=View::Sequencer;f.down(260,10);CHECK(!u.bypass);CHECK(!param_lens_touch_down(250,232));
    f.engine->clear_track(0);delete f.engine->replace_unit(0,1,new FmUnit);
    for(int i=0;i<SEQ_LOCKS;++i)CHECK(f.engine->set_step_lock(0,7,1,i,1234));
    const auto full=f.hash();
    for(int p=0;p<6;++p){const auto xy=f.point(p);f.down(xy.first,xy.second);f.drag(xy.first+6,xy.second);CHECK(f.hash()==full&&g_seq_lock_flash>0&&ring_bases(u)==bases);ring_release();}
    f.capture();module_inspector_top();CHECK(module_inspector_text("full"));
}
void test_ring_lens(){
    ModuleInspectorFixture f(UnitKind::Ring);auto& u=*f.module;ring_config(u);
    for(int p=0;p<6;++p){u.params[p].base=13000;u.params[p].mod_depth=1000;u.params[p].snap();f.pick(p);f.open();CHECK(ring_focus_selected()==p);
        param_lens_touch_down(250,129);CHECK(u.params[p].base==13000);param_lens_touch_drag(256,129);param_lens_touch_release();CHECK(u.params[p].base==13000+6*ring_range(p)/271);
        param_lens_touch_down(100,166);CHECK(u.params[p].mod_depth==1000);param_lens_touch_drag(106,166);param_lens_touch_release();CHECK(u.params[p].mod_depth==1000+6*65534/271);
        param_lens_touch_down(280,185);param_lens_touch_release();CHECK(u.params[p].src==ModSource::EnvFollow);
        param_lens_touch_down(70,185);param_lens_touch_release();CHECK(u.params[p].src==ModSource::SigilA);
        param_lens_touch_down(290,47);param_lens_touch_release();CHECK(ring_focus_selected()==(p+1)%6);
        u32 down=KEY_LEFT;param_lens_input(down,KEY_LEFT,0,false);CHECK(ring_focus_selected()==p);
        lens_permanent_probe(u,p);CHECK(ring_focus_selected()==p);f.open();
        f.capture();module_inspector_top(true);const int top=objects;ParamInspector detail;f.engine->capture_param_inspector(0,0,p,detail);module_inspector_detail(detail);
        module_inspector_surface(false);{ModuleInspectorNoEngine no;param_lens_draw_bottom();}fold_test_geometry_pair(top);
        CHECK(!module_inspector_text("temp")&&!module_inspector_text("audition"));
        down=KEY_B;param_lens_input(down,0,0,false);CHECK(!param_lens_is_open()&&ring_focus_selected()==p);
    }
    lens_quick_begin();const auto saved=ring_bases(u);g_edit_slot=1;f.capture();CHECK(!param_lens_is_open()&&!f.engine->param_temp_active()&&ring_bases(u)==saved);g_edit_slot=0;
}
int ring_expected_y(q15 value,int zero,int height){return zero-int(std::lround(double(value)*height/32768.));}
q15 ring_source_oracle(int i){const double pi=3.141592653589793;const double env=i<128?.5-.5*std::cos(pi*i/128.):.5+.5*std::cos(pi*(i-128)/895.);return q15(std::lround(24575.*env*std::sin(2*pi*i/128.)));}
q15 ring_carrier_oracle(uint16_t phase){const int tri=phase<32768?int(phase)*2-32768:32767-(int(phase)-32768)*2;return q15(std::max(-32768,std::min(32767,int((int64_t(tri)*(65536-std::abs(tri)))>>15))));}
uint32_t ring_increment_oracle(q15 raw){
    // Independent host construction of the documented 7/32-octave integer LUT.
    const unsigned n=unsigned(raw)*64,index=n>>15,fraction=n&32767;
    auto entry=[](int i){return uint32_t(std::llround(.5*std::pow(2.,i*7./32.)*4294967296./32000.));};
    const uint32_t a=entry(index),b=entry(index+1);return a+uint32_t(uint64_t(b-a)*fraction>>15);
}
void test_ring_reference(){
    namespace r=ring_reference;CHECK(r::PARAMS==6&&r::SAMPLES==1024&&r::COLUMNS==243&&r::X0==64&&r::ZERO==117&&r::HEIGHT==21&&r::CARRIER_ZERO==154&&r::CARRIER_HEIGHT==6);
    for(int i=0;i<1024;++i)CHECK(r::source_sample(i)==ring_source_oracle(i));
    for(int phase=0;phase<65536;++phase)CHECK(r::carrier_sample(uint16_t(phase))==ring_carrier_oracle(uint16_t(phase)));
    uint32_t prior=0;for(int v=0;v<32768;++v){const auto inc=ring_increment_oracle(q15(v));CHECK(r::increment(q15(v))==inc&&inc>prior);prior=inc;CHECK(std::abs(r::hz(q15(v))-inc*32000./4294967296.)<.001);}
    std::vector<RingValues> cases{RING_DEFAULTS};for(auto change:{std::pair<int,int>{3,32767},{2,22937},{5,32767}}){auto v=RING_DEFAULTS;v[change.first]=change.second;cases.push_back(v);}
    for(int p=0;p<6;++p)for(int value:{p==2?-32767:0,32767}){auto v=RING_DEFAULTS;v[p]=value;cases.push_back(v);}
    for(int edge:{0,32767})for(int fb:{-32767,0,32767})cases.push_back({q15(edge),q15(edge),q15(fb),q15(edge),q15(edge),q15(edge)});
    int samples=0,plots=0;
    for(const auto& values:cases)for(bool bypass:{false,true}){
        r::Plot a,again;r::plot(values.data(),bypass,a);r::plot(values.data(),bypass,again);
        RingUnit actual;for(int p=0;p<6;++p){actual.params[p].base=values[p];actual.params[p].snap();}actual.reset();
        std::array<q15,1024> src{},out{},carrier{};uint32_t phase=0;int envelope=0;
        for(int i=0;i<1024;++i){src[i]=out[i]=ring_source_oracle(i);if(!bypass)actual.process(&out[i],1);
            if(!bypass&&values[1]!=0){const int mag=std::abs(int(src[i]));envelope=mag>envelope?mag:envelope-((envelope-mag)>>8);
                const uint64_t base=ring_increment_oracle(values[0]),amount=uint64_t(envelope)*values[5]>>15;phase+=uint32_t(std::min(uint64_t(1610612736),base+(base*amount*7>>15)));}
            CHECK(actual.visual_phase()==phase>>16);carrier[i]=ring_carrier_oracle(actual.visual_phase());++samples;
        }
        auto audit_trace=[&](const r::Trace& trace,const r::Trace& repeat,const auto& input,int zero,int height){
            int first[243],last[243],lo[243],hi[243],counts[243]={};
            std::fill(lo,lo+243,256);std::fill(hi,hi+243,-1);
            for(int i=0;i<1024;++i){const int col=i*242/1023,y=ring_expected_y(input[i],zero,height);CHECK(r::y(input[i],zero,height)==y);
                if(!counts[col]++)first[col]=y;
                last[col]=y;lo[col]=std::min(lo[col],y);hi[col]=std::max(hi[col],y);}
            for(int col=0;col<243;++col){CHECK(counts[col]>0);CHECK(trace.first[col]==first[col]&&trace.last[col]==last[col]&&trace.lo[col]==lo[col]&&trace.hi[col]==hi[col]);
                CHECK(repeat.first[col]==first[col]&&repeat.last[col]==last[col]&&repeat.lo[col]==lo[col]&&repeat.hi[col]==hi[col]);}
        };
        audit_trace(a.source,again.source,src,117,21);audit_trace(a.shaped,again.shaped,out,117,21);audit_trace(a.carrier,again.carrier,carrier,154,6);++plots;
    }
    char text[32];for(int p=0;p<6;++p)for(int value:{p==2?-32767:0,32767}){module_inspector_value_text(text,sizeof text,UnitKind::Ring,p,q15(value));
        if(p){CHECK(std::strtol(text,nullptr,10)==(value<0?-100:value==0?0:100));CHECK(std::string(text).find('%')!=std::string::npos);}
        else{char* suffix=nullptr;double number=std::strtod(text,&suffix);if(std::string(suffix)=="khz")number*=1000;else CHECK(std::string(suffix)=="hz");CHECK(std::abs(number-ring_reference::hz(q15(value)))<(value?5.01:.0051));}}
    module_inspector_value_text(text,sizeof text,UnitKind::Ring,0,19000);CHECK(std::abs(std::strtod(text,nullptr)-139.04)<.0051);
    const auto start=std::chrono::steady_clock::now();r::Plot plot;volatile unsigned checksum=0;
    for(int i=0;i<200;++i){auto v=RING_DEFAULTS;v[0]+=i;r::plot(v.data(),false,plot);checksum+=plot.shaped.first[100];}
    const double us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count()/200;
    std::printf("ring reference: %d plots / %d actual DSP+phase samples; 1024 independent burst samples; 65536 carriers; 32768 increments; uncached host %.2fus/plot checksum %u (not 3ds timing)\n",plots,samples,us,unsigned(checksum));
}
void test_ring_marker_layers(){
    ModuleInspectorFixture f(UnitKind::Ring);auto& u=*f.module;ring_config(u);ring_focus_select(0);
    const int starts[]={14,174,14,90,242,166};
    for(int p=0;p<6;++p){u.params[p].base=p==2?-16384:8192;u.params[p].effective=p==2?16384:24576;}
    auto check_markers=[&]{
        f.capture();module_inspector_surface(false);{ModuleInspectorNoEngine no;f.lower();}module_inspector_geometry();
        for(int p=0;p<6;++p){
            auto x=[&](q15 value){return starts[p]+(int(value)+(p==2?32767:0))*ring_span(p)/ring_range(p);};
            const q15 edit=g_seq_lock_armed?seq_display_value(0,p,u.params[p].base):u.params[p].base;
            const int base=x(edit),live=x(u.params[p].effective),y=p<2?65:214;
            CHECK(pixels[(y-3)*width+base]==(p==0?m::spot():m::ink()));
            CHECK(pixels[(y-1)*width+base]==m::paper_color(y-1) && pixels[y*width+base]==m::branch());
            if(live!=base)CHECK(pixels[(y-1)*width+live]==m::spot() && pixels[y*width+live]==m::spot());
        }
    };
    check_markers();
    g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;
    for(int p=0;p<SEQ_LOCKS;++p)CHECK(f.engine->set_step_lock(0,7,0,p,q15(p==2?-8192:12000))); // four-lock budget; the other rails remain base
    check_markers();
    for(int p=0;p<6;++p)u.params[p].effective=seq_display_value(0,p,u.params[p].base);
    check_markers();
}
void test_ring_frames(){
    test_ring_marker_layers();
    ModuleInspectorFixture f(UnitKind::Ring);auto& u=*static_cast<RingUnit*>(f.module);ring_config(u);ring_live_unit=&u;ring_live_process_calls=0;
    f.capture();const auto calls=ring_process_calls;const auto start=std::chrono::steady_clock::now();for(int i=0;i<200;++i)f.capture();
    const double cached_us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count()/200;CHECK(ring_process_calls==calls);
    for(int p=0;p<6;++p){++u.params[p].effective;const auto before=ring_process_calls;f.capture();CHECK(ring_process_calls==before+1024);f.capture();CHECK(ring_process_calls==before+1024);}
    // Binding proof: the real captured plot uses effective values normally and
    // the displayed armed lock while editing, not whichever bank is convenient.
    auto plot_pixels=[&]{f.capture();module_inspector_surface(false);{ModuleInspectorNoEngine no;f.lower();}
        std::vector<u32> crop;for(int y=90;y<=160;++y)for(int x=64;x<=306;++x)crop.push_back(pixels[y*320+x]);return crop;};
    ring_config(u);ring_focus_select(3);const auto clean=plot_pixels();u.params[3].effective=32767;const auto bridge=plot_pixels();CHECK(bridge!=clean);
    u.params[3].base=32767;u.params[3].effective=0;CHECK(plot_pixels()==clean);ring_config(u);
    g_seq_lock_armed=true;g_seq_lock_step=7;CHECK(f.engine->set_step_lock(0,7,0,3,32767));CHECK(plot_pixels()==bridge);
    CHECK(f.engine->remove_step_lock(0,7,0,3));CHECK(plot_pixels()==clean);g_seq_lock_armed=false;
    u.bypass=true;const auto bypass=plot_pixels();u.bypass=false;u.params[1].effective=0;CHECK(plot_pixels()==bypass);
    ring_config(u);CHECK(plot_pixels()==clean);
    int frames=0;
    for(int p=0;p<6;++p)for(int edge:{0,32767})for(int fb:{-32767,0,32767}){
        ring_config(u);u.params[p].base=edge;u.params[2].base=fb;for(int n=0;n<6;++n)u.params[n].snap();ring_focus_select(p);f.capture();const auto before=ring_process_calls;
        const auto top=module_inspector_top();const int count=objects;CHECK(module_inspector_top(false,true)==top);
        ParamInspector detail;f.engine->capture_param_inspector(0,0,p,detail);module_inspector_detail(detail);
        module_inspector_surface(false);{ModuleInspectorNoEngine no;f.lower();}fold_test_geometry_pair(count);
        const auto lower=pixels;const int lower_count=objects;const auto saved=ring_bases(u);for(int n=0;n<6;++n)u.params[n].base=12345;
        g_ui_snapshot={};module_inspector_surface(false);{ModuleInspectorNoEngine no;f.lower();}CHECK(pixels==lower&&objects==lower_count&&module_inspector_top()==top);
        CHECK(ring_process_calls==before);for(int n=0;n<6;++n)u.params[n].base=saved[n];++frames;
    }
    ring_config(u);ring_focus_select(5);f.capture();module_inspector_top();CHECK(module_inspector_text("input level")&&module_inspector_text("not pitch"));
    for(int state=0;state<2;++state){u.bypass=state==0;f.engine->chains[0].mute=state==1;f.capture();module_inspector_top();CHECK(module_inspector_text(state==0?"bypassed":"chain muted"));}
    CHECK(ring_live_process_calls==0);ring_live_unit=nullptr;g_overlay=Overlay::None;f.capture();module_inspector_surface(true);CHECK(!module_inspector_draw_top());module_inspector_surface(false);f.lower();CHECK(texts.empty());
    std::printf("ring frozen extrema: %d pairs; six independent cache invalidations; 200 cache hits %.2fus/capture; no live-unit/draw DSP (host only)\n",frames,cached_us);
}
void dump_ring_runtime(const std::filesystem::path& directory){
    std::filesystem::create_directories(directory);std::ofstream manifest(directory/"manifest.tsv");
    manifest<<"frame\tselected\tfreq\tdepth\tfeedback\tdiode\tmix\tfollow\ttop_objects\tbottom_objects\tshared_objects\tbottom_clipped\tbottom_overlaps\n";
    ModuleInspectorFixture f(UnitKind::Ring);auto& u=*f.module;
    // Actual serial breadcrumb exactly matching the approved preview, not painted labels.
    Unit* moved=f.engine->replace_unit(0,0,new OscUnit);delete f.engine->replace_unit(0,1,moved);delete f.engine->replace_unit(0,2,new VcaUnit);g_edit_slot=1;
    auto reset=[&]{ring_config(u);ring_focus_select(0);};reset();
    auto save=[&](const std::string& name,bool lens=false){f.capture();const auto before=ring_process_calls;module_inspector_top(lens,true);const int top=objects;dump_surface(directory,name+"_top");
        module_inspector_surface(false);{ModuleInspectorNoEngine no;if(lens)param_lens_draw_bottom();else f.lower();}fold_test_geometry_pair(top);dump_surface(directory,name+"_bottom");CHECK(ring_process_calls==before);
        manifest<<name<<'\t'<<ring_focus_selected();for(auto value:ring_bases(u))manifest<<'\t'<<value;
        manifest<<'\t'<<top<<'\t'<<objects<<'\t'<<top+objects<<'\t'<<outside_lines+outside_rects<<'\t'<<text_overlaps<<'\n';};
    save("01_clean");
    const char* names[]={"02_bridge","03_feedback","04_follow","05_slow","06_fast","07_depth_full","08_negative_feedback","09_mix_dry","10_depth_zero","11_mix_full"};
    const int param[]={3,2,5,0,0,1,2,4,1,4},values[]={32767,22937,32767,0,32767,32767,-32767,0,0,32767};
    for(int i=0;i<10;++i){reset();u.params[param[i]].base=values[i];u.params[param[i]].snap();ring_focus_select(param[i]);save(names[i]);}
    reset();u.params[0].src=ModSource::SigilB;u.params[0].mod_depth=-18000;u.params[0].effective=24000;save("12_live_mod");
    g_seq_lock_armed=true;g_seq_lock_step=7;CHECK(f.engine->set_step_lock(0,7,1,0,0));save("13_lock");g_seq_lock_armed=false;
    reset();ring_focus_select(5);f.open();save("14_mod",true);lens_quick_begin();param_lens_touch_down(180,129);param_lens_touch_drag(224,129);save("15_temp",true);
    lens_frame(0,0,KEY_L);param_lens_touch_release();param_lens_cancel();ring_release();
    reset();u.bypass=true;save("16_bypass");u.bypass=false;f.engine->chains[0].mute=true;save("17_muted");f.engine->chains[0].mute=false;
    f.engine->clear_track(0);delete f.engine->replace_unit(0,2,new FmUnit);for(int i=0;i<SEQ_LOCKS;++i)CHECK(f.engine->set_step_lock(0,7,2,i,1234));
    g_seq_lock_armed=true;save("18_lock_full");g_seq_lock_armed=false;
}
