#pragma once
// Current startup wordmark/reveal checks. Independent mask and timeline oracle;
// never includes or invokes the retired splash or compass-menu implementation.
#include "wordmark_reference.h"
#include "../platform/3ds/wordmark_morph.h"
#include "../platform/3ds/startup_reveal.h"
#include <type_traits>
#include <utility>

namespace {
template<class T,class=void>struct has_legacy_intro:std::false_type {};
template<class T>struct has_legacy_intro<T,std::void_t<decltype(std::declval<T>().show_intro)>>:std::true_type {};
static_assert(!has_legacy_intro<StartMenuData>::value,"legacy stored bit must not enter the runtime DTO");

void expect_current_wordmark(u32 elapsed) {
    namespace data=wordmark_morph_data;
    namespace ref=wordmark_reference;
    // Authored contract, independent of runtime phase()/coverage()/render().
    constexpr u32 period=12000,hold=4000,moving=8000,loop=48000;
    const u32 ms=elapsed%loop,local=ms%period;
    const int from=int(ms/period),to=(from+1)%4;
    const double t=local>hold?double(local-hold)/moving:0;
    const int mix=int(std::floor(4096*t*t*(3-2*t)+.5));
    require(!top.fractional,"wordmark keeps integer native coverage");
    for(int y=0;y<240;++y)for(int x=0;x<400;++x) {
        const u32 paper=expected_paper(y),ink=theme_test::ink();
        u32 expected=paper;
        if(x>=data::kX&&x<data::kX+data::kWidth&&y>=data::kY&&y<data::kY+data::kHeight) {
            const int at=(y-data::kY)*data::kWidth+x-data::kX;
            const double distance=(double(data::kDistance[from][at])*(4096-mix)+double(data::kDistance[to][at])*mix)/4096;
            const int alpha=std::clamp(int(std::floor((distance+2)*255/4+.5)),0,255);
            expected=0xff000000u;
            for(int s=0;s<24;s+=8)
                expected|=(((paper>>s&255)*(255-alpha)+(ink>>s&255)*alpha+127)/255)<<s;
        }
        require(top.pixels[y*400+x]==expected,"actual C++ contour equals independent 4s+8s timeline and coverage oracle");
        if(ms<=hold) {
            const int sx=x-ref::kX,sy=y-ref::kY;
            const bool inside=sx>=0&&sy>=0&&sx<ref::kWidth&&sy<ref::kHeight;
            const int bit=inside?sy*ref::kWidth+sx:0;
            const bool on=inside&&(ref::kMask[bit/8]&(1u<<(bit%8)));
            require(top.pixels[y*400+x]==(on?ink:paper),"English endpoint matches approved independent native mask and placement");
        }
    }
}

void test_startup_wordmark() {
    const auto original=cabinet_fixture();
    start_menu_open(original);neutral();capture_frame();expect_current_wordmark(0);
    const auto initial=top.pixels,lower=bottom.pixels;
    int changed=0;
    auto prior=initial;
    for(u32 ms=250;ms<=48000;ms+=250) {
        start_menu_tick(250);capture_frame();expect_current_wordmark(ms);
        changed+=top.pixels!=prior;prior=top.pixels;
        for(int y=0;y<240;++y)for(int x=0;x<320;++x)
            if(bottom.pixels[y*320+x]!=lower[y*320+x])
                require(ink_home_pixel(x,y),"lower motion stays confined to exact old labels/underlines; top oracle remains independent");
        if(ms%4000==0) {
            const auto a=top.pixels,b=bottom.pixels;
            capture_frame();capture_frame();
            require(top.pixels==a&&bottom.pixels==b,"both startup draw functions are time-pure");
        }
    }
    require(changed>100&&top.pixels==initial,"four genuine morphs and exact 48-second English loop seam");
    u32 elapsed=7000;start_menu_tick(elapsed);capture_frame();expect_current_wordmark(elapsed);
    none(quick_tap(60,116));capture_frame();expect_current_wordmark(elapsed);
    auto refreshed=original;refreshed.menu_sound=false;refreshed.show_menu=false;
    start_menu_set_data(refreshed);neutral();capture_frame();expect_current_wordmark(elapsed);
    start_menu_error("test error");neutral();capture_frame();expect_current_wordmark(elapsed);
    start_menu_tick(500);elapsed+=500;capture_frame();expect_current_wordmark(elapsed);
    none(quick_tap(60,180));start_menu_tick(705);elapsed+=705;
    capture_frame();expect_current_wordmark(elapsed);
    action(quick_tap(200,42),StartMenuAction::PreferencesChanged);
    action(quick_tap(200,86),StartMenuAction::PreferencesChanged);
    none(quick_tap(200,176));capture_frame();expect_current_wordmark(elapsed);
    require(start_menu_data().menu_sound&&start_menu_data().show_menu,"current settings cannot disable or reset wordmark motion");
    none(quick_tap(200,132));start_menu_tick(9000);elapsed+=9000;
    none(start_menu_input(KEY_B,KEY_B,0,0));neutral();
    start_menu_tick(200);elapsed+=200;neutral();capture_frame();expect_current_wordmark(elapsed);
    // Even while the palette preview replaces the upper display, time keeps moving.
    start_menu_open(original);neutral();start_menu_tick(8000);capture_frame();const auto accumulated=top.pixels;
    start_menu_open(original);neutral();for(int n=0;n<500;++n)start_menu_tick(16);
    capture_frame();require(top.pixels==accumulated,"wordmark time is independent of tick grouping");
    start_menu_open(original);neutral();start_menu_tick(UINT32_MAX);capture_frame();expect_current_wordmark(UINT32_MAX%48000);
    pass("wordmark / always on + independent English + four contours + exact 48s loop + settings/refresh/modal phase + draw purity + huge delta");
}

void test_startup_reveal() {
    const auto data=cabinet_fixture();
    for(u32 elapsed:{0u,1u,1500u,2999u,3000u,UINT32_MAX})for(int width:{320,400}) {
        start_menu_open(data);neutral();start_menu_tick(elapsed);
        surface=width==320?&bottom:&top;surface->reset(width);
        constexpr u32 base=0xff604020u;
        std::fill(surface->pixels.begin(),surface->pixels.end(),base);
        start_menu_draw_reveal(width);
        const uint64_t t=elapsed<3000?elapsed:3000,d=3000;
        const unsigned alpha=255-unsigned((255*t*t*(3*d-2*t)+d*d*d/2)/(d*d*d));
        u32 expected=0xff000000u;
        for(int s=0;s<24;s+=8)expected|=(((base>>s&255)*(255-alpha)+127)/255)<<s;
        for(u32 p:surface->pixels)require(p==expected,"startup reveal keeps exact 3-second brightness law on both LCDs");
        require(surface->objects==(alpha?1:0),"completed reveal adds no primitive");
        const int objects=surface->objects;start_menu_draw_reveal(399);
        require(surface->objects==objects,"invalid reveal width is inert");
    }
    pass("startup reveal / unchanged independent 3s law + both LCDs + endpoints/overflow");
}
} // namespace
