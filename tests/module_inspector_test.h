// Included by sequencer_test.cpp: real controllers/DSP/capture/renderers, never a UI model.
// Legacy VCA + chorus shared workflow regressions. Flexible VCA adds vca_ui_test.h.
struct ModuleInspectorFixture : Fixture {
    UnitKind kind;
    Unit* module;
    explicit ModuleInspectorFixture(UnitKind k,bool modern_delay=false) : kind(k) {
        module=k==UnitKind::Delay?new DelayUnit(modern_delay):make_unit(k);CHECK(module);delete engine->replace_unit(0,0,module);
        g_edit_chain=g_edit_slot=0;g_view=View::Chains;g_overlay=Overlay::Editor;
        if(k==UnitKind::Vca)static_cast<VcaUnit*>(module)->set_flexible(false);
        g_drag=Drag::None;param_lens_cancel();module_inspector_select(kind,1);
        chorus_focus_release();
        if(k==UnitKind::Delay)delay_focus_release();
        if(k==UnitKind::Filter)filter_focus_release();
        if(k==UnitKind::Fold)fold_focus_release();
        if(k==UnitKind::Bitcrush)bit_focus_release();
        if(k==UnitKind::Tilt)tilt_focus_release();
        if(k==UnitKind::Ring)ring_focus_release();
        if(k==UnitKind::Strain)strain_focus_release();
        for(int p=0;p<(k==UnitKind::Delay?6:k==UnitKind::Filter?4:k==UnitKind::Fold?5:k==UnitKind::Bitcrush?7:3);++p){module->params[p].base=17000;module->params[p].src=ModSource::None;module->params[p].mod_depth=0;module->params[p].snap();}
        capture();
    }
    void capture() {
        engine->capture_ui_snapshot(g_ui_snapshot,g_edit_chain,g_edit_slot);
        fm_context_capture(-1);stranular_focus_capture();play_focus_capture();param_lens_capture();
    }
    std::pair<int,int> point(int p) const {
        if(kind==UnitKind::Vca) {
            if(p==0)return {295,110};
            if(p==1)return {30,184};
            return {175,184};
        }
        if(kind==UnitKind::Strain) {
            const std::pair<int,int> points[]={{78,65},{121,214},{45,214},{238,65},{197,214},{273,214}};
            return points[p];
        }
        if(kind==UnitKind::Ring) {
            const std::pair<int,int> points[]={{78,65},{238,65},{45,214},{121,214},{273,214},{197,214}};
            return points[p];
        }
        if(kind==UnitKind::Tilt) {
            const std::pair<int,int> points[]={{73,65},{253,65},{121,214},{273,214},{45,214},{197,214}};
            return points[p];
        }
        if(kind==UnitKind::Bitcrush) {
            const std::pair<int,int> points[]={{55,65},{160,65},{264,65},{44,213},{120,213},{195,213},{272,213}};
            return points[p];
        }
        if(kind==UnitKind::Fold) {
            const std::pair<int,int> points[]={{80,64},{160,210},{238,48},{56,210},{264,210}};
            return points[p];
        }
        if(kind==UnitKind::Filter) {
            const std::pair<int,int> points[]={{150,112},{298,112},{80,198},{240,198}};
            return points[p];
        }
        if(kind==UnitKind::Delay) {
            const std::pair<int,int> points[]={{80,48},{240,48},{42,194},{194,194},{270,194},{208,12}};
            return points[p];
        }
        if(p==0)return {169,43};
        if(p==1)return {68,192};
        return {299,138};
    }
    bool down(int x,int y) {
        if(kind==UnitKind::Strain)return strain_focus_touch_body(*static_cast<StrainUnit*>(module),x,y);
        if(kind==UnitKind::Ring)return ring_focus_touch_body(*static_cast<RingUnit*>(module),x,y);
        if(kind==UnitKind::Tilt)return tilt_focus_touch_body(*static_cast<TiltUnit*>(module),x,y);
        if(kind==UnitKind::Bitcrush)return bit_focus_touch_body(*static_cast<BitUnit*>(module),x,y);
        if(kind==UnitKind::Fold)return fold_focus_touch_body(*static_cast<FoldUnit*>(module),x,y);
        if(kind==UnitKind::Filter)return filter_focus_touch_body(*static_cast<FilterUnit*>(module),x,y);
        if(kind==UnitKind::Delay)return delay_focus_touch_body(*static_cast<DelayUnit*>(module),x,y);
        return kind==UnitKind::Vca?vca_focus_touch_body(*static_cast<VcaUnit*>(module),x,y):
            chorus_focus_touch_body(*static_cast<ChorusUnit*>(module),x,y);
    }
    void drag(int x,int y) {
        if(kind==UnitKind::Vca)vca_focus_drag(*static_cast<VcaUnit*>(module),x,y);
        else if(kind==UnitKind::Delay)delay_focus_drag(*static_cast<DelayUnit*>(module),x,y);
        else if(kind==UnitKind::Filter)filter_focus_drag(*static_cast<FilterUnit*>(module),x,y);
        else if(kind==UnitKind::Fold)fold_focus_drag(*static_cast<FoldUnit*>(module),x,y);
        else if(kind==UnitKind::Bitcrush)bit_focus_drag(*static_cast<BitUnit*>(module),x,y);
        else if(kind==UnitKind::Tilt)tilt_focus_drag(*static_cast<TiltUnit*>(module),x,y);
        else if(kind==UnitKind::Strain)strain_focus_drag(*static_cast<StrainUnit*>(module),x,y);
        else if(kind==UnitKind::Ring)ring_focus_drag(*static_cast<RingUnit*>(module),x,y);
        else chorus_focus_drag(*static_cast<ChorusUnit*>(module),x,y);
    }
    void pick(int p) {auto xy=point(p);CHECK(down(xy.first,xy.second));g_drag=Drag::None;CHECK(module_inspector_selected(kind)==p);}
    void open() {CHECK(param_lens_touch_down(250,232));param_lens_touch_release();CHECK(param_lens_is_open());}
    void lower() const {
        if(kind==UnitKind::Vca)vca_focus_draw(*static_cast<VcaUnit*>(module));
        else if(kind==UnitKind::Delay)delay_focus_draw(*static_cast<DelayUnit*>(module));
        else if(kind==UnitKind::Filter)filter_focus_draw(*static_cast<FilterUnit*>(module));
        else if(kind==UnitKind::Fold)fold_focus_draw(*static_cast<FoldUnit*>(module));
        else if(kind==UnitKind::Bitcrush)bit_focus_draw(*static_cast<BitUnit*>(module));
        else if(kind==UnitKind::Tilt)tilt_focus_draw(*static_cast<TiltUnit*>(module));
        else if(kind==UnitKind::Strain)strain_focus_draw(*static_cast<StrainUnit*>(module));
        else if(kind==UnitKind::Ring)ring_focus_draw(*static_cast<RingUnit*>(module));
        else chorus_focus_draw(*static_cast<ChorusUnit*>(module));
    }
};
struct ModuleInspectorNoEngine {
    Engine* engine=g_eng;platform::AudioOut* audio=g_audio;
    ModuleInspectorNoEngine(){g_eng=nullptr;g_audio=nullptr;}
    ~ModuleInspectorNoEngine(){g_eng=engine;g_audio=audio;}
};
void module_inspector_surface(bool top) {
    width=top?400:320;objects=outside_rects=outside_lines=text_overlaps=0;texts.clear();pixels.assign(width*HEIGHT,0xff000000u);
}
void module_inspector_geometry() {
    if(outside_rects||outside_lines||text_overlaps)
        std::fprintf(stderr,"module inspector geometry width=%d rect=%d line=%d overlaps=%d\n",width,outside_rects,outside_lines,text_overlaps);
    CHECK(objects>0&&objects<=8192&&outside_rects==0&&outside_lines==0&&text_overlaps==0);
    for(const auto& t:texts)CHECK(t.x>=0&&t.y>=0&&t.x+t.w<=width&&t.y+t.h<=HEIGHT);
    rev_menu_palette_and_paper();
}
bool module_inspector_text(const std::string& fragment) {
    for(const auto& t:texts)if(t.text.find(fragment)!=std::string::npos)return true;
    return false;
}
std::vector<u32> module_inspector_top(bool lens=false,bool route=false) {
    module_inspector_surface(true);ModuleInspectorNoEngine frozen;
    if(route)top_draw();else if(lens)CHECK(param_lens_draw_top());else CHECK(module_inspector_draw_top());
    module_inspector_geometry();maximum_top_objects=std::max(maximum_top_objects,objects);return pixels;
}
void module_inspector_detail(const ParamInspector& detail) {
    auto at=[](const std::string& label,int x,int y) {
        for(const auto& t:texts)if(t.text==label&&t.call_x==x&&t.call_y==y)return true;
        return false;
    };
    char value[32];
    auto format=[&](q15 raw) {module_inspector_value_text(value,sizeof value,(UnitKind)detail.kind,detail.param,raw,false);return std::string(value);};
    CHECK(at("stored base "+format(detail.base),14,166));
    CHECK(at(format(detail.effective),254,60));
    CHECK(at("control "+format(detail.control),254,78));
    CHECK(at("target "+format(detail.target),254,92));
    const bool vca=(UnitKind)detail.kind==UnitKind::Vca;
    CHECK(at(format(detail.range_min),254,vca?164:162));CHECK(at(format(detail.range_max),254,vca?178:175));
    CHECK(at(detail.clip_low||detail.clip_high?"range / clips":"potential range",254,vca?149:148));
}
void module_inspector_gothic_mask(const char* value) {
    // Independent per-bit oracle: do not call the runtime inspector_gothic renderer.
    CHECK(width==400);int native_width=0;
    for(const char* p=value;*p;++p) {
        if(*p>='0'&&*p<='9'){const auto* glyph=note_display::find(*p);CHECK(glyph&&!glyph->small);native_width+=glyph->width+2;}
        else if(*p=='.')native_width+=6;else break;
    }
    const int scale=native_width*2<=210?2:1;constexpr int baseline=134;
    CHECK(native_width>0&&native_width*scale<=210);
    std::vector<u32> expected((size_t)width*HEIGHT);
    for(int y=0;y<HEIGHT;++y)std::fill(expected.begin()+(size_t)y*width,expected.begin()+(size_t)(y+1)*width,m::paper_color(y));
    int x=14;
    for(const char* p=value;*p;++p) {
        if(*p>='0'&&*p<='9') {
            const auto* glyph=note_display::find(*p);const int stride=(glyph->width+7)/8;
            for(int y=0;y<glyph->height;++y)for(int col=0;col<glyph->width;++col)
                if(note_display::bitmap[glyph->offset+y*stride+col/8]&(0x80u>>(col&7)))
                    for(int yy=0;yy<scale;++yy)for(int xx=0;xx<scale;++xx)
                        expected[(baseline+(glyph->top+y)*scale+yy)*width+x+col*scale+xx]=m::ink();
            x+=(glyph->width+2)*scale;
        } else if(*p=='.') {
            for(int yy=baseline-3*scale;yy<baseline;++yy)for(int xx=x;xx<x+3*scale;++xx)expected[yy*width+xx]=m::ink();
            x+=6*scale;
        } else break;
    }
    for(int y=65;y<145;++y)for(int xx=14;xx<230;++xx)CHECK(pixels[y*width+xx]==expected[y*width+xx]);
}
void test_module_inspector_handles() {
    for(int k=0;k<(int)UnitKind::Count;++k)
        CHECK(module_inspector_supports((UnitKind)k)==((UnitKind)k==UnitKind::Arp||(UnitKind)k==UnitKind::Mouth||(UnitKind)k==UnitKind::Comp||(UnitKind)k==UnitKind::Fract||(UnitKind)k==UnitKind::Bird||(UnitKind)k==UnitKind::Shift||(UnitKind)k==UnitKind::Osc||(UnitKind)k==UnitKind::Grain||(UnitKind)k==UnitKind::Motor||(UnitKind)k==UnitKind::SamplePlayer||(UnitKind)k==UnitKind::Noise||(UnitKind)k==UnitKind::Vca||(UnitKind)k==UnitKind::Chorus||(UnitKind)k==UnitKind::Delay||(UnitKind)k==UnitKind::Filter||(UnitKind)k==UnitKind::Fold||(UnitKind)k==UnitKind::Bitcrush||(UnitKind)k==UnitKind::Tilt||(UnitKind)k==UnitKind::Ring||(UnitKind)k==UnitKind::Strain||(UnitKind)k==UnitKind::MicIn||(UnitKind)k==UnitKind::Recorder));
    for(UnitKind k:{UnitKind::Vca,UnitKind::Chorus})for(int p=0;p<3;++p) {
        ModuleInspectorFixture f(k);auto& u=*f.module;const auto xy=f.point(p);
        CHECK(f.down(xy.first,xy.second));CHECK(module_inspector_selected(k)==p);
        const Drag expected=k==UnitKind::Vca?(p==0?Drag::VcaGain:p==1?Drag::VcaAttack:Drag::VcaDecay):
            (p==0?Drag::ChorusRate:p==1?Drag::ChorusDepth:Drag::ChorusMix);
        CHECK(g_drag==expected);
        const q15 tap=17000;
        CHECK(u.params[p].base==tap);f.drag(xy.first,xy.second);CHECK(u.params[p].base==tap);
        // Both modules now use relative drags; no touch-down mutation.
        const int dx=k==UnitKind::Vca?(p==0?0:5):(p==0?5:0);
        const int dy=k==UnitKind::Vca?(p==0?-5:0):(p==0?0:5);
        f.drag(xy.first+dx,xy.second+dy);
        const q15 moved=k==UnitKind::Vca?(q15)(17000+5*32767/(p==0?56:160)):
            (q15)(17000+5*32767/(p==0?121:p==1?34:52));
        CHECK(u.params[p].base==moved);for(int other=0;other<3;++other)if(other!=p)CHECK(u.params[other].base==17000);
        module_inspector_select(k,p);module_inspector_select(k,-1);module_inspector_select(k,8);CHECK(module_inspector_selected(k)==p);
        if(k==UnitKind::Vca){vca_focus_select(-1);vca_focus_select(8);CHECK(vca_focus_selected()==p);}
        else {chorus_focus_select(-1);chorus_focus_select(3);CHECK(chorus_focus_selected()==p);}
    }
    // Legacy cards retain only gain/attack/decay; no accidental upgrade from
    // touching the newly visible but disabled AHD controls.
    ModuleInspectorFixture legacy(UnitKind::Vca);
    for(auto xy:{std::pair<int,int>{90,184},{244,184},{25,204},{180,204},{80,35}}) {
        const int dirty=mutations;legacy.down(xy.first,xy.second);legacy.drag(xy.first+8,xy.second+8);
        CHECK(mutations==dirty&&!static_cast<VcaUnit*>(legacy.module)->flexible);
    }
}
void test_module_inspector_lens() {
    for(UnitKind k:{UnitKind::Vca,UnitKind::Chorus})for(int p=0;p<3;++p) {
        ModuleInspectorFixture f(k);f.pick(p);f.open();const int dirty=mutations;
        CHECK(param_lens_touch_down(280,185));param_lens_touch_release();CHECK(f.module->params[p].src==ModSource::EnvFollow);
        for(int i=0;i<3;++i)if(i!=p)CHECK(f.module->params[i].src==ModSource::None);
        CHECK(mutations==dirty+1);
        f.capture();module_inspector_surface(false);{ModuleInspectorNoEngine frozen;param_lens_draw_bottom();}
        module_inspector_geometry();CHECK(module_inspector_text("env all"));CHECK(module_inspector_text("destination"));
        // No capture between navigation, close, reopen: selection cannot depend on GPU cadence.
        CHECK(param_lens_touch_down(290,47));param_lens_touch_release();const int next=(p+1)%3;
        CHECK(module_inspector_selected(k)==next);u32 down=KEY_B|KEY_X|KEY_R;
        CHECK(param_lens_input(down,0,0,false)&&down==0&&!param_lens_is_open());
        CHECK(g_overlay==Overlay::Editor&&g_edit_chain==0&&g_edit_slot==0);f.open();
        CHECK(param_lens_touch_down(70,185));param_lens_touch_release();CHECK(f.module->params[next].src==ModSource::SigilA);
        down=KEY_LEFT;CHECK(param_lens_input(down,KEY_LEFT,0,false)&&down==0);CHECK(module_inspector_selected(k)==p);
        down=KEY_B;CHECK(param_lens_input(down,0,0,false)&&!param_lens_is_open());f.open();
        lens_permanent_probe(*f.module,p,false);
        ParamInspector detail;f.engine->capture_param_inspector(0,0,p,detail);CHECK(detail.valid&&!detail.temp);
        CHECK(detail.base==f.module->params[p].base&&module_inspector_selected(k)==p);
    }
}
void test_module_inspector_ownership() {
    for(UnitKind k:{UnitKind::Vca,UnitKind::Chorus}) {
        ModuleInspectorFixture f(k);f.pick(2);auto& p=f.module->params[2];
        for(bool muted:{false,true}) {
            f.engine->chains[0].mute=muted;f.engine->transport_stop();f.open();lens_permanent_probe(*f.module,2,false);
            CHECK(module_inspector_selected(k)==2);
        }
        f.engine->chains[0].mute=false;lens_quick_begin();
        // Held modulation edits routes permanently; action buttons cannot leak.
        param_lens_touch_down(280,185);param_lens_touch_release();CHECK(p.src==ModSource::EnvFollow);
        u32 down=KEY_X|KEY_R|KEY_A;param_lens_input(down,KEY_L,0,false);
        CHECK(down==0&&module_inspector_selected(k)==2&&!f.engine->param_temp_active());
        down=KEY_B|KEY_X|KEY_TOUCH;CHECK(param_lens_input(down,KEY_L|KEY_TOUCH,0,false)&&down==0);
        CHECK(!param_lens_is_open()&&g_overlay==Overlay::Editor);param_lens_touch_release();
        const q15 base=p.base;const int dirty=mutations;
        for(int reason=0;reason<5;++reason) {
            lens_quick_begin();
            if(reason==0)g_edit_slot=1;
            if(reason==1)g_view=View::Sequencer;
            if(reason==2)g_overlay=Overlay::None;
            down=reason==3?KEY_SELECT|KEY_X:0;
            CHECK(param_lens_input(down,KEY_L,0,reason==4));
            CHECK(!param_lens_is_open()&&!f.engine->param_temp_active()&&p.base==base&&mutations==dirty);
            CHECK(down==(reason==3?u32(KEY_SELECT):0u));g_edit_slot=0;g_view=View::Chains;g_overlay=Overlay::Editor;
        }
        lens_quick_begin();delete f.engine->replace_unit(0,0,nullptr);f.module=nullptr;f.capture();
        CHECK(!param_lens_is_open()&&!f.engine->param_temp_active()&&!module_inspector_draw_top());
    }
}
void test_module_inspector_units() {
    // Independent DSP equations, with double only in the host oracle.
    for(UnitKind k:{UnitKind::Vca,UnitKind::Chorus})for(int p=0;p<3;++p)for(int v:{0,128,8192,16384,32767}) {
        char out[32];module_inspector_value_text(out,sizeof out,k,p,(q15)v,false);char* suffix=nullptr;
        const double number=std::strtod(out,&suffix);CHECK(suffix&&suffix!=out);
        double expected=0,tolerance=0;const char* unit="";
        if((k==UnitKind::Vca&&p==0)||(k==UnitKind::Chorus&&p==2)) {expected=v*100.0/32767.0;tolerance=0.051;unit="%";}
        else if(k==UnitKind::Vca) {const uint64_t frames=(((uint64_t)v*v>>15)*32000>>15)+1;expected=frames*1000.0/ENGINE_SR;tolerance=0.0051;unit="ms";}
        else if(p==0) {expected=(5000.0+v*20.0)*ENGINE_SR/4294967296.0;tolerance=0.00051;unit="Hz";}
        else {expected=(16+((uint64_t)v*300>>15))*1000.0/ENGINE_SR;tolerance=0.0051;unit="ms";}
        while(*suffix==' ')++suffix;
        CHECK(std::string(suffix)==unit);CHECK(std::abs(number-expected)<=tolerance);
        if(v==0&&unit==std::string("ms"))CHECK(number>0); // no fabricated zero time
        struct {char before='a';char text[2]={'x','x'};char after='b';} tiny;
        module_inspector_value_text(tiny.text,sizeof tiny.text,k,p,(q15)v);CHECK(tiny.before=='a'&&tiny.after=='b'&&tiny.text[1]=='\0');
        module_inspector_value_text(nullptr,0,k,p,(q15)v);
        char zero='q';module_inspector_value_text(&zero,0,k,p,(q15)v);CHECK(zero=='q');
    }
    char output[32];module_inspector_value_text(output,sizeof output,UnitKind::Vca,3,1234,false);CHECK(std::string(output)=="--");
    module_inspector_value_text(output,sizeof output,UnitKind::Fm,0,1234);CHECK(std::string(output)=="--");
    module_inspector_value_text(output,sizeof output,UnitKind::Chorus,0,-32768);CHECK(std::string(output)=="0.037Hz");
}
void test_module_inspector_locks() {
    for(UnitKind k:{UnitKind::Vca,UnitKind::Chorus}) {
        ModuleInspectorFixture f(k);module_inspector_select(k,1);g_seq_lock_armed=true;g_seq_lock_step=7;
        auto& p=f.module->params[1];p.base=17000;p.effective=9000;
        CHECK(f.engine->set_step_lock(0,7,0,1,4000));f.capture();
        const auto before=f.hash();const int dirty=mutations;const auto shot=module_inspector_top();
        CHECK(module_inspector_text("lock"));CHECK(f.hash()==before&&mutations==dirty&&p.base==17000&&p.effective==9000);
        ParamInspector detail;f.engine->capture_param_inspector(0,0,1,detail);module_inspector_detail(detail);
        char edit[32];module_inspector_value_text(edit,sizeof edit,k,1,4000,false);module_inspector_gothic_mask(edit);
        CHECK(f.engine->set_step_lock(0,7,0,1,23000));f.capture();CHECK(module_inspector_top()!=shot&&p.base==17000);
        // Full foreign lock bank must refuse, never redirect an edit into module base.
        f.engine->clear_track(0);delete f.engine->replace_unit(0,1,new FmUnit);
        for(int n=0;n<SEQ_LOCKS;++n)CHECK(f.engine->set_step_lock(0,7,1,n,(q15)(5000+n)));
        const auto full=f.hash();const auto xy=f.point(1);CHECK(f.down(xy.first,xy.second));f.drag(xy.first+7,xy.second+7);
        CHECK(f.hash()==full&&p.base==17000&&g_seq_lock_flash>0);CHECK(!param_lens_touch_down(250,232));
        f.capture();module_inspector_top();CHECK(module_inspector_text("full"));
        g_seq_lock_armed=false;f.capture();module_inspector_top();
    }
}
void test_module_inspector_render() {
    for(UnitKind k:{UnitKind::Vca,UnitKind::Chorus}) {
        ModuleInspectorFixture f(k);
        for(int p=0;p<3;++p)for(int edge:{0,32767}) {
            module_inspector_select(k,p);f.module->params[p].base=(q15)edge;f.module->params[p].src=ModSource::SigilD;
            f.module->params[p].mod_depth=edge?-32767:32767;f.module->params[p].effective=12000;f.capture();
            const auto frozen=module_inspector_top();const int top=objects;
            ParamInspector detail;f.engine->capture_param_inspector(0,0,p,detail);module_inspector_detail(detail);
            char hero[32];module_inspector_value_text(hero,sizeof hero,k,p,(q15)edge,false);module_inspector_gothic_mask(hero);
            CHECK(module_inspector_top(false,true)==frozen); // actual top dispatcher, not direct-only proof
            const q15 saved=f.module->params[p].base;f.module->params[p].base=12345;
            g_ui_snapshot={};CHECK(module_inspector_top()==frozen);f.module->params[p].base=saved;
            f.capture();module_inspector_surface(false);f.lower();const auto lower=pixels;const int bottom=objects;
            if(k==UnitKind::Chorus || k==UnitKind::Vca)module_inspector_geometry(); // newly migrated full lower surface
            CHECK(bottom>0&&top+bottom<=8192);maximum_objects=std::max(maximum_objects,bottom);
            maximum_shared_objects=std::max(maximum_shared_objects,top+bottom);
            f.capture();module_inspector_top();module_inspector_surface(false);f.lower();CHECK(pixels==lower&&objects==bottom);
        }
        module_inspector_select(k,1);f.open();
        for(bool held_mod:{false,true}) {
            if(held_mod){lens_quick_begin();param_lens_touch_down(295,129);}
            f.capture();const auto frozen=module_inspector_top(true);const int top=objects;
            CHECK(module_inspector_top(true,true)==frozen);
            ParamInspector detail;f.engine->capture_param_inspector(0,0,1,detail);module_inspector_detail(detail);
            CHECK(!detail.temp);if(held_mod){char value[32];module_inspector_value_text(value,sizeof value,k,1,detail.base,false);module_inspector_gothic_mask(value);}
            module_inspector_surface(false);{ModuleInspectorNoEngine frozen;param_lens_draw_bottom();}
            module_inspector_geometry();CHECK(top+objects<=8192);maximum_shared_objects=std::max(maximum_shared_objects,top+objects);
            CHECK(module_inspector_text("base ")&&!module_inspector_text("temp")&&!module_inspector_text("audition"));
            CHECK(module_inspector_text("modulation depth"));CHECK(module_inspector_text("destination"));
        }
        lens_frame(0,0,KEY_L);param_lens_touch_release();param_lens_cancel();
        // Densest serial route vocabulary and selected final slot, bypass/mute labels.
        Unit* moved=f.engine->replace_unit(0,0,new StrainUnit);
        for(int slot=1;slot<UNITS_PER_CHAIN-1;++slot)delete f.engine->replace_unit(0,slot,new StrainUnit);
        delete f.engine->replace_unit(0,UNITS_PER_CHAIN-1,moved);g_edit_slot=UNITS_PER_CHAIN-1;
        f.capture();module_inspector_top();CHECK(module_inspector_text("ch01 / 06"));
        f.module->bypass=true;f.capture();module_inspector_top();CHECK(module_inspector_text("bypassed"));
        f.module->bypass=false;f.engine->chains[0].mute=true;f.capture();module_inspector_top();CHECK(module_inspector_text("chain muted"));
        g_overlay=Overlay::None;f.capture();module_inspector_surface(true);CHECK(!module_inspector_draw_top()&&objects==0);
    }
}
void dump_module_inspector(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);std::ofstream manifest(directory/"manifest.tsv");
    manifest<<"frame\tkind\tparam\ttop_objects\tbottom_objects\tshared_objects\tbottom_clipped\tbottom_overlaps\n";
    for(UnitKind k:{UnitKind::Vca,UnitKind::Chorus}) {
        ModuleInspectorFixture f(k);const std::string prefix=k==UnitKind::Vca?"vca":"chorus";
        if(k==UnitKind::Chorus) {
            f.module->params[0].base=3105;f.module->params[2].base=16711;
            for(int p=0;p<3;++p)f.module->params[p].snap();
            q15 silence[128]={};for(int n=0;n<188;++n){std::fill(std::begin(silence),std::end(silence),0);f.module->process(silence,128);}
        }
        auto save=[&](const std::string& tag,bool lens=false) {
            f.capture();module_inspector_top(lens);const int top=objects;dump_surface(directory,prefix+"_"+tag+"_top");
            module_inspector_surface(false);if(lens){ModuleInspectorNoEngine frozen;param_lens_draw_bottom();module_inspector_geometry();}else f.lower();
            CHECK(top+objects<=8192);dump_surface(directory,prefix+"_"+tag+"_bottom");
            manifest<<prefix+"_"+tag<<'\t'<<(int)k<<'\t'<<module_inspector_selected(k)<<'\t'<<top<<'\t'<<objects<<'\t'<<top+objects<<'\t'<<outside_rects+outside_lines<<'\t'<<text_overlaps<<'\n';
        };
        for(int p=0;p<3;++p) {module_inspector_select(k,p);save("param_"+std::to_string(p));}
        module_inspector_select(k,1);f.module->params[1].src=ModSource::SigilB;f.module->params[1].mod_depth=24576;f.module->params[1].effective=25000;save("modulated");
        g_seq_lock_armed=true;g_seq_lock_step=7;CHECK(f.engine->set_step_lock(0,7,0,1,4000));save("lock");g_seq_lock_armed=false;
        f.open();save("mod",true);lens_quick_begin();param_lens_touch_down(260,129);save("temp",true);
        lens_frame(0,0,KEY_L);param_lens_touch_release();param_lens_cancel();save("return");
    }
}
