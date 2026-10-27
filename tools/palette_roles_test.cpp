// Current palette API: explicit independent endpoints and interruptible time law.
#include "../platform/3ds/ui_palette.h"
#include <array>
#include <cassert>
#include <climits>
#include <cstdio>
namespace p=seance::platform::ui3ds::palette;
namespace t=seance::platform::ui3ds::theme_config;
using Colors=std::array<uint32_t,14>;
constexpr uint32_t rgb(unsigned r,unsigned g,unsigned b){return 0xff000000u|r|(g<<8)|(b<<16);}
Colors colors(){return {p::bg(),p::cell(),p::cell_dim(),p::ink(),p::spot(),p::ghost(),p::branch(),p::subdued(),p::inverse(),p::menu_ghost(),p::shadow(),p::playhead(),p::paper_color(0),p::paper_color(239)};}
Colors expected(bool g){
    if(g)return {rgb(36,36,36),rgb(48,48,48),rgb(28,28,28),rgb(244,244,244),rgb(181,154,232),rgb(184,184,184),rgb(255,255,255),rgb(112,112,112),rgb(48,48,48),rgb(184,184,184),rgb(112,112,112),rgb(255,255,255),rgb(36,36,36),rgb(36,36,36)};
    return {rgb(226,190,206),rgb(242,216,226),rgb(207,165,184),rgb(31,20,31),rgb(91,49,124),rgb(174,130,151),rgb(174,145,165),rgb(207,165,184),rgb(242,216,226),rgb(126,100,119),rgb(174,130,151),rgb(112,139,91),rgb(228,194,207),rgb(211,187,206)};
}
int main(){
    for(bool gray:{false,true,false}){
        p::set(gray?t::Id::Graphite:t::Id::Pink);assert(colors()==expected(gray));
        for(int y=0;y<240;++y){constexpr int64_t d=239LL*239*239;const int64_t n=int64_t(y)*y*(717-2*y);
            assert(p::paper_color(y)==(gray?rgb(36,36,36):rgb((228*d-17*n+d/2)/d,(194*d-7*n+d/2)/d,(207*d-n+d/2)/d)));}
        assert(p::paper_color(INT_MIN)==p::paper_color(0)&&p::paper_color(INT_MAX)==p::paper_color(239));
    }
    p::set(t::Id::Pink);p::animate_to(t::Id::Graphite,1000);assert(colors()==expected(false));
    p::capture(999);p::capture(1000);assert(colors()==expected(false));p::capture(1080);const auto mid=colors();assert(mid!=expected(false)&&mid!=expected(true));
    p::animate_to(t::Id::Pink,1080);p::capture(1080);assert(colors()==mid);p::capture(1250);assert(colors()==expected(false));
    p::animate_to(t::Id::Graphite,UINT64_MAX-170);p::capture(UINT64_MAX);assert(colors()==expected(true));
    p::set(t::Id::Pink);p::animate_to(t::Id::Graphite,100);p::capture(200);const auto direct=colors();
    p::set(t::Id::Pink);p::animate_to(t::Id::Graphite,100);for(int i=100;i<=200;++i)p::capture(i);assert(colors()==direct);
    for(int id=2;id<t::kCount;++id)for(int swatch=0;swatch<t::kSwatchCount;++swatch){
        t::Settings s;s.id=t::Id(id);s.custom={t::swatches[swatch],t::swatches[(swatch+8)%t::kSwatchCount],t::swatches[(swatch+16)%t::kSwatchCount]};
        p::set(s);const auto c=t::colors(s);assert(p::bg()==c.paper&&p::ink()==c.ink&&p::spot()==c.accent&&p::playhead()==c.accent);
        assert(p::cell()==t::mix(c.paper,c.ink,18)&&p::cell_dim()==t::mix(c.paper,c.ink,10));
        assert(p::ghost()==t::mix(c.paper,c.ink,184)&&p::subdued()==t::mix(c.paper,c.ink,79));
        assert(p::inverse()==t::inverse(c.accent));assert(p::shadow()==t::mix(c.paper,c.ink,92));
        for(int y:{INT_MIN,0,123,239,INT_MAX})assert(p::paper_color(y)==c.paper);
        const auto frozen=colors();p::capture(UINT64_MAX);assert(colors()==frozen);
    }
    p::set(t::Id(255));assert(colors()==expected(false));
    std::puts("pass current palette: 14 exact legacy endpoints, all paper rows, retarget/time/extremes, 72 custom/preset swatches; removed splash/shimmer not exercised");
}
