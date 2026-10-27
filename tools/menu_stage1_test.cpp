// Focused host harness: REAL menu/chains/controller + portable engine.
// Hardware-only C2D raster shim derived from sequencer_test.cpp, not copied UI.
// Line coverage approximates Citro2D; no hardware/performance claim.
#include "theme_test_mode.h"
#include "../platform/3ds/ui_state.h"
#include "../platform/3ds/ui_theme.h"
#include "../platform/3ds/ui_nav.h"
#include "../platform/3ds/ui_picker.h"
#include "../platform/3ds/ui_chains.h"
#include "../platform/3ds/menu_style.h"
#include "../platform/3ds/menu_labels_data.h"
#include "../core/ui/draw.h"
#include "../core/ui/font.h"
#include "../core/engine/patch_io.h"
#include <cstring>
#include <iterator>
#include <citro2d.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
using namespace seance;
using namespace seance::platform::ui3ds;
namespace m = seance::platform::ui3ds::menu_style;
namespace {
int mutations = 0, active_slot = 0;
circlePosition circle;
constexpr int HEIGHT = 240;
int width = 320, objects = 0, outside_rects = 0, outside_lines = 0;
int maximum_shared_objects = 0, native_text_depth = 0;
std::vector<u32> pixels(320 * HEIGHT);
std::vector<int> text_owners(320 * HEIGHT);
std::vector<bool> text_collisions(320 * HEIGHT);
int next_text_id=0, current_text_id=0;
struct TextRecord { int x, y, w, h; std::string text; };
std::vector<TextRecord> texts, raw_texts;
void check(bool ok, const char* what, int line) {
    if (!ok) throw std::runtime_error(std::string(what) + " [line " + std::to_string(line) + "]");
}
#define CHECK(...) check((__VA_ARGS__), #__VA_ARGS__, __LINE__)
void record_text(int x, int y, const char* text, int scale) {
    int minx=4096,miny=4096,maxx=-1,maxy=-1,cx=x;
    for (const char* p=text; *p; ++p,cx+=6*scale) {
        if (*p=='\n') { cx=x-6*scale; y+=8*scale; continue; }
        for (int row=0;row<8;++row) for (int col=0;col<6;++col)
            if (trackr::ui::glyph_row(*p,row)&(1u<<(5-col))) {
                minx=std::min(minx,cx+col*scale); miny=std::min(miny,y+row*scale);
                maxx=std::max(maxx,cx+(col+1)*scale); maxy=std::max(maxy,y+(row+1)*scale);
            }
    }
    if(maxx>=0) {
        texts.push_back({minx,miny,maxx-minx,maxy-miny,text});
        raw_texts.push_back(texts.back());
    }
}
void pixel(int x,int y,u32 c) {
    if(x<0||y<0||x>=width||y>=HEIGHT) return;
    const int at=y*width+x;
    if(native_text_depth) {
        if(text_owners[at] && text_owners[at]!=current_text_id) text_collisions[at]=true;
        text_owners[at]=current_text_id;
    } else if((c>>24)==255) { text_owners[at]=0; text_collisions[at]=false; }
    u32& dst=pixels[at]; unsigned a=c>>24; u32 result=0xff000000;
    for(unsigned shift:{0u,8u,16u}) result|=((((c>>shift)&255)*a+((dst>>shift)&255)*(255-a)+127)/255)<<shift;
    dst=result;
}
}
namespace seance::platform::ui3ds {
Engine* g_eng=nullptr; platform::AudioOut* g_audio=nullptr; EngineUiSnapshot g_ui_snapshot;
View g_view=View::Chains; Overlay g_overlay=Overlay::None; Drag g_drag=Drag::None;
int g_edit_chain=0,g_edit_slot=0,g_sel_chain=0,g_sel_slot=0,g_sel_sigil=0,g_drag_param=0;
int g_last_px=-1,g_last_py=-1,g_patch_slot=0,g_save_flash=0;
bool g_has_dsp=false,g_mic_ok=false,g_seq_lock_armed=false; q15 g_mic_peak=0;
int g_seq_lock_chain=0,g_seq_lock_step=0,g_seq_lock_flash=0;
Unit* g_clip_unit=nullptr; u32 g_ui_frame=0;
void mark_project_mutated() { ++mutations; }
void mark_project_structural_mutated() { ++mutations; }
bool project_is_dirty() { return mutations!=0; }
int project_active_slot() { return active_slot; }
ModSource sigil_source(int s) { return sigil_mod_source(s); }
q15 norm_h(int x,int x0,int w,bool bipolar) {
    const int v=std::clamp(x-x0,0,w)*32767/w;
    return q15(bipolar ? v*2-32767 : v);
}
Unit* edited_unit() { return g_eng->chains[g_edit_chain].units[g_edit_slot]; }
// Unreachable editor/sample status UI: observe result, never replace engine work.
trackr::synth::SampleEditResult expected_sample_result = trackr::synth::SampleEditResult::Ok;
void module_focus_show_sample_result(trackr::synth::SampleEditResult result) {
    CHECK(result==expected_sample_result);
}
}
void real_text(int, int, const char*, uint32_t, int) asm("__real__ZN6seance2ui4textEiiPKcji");
extern "C" void __wrap__ZN6seance2ui4textEiiPKcji(int x, int y, const char* text, uint32_t color, int scale) {
    record_text(x, y, text, scale);
    current_text_id=++next_text_id;
    ++native_text_depth; real_text(x, y, text, color, scale); --native_text_depth;
}
void hidCircleRead(circlePosition* position) { *position = circle; }
bool C2D_DrawRectSolid(float x, float y, float z, float w, float h, u32 color) {
    CHECK(std::isfinite(x) && std::isfinite(y) && std::isfinite(z));
    CHECK(std::isfinite(w) && std::isfinite(h) && w >= 0 && h >= 0);
    CHECK(std::abs(x) < 4096 && std::abs(y) < 4096 && w < 4096 && h < 4096);
    ++objects;
    // Solid overlays hide covered source labels; they aren't visible collisions.
    if ((color >> 24) == 255 && w >= 200 && h >= 100)
        texts.erase(std::remove_if(texts.begin(), texts.end(), [&](const TextRecord& t) {
            return t.x >= x && t.y >= y && t.x + t.w <= x + w && t.y + t.h <= y + h;
        }), texts.end());
    if (x < 0 || y < 0 || x + w > width || y + h > HEIGHT) ++outside_rects;
    for (int yy = (int)std::ceil(y); yy < (int)std::ceil(y + h); ++yy)
        for (int xx = (int)std::ceil(x); xx < (int)std::ceil(x + w); ++xx) pixel(xx, yy, color);
    return true;
}
bool C2D_DrawLine(float x0, float y0, u32 c0, float x1, float y1, u32 c1, float thickness, float z) {
    CHECK(c0 == c1 && std::isfinite(z) && std::isfinite(thickness) && thickness > 0);
    CHECK(std::isfinite(x0) && std::isfinite(y0) && std::isfinite(x1) && std::isfinite(y1));
    CHECK(std::abs(x0) < 4096 && std::abs(y0) < 4096 && std::abs(x1) < 4096 && std::abs(y1) < 4096);
    ++objects;
    int x = (int)std::lround(x0), y = (int)std::lround(y0);
    const int ex = (int)std::lround(x1), ey = (int)std::lround(y1);
    const int dx = std::abs(ex - x), dy = -std::abs(ey - y), sx = x < ex ? 1 : -1, sy = y < ey ? 1 : -1;
    int error = dx + dy;
    const int line_width = std::max(1, (int)std::lround(thickness));
    if (std::min(x, ex) < 0 || std::min(y, ey) < 0 ||
        std::max(x, ex) + line_width > width || std::max(y, ey) + line_width > HEIGHT) ++outside_lines;
    for (;;) {
        for (int yy = 0; yy < line_width; ++yy) for (int xx = 0; xx < line_width; ++xx) pixel(x + xx, y + yy, c0);
        if (x == ex && y == ey) break;
        const int e2 = 2 * error;
        if (e2 >= dy) { error += dy; x += sx; }
        if (e2 <= dx) { error += dx; y += sy; }
    }
    return true;
}

namespace {
struct Frame { int width, objects, outside; std::vector<u32> pixels; std::vector<TextRecord> texts, raw_texts; };
Frame render(int w, const std::function<void()>& fn, bool strict=true) {
    width=w; pixels.assign(width*HEIGHT,0xff000000); texts.clear();raw_texts.clear();
    text_owners.assign(width*HEIGHT,0);text_collisions.assign(width*HEIGHT,false);next_text_id=0;
    objects=outside_rects=outside_lines=0; fn();
    CHECK(std::count(text_collisions.begin(),text_collisions.end(),true)==0);
    CHECK(objects>0 && objects<8192);
    if(strict) CHECK(outside_rects==0 && outside_lines==0);
    return {w,objects,outside_rects+outside_lines,pixels,texts,raw_texts};
}
u32 paper_color(int y) { return theme_test::paper(y); }
void palette(const Frame& f) {
    std::set<u32> allowed={m::ink(),m::spot(),m::ghost(),m::branch(),
                           theme_test::p::cell_dim(),theme_test::p::inverse(),theme_test::p::subdued()};
    for(int y=0;y<240;++y) allowed.insert(paper_color(y));
    for(u32 c:f.pixels) CHECK(allowed.count(c));
}
void paper_edges(const Frame& f) {
    for(int y=0;y<240;++y) for(int x:{0,1,f.width-2,f.width-1}) CHECK(f.pixels[y*f.width+x]==paper_color(y));
}
bool text_has(const Frame& f,const std::string& text) {
    for(const auto& t:f.texts) if(t.text==text) return true;
    return false;
}
struct Fixture {
    std::unique_ptr<Engine> engine=std::make_unique<Engine>(); platform::AudioOut audio;
    Fixture() {
        g_eng=engine.get(); g_audio=&audio; g_sel_chain=g_sel_slot=g_edit_chain=g_edit_slot=0;
        g_view=View::Chains; g_overlay=Overlay::None; g_drag=Drag::None; g_ui_frame=1; host_time_ms=0;
        g_patch_slot=0;active_slot=0;g_save_flash=0;mutations=0;circle={};nav_close();picker_cancel_input();
        chains_close_context_branch(); chains_view_touch_release(false); workspace_reset_all(); refresh();
    }
    ~Fixture() { picker_cancel_input(); chains_close_context_branch(); chains_view_touch_release(false);
        delete g_clip_unit; g_clip_unit=nullptr; g_eng=nullptr; g_audio=nullptr; }
    void refresh() { engine->capture_ui_snapshot(g_ui_snapshot); }
    void four() {
        engine->chains[0].units[0]=make_unit(UnitKind::Fm);
        static_cast<FmUnit*>(engine->chains[0].units[0])->drone=true;
        engine->chains[0].units[1]=make_unit(UnitKind::Stranular);
        engine->chains[0].units[2]=make_unit(UnitKind::Ghost);
        engine->chains[0].units[3]=make_unit(UnitKind::Reverb);
        const int xy[][2]={{16,12},{172,20},{80,86},{188,86},{16,140},{172,140}};
        for(int i=0;i<6;++i) workspace_set_point(0,i,xy[i][0],xy[i][1]);
        g_sel_slot=g_edit_slot=1; refresh();
    }
    void picker(int slot=0) { g_edit_slot=slot; picker_open(); g_overlay=Overlay::Picker; picker_buttons(0); }
};
void test_helper() {
    CHECK(m::ink()==theme_test::ink() && m::spot()==theme_test::spot());
    for(int w:{320,400}) {
        auto f=render(w,[&]{m::paper(w);});
        for(int y=0;y<240;++y) for(int x=0;x<w;++x) CHECK(f.pixels[y*w+x]==paper_color(y));
        render(w,[&]{m::paper(w); ui::rect(0,59,w,156,m::ink());m::paper(w,59,215);}); CHECK(pixels==f.pixels);
        render(w,[&]{m::paper(w,-80,500);}); CHECK(pixels==f.pixels);
    }
    for(const auto& l:menu_labels::labels) {
        auto f=render(320,[&]{m::paper(320);m::heading(10,30,l.text,m::ink());});
        CHECK(m::heading_width(l.text)==l.width);
        for(int y=0;y<l.height;++y) for(int x=0;x<l.width;++x) {
            const bool on=menu_labels::bitmap[l.offset+y*((l.width+7)/8)+x/8]&(0x80>>(x%8));
            CHECK(f.pixels[(30+y)*320+10+x]==(on?m::ink():paper_color(30+y)));
        }
    }
    for(int k=0;k<int(UnitKind::Count);++k) {
        CHECK(std::string(m::module_name(UnitKind(k)))!="?"); CHECK(m::heading_width(m::module_name(UnitKind(k)))>0);
        CHECK(ui::text_w(m::module_description(UnitKind(k)))<140);
    }
}
constexpr NavChoice CHOICES[]={NavChoice::Chains,NavChoice::Sigils,NavChoice::Patchbay,NavChoice::Sequencer,NavChoice::Performance,NavChoice::Bounce,NavChoice::Patch,NavChoice::Exit};
constexpr int NAV_CHOICE_COUNT=sizeof(CHOICES)/sizeof(CHOICES[0]);
void test_nav() {
    Fixture f; NavChoice out;
    for(int i=0;i<NAV_CHOICE_COUNT;++i) {
        nav_open(); nav_buttons(0,out); nav_touch_down(80,64+i*20,out); CHECK(out==CHOICES[i]&&!nav_is_open());
        nav_open(); nav_buttons(0,out); nav_capture(); auto image=render(320,nav_draw); palette(image);paper_edges(image);
        CHECK(!text_has(image,"stress"));
        nav_buttons(KEY_A,out);CHECK(out==CHOICES[i]);
        nav_open();nav_buttons(KEY_DOWN,out);nav_buttons(KEY_A,out);CHECK(out==CHOICES[(i+1)%NAV_CHOICE_COUNT]);
    }
    nav_open();nav_buttons(KEY_B,out);CHECK(!nav_is_open()&&out==NavChoice::None);
    nav_open();nav_buttons(KEY_SELECT,out);CHECK(!nav_is_open());
    nav_open();nav_buttons(KEY_DOWN,out);nav_touch_down(90,64,out);CHECK(out==NavChoice::None&&nav_is_open());
    nav_buttons(0,out);nav_touch_down(20,100,out);CHECK(nav_is_open()&&out==NavChoice::None);nav_close();
    // A committed choice keeps one pointer-free, draw-only copy of the exact
    // menu pixels for the closing half of the runtime blackletter gate.
    nav_open();nav_capture();auto live=render(320,nav_draw);nav_buttons(KEY_A,out);
    auto frozen=render(320,nav_draw_transition);CHECK(live.pixels==frozen.pixels);
    CHECK(live.objects==frozen.objects);nav_transition_clear();
}
constexpr UnitKind ORDER[]={UnitKind::Osc,UnitKind::Noise,UnitKind::Fm,UnitKind::Motor,UnitKind::SamplePlayer,UnitKind::Grain,UnitKind::MicIn,UnitKind::Filter,UnitKind::Vca,UnitKind::Delay,UnitKind::Recorder,UnitKind::Fold,UnitKind::Bitcrush,UnitKind::Chorus,UnitKind::Stranular,UnitKind::Reverb,UnitKind::Ghost,UnitKind::Ring,UnitKind::Tilt,UnitKind::Strain};
void test_picker() {
    for(int i=0;i<20;++i) {
        Fixture f;f.picker();
        for(int j=0;j<i;++j) {picker_buttons(KEY_DOWN);picker_buttons(0);}
        auto image=render(320,picker_draw);palette(image);paper_edges(image);CHECK(text_has(image,m::module_name(ORDER[i])));
        picker_buttons(KEY_A);CHECK(g_overlay==Overlay::None);CHECK(f.engine->chains[0].units[0]->kind==ORDER[i]);
    }
    for(int i=0;i<20;++i) {
        Fixture f;f.picker();
        int first=std::min(i,14);
        for(int j=0;j<first;++j) {picker_touch_down(100,190);picker_touch_hold(100,164);picker_touch_release();}
        const int y=66+(i-first)*26;
        picker_touch_down(90,y);CHECK(!f.engine->chains[0].units[0]);picker_touch_release();
        CHECK(g_overlay==Overlay::None&&f.engine->chains[0].units[0]->kind==ORDER[i]);
    }
    Fixture f;f.picker();
    picker_touch_down(90,66);picker_touch_hold(90,73);picker_touch_release();CHECK(!f.engine->chains[0].units[0]);
    picker_touch_down(90,66);picker_touch_hold(20,66);picker_touch_hold(90,66);picker_touch_release();CHECK(!f.engine->chains[0].units[0]);
    picker_touch_down(90,66);picker_touch_hold(98,66);picker_touch_release();CHECK(!f.engine->chains[0].units[0]);
    picker_touch_down(90,66);picker_cancel_input();picker_touch_release();CHECK(!f.engine->chains[0].units[0]);
    picker_touch_down(90,66);picker_buttons(KEY_DOWN);picker_touch_release();CHECK(!f.engine->chains[0].units[0]);picker_buttons(0);
    picker_touch_down(303,210);picker_touch_hold(303,70);picker_touch_release();CHECK(g_overlay==Overlay::Picker&&!f.engine->chains[0].units[0]);
    picker_touch_down(260,228);CHECK(g_overlay==Overlay::Picker);picker_touch_hold(220,228);picker_touch_release();CHECK(g_overlay==Overlay::Picker);
    picker_touch_down(260,228);picker_touch_release();CHECK(g_overlay==Overlay::None);
    f.picker();picker_buttons(KEY_B|KEY_A);CHECK(g_overlay==Overlay::None&&!f.engine->chains[0].units[0]);
    f.picker();picker_touch_down(90,66);g_edit_slot=1;picker_touch_release();CHECK(!f.engine->chains[0].units[0]&&!f.engine->chains[0].units[1]);
}
void branch(int slot, bool settle=true) {
    chains_close_context_branch(); g_overlay=Overlay::None;
    const int x=workspace_screen_x(g_sel_chain,slot)+29,y=workspace_screen_y(g_sel_chain,slot)+18;
    chains_view_touch_down(x,y);CHECK(!chains_context_is_open());
    chains_view_touch_release(true);CHECK(chains_context_is_open());
    if(settle) host_time_ms+=180; // existing layout assertions inspect the final pose
}
void leaf(int i) { chains_view_touch_down(190,88+i*21);chains_view_touch_release(true); }
void dual_check(bool strict=false) {
    g_eng->capture_ui_snapshot(g_ui_snapshot);
    auto top=render(400,chains_context_draw_top);palette(top);paper_edges(top);
    auto bot=render(320,chains_view_draw,strict);palette(bot);
    CHECK(top.objects+bot.objects<8192);maximum_shared_objects=std::max(maximum_shared_objects,top.objects+bot.objects);
}
void test_chains_actions() {
    { Fixture f;f.four();
      for(int c=0;c<4;++c) for(int edge:{0,39}) {chains_view_touch_down(80+c*48+edge,44);chains_view_touch_release(true);CHECK(g_sel_chain==c&&g_edit_chain==c);}
      chains_view_touch_down(127,54);chains_view_touch_release(true);CHECK(g_sel_chain==3);
      g_sel_chain=g_edit_chain=0;
      auto point=workspace_point(0,1);branch(1);CHECK(workspace_point(0,1).x==point.x&&workspace_point(0,1).y==point.y);
      dual_check(true);CHECK(chains_context_buttons(KEY_B));CHECK(!chains_context_is_open());
      branch(1);chains_view_touch_down(190,109);CHECK(!f.engine->chains[0].units[1]->bypass);
      chains_view_touch_hold(310,109);chains_view_touch_hold(190,109);chains_view_touch_release(true);CHECK(!f.engine->chains[0].units[1]->bypass);
      chains_view_touch_down(190,109);chains_view_touch_hold(196,109);chains_view_touch_release(true);CHECK(!f.engine->chains[0].units[1]->bypass);
      chains_view_touch_down(190,109);chains_view_touch_release(false);CHECK(!f.engine->chains[0].units[1]->bypass);
      leaf(1);CHECK(f.engine->chains[0].units[1]->bypass&&!chains_context_is_open());
      branch(1);CHECK(chains_context_buttons(KEY_DOWN));CHECK(chains_context_buttons(KEY_A));CHECK(!f.engine->chains[0].units[1]->bypass);
      branch(1);leaf(0);CHECK(g_overlay==Overlay::Editor&&g_edit_slot==1);g_overlay=Overlay::None;
      branch(1);leaf(2);CHECK(g_clip_unit&&g_clip_unit->kind==UnitKind::Stranular&&g_clip_unit!=f.engine->chains[0].units[1]);
      branch(1);leaf(3);CHECK(f.engine->chains[0].units[4]&&f.engine->chains[0].units[4]->kind==UnitKind::Stranular);
      branch(1);leaf(5);CHECK(g_overlay==Overlay::Picker&&g_edit_slot==5);picker_cancel_input();g_overlay=Overlay::None;
    }
    { Fixture f;f.four();auto* victim=f.engine->chains[0].units[1];
      branch(1);leaf(4);CHECK(chains_context_is_open()&&f.engine->chains[0].units[1]==victim);
      auto image=render(320,chains_view_draw);CHECK(text_has(image,"confirm delete"));
      leaf(4);CHECK(!chains_context_is_open()&&f.engine->chains[0].units[1]!=victim);
      CHECK(f.engine->structural_undo_available());chains_workspace_buttons(KEY_ZR);
      CHECK(f.engine->chains[0].units[1]&&f.engine->chains[0].units[1]->kind==UnitKind::Stranular);
      CHECK(workspace_point(0,1).x==172&&workspace_point(0,1).y==20);
      branch(1);leaf(4);chains_context_buttons(KEY_B);branch(1);leaf(4);CHECK(chains_context_is_open());
      g_ui_frame+=91;leaf(4);CHECK(chains_context_is_open()); // timeout re-arms, never deletes
    }
    { Fixture f;f.four();f.engine->chains[0].units[4]=make_unit(UnitKind::Fm);f.engine->chains[0].units[5]=make_unit(UnitKind::Noise);
      g_clip_unit=make_unit(UnitKind::Strain);branch(1);leaf(3);CHECK(f.engine->chains[0].units[5]->kind==UnitKind::Noise);
      branch(1);leaf(5);CHECK(g_overlay==Overlay::None);dual_check();
    }
    // REC copy/paste must allocate a distinct real SampleBank target, not alias.
    { Fixture f;f.picker();for(int i=0;i<10;++i) picker_buttons(KEY_DOWN);picker_buttons(KEY_A);
      CHECK(f.engine->chains[0].units[0]->kind==UnitKind::Recorder);workspace_set_point(0,0,16,12);
      const int source=static_cast<RecUnit*>(f.engine->chains[0].units[0])->sample_slot;
      branch(0);leaf(2);CHECK(g_clip_unit&&g_clip_unit->kind==UnitKind::Recorder);branch(0);leaf(3);
      CHECK(f.engine->chains[0].units[1]&&f.engine->chains[0].units[1]->kind==UnitKind::Recorder);
      CHECK(static_cast<RecUnit*>(f.engine->chains[0].units[1])->sample_slot!=source);
    }
    { Fixture f;chains_view_touch_down(40,210);CHECK(g_overlay==Overlay::None);chains_view_touch_release(true);CHECK(g_overlay==Overlay::Picker&&g_edit_slot==0);
      g_overlay=Overlay::None;picker_cancel_input();chains_view_touch_down(40,210);chains_view_touch_hold(100,210);chains_view_touch_release(true);CHECK(g_overlay==Overlay::None);
      chains_view_touch_down(130,120);chains_view_touch_release(true);CHECK(g_overlay==Overlay::Picker);
    }
}
void test_chain_mute_visual() {
    for(auto theme:{theme_test::p::Theme::Pink,theme_test::p::Theme::Graphite}) {
        theme_test::p::set(theme);Fixture f;
        f.engine->chains[0].units[0]=make_unit(UnitKind::Osc);
        workspace_set_point(0,0,40,24);g_sel_slot=g_edit_slot=1;f.refresh();
        const auto point=workspace_point(0,0);
        const int x=workspace_screen_x(0,0)+WORKSPACE_NODE_W+1;
        const int y=workspace_screen_y(0,0)+WORKSPACE_NODE_H/2;
        const auto live=render(320,chains_view_draw);palette(live);
        CHECK(live.pixels[y*320+x+10]==m::branch()&&text_has(live,"out"));
        f.engine->chains[0].mute=true;f.refresh();
        const int dirty=mutations;
        const auto muted=render(320,chains_view_draw);palette(muted);
        CHECK(text_has(muted,"mute")&&!text_has(muted,"out"));
        CHECK(muted.pixels[y*320+x+10]==paper_color(y));
        CHECK(muted.pixels[y*320+x+5]==theme_test::p::subdued());
        CHECK(muted.pixels!=live.pixels && workspace_point(0,0).x==point.x && workspace_point(0,0).y==point.y);
        f.engine->chains[0].mute=false; // frozen mute DTO, not a live engine read
        CHECK(render(320,chains_view_draw).pixels==muted.pixels&&mutations==dirty);
        f.refresh();CHECK(render(320,chains_view_draw).pixels==live.pixels);
        // Badges remain visible on empty and non-selected chains as well.
        f.engine->chains[3].mute=true;f.refresh();CHECK(text_has(render(320,chains_view_draw),"mute"));
    }
}
void test_module_menu_motion() {
    const auto original_theme=theme_test::p::current();
    for(auto theme:{theme_test::p::Theme::Pink,theme_test::p::Theme::Graphite}) {
        theme_test::p::set(theme);
        Fixture f;f.four();host_time_ms=1000;
        const auto point=workspace_point(0,1);
        const int x=workspace_screen_x(0,1),cy=workspace_screen_y(0,1)+18;
        const std::string name=m::module_name(f.engine->chains[0].units[1]->kind);
        auto label=[&](const Frame& frame) {
            for(const auto& text:frame.texts) if(text.text==name) return text;
            throw std::runtime_error("missing transition module label");
        };
        auto rest=render(320,chains_view_draw);
        chains_view_touch_down(x+29,cy);host_time_ms+=55;
        auto press=render(320,chains_view_draw);
        CHECK(press.pixels[(cy-13)*320+x+29]==paper_color(cy-13));
        CHECK(rest.pixels[(cy-13)*320+x+29]==m::spot());
        CHECK(label(rest).x==label(press).x && label(rest).y==label(press).y);
        chains_view_touch_release(true);CHECK(chains_context_is_open());
        auto opening=render(320,chains_view_draw);
        CHECK(label(opening).x==label(rest).x && label(opening).y==label(rest).y);
        CHECK(!text_has(opening,"edit"));
        host_time_ms+=95;
        auto middle=render(320,chains_view_draw);
        CHECK(text_has(middle,"edit") && !text_has(middle,"add"));
        CHECK(label(middle).x<label(opening).x);
        host_time_ms+=85;
        auto ready=render(320,chains_view_draw);palette(ready);
        for(const char* word:{"edit","bypass","copy","paste","delete","add"}) CHECK(text_has(ready,word));
        CHECK(ready.pixels[92*320+167]==m::spot());
        chains_context_buttons(KEY_DOWN);
        auto first=render(320,chains_view_draw);
        CHECK(first.pixels[92*320+167]==m::spot()); // marker starts at its last shown pose
        host_time_ms+=40;
        auto moving=render(320,chains_view_draw);
        chains_context_buttons(KEY_DOWN);
        auto retarget=render(320,chains_view_draw);
        for(int y=82;y<208;++y) CHECK(moving.pixels[y*320+167]==retarget.pixels[y*320+167]);
        host_time_ms+=85;
        auto focused=render(320,chains_view_draw);CHECK(focused.pixels[134*320+167]==m::spot());
        CHECK(mutations==0 && workspace_point(0,1).x==point.x && workspace_point(0,1).y==point.y);
        chains_context_buttons(KEY_B);CHECK(!chains_context_is_open());
        auto closing=render(320,chains_view_draw);CHECK(text_has(closing,"edit"));
        host_time_ms+=50;
        auto tail=render(320,chains_view_draw);CHECK(!text_has(tail,"add"));
        // Fresh input immediately interrupts closing, even while its pixels remain.
        chains_view_touch_down(x+29,cy);chains_view_touch_release(true);CHECK(chains_context_is_open());
        chains_context_buttons(KEY_DOWN);
        auto immediate=render(320,chains_view_draw);CHECK(text_has(immediate,"add"));
        chains_context_buttons(KEY_A);CHECK(!chains_context_is_open() && f.engine->chains[0].units[1]->bypass);
        host_time_ms+=101;
        auto returned=render(320,chains_view_draw);CHECK(!text_has(returned,"edit"));
        // Opening never delays actions or leaks a tail through editor/lifecycle exit.
        branch(1,false);chains_context_buttons(KEY_A);CHECK(g_overlay==Overlay::Editor);
        chains_close_context_branch();g_overlay=Overlay::None;
        auto cleared=render(320,chains_view_draw);CHECK(!text_has(cleared,"edit"));
        workspace_set_camera(0,448,350);
        chains_workspace_buttons(KEY_R);CHECK(chains_context_is_open());
        render(320,chains_view_draw); // offscreen source uses a bounded menu anchor
        chains_close_context_branch();
    }
    theme_test::p::set(original_theme);
}

void test_module_plates() {
    Fixture f;
    workspace_set_point(0,0,40,36);
    const int x=workspace_screen_x(0,0), cy=workspace_screen_y(0,0)+18, top=cy-13;
    const u32 dim=theme_test::gray()?theme_test::rgb(28,28,28):theme_test::rgb(207,165,184);
    const u32 inverse=theme_test::gray()?theme_test::rgb(48,48,48):theme_test::rgb(242,216,226);
    // Short/long names share one silhouette; selection and bypass never resize it.
    for(UnitKind kind:{UnitKind::Fm,UnitKind::Strain,UnitKind::Bird}) {
        delete f.engine->replace_unit(0,0,make_unit(kind));
        for(bool selected:{false,true}) for(bool bypass:{false,true}) {
            g_sel_slot=g_edit_slot=selected?0:1;
            f.engine->chains[0].units[0]->bypass=bypass;
            auto frame=render(320,chains_view_draw);palette(frame);
            const u32 fill=selected?m::spot():dim;
            for(int dy:{0,1,2,22,23,24,25}) for(int dx=-1;dx<=58;++dx) {
                const int inset=(dy==0||dy==25)?2:(dy==1||dy==24)?1:0;
                const bool inside=dx>=inset&&dx<58-inset;
                CHECK(frame.pixels[(top+dy)*320+x+dx]==(inside?fill:paper_color(top+dy)));
            }
            const char* name=m::module_name(kind);
            const int name_x=x+(58-ui::text_w(name))/2;
            const u32 label=selected?inverse:bypass?m::ghost():m::ink();
            for(int ch=0;name[ch];++ch) for(int row=0;row<8;++row) for(int col=0;col<6;++col) {
                const bool on=trackr::ui::glyph_row(name[ch],row)&(1u<<(5-col));
                CHECK(frame.pixels[(cy-4+row)*320+name_x+6*ch+col]==(on?label:fill));
            }
            CHECK(text_has(frame,"byp")==bypass);
            for(const auto& t:frame.texts) if(t.text=="byp") CHECK(t.y+t.h<=top-2);
            const u32 socket=selected?m::spot():bypass?m::ghost():m::ink();
            for(int sx:{x-2,x+59}) CHECK(frame.pixels[(cy-2)*320+sx]==socket);
            CHECK(workspace_point(0,0).x==40&&workspace_point(0,0).y==36&&mutations==0);
        }
    }
    // The move marker is inverse ink too, never invisible on the selected fill.
    f.engine->chains[0].units[0]->bypass=false;
    chains_view_touch_down(x+29,cy);g_ui_frame+=18;chains_view_touch_hold(x+29,cy);
    auto moving=render(320,chains_view_draw);
    const int marker_x=x+(58-ui::text_w("bird"))/2-5;
    CHECK(moving.pixels[cy*320+marker_x]==inverse);
    chains_view_touch_release(false);
}
void test_chains_geometry() {
    { Fixture f;f.four();const auto p=workspace_point(0,0);const int x=workspace_screen_x(0,0)+29,y=workspace_screen_y(0,0)+18;
      chains_view_touch_down(x,y);g_ui_frame+=17;chains_view_touch_hold(x+10,y+8);CHECK(workspace_point(0,0).x==p.x);
      ++g_ui_frame;chains_view_touch_hold(x+20,y+10);chains_view_touch_release(true);CHECK(workspace_point(0,0).x==p.x+20&&workspace_point(0,0).y==p.y+10);
      CHECK(!chains_context_is_open());
      workspace_set_camera(0,70,60);chains_workspace_buttons(KEY_CPAD_RIGHT|KEY_CPAD_DOWN);CHECK(workspace_camera_x(0)==88&&workspace_camera_y(0)==78);
      chains_view_touch_down(300,180);chains_view_touch_hold(280,160);chains_view_touch_release(true);CHECK(workspace_camera_x(0)==108&&workspace_camera_y(0)==98);
      workspace_set_point(0,0,500,320);g_sel_slot=0;chains_workspace_buttons(KEY_X);CHECK(workspace_camera_x(0)>0&&workspace_camera_y(0)>0);dual_check();
    }
    { Fixture f;f.four();auto* fm=f.engine->chains[0].units[0];auto* ghost=f.engine->chains[0].units[2];
      const int sx=workspace_screen_x(0,2)+59,sy=workspace_screen_y(0,2)+18;
      const int tx=workspace_screen_x(0,0)-2,ty=workspace_screen_y(0,0)+18;
      chains_view_touch_down(sx,sy);chains_view_touch_hold(tx,ty);chains_view_touch_release(false);CHECK(f.engine->chains[0].units[0]==fm);
      chains_view_touch_down(sx,sy);chains_view_touch_hold(tx,ty);chains_view_touch_release(true);CHECK(f.engine->chains[0].units[0]==ghost&&f.engine->chains[0].units[1]==fm);
      CHECK(workspace_point(0,0).x==80);chains_workspace_buttons(KEY_ZR);CHECK(f.engine->chains[0].units[0]->kind==UnitKind::Fm&&workspace_point(0,0).x==16);
    }
    { Fixture f;f.four();for(int c=0;c<4;++c) {g_sel_chain=g_edit_chain=c;dual_check();}
      g_sel_chain=g_edit_chain=0;f.engine->chains[0].units[1]->params[0].src=ModSource::SigilA;f.engine->chains[0].units[1]->params[0].mod_depth=20000;
      f.engine->chains[0].units[1]->params[1].src=ModSource::SigilB;f.engine->chains[0].units[1]->params[1].mod_depth=-12000;dual_check();
      for(int x:{0,20,300,448}) for(int y:{0,30,200,350}) {workspace_set_camera(0,x,y);dual_check();}
      g_sel_slot=g_edit_slot=4;dual_check(); // truly empty selected slot
    }
}
void test_context_identity() {
    Fixture f;f.four();active_slot=6;g_patch_slot=1;mutations=0;
    auto top=render(400,chains_context_draw_top);CHECK(text_has(top,"patch07")&&!text_has(top,"patch02"));
    active_slot=-1;top=render(400,chains_context_draw_top);CHECK(text_has(top,"new / unsaved"));
    for(UnitKind kind:ORDER) {
        delete f.engine->replace_unit(0,1,make_unit(kind));
        for(bool bypass:{false,true}) {
            f.engine->chains[0].units[1]->bypass=bypass;auto im=render(400,chains_context_draw_top);palette(im);paper_edges(im);
            CHECK(text_has(im,bypass?"selected / slot 02 / bypass":"selected / slot 02"));
        }
    }
    f.picker(5);auto im=render(400,chains_context_draw_top);CHECK(text_has(im,"selected / slot 06"));
    Engine* saved=g_eng;g_eng=nullptr;im=render(400,chains_context_draw_top);paper_edges(im);g_eng=saved;
}
void test_shared_context() {
    Fixture f;f.four();
    // main uses one object pool for both screens. Count actual route underlay as
    // well, conservatively even when the runtime elects to skip covered chains.
    auto budget=[&](const std::function<void()>& overlay){
        f.refresh();auto top=render(400,chains_context_draw_top);
        auto bottom=render(320,[&]{chains_view_draw();overlay();},false);
        palette(top);palette(bottom);
        const int n=top.objects+bottom.objects;CHECK(n<8192);
        maximum_shared_objects=std::max(maximum_shared_objects,n);
    };
    NavChoice out;nav_open();
    for(int i=0;i<NAV_CHOICE_COUNT;++i){nav_capture();budget(nav_draw);nav_buttons(KEY_DOWN,out);}
    nav_close();f.picker(4);
    for(int i=0;i<20;++i){picker_buttons(0);budget(picker_draw);picker_buttons(KEY_DOWN);}
    // Every bounded scroll pixel, including fractional rows, has pure palette,
    // exact restored header/footer, no visible native-text ink collisions.
    picker_buttons(0);
    for(int y=70;y<=204;++y) {
        picker_touch_down(302,y);picker_touch_release();
        auto im=render(320,picker_draw);palette(im);paper_edges(im);
    }
}
void ppm(const std::filesystem::path& path,const Frame& f) {
    std::ofstream out(path,std::ios::binary);out<<"P6\n"<<f.width<<" 240\n255\n";
    for(u32 c:f.pixels) for(int shift:{0,8,16}) out.put(char(c>>shift));
    CHECK(bool(out));
}
void dump(const std::filesystem::path& dir) {
    std::filesystem::create_directories(dir);std::ofstream report(dir/"frames.tsv");
    report<<"name\ttop_objects\tbottom_objects\tout_of_bounds\n";
    Fixture f;f.four();
    auto pair=[&](const std::string& name,const std::function<void()>& bottom,bool strict=true){
        f.refresh();nav_capture();
        auto top=render(400,chains_context_draw_top);palette(top);paper_edges(top);
        auto bot=render(320,bottom,strict);palette(bot);CHECK(top.objects+bot.objects<8192);
        maximum_shared_objects=std::max(maximum_shared_objects,top.objects+bot.objects);
        ppm(dir/(name+"_top.ppm"),top);ppm(dir/(name+"_bottom.ppm"),bot);
        report<<name<<'\t'<<top.objects<<'\t'<<bot.objects<<'\t'<<bot.outside+top.outside<<'\n';
    };
    for(int i=0;i<NAV_CHOICE_COUNT;++i){ NavChoice o;nav_open();nav_buttons(0,o);nav_touch_down(80,64+i*20,o);nav_open();pair("nav_"+std::to_string(i),nav_draw);nav_close(); }
    f.picker();pair("picker_first",picker_draw);picker_buttons(KEY_DOWN);picker_buttons(KEY_DOWN);picker_buttons(0);pair("picker_fm",picker_draw);
    picker_touch_down(302,204);picker_touch_release();pair("picker_last",picker_draw);picker_touch_down(302,135);picker_touch_release();pair("picker_scroll",picker_draw);
    picker_cancel_input();g_overlay=Overlay::None;pair("chains_four",chains_view_draw,false);
    branch(1);pair("chains_branch",chains_view_draw);leaf(4);pair("chains_delete_confirm",chains_view_draw);
    chains_context_buttons(KEY_B);branch(1);leaf(1);pair("chains_bypass",chains_view_draw,false);
    branch(1);leaf(2);pair("chains_copied",chains_view_draw,false);branch(1);leaf(3);pair("chains_pasted",chains_view_draw,false);
    f.engine->chains[0].units[5]=make_unit(UnitKind::Strain);g_sel_slot=g_edit_slot=1;pair("chains_full",chains_view_draw,false);
    branch(1);pair("chains_full_branch",chains_view_draw);chains_context_buttons(KEY_B);
    delete f.engine->replace_unit(0,0,nullptr);delete f.engine->replace_unit(0,2,nullptr);pair("chains_holes",chains_view_draw,false);
    g_sel_slot=g_edit_slot=0;pair("chains_empty_selected",chains_view_draw,false);
    g_sel_slot=g_edit_slot=1;auto* u=f.engine->chains[0].units[1];
    u->params[0].src=ModSource::SigilA;u->params[0].mod_depth=23000;
    u->params[1].src=ModSource::SigilD;u->params[1].mod_depth=-16000;pair("chains_mod_routes",chains_view_draw,false);
    workspace_set_camera(0,180,75);pair("chains_panned",chains_view_draw,false);
    workspace_set_camera(0,448,350);pair("chains_offscreen",chains_view_draw,false);
    g_sel_chain=g_edit_chain=3;pair("chains_empty",chains_view_draw,false);
    f.engine->chains[3].units[5]=make_unit(UnitKind::Ghost);workspace_set_point(3,5,110,25);g_sel_slot=g_edit_slot=5;
    pair("chains_chain4_slot6",chains_view_draw,false);
    f.picker(4);pair("picker_chain4_slot5",picker_draw);
    picker_cancel_input();g_overlay=Overlay::None;
    for(int s=0;s<6;++s) {delete f.engine->replace_unit(3,s,make_unit(UnitKind::Strain));}
    g_sel_slot=g_edit_slot=5;pair("chains_six_long_names",chains_view_draw,false);
    g_sel_slot=g_edit_slot=0;f.engine->chains[3].mute=true;pair("chains_muted",chains_view_draw,false);
    active_slot=-1;pair("chains_unsaved_new",chains_view_draw,false);
    active_slot=7;g_patch_slot=2;pair("chains_active_not_browser_slot",chains_view_draw,false);
    std::ofstream summary(dir/"summary.json");
    summary<<"{\"max_shared_with_underlay\":"<<maximum_shared_objects
        <<",\"suites\":7,\"real_top\":\"chains_context_draw_top (not top_draw/main dispatch)\","
        "\"bounds\":\"strict nav/picker/top/branch; world-space chains geometry may clip at LCD/viewport\","
        "\"visible_native_text_pixel_collisions\":0}\n";
}
}
#ifdef SEANCE_SAMPLE_BROWSER_TEST
#include "sample_browser_test.h"
#endif
#include "nav_clock_test.h"
#include "clock_mod_ui_test.h"
#ifdef SEANCE_PATCH_MENU_TEST
#include "patch_menu_test.h"
#endif
#include "theme_nav_test.h"
int main(int argc,char** argv) {
    theme_test::select();
    if(argc>=2 && std::string(argv[1])=="--theme-only") {
        try { test_nav_theme(); return 0; }
        catch(const std::exception& e) { std::fprintf(stderr,"FAIL: %s\n",e.what());return 1; }
    }
    try {
        if(argc>=2 && std::string(argv[1])=="--mute-visual-only") {
            test_chain_mute_visual();
            std::puts("pass muted chains: both palettes, snapshot-only dim ink/badges, native output gap, exact unmute restore, empty/other chains; input/layout unchanged");
            return 0;
        }
        if(argc>=2 && std::string(argv[1])=="--module-menu-motion-only") {
            test_chains_actions();
            test_module_plates();
            test_module_menu_motion();
            std::puts("pass module menu motion: native press/open/text/focus/close, immediate actions/cancel, both themes");
            return 0;
        }
#ifdef SEANCE_SAMPLE_BROWSER_TEST
        if (argc >= 2 && (std::string(argv[1]) == "--sample-browser-only" || std::string(argv[1]) == "--dump-samples")) {
            test_sample_browser(argc == 3 ? argv[2] : nullptr);
            return 0;
        }
#endif
#ifdef SEANCE_PATCH_MENU_TEST
        if (argc >= 2 && (std::string(argv[1]) == "--patch-menu-only" || std::string(argv[1]) == "--dump-patch-menu" || std::string(argv[1]) == "--dump-legacy-project")) {
            test_patch_menu();
            if (argc == 3 && std::string(argv[1]) == "--dump-patch-menu") patch_menu_test::dump(argv[2]);
            if (argc == 3 && std::string(argv[1]) == "--dump-legacy-project") patch_menu_test::dump_project(argv[2]);
            return 0;
        }
#endif
        if(argc>=2 && (std::string(argv[1])=="--clock-mod-only" || std::string(argv[1])=="--dump-clock-mod")) {
            test_clock_mod_ui();
            if(argc==3 && std::string(argv[1])=="--dump-clock-mod") dump_clock_mod_ui(argv[2]);
            return 0;
        }
        if(argc>=2 && (std::string(argv[1])=="--nav-clock-only" || std::string(argv[1])=="--dump-nav-clock")) {
            test_nav_clock();
            if(argc==3 && std::string(argv[1])=="--dump-nav-clock") dump_nav_clock(argv[2]);
            return 0;
        }
        test_helper();std::puts("pass helper gradient/glyphs/palette");
        test_nav();std::printf("pass %d nav choices/button focus; no stress label\n",NAV_CHOICE_COUNT);
        test_picker();std::puts("pass all20 picker touch/button/cancel/scroll/footer");
        test_chains_actions();std::puts("pass chains branch six actions/cancel/delete/undo/full/add");
        test_module_plates();std::puts("pass uniform module plates/glyphs/selection/bypass/move");
        test_module_menu_motion();std::puts("pass module menu motion/instant input/both themes");
        test_chains_geometry();std::puts("pass chains tabs/hold/move/pan/cpad/wire/undo/top/palette/budget");
        test_context_identity();std::puts("pass actual project identity/all20 contextual heroes/bypass/empty");
        test_shared_context();std::puts("pass shared real underlay+overlay budget and every scrollbar coordinate");
        if(argc==3&&std::string(argv[1])=="--dump") dump(argv[2]);
        std::printf("pass menu-stage1; max tested shared objects=%d/8192\n",maximum_shared_objects);
        return 0;
    } catch(const std::exception& e) { std::fprintf(stderr,"FAIL: %s\n",e.what());return 1; }
}
