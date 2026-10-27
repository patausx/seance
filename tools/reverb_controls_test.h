// Included by the real-controller harness; no replacement input or DSP model.
struct RevFixture : Fixture {
    ReverbUnit* rev;
    RevFixture() {
        delete engine->replace_unit(0,0,new ReverbUnit);
        rev=static_cast<ReverbUnit*>(engine->chains[0].units[0]);
        g_edit_chain=g_edit_slot=0;g_view=View::Chains;g_overlay=Overlay::Editor;
        g_seq_lock_armed=false;g_seq_lock_chain=g_seq_lock_step=0;
        reverb_focus_release();reverb_focus_input(0,0,false);
        engine->request_pattern(0);sequencer_view_enter();
    }
    void down(int x,int y) { CHECK(reverb_focus_touch_body(*rev,x,y)); }
    void drag(int x,int y) { reverb_focus_drag(*rev,x,y); }
    void tap(int x,int y) { down(x,y);reverb_focus_touch_release(); }
};
void test_rev_controls() {
    RevFixture f;auto& u=*f.rev;
    const int x[]={50,210,210,50},y[]={40,40,198,198};
    for(int p=0;p<4;++p) {
        u.params[p].base=12000;
        q15 before[5];for(int i=0;i<5;++i)before[i]=u.params[i].base;
        const int dirty=mutations;
        f.down(x[p],y[p]);CHECK(reverb_focus_selected()==p);
        f.drag(x[p],y[p]);CHECK(mutations==dirty && u.params[p].base==12000);
        f.drag(x[p]+24,y[p]+70);CHECK(u.params[p].base==12000+3276);
        for(int i=0;i<5;++i)if(i!=p)CHECK(u.params[i].base==before[i]);
        const int changed=mutations;u.params[p].base=17000;
        f.drag(x[p]+24,y[p]);CHECK(u.params[p].base==17000 && mutations==changed);
        f.drag(x[p],y[p]);CHECK(u.params[p].base==12000);
        for(int repeat=0;repeat<5;++repeat) {f.down(x[p],y[p]);f.drag(319,y[p]);}
        CHECK(u.params[p].base==32767);
        for(int repeat=0;repeat<8;++repeat) {f.down(x[p],y[p]);f.drag(0,y[p]);}
        CHECK(u.params[p].base==0);
        f.down(x[p],y[p]);reverb_focus_release();f.drag(319,y[p]);CHECK(u.params[p].base==0);
    }
    const int dirty=mutations;
    for(int yy:{65,100,116,160,178,222})for(int xx:{20,110,166,250,300}) {
        f.down(xx,yy);f.drag(319,198);CHECK(g_drag==Drag::None && mutations==dirty);
    }
    f.tap(290,10);CHECK(f.engine->chains[0].units[0]==&u && mutations==dirty);
    f.tap(210,10);CHECK(u.bypass);f.tap(210,10);CHECK(!u.bypass);
    f.tap(150,10);CHECK(u.params[4].base==32767);
    for(auto& p:u.params)p.snap();
    std::array<q15,512> audio{};u.process(audio.data(),512);CHECK(u.visual_freeze()==32767);
    for(int p:{0,1,3}) {const q15 v=u.params[p].base;f.down(x[p],y[p]);f.drag(319,y[p]);CHECK(u.params[p].base==v);}
    f.down(210,198);f.drag(234,198);CHECK(u.params[2].base==3276);
    f.tap(150,10);for(auto& p:u.params)p.snap();u.process(audio.data(),512);CHECK(u.visual_freeze()==0);
    f.down(50,40);g_edit_slot=1;f.drag(100,40);CHECK(u.params[0].base==0);g_edit_slot=0;
    f.down(50,40);g_overlay=Overlay::None;f.drag(100,40);CHECK(u.params[0].base==0);
}
void test_rev_locks_lens() {
    RevFixture f;auto& u=*f.rev;g_seq_lock_armed=true;g_seq_lock_step=7;
    CHECK(f.engine->set_step_lock(0,7,0,0,1000));const q15 base=u.params[0].base;
    const int dirty=mutations;const auto old=f.hash();
    f.down(50,40);f.drag(50,40);CHECK(f.hash()==old && mutations==dirty);
    f.drag(74,40);CHECK(seq_display_value(0,0,0)==4276 && u.params[0].base==base);
    CHECK(f.engine->set_step_lock(0,7,0,0,7000));const auto external=f.hash();
    f.drag(74,40);CHECK(f.hash()==external);
    f.drag(50,40);CHECK(seq_display_value(0,0,0)==1000);
    for(int p:{1,2,3})CHECK(f.engine->set_step_lock(0,7,0,p,1000));
    const auto full=f.hash();f.tap(150,10);f.tap(210,10);
    CHECK(f.hash()==full && u.params[4].base==0 && !u.bypass);
    CHECK(!param_lens_touch_down(250,232));
    f.engine->clear_track(0);f.tap(150,10);CHECK(seq_display_value(0,4,0)==32767 && u.params[4].base==0);
    f.tap(150,10);CHECK(seq_display_value(0,4,32767)==0);
    f.down(50,40);g_seq_lock_step=8;f.drag(100,40);CHECK(u.params[0].base==base && g_drag==Drag::None);
    g_seq_lock_armed=false;f.tap(50,198);CHECK(reverb_focus_selected()==3);
    CHECK(param_lens_touch_down(250,232));param_lens_touch_release();
    param_lens_touch_down(300,185);param_lens_touch_release();
    CHECK(u.params[3].src==ModSource::EnvFollow && u.params[0].src==ModSource::None);
    lens_permanent_probe(u,3);CHECK(reverb_focus_selected()==3);
    CHECK(param_lens_touch_down(250,232));param_lens_touch_release();lens_frame(KEY_B,KEY_B);CHECK(!param_lens_is_open());
}
void rev_menu_palette_and_paper() {
    auto paper_row=[](int y) { return theme_test::paper(y); };
    const std::array<u32,4> inks={theme_test::ink(),theme_test::spot(),theme_test::ghost(),theme_test::branch()};
    CHECK(pixels.size()==(size_t)width*HEIGHT);
    for(int yy=0;yy<HEIGHT;++yy)for(int xx=0;xx<width;++xx) {
        const u32 px=pixels[yy*width+xx];
        if(!(px==paper_row(yy)||std::find(inks.begin(),inks.end(),px)!=inks.end()))
            std::fprintf(stderr,"rev bad palette x=%d y=%d px=%08x paper=%08x\n",xx,yy,(unsigned)px,(unsigned)paper_row(yy));
        CHECK(px==paper_row(yy)||std::find(inks.begin(),inks.end(),px)!=inks.end());
    }
    for(int yy=0;yy<HEIGHT;++yy)for(int xx=0;xx<2;++xx)CHECK(pixels[yy*width+xx]==paper_row(yy));
}
void rev_render(RevFixture& f,bool top=false) {
    f.engine->capture_ui_snapshot(g_ui_snapshot,0,0);reverb_focus_capture();
    width=top?400:320;objects=outside_rects=outside_lines=text_overlaps=0;texts.clear();pixels.assign(width*HEIGHT,0xff000000u);
    struct Restore { Engine* e=g_eng;platform::AudioOut* a=g_audio;Restore(){g_eng=nullptr;g_audio=nullptr;}~Restore(){g_eng=e;g_audio=a;} } restore;
    if(top)CHECK(reverb_focus_draw_top());else reverb_focus_draw(*f.rev);
    if(outside_rects||outside_lines||text_overlaps)std::fprintf(stderr,"rev %s diagnostics rect=%d line=%d overlap=%d\n",top?"top":"bottom",outside_rects,outside_lines,text_overlaps);
    CHECK(objects>0&&objects<C2D_DEFAULT_MAX_OBJECTS*2&&outside_rects==0&&outside_lines==0&&text_overlaps==0);
    for(const auto& t:texts)CHECK(t.x>=0&&t.y>=0&&t.x+t.w<=width&&t.y+t.h<=HEIGHT);
    rev_menu_palette_and_paper();
    if(top)maximum_top_objects=std::max(maximum_top_objects,objects);else maximum_objects=std::max(maximum_objects,objects);
}
void rev_dual(RevFixture& f) {
    rev_render(f,true);const int top=objects;rev_render(f,false);
    maximum_shared_objects=std::max(maximum_shared_objects,top+objects);CHECK(top+objects<C2D_DEFAULT_MAX_OBJECTS*2);
}
void test_rev_menu_style_geometry() {
    RevFixture f;auto& u=*f.rev;
    struct Zone{int x,y,w,h,p;} zones[]={{8,26,148,31,0},{166,26,148,31,1},{166,184,148,31,2},{8,184,148,31,3}};
    for(const auto& z:zones)for(int edge=0;edge<4;++edge) {
        const int xx=edge==0?z.x:edge==1?z.x+z.w-1:z.x+z.w/2;
        const int yy=edge==2?z.y:z.y+z.h-1;f.down(xx,yy);CHECK(reverb_focus_selected()==z.p);reverb_focus_release();
    }
    const auto before=f.hash();q15 values[5];for(int p=0;p<5;++p)values[p]=u.params[p].base;
    for(auto xy:std::array<std::pair<int,int>,9>{{{7,26},{156,40},{165,40},{314,40},{20,116},{166,116},{250,160},{300,178},{290,10}}}) {
        f.down(xy.first,xy.second);f.drag(319,xy.second);reverb_focus_release();
    }
    // Exact header boundaries: freeze [126,192), bypass [195,233), all gaps inert.
    for(int xx:{125,192,194,233,284,319}) {f.tap(xx,10);CHECK(f.hash()==before);}
    f.tap(126,10);CHECK(u.params[4].base==32767);f.tap(191,10);CHECK(u.params[4].base==0);
    f.tap(195,10);CHECK(u.bypass);f.tap(232,10);CHECK(!u.bypass);
    CHECK(f.hash()==before);
    CHECK(f.hash()==before);for(int p=0;p<5;++p)CHECK(u.params[p].base==values[p]);
    rev_dual(f);
    g_overlay=Overlay::None;reverb_focus_capture();width=400;objects=0;pixels.assign(width*HEIGHT,0xff000000u);CHECK(!reverb_focus_draw_top());
}
void test_rev_draw() {
    RevFixture f;auto& u=*f.rev;
    for(int p=0;p<5;++p)for(int edge:{0,32767}) {
        for(auto& q:u.params){q.base=12345;q.src=ModSource::SigilD;q.mod_depth=32767;q.snap();}
        u.params[p].base=(q15)edge;u.params[p].snap();
        std::array<q15,512> audio{};audio.fill(9000);u.process(audio.data(),512);
        if(p<4)f.tap(p==0||p==3?50:210,p<2?40:198);else {if((u.params[4].base>16384)!=(edge>0))f.tap(150,10);}
        rev_dual(f);
    }
    for(auto& p:u.params){p.base=0;p.snap();}std::array<q15,512> audio{};u.process(audio.data(),512);rev_render(f,false);
    bool mix=false,decay=false,size=false;for(const auto& t:texts){if(t.text=="0%")mix=true;if(t.text=="0.20s")decay=true;if(t.text=="0%")size=true;}CHECK(mix&&decay&&size);
    for(auto& p:u.params){p.base=32767;p.snap();}audio.fill(9000);u.process(audio.data(),512);rev_render(f,false);
    mix=decay=size=false;for(const auto& t:texts){if(t.text=="100%")mix=true;if(t.text=="8.00s")decay=true;if(t.text=="100%")size=true;}CHECK(mix&&decay&&size);
    // Exact existing Seance Note masks and fixed left-pane bounds for every numeric grammar.
    const int xs[4]={50,210,210,50},ys[4]={40,40,198,198};
    for(int p=0;p<4;++p){f.tap(xs[p],ys[p]);u.params[p].base=p==0?32767:p==1?32767:12345;u.params[p].snap();rev_render(f,true);char hero[24];reverb_focus_value_text(hero,sizeof hero,p,u.params[p].base,u.modern());check_inspector_gothic_mask(hero,113);CHECK(!has_text_at(hero,14,83));const int top=objects;rev_render(f,false);CHECK(top+objects<C2D_DEFAULT_MAX_OBJECTS*2);}
    g_seq_lock_armed=true;g_seq_lock_step=0;CHECK(f.engine->set_step_lock(0,0,0,2,0));rev_dual(f);
    bool dry=false;for(const auto& t:texts)if(t.text=="0%")dry=true;CHECK(dry);
}
void test_rev_menu_lens_draw() {
    RevFixture f;auto& u=*f.rev;f.tap(50,198);CHECK(param_lens_touch_down(250,232));param_lens_touch_release();
    param_lens_capture();width=320;objects=outside_rects=outside_lines=text_overlaps=0;texts.clear();pixels.assign(width*HEIGHT,0xff000000u);param_lens_draw_bottom();
    CHECK(objects>0&&outside_rects==0&&outside_lines==0&&text_overlaps==0);rev_menu_palette_and_paper();
    width=400;objects=outside_rects=outside_lines=text_overlaps=0;texts.clear();pixels.assign(width*HEIGHT,0xff000000u);CHECK(param_lens_draw_top());
    CHECK(objects>0&&outside_rects==0&&outside_lines==0&&text_overlaps==0);rev_menu_palette_and_paper();
    param_lens_touch_down(300,185);param_lens_touch_release();CHECK(u.params[3].src==ModSource::EnvFollow);
    lens_quick_begin();param_lens_touch_down(295,129);param_lens_capture();
    width=320;objects=outside_rects=outside_lines=text_overlaps=0;texts.clear();pixels.assign(width*HEIGHT,0xff000000u);param_lens_draw_bottom();rev_menu_palette_and_paper();
    width=400;objects=outside_rects=outside_lines=text_overlaps=0;texts.clear();pixels.assign(width*HEIGHT,0xff000000u);CHECK(param_lens_draw_top());rev_menu_palette_and_paper();
    lens_frame(0,KEY_TOUCH,KEY_L);param_lens_touch_release();lens_frame(KEY_B,KEY_B);CHECK(!param_lens_is_open());
}
void dump_rev(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);std::ofstream manifest(directory/"manifest.tsv");
    manifest<<"frame\tparam\ttop_objects\tbottom_objects\tshared_objects\n";RevFixture f;auto& u=*f.rev;
    std::array<q15,512> input{};std::array<q15,1024> output{};for(auto& p:u.params)p.snap();
    for(int block=0;block<16;++block){for(int i=0;i<512;++i)input[i]=(q15)(std::sin((block*512+i)*0.031)*10000);u.process(input.data(),512);}
    auto save=[&](const std::string& name){
        f.engine->capture_ui_snapshot(g_ui_snapshot,0,0);reverb_focus_capture();
        rev_render(f,true);const int top=objects;dump_surface(directory,name+"_top");
        rev_render(f,false);const int bottom=objects;dump_surface(directory,name+"_bottom");
        manifest<<name<<'\t'<<reverb_focus_selected()<<'\t'<<top<<'\t'<<bottom<<'\t'<<top+bottom<<'\n';
    };
    const int xs[4]={50,210,210,50},ys[4]={40,40,198,198};
    for(int p=0;p<4;++p){f.tap(xs[p],ys[p]);u.params[p].base=(q15)(4000+p*6500);u.params[p].snap();save("param_"+std::to_string(p));}
    u.params[0].src=ModSource::SigilB;u.params[0].mod_depth=24576;u.params[0].base=12000;u.params[0].snap();
    f.engine->sigils[1].mode=SigilMode::Loop;f.engine->sigils[1].depth=24576;for(int i=0;i<SIGIL_POINTS;++i)f.engine->sigils[1].table[i]=(q15)(std::sin(i*6.283185307179586/SIGIL_POINTS)*24000);for(int i=0;i<8;++i)f.engine->render(output.data(),512);f.tap(50,40);save("size_mod");
    f.tap(150,10);for(auto& p:u.params)p.snap();u.process(input.data(),512);save("freeze");
    g_seq_lock_armed=true;g_seq_lock_step=7;CHECK(f.engine->set_step_lock(0,7,0,2,3000));f.tap(210,198);save("mix_lock");g_seq_lock_armed=false;
    CHECK(param_lens_touch_down(250,232));param_lens_touch_release();param_lens_capture();
    width=400;objects=outside_rects=outside_lines=text_overlaps=0;texts.clear();pixels.assign(width*HEIGHT,0xff000000u);CHECK(param_lens_draw_top());dump_surface(directory,"mod_top");const int mt=objects;
    width=320;objects=outside_rects=outside_lines=text_overlaps=0;texts.clear();pixels.assign(width*HEIGHT,0xff000000u);param_lens_draw_bottom();dump_surface(directory,"mod_bottom");manifest<<"mod\t2\t"<<mt<<'\t'<<objects<<'\t'<<mt+objects<<'\n';
    lens_quick_begin();param_lens_touch_down(260,129);param_lens_capture();
    width=400;objects=outside_rects=outside_lines=text_overlaps=0;texts.clear();pixels.assign(width*HEIGHT,0xff000000u);CHECK(param_lens_draw_top());dump_surface(directory,"temp_top");const int tt=objects;
    width=320;objects=outside_rects=outside_lines=text_overlaps=0;texts.clear();pixels.assign(width*HEIGHT,0xff000000u);param_lens_draw_bottom();dump_surface(directory,"temp_bottom");manifest<<"temp\t2\t"<<tt<<'\t'<<objects<<'\t'<<tt+objects<<'\n';
    lens_frame(0,KEY_TOUCH,KEY_L);param_lens_touch_release();param_lens_cancel();
}
