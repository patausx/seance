// Focused actual Strain controllers and native C++ draws; never Stranular or authored pixels.
using StrainValues=std::array<q15,6>;
using StrainBuffer=std::array<q15,1024>;
constexpr StrainValues STRAIN_DEFAULTS={15000,0,15000,12000,9000,24500};
StrainValues strain_bases(const Unit& u){StrainValues a;for(int p=0;p<6;++p)a[p]=u.params[p].base;return a;}
void strain_config(Unit& u){for(int p=0;p<6;++p){u.params[p].base=STRAIN_DEFAULTS[p];u.params[p].src=ModSource::None;u.params[p].mod_depth=0;u.params[p].snap();}}
int strain_span(int p){return p==0||p==3?128:62;}
int strain_range(int p){return p==1?65534:32767;}
void strain_release(){strain_focus_release();g_drag=Drag::None;}
void test_strain_controls(){
    ModuleInspectorFixture f(UnitKind::Strain);auto& u=*f.module;strain_config(u);
    struct Box{int x,y,w,h;};const Box boxes[]={{8,27,146,46},{84,177,72,44},{8,177,72,44},{168,27,146,46},{160,177,72,44},{236,177,72,44}};
    for(int p=0;p<6;++p){const auto b=boxes[p];const auto xy=f.point(p);
        for(int edge:{p==1?-32767:0,32767})for(int corner=0;corner<4;++corner){u.params[p].base=edge;const auto before=strain_bases(u);
            CHECK(f.down(b.x+(corner&1?b.w-1:0),b.y+(corner&2?b.h-1:0)));CHECK(strain_focus_selected()==p&&strain_bases(u)==before&&g_drag==Drag::StrainParam);strain_release();}
        u.params[p].base=10000;auto before=strain_bases(u);const int dirty=mutations;CHECK(f.down(xy.first,xy.second));
        f.drag(xy.first,xy.second-30);CHECK(strain_bases(u)==before&&mutations==dirty);
        f.drag(xy.first+6,xy.second);before[p]=10000+6*strain_range(p)/strain_span(p);CHECK(strain_bases(u)==before);
        f.drag(xy.first+1000,xy.second);CHECK(u.params[p].base==32767);f.drag(xy.first-1000,xy.second);CHECK(u.params[p].base==(p==1?-32767:0));strain_release();
    }
    strain_config(u);const auto before=strain_bases(u);const int dirty=mutations;
    for(auto xy:{std::pair<int,int>{155,118},{155,50},{80,190},{158,180},{232,200},{100,230}}){f.down(xy.first,xy.second);f.drag(xy.first+30,xy.second);CHECK(strain_bases(u)==before&&mutations==dirty);}
    f.down(260,10);CHECK(u.bypass);f.down(260,10);CHECK(!u.bypass);strain_release();
    strain_focus_select(5);strain_focus_select(-1);strain_focus_select(6);CHECK(strain_focus_selected()==5);
}
void test_strain_locks(){
    ModuleInspectorFixture f(UnitKind::Strain);auto& u=*f.module;strain_config(u);const auto bases=strain_bases(u);
    g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;g_view=View::Sequencer;
    const int labels[]={14,90,14,174,166,242};
    for(int p=0;p<6;++p){const auto xy=f.point(p);const q15 origin=p==1?-4000:4000;
        CHECK(f.engine->set_step_lock(0,7,0,p,origin));const auto saved=f.hash();const int dirty=mutations;
        f.down(xy.first,xy.second);f.drag(xy.first,xy.second);CHECK(f.hash()==saved&&mutations==dirty);
        f.drag(xy.first+6,xy.second);CHECK(seq_display_value(0,p,bases[p])==origin+6*strain_range(p)/strain_span(p)&&strain_bases(u)==bases);
        const auto locked=f.hash();g_seq_lock_step=8;f.drag(xy.first+12,xy.second);CHECK(f.hash()==locked);g_seq_lock_step=7;
        f.down(labels[p],p==0||p==3?32:182);CHECK(seq_display_value(0,p,bases[p])==bases[p]&&strain_focus_selected()==p);strain_release();
        const auto empty=f.hash();const int clean=mutations;f.down(xy.first,xy.second);f.drag(xy.first,xy.second);CHECK(f.hash()==empty&&mutations==clean);strain_release();
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
        if(reason==7)strain_release();
        if(reason==8)held=f.engine->replace_unit(0,0,new StrainUnit);
        f.drag(xy.first+30,xy.second);CHECK(f.hash()==saved&&strain_bases(u)==bases&&mutations==dirty);
        if(held)delete f.engine->replace_unit(0,0,held);
        g_seq_lock_chain=g_edit_chain=g_edit_slot=0;g_seq_lock_step=7;g_overlay=Overlay::Editor;strain_release();
    }
    g_seq_lock_armed=true;g_view=View::Sequencer;f.down(260,10);CHECK(!u.bypass);CHECK(!param_lens_touch_down(250,232));
    f.engine->clear_track(0);delete f.engine->replace_unit(0,1,new FmUnit);
    for(int i=0;i<SEQ_LOCKS;++i)CHECK(f.engine->set_step_lock(0,7,1,i,1234));
    const auto full=f.hash();
    for(int p=0;p<6;++p){const auto xy=f.point(p);f.down(xy.first,xy.second);f.drag(xy.first+6,xy.second);CHECK(f.hash()==full&&g_seq_lock_flash>0&&strain_bases(u)==bases);strain_release();}
    f.capture();module_inspector_top();CHECK(module_inspector_text("full"));
}
void test_strain_lens(){
    ModuleInspectorFixture f(UnitKind::Strain);auto& u=*f.module;strain_config(u);
    for(int p=0;p<6;++p){u.params[p].base=13000;u.params[p].mod_depth=1000;u.params[p].snap();f.pick(p);f.open();CHECK(strain_focus_selected()==p);
        param_lens_touch_down(250,129);CHECK(u.params[p].base==13000);param_lens_touch_drag(256,129);param_lens_touch_release();CHECK(u.params[p].base==13000+6*strain_range(p)/271);
        param_lens_touch_down(100,166);CHECK(u.params[p].mod_depth==1000);param_lens_touch_drag(106,166);param_lens_touch_release();CHECK(u.params[p].mod_depth==1000+6*65534/271);
        param_lens_touch_down(280,185);param_lens_touch_release();CHECK(u.params[p].src==ModSource::EnvFollow);
        param_lens_touch_down(70,185);param_lens_touch_release();CHECK(u.params[p].src==ModSource::SigilA);
        param_lens_touch_down(290,47);param_lens_touch_release();CHECK(strain_focus_selected()==(p+1)%6);
        u32 down=KEY_LEFT;param_lens_input(down,KEY_LEFT,0,false);CHECK(strain_focus_selected()==p);
        lens_permanent_probe(u,p);CHECK(strain_focus_selected()==p);f.open();
        f.capture();module_inspector_top(true);const int top=objects;ParamInspector detail;f.engine->capture_param_inspector(0,0,p,detail);module_inspector_detail(detail);
        module_inspector_surface(false);{ModuleInspectorNoEngine no;param_lens_draw_bottom();}fold_test_geometry_pair(top);
        CHECK(!module_inspector_text("temp")&&!module_inspector_text("audition"));
        down=KEY_B;param_lens_input(down,0,0,false);CHECK(!param_lens_is_open()&&strain_focus_selected()==p);
    }
    lens_quick_begin();const auto saved=strain_bases(u);g_edit_slot=1;f.capture();CHECK(!param_lens_is_open()&&!f.engine->param_temp_active()&&strain_bases(u)==saved);g_edit_slot=0;
}
q15 strain_source_oracle(int i){
    if(i>=512)return 0;
    const double pi=3.141592653589793,env=i<64?.5-.5*std::cos(pi*i/64.):.5+.5*std::cos(pi*(i-64)/447.);
    return q15(std::lround(24575.*env*std::sin(2*pi*i/128.)));
}
void strain_setup(StrainUnit& u,const StrainValues& values){for(int p=0;p<6;++p){u.params[p].base=values[p];u.params[p].snap();}u.reset();}
StrainBuffer strain_oracle_render(StrainUnit& u,StrainBuffer b,int subdivision){
    for(int block=0;block<1024;block+=32){u.control_tick();for(int i=block;i<block+32;){
        int n=subdivision==1?1:subdivision==2?1+(i*17)%19:32;n=std::min(n,block+32-i);u.process(b.data()+i,n);i+=n;}}
    return b;
}
void test_strain_reference(){
    namespace r=strain_reference;CHECK(r::PARAMS==6&&r::SAMPLES==1024&&r::BURST==512&&r::COLUMNS==243&&r::X0==64&&r::ZERO==120&&r::HEIGHT==24);
    CHECK(sizeof(r::Plot)==1944);static_assert(ENGINE_SR==32000&&CONTROL_INTERVAL==32,"reference timing drift");
    StrainUnit defaults;CHECK(strain_bases(defaults)==STRAIN_DEFAULTS);
    StrainBuffer source{};for(int i=0;i<1024;++i){source[i]=strain_source_oracle(i);CHECK(r::source_sample(i)==source[i]);}
    std::vector<StrainValues> cases{STRAIN_DEFAULTS};
    for(auto change:{std::pair<int,int>{3,32767},{2,27852},{4,32767},{1,19660},{0,32767}}){auto v=STRAIN_DEFAULTS;v[change.first]=change.second;cases.push_back(v);}
    for(int p=0;p<6;++p)for(int value:{p==1?-32767:0,32767}){auto v=STRAIN_DEFAULTS;v[p]=value;cases.push_back(v);}
    for(int edge:{0,32767})for(int bias:{-32767,0,32767})cases.push_back({q15(edge),q15(bias),q15(edge),q15(edge),q15(edge),q15(edge)});
    int samples=0,plots=0,checks=0;
    for(const auto& values:cases){
        StrainUnit actual;strain_setup(actual,values);const auto out=strain_oracle_render(actual,source,0);actual.reset();CHECK(strain_oracle_render(actual,source,0)==out);++checks;
        for(int subdivision:{1,2}){strain_setup(actual,values);CHECK(strain_oracle_render(actual,source,subdivision)==out);++checks;}
        strain_setup(actual,values);CHECK(strain_oracle_render(actual,StrainBuffer{},0)==StrainBuffer{});++checks;
        auto dry=values;dry[5]=0;strain_setup(actual,dry);CHECK(strain_oracle_render(actual,source,0)==source);++checks;
        for(bool bypass:{false,true}){
            r::Plot a,again;StrainBuffer captured_in{},captured_out{};strain_probe_input=&captured_in;strain_probe_output=&captured_out;strain_probe_offset=0;
            const auto before=strain_process_calls;r::plot(values.data(),bypass,a);strain_probe_input=strain_probe_output=nullptr;
            CHECK(strain_process_calls-before==(bypass?0:32));CHECK(strain_probe_offset==(bypass?0:1024));
            if(!bypass)for(int i=0;i<1024;++i){CHECK(captured_in[i]==source[i]&&captured_out[i]==out[i]);++samples;}
            r::plot(values.data(),bypass,again);
            auto audit_trace=[&](const r::Trace& trace,const r::Trace& repeat,const StrainBuffer& input){
                int first[243],last[243],lo[243],hi[243],counts[243]={};std::fill(lo,lo+243,256);std::fill(hi,hi+243,-1);
                for(int i=0;i<1024;++i){const int col=i*242/1023,yy=120-int(std::lround(double(input[i])*24/32768.));CHECK(r::y(input[i])==yy);
                    if(!counts[col]++)first[col]=yy;
                    last[col]=yy;lo[col]=std::min(lo[col],yy);hi[col]=std::max(hi[col],yy);}
                for(int col=0;col<243;++col){CHECK(counts[col]>0);CHECK(trace.first[col]==first[col]&&trace.last[col]==last[col]&&trace.lo[col]==lo[col]&&trace.hi[col]==hi[col]);
                    CHECK(repeat.first[col]==first[col]&&repeat.last[col]==last[col]&&repeat.lo[col]==lo[col]&&repeat.hi[col]==hi[col]);}
            };
            audit_trace(a.source,again.source,source);audit_trace(a.shaped,again.shaped,bypass?source:out);++plots;
        }
    }
    // Mix0 does not pause the loop: restore the same mix after identical dry/wet history.
    StrainValues wet=STRAIN_DEFAULTS,dry=wet;wet[5]=32767;dry[5]=0;StrainUnit a,b;strain_setup(a,wet);strain_setup(b,dry);
    strain_oracle_render(a,source,0);strain_oracle_render(b,source,0);b.params[5].base=32767;b.params[5].snap();
    CHECK(strain_oracle_render(a,StrainBuffer{},0)==strain_oracle_render(b,StrainBuffer{},0));
    char text[32];for(int p=0;p<6;++p)for(int value:{p==1?-32767:0,15000,32767}){
        module_inspector_value_text(text,sizeof text,UnitKind::Strain,p,q15(value));char* suffix=nullptr;const double number=std::strtod(text,&suffix);
        if(p==0){CHECK(std::string(suffix)=="x");CHECK(std::abs(number-(32767.+7*value)/32768.)<.0051);}
        else{CHECK(std::string(suffix)=="%");CHECK(std::abs(number-value*100./32767.)<.51);}
    }
    const auto start=std::chrono::steady_clock::now();r::Plot plot;volatile unsigned checksum=0;
    for(int i=0;i<200;++i){auto v=STRAIN_DEFAULTS;v[0]+=i;r::plot(v.data(),false,plot);checksum+=plot.shaped.first[100];}
    const double us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count()/200;
    std::printf("strain reference: %d plots / %d exact private DSP samples; 1024 independent burst samples; %d reset/subdivision/silence/dry checks; mix0 loop preserved; uncached host %.2fus/plot checksum %u (not 3ds timing)\n",plots,samples,checks,us,unsigned(checksum));
}
void test_strain_frames(){
    ModuleInspectorFixture f(UnitKind::Strain);auto& u=*static_cast<StrainUnit*>(f.module);strain_config(u);strain_live_unit=&u;strain_live_process_calls=0;
    f.capture();const auto calls=strain_process_calls;const auto start=std::chrono::steady_clock::now();for(int i=0;i<200;++i)f.capture();
    const double cached_us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count()/200;CHECK(strain_process_calls==calls);
    for(int p=0;p<6;++p){++u.params[p].effective;const auto before=strain_process_calls;f.capture();CHECK(strain_process_calls==before+32);f.capture();CHECK(strain_process_calls==before+32);}
    auto plot_pixels=[&]{f.capture();module_inspector_surface(false);{ModuleInspectorNoEngine no;f.lower();}
        std::vector<u32> crop;for(int y=90;y<=150;++y)for(int x=64;x<=306;++x)crop.push_back(pixels[y*320+x]);return crop;};
    strain_config(u);strain_focus_select(3);const auto clean=plot_pixels();u.params[3].effective=32767;const auto teeth=plot_pixels();CHECK(teeth!=clean);
    u.params[3].base=32767;u.params[3].effective=12000;CHECK(plot_pixels()==clean);strain_config(u);
    g_seq_lock_armed=true;g_seq_lock_step=7;CHECK(f.engine->set_step_lock(0,7,0,3,32767));CHECK(plot_pixels()==teeth);
    CHECK(f.engine->remove_step_lock(0,7,0,3));CHECK(plot_pixels()==clean);g_seq_lock_armed=false;
    const auto before_bypass=strain_process_calls;u.bypass=true;const auto bypass=plot_pixels();CHECK(strain_process_calls==before_bypass);
    u.bypass=false;u.params[5].effective=0;CHECK(plot_pixels()==bypass);CHECK(strain_process_calls==before_bypass+32);
    strain_config(u);CHECK(plot_pixels()==clean);
    const auto before_mute=strain_process_calls;f.engine->chains[0].mute=true;const auto muted_wet=plot_pixels();CHECK(strain_process_calls==before_mute);
    u.params[5].effective=0;const auto muted_dry=plot_pixels();CHECK(muted_wet!=muted_dry);f.engine->chains[0].mute=false;strain_config(u);CHECK(plot_pixels()==clean);int frames=0;
    for(int p=0;p<6;++p)for(int edge:{0,32767})for(int bias:{-32767,0,32767}){
        strain_config(u);u.params[p].base=edge;u.params[1].base=bias;for(int n=0;n<6;++n)u.params[n].snap();strain_focus_select(p);f.capture();const auto before=strain_process_calls;
        const auto top=module_inspector_top();const int count=objects;CHECK(module_inspector_top(false,true)==top);
        ParamInspector detail;f.engine->capture_param_inspector(0,0,p,detail);module_inspector_detail(detail);
        module_inspector_surface(false);{ModuleInspectorNoEngine no;f.lower();}fold_test_geometry_pair(count);
        const auto lower=pixels;const int lower_count=objects;const auto saved=strain_bases(u);for(int n=0;n<6;++n)u.params[n].base=12345;
        g_ui_snapshot={};module_inspector_surface(false);{ModuleInspectorNoEngine no;f.lower();}CHECK(pixels==lower&&objects==lower_count&&module_inspector_top()==top);
        CHECK(strain_process_calls==before);for(int n=0;n<6;++n)u.params[n].base=saved[n];++frames;
    }
    for(int state=0;state<2;++state){u.bypass=state==0;f.engine->chains[0].mute=state==1;f.capture();module_inspector_top();CHECK(module_inspector_text(state==0?"bypassed":"chain muted"));}
    CHECK(strain_live_process_calls==0);strain_live_unit=nullptr;g_overlay=Overlay::None;f.capture();module_inspector_surface(true);CHECK(!module_inspector_draw_top());module_inspector_surface(false);f.lower();CHECK(texts.empty());
    std::printf("strain frozen extrema: %d pairs; six independent cache invalidations; 200 cache hits %.2fus/capture; no live-unit/draw DSP (host only)\n",frames,cached_us);
}
void dump_strain_runtime(const std::filesystem::path& directory){
    std::filesystem::create_directories(directory);std::ofstream manifest(directory/"manifest.tsv");
    manifest<<"frame\tselected\tdrive\tbias\tbody\tteeth\tfeedback\tmix\ttop_objects\tbottom_objects\tshared_objects\tbottom_clipped\tbottom_overlaps\n";
    ModuleInspectorFixture f(UnitKind::Strain);auto& u=*f.module;
    Unit* moved=f.engine->replace_unit(0,0,new OscUnit);delete f.engine->replace_unit(0,1,moved);delete f.engine->replace_unit(0,2,new VcaUnit);g_edit_slot=1;
    auto reset=[&]{strain_config(u);strain_focus_select(0);};reset();
    auto save=[&](const std::string& name,bool lens=false){f.capture();const auto before=strain_process_calls;module_inspector_top(lens,true);const int top=objects;dump_surface(directory,name+"_top");
        module_inspector_surface(false);{ModuleInspectorNoEngine no;if(lens)param_lens_draw_bottom();else f.lower();}fold_test_geometry_pair(top);dump_surface(directory,name+"_bottom");CHECK(strain_process_calls==before);
        manifest<<name<<'\t'<<strain_focus_selected();for(auto value:strain_bases(u))manifest<<'\t'<<value;
        manifest<<'\t'<<top<<'\t'<<objects<<'\t'<<top+objects<<'\t'<<outside_lines+outside_rects<<'\t'<<text_overlaps<<'\n';};
    save("01_default");
    const char* names[]={"02_teeth","03_body","04_feedback","05_bias","06_drive","07_drive_min","08_negative_bias","09_mix_dry","10_bias_full","11_mix_full"};
    const int param[]={3,2,4,1,0,0,1,5,1,5},values[]={32767,27852,32767,19660,32767,0,-32767,0,32767,32767};
    for(int i=0;i<10;++i){reset();u.params[param[i]].base=values[i];u.params[param[i]].snap();strain_focus_select(param[i]);save(names[i]);}
    reset();u.params[2].src=ModSource::SigilB;u.params[2].mod_depth=-18000;u.params[2].effective=24000;strain_focus_select(2);save("12_live_mod");
    g_seq_lock_armed=true;g_seq_lock_step=7;CHECK(f.engine->set_step_lock(0,7,1,2,0));save("13_lock");g_seq_lock_armed=false;
    reset();strain_focus_select(1);f.open();save("14_mod",true);lens_quick_begin();param_lens_touch_down(180,129);param_lens_touch_drag(224,129);save("15_temp",true);
    lens_frame(0,0,KEY_L);param_lens_touch_release();param_lens_cancel();strain_release();
    reset();u.bypass=true;save("16_bypass");u.bypass=false;f.engine->chains[0].mute=true;save("17_muted");f.engine->chains[0].mute=false;
    f.engine->clear_track(0);delete f.engine->replace_unit(0,2,new FmUnit);for(int i=0;i<SEQ_LOCKS;++i)CHECK(f.engine->set_step_lock(0,7,2,i,1234));
    g_seq_lock_armed=true;save("18_lock_full");g_seq_lock_armed=false;
}
