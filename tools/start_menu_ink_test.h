#pragma once
// Scoped lower living-ink proof. Only actual public startup APIs render media.
// Independent mask admission uses the pre-existing immutable native font bits.
#include "../platform/3ds/start_menu_ink.h"
#include "../platform/3ds/start_menu_labels_data.h"
#include "../platform/3ds/cabinet_font_data.h"
#include "../platform/3ds/wordmark_morph.h"
#include <array>
#include <limits>

namespace {
using InkMask=std::vector<unsigned char>;
int ink_rasters=0, ink_peak_top=0, ink_peak_bottom=0;
unsigned ink_peak_top_ms=0;

bool ink_home_pixel(int x,int y,bool underline=true,int only=-1) {
    constexpr int ys[]={35,99,166}, lines[]={63,127,191};
    for(int i=0;i<3;++i) {
        if(only>=0&&i!=only)continue;
        const auto& label=start_menu_labels::labels[i];
        if(underline&&y==lines[i]&&x>=20&&x<20+label.width)return true;
        const int xx=x-20,yy=y-ys[i];
        if(xx>=0&&xx<label.width&&yy>=0&&yy<label.height) {
            const int stride=(label.width+7)/8;
            if(start_menu_labels::bitmap[label.offset+yy*stride+xx/8]&(1u<<(7-xx%8)))return true;
        }
    }
    return false;
}
void ink_frame(unsigned ms=0) {
    diagnostic_ink_ms=ms;capture_frame();++ink_rasters;
    if(top.objects>ink_peak_top){ink_peak_top=top.objects;ink_peak_top_ms=ms;}
    ink_peak_bottom=std::max(ink_peak_bottom,bottom.objects);
}
StartMenuResult ink_key(u32 k) {
    const auto result=start_menu_input(k,k,0,0);neutral();return result;
}
void ink_open(const StartMenuData& d) {start_menu_open(d);neutral();}
void ink_home_admission(bool readable) {
    for(int y=0;y<240;++y)for(int x=0;x<320;++x) {
        const auto value=bottom.pixels[y*320+x];
        if(!ink_home_pixel(x,y))require(value==expected_paper(y),"home animation has no pixel outside exact old masks/underlines");
        if(readable&&ink_home_pixel(x,y,false))require(value!=expected_paper(y),"every original native heading pixel stays readable and stationary");
    }
}
void ink_mask_set(InkMask& m,int x,int y) {
    require(x>=0&&x<320&&y>=0&&y<240,"independent glyph oracle fits native screen");m[y*320+x]=1;
}
void ink_mask_small(InkMask& m,int x,int y,const char* s) {
    for(int i=0;s[i];++i)for(int row=0;row<8;++row)for(int col=0;col<6;++col)
        if(trackr::ui::glyph_row(s[i],row)&(1u<<(5-col)))ink_mask_set(m,x+6*i+col,y+row);
}
void ink_mask_title(InkMask& m,int x,int baseline,const char* s) {
    int advance=0;
    for(int i=0;s[i];++i) {
        const auto& glyph=cabinet_font::glyphs[unsigned(s[i])-32];
        for(int row=0;row<glyph.height;++row)for(int col=0;col<glyph.width;++col)
            if(cabinet_font::bits[glyph.offset+row*((glyph.width+7)/8)+col/8]&(1u<<(7-col%8)))
                ink_mask_set(m,x+(advance+32)/64+glyph.x+col,baseline+glyph.y+row);
        advance+=glyph.advance;
    }
}
InkMask ink_right_mask(const StartMenuData& d,int panel) {
    InkMask out(320*240);
    if(panel==0) {
        for(int i=0;i<d.recent_count;++i) {
            const int baseline=49+36*i;
            // Independent original hollow diamond stencil; no new symbol allowed.
            for(int dy=-3;dy<=3;++dy) {
                const int radius=(3-std::abs(dy))*2/3;
                ink_mask_set(out,156-radius,baseline-7+dy);
                ink_mask_set(out,156+radius,baseline-7+dy);
            }
            const auto& item=d.projects[d.recent_slots[i]];
            ink_mask_title(out,168,baseline,item.name);
            ink_mask_small(out,168,baseline+9,item.age);
        }
        ink_mask_small(out,168,223,"all patches >");
    } else {
        const char* names[]={"menu sound","startup","palette editor"};
        const char* values[]={"on","menu",theme_config::name(d.theme.id)};
        for(int i=0;i<3;++i){ink_mask_title(out,158,49+44*i,names[i]);ink_mask_small(out,158,58+44*i,values[i]);}
    }
    return out;
}
void ink_right_admission(const InkMask& allowed,bool complete=false) {
    for(int y=0;y<240;++y)for(int x=144;x<320;++x) {
        const bool visible=bottom.pixels[y*320+x]!=expected_paper(y);
        if(visible)require(allowed[y*320+x],"right reveal never paints haze or moves/adds original glyph geometry");
        if(complete&&allowed[y*320+x])require(visible,"settled right panel has every original native glyph pixel");
    }
}
void test_ink_clock_raster() {
    using cabinet_ink::Clock;
    Clock a,b;
    const unsigned initial=a.ms;
    a.tick(0);require(a.ms==initial,"zero lower tick is inert");
    a.pulse(1);b.pulse(1);
    require(a.pulse_age[1]==0,"only committed heading pulse starts at zero");
    a.tick(12345);for(int n=0;n<823;++n)b.tick(15);
    require(a.ms==b.ms,"lower helper clock grouping");
    for(int i=0;i<3;++i)require(a.pulse_age[i]==b.pulse_age[i],"pulse saturation independent of tick grouping");
    const unsigned before=a.ms;a.tick(UINT32_MAX);
    Clock busy;busy.pulse(1);busy.tick(160);busy.pulse(1);
    require(busy.pulse_age[1]==160,"accepted rapid-open carries visible unfinished pulse without restart jump");
    busy.tick(520);busy.pulse(1);require(busy.pulse_age[1]==0,"finished pulse may start again on a later accepted open");
    require(a.ms==(before+UINT32_MAX%16000u)%16000u,"lower clock huge tick is overflow-safe modulo16000");
    b=a;a.tick(16000);require(a.ms==b.ms,"lower helper has exact authored 16s period");
    double worst=0;
    for(int n=-40000;n<=40000;++n) {
        const float x=float(n)*.003f;
        worst=std::max(worst,std::abs(double(cabinet_ink::sine(x))-std::sin(double(x))));
    }
    require(worst<.00111,"bounded sine agrees with independent std::sin across +/-120 radians");
    const auto d=cabinet_fixture();ink_open(d);ink_frame();ink_home_admission(true);
    const auto initial_pixels=bottom.pixels;int changed=0;
    for(unsigned ms=40;ms<=16000;ms+=40) {
        const auto previous=bottom.pixels;start_menu_tick(40);ink_frame(ms);ink_home_admission(true);
        changed+=previous!=bottom.pixels;
        const auto lower=bottom.pixels,upper=top.pixels;ink_frame(ms);
        require(lower==bottom.pixels&&upper==top.pixels,"two actual draw functions remain byte-pure at every idle frame");
    }
    require(changed>100&&bottom.pixels==initial_pixels,"lower raster really moves and closes its exact16s loop without a geometry jump");
    ink_open(d);start_menu_tick(8731);ink_frame();const auto grouped=bottom.pixels;
    ink_open(d);for(int n=0;n<873;++n)start_menu_tick(10);start_menu_tick(1);ink_frame();
    require(bottom.pixels==grouped,"actual lower raster time independent of grouping");
    const auto zero=bottom.pixels;start_menu_tick(0);ink_frame();require(bottom.pixels==zero,"actual zero tick does not change ink");
    ink_open(d);start_menu_tick(UINT32_MAX);ink_frame();const auto huge=bottom.pixels;
    ink_open(d);start_menu_tick(UINT32_MAX%16000u);ink_frame();require(bottom.pixels==huge,"actual lower huge tick has correct16s phase");
    std::printf("ink sine worst %.9g; idle changed %d/400\n",worst,changed);
    pass("ink clock/raster / independent sine + zero/grouping/huge + exact16s + pure draws + every native label pixel");
}
void test_ink_reveal_raster() {
    using cabinet_motion::Reveal;
    static_assert(sizeof(Reveal)<=13000,"authored reveal stays within13k");
    require(Reveal::kDurationMs==705,"authored maximum reveal duration");
    Reveal r;r.retarget(0);require(!r.ready(),"newly targeted invisible right panel is not ready");
    const auto zero=r;r.tick(0);
    require(!std::memcmp(r.alpha,zero.alpha,sizeof r.alpha)&&!std::memcmp(r.caption_alpha,zero.caption_alpha,sizeof r.caption_alpha),"zero reveal tick leaves both fields exact");
    r.tick(100);int earlier=0,lo=255,hi=0;
    for(int y=0;y<240;y+=5)for(int x=144;x<=304;x+=5) {
        const int main=r.sample(0,x,y),caption=r.sample(0,x,y,true);
        earlier+=main>caption;lo=std::min(lo,main);hi=std::max(hi,main);
    }
    require(earlier>100&&hi>lo+15,"titles lead metadata55ms and authored field is nonuniform");
    r.tick(494);require(!r.settled()&&!r.ready(),"first open is still owned at594ms");
    r.tick(1);require(r.settled()&&r.ready(),"first open settles at595ms");
    r.retarget(1);r.tick(233);auto visible=r;r.retarget(0);
    require(!std::memcmp(r.alpha,visible.alpha,sizeof r.alpha)&&!std::memcmp(r.caption_alpha,visible.caption_alpha,sizeof r.caption_alpha),"retarget retains exact title AND metadata fields");
    for(int p=0;p<2;++p)for(int y=0;y<240;y+=3)for(int x=144;x<320;x+=3)
        require(r.sample(p,x,y)==visible.sample(p,x,y)&&r.sample(p,x,y,true)==visible.sample(p,x,y,true),"retarget retains exact bilinear samples");
    r.tick(704);require(!r.settled()&&!r.ready(),"switch is still owned at704ms");
    r.tick(1);require(r.settled()&&r.ready(),"switch settles at705ms");
    r.retarget(-1);r.tick(259);require(!r.settled()&&!r.ready(),"close is owned until260ms");
    r.tick(1);require(r.settled()&&!r.ready(),"actual close settles260ms with no target");
    r.retarget(1);r.tick(UINT32_MAX);require(r.settled()&&r.ready(),"huge reveal delta saturates");
    const auto d=cabinet_fixture();const auto library=ink_right_mask(d,0),settings=ink_right_mask(d,1);
    auto both=library;for(size_t i=0;i<both.size();++i)both[i]|=settings[i];
    ink_open(d);ink_frame();const auto closed=bottom.pixels;
    none(quick_tap(60,116));ink_frame();require(bottom.pixels==closed,"panel-open starts at exact prior raster with no immediate ink jump");
    for(int n=0;n<15;++n){start_menu_tick(40);ink_frame();ink_right_admission(library);}
    ink_right_admission(library,true);
    for(int switcher=0;switcher<10;++switcher) {
        const auto before=bottom.pixels;none(quick_tap(60,switcher%2?116:180));ink_frame();
        require(bottom.pixels==before,"actual interrupted panel retarget preserves full immediate lower raster including carried heading pulse");
        for(int n=0;n<4;++n){start_menu_tick(40);ink_frame();ink_right_admission(both);}
    }
    none(quick_tap(60,180));start_menu_tick(705);ink_frame();ink_right_admission(settings,true);
    none(ink_key(KEY_B));for(int n=0;n<13;++n){start_menu_tick(20);ink_frame();ink_right_admission(settings);}
    ink_home_admission(true);
    pass("ink reveal/raster /595-705-260 boundaries + metadata delay + exact retarget + independent native glyph admission/no haze");
}
std::vector<u32> ink_labels() {return region(bottom,0,26,132,174);}
void test_ink_ownership_freeze() {
    const auto d=cabinet_fixture();
    // Compare actual label rasters after the same elapsed time, differing only in
    // whether a gesture committed. Cancelled touches keep ordinary focus changes.
    auto route=[&](int mode) {
        ink_open(d);
        none(start_menu_input(KEY_TOUCH,KEY_TOUCH,60,116));
        if(mode!=1) {
            none(start_menu_input(0,KEY_TOUCH,140,116));
            none(start_menu_input(0,KEY_TOUCH,60,116));
        }
        if(mode==2)none(start_menu_input(KEY_X,KEY_TOUCH|KEY_X,60,116));
        none(start_menu_input(0,0,0,0));neutral();
        if(mode==3){none(ink_key(1u<<14));none(ink_key(1u<<15));} // real libctru ZL/ZR bits, absent stub aliases
        start_menu_tick(160);ink_frame();return bottom.pixels;
    };
    const auto cancelled=route(0),committed=route(1),ignored=route(2),screenshot=route(3);
    bool pulsed=false;
    for(int y=0;y<240;++y)for(int x=0;x<132;++x) {
        const int i=y*320+x;
        require(cancelled[i]==ignored[i]&&cancelled[i]==screenshot[i],"ignored keys/ZL and cancelled out-back do not pulse");
        if(ink_home_pixel(x,y,false,1))pulsed|=cancelled[i]!=committed[i];
        else require(cancelled[i]==committed[i],"committed Library pulse does not spill onto other headings/paper");
    }
    require(pulsed,"only successful panel-open visibly pulses its existing heading mask");
    ink_open(d);none(quick_tap(60,116));start_menu_tick(594);
    none(quick_tap(200,44));start_menu_tick(1);neutral();
    action(quick_tap(200,44),StartMenuAction::LoadProject,0);
    ink_open(d);none(quick_tap(60,116));start_menu_tick(595);none(quick_tap(60,180));
    none(quick_tap(200,44));start_menu_tick(704);none(quick_tap(200,44));
    require(start_menu_data().menu_sound,"partial/outgoing text never acts on target controls");
    start_menu_tick(1);neutral();require(start_menu_data().menu_sound,"partial touch is discarded, never queued");
    action(quick_tap(200,44),StartMenuAction::PreferencesChanged);
    ink_open(d);none(ink_key(KEY_RIGHT));ink_frame();action(ink_key(KEY_A),StartMenuAction::LoadProject,0);
    for(int n=0;n<8;++n)neutral();
    ink_open(d);none(ink_key(KEY_DOWN));none(ink_key(KEY_RIGHT));
    action(ink_key(KEY_A),StartMenuAction::PreferencesChanged);
    require(!start_menu_data().menu_sound,"explicit keyboard-right still settles immediately without waiting or queued action");
    ink_open(d);none(quick_tap(60,180));start_menu_tick(1000);ink_frame();const auto before_palette=ink_labels();
    none(quick_tap(200,132));start_menu_tick(9000);ink_frame();const auto preview=top.pixels;
    start_menu_tick(17000);ink_frame();require(top.pixels==preview,"existing palette preview itself stays unchanged");
    none(ink_key(KEY_B));start_menu_tick(160);neutral();ink_frame();
    require(ink_labels()==before_palette,"lower ink freezes through palette and closing modal with no catchup");
    const auto frozen=ink_labels();start_menu_tick(1000);ink_frame();require(ink_labels()!=frozen,"lower ink resumes normally on subsequent home tick");
    ink_open(d);none(quick_tap(60,116));start_menu_tick(1000);ink_frame();const auto before_browser=ink_labels();
    none(quick_tap(200,224));ink_frame();const auto browser=bottom.pixels;
    start_menu_tick(9001);ink_frame();require(bottom.pixels==browser,"browser has no new living-ink animation");
    none(ink_key(KEY_B));ink_frame();require(ink_labels()==before_browser,"browser visit freezes lower home phase without catchup");
    pass("ink ownership / commit-only pulse + no partial/outgoing touch + immediate keyboard + palette/close/browser freeze-resume");
}
void test_ink_budget() {
    auto d=cabinet_fixture();
    for(int i=0;i<5;++i){std::memset(d.projects[i].name,'W',23);d.projects[i].name[23]=0;std::memset(d.projects[i].age,'W',15);d.projects[i].age[15]=0;}
    // The upper contour is real, not a frozen low-cost English endpoint.
    ink_open(d);none(ink_key(KEY_RIGHT));
    int cycle_top=0,home_bottom=0;unsigned phase=0;
    const auto home_frame=[&](unsigned ms=0){ink_frame(ms);home_bottom=std::max(home_bottom,bottom.objects);};
    for(unsigned ms=0;ms<=48000;ms+=40) {
        if(ms)start_menu_tick(40);
        home_frame(ms);
        cycle_top=std::max(cycle_top,top.objects);
    }
    // All distinct production contour states reachable by integer-ms ticks,
    // not merely the40ms raster grid. Then exercise its worst time through the
    // actual public dual draw below. Skipping repeated phases is only counting.
    int last_word=-1,last_mix=-1,rect_peak=0;
    for(unsigned ms=0;ms<48000;++ms) {
        const auto p=wordmark_morph::phase(ms);
        if(p.from==last_word&&p.mix==last_mix)continue;
        last_word=p.from;last_mix=p.mix;int objects=0;
        wordmark_morph::render(ms,[&](int,int,int,int,uint8_t){++objects;});
        if(objects>rect_peak){rect_peak=objects;phase=ms;}
    }
    ink_open(d);start_menu_tick(phase);none(ink_key(KEY_RIGHT));home_frame(phase);
    cycle_top=std::max(cycle_top,top.objects);
    int ascii_peak=0;
    for(int ch=32;ch<=126;++ch) {
        diagnostic_ascii=ch;
        for(int i=0;i<5;++i){std::memset(d.projects[i].name,ch,23);d.projects[i].name[23]=0;std::memset(d.projects[i].age,ch,15);d.projects[i].age[15]=0;}
        ink_open(d);start_menu_tick(phase);none(ink_key(KEY_RIGHT));home_frame(phase);
        ascii_peak=std::max(ascii_peak,bottom.objects);
        const auto a=bottom.pixels,b=top.pixels;home_frame(phase);
        require(a==bottom.pixels&&b==top.pixels,"ASCII worst-phase stress retains repeated-draw equality");
        none(ink_key(KEY_LEFT));none(quick_tap(60,180));
        for(int step=0;step<18;++step){start_menu_tick(40);home_frame((phase+40*(step+1))%48000);}
        none(quick_tap(200,132));
        for(int step=0;step<5;++step){start_menu_tick(40);ink_frame();}
        // Palette preview replaces the upper logo. Wait while the lower state
        // is frozen, then close at the actual worst upper phase: this reaches
        // the expensive home+closing-overlay combination without inventing a
        // sum of incompatible preview/wordmark states.
        start_menu_tick(48000-920);none(ink_key(KEY_B));home_frame(phase);
        for(int step=0;step<4;++step){start_menu_tick(40);home_frame((phase+40*(step+1))%48000);}
        neutral();
        for(int step=0;step<12;++step){none(quick_tap(60,step%2?180:116));start_menu_tick(40);home_frame();}
    }
    std::printf("ink budget: top%d full-phase-top%d at%ums bottom%d home-bottom%d ascii%d reachable-independent-sum%d actual-dual-peak%d/8192 rasters%d;1201 dual phases x40ms+all integer-ms upper states,95ASCII x5recent x23+15bytes,95modal closes at true upper peak\n",
        ink_peak_top,cycle_top,phase,ink_peak_bottom,home_bottom,ascii_peak,cycle_top+home_bottom,peak,ink_rasters);
    require(cycle_top+home_bottom<=8192,"independent upper phase and all sampled HOME-mode lower states incl closing modal fit shared pool");
    pass("ink budget / actual48s upper morph +95ASCII maximum names/metadata +5recent +settings/palette/rapid-switch +8192 shared pool");
}
void test_start_menu_ink() {
    test_ink_clock_raster();test_ink_reveal_raster();test_ink_ownership_freeze();test_ink_budget();
    require(suites==4,"exactly four scoped living-ink groups");
}
void capture_start_menu_ink() {
    auto d=cabinet_fixture();
    const char* names[]={"velvet static","sunday loop","cold pressure","glass choir","last light"};
    const char* ages[]={"2 min","18 min","3 hours","yesterday","4 days"};
    for(int i=0;i<5;++i){std::strcpy(d.projects[i].name,names[i]);std::strcpy(d.projects[i].age,ages[i]);}
    ink_open(d);
    std::ofstream times(dump_dir+"/timeline.csv");times<<"ms,event,top_objects,bottom_objects\n";
    for(unsigned ms=0;ms<=16000;ms+=40) {
        if(ms)start_menu_tick(40);
        const char* event="";
        if(ms==2000||ms==9600){none(quick_tap(60,116));event="library";}
        if(ms==6400||ms==12400){none(quick_tap(60,180));event="settings";}
        if(ms==5200||ms==14800){none(ink_key(KEY_B));event="close";}
        ink_frame(ms);const auto a=bottom.pixels,b=top.pixels;ink_frame(ms);
        require(a==bottom.pixels&&b==top.pixels,"every packaged source frame is actual time-pure dual draw");
        char name[32];std::snprintf(name,sizeof name,"/%05u-bottom.ppm",ms);ppm(bottom,dump_dir+name);
        if(ms%2000==0||*event||ms==15960) {
            std::snprintf(name,sizeof name,"/%05u-top.ppm",ms);ppm(top,dump_dir+name);
        }
        times<<ms<<','<<event<<','<<top.objects<<','<<bottom.objects<<'\n';
    }
    std::printf("ink runtime:401 source frames (400 at40ms +16000ms endpoint), six public-API events; static actual host paper fallback; no authored replacement pixels\n");
}
} // namespace
