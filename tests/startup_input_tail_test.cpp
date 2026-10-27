// Actual current controller, linked to the existing native software raster stubs.
#include "start_menu_test_support.h"
#include "../platform/3ds/startup_input_tail.h"
#ifndef START_MENU_CONTROLLER
#define START_MENU_CONTROLLER "../platform/3ds/ui_start_menu.cpp"
#endif
#include START_MENU_CONTROLLER

static int cases=0;
static StartMenuData fixture() {
    StartMenuData d;
    d.theme.id=theme_test::gray()?theme_config::Id::Graphite:theme_config::Id::Pink;
    d.projects[0].exists=d.projects[0].valid=true;
    std::strcpy(d.projects[0].name,"first patch");
    d.recent_count=1;d.recent_slots[0]=0;
    return d;
}
static void entered(u32 captured) {
    start_menu_open(fixture());
    none(start_menu_input(captured,captured,60,49));
    start_menu_tick(1000);
    require(g.selected==Library&&g.panel==Panel::None,"first sample swallowed without activation");
}
static StartMenuResult touch_with(u32 sticky,int x=60,int y=49) {
    none(start_menu_input(KEY_TOUCH,sticky|KEY_TOUCH,x,y));
    return start_menu_input(0,sticky,0,0);
}
static void test_repro() {
    entered(KEY_R);
    for(int i=0;i<8;++i)none(start_menu_input(0,KEY_R,0,0));
    none(start_menu_input(KEY_DUP,KEY_R|KEY_DUP,0,0));
    require(g.selected==New,"fresh dpad works while captured R never releases");
    none(start_menu_input(0,KEY_R,0,0));
    action(touch_with(KEY_R),StartMenuAction::NewProject);
    ++cases;
}
static void test_single_bits() {
    for(int bit=0;bit<32;++bit) {
        const u32 sticky=u32(1)<<bit;
        entered(sticky);
        for(int n=0;n<3;++n)none(start_menu_input(0,sticky,60,49));
        const u32 direction=sticky==KEY_DUP?KEY_DDOWN:KEY_DUP;
        none(start_menu_input(direction,sticky|direction,0,0));
        require(g.selected==(direction==KEY_DUP?New:Settings),"all32 bits: unrelated dpad works");
        none(start_menu_input(0,sticky,0,0));
        if(sticky==KEY_TOUCH) {
            action(start_menu_input(KEY_A,sticky|KEY_A,0,0),StartMenuAction::NewProject);
        } else {
            action(touch_with(sticky),StartMenuAction::NewProject);
        }
        ++cases;
    }
    for(int bit=0;bit<32;++bit) {
        const u32 sticky=u32(1)<<bit;
        entered(sticky);none(start_menu_input(0,0,0,0));
        require(g.selected==Library&&g.panel==Panel::None,"individual release never replays activation");
        none(start_menu_input(KEY_DUP,KEY_DUP,0,0));none(start_menu_input(0,0,0,0));
        action(start_menu_input(KEY_A,KEY_A,0,0),StartMenuAction::NewProject);
        ++cases;
    }
    pass("actual controller: all32 captured keybits including reserved24..31; fresh dpad/touch/A; independent release");
}
static void test_multiheld() {
    const u32 masks[]={KEY_R|KEY_A|KEY_TOUCH,0xffffffffu,0xff000000u|KEY_A|KEY_R,
                       KEY_A|KEY_B|KEY_TOUCH|KEY_DUP|KEY_DDOWN|KEY_DLEFT|KEY_DRIGHT};
    for(u32 initial:masks) {
        entered(initial);
        none(start_menu_input(0,initial,60,49));
        u32 held=initial&~u32(KEY_DUP);
        none(start_menu_input(0,held,0,0));
        none(start_menu_input(KEY_DUP,held|KEY_DUP,0,0));
        require(g.selected==New,"released direction rearms while all other captured keys remain held");
        none(start_menu_input(0,held,0,0));
        held&=~u32(KEY_A);none(start_menu_input(0,held,0,0));
        action(start_menu_input(KEY_A,held|KEY_A,0,0),StartMenuAction::NewProject);
        ++cases;
    }
    // Opening a panel captures the transition's held A+R tail once, not forever.
    open(fixture());none(start_menu_input(KEY_A,KEY_A|KEY_R,0,0));
    none(start_menu_input(0,KEY_A|KEY_R,0,0));start_menu_tick(1000);
    none(start_menu_input(KEY_DDOWN,KEY_A|KEY_R|KEY_DDOWN,0,0));
    require(g.selected==Settings,"post-panel first sample consumed once; later dpad active");
    none(start_menu_input(0,KEY_A|KEY_R,0,0));
    action(touch_with(KEY_A|KEY_R),StartMenuAction::NewProject);++cases;
    // Refresh and failed save keep the same conservative held-entry safety.
    for(int error=0;error<2;++error) {
        open(fixture());none(start_menu_input(KEY_TOUCH,KEY_TOUCH,60,49));
        start_menu_set_data(fixture());if(error)start_menu_error("settings not saved");
        none(start_menu_input(KEY_A,KEY_A|KEY_TOUCH|KEY_R,60,49));
        none(start_menu_input(0,KEY_A|KEY_TOUCH|KEY_R,60,49));
        const int previous=g.selected;
        none(start_menu_input(KEY_DUP,KEY_A|KEY_TOUCH|KEY_R|KEY_DUP,0,0));
        require(g.selected!=previous,"refresh/error blocked touch+A cannot stall fresh dpad");
        none(start_menu_input(0,KEY_R,0,0));
        action(touch_with(KEY_R),StartMenuAction::NewProject);++cases;
    }
    pass("actual controller: multiheld/all32 masks, partial release, panel transition, refresh + failed-save safety");
}
static void test_healthy_ownership() {
    open(fixture());
    none(start_menu_input(KEY_TOUCH,KEY_TOUCH,60,49));
    none(start_menu_input(0,KEY_TOUCH,140,49));
    none(start_menu_input(0,KEY_TOUCH,60,49));none(start_menu_input(0,0,0,0));
    require(g.panel==Panel::None,"drag out/back cancels");++cases;
    open(fixture());none(start_menu_input(KEY_TOUCH,KEY_TOUCH,60,49));
    none(start_menu_input(KEY_R,KEY_R|KEY_TOUCH,60,49));
    none(start_menu_input(0,KEY_TOUCH,60,49));none(start_menu_input(0,0,0,0));
    require(g.panel==Panel::None,"fresh ignored R chord owns touch/lift");++cases;
    // Ignored/reserved keys after arming keep their old button ownership behavior.
    for(int bit=0;bit<32;++bit) {
        if(bit==20||bit==14||bit==0||bit==1||(bit>=4&&bit<=7)||(bit>=28))continue;
        const u32 keybit=u32(1)<<bit;
        open(fixture());none(start_menu_input(KEY_TOUCH,KEY_TOUCH,60,49));
        none(start_menu_input(keybit,keybit|KEY_TOUCH,60,49));
        none(start_menu_input(0,KEY_TOUCH,60,49));none(start_menu_input(0,0,0,0));
        require(g.panel==Panel::None,"fresh ignored/reserved chord still cancels touch");++cases;
    }
    open(fixture());none(start_menu_input(KEY_TOUCH,KEY_TOUCH,60,49));
    none(start_menu_input(u32(1)<<14,(u32(1)<<14)|KEY_TOUCH,60,49));
    action(start_menu_input(0,0,0,0),StartMenuAction::NewProject);++cases;
    // A touch held on entry never commits, including its eventual lift.
    entered(KEY_TOUCH|KEY_A);
    none(start_menu_input(0,KEY_TOUCH|KEY_A,60,49));
    none(start_menu_input(0,KEY_TOUCH,60,49));none(start_menu_input(0,0,0,0));
    require(g.panel==Panel::None,"held-entry A+touch lift cannot activate");
    action(touch_with(0),StartMenuAction::NewProject);++cases;
    // Closing modal owns cross-boundary touch; stuck R cannot deadlock after it.
    open(fixture());none(tap(60,180));none(tap(210,132));
    require(g.page==Page::Theme,"palette entered");
    none(start_menu_input(KEY_B,KEY_B|KEY_R,0,0));
    none(start_menu_input(KEY_TOUCH,KEY_TOUCH|KEY_R,60,49));
    start_menu_tick(200);none(start_menu_input(0,KEY_TOUCH|KEY_R,60,49));
    none(start_menu_input(0,KEY_R,0,0));
    require(g.page==Page::Home&&g.panel==Panel::Settings,"modal crossing touch does not pass through");
    action(touch_with(KEY_R),StartMenuAction::NewProject);++cases;
    pass("actual controller: lift-only commit, dragout cancel, fresh ignored chords, screenshot, held-entry + modal closing");
}
static void test_tail() {
    for(int bit=0;bit<32;++bit) {
        StartupInputTail tail;
        const u32 b=u32(1)<<bit,other=bit?KEY_A:KEY_R;
        tail.capture(b,0); // down-only also owns its release tail
        u32 down=b|other,held=b|other,up=0;
        tail.filter(down,held,up);require(down==other&&held==other&&!up,"tail masks captured down+held, preserves fresh other");
        down=0;held=other;up=b;tail.filter(down,held,up);
        require(!up&&held==other,"tail masks captured release up");
        down=b;held=b;up=other;tail.filter(down,held,up);
        require(down==b&&held==b&&up==other,"tail rearms only after individual release");++cases;
    }
    StartupInputTail tail;tail.capture(0,0xffffffffu);
    u32 down=0,held=0xfffffffeu,up=1;tail.filter(down,held,up);
    require(!down&&!held&&!up,"allbits captured, first individual release consumed");
    down=1;held=0xffffffffu;up=0;tail.filter(down,held,up);
    require(down==1&&held==1&&!up,"fresh A works while31 captured others remain held");++cases;
    pass("shared per-key app tail: all32 down/held/up suppression + independent rearm, no activation leaks");
}
int main(int argc,char** argv) {
    std::setvbuf(stdout,nullptr,_IONBF,0);theme_test::select();
    if(argc==2&&std::string(argv[1])=="--repro")test_repro();
    else {test_repro();test_single_bits();test_multiheld();test_healthy_ownership();test_tail();}
    std::printf("menu-input-tail: cases%d groups%d theme%s\n",cases,suites,theme_test::gray()?"graphite":"pink");
}
