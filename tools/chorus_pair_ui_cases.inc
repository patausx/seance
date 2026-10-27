// Current Chorus-only tests. Reuse host fixture utilities, never old suite mains.
namespace chorus_pair_cases {
namespace h=module_headings_cases;
namespace f=fader_lancet_cases;
using C=ChorusUnit;
struct Scene:h::Scene {
    Scene():h::Scene(UnitKind::Chorus){chorus_focus_select(C::P_SPREAD);capture();}
    ~Scene(){editor_cancel_input();}
};
int gestures=0,frozen=0,motion_frames=0;
std::ofstream motion;
C& unit(Scene&s){return *static_cast<C*>(s.unit);}
std::array<q15,4> bases(const C&u){return {u.params[0].base,u.params[1].base,u.params[2].base,u.params[3].base};}
void down(int x,int y){editor_touch_down(x,y);}
void drag(int x,int y){editor_touch_drag(x,y);}
void lift(){editor_touch_release();small_press::cancel();}
void cancel(){editor_cancel_input();param_lens_touch_release();small_press::cancel();}
void take(Scene&s,const std::string&id,const std::string&variant){
    h::take(s,id,variant);s.capture();editor_style::capture();editor_gpu::begin();
    module_inspector_surface(true);{f::AudioHidden hidden;top_draw();}
    CHECK(!outside_rects&&!outside_lines&&!text_overlaps);const auto top=pixels;const int upper=objects;
    module_inspector_surface(false);{f::AudioHidden hidden;editor_draw();}
    CHECK(!outside_rects&&!outside_lines&&!text_overlaps);const auto bottom=pixels;const int lower=objects;
    const auto redraw=[&]{
        module_inspector_surface(true);{ModuleInspectorNoEngine hidden;top_draw();}
        CHECK(pixels==top&&objects==upper&&!text_overlaps);
        module_inspector_surface(false);{ModuleInspectorNoEngine hidden;
            chorus_focus_draw(unit(s));recorder_focus_draw_return();if(param_lens_is_open())param_lens_draw_bottom();}
        CHECK(pixels==bottom&&objects==lower&&!text_overlaps);
    };
    redraw();{f::PoisonAfterCapture poison(s);redraw();}++frozen;
}
void controls(){
    Scene s;auto&u=unit(s);
    for(int raw:{-32767,0,32767}) {
        char text[24];module_inspector_value_text(text,sizeof text,UnitKind::Chorus,3,q15(raw));
        CHECK(std::string(text)==(raw<0?"0 deg":raw>0?"180 deg":"90 deg"));++gestures;
    }
    for(int p=0;p<4;++p)for(int v:{p==3?-32767:0,12000,32767})for(int corner=0;corner<4;++corner){
        s.set(p,v);const auto before=bases(u);const int dirty=mutations;
        const int x=corner&1?311:8,y=39+p*37+((corner&2)?36:0);
        down(x,y);CHECK(chorus_focus_selected()==p&&bases(u)==before&&mutations==dirty);
        drag(x,y);CHECK(bases(u)==before&&mutations==dirty);lift();++gestures;
    }
    for(int p=0;p<4;++p){
        s.set(p,p==3?0:12000);const int y=65+p*37;const int old=u.params[p].base;
        down(170,y);drag(176,y+3);CHECK(u.params[p].base==old+6*(p==3?65534:32767)/203);
        const int dirty=mutations;drag(176,y+3);CHECK(mutations==dirty);lift();++gestures;
        down(170,y);u.params[p].base=2222;drag(170,y);CHECK(u.params[p].base==2222);cancel();++gestures;
        down(170,y);const auto before=bases(u);chorus_focus_select((p+1)%4);drag(220,y);CHECK(bases(u)==before);cancel();++gestures;
    }
    const auto before=bases(u);
    for(auto xy:{std::pair<int,int>{160,32},{180,189},{160,202},{305,219}}){down(xy.first,xy.second);drag(xy.first+8,xy.second);lift();CHECK(bases(u)==before);++gestures;}
}
void lock_test(const std::string&prefix){
    Scene s;auto&u=unit(s);g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;g_view=View::Sequencer;
    for(int p=0;p<4;++p){
        const int old=p==3?-11000:4000;CHECK(s.engine->set_step_lock(0,7,s.slot,p,old));const auto before=bases(u);
        down(180,65+p*37);drag(186,65+p*37);lift();
        CHECK(seq_display_value(s.slot,p,u.params[p].base)==old+6*(p==3?65534:32767)/203&&bases(u)==before);
        down(20,47+p*37);lift();CHECK(seq_display_value(s.slot,p,u.params[p].base)==u.params[p].base);++gestures;
    }
    CHECK(s.engine->set_step_lock(0,7,s.slot,3,-20000));s.select(3);take(s,prefix+"chorus_lock","lock");
    down(30,202);CHECK(!u.dry_compare_requested());lift();++gestures;
    const bool bypass=u.bypass;down(256,10);lift();CHECK(u.bypass==bypass);
    // Fill with foreign locks; no silent base overwrite when the bank is full.
    s.engine->pattern.steps[0][7].lock_count=0;
    for(int i=0;i<SEQ_LOCKS;++i)CHECK(s.engine->set_step_lock(0,7,0,i,1234));
    const auto before=bases(u);down(180,176);drag(186,176);lift();CHECK(bases(u)==before);
    CHECK(seq_display_value(s.slot,3,u.params[3].base)==u.params[3].base);++gestures;
    take(s,prefix+"chorus_lock_full","lock-full");
    down(180,176);g_seq_lock_step=8;drag(210,176);CHECK(bases(u)==before);cancel();++gestures;
}
void comparison(const std::string&prefix){
    for(int reason=0;reason<13;++reason){
        Scene s;auto&u=unit(s);const auto before=bases(u);const auto stored=s.hash();const int dirty=mutations;
        down(30,202);CHECK(u.dry_compare_requested());s.render(256);CHECK(u.visual_compare_q15()==32768);
        drag(30,202);CHECK(u.dry_compare_requested());
        switch(reason){
            case 0:take(s,prefix+"chorus_compare","compare");lift();break;
            case 1:drag(116,202);drag(30,202);break; // returning held cannot re-arm
            case 2:cancel();break;
            case 3:g_overlay=Overlay::None;s.capture();break;
            case 4:g_edit_slot=0;s.capture();break;
            case 5:g_edit_chain=1;s.capture();break;
            case 6:g_view=View::Sequencer;s.capture();break;
            case 7:s.engine->active_pattern=1;s.capture();s.engine->active_pattern=0;break;
            case 8:g_seq_lock_armed=true;s.capture();g_seq_lock_armed=false;break;
            case 9:u.bypass=true;s.capture();u.bypass=false;break;
            case 10:s.engine->chains[0].mute=true;s.capture();s.engine->chains[0].mute=false;break;
            case 11:{u32 key=KEY_L;CHECK(param_lens_input(key,KEY_L|KEY_TOUCH,0,false));CHECK(param_lens_is_open());break;}
            case 12:s.render(8192);drag(30,202);break; // expired lease cannot be resurrected
        }
        CHECK(!u.dry_compare_requested());g_edit_chain=0;g_edit_slot=s.slot;param_lens_cancel();
        s.render(256);CHECK(u.visual_compare_q15()==0&&bases(u)==before&&mutations==dirty&&s.hash()==stored);cancel();++gestures;
    }
    for(int why=0;why<4;++why){
        Scene s;auto&u=unit(s);
        if(why==0)u.reset();
        if(why==1)u.bypass=true;
        if(why==2)s.engine->chains[0].mute=true;
        if(why==3){g_seq_lock_armed=true;g_view=View::Sequencer;g_seq_lock_chain=0;g_seq_lock_step=7;}
        down(30,202);CHECK(!u.dry_compare_requested());lift();++gestures;
    }
    // Four edges belong to the invisible 100x24 target, without changing values.
    {Scene s;auto&u=unit(s);for(auto xy:{std::pair<int,int>{16,194},{115,194},{16,217},{115,217}}){
        down(xy.first,xy.second);CHECK(u.dry_compare_requested());lift();CHECK(!u.dry_compare_requested());++gestures;}}
    // Delete a captured unit before cancellation: no stale pointer read under ASAN.
    {Scene s;auto*old=s.unit;down(30,202);s.unit=new C;delete s.engine->replace_unit(0,s.slot,s.unit);cancel();
        CHECK(!unit(s).dry_compare_requested());(void)old;++gestures;}
}
void states(const std::string&prefix){
    for(const std::string v:{"default","waiting","bypass","mute","dry","maximum","long6","holes","lens","idle_first","idle_later"}){
        Scene s;auto&u=unit(s);
        if(v=="waiting")u.reset();
        if(v=="bypass")u.bypass=true;
        if(v=="mute")s.engine->chains[0].mute=true;
        if(v=="dry"){s.set(2,0);s.render(256);}if(v=="maximum"){for(int p=0;p<4;++p)s.set(p,32767);s.render(512);}
        if(v=="long6"||v=="holes")s.path(v=="holes");
        if(v=="lens"){u32 key=KEY_L;CHECK(param_lens_input(key,KEY_L,0,false));param_lens_touch_release();}
        if(v=="idle_later"){host_time_ms+=5000;g_ui_frame+=137;}
        take(s,prefix+"chorus_"+v,v);
        if(v=="lens")CHECK(module_inspector_text("90 deg"));
    }
    for(int p=0;p<4;++p)for(int raw:{p==3?-32767:0,32767}){
        Scene s;s.set(p,raw);s.select(p);s.render(512);take(s,prefix+"chorus_p"+std::to_string(p)+"_"+std::to_string(raw+32767),"edge");
    }
    // MOD editing for the added bipolar parameter uses the ordinary owned relative law.
    {Scene s;auto&u=unit(s);u32 key=KEY_L;CHECK(param_lens_input(key,KEY_L,0,false));param_lens_touch_release();
        down(140,96);drag(146,96);lift();CHECK(u.params[3].base==6*65534/271);++gestures;
        take(s,prefix+"chorus_lens_spread","lens-spread");CHECK(module_inspector_text("94 deg"));param_lens_cancel();}
}
void routes(const std::string&prefix){
    for(int i=0;i<10;++i){Scene s;auto&u=unit(s);auto&q=u.params[3];q.base=0;
        q.src=i==8?ModSource::EnvFollow:sigil_mod_source(i==9?0:i);q.mod_depth=i==7?-20000:20000;q.mod_enabled=i!=9;q.snap();
        if(i!=8){const int sg=i==9?0:i;auto&sig=s.engine->sigils[sg];sig.depth=32767;
            for(int j=0;j<SIGIL_POINTS;++j)sig.set_point(j,22000);
            s.engine->reset_sigil(sg);}
        s.render(1024);CHECK(q.base==0);if(i==9)CHECK(q.target==0);
        take(s,prefix+"chorus_route_"+std::to_string(i),"route");++gestures;
    }
}
void live_motion(const std::string&prefix){
    Scene s;auto&u=unit(s);const int base[]={7000,12000,16000,0},depth[]={6000,10000,14000,25000};
    for(int p=0;p<4;++p){auto&q=u.params[p];q.base=base[p];q.src=ModSource::SigilA;q.mod_depth=depth[p];q.mod_enabled=true;q.snap();}
    auto&sig=s.engine->sigils[0];sig.depth=32767;
    for(int j=0;j<SIGIL_POINTS;++j)sig.set_point(j,q15(std::lround(std::sin(double(j)*6.283185307179586/SIGIL_POINTS)*30000)));
    auto config=s.engine->sigil_config[0];config.enabled=true;config.legacy=true;s.engine->set_sigil_config(0,config);
    s.engine->set_sigil_sync(0,false);s.engine->set_sigil_rate(0,6000);s.engine->reset_sigil(0);
    for(int n=0;n<48;++n){s.render(1024);const std::string id=prefix+"chorus_motion_"+(n<10?"0":"")+std::to_string(n);
        take(s,id,"engine-sigil");motion<<prefix<<'\t'<<n<<'\t'<<id<<'\t'<<sig.value<<'\t'<<u.visual_delay_q16(0)<<'\t'<<u.visual_delay_q16(1);
        for(int p=0;p<4;++p){const auto&q=u.params[p];CHECK(q.base==base[p]);const int lo=p==3?-32767:0;
            CHECK(q.target==std::max(lo,std::min(32767,base[p]+int((int64_t(sig.value)*depth[p])>>15))));
            const int v=u.visual_effective(p);const int x=100+(v-lo)*203/(32767-lo);
            CHECK(pixels[(66+p*37)*320+x]==palette::spot());
            motion<<'\t'<<q.base<<'\t'<<q.target<<'\t'<<v;
        }motion<<'\n';++motion_frames;
    }
}
int run(const std::filesystem::path&dir){
    CHECK(!std::filesystem::exists(dir));std::filesystem::create_directories(dir);h::output=dir;h::capture_files=true;
    h::manifest.open(dir/"surfaces.tsv");h::manifest<<"id\tkind\tmodule\tvariant\tsurface\tobjects\toutside_rects\toutside_lines\ttext_overlaps\traster_fnv64\n";
    motion.open(dir/"motion.tsv");motion<<"theme\tframe\tid\tsource\tdelay0\tdelay1";
    for(int p=0;p<4;++p)motion<<"\tbase"<<p<<"\ttarget"<<p<<"\tlive"<<p;
    motion<<'\n';
    f::sample_fixture();CHECK(editor_style::init());
    for(auto theme:{palette::Theme::Graphite,palette::Theme::Pink}){palette::set(theme);const std::string prefix=theme==palette::Theme::Graphite?"graphite_":"pink_";
        controls();lock_test(prefix);comparison(prefix);states(prefix);routes(prefix);live_motion(prefix);
    }
    editor_style::shutdown();CHECK(editor_gpu::textures.empty());h::manifest.close();motion.close();
    std::ofstream out(dir/"behavior.json");out<<"{\"status\":\"passed\",\"gestures\":"<<gestures<<",\"frozen_pairs\":"<<frozen
        <<",\"motion_frames\":"<<motion_frames<<",\"duals\":"<<h::duals<<",\"peak_objects\":"<<h::peak<<",\"old_suites_run\":0}\n";
    std::printf("chorus pair: %d gestures, %d frozen pairs, %d engine-sigil frames, peak %d/8192 software objects\n",gestures,frozen,motion_frames,h::peak);return 0;
}
}
int main(int argc,char**argv){try{CHECK(argc==3);return chorus_pair_cases::run(argv[2]);}
catch(const std::exception&e){std::fprintf(stderr,"chorus pair failed: %s\n",e.what());return 1;}}
