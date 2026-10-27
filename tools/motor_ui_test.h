// Included by the native raster harness: exact MOTOR reference, controls and actual dual renders.
void motor_geometry(int top=0){module_inspector_geometry();CHECK(top+objects<=8192);maximum_shared_objects=std::max(maximum_shared_objects,top+objects);}
struct MotorFixture:Fixture{
    MotorUnit* motor=nullptr;
    MotorFixture(){motor=new MotorUnit;delete engine->replace_unit(0,0,motor);g_edit_chain=g_edit_slot=0;g_view=View::Chains;g_overlay=Overlay::Editor;param_lens_cancel();motor_focus_release();motor_focus_select(MotorUnit::P_RPM);capture();}
    ~MotorFixture(){g_overlay=Overlay::None;param_lens_capture();}
    void capture(){engine->capture_ui_snapshot(g_ui_snapshot,0,0);param_lens_capture();}
    void lower(){module_inspector_surface(false);{ModuleInspectorNoEngine frozen;motor_focus_draw(*motor);}motor_geometry();}
    void pair(){capture();module_inspector_top(false,true);const int top=objects;lower();motor_geometry(top);}
    void touch(int x,int y){CHECK(motor_focus_touch_body(*motor,x,y));}
    void drag(int x,int y){motor_focus_drag(*motor,x,y);}void up(){motor_focus_release();g_drag=Drag::None;}
    void snap(){for(int p=0;p<6;++p)motor->params[p].snap();motor->control_tick();}
    static std::pair<int,int>point(int p){static const std::pair<int,int> xy[]={{70,155},{50,195},{150,195},{230,155},{255,195},{160,44}};return xy[p];}
    static int extent(int p){static const int values[]={138,82,82,128,82};return values[p];}
};
void test_motor_exact_reference(){
    for(const auto& values:std::vector<std::array<int,6>>{{0,0,0,0,0,0},{10000,8192,19660,18000,18000,16384},{32767,32767,32767,32767,32767,32767},{22000,31000,12000,27000,24000,0}}){
        MotorUnit u;for(int p=0;p<6;++p)u.params[p].base=u.params[p].effective=q15(values[p]);u.reset();u.control_tick();
        constexpr int N=2048;q15 actual[N]={},expected[N]={},pre[N]={};u.process(actual,N);
        motor_focus_reset_reference(pre,expected,N,q15(values[0]),q15(values[1]),q15(values[2]),q15(values[3]),q15(values[4]),q15(values[5]));
        for(int i=0;i<N;++i){CHECK(actual[i]==expected[i]);CHECK(pre[i]>=-32768&&pre[i]<=32767);}
    }
}
void test_motor_controls_locks(){MotorFixture f;auto&u=*f.motor;CHECK(module_inspector_supports(UnitKind::Motor));
    for(int p=0;p<5;++p){const auto xy=MotorFixture::point(p);u.params[p].base=10000;const auto before=u.params[p].base;f.touch(xy.first,xy.second);CHECK(u.params[p].base==before&&motor_focus_selected()==p);f.drag(xy.first+7,xy.second);CHECK(u.params[p].base==q15(10000+7*32767/MotorFixture::extent(p)));f.up();}
    f.touch(250,44);CHECK(MotorUnit::P_SOURCE==motor_focus_selected()&&u.params[MotorUnit::P_SOURCE].base==32767);
    f.touch(70,44);CHECK(u.params[MotorUnit::P_SOURCE].base==0);
    g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=5;g_view=View::Sequencer;const q15 base=u.params[MotorUnit::P_RPM].base;CHECK(f.engine->set_step_lock(0,5,0,MotorUnit::P_RPM,9000));f.touch(70,155);f.drag(77,155);CHECK(u.params[MotorUnit::P_RPM].base==base&&seq_display_value(0,MotorUnit::P_RPM,base)==9000+7*32767/138);f.up();
    g_seq_lock_armed=false;g_view=View::Chains;u.params[MotorUnit::P_LOAD].src=ModSource::SigilA;u.params[MotorUnit::P_LOAD].mod_depth=1000;motor_focus_select(MotorUnit::P_LOAD);f.capture();CHECK(param_lens_touch_down(250,232));param_lens_touch_release();CHECK(param_lens_is_open());param_lens_touch_down(100,166);param_lens_touch_drag(106,166);param_lens_touch_release();CHECK(u.params[MotorUnit::P_LOAD].mod_depth==1000+6*65534/271);u32 down=KEY_B;param_lens_input(down,0,0,false);CHECK(!param_lens_is_open());
}
void test_motor_frozen_frames(){MotorFixture f;auto&u=*f.motor;int pairs=0;
    for(const auto& values:std::vector<std::array<int,6>>{{10000,8192,19660,18000,18000,0},{22000,25000,28000,30000,23000,16384},{32767,32767,5000,12000,32767,32767}}){
        for(int p=0;p<6;++p)u.params[p].base=u.params[p].effective=q15(values[p]);
        f.snap();q15 audio[512]={};u.process(audio,512);
        motor_focus_select(pairs==0?MotorUnit::P_RPM:pairs==1?MotorUnit::P_RESONANCE:MotorUnit::P_SOURCE);
        f.capture();const auto top=module_inspector_top(false,true);const int top_count=objects;
        f.lower();const auto lower=pixels;const int bottom=objects;motor_geometry(top_count);
        for(int p=0;p<6;++p)u.params[p].base=1234;
        g_ui_snapshot={};CHECK(module_inspector_top()==top);module_inspector_surface(false);
        {ModuleInspectorNoEngine frozen;motor_focus_draw(u);}
        CHECK(pixels==lower&&objects==bottom);++pairs;
    }
    std::printf("motor frames: %d frozen exact reset-reference duals / budget\n",pairs);
}
// Focused presentation checks deliberately use the real captured runtime frame.
void test_motor_clear_presentation(){
    MotorFixture f;auto& u=*f.motor;MotorUnit defaults;int pairs=0;
    const char* names[]={"rate","jitter","res","load","gain","source"};
    auto at=[](const char* text,int x,int y){for(const auto& t:texts)if(t.text==text&&t.call_x==x&&t.call_y==y)return true;return false;};
    for(int p=0;p<6;++p)for(int raw:{0,int(defaults.params[p].base),32767}){
        u.params[p].base=q15(raw);motor_focus_select(p);f.snap();f.capture();module_inspector_top(false,true);const int top=objects;
        char value[32],expected[32];module_inspector_value_text(value,sizeof value,UnitKind::Motor,p,q15(raw));
        if(p==MotorUnit::P_RPM)std::snprintf(expected,sizeof expected,"%uhz",18u+unsigned(raw)*742u/32767u);
        else if(p==MotorUnit::P_SOURCE){const char* source[]={"saw","pulse","mix"};std::snprintf(expected,sizeof expected,"%s",source[std::min(2,raw*3/32768)]);}
        else std::snprintf(expected,sizeof expected,"%u%%",(unsigned(raw)*100u+16383u)/32767u);
        CHECK(std::string(value)==expected&&at(names[p],14,41));
        for(const char* retired:{"stored base","control ","target ","potential range","effective"})CHECK(!module_inspector_text(retired));
        CHECK(!module_inspector_text("live  ")&&module_inspector_text("mod off"));
        for(const auto& t:texts)if(t.y>=70&&t.y<134)CHECK(t.scale==1); // no chunky native hero fallback
        if(p!=MotorUnit::P_SOURCE){
            module_inspector_value_text(value,sizeof value,UnitKind::Motor,p,0);CHECK(at(value,14,179));
            module_inspector_value_text(value,sizeof value,UnitKind::Motor,p,32767);CHECK(at(value,386-ui::text_w(value),179));
        }
        f.lower();motor_geometry(top);CHECK(module_inspector_text("36.5ms model")&&module_inspector_text("outline base / fill live"));
        CHECK(!module_inspector_text("live phase")&&!module_inspector_text("36ms reset"));
        for(const auto& t:texts)if(t.call_y>=72&&t.call_y<=128)CHECK(t.x+t.w<=14); // scale only, no labels on traces
        ++pairs;
    }
    auto& res=u.params[MotorUnit::P_RESONANCE];res.base=12000;res.src=ModSource::SigilB;res.mod_depth=17000;f.snap();res.effective=26000;
    motor_focus_select(MotorUnit::P_RESONANCE);f.capture();module_inspector_top();
    CHECK(module_inspector_text("live  79%")&&module_inspector_text("sg.b > res  +51%"));
    // A rounded-identical applied value does not get repeated; a real layer still does.
    res.src=ModSource::None;res.mod_depth=0;res.effective=12001;f.capture();module_inspector_top();CHECK(!module_inspector_text("live  "));
    res.step_lock_override=true;res.step_lock=12001;f.capture();module_inspector_top();
    CHECK(!module_inspector_text("live  ")&&module_inspector_text("via step lock"));res.step_lock_override=false;
    g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;
    CHECK(f.engine->set_step_lock(0,7,0,MotorUnit::P_RESONANCE,9000));res.effective=26000;f.capture();module_inspector_top();
    CHECK(module_inspector_text("edit lock")&&module_inspector_text("live  79%"));f.lower();CHECK(module_inspector_text("p-lock / name: remove"));
    g_view=View::Chains;g_seq_lock_armed=false;u.bypass=true;f.capture();module_inspector_top();CHECK(module_inspector_text("bypassed"));
    u.bypass=false;f.engine->chains[0].mute=true;f.capture();module_inspector_top();CHECK(module_inspector_text("muted"));
    std::printf("motor clear: %d extrema/default pairs / single physical hero / conditional live+layer / route / lock / status\n",pairs);
}
void test_motor_gothic_source(){
    MotorFixture f;auto& u=*f.motor;motor_focus_select(MotorUnit::P_SOURCE);int masks=0;
    const char* names[]={"saw","pulse","mix"};const int raw_values[]={0,1,10922,10923,16384,21845,21846,32766,32767};
    for(int raw:raw_values){const int source=std::min(2,raw*3/32768);u.params[MotorUnit::P_SOURCE].base=q15(raw);f.snap();f.capture();
        const auto actual=module_inspector_top(false,true);const int top=objects;CHECK(!module_inspector_text("live  "));
        // Source is a three-state choice, not a continuous raw Q15 position.
        const int bx=14+source*186;CHECK(actual[161*400+bx]==m::ink());
        CHECK(module_inspector_text("saw")&&module_inspector_text("pulse")&&module_inspector_text("mix"));
        CHECK(m::heading_width(names[source])>ui::text_w(names[source]));
        module_inspector_surface(true);m::paper(400);m::heading(14,84,names[source],m::ink());
        for(int y=84;y<132;++y){for(int x=14;x<14+m::heading_width(names[source]);++x)CHECK(actual[y*400+x]==pixels[y*400+x]);}
        ++masks;
        f.lower();motor_geometry(top);const auto bottom=pixels;
        const int sx=70+source*90;CHECK(pixels[53*320+sx]==m::spot());CHECK(pixels[54*320+sx]==m::paper_color(54));
        u.params[MotorUnit::P_SOURCE].base=12345;u.params[MotorUnit::P_SOURCE].effective=23456;g_ui_snapshot={};
        CHECK(module_inspector_top()==actual);f.lower();CHECK(pixels==bottom);
    }
    // Lower underline is the edited source; only a differing actual state adds a fill.
    u.params[MotorUnit::P_SOURCE].base=0;u.params[MotorUnit::P_SOURCE].effective=32767;u.params[MotorUnit::P_SOURCE].target=16384;
    f.capture();module_inspector_top();CHECK(module_inspector_text("live  mix"));f.lower();
    CHECK(pixels[53*320+70]==m::spot()&&pixels[54*320+160]==m::paper_color(54));
    for(int dy=-2;dy<=2;++dy)for(int dx=-(2-std::abs(dy));dx<=2-std::abs(dy);++dx)CHECK(pixels[(54+dy)*320+250+dx]==m::spot());
    std::printf("motor gothic source: %d baked saw/pulse/mix masks / exact discrete thresholds / frozen / base!=live!=target\n",masks);
}
void test_motor_live_rails(){
    MotorFixture f;auto& u=*f.motor;int cases=0;
    auto bases=[&](){std::array<q15,6> result{};for(int p=0;p<6;++p)result[p]=u.params[p].base;return result;};
    auto verify=[&](int p,int base,int live){
        const int xs[]={14,14,118,178,222},ys[]={169,211,211,169,211};
        const int x=xs[p],y=ys[p],w=MotorFixture::extent(p),bx=x+base*w/32767,lx=x+live*w/32767;
        for(int dy=-2;dy<=2;++dy)for(int dx=-(2-std::abs(dy));dx<=2-std::abs(dy);++dx)CHECK(pixels[(y+dy)*320+lx+dx]==m::spot());
        const u32 outline=motor_focus_selected()==p?m::spot():m::ink();CHECK(pixels[(y-4)*320+bx]==outline);
        for(int dx:{-4,-3,3,4})if(std::abs(bx+dx-lx)>2)CHECK(pixels[y*320+bx+dx]==outline);
        int rail_pixels=0;for(int dx=0;dx<=w;++dx){const int at=x+dx;if(std::abs(at-bx)<=4||std::abs(at-lx)<=4)continue;
            const bool tick=dx==0||dx==w/2||dx==w;CHECK(pixels[y*320+at]==m::branch());
            CHECK(pixels[(y-1)*320+at]==(tick?m::branch():m::paper_color(y-1)));++rail_pixels;}
        CHECK(rail_pixels>50);++cases;
    };
    for(int p=0;p<5;++p)for(int base:{0,16384,32767})for(int live:{0,6000,16384,27000,32767}){
        auto& param=u.params[p];param.base=q15(base);param.effective=q15(live);param.target=q15(32767-live);
        motor_focus_select((p+1)%6);f.capture();f.lower();verify(p,base,live);const auto bottom=pixels;const int dirty=mutations;
        param.base=1234;param.effective=2345;g_ui_snapshot={};f.lower();CHECK(pixels==bottom&&mutations==dirty);
    }
    for(int p=0;p<5;++p){auto& param=u.params[p];param.base=16000;param.src=ModSource::SigilA;param.mod_depth=12000;
        param.snap();param.resolve(0);motor_focus_select(p);const auto before=bases();const int dirty=mutations;
        for(int source:{-32767,-22000,0,12000,32767,0}){param.resolve(q15(source));CHECK(param.effective!=param.target);
            f.capture();f.lower();verify(p,16000,param.effective);CHECK(bases()==before&&mutations==dirty);}
    }
    g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;
    CHECK(f.engine->set_step_lock(0,7,0,MotorUnit::P_GAIN,9000));u.params[MotorUnit::P_GAIN].effective=26000;
    f.capture();f.lower();verify(MotorUnit::P_GAIN,9000,26000);g_view=View::Chains;g_seq_lock_armed=false;f.pair();
    std::printf("motor live rails: %d positions / 5 real smoothed params / ordinary tracks / hollow base + filled live / frozen / lock\n",cases);
}
void test_motor_truthful_graph(){
    MotorFixture f;auto& u=*f.motor;int columns=0,raw_segments=0;
    struct Capture{std::vector<GranLine> lines;Capture(){gran_lines=&lines;}~Capture(){gran_lines=nullptr;}};
    auto ordinate=[](q15 sample){return std::max(-28,std::min(28,int(sample)*28/32768));};
    for(const auto& values:std::vector<std::array<int,6>>{{10000,8192,19660,18000,18000,0},{22000,27000,29000,28000,22000,32767},{30000,32767,8000,12000,32767,16384},{0,0,0,0,0,16384}}){
        for(int p=0;p<6;++p){u.params[p].base=q15(values[p]);}
        f.snap();f.capture();q15 pre[292*4],out[292*4];
        motor_focus_reset_reference(pre,out,292*4,values[0],values[1],values[2],values[3],values[4],values[5]);
        Capture capture;f.lower();gran_lines=nullptr;int last_raw=-1,first_out=int(capture.lines.size()),raw_count=0,out_count=0;
        for(size_t i=0;i<capture.lines.size();++i){const auto& line=capture.lines[i];
            if(line.color==m::ghost()&&line.x0>=14&&line.x0<305&&line.x1==line.x0+1&&line.y0>=72&&line.y0<=128&&line.y1>=72&&line.y1<=128){
                const int col=line.x1-14;CHECK(line.y0==100-ordinate(pre[(col-1)*4+3])&&line.y1==100-ordinate(pre[col*4+3]));last_raw=int(i);++raw_count;++raw_segments;}
            if(line.color==m::spot()&&line.x0==line.x1&&line.x0>=14&&line.x0<=305&&line.y0>=72&&line.y1<=129){
                const int col=line.x0-14;int lo=32767,hi=-32768;for(int k=0;k<4;++k){lo=std::min(lo,int(out[col*4+k]));hi=std::max(hi,int(out[col*4+k]));}
                const int y0=100-ordinate(q15(hi)),y1=std::max(y0+1,100-ordinate(q15(lo)));
                CHECK(line.y0==y0&&line.y1==y1&&line.y1>line.y0);first_out=std::min(first_out,int(i));++out_count;++columns;}
        }
        CHECK(raw_count==291&&out_count==292&&last_raw<first_out);
        if(values[4]==0)for(int x=14;x<=305;++x)CHECK(pixels[100*320+x]==m::spot()&&pixels[101*320+x]==m::spot());
    }
    auto crop=[](int x0,int y0,int x1,int y1){std::vector<u32> result;for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x)result.push_back(pixels[y*320+x]);return result;};
    // Published telemetry scalars are independent of the reset model and gain position.
    u.params[MotorUnit::P_GAIN].base=24000;f.snap();f.capture();
    auto telemetry=[&](unsigned phase,unsigned energy){g_ui_snapshot.effect_focus_valid=true;g_ui_snapshot.effect_focus_kind=uint8_t(UnitKind::Motor);
        g_ui_snapshot.effect_focus_chain=g_ui_snapshot.effect_focus_slot=0;g_ui_snapshot.effect_focus_audible=true;
        g_ui_snapshot.generator_phase_hi=uint8_t(phase>>8);g_ui_snapshot.generator_phase_lo=uint8_t(phase);g_ui_snapshot.generator_energy=uint16_t(energy);
        motor_focus_capture();f.lower();};
    motor_focus_select(MotorUnit::P_RPM);telemetry(0,10000);const auto initial=pixels;const auto graph=crop(14,70,306,132),gain=crop(216,180,312,221);
    telemetry(65535,10000);CHECK(pixels==initial); // no phase cursor or artificial animation anywhere
    motor_focus_select(MotorUnit::P_RESONANCE);telemetry(23456,10000);CHECK(crop(14,70,306,132)==graph&&crop(216,180,312,221)==gain);
    const auto quiet=pixels;telemetry(23456,30000);CHECK(pixels!=quiet);int changed=0;
    for(int y=0;y<240;++y)for(int x=0;x<320;++x)if(pixels[y*320+x]!=quiet[y*320+x]){CHECK(x>=184&&x<=239&&y>=10&&y<=18);++changed;}
    CHECK(changed>0&&crop(14,70,306,132)==graph&&crop(216,180,312,221)==gain);
    const auto frozen=pixels;const int dirty=mutations;for(auto& p:u.params){p.base=1234;p.effective=2345;}g_ui_snapshot={};f.lower();CHECK(pixels==frozen&&mutations==dirty);
    std::printf("motor truthful graph: %d exact four-probe extrema / %d last-sample raw segments / nonzero silent output / phase+selection invariant / isolated peak / frozen\n",columns,raw_segments);
}
void dump_motor_runtime(const std::filesystem::path&directory){std::filesystem::create_directories(directory);std::ofstream manifest(directory/"manifest.tsv");manifest<<"frame\ttop\tbottom\tshared\tclipped\toverlaps\n";MotorFixture f;auto&u=*f.motor;delete f.engine->replace_unit(0,1,new FilterUnit);delete f.engine->replace_unit(0,2,new VcaUnit);
    auto save=[&](const char*name,int selected){f.snap();
        if(selected==MotorUnit::P_RESONANCE){auto& res=u.params[MotorUnit::P_RESONANCE];res.resolve(0);for(int i=0;i<6;++i)res.resolve(-32767);u.control_tick();}
        q15 audio[768]={};u.process(audio,768);motor_focus_select(selected);f.capture();module_inspector_top(false,true);const int top=objects;dump_surface(directory,std::string(name)+"_top");f.lower();motor_geometry(top);dump_surface(directory,std::string(name)+"_bottom");manifest<<name<<'\t'<<top<<'\t'<<objects<<'\t'<<top+objects<<'\t'<<outside_lines+outside_rects<<'\t'<<text_overlaps<<'\n';};
    u.params[0].base=10000;u.params[1].base=8192;u.params[2].base=19660;u.params[3].base=18000;u.params[4].base=18000;u.params[5].base=0;save("01_saw",MotorUnit::P_RPM);
    u.params[0].base=22000;u.params[1].base=27000;u.params[2].base=29000;u.params[3].base=28000;u.params[4].base=22000;u.params[5].base=32767;u.params[2].src=ModSource::SigilB;u.params[2].mod_depth=17000;save("02_mix_res",MotorUnit::P_RESONANCE);
    u.params[0].base=30000;u.params[1].base=32767;u.params[2].base=8000;u.params[3].base=12000;u.params[4].base=32767;u.params[5].base=16384;u.params[2].src=ModSource::None;u.params[2].mod_depth=0;save("03_pulse",MotorUnit::P_SOURCE);
}
