// Included after nav_clock_test.h in real menu harness; no copied runtime UI.
namespace {
int clock_mod_ui_peak=0;
void mod_open(Fixture& f,int field=0) {
    clock_values(f);clock_key(KEY_X);if(field)clock_key(KEY_X);clock_key(KEY_Y);clock_key(0);nav_capture();
    CHECK(nav_is_open()&&nav_clock_top_active());
}
void mod_tap(int x,int y) { clock_key(0);NavChoice o;nav_touch_down(x,y,o);CHECK(o==NavChoice::None);nav_touch_release();nav_capture(); }
void mod_render_audio(Engine& e,int frames=32000) {
    q15 pcm[512];while(frames){int n=std::min(frames,256);e.render(pcm,n);frames-=n;}
}
std::pair<Frame,Frame> mod_frames(bool capture=true) {
    if(capture)nav_capture();
    auto t=render(400,nav_clock_draw_top);auto b=render(320,nav_draw);
    raw_text_audit(t);raw_text_audit(b);palette(t);palette(b);paper_edges(t);paper_edges(b);
    CHECK(t.objects+b.objects<8192);clock_mod_ui_peak=std::max(clock_mod_ui_peak,t.objects+b.objects);
    return {t,b};
}
void mod_ui_keyboard() {
    Fixture f;mod_open(f);CHECK(text_has(mod_frames().second,"sigil"));
    clock_key(KEY_X);clock_key(KEY_DRIGHT);CHECK(f.engine->clock_mod(ClockTarget::Tempo).src==ModSource::SigilA&&mutations==1);
    clock_key(KEY_X);clock_key(KEY_DUP);clock_key(KEY_DRIGHT);CHECK(f.engine->clock_mod(ClockTarget::Tempo).depth==6&&mutations==3);
    clock_key(KEY_B);CHECK(nav_is_open());auto b=clock_frame();CHECK(text_has(b,"a")&&!text_has(b,"sigil"));
    clock_key(0);NavChoice out;nav_touch_down(303,12,out);nav_capture();CHECK(text_has(clock_frame(),"sigil"));
    clock_key(KEY_B);clock_key(KEY_B);CHECK(nav_is_open());clock_key(KEY_B);CHECK(!nav_is_open());
    mod_open(f,1);clock_key(KEY_A);for(int i=0;i<4;++i)clock_key(KEY_RIGHT);
    CHECK(f.engine->clock_mod(ClockTarget::Swing).src==ModSource::SigilD);
    clock_key(KEY_A);for(int i=0;i<10;++i)clock_key(KEY_DOWN);
    CHECK(f.engine->clock_mod(ClockTarget::Swing).depth==-25);int m=mutations;clock_key(KEY_DOWN);CHECK(mutations==m);
    clock_key(KEY_SELECT);CHECK(!nav_is_open());
}
void mod_ui_touch() {
    Fixture f;mod_open(f);
    mod_tap(158,143);CHECK(f.engine->clock_mod(ClockTarget::Tempo).src==ModSource::SigilA&&mutations==1);
    clock_key(0);NavChoice out;nav_touch_down(250,178,out);nav_touch_hold(274,178);
    CHECK(f.engine->clock_mod(ClockTarget::Tempo).depth==0&&mutations==1);
    CHECK(text_has(mod_frames().second,"+12 bpm"));nav_touch_release();CHECK(mutations==2&&f.engine->clock_mod(ClockTarget::Tempo).depth==12);
    mod_tap(158,143);CHECK(mutations==2);
    clock_key(0);nav_touch_down(220,90,out);nav_touch_hold(240,90);CHECK(f.engine->bpm==120);nav_touch_release();CHECK(f.engine->bpm==130&&mutations==3);
    mod_tap(280,230);CHECK(!text_has(clock_frame(),"sigil"));
    mod_tap(231,230);CHECK(text_has(clock_frame(),"sigil"));
    clock_key(0);nav_touch_down(204,143,out);nav_touch_hold(270,143);nav_touch_hold(204,143);nav_touch_release();
    CHECK(f.engine->clock_mod(ClockTarget::Tempo).src==ModSource::SigilA&&mutations==3);
    mod_tap(110,143);CHECK(f.engine->clock_mod(ClockTarget::Tempo).src==ModSource::None&&f.engine->clock_mod(ClockTarget::Tempo).depth==12);
}
void mod_ui_ownership() {
    Fixture f;NavChoice out;
    for(int action=0;action<7;++action){
        mod_open(f);nav_touch_down(220,178,out);nav_touch_hold(244,178);
        auto other=std::make_unique<Engine>();platform::AudioOut other_audio;
        if(action==0)clock_key(KEY_B|KEY_RIGHT);
        if(action==1)clock_key(KEY_SELECT|KEY_TOUCH);
        if(action==2){nav_touch_hold(244,202);nav_touch_hold(244,178);}
        if(action==3)f.engine->set_clock_mod(ClockTarget::Swing,ModSource::SigilB,3);
        if(action==4)g_eng=other.get();
        if(action==5)g_audio=&other_audio;
        if(action==6){nav_close();nav_open();}
        nav_capture();nav_touch_release();g_eng=f.engine.get();g_audio=&f.audio;
        CHECK(f.engine->clock_mod(ClockTarget::Tempo).depth==0&&mutations==0);
    }
    mod_open(f);f.engine->set_clock_mod(ClockTarget::Tempo,ModSource::SigilA,12);
    f.engine->sigils[0].drawn=true;f.engine->sigils[0].depth=32767;for(auto& v:f.engine->sigils[0].table)v=20000;
    nav_touch_down(220,90,out);nav_touch_hold(240,90);mod_render_audio(*f.engine);nav_capture();nav_touch_release();
    CHECK(f.engine->bpm==130&&mutations==1); // moving applied values do not invalidate base draft
}
void mod_ui_frozen_and_limits() {
    Fixture f;
    for(int field=0;field<2;++field)for(int source=0;source<5;++source)for(int extreme=0;extreme<2;++extreme){
        mod_open(f,field);const auto t=field?ClockTarget::Swing:ClockTarget::Tempo;
        f.engine->set_clock_mod(t,(ModSource)source,field?(extreme?25:-25):(extreme?280:-280));
        f.engine->set_bpm(extreme?300:20);f.engine->set_swing(extreme?75:50);
        for(auto& s:f.engine->sigils){s.drawn=true;s.depth=32767;for(auto& v:s.table)v=extreme?32767:-32767;}
        mod_render_audio(*f.engine);auto image=mod_frames();
        auto* e=g_eng;auto* a=g_audio;g_eng=nullptr;g_audio=nullptr;auto frozen=mod_frames(false);
        CHECK(image.first.pixels==frozen.first.pixels&&image.second.pixels==frozen.second.pixels);g_eng=e;g_audio=a;
    }
}
std::vector<char> file_bytes(const std::filesystem::path& p){std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
void put_bytes(const std::filesystem::path& p,const std::vector<char>& b){std::ofstream out(p,std::ios::binary);out.write(b.data(),b.size());CHECK(bool(out));}
size_t find_chunk(const std::vector<char>& b,const char* id){
    for(size_t p=5;p+8<=b.size();){
        if(!std::memcmp(&b[p],id,4))return p;
        uint32_t len=0;for(int i=0;i<4;++i)len|=uint32_t(uint8_t(b[p+4+i]))<<(8*i);
        p+=8+len;
    }return b.size();
}
void mod_persistence_format() {
    Fixture f;auto loaded=std::make_unique<Engine>();
    auto dir=std::filesystem::temp_directory_path()/"seance-clock-mod-format";std::filesystem::create_directories(dir);auto p=dir/"test.snc";
    const auto before=clock_patch(*f.engine,p);CHECK(find_chunk(before,"CLCK")==before.size());
    f.engine->set_clock_mod(ClockTarget::Tempo,ModSource::SigilC,-280);f.engine->set_clock_mod(ClockTarget::Swing,ModSource::SigilD,25);
    f.engine->set_bpm(123);f.engine->set_swing(57);
    const auto saved=clock_patch(*f.engine,p);size_t at=find_chunk(saved,"CLCK");CHECK(at+16<=saved.size());
    CHECK(uint8_t(saved[at+4])==8&&saved[at+8]==1&&saved[at+9]==2);
    CHECK(patchio::load(*loaded,p.c_str()));CHECK(loaded->bpm==123&&loaded->swing_percent==57);
    CHECK(loaded->clock_mod(ClockTarget::Tempo).src==ModSource::SigilC&&loaded->clock_mod(ClockTarget::Tempo).depth==-280);
    CHECK(loaded->clock_mod(ClockTarget::Swing).src==ModSource::SigilD&&loaded->clock_mod(ClockTarget::Swing).depth==25);
    CHECK(clock_patch(*loaded,dir/"round.snc")==saved);
    // Captured document owns route scalars, not alias/live state.
    patchio::PatchDocument doc;CHECK(patchio::capture(*f.engine,doc));
    f.engine->set_clock_mod(ClockTarget::Tempo,ModSource::SigilA,12);mod_render_audio(*f.engine);
    CHECK(patchio::save(doc,(dir/"frozen.snc").c_str()));CHECK(file_bytes(dir/"frozen.snc")==saved);
    // CLCK may precede ROUT/TRNS: parsing is deferred, no order-dependent base.
    std::vector<char> reordered(saved.begin(),saved.begin()+5);reordered.insert(reordered.end(),saved.begin()+at,saved.begin()+at+16);
    reordered.insert(reordered.end(),saved.begin()+5,saved.begin()+at);reordered.insert(reordered.end(),saved.begin()+at+16,saved.end());
    put_bytes(p,reordered);CHECK(patchio::load(*loaded,p.c_str()));CHECK(loaded->bpm==123&&loaded->swing_percent==57);
    // Missing optional chunk overrides previous active routes with None/0.
    put_bytes(p,before);CHECK(patchio::load(*loaded,p.c_str()));CHECK(loaded->clock_mod(ClockTarget::Tempo).src==ModSource::None&&loaded->clock_mod(ClockTarget::Swing).depth==0);
    CHECK(clock_patch(*loaded,p)==before);
    // Every malformed payload/duplicate is transactionally rejected.
    std::vector<std::vector<char>> bad;
    for(int offset:{4,8,9,10,13})for(int value:{5,255}){auto b=saved;b[at+offset]=char(value);bad.push_back(std::move(b));}
    for(int off:{11,14})for(int16_t value:{int16_t(-32768),int16_t(32767)}){auto b=saved;b[at+off]=char(value&255);b[at+off+1]=char(uint16_t(value)>>8);bad.push_back(std::move(b));}
    auto dup=saved;dup.insert(dup.end(),saved.begin()+at,saved.begin()+at+16);bad.push_back(dup);
    for(size_t cut=0;cut<16;++cut){auto b=saved;b.resize(at+cut);bad.push_back(std::move(b));}
    const auto stable=clock_patch(*loaded,dir/"stable.snc");
    for(const auto& b:bad){put_bytes(p,b);CHECK(!patchio::load(*loaded,p.c_str()));CHECK(clock_patch(*loaded,dir/"stable.snc")==stable);}
    // None retains signed depth and survives save/load for convenient reassigning.
    loaded->set_clock_mod(ClockTarget::Tempo,ModSource::None,-12);CHECK(patchio::save(*loaded,p.c_str()));
    CHECK(patchio::load(*f.engine,p.c_str()));CHECK(f.engine->clock_mod(ClockTarget::Tempo).depth==-12);
    std::filesystem::remove_all(dir);
}
void test_clock_mod_ui(){
    mod_ui_keyboard();std::puts("pass clock-mod UI keyboard / route badges / return");
    mod_ui_touch();std::puts("pass clock-mod UI source/depth/base drafts / no-op / stylus entry");
    mod_ui_ownership();std::puts("pass clock-mod UI cancellation / persistent guard / live-safe draft");
    mod_ui_frozen_and_limits();std::puts("pass clock-mod UI 20 extrema / frozen dual / raw text / palette / budget");
    mod_persistence_format();std::puts("pass clock-mod optional CLCK / migration / frozen save / corruption rollback");
    std::printf("pass clock-mod-ui 5/5 peak=%d/8192\n",clock_mod_ui_peak);
}
void dump_clock_mod_ui(const std::filesystem::path& dir){
    std::filesystem::create_directories(dir);std::ofstream report(dir/"frames.tsv"),raw(dir/"raw_text.tsv");
    report<<"name\ttop_objects\tbottom_objects\tout_of_bounds\n";raw<<"name\tscreen\tx\ty\tw\th\ttext\n";
    Fixture f;mod_open(f);for(auto& s:f.engine->sigils){s.drawn=true;s.depth=32767;for(auto& v:s.table)v=16384;}
    auto pair=[&](const char* name){auto images=mod_frames();const Frame* frames[]={&images.first,&images.second};
        for(int i=0;i<2;++i){std::string screen=i?"bottom":"top";ppm(dir/(std::string(name)+"_"+screen+".ppm"),*frames[i]);
            for(auto& t:frames[i]->raw_texts)raw<<name<<'\t'<<screen<<'\t'<<t.x<<'\t'<<t.y<<'\t'<<t.w<<'\t'<<t.h<<'\t'<<t.text<<'\n';}
        report<<name<<'\t'<<images.first.objects<<'\t'<<images.second.objects<<'\t'<<images.first.outside+images.second.outside<<'\n';};
    pair("01_tempo_none");f.engine->set_clock_mod(ClockTarget::Tempo,ModSource::SigilA,12);mod_render_audio(*f.engine);pair("02_tempo_applied");
    clock_key(KEY_B);pair("03_header_tempo");clock_key(KEY_X);clock_key(KEY_Y);
    f.engine->set_clock_mod(ClockTarget::Swing,ModSource::SigilB,-6);mod_render_audio(*f.engine);pair("04_swing_applied");
    clock_key(KEY_B);pair("05_header_both");clock_key(KEY_Y);clock_key(0);NavChoice o;nav_touch_down(230,178,o);nav_touch_hold(246,178);pair("06_depth_draft");nav_touch_release();
    clock_key(KEY_B);clock_key(KEY_X);clock_key(KEY_Y);f.engine->set_bpm(299);f.engine->set_clock_mod(ClockTarget::Tempo,ModSource::SigilD,280);mod_render_audio(*f.engine);pair("07_limited");
    f.engine->set_bpm(20);f.engine->set_clock_mod(ClockTarget::Tempo,ModSource::SigilD,-280);mod_render_audio(*f.engine);pair("08_minimum");
}
}
