// OSC v2: exact main dispatcher, real audio collector and frozen native draws.
// No exported images, mock signals or wall-clock animations.
namespace {
struct OscCleanFixture:OscFixture {
    OscCleanFixture(){osc->clean=true;capture();}
    static std::pair<int,int> point(int p){if(p==OscUnit::P_SHAPE)return {153,75};const int i=index(p);return {14+(i%2)*162+65,119+(i/2)*41};}
    static std::pair<int,int> label(int p){if(p==OscUnit::P_SHAPE)return {16,56};const int i=index(p);return {16+(i%2)*162,100+(i/2)*41};}
    void feed(int n=3200){q15 b[32];while(n>0){const int count=std::min(32,n);osc->control_tick();osc->process(b,count);n-=count;}}
};
void test_osc_clean_controls(){
    using C=OscUnit;
    for(bool lock:{false,true})for(int p=0;p<C::P_COUNT;++p){
        OscCleanFixture f;auto& u=*f.osc;u.params[p].base=12000;u.params[p].snap();
        if(lock){g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;CHECK(f.engine->set_step_lock(0,7,0,p,9000));}
        const auto before=f.bases();const auto xy=OscCleanFixture::point(p);const int start=lock?9000:12000,extent=p==C::P_SHAPE?278:130;
        input::down(xy.first,xy.second);CHECK(g_drag==Drag::OscShape&&osc_focus_selected()==p&&f.bases()==before);
        f.capture();input::move(xy.first,xy.second);CHECK(f.bases()==before);
        for(int dx:{7,-7,14}){input::move(xy.first+dx,xy.second);f.capture();const int expected=start+dx*32767/extent;
            if(lock)CHECK(f.bases()==before&&seq_display_value(0,p,before[p])==expected);
            else{auto values=before;values[p]=q15(expected);CHECK(f.bases()==values);}}
        input::up();const auto saved=f.bases();const q15 stopped=seq_display_value(0,p,saved[p]);input::move(xy.first+28,xy.second);CHECK(f.bases()==saved&&seq_display_value(0,p,saved[p])==stopped);
        if(lock){const auto at=OscCleanFixture::label(p);input::down(at.first,at.second);input::up();CHECK(seq_display_value(0,p,before[p])==before[p]&&f.bases()==before);}
        const int dirty=mutations;input::down(280,38);input::up();CHECK(mutations==dirty);f.capture();f.lower();CHECK(module_inspector_text("fine /32"));
        const q15 fine_start=seq_display_value(0,p,u.params[p].base);input::down(xy.first,xy.second);input::move(xy.first+1,xy.second);input::up();
        CHECK(seq_display_value(0,p,u.params[p].base)==fine_start+32767/(extent*32));
        if(lock)CHECK(f.bases()==before);
    }
    // Fine is transient and the old header targets retain their exact ownership.
    OscCleanFixture f;auto& u=*f.osc;f.capture();f.lower();CHECK(module_inspector_text("wide"));
    input::down(230,9);input::up();CHECK(u.bypass);input::down(230,9);input::up();CHECK(!u.bypass);
    input::down(305,9);input::up();CHECK(u.pitch_quantize);input::down(305,9);input::up();CHECK(!u.pitch_quantize);
    const int dirty=mutations;for(auto xy:{std::pair<int,int>{160,120},{160,160},{120,95},{170,35},{280,220}}){input::down(xy.first,xy.second);input::move(xy.first+10,xy.second);input::up();CHECK(mutations==dirty);}
    // A changed edit bank cancels a lock drag; it cannot write into the new bank.
    g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;
    CHECK(f.engine->set_step_lock(0,7,0,C::P_GAIN,12000));const auto xy=OscCleanFixture::point(C::P_GAIN);
    input::down(xy.first,xy.second);f.engine->active_pattern=1;input::move(xy.first+10,xy.second);input::up();f.engine->active_pattern=0;
    CHECK(seq_display_value(0,C::P_GAIN,u.params[C::P_GAIN].base)==12000);
    g_seq_lock_chain=1;const auto before=f.bases();input::down(xy.first,xy.second);input::move(xy.first+20,xy.second);input::up();CHECK(f.bases()==before);
}
void test_osc_clean_lens_identity(){
    using C=OscUnit;OscCleanFixture f;auto& u=*f.osc;
    for(int p=0;p<C::P_COUNT;++p){
        osc_focus_select(p);input::frame(0,0);input::frame(KEY_L,KEY_L);CHECK(param_lens_is_open());f.capture();
        const q15 saved=u.params[p].base;input::down(220,129);input::move(213,129);input::up();CHECK(u.params[p].base==std::max(0,int(saved)-7*32767/271));
        input::down(75,185);input::up();CHECK(u.params[p].src==ModSource::SigilA);
        input::down(150,160);input::move(130,160);input::up();CHECK(u.params[p].mod_depth<0);
        f.feed();f.capture();const auto top=module_inspector_top(true,true);const int count=objects;CHECK(module_inspector_text("before fx"));
        module_inspector_surface(false);{ModuleInspectorNoEngine frozen;param_lens_draw_bottom();}osc_geometry(count);
        input::frame(0,0,KEY_L);CHECK(!param_lens_is_open()&&!f.engine->param_temp_active());
        input::frame(0,0);
    }
    // A frozen captured mode formats correctly even without engine pointers.
    for(auto& p:u.params){p.src=ModSource::None;p.mod_depth=0;}u.params[C::P_PITCH].base=16384;u.params[C::P_PW].base=16384;f.snap();f.capture();
    char value[32];osc_focus_value_text(value,sizeof value,C::P_PITCH,16384);CHECK(std::string(value).find("c4+00 / 261.6hz")!=std::string::npos);
    osc_focus_value_text(value,sizeof value,C::P_PW,16384);CHECK(std::string(value)=="50.0%");
    const auto xy=OscCleanFixture::point(C::P_PITCH);const q15 before=u.params[C::P_PITCH].base;
    input::down(xy.first,xy.second);g_overlay=Overlay::None;f.capture();g_overlay=Overlay::Editor;input::move(xy.first+30,xy.second);input::up();CHECK(u.params[C::P_PITCH].base==before);
    // Hold-L quarantines a pre-existing stylus contact; release cannot leak a drag.
    f.capture();input::touch={xy.first,xy.second};input::frame(KEY_TOUCH,KEY_TOUCH);input::frame(KEY_L,KEY_L|KEY_TOUCH);
    input::touch={290,129};input::frame(0,KEY_L|KEY_TOUCH);CHECK(u.params[C::P_PITCH].base==before);
    input::frame(0,KEY_TOUCH,KEY_L);input::frame(0,0,KEY_TOUCH);
    // Old cards never silently opt into clean, not even on first SHAPE conversion.
    u.clean=false;u.shape_morph=false;u.wave=C::Wave::Saw;f.capture();input::down(100,148);input::up();CHECK(!u.clean&&u.shape_morph&&u.params[C::P_SHAPE].base==21845);
    f.capture();osc_focus_value_text(value,sizeof value,C::P_PITCH,16384);CHECK(std::string(value)=="c5 / 523.3hz");
    osc_focus_value_text(value,sizeof value,C::P_PW,16384);CHECK(std::string(value)=="37.5%");
    // Score metadata carries clean independently of morph/qnt/wave. Readouts are
    // pure stored-mode mappings even if another live OSC currently owns focus.
    for(bool clean:{false,true})for(bool qnt:{false,true}){
        u.clean=clean;u.pitch_quantize=qnt;const uint8_t variant=seq_lock::variant(u);
        CHECK(bool(variant&16)==clean&&bool(variant&2)==qnt);
        seq_lock::format_value(UnitKind::Osc,variant,C::P_PITCH,16384,value,sizeof value);
        CHECK(std::string(value)==(clean?"261.6hz":"523.3hz"));
        seq_lock::format_value(UnitKind::Osc,variant,C::P_PW,32767,value,sizeof value);
        CHECK(std::string(value)==(clean?"75.0%":"50.0%"));
        CHECK(seq_lock::supports_offset(UnitKind::Osc,variant,C::P_PITCH)==!qnt);
    }
}
void test_osc_clean_visual_lifecycle(){
    using C=OscUnit;OscCleanFixture f;auto& u=*f.osc;auto* other=new C;delete f.engine->replace_unit(0,1,other);
    f.snap();f.feed(16000);f.capture();CHECK(u.visual().valid&&u.visual().pitch_count>0&&!other->visual().valid);
    const auto generation=u.visual().generation;f.engine->chains[0].mute=true;
    q15 stereo[128];for(int i=0;i<64;++i)f.engine->render(stereo,64);f.capture();CHECK(u.visual().generation!=generation);
    module_inspector_top(false,true);CHECK(module_inspector_text("chain muted"));
    u.bypass=true;f.capture();CHECK(!u.visual().valid);u.bypass=false;f.capture();CHECK(!u.visual().valid);f.feed();f.capture();CHECK(u.visual().valid);
    g_edit_slot=1;f.capture();CHECK(!u.visual().valid&&!other->visual().valid);other->control_tick();q15 mono[32];for(int i=0;i<100;++i)other->process(mono,32);f.capture();CHECK(other->visual().valid);
    g_overlay=Overlay::None;f.capture();CHECK(!other->visual().valid);module_inspector_surface(true);CHECK(!osc_focus_draw_top());
    // Focused replacement invalidates old traces without touching a freed pointer.
    g_overlay=Overlay::Editor;g_edit_slot=0;delete f.engine->replace_unit(0,0,new C);f.osc=static_cast<C*>(f.engine->chains[0].units[0]);f.capture();CHECK(!f.osc->visual().valid);
}
void test_osc_clean_frozen_audio(){
    using C=OscUnit;OscCleanFixture f;auto& u=*f.osc;
    for(auto theme:{palette::Theme::Pink,palette::Theme::Graphite}){
        palette::set(theme);
        for(int p=0;p<C::P_COUNT;++p){
            osc_focus_select(p);u.params[C::P_PITCH].base=p==C::P_PITCH?32767:16384;
            u.params[C::P_SHAPE].base=q15((p*5000)%32768);u.params[C::P_FOLD].base=23000;u.params[C::P_SUB].base=9000;
            for(int n=0;n<C::P_COUNT;++n){u.params[n].src=ModSource(int(ModSource::SigilA)+n%4);u.params[n].mod_depth=n%2?-11000:8000;u.params[n].snap();}
            f.snap();f.feed(16000);f.capture();CHECK(u.visual().valid);
            const auto trace=u.visual();const auto top=module_inspector_top(false,true);const int top_count=objects;
            CHECK(module_inspector_text("osc out")&&module_inspector_text("stored"));
            // Every output column comes from the actual published extrema, fixed +/-FS.
            int occupied=0;for(int col=0;col<C::Visual::COLUMNS;++col)if(trace.occupied[col]){
                ++occupied;const int x=24+col*226/(C::Visual::COLUMNS-1),y=110-int(trace.out_max[col])*42/32768;
                CHECK(pixels[y*400+x]==m::spot());}
            CHECK(occupied>0);if(p==C::P_PITCH)CHECK(occupied<C::Visual::COLUMNS);
            f.lower();osc_geometry(top_count);const auto lower=pixels;
            // Stored outline / effective fill remain distinct, including edited locks.
            const auto xy=OscCleanFixture::point(p);const int i=OscCleanFixture::index(p),x=p==C::P_SHAPE?14:14+(i%2)*162,w=p==C::P_SHAPE?278:130;
            CHECK(pixels[(xy.second-4)*320+x+int(u.params[p].base)*w/32767]==m::spot());
            const q15 saved=u.params[p].base;u.params[p].base=1234;g_ui_snapshot={};
            {ModuleInspectorNoEngine frozen;module_inspector_surface(false);osc_focus_draw(u);osc_geometry();CHECK(pixels==lower);
                module_inspector_surface(true);CHECK(osc_focus_draw_top());osc_geometry();CHECK(pixels==top);}
            u.params[p].base=saved;
        }
    }
    g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;
    osc_focus_select(C::P_GAIN);CHECK(f.engine->set_step_lock(0,7,0,C::P_GAIN,7000));u.params[C::P_GAIN].effective=27000;f.capture();
    module_inspector_top();CHECK(module_inspector_text("edit lock")&&module_inspector_text("live != edit step"));f.lower();
    const auto xy=OscCleanFixture::point(C::P_GAIN);CHECK(pixels[(xy.second-4)*320+176+7000*130/32767]==m::spot());
    CHECK(pixels[xy.second*320+176+27000*130/32767]==m::spot());theme_test::select();
}
int run_osc_clean_ui(){
    int failed=0;const auto run=[&](const char* name,auto test){try{test();std::printf("ok: %s\n",name);}catch(const std::exception& e){++failed;std::printf("FAIL: %s: %s\n",name,e.what());}};
    run("osc v2 main / seven base+lock rails / fine / bank ownership",test_osc_clean_controls);
    run("osc v2 lens / quarantine / mode-aware values / old mode isolation",test_osc_clean_lens_identity);
    run("osc v2 actual audio / mute / bypass / focus replacement",test_osc_clean_visual_lifecycle);
    run("osc v2 actual minmax / sparse bins / frozen pairs / both themes",test_osc_clean_frozen_audio);
    std::printf("osc v2 ui: %d/4 passed, shared peak %d/8192 (not GPU timing)\n",4-failed,maximum_shared_objects);return failed?1:0;
}
} // namespace
