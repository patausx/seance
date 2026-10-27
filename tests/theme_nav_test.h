// Included by the actual-menu/engine raster harness. Themes are startup-only.
void test_nav_theme() {
    namespace p=seance::platform::ui3ds::palette;
    Fixture f;
    const auto initial=p::settings();
    for(auto theme:{p::Theme::Pink,p::Theme::Graphite,p::Theme::Grimoire,p::Theme::BlackMass,p::Theme::Custom}) {
        p::set(theme);nav_open();nav_gpu_frame();nav_capture();
        const auto before=p::settings();const auto revision=p::revision();
        const auto image=render(320,nav_draw);
        CHECK(!text_has(image,"y theme")&&!text_has(image,"sd error / y retry"));
        for(int i=0;i<3;++i) {
            CHECK(clock_key(KEY_Y)==NavChoice::None&&nav_is_open());
            NavChoice out;nav_touch_down(40,64,out);CHECK(out==NavChoice::None); // button still owns simultaneous touch
            clock_key(0);
            for(int x:{248,280,319})for(int y:{218,226,239}) {
                nav_touch_down(x,y,out);CHECK(out==NavChoice::None&&nav_is_open());
                nav_touch_hold(x,y);nav_touch_release();
            }
        }
        nav_capture();CHECK(render(320,nav_draw).pixels==image.pixels);
        CHECK(p::settings()==before&&p::revision()==revision&&mutations==0);
        nav_close();
    }
    g_eng=nullptr;g_audio=nullptr;nav_open();
    CHECK(clock_key(KEY_Y)==NavChoice::None&&nav_is_open());
    nav_close();g_eng=f.engine.get();g_audio=&f.audio;p::set(initial);
    std::puts("pass startup-only themes: menu Y/former footer inert in all five themes; no palette/revision/project change; button ownership retained");
}
