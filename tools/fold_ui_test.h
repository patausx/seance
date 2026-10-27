// Included after module_inspector_test.h; real controllers, pre-GPU captures and C++ pixels.
// The UI-only shaping model is checked against unchanged FoldUnit::process, not a copied formula.
std::array<q15,5> fold_test_bases(const Unit& u) {
    std::array<q15,5> result{};for(int p=0;p<5;++p)result[p]=u.params[p].base;return result;
}
void fold_test_release() {fold_focus_release();g_drag=Drag::None;}
int fold_test_span(int p) {return p==0?128:p==2?106:82;}
int fold_test_range(int p) {return p==3?65534:32767;}
void fold_test_delta(ModuleInspectorFixture& f,int p,int delta) {
    const auto xy=f.point(p);f.drag(xy.first+delta,xy.second);
}
void fold_test_configure(FoldUnit& u,int drive=8192,int mode=0,int bias=0,int trim=32767,int mix=32767) {
    const q15 values[]={q15(drive),q15(trim),q15(mode),q15(bias),q15(mix)};
    for(int p=0;p<5;++p){u.params[p].base=values[p];u.params[p].src=ModSource::None;u.params[p].mod_depth=0;u.params[p].snap();}
    u.bypass=false;fold_focus_select(0);
}
void test_fold_controls() {
    ModuleInspectorFixture f(UnitKind::Fold);auto& u=*static_cast<FoldUnit*>(f.module);
    CHECK(u.num_params==5&&module_inspector_supports(UnitKind::Fold));
    CHECK(FoldUnit::P_DRIVE==0&&FoldUnit::P_TRIM==1&&FoldUnit::P_MODE==2&&FoldUnit::P_BIAS==3&&FoldUnit::P_MIX==4);
    struct Box {int x0,y0,x1,y1,p;};
    // Approved rectangles are half-open, matching the native hit() helper.
    const Box boxes[]={{8,27,154,73,0},{112,177,208,220,1},{164,27,314,73,2},{8,177,104,220,3},{216,177,312,220,4}};
    const Drag drags[]={Drag::FoldDrive,Drag::FoldTrim,Drag::FoldMode,Drag::FoldBias,Drag::FoldMix};
    for(int edge:{0,17000,32767})for(const auto& b:boxes)for(int corner=0;corner<4;++corner) {
        u.params[b.p].base=q15(b.p==3&&edge==0?-32767:edge);
        const auto before=fold_test_bases(u);const auto locks=f.hash();const int dirty=mutations;
        const int x=corner&1?b.x1-1:b.x0,y=corner&2?b.y1-1:b.y0;
        CHECK(f.down(x,y));CHECK(fold_focus_selected()==b.p&&g_drag==drags[b.p]);
        CHECK(fold_test_bases(u)==before&&f.hash()==locks&&mutations==dirty);
        f.drag(x,y);CHECK(fold_test_bases(u)==before&&mutations==dirty);fold_test_release();
    }
    for(int p=0;p<5;++p) {
        for(int n=0;n<5;++n)u.params[n].base=13000;
        const auto xy=f.point(p);CHECK(f.down(xy.first,xy.second));fold_test_delta(f,p,6);
        CHECK(u.params[p].base==13000+6*fold_test_range(p)/fold_test_span(p));
        for(int n=0;n<5;++n)if(n!=p)CHECK(u.params[n].base==13000);
        // Vertical motion cannot alter horizontal control origins.
        const q15 moved=u.params[p].base;f.drag(xy.first+6,xy.second-70);CHECK(u.params[p].base==moved);
        fold_test_delta(f,p,1000);CHECK(u.params[p].base==32767);
        fold_test_delta(f,p,-1000);CHECK(u.params[p].base==(p==3?-32767:0));fold_test_release();
    }
    // MODE anchor words are one continuous control, never quantized switches.
    u.params[2].base=9000;
    for(int x:{180,238,294}){CHECK(f.down(x,62));CHECK(u.params[2].base==9000&&fold_focus_selected()==2);fold_test_release();}
    const auto before=fold_test_bases(u);const int dirty=mutations;const auto locks=f.hash();
    for(auto xy:{std::pair<int,int>{160,50},{108,200},{212,200},{80,175},{80,221},{90,111},{160,111},{294,111},{190,150},{296,12}}) {
        f.down(xy.first,xy.second);f.drag(xy.first+30,xy.second-20);
        CHECK(fold_test_bases(u)==before&&mutations==dirty&&f.hash()==locks);fold_test_release();
    }
    const bool bypass=u.bypass;f.down(260,10);CHECK(u.bypass!=bypass);fold_test_release();
    fold_focus_select(4);fold_focus_select(-1);fold_focus_select(5);CHECK(fold_focus_selected()==4);
}
void test_fold_locks() {
    ModuleInspectorFixture f(UnitKind::Fold);auto& u=*static_cast<FoldUnit*>(f.module);
    g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;g_view=View::Sequencer;
    const std::pair<int,int> labels[]={{20,34},{125,184},{180,34},{20,184},{230,184}};
    const auto bases=fold_test_bases(u);
    for(int p=0;p<5;++p) {
        const q15 origin=p==3?-4000:4000;CHECK(f.engine->set_step_lock(0,7,0,p,origin));
        const auto xy=f.point(p);const auto locked=f.hash();const int dirty=mutations;
        CHECK(f.down(xy.first,xy.second));CHECK(fold_focus_selected()==p&&f.hash()==locked&&mutations==dirty);
        f.drag(xy.first,xy.second);CHECK(f.hash()==locked&&mutations==dirty);
        fold_test_delta(f,p,6);CHECK(seq_display_value(0,p,u.params[p].base)==origin+6*fold_test_range(p)/fold_test_span(p));
        CHECK(fold_test_bases(u)==bases);const auto moved=f.hash();g_seq_lock_step=8;
        fold_test_delta(f,p,20);CHECK(f.hash()==moved);g_seq_lock_step=7;fold_test_release();
        f.drag(xy.first+30,xy.second);CHECK(f.hash()==moved);
        CHECK(f.down(labels[p].first,labels[p].second));CHECK(seq_display_value(0,p,u.params[p].base)==u.params[p].base);
        CHECK(fold_focus_selected()==p&&fold_test_bases(u)==bases);fold_test_release();
        // No-motion selecting an unlocked body must not create a redundant lock.
        const auto empty=f.hash();const int clean=mutations;f.down(xy.first,xy.second);f.drag(xy.first,xy.second);
        CHECK(f.hash()==empty&&mutations==clean);fold_test_release();
    }
    for(bool armed:{false,true})for(int reason=0;reason<9;++reason) {
        // Inactive lock fields do not own an ordinary base gesture (shared Filter contract).
        if(!armed&&(reason==0||reason==6))continue;
        g_seq_lock_armed=armed;g_view=armed?View::Sequencer:View::Chains;
        CHECK(f.engine->set_step_lock(0,7,0,0,4000));const auto xy=f.point(0);f.down(xy.first,xy.second);
        const auto saved=f.hash();const int dirty=mutations;Unit* held=nullptr;
        if(reason==0)g_seq_lock_chain=1;
        if(reason==1)g_seq_lock_armed=!armed;
        if(reason==2)g_edit_chain=1;
        if(reason==3)g_edit_slot=1;
        if(reason==4)g_overlay=Overlay::None;
        if(reason==5)g_view=View::Sigils;
        if(reason==6)g_seq_lock_step=8;
        if(reason==7)fold_test_release();
        if(reason==8)held=f.engine->replace_unit(0,0,new FoldUnit);
        fold_test_delta(f,0,30);CHECK(f.hash()==saved&&fold_test_bases(u)==bases&&mutations==dirty);
        if(held)delete f.engine->replace_unit(0,0,held);
        g_seq_lock_chain=g_edit_chain=g_edit_slot=0;g_seq_lock_step=7;g_overlay=Overlay::Editor;fold_test_release();
    }
    g_seq_lock_armed=true;g_view=View::Sequencer;
    const bool bypass=u.bypass;const int dirty=mutations;f.down(260,10);CHECK(u.bypass==bypass&&mutations==dirty);fold_test_release();
    f.engine->clear_track(0);delete f.engine->replace_unit(0,1,new FmUnit);
    for(int n=0;n<SEQ_LOCKS;++n)CHECK(f.engine->set_step_lock(0,7,1,n,q15(5000+n)));
    const auto full=f.hash();
    for(int p=0;p<5;++p){const auto xy=f.point(p);f.down(xy.first,xy.second);fold_test_delta(f,p,6);CHECK(f.hash()==full&&fold_test_bases(u)==bases&&g_seq_lock_flash>0);fold_test_release();}
    CHECK(!param_lens_touch_down(250,232));g_view=View::Chains;f.capture();module_inspector_top();CHECK(module_inspector_text("full"));
}
void test_fold_lens_temp() {
    ModuleInspectorFixture f(UnitKind::Fold);auto& u=*f.module;
    for(int selected=0;selected<5;++selected) {
        f.pick(selected);fold_test_release();f.open();auto& p=u.params[selected];p.base=selected==3?-13000:13000;p.mod_depth=1000;p.snap();
        const q15 base=p.base;int dirty=mutations;CHECK(param_lens_touch_down(250,129));CHECK(p.base==base&&mutations==dirty);
        CHECK(param_lens_touch_drag(256,129));CHECK(p.base==base+6*fold_test_range(selected)/271);param_lens_touch_release();
        dirty=mutations;CHECK(param_lens_touch_down(100,166));CHECK(p.mod_depth==1000&&mutations==dirty);
        param_lens_touch_drag(106,166);CHECK(p.mod_depth==1000+6*65534/271);param_lens_touch_release();
        CHECK(param_lens_touch_down(70,185));param_lens_touch_release();CHECK(p.src==ModSource::SigilA);
        CHECK(param_lens_touch_down(290,47));param_lens_touch_release();CHECK(fold_focus_selected()==(selected+1)%5);
        u32 down=KEY_LEFT;CHECK(param_lens_input(down,KEY_LEFT,0,false)&&down==0&&fold_focus_selected()==selected);
        lens_permanent_probe(u,selected);CHECK(fold_focus_selected()==selected);f.open();
        down=KEY_B|KEY_X;CHECK(param_lens_input(down,0,0,false)&&down==0&&!param_lens_is_open());
        CHECK(fold_focus_selected()==selected&&g_overlay==Overlay::Editor);f.open();CHECK(fold_focus_selected()==selected);param_lens_cancel();fold_test_release();
    }
    f.pick(3);
    for(bool muted:{false,true}) {
        f.engine->chains[0].mute=muted;f.engine->transport_stop();f.open();lens_permanent_probe(u,3);CHECK(fold_focus_selected()==3);
    }
    f.engine->chains[0].mute=false;const auto saved=fold_test_bases(u);const int dirty=mutations;
    for(int reason=0;reason<6;++reason) {
        lens_quick_begin();
        if(reason==0)g_edit_slot=1;
        if(reason==1)g_edit_chain=1;
        if(reason==2)g_view=View::Sequencer;
        if(reason==3)g_overlay=Overlay::None;
        u32 down=reason==4?KEY_SELECT|KEY_X:0;CHECK(param_lens_input(down,KEY_L|KEY_TOUCH,0,reason==5));
        param_lens_touch_drag(295,129);
        CHECK(!param_lens_is_open()&&!f.engine->param_temp_active()&&fold_test_bases(u)==saved&&mutations==dirty);
        g_edit_chain=g_edit_slot=0;g_view=View::Chains;g_overlay=Overlay::Editor;param_lens_touch_release();
    }
    // Physical bipolar sign and bounds are distinct from unipolar percentages.
    for(int raw:{-32767,-16384,0,16384,32767}) {
        char value[32];module_inspector_value_text(value,sizeof value,UnitKind::Fold,3,q15(raw));char* suffix=nullptr;
        const double number=std::strtod(value,&suffix);CHECK(suffix&&std::string(suffix)=="fs");
        CHECK(std::abs(number-raw/32767.0)<=0.0051);CHECK(value[0]==(raw<0?'-':'+'));
    }
    for(int raw:{0,1,8192,32767}) {
        char value[32];module_inspector_value_text(value,sizeof value,UnitKind::Fold,0,q15(raw));char* suffix=nullptr;
        const double number=std::strtod(value,&suffix);CHECK(suffix&&std::string(suffix)=="x");CHECK(std::abs(number-(32767+7*raw)/32768.0)<=0.0051);
        module_inspector_value_text(value,sizeof value,UnitKind::Fold,1,q15(raw));
        if(raw==0)CHECK(std::string(value)=="mute");
        else {const double db=std::strtod(value,&suffix);CHECK(std::string(suffix)=="db"&&std::abs(db-20*std::log10(raw/32768.0))<=0.051);}
        for(int p:{2,4}) {
            module_inspector_value_text(value,sizeof value,UnitKind::Fold,p,q15(raw));
            const double percent=std::strtod(value,&suffix);CHECK(std::string(suffix)=="%"&&std::abs(percent-raw*100.0/32767)<=0.51);
        }
    }
}
void test_fold_wave_model() {
    namespace model=fold_wave_model;FoldUnit u(false); // explicit historical DSP, not the new-card default
    std::vector<q15> input;for(int raw=-32768;raw<=32767;raw+=127)input.push_back(q15(raw));
    for(int raw:{-32768,-32767,-24575,-16384,-8192,-2,-1,0,1,2,8192,16384,24575,32766,32767})input.push_back(q15(raw));
    uint32_t random=0x46534f4c;for(int n=0;n<257;++n){random=random*1664525u+1013904223u;input.push_back(q15(random>>16));}
    size_t compared=0;
    for(int drive:{0,1,8192,32767})for(int mode:{0,1,8192,16383,16384,16385,24576,32766,32767})
        for(int bias:{-32767,-11468,0,11468,32767})for(int trim:{0,8192,32767})for(int mix:{0,16384,32767}) {
            const q15 params[]={q15(drive),q15(trim),q15(mode),q15(bias),q15(mix)};
            for(int p=0;p<5;++p)u.params[p].effective=params[p];
            auto actual=input;u.process(actual.data(),int(actual.size()));
            for(size_t n=0;n<input.size();++n) {
                const q15 predicted=model::sample(input[n],params);CHECK(predicted==actual[n]);
                if(mix==0)CHECK(predicted==input[n]);
                ++compared;
            }
        }
    // Include every input code at endpoints; catches single-LSB wrap/reflection seams.
    std::vector<q15> all(65536);for(int n=0;n<65536;++n)all[n]=q15(n-32768);
    for(int mode:{0,8192,16384,24576,32767}) {
        const q15 params[]={32767,32767,q15(mode),-32767,32767};for(int p=0;p<5;++p)u.params[p].effective=params[p];
        auto actual=all;u.process(actual.data(),int(actual.size()));
        for(size_t n=0;n<all.size();++n){CHECK(model::sample(all[n],params)==actual[n]);++compared;}
    }
    // Independent sine + actual DSP columns prove fixed input amplitude, x/y scale,
    // per-column wrap edge envelopes and bypass identity. No production sample()
    // or y() is used to construct the expected plotted values.
    CHECK(model::X0==28&&model::X1==306&&model::ZERO==132&&model::HEIGHT==36);
    CHECK(model::COLUMNS==279&&model::OVERSAMPLE==8&&model::SAMPLES==2225);
    auto expected_y=[](q15 v){return 132-int(std::lround(v*36.0/32768.0));};
    std::vector<q15> sine(2225);
    for(int i=0;i<2225;++i){sine[i]=q15(std::lround(24575*std::sin(4*3.14159265358979323846*i/2224)));CHECK(model::source_sample(i)==sine[i]);}
    CHECK(sine.front()==0&&sine.back()==0&&*std::max_element(sine.begin(),sine.end())==24575&&*std::min_element(sine.begin(),sine.end())==-24575);
    CHECK(model::y(-32768)==168&&model::y(32767)==96&&model::y(0)==132);
    for(int mode:{0,8192,16384,24576,32767})for(int bias:{-32767,0,32767})for(bool bypass:{false,true}) {
        const q15 params[]={32767,32767,q15(mode),q15(bias),32767};for(int p=0;p<5;++p)u.params[p].effective=params[p];
        auto actual=sine;if(!bypass)u.process(actual.data(),int(actual.size()));model::Plot plot;model::plot(params,bypass,plot);
        for(int column=0;column<279;++column) {
            const int first=std::max(0,column*8-4),last=std::min(2224,column*8+4);int lo=255,hi=0;
            CHECK(plot.source[column]==expected_y(sine[column*8]));
            for(int i=first;i<=last;++i){lo=std::min(lo,expected_y(actual[i]));hi=std::max(hi,expected_y(actual[i]));}
            const auto& point=plot.shaped[column];CHECK(point.first==expected_y(actual[first])&&point.last==expected_y(actual[last]));
            CHECK(point.low==lo&&point.high==hi&&lo>=96&&hi<=168);
        }
    }
    // Existing diode DC/positive quarter branch is deliberate, not corrected by UI.
    const q15 diode[]={0,32767,32767,0,32767};CHECK(model::sample(-16384,diode)>4000);CHECK(model::sample(16384,diode)>16000);
    std::printf("fold model oracle: %zu exact samples against unchanged FoldUnit::process\n",compared);
}
void fold_test_geometry_pair(int top) {
    module_inspector_geometry();CHECK(top+objects<=8192);maximum_objects=std::max(maximum_objects,objects);
    maximum_shared_objects=std::max(maximum_shared_objects,top+objects);
}
std::vector<u32> fold_test_plot_pixels() {
    CHECK(width==320);std::vector<u32> result;
    for(int y=94;y<=170;++y)for(int x=28;x<=306;++x)result.push_back(pixels[y*width+x]);
    return result;
}
void test_fold_marker_layers() {
    ModuleInspectorFixture f(UnitKind::Fold);auto& u=*static_cast<FoldUnit*>(f.module);fold_test_configure(u);
    const int starts[]={14,118,186,14,222},ys[]={64,213,48,213,213};
    for(int p=0;p<5;++p){u.params[p].base=p==3?-16384:8192;u.params[p].effective=p==3?16384:24576;}
    auto check_markers=[&] {
        f.capture();module_inspector_surface(false);{ModuleInspectorNoEngine no;f.lower();}module_inspector_geometry();
        for(int p=0;p<5;++p) {
            auto x=[&](q15 value){return starts[p]+(int(value)+(p==3?32767:0))*fold_test_span(p)/fold_test_range(p);};
            const q15 edit=g_seq_lock_armed?seq_display_value(0,p,u.params[p].base):u.params[p].base;
            const int base=x(edit),live=x(u.params[p].effective),y=ys[p];
            CHECK(pixels[(y-3)*width+base]==(p==0?m::spot():m::ink()));
            CHECK(pixels[(y-1)*width+base]==m::paper_color(y-1) && pixels[y*width+base]==m::branch());
            if(live!=base)CHECK(pixels[(y-1)*width+live]==m::spot() && pixels[y*width+live]==m::spot());
        }
    };
    check_markers(); // hollow stored base, filled distinct effective
    g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;
    for(int p=0;p<SEQ_LOCKS;++p)CHECK(f.engine->set_step_lock(0,7,0,p,q15(p==3?-8192:12000))); // four-lock budget; mix remains base
    check_markers(); // the hollow marker follows the lock draft, not stored base
    for(int p=0;p<5;++p)u.params[p].effective=seq_display_value(0,p,u.params[p].base);
    check_markers(); // coincident values retain a single hollow edit marker
}
void test_fold_frames() {
    test_fold_marker_layers();
    ModuleInspectorFixture f(UnitKind::Fold);auto& u=*static_cast<FoldUnit*>(f.module);
    for(int mode:{0,8192,16384,24576,32767})for(int bias:{-32767,0,32767})for(int edge:{0,32767})for(int selected=0;selected<5;++selected) {
        fold_test_configure(u,edge,mode,bias,edge,edge);fold_focus_select(selected);f.capture();
        const auto top=module_inspector_top();const int top_count=objects;CHECK(module_inspector_top(false,true)==top);
        module_inspector_surface(false);{ModuleInspectorNoEngine detached;f.lower();}fold_test_geometry_pair(top_count);
        CHECK(module_inspector_text("drive")&&module_inspector_text("mode")&&module_inspector_text("bias")&&module_inspector_text("trim")&&module_inspector_text("mix"));
        CHECK(module_inspector_text(".75fs")&&module_inspector_text("mod")&&!module_inspector_text("temp"));
        // Fixed rail positions and marker ownership stay inside the approved boxes.
        const int starts[]={14,118,186,14,222},ys[]={64,213,48,213,213};
        for(int p=0;p<5;++p) {
            const int raw=u.params[p].base+(p==3?32767:0),x=starts[p]+raw*fold_test_span(p)/fold_test_range(p);
            CHECK(pixels[(ys[p]-3)*width+x]==(p==selected?m::spot():m::ink()));
        }
        const auto lower=pixels;const int bottom_count=objects;FoldUnit decoy;decoy.bypass=true;
        fold_test_configure(u,1234,5678,9012,12345,2000);g_ui_snapshot={};
        CHECK(module_inspector_top()==top);module_inspector_surface(false);
        {ModuleInspectorNoEngine detached;fold_focus_draw(decoy);}CHECK(pixels==lower&&objects==bottom_count);
    }
    // The curve must use effective values, not stored base, while editing values stay stored.
    fold_test_configure(u);u.params[0].base=13000;u.params[0].effective=8000;u.params[0].src=ModSource::SigilA;
    u.params[0].mod_depth=10000;f.engine->sigils[0].value=16384;fold_focus_select(0);f.capture();
    ParamInspector detail;f.engine->capture_param_inspector(0,0,0,detail);CHECK(detail.control!=detail.target&&detail.target!=detail.effective);
    module_inspector_top();module_inspector_detail(detail);module_inspector_surface(false);f.lower();const auto effective_plot=fold_test_plot_pixels();
    u.params[0].base=30000;f.capture();module_inspector_surface(false);f.lower();CHECK(fold_test_plot_pixels()==effective_plot);
    u.params[0].effective=30000;f.capture();module_inspector_surface(false);f.lower();CHECK(fold_test_plot_pixels()!=effective_plot);
    // Armed-step plot is a draft: changing lock changes curve; effective changes cannot.
    g_seq_lock_armed=true;g_seq_lock_step=7;CHECK(f.engine->set_step_lock(0,7,0,0,4000));f.capture();
    module_inspector_top();CHECK(module_inspector_text("lock draft")&&module_inspector_text("not audio"));module_inspector_surface(false);f.lower();const auto lock_plot=fold_test_plot_pixels();
    u.params[0].effective=2000;f.capture();module_inspector_surface(false);f.lower();CHECK(fold_test_plot_pixels()==lock_plot);
    CHECK(f.engine->set_step_lock(0,7,0,0,27000));f.capture();module_inspector_surface(false);f.lower();CHECK(fold_test_plot_pixels()!=lock_plot);g_seq_lock_armed=false;
    // Bypass is dry identity at the same fixed plot scale; mute preserves a hypothetical model.
    u.bypass=true;f.capture();module_inspector_top();CHECK(module_inspector_text("bypassed"));module_inspector_surface(false);f.lower();const auto bypass_plot=fold_test_plot_pixels();
    fold_test_configure(u,32767,32767,-32767,10000,0);f.capture();module_inspector_surface(false);f.lower();CHECK(fold_test_plot_pixels()==bypass_plot);
    fold_test_configure(u);f.capture();module_inspector_surface(false);f.lower();const auto active_plot=fold_test_plot_pixels();
    auto muted_plot=active_plot;for(auto& pixel:muted_plot)if(pixel==m::spot())pixel=m::ghost();
    f.engine->chains[0].mute=true;f.capture();module_inspector_top();CHECK(module_inspector_text("chain muted")&&module_inspector_text("muted test model"));module_inspector_surface(false);f.lower();CHECK(fold_test_plot_pixels()==muted_plot);f.engine->chains[0].mute=false;
    fold_focus_select(3);f.open();
    for(bool held_mod:{false,true}) {
        if(held_mod){lens_quick_begin();param_lens_touch_down(250,129);param_lens_touch_drag(244,129);}
        f.capture();const auto top=module_inspector_top(true);const int top_count=objects;CHECK(module_inspector_top(true,true)==top);
        module_inspector_surface(false);{ModuleInspectorNoEngine detached;param_lens_draw_bottom();}fold_test_geometry_pair(top_count);CHECK(module_inspector_text("base ")&&!module_inspector_text("temp"));
    }
    lens_frame(0,0,KEY_L);param_lens_touch_release();param_lens_cancel();
    g_overlay=Overlay::None;f.capture();module_inspector_surface(true);CHECK(!module_inspector_draw_top()&&objects==0);
    module_inspector_surface(false);f.lower();CHECK(texts.empty());
}
void dump_fold_runtime(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);std::ofstream manifest(directory/"manifest.tsv");
    manifest<<"frame\tparam\tdrive_raw\ttrim_raw\tmode_raw\tbias_raw\tmix_raw\tplot_layer\ttop_objects\tbottom_objects\tshared_objects\tbottom_clipped\tbottom_overlaps\n";
    ModuleInspectorFixture f(UnitKind::Fold);auto& u=*static_cast<FoldUnit*>(f.module);
    auto save=[&](const std::string& name,bool lens=false) {
        f.capture();module_inspector_top(lens,true);const int top=objects;dump_surface(directory,name+"_top");
        module_inspector_surface(false);{ModuleInspectorNoEngine detached;if(lens)param_lens_draw_bottom();else f.lower();}
        fold_test_geometry_pair(top);dump_surface(directory,name+"_bottom");manifest<<name<<'\t'<<fold_focus_selected();
        for(int p=0;p<5;++p)manifest<<'\t'<<u.params[p].base;
        manifest<<'\t'<<(lens?"lens":u.bypass?"bypass dry":g_seq_lock_armed?"armed lock draft":f.engine->chains[0].mute?"muted hypothetical":"effective")
            <<'\t'<<top<<'\t'<<objects<<'\t'<<top+objects<<'\t'<<outside_rects+outside_lines<<'\t'<<text_overlaps<<'\n';
    };
    fold_test_configure(u);save("01_fold");fold_test_configure(u,8192,16384);fold_focus_select(2);save("02_wrap");
    fold_test_configure(u,8192,32767);fold_focus_select(2);save("03_diode");fold_test_configure(u,8192,0,11468);fold_focus_select(3);save("04_bias");
    fold_test_configure(u,0);save("05_drive_min");fold_test_configure(u,32767);save("06_drive_max");
    fold_test_configure(u,8192,0,0,0);fold_focus_select(1);save("07_trim_mute");fold_test_configure(u,8192,0,0,8192);fold_focus_select(1);save("08_trim_low");
    fold_test_configure(u,8192,0,0,32767,0);fold_focus_select(4);save("09_mix_dry");fold_test_configure(u,8192,0,0,32767,16384);fold_focus_select(4);save("10_mix_half");
    fold_test_configure(u,8192,24576);fold_focus_select(2);save("11_morph");
    fold_test_configure(u);u.params[0].src=ModSource::SigilB;u.params[0].mod_depth=12000;u.params[0].effective=18000;f.engine->sigils[1].value=16384;save("12_live_mod");
    g_seq_lock_armed=true;g_seq_lock_step=7;CHECK(f.engine->set_step_lock(0,7,0,0,4000));save("13_lock");g_seq_lock_armed=false;
    fold_focus_select(3);f.open();save("14_mod",true);lens_quick_begin();param_lens_touch_down(180,129);param_lens_touch_drag(204,129);save("15_temp",true);
    lens_frame(0,0,KEY_L);param_lens_touch_release();param_lens_cancel();fold_test_release();
    u.bypass=true;save("16_bypass");u.bypass=false;f.engine->chains[0].mute=true;save("17_muted");
}
