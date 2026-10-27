// Appended ONLY to a private copied boundary. Production picker/chains/workspace
// and exact main touch dispatch are linked; no inherited test main is executed.
#include "platform/3ds/picker_catalogue_data.h"
#include "platform/3ds/ui_nav.h"
#include "platform/3ds/sigil_surface.h"
#include "platform/3ds/menu_style.h"
#include <filesystem>
#include <fstream>
namespace picker_cases {
namespace cat=seance::platform::ui3ds::picker_catalogue;
namespace pm=seance::platform::ui3ds::picker_motion;
namespace hid=osc_input_main;
int creations=0,duals=0,peak=0,host_peak=0,commits=0,stale_after_make=0;
std::string where;
struct Scene:Fixture {
    Scene(){
        for(auto&c:engine->chains)for(auto*&u:c.units){delete u;u=nullptr;}
        g_view=View::Chains;g_overlay=Overlay::None;g_sel_chain=g_edit_chain=0;g_sel_slot=g_edit_slot=0;
        g_drag=Drag::None;hid::nav_opened=false;picker_capture();workspace_reset_all();
        engine->transport_stop();engine->capture_ui_snapshot(g_ui_snapshot);chains_capture();
        host_time_ms+=220;chains_capture();
    }
    ~Scene(){g_eng=engine.get();g_audio=&audio;g_overlay=Overlay::None;picker_capture();chains_close_context_branch();}
    Unit* add(int slot,UnitKind kind=UnitKind::Filter,int chain=0){auto*u=make_unit(kind);CHECK(u);delete engine->replace_unit(chain,slot,u);return u;}
    void settle(){picker_capture();chains_capture();host_time_ms+=220;picker_capture();chains_capture();}
    void open(int slot=0,int selected=-1){
        g_overlay=Overlay::None;picker_capture();g_sel_slot=selected<0?slot:selected;g_edit_slot=g_sel_slot;
        engine->capture_ui_snapshot(g_ui_snapshot);chains_capture();host_time_ms+=220;chains_capture();
        g_edit_slot=slot;picker_open();g_overlay=Overlay::Picker;picker_capture();chains_capture();CHECK(g_overlay==Overlay::Picker);
    }
};
void button(u32 key){picker_buttons(key);picker_capture();chains_capture();picker_buttons(0);}
void down(int x,int y){picker_buttons(0);hid::down(x,y);picker_capture();chains_capture();}
void move(int x,int y){picker_buttons(0);hid::move(x,y);picker_capture();chains_capture();}
void up(){picker_buttons(0);hid::up();picker_capture();chains_capture();}
void tap(int x,int y){down(x,y);up();}
void filter(int i){tap(45,46+i*27+9);CHECK(g_overlay==Overlay::Picker);}
void focus(UnitKind kind){
    filter(0);down(287,55);up();down(120,55);picker_cancel_input();up();
    int at=-1;for(int i=0;i<cat::count;++i)if(cat::entries[i].kind==kind)at=i;CHECK(at>=0);
    button(KEY_RIGHT);for(int i=0;i<at;++i)button(KEY_DOWN);
}
void approved_viewport(){
    focus(UnitKind::Filter);down(287,75);up(); // real bead drag -> first visible index3
    down(150,46+3*27+13);picker_cancel_input();up(); // focus filter, no placement
}
bool has(const std::vector<TextRecord>& list,const char* s){return std::any_of(list.begin(),list.end(),[&](const auto&t){return t.text==s;});}
struct Pair { std::vector<u32> top,bottom;std::vector<TextRecord> upper,lower;int tc=0,bc=0; };
void bounds(){CHECK(!outside_rects&&!outside_lines&&objects>0);for(const auto&t:texts)CHECK(t.x>=0&&t.y>=0&&t.x+t.w<=width&&t.y+t.h<=240);}
int cache_runs(){
    const auto&c=sigil_surface::bottom_cache;
    if(!c.valid||pm::state.progress<=0)return 0;
    int n=0;for(int y=0;y<sigil_surface::H;++y)for(int x=0;x<sigil_surface::W;){
        int end=x+1;while(end<sigil_surface::W&&c.pixels[y*sigil_surface::W+end]==c.pixels[y*sigil_surface::W+x])++end;
        x=end;++n;
    }return n;
}
Pair draw(bool frozen=true){
    Pair out;auto*engine=g_eng;auto*audio=g_audio;const auto clock=host_time_ms;const int dirty=mutations;
    const auto render=[&](bool top){fader_lancet_cases::surface(top);chains_draw_surface(top);bounds();};
    render(true);out.top=pixels;out.upper=texts;out.tc=objects;
    render(false);out.bottom=pixels;out.lower=texts;out.bc=objects;
    const int runs=cache_runs();const int equivalent=out.tc+out.bc-(runs?runs-1:0);
    CHECK(equivalent<=8192);peak=std::max(peak,equivalent);host_peak=std::max(host_peak,out.tc+out.bc);++duals;
    if(frozen){g_eng=nullptr;g_audio=nullptr;host_time_ms+=90000;
        render(true);CHECK(pixels==out.top&&objects==out.tc);render(false);CHECK(pixels==out.bottom&&objects==out.bc);
        g_eng=engine;g_audio=audio;host_time_ms=clock;
    }
    CHECK(mutations==dirty);return out;
}
Pair foreground(){
    Pair out;fader_lancet_cases::surface(true);picker_draw_content(true,60,8,339,239,1);out.top=pixels;
    fader_lancet_cases::surface(false);picker_draw_content(false,20,0,299,216,1);out.bottom=pixels;return out;
}
// Black-box scroll observations: only the public captured raster and catalogue
// label masks, never private picker_scroll state or an alternate input path.
void title_mask(const Pair&,const cat::Label&,u32);
std::vector<const cat::Entry*> entries_for(int group=0){
    std::vector<const cat::Entry*> out;for(const auto&e:cat::entries)if(!group||int(e.group)==group)out.push_back(&e);return out;
}
bool list_ink(u32 pixel){return pixel==menu_style::ink()||pixel==menu_style::spot();}
bool label_bit(const cat::Label&l,int x,int y){return cat::bitmap[l.offset+y*((l.width+7)/8)+x/8]&(1<<(7-x%8));}
void list_mask(const Pair&p,int offset,int group=0){
    std::array<bool,320*240> expected{};const auto entries=entries_for(group);
    for(size_t i=0;i<entries.size();++i){const auto&l=entries[i]->row;const int top=66+int(i)*27-offset+l.dy;
        for(int y=0;y<l.height;++y)for(int x=0;x<l.width;++x)
            if(top+y>=46&&top+y<208&&120+x<276&&label_bit(l,x,y))expected[(top+y)*320+120+x]=true;
    }
    // Both clipped row edges, partial seventh row, and the blank bands between
    // header/list/footer. A row must never paint into either neighbouring band.
    for(int y=34;y<217;++y)for(int x=120;x<276;++x)CHECK(list_ink(p.bottom[y*320+x])==expected[y*320+x]);
}
int raster_offset(const Pair&p,int group=0){
    const auto entries=entries_for(group);const int last=std::max(0,(int(entries.size())-6)*27);
    for(size_t i=0;i<entries.size();++i){const auto&l=entries[i]->row;
        for(int top=46;top+l.height<=208;++top){const int offset=66+int(i)*27+l.dy-top;if(offset<0||offset>last)continue;
            bool match=true;
            for(int y=0;y<l.height&&match;++y)for(int x=0;x<l.width;++x)
                if(list_ink(p.bottom[(top+y)*320+120+x])!=label_bit(l,x,y)){match=false;break;}
            if(match){list_mask(p,offset,group);return offset;}
        }
    }
    CHECK(false);return -1;
}
int scroll_offset(int group=0){return raster_offset(foreground(),group);}
void tick(unsigned ms){host_time_ms+=ms;picker_capture();chains_capture();}
void top_list(){focus(cat::entries[0].kind);CHECK(scroll_offset()==0);}
void fling(){
    top_list();down(150,180);host_time_ms+=16;move(150,160);host_time_ms+=16;move(150,140);
    CHECK(scroll_offset()==40);up();CHECK(scroll_offset()==40);tick(16);CHECK(scroll_offset()>40);
}
int bead_y(const Pair&p){
    int low=240,high=-1;for(int y=50;y<205;++y)for(int x=285;x<=289;++x)if(p.bottom[y*320+x]==menu_style::spot()){low=std::min(low,y);high=std::max(high,y);}
    CHECK(high>=low&&high-low==4);return (low+high)/2;
}
void smooth_pixels(){
    for(auto theme:{palette::Theme::Graphite,palette::Theme::Pink}){
        Scene s;palette::set(theme);auto*old=s.add(0);s.open();s.settle();top_list();const int made=creations,dirty=mutations;
        down(150,160);host_time_ms+=16;move(150,156);CHECK(scroll_offset()==0); // four pixels remains a tap candidate
        host_time_ms+=16;move(150,154);CHECK(scroll_offset()==6); // six pixels, not a whole 27px row
        host_time_ms+=16;move(153,153);CHECK(scroll_offset()==7);draw(); // detached draw is frozen even mid-drag
        tick(140);move(153,153);up();CHECK(scroll_offset()==7); // a stationary stylus older than 100ms cannot fling
        for(unsigned ms:{16u,80u,400u}){tick(ms);CHECK(scroll_offset()==7);}
        // Overscroll clamps exactly, without spring/snap/paint outside the viewport.
        down(150,70);host_time_ms+=16;move(150,200);CHECK(scroll_offset()==0);up();tick(200);CHECK(scroll_offset()==0);
        down(287,200);up();CHECK(scroll_offset()==(cat::count-6)*27);
        down(150,190);host_time_ms+=16;move(150,50);CHECK(scroll_offset()==(cat::count-6)*27);up();tick(200);CHECK(scroll_offset()==(cat::count-6)*27);
        down(150,100);host_time_ms+=16;move(150,106);CHECK(scroll_offset()==(cat::count-6)*27-6);tick(140);up();CHECK(scroll_offset()==(cat::count-6)*27-6);draw();
        CHECK(g_overlay==Overlay::Picker&&creations==made&&mutations==dirty&&s.engine->chains[0].units[0]==old);
    }
}
void inertia_catch(){
    for(auto theme:{palette::Theme::Graphite,palette::Theme::Pink}){
        Scene s;palette::set(theme);auto*old=s.add(0);s.open();s.settle();const int made=creations,dirty=mutations;
        fling();const int moving=scroll_offset();draw();tick(16);CHECK(scroll_offset()>moving);
        down(150,100);const int caught=scroll_offset();tick(32);CHECK(scroll_offset()==caught);up();
        CHECK(g_overlay==Overlay::Picker&&creations==made);tick(160);CHECK(scroll_offset()==caught); // catch-lift is never a placement
        fling();int previous=scroll_offset();bool travelled=false;
        for(int i=0;i<100;++i){tick(16);const int current=scroll_offset();CHECK(current>=previous&&current<=(cat::count-6)*27);travelled|=current>previous;previous=current;}
        CHECK(travelled&&previous>40);tick(300);CHECK(scroll_offset()==previous);draw();
        CHECK(g_overlay==Overlay::Picker&&creations==made&&mutations==dirty&&s.engine->chains[0].units[0]==old);
    }
}
void scroll_interruptions(){
    for(int reason=0;reason<6;++reason){
        where="scroll stop "+std::to_string(reason);Scene s;auto*old=s.add(0);s.open();s.settle();const int made=creations,dirty=mutations;fling();int group=0;
        if(reason==0){picker_cancel_input();picker_capture();chains_capture();}
        if(reason==1){nav_open();picker_capture();tick(64);nav_close();picker_capture();chains_capture();}
        if(reason==2)button(KEY_RIGHT);
        if(reason==3){filter(1);group=1;}
        if(reason==4){button(KEY_B);s.settle();CHECK(g_overlay==Overlay::None);s.open();s.settle();}
        if(reason==5){s.open();s.settle();}
        const int stopped=scroll_offset(group);for(unsigned ms:{16u,48u,160u,400u}){tick(ms);CHECK(scroll_offset(group)==stopped);}draw();
        CHECK(g_overlay==Overlay::Picker&&creations==made&&mutations==dirty&&s.engine->chains[0].units[0]==old);
    }
}
void scrollbar_groups_dpad(){
    Scene s;auto*old=s.add(0);s.open();s.settle();const int made=creations,dirty=mutations;top_list();
    CHECK(bead_y(foreground())==55);down(287,55);host_time_ms+=16;move(287,56);
    const int fractional=scroll_offset();CHECK(fractional>0&&fractional<27&&bead_y(foreground())==56);up();tick(200);CHECK(scroll_offset()==fractional);
    // Fractional content offsets never make dpad selection fractional/skippy.
    button(KEY_RIGHT);button(KEY_DOWN);CHECK(scroll_offset()%27==0);top_list();
    for(int i=0;i<cat::count;++i){const auto p=draw();CHECK(has(p.upper,cat::entries[i].description[0]));title_mask(p,cat::entries[i].title,palette::spot());CHECK(scroll_offset()%27==0);button(KEY_DOWN);}
    CHECK(has(draw().upper,cat::entries[0].description[0])&&scroll_offset()==0);button(KEY_UP);
    CHECK(has(draw().upper,cat::entries[cat::count-1].description[0])&&scroll_offset()==(cat::count-6)*27);
    filter(4);down(150,55);picker_cancel_input();up();const auto small=foreground();CHECK(raster_offset(small,4)==0);
    for(int dy:{6,40,-80}){down(150,55);host_time_ms+=16;move(150,55+dy);up();tick(160);CHECK(raster_offset(foreground(),4)==0);}
    CHECK(foreground().bottom==small.bottom);filter(5);const auto empty=foreground();
    for(int y:{55,100,190}){down(150,y);host_time_ms+=16;move(150,100);up();tick(160);CHECK(foreground().bottom==empty.bottom);}
    CHECK(has(draw().lower,"no modules")&&creations==made&&mutations==dirty&&s.engine->chains[0].units[0]==old);draw();
}
void title_mask(const Pair&p,const cat::Label&label,u32 color){
    const int stride=(label.width+7)/8;
    for(int y=0;y<label.height;++y)for(int x=0;x<label.width;++x)
        if(cat::bitmap[label.offset+y*stride+x/8]&(1<<(7-x%8)))CHECK(p.top[(92+label.dy+y)*400+76+x]==color);
}
void catalogue(){
    Scene s;s.add(0,UnitKind::SamplePlayer);s.add(1);s.add(2,UnitKind::Ghost);s.open(3,1);s.settle();
    const int made=creations,dirty=mutations;const auto time=s.engine->transport_frame;
    auto&bank=trackr::synth::SampleBank::instance();std::array<uint32_t,trackr::synth::SAMPLE_BANK_SIZE> generations{};
    for(size_t i=0;i<generations.size();++i)generations[i]=bank.generation(i);
    int groups[cat::filter_count]={};
    for(const auto&e:cat::entries){++groups[int(e.group)];CHECK(e.kind!=UnitKind::Empty);}
    CHECK(cat::count==28&&groups[1]==9&&groups[2]==10&&groups[3]==7&&groups[4]==2&&groups[5]==0);
    CHECK(cat::entries[0].kind==UnitKind::Osc);
    for(int i=1;i<cat::count;++i)CHECK(int(cat::entries[i-1].group)<=int(cat::entries[i].group));
    for(const auto&e:cat::entries)if(e.kind==UnitKind::Bird)CHECK(e.group==cat::Group::Sources);
    for(auto theme:{palette::Theme::Graphite,palette::Theme::Pink}) {
        palette::set(theme);chains_capture();
        for(const auto&e:cat::entries){where=e.name;focus(e.kind);const auto pair=draw();
            CHECK(has(pair.upper,e.description[0]));if(*e.description[1])CHECK(has(pair.upper,e.description[1]));
            title_mask(pair,e.title,palette::spot());
            CHECK(has(pair.upper,"ch01 / add at chain end")&&has(pair.lower,"28 total"));
        }
        filter(5);const auto empty=draw();CHECK(has(empty.upper,"catalogue / empty")&&has(empty.lower,"no modules")&&has(empty.lower,"0 total"));
        button(KEY_RIGHT);button(KEY_A);CHECK(g_overlay==Overlay::Picker&&creations==made);
    }
    CHECK(creations==made&&mutations==dirty&&s.engine->transport_frame==time&&gran_browser_scans==0&&rec_export_calls==0);
    for(size_t i=0;i<generations.size();++i)CHECK(generations[i]==bank.generation(i));
}
void buttons_session(){
    Scene s;auto*original=s.add(0);s.open();const int made=creations;
    button(KEY_A);CHECK(pm::state.progress==1&&g_overlay==Overlay::Picker&&creations==made&&s.engine->chains[0].units[0]==original);
    focus(UnitKind::Delay);button(KEY_LEFT);button(KEY_DOWN); // focus sources without applying
    CHECK(has(draw().lower,"28 total"));button(KEY_A);CHECK(has(draw().lower,"9 total")&&creations==made);
    filter(3);button(KEY_RIGHT);button(KEY_DOWN);button(KEY_DOWN);const auto before=foreground();
    const int sel=g_sel_slot;button(KEY_B);CHECK(g_overlay==Overlay::Picker&&pm::state.closing);
    button(KEY_A);CHECK(creations==made);host_time_ms+=160;picker_capture();chains_capture();CHECK(g_overlay==Overlay::None&&g_sel_slot==sel);
    s.open();s.settle();const auto after=foreground();CHECK(before.top==after.top&&before.bottom==after.bottom);
    button(KEY_LEFT);button(KEY_A);CHECK(g_overlay==Overlay::Picker&&creations==made);
    // Close touch has exactly the B semantics, only on real lift.
    down(270,228);CHECK(!pm::state.closing);up();CHECK(pm::state.closing);s.settle();CHECK(g_overlay==Overlay::None);
}
void gestures(){
    Scene s;auto*original=s.add(0);s.open();const int made=creations,dirty=mutations;
    focus(cat::entries[0].kind);
    for(int kind=0;kind<8;++kind){
        down(150,55);
        if(kind==0){move(151,56);picker_cancel_input();}
        if(kind==1){move(155,55);move(150,55);}
        if(kind==2){move(150,60);move(150,55);}
        if(kind==3){move(310,55);move(150,55);}
        if(kind==4){button(KEY_DOWN);}
        if(kind==5){move(150,90);move(150,55);}
        if(kind==6){picker_cancel_input();}
        if(kind==7){move(107,55);move(150,55);}
        up();CHECK(g_overlay==Overlay::Picker&&creations==made&&mutations==dirty&&s.engine->chains[0].units[0]==original);
    }
    // Sub-threshold row crossing and re-entry stays cancelled.
    down(150,72);move(150,73);move(150,72);up();CHECK(creations==made);
    filter(0);const auto before=draw();down(45,190);move(45,196);move(45,190);up();CHECK(draw().top==before.top);
    down(287,180);move(307,190);move(308,190);move(287,55);up();CHECK(creations==made);
    down(150,160);move(150,60);up();CHECK(creations==made);const auto scrolled=draw();CHECK(!has(scrolled.upper,"catalogue / empty"));
    focus(cat::entries[0].kind);const auto image=draw();(void)image;
    down(150,55);move(152,57);CHECK(creations==made);up();CHECK(creations==made+1&&g_overlay==Overlay::None&&s.engine->chains[0].units[0]->kind==cat::entries[0].kind);++commits;
}
void append_replace(){
    for(int slot=0;slot<6;++slot){
        Scene s;for(int i=0;i<6;++i)s.add(i,UnitKind::Strain);
        auto*old=s.engine->chains[0].units[slot];s.open(slot);s.settle();focus(UnitKind::SamplePlayer);
        const auto before=draw();char caption[64];std::snprintf(caption,sizeof caption,"ch01 / replace %02d strain",slot+1);CHECK(has(before.upper,caption));
        button(KEY_A);auto*u=s.engine->chains[0].units[slot];CHECK(g_overlay==Overlay::None&&u!=old&&u->kind==UnitKind::SamplePlayer);
        CHECK(static_cast<PlayUnit*>(u)->pitch_mode()==PlayUnit::PitchMode::Normal);++commits;
        for(int i=0;i<6;++i)if(i!=slot)CHECK(s.engine->chains[0].units[i]->kind==UnitKind::Strain);
    }
    Scene s;auto*first=s.add(0,UnitKind::SamplePlayer);auto*tail=s.add(5,UnitKind::Ghost);
    CHECK(s.engine->set_step_lock(0,2,5,0,2222));s.engine->macro_pads[0].route_count=1;s.engine->macro_pads[0].routes[0]={0,5,0,0,32767};
    workspace_set_point(0,5,420,200);const auto tail_point=workspace_point(0,5);
    s.open(1,0);s.settle();focus(UnitKind::Osc);button(KEY_A);
    CHECK(g_overlay==Overlay::None&&s.engine->chains[0].units[0]==first&&s.engine->chains[0].units[4]==tail);
    CHECK(s.engine->chains[0].units[5]->kind==UnitKind::Osc&&g_sel_slot==5&&g_edit_slot==5);
    CHECK(s.engine->pattern.steps[0][2].locks[0].slot==4&&s.engine->macro_pads[0].routes[0].slot==4);
    CHECK(workspace_point(0,4).x==tail_point.x&&workspace_point(0,4).y==tail_point.y);++commits;
    // Visible ordinal02 refers to actual physical slot4, not the empty slot1.
    s.open(4);s.settle();focus(UnitKind::Arp);CHECK(has(draw().upper,"ch01 / replace 02 ghost"));button(KEY_A);
    CHECK(s.engine->chains[0].units[4]->kind==UnitKind::Arp&&!s.engine->chains[0].units[1]);
    CHECK(s.engine->chains[0].units[0]==first&&s.engine->chains[0].units[5]->kind==UnitKind::Osc);++commits;
}
void stale(){
    for(int reason=0;reason<10;++reason){
        Scene s;auto*old=s.add(0);s.add(2,UnitKind::Ghost);s.open();s.settle();focus(cat::entries[0].kind);down(150,55);const int made=creations;
        Engine*engine=g_eng;auto*audio=g_audio;
        if(reason==0)g_eng=nullptr;
        if(reason==1)g_audio=nullptr;
        if(reason==2)g_view=View::Sigils;
        if(reason==3)g_edit_slot=1;
        if(reason==4)g_sel_slot=2;
        if(reason==5)g_sel_chain=1;
        if(reason==6)delete s.engine->replace_unit(0,2,new NoiseUnit);
        if(reason==7)s.engine->chains[1].units[4]=new OscUnit;
        if(reason==8)g_overlay=Overlay::Editor;
        if(reason==9)std::swap(s.engine->chains[0].units[0],s.engine->chains[0].units[2]);
        picker_capture();g_eng=engine;g_audio=audio;g_view=View::Chains;g_edit_slot=g_sel_slot=g_sel_chain=0;
        up();CHECK(creations==made);CHECK(s.engine->chains[0].units[reason==9?2:0]==old);
        CHECK(g_overlay!=Overlay::Picker);
    }
    Scene s;auto*old=s.add(0);s.open();s.settle();focus(UnitKind::Arp);stale_after_make=1;
    button(KEY_A);CHECK(g_overlay==Overlay::None&&s.engine->chains[0].units[0]==old&&mutations==0);g_sel_slot=0;
}
void main_stage(u32 down,u32 held){
    const auto input_overlay=g_overlay;
    const bool stage_modal_frame=g_view==View::Chains&&(g_overlay==Overlay::Picker||(g_overlay==Overlay::None&&chains_context_is_open()));
    bool stage_skip_touch=false;
#include "main_picker_stage.inc"
    if(!stage_skip_touch)hid::handle_touch(down,held);
    picker_capture();chains_capture();
}
void ownership_motion(){
    for(int count:{0,1,3,6}){
        Scene s;for(int i=0;i<count;++i)s.add(i,UnitKind::Filter);
        g_sel_slot=g_edit_slot=count?count/2:0;s.settle();const auto closed=draw();
        picker_open();g_overlay=Overlay::Picker;picker_capture();chains_capture();const auto zero=draw();
        CHECK(zero.top==closed.top&&zero.bottom==closed.bottom);
        host_time_ms+=80;picker_capture();chains_capture();CHECK(pm::state.progress>0&&pm::state.progress<1);
        const auto half=draw();(void)half;button(KEY_B);CHECK(pm::state.closing);
        host_time_ms+=80;picker_capture();chains_capture();CHECK(g_overlay==Overlay::None);
    }
    Scene s;auto*old=s.add(0);s.open();const int made=creations;
    // Fresh touch snaps BEFORE target selection, and down alone does not create.
    down(45,46+5*27+9);CHECK(pm::state.progress==1&&creations==made);up();CHECK(has(draw().upper,"catalogue / empty"));
    focus(cat::entries[0].kind);
    for(u32 key:{u32(KEY_START),u32(KEY_Y)}){
        down(150,55);main_stage(key|KEY_A|KEY_TOUCH,key|KEY_TOUCH);move(150,55);up();
        CHECK(g_overlay==Overlay::Picker&&s.engine->chains[0].units[0]==old&&creations==made);
    }
    // Modal owns all held A tails: main feeds fresh down edges only.
    s.open();main_stage(KEY_A,KEY_A);CHECK(pm::state.progress==1&&creations==made);
    for(int i=0;i<4;++i)main_stage(0,KEY_A);
    CHECK(creations==made&&g_overlay==Overlay::Picker);
    main_stage(0,0);filter(0);focus(cat::entries[0].kind);down(150,55);
    nav_open();picker_capture();CHECK(g_overlay==Overlay::Picker);nav_close();up();CHECK(creations==made&&g_overlay==Overlay::Picker);
}
void recorder(){
    auto&bank=trackr::synth::SampleBank::instance();const size_t budget=bank.pcm_budget_bytes();
    Scene s;auto*old=s.add(0);s.open();s.settle();focus(UnitKind::Recorder);
    std::array<uint32_t,trackr::synth::SAMPLE_BANK_SIZE> gen{};for(size_t i=0;i<gen.size();++i)gen[i]=bank.generation(i);
    bank.set_pcm_budget_for_test(0);button(KEY_A);bank.set_pcm_budget_for_test(budget);
    CHECK(g_overlay==Overlay::Picker&&s.engine->chains[0].units[0]==old&&mutations==0);
    for(size_t i=0;i<gen.size();++i)CHECK(gen[i]==bank.generation(i));
    button(KEY_A);CHECK(g_overlay==Overlay::None);auto*rec=static_cast<RecUnit*>(s.engine->chains[0].units[0]);
    CHECK(rec->kind==UnitKind::Recorder&&s.engine->recorder_is_private(*rec));CHECK(!rec->is_recording()&&rec->recorded_frames()==0);
    const int sample=rec->sample_slot;CHECK(bank.slot(sample).num_frames()==rec->required_capacity_frames());++commits;
    delete s.engine->replace_unit(0,0,nullptr);bank.clear_slot(sample);
}
void capture(const std::filesystem::path&out){
    CHECK(!std::filesystem::exists(out));std::filesystem::create_directories(out);
    for(auto theme:{palette::Theme::Graphite,palette::Theme::Pink}){
        Scene s;palette::set(theme);s.add(0,UnitKind::SamplePlayer);s.add(1);s.add(2,UnitKind::Ghost);s.open(3,1);s.settle();approved_viewport();
        const auto p=draw();const std::string name=theme==palette::Theme::Graphite?"graphite":"pink";
        width=400;pixels=p.top;dump_surface(out.string(),name+"_top");width=320;pixels=p.bottom;dump_surface(out.string(),name+"_bottom");
    }
    const auto motion=out/"motion";std::filesystem::create_directories(motion);
    Scene s;palette::set(palette::Theme::Graphite);s.add(0,UnitKind::SamplePlayer);s.add(1);s.add(2,UnitKind::Ghost);
    s.open(3,1);s.settle();approved_viewport();button(KEY_B);s.settle();s.open(3,1);
    const uint64_t began=host_time_ms;
    const auto save=[&](const std::string&name){picker_capture();chains_capture();const auto p=draw();
        width=400;pixels=p.top;dump_surface(motion.string(),name+"_top");width=320;pixels=p.bottom;dump_surface(motion.string(),name+"_bottom");};
    for(int ms:{0,40,100,160,200}){host_time_ms=began+ms;save("open-"+std::to_string(ms));}
    button(KEY_B);host_time_ms=began+280;save("close-80");host_time_ms=began+360;save("close-160");
    std::puts("module picker capture: 2 current C++ duals + 7 actual motion duals, 18 raw PPMs; 0 regression groups");
}
int run(){int count=0,failed=0;const auto group=[&](const char*name,auto test){++count;try{test();std::printf("ok: picker %s\n",name);}catch(const std::exception&e){++failed;std::fprintf(stderr,"FAIL picker %s [%s]: %s\n",name,where.c_str(),e.what());g_overlay=Overlay::None;picker_capture();hid::nav_opened=false;}};
    group("28 metadata / both themes / detached draw / no creation or preview",catalogue);
    group("opening A / two columns / filter-A / B and close / session",buttons_session);
    group("real main touch / sticky out-back-jitter / list and scrollbar",gestures);
    group("pixel threshold / partial seventh row / hard bounds / stale-lift no fling / both themes",smooth_pixels);
    group("timed physical-lift inertia / frozen DTO / catch-without-placement / decay / both themes",inertia_catch);
    group("fling cancellation / nav / buttons / filter / close / reopen",scroll_interruptions);
    group("continuous scrollbar / exact dpad stepping / small and empty groups",scrollbar_groups_dpad);
    group("six duplicate replacements / normal PLAY / sparse true-tail append",append_replace);
    group("engine audio view selection all-chain identity / post-allocate stale",stale);
    group("REC failure transaction / private staged workspace lifecycle",recorder);
    group("real stage START-Y / held A tails / opening touch / reverse / zero / nav",ownership_motion);
    std::printf("module picker: %d/%d groups; %d frozen duals; %d commits; conservative GPU-equivalent peak %d/8192; raw host peak %d\n",count-failed,count,duals,commits,peak,host_peak);
    std::puts("zero inherited groups; current C++ host only, no hardware/FPS/PCM claims");return failed?1:0;
}
}
extern "C" Unit* __real__ZN6seance9make_unitENS_8UnitKindE(UnitKind);
extern "C" Unit* __wrap__ZN6seance9make_unitENS_8UnitKindE(UnitKind kind){
    ++picker_cases::creations;auto*unit=__real__ZN6seance9make_unitENS_8UnitKindE(kind);
    if(picker_cases::stale_after_make){picker_cases::stale_after_make=0;g_sel_slot=1;}return unit;
}
int main(int argc,char**argv){if(argc==2&&std::string(argv[1])=="--proof-only")return picker_cases::run();
    if(argc==3&&std::string(argv[1])=="--capture-only"){picker_cases::capture(argv[2]);return 0;}return 2;}
