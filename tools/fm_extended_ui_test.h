// Actual extended FM UI: no alternative controller or authored screens.
void test_fm_extended_controls(){
    FmMenuFixture f;auto& u=f.fm;CHECK(u.extended&&u.num_params==12);
    for(int p:{FmUnit::P_PITCH_ENV,FmUnit::P_PITCH_TIME}){
        const int y=p==10?155:195;auto& v=u.params[p];v.base=p==10?-6000:14000;
        f.choose(p);const q15 before=v.base;const int dirty=mutations;
        f.down(258,y);CHECK(v.base==before);fm_focus_drag(u,258,y);CHECK(mutations==dirty);
        fm_focus_drag(u,265,y);CHECK(v.base==before+7*32767/(p==10?168:128));f.lift();
        g_seq_lock_armed=true;g_view=View::Sequencer;g_seq_lock_chain=0;g_seq_lock_step=5;
        CHECK(f.engine->set_step_lock(0,5,0,p,p==10?-12000:12000));const q15 base=v.base;
        f.down(258,y);fm_focus_drag(u,265,y);CHECK(v.base==base);
        CHECK(seq_display_value(0,p,base)==(p==10?-12000:12000)+7*32767/(p==10?168:128));f.lift();
        f.tap(230,y-13);CHECK(seq_display_value(0,p,base)==base);
        g_seq_lock_armed=false;g_view=View::Chains;
        u.drone=true;const int frozen=mutations;f.down(258,y);fm_focus_drag(u,300,y);f.lift();CHECK(v.base==base&&mutations==frozen);u.drone=false;
        // Shared held-L mod carries each appended param, edits persist, never enables TEMP.
        fm_focus_select(p);f.capture();open_lens(f);lens_permanent_probe(u,p,false);CHECK(fm_focus_selected()==p&&!f.engine->param_temp_active());
        param_lens_cancel();
    }
    f.tap(250,35);int count=0;
    for(int row=0;row<2;++row)for(int wave=0;wave<4;++wave){
        const int xs[]={84,144,204,274};f.tap(xs[wave],81+49*row);
        CHECK(int(row?u.mod_wave:u.carrier_wave)==wave);f.capture();fm_menu_render(f,false);++count;
    }
    g_seq_lock_armed=true;g_view=View::Sequencer;const auto carrier=u.carrier_wave,mod=u.mod_wave;const bool phase=u.phase_reset,guard=u.alias_safe;const int dirty=mutations;
    f.tap(84,81);f.tap(84,130);f.tap(70,204);f.tap(230,204);f.tap(150,10);f.tap(258,10);
    CHECK(u.carrier_wave==carrier&&u.mod_wave==mod&&u.phase_reset==phase&&u.alias_safe==guard&&!u.drone&&!u.bypass&&mutations==dirty);
    g_seq_lock_armed=false;g_view=View::Chains;
    // Harmonic mode is an edit aid, not a destructive quantizer or hidden DSP mode.
    auto& ratio=u.params[FmUnit::P_RATIO];ratio.base=8123;ratio.src=ModSource::SigilB;ratio.mod_depth=4000;
    const int unchanged=mutations;f.tap(230,162);CHECK(ratio.base==8123&&mutations==unchanged);
    f.tap(40,35);f.down(154,155);CHECK(ratio.base==8123);fm_focus_drag(u,164,155);f.lift();
    const int ratios[]={2048,4096,6144,8192,12288,16384,20480,24576,32768};bool anchor=false;
    for(int q12:ratios)anchor|=ratio.base==((q12-2048)*32767+30719)/30720;
    CHECK(anchor&&ratio.src==ModSource::SigilB&&ratio.mod_depth==4000);
    f.down(154,155);const q15 snapped=ratio.base;fm_focus_drag(u,154,155);CHECK(ratio.base==snapped);f.lift();
    // Legacy selection, modulation entry and hidden controls cannot upgrade a card.
    u.set_extended(false);u.params[10].base=19000;u.params[10].src=ModSource::SigilD;u.params[10].mod_depth=18000;
    std::array<q15,10> old{};for(int p=0;p<10;++p)old[p]=u.params[p].base;
    fm_focus_select(7);f.tap(258,155);f.capture();CHECK(!u.extended&&u.num_params==10);
    f.tap(250,35);g_seq_lock_armed=true;g_view=View::Sequencer;f.tap(100,82);CHECK(!u.extended);
    g_seq_lock_armed=false;g_view=View::Chains;f.tap(100,82);CHECK(u.extended&&u.num_params==12&&u.params[10].base==0&&u.params[10].src==ModSource::None&&u.params[10].mod_depth==0);
    CHECK(u.params[11].base==FmUnit::DEFAULT_PITCH_TIME&&u.carrier_wave==FmUnit::Wave::Sine&&u.mod_wave==FmUnit::Wave::Sine);
    for(int p=0;p<10;++p)CHECK(old[p]==u.params[p].base);
    f.tap(140,162); // restore free edit mode for other suites
    std::printf("fm extended controls: 2 relative/lock/held-mod parameters, %d wave choices, inert lock policies, harmonic edit and explicit neutral legacy upgrade\n",count);
}
void test_fm_extended_markers(){
    FmMenuFixture f;auto& u=f.fm;int count=0;
    const int xs[]={14,118,14,118,118,222,14,14,222,118,222,222};
    const int ys[]={170,170,214,214,170,214,214,170,170,214,170,214};
    for(int p=0;p<12;++p)for(int base:{0,15000,32767})for(int applied:{0,7000,27000}){
        auto& v=u.params[p];const int b=v.bipolar?base*2-32767:base,l=v.bipolar?applied*2-32767:applied;
        v.base=q15(b);v.effective=q15(l);v.target=32000;fm_focus_select(p);f.capture();fm_menu_render(f,false);
        const int bx=xs[p]+(v.bipolar?(b+32767)*84/65534:b*84/32767),lx=xs[p]+(v.bipolar?(l+32767)*84/65534:l*84/32767),y=ys[p];
        CHECK(pixels[(y-4)*320+bx]==m::spot());
        for(int dy=-2;dy<=2;++dy)for(int dx=-(2-std::abs(dy));dx<=2-std::abs(dy);++dx)CHECK(pixels[(y+dy)*320+lx+dx]==m::spot());
        for(int x=xs[p];x<=xs[p]+84;++x)if(std::abs(x-bx)>4&&std::abs(x-lx)>4){
            CHECK(pixels[y*320+x]==m::branch());const bool tick=x==xs[p]||x==xs[p]+42||x==xs[p]+84;
            CHECK(pixels[(y-1)*320+x]==(tick?m::branch():m::paper_color(y-1)));
        }
        ++count;
    }
    for(int p:{0,2,8,10,11}){auto& v=u.params[p];v.base=v.bipolar?-6000:12000;v.snap();v.src=ModSource::SigilA;v.mod_depth=12000;v.resolve(0);
        for(int source:{-32767,12000,32767}){v.resolve(q15(source));CHECK(v.target!=v.effective);fm_focus_select(p);f.capture();fm_menu_render(f,true);const int top=objects;fm_menu_render(f,false);CHECK(top+objects<8192);maximum_shared_objects=std::max(maximum_shared_objects,top+objects);}
    }
    std::printf("fm extended markers: %d base/live positions / 12 thin rails / signed pitch envelope / actual Param slew\n",count);
}
void test_fm_extended_pitch_preview(){
    FmMenuFixture f;auto& u=f.fm;int segments=0;
    const char* notes[]={"c","c#","d","d#","e","f","f#","g","g#","a","a#","b"};
    for(int raw=0;raw<=32767;++raw){
        const int midi=(2400+raw*7200/32768+50)/100;char actual[20],expected[20];
        fm_context_value_text(actual,sizeof actual,0,q15(raw));std::snprintf(expected,sizeof expected,"%s%d",notes[midi%12],midi/12-1);CHECK(std::strcmp(actual,expected)==0);
    }
    for(int raw:{0,6599,16156,32767}){
        u.params[0].base=q15(raw);u.params[0].snap();fm_focus_select(0);f.capture();fm_menu_render(f,true);const auto image=pixels;
        char note[20];fm_context_value_text(note,sizeof note,0,q15(raw));pixels.assign(400*240,0);m::paper(400);CHECK(note_display::melodic(note,m::ink(),m::spot(),14,45));
        for(int y=75;y<=150;++y)for(int x=24;x<=130;++x)CHECK(pixels[y*400+x]==image[y*400+x]);
    }
    for(int depth:{-32767,0,16384,32767})for(bool drone:{false,true}){
        u.drone=drone;u.params[10].base=q15(depth);u.params[10].snap();u.params[11].base=FmUnit::DEFAULT_PITCH_TIME;u.params[11].snap();fm_focus_select(10);f.capture();
        std::vector<GranLine> lines;gran_lines=&lines;fm_menu_render(f,false);gran_lines=nullptr;CHECK(fm_has("next strike")&&fm_label("100ms"));
        const int cents=FmUnit::pitch_env_cents(q15(depth));int count=0;
        for(const auto& line:lines)if(line.color==m::spot()&&line.x1==line.x0+1&&line.x0>=14&&line.x1<=305&&line.y0>=67&&line.y1<=121){
            const int x=line.x1-14;CHECK(line.y0==94-cents*(292-x)*27/(4800*291));CHECK(line.y1==94-cents*(291-x)*27/(4800*291));++count;
        }
        CHECK(count==(drone?0:291));if(drone)CHECK(fm_label("held in drone"));segments+=count;
        const auto image=pixels;u.params[10].effective=1234;u.params[11].effective=2222;fm_menu_render(f,false);CHECK(pixels==image);
    }
    std::printf("fm pitch preview: 32768 exact note mappings / 4 gothic notes / %d signed full-duration segments / exact zero endpoint / held drone / frozen settings\n",segments);
}
void test_fm_extended_reference_frames(){
    FmMenuFixture f;auto& u=f.fm;int columns=0,pairs=0;
    auto ord=[](q15 x){return std::max(-27,std::min(27,int(x)*27/32768));};
    struct Lines{std::vector<GranLine> data;Lines(){gran_lines=&data;}~Lines(){gran_lines=nullptr;}};
    for(bool drone:{false,true})for(int cw=0;cw<4;++cw)for(int mw=0;mw<4;++mw){
        u.carrier_wave=FmUnit::Wave(cw);u.mod_wave=FmUnit::Wave(mw);u.drone=drone;u.alias_safe=(mw&1)!=0;
        u.params[10].base=q15(cw&1?-16000:22000);u.params[11].base=q15(mw*9000);for(auto& p:u.params)p.snap();
        fm_focus_select(2);f.capture();FmUnit oracle;oracle.carrier_wave=u.carrier_wave;oracle.mod_wave=u.mod_wave;oracle.drone=u.drone;oracle.alias_safe=u.alias_safe;oracle.phase_reset=true;
        for(int p=0;p<12;++p){oracle.params[p].base=u.params[p].effective;oracle.params[p].snap();}oracle.reset();oracle.control_tick();oracle.trigger();
        std::array<int,292> low{},high{},mlow{},mhigh{};
        for(int x=0;x<292;++x){int lo=32767,hi=-32768,ml=32767,mh=-32768;for(int k=0;k<4;++k){q15 out=0;oracle.process(&out,1);lo=std::min(lo,int(out));hi=std::max(hi,int(out));ml=std::min(ml,int(oracle.visual_modulator()));mh=std::max(mh,int(oracle.visual_modulator()));}low[x]=ord(q15(lo));high[x]=ord(q15(hi));mlow[x]=ord(q15(ml));mhigh[x]=ord(q15(mh));}
        Lines lines;fm_menu_render(f,false);gran_lines=nullptr;int out_count=0,mod_count=0;
        for(const auto& line:lines.data)if(line.x0==line.x1&&line.x0>=14&&line.x0<=305&&line.y0>=67&&line.y1<=122){const int x=line.x0-14;
            if(line.color==m::spot()){CHECK(line.y0==94-high[x]&&line.y1==std::max(line.y0+1,94-low[x]));++out_count;}
            if(line.color==m::ghost()){CHECK(line.y0==94-mhigh[x]&&line.y1==std::max(line.y0+1,94-mlow[x]));++mod_count;}
        }
        CHECK(out_count==292&&mod_count==292);columns+=out_count+mod_count;
        const auto bottom=pixels;fm_menu_render(f,true);const auto top=pixels;
        Engine* engine=g_eng;auto* audio=g_audio;g_eng=nullptr;g_audio=nullptr;
        u.carrier_wave=FmUnit::Wave::Sine;u.params[10].base=123;g_ui_snapshot={};
        fm_menu_render(f,false);CHECK(pixels==bottom);const int n=objects;fm_menu_render(f,true);CHECK(pixels==top&&n+objects<8192);maximum_shared_objects=std::max(maximum_shared_objects,n+objects);
        g_eng=engine;g_audio=audio;++pairs;
    }
    // Phase changes alone cannot move a reset waveform. Published amp stays separate.
    fm_focus_select(2);f.capture();fm_menu_render(f,false);const auto fixed=pixels;
    g_ui_snapshot.generator_phase_hi^=255;g_ui_snapshot.generator_phase_lo^=255;fm_focus_capture();fm_menu_render(f,false);CHECK(pixels==fixed);
    // Engine state must not be advanced by either a cache hit or regeneration.
    FmUnit twin=u;q15 a[64]={},b[64]={};u.process(a,64);for(int k=0;k<10;++k)f.capture();twin.process(b,64);CHECK(std::equal(a,a+64,b));
    std::printf("fm extended model: %d native columns / %d frozen duals / all16 wave pairs in strike+drone / detached draw and phase isolation\n",columns,pairs);
}
