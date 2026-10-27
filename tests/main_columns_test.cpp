// Real controller/engine assertions only; no image export, device or FPS claim.
#include "../platform/3ds/chain_columns.h"
#include "../platform/3ds/sigil_surface.h"
#include "../platform/3ds/ui_transition.h"
#include "../platform/3ds/debug_overlay.h"
#include "../platform/3ds/play_readout.h"
#define main inherited_menu_stage1_main
#include "menu_stage1_test.cpp"
#undef main

namespace columns_test {
namespace cc=seance::platform::ui3ds::chain_columns;
namespace sf=seance::platform::ui3ds::sigil_surface;
void tap(int x,int y){chains_view_touch_down(x,y);chains_view_touch_release(true);}
void settle(){host_time_ms+=220;chains_capture();}
struct Main : Fixture {
    Main(){chains_capture();}
    Unit* add(int slot,UnitKind k=UnitKind::Ring){auto* u=make_unit(k);CHECK(u);delete engine->replace_unit(0,slot,u);return u;}
    void six(){for(int i=0;i<6;++i)add(i);g_sel_slot=g_edit_slot=1;chains_capture();settle();}
};
void geometry(){
    int order[]={0,1,2,3,4,5};
    for(int count=0;count<=6;++count)for(int selection=0;selection<=count;++selection){
        if(count==6&&selection==6)continue;
        const int s=selection==count?cc::Add:selection;auto l=cc::layout(order,count,s);
        const int left=cc::group_left(count);int end=left;
        for(int i=0;i<count;++i){CHECK(l.box[i].x==end);CHECK(l.box[i].w==(i==s?160:32));end+=l.box[i].w;}
        CHECK(end<=320);CHECK(bool(l.box[cc::Add].w)==(count<6));
        if(count<6){CHECK(l.box[cc::Add].x==end);CHECK(l.box[cc::Add].w==(s==cc::Add?160:32));end+=l.box[cc::Add].w;CHECK(end<=320);}
        CHECK(left==320-end); // includes add; independent of selected position
        CHECK(count<5||left==0); // 5+add / 6 keep the shipped full-width geometry
        CHECK(cc::hit(l,-1)==-1&&cc::hit(l,320)==-1);
        for(int x=0;x<320;++x)CHECK((cc::hit(l,x)>=0)==(x>=left&&x<end));
    }
    cc::Motion a;auto first=cc::layout(order,6,0),last=cc::layout(order,6,5);
    a.aim(first,0);a.aim(last,0);a.sample(73);auto half=a.shown;
    a.aim(cc::layout(order,6,2),73);CHECK(!std::memcmp(&half,&a.shown,sizeof half));
    a.sample(273);CHECK(a.shown.box[2].w==160&&a.shown.box[5].w==32);
    for(int i=1;i<6;++i)CHECK(a.shown.box[i].x==a.shown.box[i-1].x+a.shown.box[i-1].w);
    cc::Motion sparse;int holes[]={1,4};
    sparse.aim(cc::layout(holes,2,1),0);sparse.aim(cc::layout(holes,2,4),0);
    sparse.sample(73);const auto in_flight=sparse.shown;
    CHECK(in_flight.box[1].x==48&&in_flight.box[cc::Add].x+in_flight.box[cc::Add].w==272);
    sparse.aim(cc::layout(holes,2,cc::Add),73);CHECK(!std::memcmp(&in_flight,&sparse.shown,sizeof in_flight));
    sparse.sample(273);CHECK(sparse.shown.box[cc::Add].w==160&&sparse.shown.box[1].x==48);
    CHECK(sparse.shown.box[4].x==80&&sparse.shown.box[cc::Add].x==112);
    std::puts("ok layout: centered 0..6 incl add, inert margins, unchanged full width, interrupted sparse selection");
}
void reorder(){
    Main f;f.six();Unit* identity=f.engine->chains[0].units[1];
    identity->params[0].base=12222;identity->params[0].src=ModSource::SigilH;identity->params[0].mod_depth=-3333;
    CHECK(f.engine->set_step_lock(0,3,1,0,1234));
    f.engine->pattern_banks[3].steps[0][4].lock_count=1;
    f.engine->pattern_banks[3].steps[0][4].locks[0]={1,0,2345};
    f.engine->scenes[0][0].kind[1]=UnitKind::Ring;f.engine->scenes[0][0].base[1][0]=12222;
    f.engine->macro_pads[0].route_count=1;f.engine->macro_pads[0].routes[0]={0,1,0,0,32767};
    chains_view_touch_down(112,188);host_time_ms+=190;chains_view_touch_hold(208,188);chains_capture();
    CHECK(f.engine->chains[0].units[1]==identity&&mutations==0);
    chains_view_touch_release(false);CHECK(f.engine->chains[0].units[1]==identity&&mutations==0);settle();
    chains_view_touch_down(112,188);chains_view_touch_hold(208,188);chains_capture();chains_view_touch_release(true);
    CHECK(g_sel_slot==4&&f.engine->chains[0].units[4]==identity&&mutations==1);
    CHECK(identity->params[0].src==ModSource::SigilH&&identity->params[0].mod_depth==-3333&&identity->params[0].base==12222);
    CHECK(f.engine->pattern.steps[0][3].locks[0].slot==4);
    CHECK(f.engine->pattern_banks[3].steps[0][4].locks[0].slot==4);
    CHECK(f.engine->scenes[0][0].base[4][0]==12222&&f.engine->macro_pads[0].routes[0].slot==4);
    chains_workspace_buttons(KEY_ZR);CHECK(f.engine->chains[0].units[1]==identity);settle();
    chains_view_touch_down(112,188);chains_view_touch_hold(240,188);chains_view_touch_release(true);
    CHECK(g_sel_slot==5&&f.engine->chains[0].units[5]==identity); // new tail boundary
    CHECK(f.engine->pattern.steps[0][3].locks[0].slot==5);
    CHECK(f.engine->macro_pads[0].routes[0].slot==5);
    CHECK(patchio::save(*f.engine,"reorder.snc"));auto loaded=std::make_unique<Engine>();
    CHECK(patchio::load(*loaded,"reorder.snc"));
    CHECK(loaded->chains[0].units[5]->params[0].base==12222&&loaded->chains[0].units[5]->params[0].src==ModSource::SigilH);
    CHECK(loaded->pattern.steps[0][3].locks[0].slot==5&&loaded->macro_pads[0].routes[0].slot==5);
    chains_workspace_buttons(KEY_ZR);
    CHECK(f.engine->chains[0].units[1]==identity);settle();
    int before=mutations;chains_view_touch_down(112,188);chains_view_touch_hold(208,188);
    g_sel_chain=1;chains_capture();chains_view_touch_release(true);CHECK(mutations==before&&f.engine->chains[0].units[1]==identity);
    // Engine boundary also preserves sparse old slot identities, then undo.
    g_sel_chain=0;delete f.engine->replace_unit(0,3,nullptr);delete f.engine->replace_unit(0,5,nullptr);
    CHECK(f.engine->move_unit_before_with_undo(0,1,UNITS_PER_CHAIN)==4);
    CHECK(f.engine->chains[0].units[4]==identity);CHECK(f.engine->undo_structural_edit());
    CHECK(f.engine->chains[0].units[1]==identity);
    std::puts("ok reorder: preview/cancel/drop/last slot, identity/routes/locks/banks/scenes/macros and undo");
}
void input(){
    Main f;f.six();auto* u=f.engine->chains[0].units[1];
    tap(50,22);CHECK(g_overlay==Overlay::Editor);g_overlay=Overlay::None;chains_capture();settle();
    tap(100,22);CHECK(u->bypass);tap(100,22);CHECK(!u->bypass);
    const int before=mutations;
    chains_view_touch_down(46,94);chains_view_touch_hold(177,94);chains_view_touch_release(true);
    CHECK(u->params[0].base==32767&&f.engine->chains[0].units[1]==u&&mutations>before);
    tap(300,90);CHECK(g_sel_slot==5&&g_overlay==Overlay::None);settle();
    chains_workspace_buttons(KEY_RIGHT);CHECK(g_sel_slot==0);settle();
    chains_workspace_buttons(KEY_LEFT);CHECK(g_sel_slot==5);settle();
    chains_workspace_buttons(KEY_R);CHECK(chains_context_is_open());chains_capture();
    const int dirty=mutations;tap(20,100);CHECK(!chains_context_is_open()&&mutations==dirty&&g_sel_slot==5);
    chains_workspace_buttons(KEY_R);chains_capture();chains_context_buttons(KEY_DOWN);chains_context_buttons(KEY_A);
    CHECK(g_clip_unit&&!chains_context_is_open());
    chains_workspace_buttons(KEY_R);chains_context_buttons(KEY_DOWN);chains_context_buttons(KEY_DOWN);chains_context_buttons(KEY_A);
    CHECK(chains_context_is_open()&&mutations==dirty); // paste disabled when full
    chains_context_buttons(KEY_DOWN);chains_context_buttons(KEY_A);CHECK(f.engine->chains[0].units[5]);
    tap(20,100);chains_workspace_buttons(KEY_R); // dismissal clears confirmation
    chains_context_buttons(KEY_DOWN);chains_context_buttons(KEY_DOWN);chains_context_buttons(KEY_DOWN);
    chains_context_buttons(KEY_A);CHECK(f.engine->chains[0].units[5]);
    chains_context_buttons(KEY_A);CHECK(!f.engine->chains[0].units[5]&&!chains_context_is_open());
    chains_workspace_buttons(KEY_ZR);CHECK(f.engine->chains[0].units[5]);settle();
    chains_workspace_buttons(KEY_R);chains_context_buttons(KEY_A);CHECK(g_overlay==Overlay::Picker);
    picker_buttons(KEY_B);CHECK(g_overlay==Overlay::None);settle();
    tap(54,220);CHECK(g_sel_chain==1);chains_capture();settle();tap(94,24);CHECK(g_overlay==Overlay::Picker);
    picker_buttons(KEY_A);CHECK(g_overlay==Overlay::None&&f.engine->chains[1].units[0]);
    // Sparse append remains after the heard serial chain, never into its hole.
    g_sel_chain=g_edit_chain=0;g_sel_slot=g_edit_slot=4;
    Unit* tail=f.engine->chains[0].units[5];delete f.engine->replace_unit(0,1,nullptr);
    chains_capture();settle();chains_workspace_buttons(KEY_RIGHT);chains_workspace_buttons(KEY_RIGHT);
    chains_workspace_buttons(KEY_R);CHECK(g_overlay==Overlay::Picker);picker_buttons(KEY_A);
    CHECK(f.engine->chains[0].units[4]==tail&&f.engine->chains[0].units[5]->kind==UnitKind::Osc);
    std::puts("ok input: edit/bypass/stored sliders, tap selection, wrap, word menu dismissal, clipboard/capacity/delete confirmation/add/replace");
}
void audit_safety(){
    static_assert(!seance::platform::debug_overlay::Available,"normal builds must leave ZR for undo");
    Main f;f.six();Unit* original=f.engine->chains[0].units[1];
    original->params[0].base=12345;CHECK(f.engine->set_step_lock(0,3,1,0,2345));
    const int before=mutations;
    chains_workspace_buttons(KEY_X);CHECK(g_overlay==Overlay::Editor);
    chains_capture();CHECK(chains_confirm_editor_delete());chains_capture();
    CHECK(g_overlay==Overlay::None&&chains_context_is_open()&&mutations==before);
    auto image=render(320,chains_view_draw);CHECK(text_has(image,"sure?"));
    chains_context_buttons(KEY_X);CHECK(f.engine->chains[0].units[1]==original&&mutations==before);
    chains_context_buttons(KEY_B);CHECK(!chains_context_is_open()&&f.engine->chains[0].units[1]==original);
    chains_workspace_buttons(KEY_X);chains_capture();CHECK(chains_confirm_editor_delete());chains_capture();
    image=render(320,chains_view_draw);
    for(const auto& t:image.texts)if(t.text=="sure?"){
        chains_view_touch_down(t.x,t.y+2);chains_view_touch_hold(0,200);chains_view_touch_release(true);
    }
    CHECK(f.engine->chains[0].units[1]==original&&mutations==before);
    chains_context_buttons(KEY_A);CHECK(!chains_context_is_open()&&mutations==before+1);
    CHECK(std::find(std::begin(f.engine->chains[0].units),std::end(f.engine->chains[0].units),original)==std::end(f.engine->chains[0].units));
    chains_workspace_buttons(KEY_ZR);CHECK(f.engine->chains[0].units[1]==original);
    CHECK(original->params[0].base==12345&&f.engine->pattern.steps[0][3].locks[0].value==2345);

    // A stale confirmation never deletes a replacement occupying the same slot.
    chains_workspace_buttons(KEY_X);chains_capture();CHECK(chains_confirm_editor_delete());
    Unit* replacement=make_unit(UnitKind::Noise);delete f.engine->replace_unit(0,1,replacement);
    const int stale_before=mutations;chains_context_buttons(KEY_A);
    CHECK(f.engine->chains[0].units[1]==replacement&&mutations==stale_before&&!chains_context_is_open());
    std::puts("ok audit deletion: X/edit then X/confirm, repeated X inert, B/drag cancel, explicit commit, ZR restores state/locks, stale identity rejected");
}
void play_card_readout(){
    Main f;auto* play=static_cast<PlayUnit*>(f.add(0,UnitKind::SamplePlayer));
    char expected[24];
    for(bool modern:{false,true})for(bool quantize:{false,true}){
        play->set_modern(modern);play->pitch_quantize=quantize;
        play->params[PlayUnit::P_SPEED].base=play->params[PlayUnit::P_SPEED].effective=-9000;
        g_sel_slot=g_edit_slot=0;chains_capture();settle();
        seance::platform::ui3ds::play_readout::value_text(expected,sizeof expected,PlayUnit::P_SPEED,-9000,modern,quantize);
        const auto top=render(400,chains_context_draw_top),bottom=render(320,chains_view_draw);
        CHECK(text_has(top,expected)&&text_has(bottom,expected));
        play->pitch_quantize=!quantize;play->params[PlayUnit::P_SPEED].base=8192;
        CHECK(render(400,chains_context_draw_top).pixels==top.pixels);
        CHECK(render(320,chains_view_draw).pixels==bottom.pixels);
    }
    std::puts("ok PLAY card readout: signed multiplier, legacy/modern/qnt, both frozen LCDs");
}
void sparse_input(){
    Main f;Unit* first=f.add(0);Unit* second=f.add(4);
    g_sel_slot=g_edit_slot=0;chains_capture();settle();
    const int unchanged=mutations;
    tap(8,22);tap(304,90);CHECK(g_sel_slot==0&&g_overlay==Overlay::None&&mutations==unchanged);
    const auto top=render(400,chains_context_draw_top),bottom=render(320,chains_view_draw);
    CHECK(top.objects+bottom.objects<8192);
    bool shifted=false;for(const auto& t:bottom.texts)if(t.text=="edit")shifted=t.x==60&&t.y==22;
    CHECK(shifted); // lower origin48; top column retains the exact +40 offset
    CHECK(top.pixels[40*400+90]==menu_style::spot()&&bottom.pixels[197*320+50]==menu_style::spot());
    chains_view_touch_down(62,94);chains_view_touch_hold(193,94);chains_view_touch_release(true);
    CHECK(first->params[0].base==32767);
    const auto second_base=second->params[0].base;tap(224,94);
    CHECK(g_sel_slot==4&&second->params[0].base==second_base);settle();
    tap(94,22);CHECK(g_overlay==Overlay::Editor);g_overlay=Overlay::None;chains_capture();settle();
    tap(142,22);CHECK(second->bypass);tap(142,22);CHECK(!second->bypass);
    tap(210,22);CHECK(chains_context_is_open());chains_capture();tap(8,100);CHECK(!chains_context_is_open());
    chains_workspace_buttons(KEY_LEFT);settle();CHECK(g_sel_slot==0);
    int before=mutations;
    chains_view_touch_down(128,188);chains_view_touch_hold(0,188);chains_view_touch_release(true);
    CHECK(f.engine->chains[0].units[0]==first&&mutations==before);settle(); // left margin -> head/no-op
    chains_view_touch_down(128,188);chains_view_touch_hold(319,188);chains_capture();
    CHECK(f.engine->chains[0].units[0]==first&&mutations==before); // preview is not a commit
    chains_view_touch_release(true);
    CHECK(f.engine->chains[0].units[g_sel_slot]==first&&mutations==before+1);
    Unit* head=nullptr;Unit* tail=nullptr;
    for(auto* u:f.engine->chains[0].units)if(u){if(!head)head=u;tail=u;}
    CHECK(head==second&&tail==first); // centered right margin reaches sparse tail
    chains_workspace_buttons(KEY_ZR);settle();
    CHECK(f.engine->chains[0].units[0]==first&&f.engine->chains[0].units[4]==second&&g_sel_slot==0);
    before=mutations;
    for(int x:{-1,320}){chains_view_touch_down(128,188);chains_view_touch_hold(x,188);chains_view_touch_release(true);settle();}
    CHECK(mutations==before&&f.engine->chains[0].units[0]==first);
    tap(252,22);CHECK(g_overlay==Overlay::Picker);picker_buttons(KEY_B);chains_capture();settle();
    std::puts("ok sparse input: centered hitboxes/actions/rails/add, dual edges, head/tail preview/drop/undo and offscreen cancel");
}
void polish(){
    Main f;
    const auto centered_add=[](const Frame& image,int center){
        int found=0;
        for(const auto& t:image.texts)if(t.text=="+"||t.text=="add"){
            CHECK(std::abs(2*t.x+t.w-2*center)<=2);
            CHECK(t.y>=22&&t.y+t.h<=48);++found;
        }
        CHECK(found==2);
    };
    auto bottom=render(320,chains_view_draw);centered_add(bottom,160);
    auto top=render(400,chains_context_draw_top);bool empty=false;
    for(const auto& t:top.texts)if(t.text=="empty chain / add a module below"){
        CHECK(std::abs(2*t.x+t.w-400)<=2&&t.y==136);empty=true;
    }
    CHECK(empty);
    tap(160,24);CHECK(g_overlay==Overlay::Picker);chains_capture();
    centered_add(render(320,picker_draw),160); // same sharp anchor; no catalog overlap
    picker_buttons(KEY_B);chains_capture();
    f.add(0);g_sel_slot=g_edit_slot=0;settle();
    centered_add(render(320,chains_view_draw),240); // 32px trailing add
    for(auto theme:{palette::Theme::Pink,palette::Theme::Graphite}){
        palette::set(theme);chains_capture();
        const auto paper=render(320,[]{m::paper(320);});
        bottom=render(320,chains_view_draw);
        // Selected tile starts64, border66; its disjoint 8px halo stays subtle.
        CHECK(bottom.pixels[100*320+65]==sf::Cache::blend(paper.pixels[100*320+65],m::spot(),17,255));
        CHECK(bottom.pixels[100*320+63]==sf::Cache::blend(paper.pixels[100*320+63],m::spot(),10,255));
        CHECK(bottom.pixels[100*320+56]==paper.pixels[100*320+56]);
        CHECK(bottom.pixels[207*320+65]==paper.pixels[207*320+65]); // no footer glow
        const auto upper_paper=render(400,[]{m::paper(400);});
        top=render(400,chains_context_draw_top);
        CHECK(top.pixels[100*400+105]==sf::Cache::blend(upper_paper.pixels[100*400+105],m::spot(),17,255));
        CHECK(top.pixels[35*400+105]==upper_paper.pixels[35*400+105]); // clear heading lane
        CHECK(top.objects+bottom.objects<8192);
    }
    chains_workspace_buttons(KEY_RIGHT);settle();
    centered_add(render(320,chains_view_draw),176); // 160px selected add after32px module
    const auto paper=render(320,[]{m::paper(320);});bottom=render(320,chains_view_draw);
    CHECK(bottom.pixels[100*320+65]==paper.pixels[100*320+65]); // no module selected -> no halo
    chains_workspace_buttons(KEY_R);chains_capture();centered_add(render(320,picker_draw),176);
    picker_buttons(KEY_B);chains_capture();
    std::puts("ok polish: centered empty/narrow/wide add + sharp picker anchor, centered upper empty text, quiet selected-only dual halo and clean chrome");
}
void paper_routing(){
    Main f;f.add(0);g_sel_slot=g_edit_slot=0;
    const auto old_theme=palette::current();palette::set(palette::Theme::Graphite);
    m::ambient_shutdown();transition::reset();
    auto samples=[] {
        std::vector<uint32_t> out;
        for(bool top:{false,true})for(int y=10;y<240;y+=50)for(int x=10;x<320;x+=60)
            out.push_back(m::patch_sample(top,float(x),float(y)));
        return out;
    };
    auto capture=[&](uint64_t dt=0){host_time_ms+=dt;chains_capture();return samples();};
    const auto quiet=capture();const auto phase0=capture(240);CHECK(phase0!=quiet);
    const auto phase100=capture(100);CHECK(phase100!=phase0);
    CHECK(capture(18000)==phase100); // Chains retains the approved 18s period.
    g_view=View::Performance;CHECK(capture()==phase100);
    nav_open();const auto outgoing=capture(120);CHECK(outgoing!=phase100&&outgoing!=quiet);
    nav_close();CHECK(capture(120)==quiet); // existing fade continues even with nav
    CHECK(capture(100)==quiet);CHECK(capture(40000)==quiet);
    for(auto overlay:{Overlay::Picker,Overlay::Editor,Overlay::SampleBrowser,Overlay::PatchBrowser,Overlay::ConfirmDiscard}){
        g_overlay=overlay;CHECK(capture(17)==quiet);CHECK(capture(40000)==quiet);
        g_overlay=Overlay::None;CHECK(capture(500)==quiet);
    }
    transition::open();CHECK(transition::active());CHECK(capture()==quiet);
    nav_open();CHECK(capture(120)==quiet);CHECK(capture(40000)==quiet);
    nav_close();CHECK(capture(120)==quiet);
    transition::close();CHECK(capture()==quiet);CHECK(capture(40000)==quiet);
    for(auto view:{View::Performance,View::Sequencer,View::Sigils,View::Bounce,View::Patchbay}){
        g_view=View::Chains;CHECK(capture()==quiet);CHECK(capture(240)==phase100);
        g_view=view;CHECK(capture()==phase100);CHECK(capture(240)==quiet);CHECK(capture(40000)==quiet);
    }
    // Existing Chains policy: more/picker freeze phase AND strength, including
    // during entrance; nav/editors fade out. Hidden time never advances phase.
    g_view=View::Chains;m::ambient_shutdown();CHECK(capture()==quiet);const auto halfway=capture(120);
    CHECK(halfway!=quiet&&halfway!=phase0);
    chains_workspace_buttons(KEY_R);CHECK(chains_context_is_open());
    CHECK(capture(17)==halfway);CHECK(capture(40000)==halfway);
    chains_context_buttons(KEY_B);CHECK(capture(500)==halfway);
    g_edit_slot=1;picker_open();g_overlay=Overlay::Picker;
    CHECK(capture(17)==halfway);CHECK(capture(40000)==halfway);
    picker_buttons(KEY_B);CHECK(g_overlay==Overlay::None);CHECK(capture(500)==halfway);
    CHECK(capture(120)==phase0);const auto chain_moved=capture(100);
    CHECK(chain_moved!=phase0);chains_workspace_buttons(KEY_R);CHECK(chains_context_is_open());
    CHECK(capture(17)==chain_moved);CHECK(capture(40000)==chain_moved);
    chains_context_buttons(KEY_B);CHECK(capture(500)==chain_moved);
    g_edit_slot=1;picker_open();g_overlay=Overlay::Picker;
    CHECK(capture(17)==chain_moved);CHECK(capture(40000)==chain_moved);
    picker_buttons(KEY_B);CHECK(g_overlay==Overlay::None);CHECK(capture(500)==chain_moved);
    nav_open();CHECK(capture()==chain_moved);CHECK(capture(240)==quiet);
    nav_close();CHECK(capture()==quiet);CHECK(capture(240)==chain_moved);
    g_overlay=Overlay::Editor;CHECK(capture()==chain_moved);CHECK(capture(240)==quiet);
    g_overlay=Overlay::None;CHECK(capture()==quiet);CHECK(capture(240)==chain_moved);
    g_view=View::Performance;transition::open();CHECK(transition::active());
    CHECK(capture()==chain_moved);CHECK(capture(240)==quiet);CHECK(capture(40000)==quiet);
    transition::close();CHECK(capture()==quiet);CHECK(capture(40000)==quiet);
    m::ambient_shutdown();palette::set(old_theme);
    std::puts("ok paper routing: real chains_capture/transition/nav; static perform+transition after240ms, chain18s field and modal phase+strength freeze, unchanged exits/reentry");
}
void captures(){
    Main f;f.six();
    for(auto theme:{theme_test::p::Theme::Pink,theme_test::p::Theme::Graphite}){
        theme_test::p::set(theme);g_sel_chain=0;g_sel_slot=1;settle();
        auto* engine=g_eng;auto* audio=g_audio;g_eng=nullptr;g_audio=nullptr;
        auto top=render(400,chains_context_draw_top);auto bottom=render(320,chains_view_draw);
        CHECK(top.objects+bottom.objects<8192);
        CHECK(!text_has(bottom,"ring")&&text_has(bottom,"edit")&&text_has(bottom,"bypass")&&text_has(bottom,"more"));
        CHECK(top.pixels==render(400,chains_context_draw_top).pixels);
        g_eng=engine;g_audio=audio;
        // Check full catalog names/parameter ids, including long names, no assets.
        for(int kind=1;kind<int(UnitKind::Count);++kind){f.add(1,UnitKind(kind));chains_capture();
            render(400,chains_context_draw_top);render(320,chains_view_draw);}
        chains_workspace_buttons(KEY_R);chains_capture();
        std::vector<uint32_t> upper(std::begin(sf::top_cache.pixels),std::end(sf::top_cache.pixels));
        std::vector<uint32_t> lower(std::begin(sf::bottom_cache.pixels),std::end(sf::bottom_cache.pixels));
        CHECK(sf::top_cache.valid&&sf::bottom_cache.valid);
        f.engine->chains[0].units[1]->params[0].base=1234;host_time_ms+=500;chains_context_buttons(KEY_DOWN);chains_capture();
        CHECK(std::equal(upper.begin(),upper.end(),sf::top_cache.pixels)&&std::equal(lower.begin(),lower.end(),sf::bottom_cache.pixels));
        // Allocation failure path is the native DTO + veil; don't export raster.
        sf::top_cache.valid=sf::bottom_cache.valid=false;
        render(400,[]{chains_context_draw_top();std::fill(text_collisions.begin(),text_collisions.end(),false);});
        render(320,[]{chains_view_draw();std::fill(text_collisions.begin(),text_collisions.end(),false);});
        // Intentional sharp-anchor repaint over the dim native label is not a
        // live-text collision. Bounds/object assertions remain in render().
        chains_context_buttons(KEY_B);chains_capture();
        chains_workspace_buttons(KEY_R);chains_context_buttons(KEY_A);chains_capture();
        sf::top_cache.valid=sf::bottom_cache.valid=false;
        render(400,[]{chains_context_draw_top();std::fill(text_collisions.begin(),text_collisions.end(),false);});
        render(320,[]{picker_draw();std::fill(text_collisions.begin(),text_collisions.end(),false);});
        picker_buttons(KEY_B);chains_capture();
    }
    std::puts("ok capture: both LCDs/themes, full24 catalog, pointer-free draw, frozen dual blur and fallback/picker");
}
}
int main(){try{
    if(std::getenv("SEANCE_PAPER_ROUTING_ONLY")){columns_test::paper_routing();return 0;}
    if(std::getenv("SEANCE_MAIN_SAFETY_ONLY")){
        columns_test::audit_safety();columns_test::play_card_readout();columns_test::input();columns_test::reorder();
        std::puts("main audit safety: 4/4 passed; no exports/device/FPS claim");return 0;
    }
    columns_test::geometry();columns_test::reorder();columns_test::input();columns_test::sparse_input();columns_test::polish();columns_test::captures();columns_test::paper_routing();
    std::puts("main columns: 7/7 passed; no exports/device/FPS claim");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"main columns FAIL: %s\n",e.what());
    for(const auto& t:texts)std::fprintf(stderr,"text %d,%d %dx%d %s\n",t.x,t.y,t.w,t.h,t.text.c_str());
    return 1;}}
