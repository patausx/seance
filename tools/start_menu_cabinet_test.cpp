// Current cabinet controller + actual software raster, with no inherited suites.
#include "start_menu_test_support.h"
#include "../platform/3ds/menu_style.h"
#include "../platform/3ds/start_menu_motion.h"

static int frames_checked=0, diagnostic_ascii=-1;
static unsigned diagnostic_ink_ms=0;
static StartMenuData cabinet_fixture(int count=128) {
    StartMenuData d;
    d.theme.id=theme_test::gray()?theme_config::Id::Graphite:theme_config::Id::Pink;
    for(int i=0;i<count;++i) {
        auto& p=d.projects[i];p.exists=p.valid=true;
        std::snprintf(p.name,sizeof p.name,"patch %03d",i+1);
        std::snprintf(p.age,sizeof p.age,"%d min",i+1);
    }
    d.recent_count=count<5?count:5;
    for(int i=0;i<d.recent_count;++i)d.recent_slots[i]=i;
    return d;
}
static void capture_frame(const char* name=nullptr) {
    top.reset(400);bottom.reset(320);surface=&top;start_menu_draw_top();surface=&bottom;start_menu_draw_bottom();
    peak=std::max(peak,top.objects+bottom.objects);
    if(top.objects+bottom.objects>8192)
        std::fprintf(stderr,"pool overflow: frame%d upper%d lower%d sum%d ink-ms%u ascii%d\n",frames_checked,top.objects,bottom.objects,top.objects+bottom.objects,diagnostic_ink_ms,diagnostic_ascii);
    require(top.objects+bottom.objects<=8192,"both displays within shared primitive pool");
    require(!top.fractional&&!bottom.fractional,"native integer coverage");
    for(const Image* image:{&top,&bottom})for(const auto& t:image->texts)
        require(t.x>=0&&t.y>=0&&t.x+t.w<=image->width&&t.y+t.h<=240,"all native text bounds");
    ++frames_checked;
    if(name&&!dump_dir.empty()) {ppm(top,dump_dir+"/"+name+"_top.ppm");ppm(bottom,dump_dir+"/"+name+"_bottom.ppm");}
}
static StartMenuResult quick_tap(int x,int y) {
    none(start_menu_input(KEY_TOUCH,KEY_TOUCH,x,y));
    const auto r=start_menu_input(0,0,0,0);neutral();return r;
}
static void settle() { start_menu_tick(1000);neutral(); }
static void show_library(const StartMenuData& d) {open(d);none(tap(60,116));}
static void show_settings(const StartMenuData& d) {open(d);none(tap(60,180));}
static void show_palette(const StartMenuData& d) {show_settings(d);none(tap(210,132));}

static void test_cabinet_routes() {
    const auto d=cabinet_fixture();open(d);capture_frame();
    const auto hidden=region(bottom,144,0,176,240);
    surface=&bottom;bottom.reset(320);menu_style::paper(320);
    require(hidden==region(bottom,144,0,176,240),"closed right column contains only current paper");
    none(tap(180,44)); // no invisible recent load before activation
    for(int i=0;i<5;++i) {show_library(d);action(tap(200,44+36*i),StartMenuAction::LoadProject,i);}
    show_library(d);none(tap(200,224));capture_frame();
    require(has("all patches")&&has("patch 001")&&has("patch 006"),"full library starts at first six saved records");
    for(int page=0;page<22;++page) {
        capture_frame();
        for(int row=0;row<6&&page*6+row<128;++row) {
            action(tap(90,74+22*row),StartMenuAction::LoadProject,page*6+row);
        }
        if(page<21)none(key(KEY_R));
    }
    none(key(KEY_R));capture_frame();require(has("patch 128"),"last page clamp includes slot128");
    none(key(KEY_B));capture_frame();require(has("all patches >"),"browser back restores open library");
    none(key(KEY_B));capture_frame();require(!has("all patches >"),"B closes right column before exit");
    action(key(KEY_B),StartMenuAction::Exit);
    open(d);none(key(KEY_RIGHT));action(key(KEY_A),StartMenuAction::LoadProject,0);
    open(d);none(key(KEY_DOWN));none(key(KEY_A));none(key(KEY_RIGHT));
    action(key(KEY_A),StartMenuAction::PreferencesChanged);require(!start_menu_data().menu_sound,"keyboard settings column toggles sound");
    for(int i=0;i<2;++i) {
        show_settings(d);action(tap(200,42+44*i),StartMenuAction::PreferencesChanged);
        require(start_menu_data().menu_sound==(i!=0)&&start_menu_data().show_menu==(i!=1),"each current toggle changes only itself");
    }
    show_settings(d);none(tap(200,176));capture_frame();
    require(!has("save")&&start_menu_data().menu_sound&&start_menu_data().show_menu,"retired fourth row has no hidden target");
    const auto blank=region(bottom,148,158,162,82);
    surface=&bottom;bottom.reset(320);menu_style::paper(320);
    require(blank==region(bottom,148,158,162,82),"three settings leave no stale fourth-row ink");
    show_settings(d);none(key(KEY_RIGHT));none(key(KEY_DOWN));
    action(key(KEY_A),StartMenuAction::PreferencesChanged);
    require(!start_menu_data().show_menu&&start_menu_data().menu_sound,"second keyboard row is startup, not logo");
    none(key(KEY_DOWN));none(key(KEY_A));capture_frame();require(has("save"),"third keyboard row is palette");
    none(key(KEY_B));settle();none(key(KEY_DOWN));action(key(KEY_A),StartMenuAction::PreferencesChanged);
    require(!start_menu_data().menu_sound,"keyboard wraps after exactly three settings");
    none(key(KEY_UP));none(key(KEY_A));capture_frame();require(has("save"),"reverse keyboard wrap returns directly to palette");
    open(d);action(tap(64,49),StartMenuAction::NewProject);
    pass("cabinet routes / 5 recent + 128 full records + 3 settings + keyboard columns + back");
}
static void test_cabinet_ownership() {
    auto d=cabinet_fixture();open(d);
    none(start_menu_input(KEY_TOUCH,KEY_TOUCH,60,116));none(start_menu_input(0,KEY_TOUCH,136,116));
    none(start_menu_input(0,KEY_TOUCH,60,116));none(start_menu_input(0,0,0,0));neutral();settle();
    capture_frame();require(!has("all patches >"),"out and back never commits");
    show_library(d);none(start_menu_input(KEY_TOUCH,KEY_TOUCH,200,44));
    none(start_menu_input(KEY_R,KEY_R|KEY_TOUCH,200,44));none(start_menu_input(0,KEY_TOUCH,200,44));
    none(start_menu_input(0,0,0,0));neutral();
    none(start_menu_input(KEY_TOUCH,KEY_TOUCH,200,44));start_menu_set_data(d);
    none(start_menu_input(0,0,0,0));neutral();
    none(start_menu_input(KEY_TOUCH,KEY_TOUCH,200,44));start_menu_error("load failed");
    none(start_menu_input(0,0,0,0));neutral();capture_frame();require(has("load failed"),"refresh/error quarantine lift");
    start_menu_open(d);none(start_menu_input(KEY_A,KEY_A,0,0));none(start_menu_input(0,0,0,0));
    capture_frame();require(!has("all patches >"),"held-entry button cannot activate library");
    open(d);none(quick_tap(60,116));none(quick_tap(60,180));none(quick_tap(60,116));settle();
    action(tap(200,44),StartMenuAction::LoadProject,0);
    show_library(d);none(quick_tap(60,180));
    none(quick_tap(200,44));require(start_menu_data().menu_sound,"outgoing project text cannot toggle hidden sound");
    start_menu_tick(160);none(quick_tap(200,44));require(start_menu_data().menu_sound,"partial settings reveal is not a touch target");
    settle();action(tap(200,44),StartMenuAction::PreferencesChanged);
    show_palette(d);none(start_menu_input(KEY_B,KEY_B,0,0));
    none(start_menu_input(KEY_TOUCH,KEY_TOUCH,60,49));start_menu_tick(200);
    none(start_menu_input(0,KEY_TOUCH,60,49));none(start_menu_input(0,0,0,0));neutral();
    require(start_menu_data().theme==d.theme,"modal closing owns whole crossing touch");
    action(tap(60,49),StartMenuAction::NewProject);
    pass("ownership / cancel out-back + ignored key + held entry + data/error + rapid panel retarget + modal no-through");
}
static void test_cabinet_palette() {
    const auto d=cabinet_fixture();
    show_palette(d);const auto before=start_menu_data().theme;
    none(tap(68,63));require(start_menu_data().theme==before,"preset is draft-only");
    none(key(KEY_B));settle();require(start_menu_data().theme==before,"B discards draft");
    for(int id=0;id<5;++id) {
        show_palette(d);none(tap(80+(id%2)*142,64+(id/2)*21));
        const auto r=tap(280,212);
        action(r,id==int(d.theme.id)?StartMenuAction::None:StartMenuAction::PreferencesChanged);
        require(start_menu_data().theme.id==theme_config::Id(id),"saved preset id");settle();
    }
    for(int role=0;role<3;++role)for(int swatch=0;swatch<24;++swatch) {
        show_palette(d);none(tap(60+role*92,133));none(tap(38+(swatch%8)*34,156+(swatch/8)*17));
        capture_frame();action(tap(280,212),StartMenuAction::PreferencesChanged);settle();
        const auto s=start_menu_data().theme;const u32 c=role==0?s.custom.paper:role==1?s.custom.ink:s.custom.accent;
        require(s.id==theme_config::Id::Custom&&c==theme_config::swatches[swatch],"all 72 role/swatch combinations");
    }
    show_palette(d);none(tap(68,63));start_menu_set_data(d);settle();
    require(start_menu_data().theme==d.theme,"failed save data refresh restores committed settings");
    capture_frame();require(!has("save"),"refresh dismisses palette modal");
    show_palette(d);none(tap(theme_test::gray()?68:220,63));
    action(quick_tap(280,212),StartMenuAction::PreferencesChanged);
    start_menu_set_data(d);start_menu_error("settings not saved");
    none(start_menu_input(KEY_TOUCH,KEY_TOUCH,60,49));settle();
    none(start_menu_input(0,0,0,0));neutral();capture_frame();
    require(start_menu_data().theme==d.theme&&has("settings not saved"),"actual save-then-main-rollback closing path");
    pass("palette / draft-save-cancel + five presets + all72 role/swatches + rollback refresh");
}
static void test_cabinet_draws() {
    auto d=cabinet_fixture();
    for(int ch=32;ch<=126;++ch) {
        for(int i=0;i<5;++i) {std::memset(d.projects[i].name,ch,23);d.projects[i].name[23]=0;}
        show_library(d);capture_frame();
        if(ch!=32) {
            bool visible=false;
            for(int y=28;y<57;++y)for(int x=168;x<304;++x)
                if(bottom.pixels[y*320+x]!=expected_paper(y))visible=true;
            require(visible,"every printable non-space title must have visible ink");
            for(int i=0;i<5;++i) {d.projects[i].name[0]=char(ch);d.projects[i].name[1]=0;}
            show_library(d);capture_frame();visible=false;
            for(int y=28;y<57;++y)for(int x=168;x<200;++x)
                if(bottom.pixels[y*320+x]!=expected_paper(y))visible=true;
            require(visible,"single literal glyph visible without ellipsis masking missing ink");
        }
    }
    // Saturated/unknown metadata and sparse catalogue keep every saved slot reachable.
    d=cabinet_fixture(0);d.projects[127].exists=true;d.projects[127].valid=true;
    std::strcpy(d.projects[127].name,"only last");d.recent_count=5;
    d.recent_slots[0]=127;d.recent_slots[1]=127;d.recent_slots[2]=-1;d.recent_slots[3]=128;d.recent_slots[4]=9000;
    show_library(d);require(start_menu_data().recent_count==1,"duplicate/invalid recent filtering");
    none(tap(200,224));capture_frame();require(has("only last")&&!has("empty"),"sparse browser has no127 empty rows");
    action(tap(90,74),StartMenuAction::LoadProject,127);
    d.projects[127].valid=false;open(d);none(tap(60,116));none(tap(200,224));none(tap(90,74));capture_frame();
    require(has("check file - cannot load"),"corrupt record visible but never loaded");
    show_library(cabinet_fixture(0));capture_frame();require(has("no recent patches")&&has("all patches >"),"empty library keeps full-library route");
    none(tap(200,224));capture_frame();require(has("no saved patches"),"empty browser has back");none(key(KEY_B));
    d=cabinet_fixture();open(d);
    for(int frame=0;frame<60;++frame) {
        if(frame%10==0)none(quick_tap(60,frame%20==0?116:180));
        start_menu_tick(16);capture_frame();const auto a=bottom.pixels,b=top.pixels;
        capture_frame();require(a==bottom.pixels&&b==top.pixels,"both draw calls are time/read-only");
    }
    start_menu_tick(UINT32_MAX);capture_frame();
    for(int i=0;i<5;++i) {std::memset(d.projects[i].name,'W',23);d.projects[i].name[23]=0;}
    show_library(d);none(quick_tap(60,180));
    for(int i=0;i<26;++i){start_menu_tick(16);capture_frame();}
    none(quick_tap(60,116));for(int i=0;i<26;++i){start_menu_tick(16);capture_frame();}
    d=cabinet_fixture();d.recent_count=2147483647;d.theme.id=theme_config::Id(255);
    std::memset(d.projects[0].name,'x',sizeof d.projects[0].name);
    std::memset(d.projects[0].age,'x',sizeof d.projects[0].age);
    show_library(d);capture_frame();
    require(start_menu_data().recent_count==5&&start_menu_data().projects[0].name[23]==0&&start_menu_data().projects[0].age[15]==0,"malformed DTO bounded strings/count");
    start_menu_set_data(start_menu_data());settle();capture_frame();
    require(start_menu_data().recent_count==5,"aliased refresh preserves recent snapshot");
    d.recent_count=-100;open(d);require(start_menu_data().recent_count==0,"negative recent count filtered");
    d=cabinet_fixture(7);show_library(d);none(tap(200,224));none(key(KEY_R));
    d.projects[6].exists=false;start_menu_set_data(d);settle();capture_frame();
    require(has("patch 001")&&!has("patch 007"),"removed last-page record normalizes sparse browser");
    pass("raster / 95 ASCII long-name fitting + empty/sparse/corrupt + 60 interrupted frames + detached draw repeat");
}
static void test_cabinet_motion() {
    using seance::platform::ui3ds::cabinet_motion::Reveal;
    Reveal r;require(r.sample(0,200,50)==0,"starts invisible");r.retarget(0);
    r.tick(160);int lo=255,hi=0,partial=0;
    for(int y=0;y<240;y+=7)for(int x=144;x<304;x+=7){const int a=r.sample(0,x,y);lo=std::min(lo,a);hi=std::max(hi,a);partial+=a>0&&a<255;}
    require(hi>lo+15&&lo>=0&&hi<=255&&partial>0,"mid-reveal has bounded mixed opacity; authored ranks may include hidden and opaque cells");
    auto copy=r; r.retarget(1);
    require(!std::memcmp(r.alpha,copy.alpha,sizeof r.alpha),"retarget retains exact visible field");
    r.tick(705);require(r.sample(0,180,90)==0&&r.sample(1,180,90)==255&&r.ready(),"authored705ms switch ends opaque, outgoing absent");
    r.retarget(-1);r.tick(UINT32_MAX);require(r.sample(1,180,90)==0,"huge delta cannot overflow");
    // Startup's modal freeze uses production material capture, not a new seed.
    auto d=cabinet_fixture();open(d);start_menu_capture(0);start_menu_capture(300);start_menu_capture(900);
    const u32 a=menu_style::patch_sample(false,73,122);
    show_palette(d);start_menu_capture(1000);start_menu_capture(90000);
    require(menu_style::patch_sample(false,73,122)==a,"palette freezes preceding paper phase");
    none(key(KEY_B));settle();start_menu_capture(90100);
    require(menu_style::patch_sample(false,73,122)==a,"no modal-time catch-up on return");
    pass("motion / field interpolation + nonuniform alpha + exact retarget + huge delta + frozen paper return");
}
static void capture_cabinet() {
    auto d=cabinet_fixture();open(d);capture_frame("01-home");
    none(quick_tap(60,116));start_menu_tick(160);capture_frame("02-smoke");settle();capture_frame("03-library");
    none(start_menu_input(KEY_TOUCH,KEY_TOUCH,200,44));capture_frame("04-selected");
    none(start_menu_input(0,KEY_TOUCH,140,44));none(start_menu_input(0,0,0,0));neutral();
    none(tap(200,224));capture_frame("05-browser");
    for(int i=0;i<21;++i)none(key(KEY_R));
    capture_frame("06-last-page");
    show_settings(d);capture_frame("07-settings");none(tap(200,132));capture_frame("08-palette");
    show_library(cabinet_fixture(0));capture_frame("09-empty");
    for(int i=0;i<5;++i)std::strcpy(d.projects[i].name,"a very long patch title");
    show_library(d);capture_frame("10-long-names");
}
#include "start_menu_ink_test.h"
#include "start_menu_wordmark_test.h"

int main(int argc,char** argv) {
    std::setvbuf(stdout,nullptr,_IONBF,0);
    theme_test::select();
    if(argc==3&&std::string(argv[1])=="--capture") {
        dump_dir=argv[2];std::filesystem::create_directories(dump_dir);capture_cabinet();
    } else if(argc==3&&std::string(argv[1])=="--ink-capture") {
        dump_dir=argv[2];std::filesystem::create_directories(dump_dir);capture_start_menu_ink();
    } else if(argc==2&&std::string(argv[1])=="--ink") {
        test_start_menu_ink();
    } else {
        test_cabinet_routes();test_cabinet_ownership();test_cabinet_palette();test_cabinet_draws();
        test_startup_wordmark();test_startup_reveal();test_cabinet_motion();
        require(suites==7,"exactly seven current startup groups, no inherited suite");
    }
    std::printf("cabinet: frames%d peak%d/8192 groups%d theme%s\n",frames_checked,peak,suites,theme_test::gray()?"graphite":"pink");
}
