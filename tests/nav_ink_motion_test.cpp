// Current popup handoff, not the retired GO TO title morph.
#include "../platform/3ds/nav_ink_motion.h"
#include <cassert>
#include <cmath>
#include <climits>
#include <cstdio>
#include <vector>
namespace n = seance::platform::ui3ds::nav_ink;
struct Rect { int x,y,w,h,a; bool operator==(const Rect& b) const {return x==b.x&&y==b.y&&w==b.w&&h==b.h&&a==b.a;} };
static std::vector<Rect> rects;
namespace seance::platform::ui3ds::menu_style {
void paper_rect(int x,int y,int w,int h,int a) {assert(w>0&&h>0&&a>0&&a<=255);rects.push_back({x,y,w,h,a});}
}
static int oracle(int t,int d) {if(t<=0)return 0;if(t>=d)return 255;const double x=double(t)/d;return int(std::lround(255*x*x*(3-2*x)));}
int main() {
    assert(n::kDurationMs==200&&n::detail::kRevealMs==110);
    for(int d:{90,110,200})for(int t=-1;t<=d+1;++t)assert(n::detail::ease(t,d)==oracle(t,d));
    assert(n::detail::ease(INT_MIN,110)==0&&n::detail::ease(INT_MAX,110)==255);
    for(int target=0;target<6;++target)for(unsigned ms=0;ms<=201;++ms) {
        n::Frame f{n::Target(target),ms,true};rects.clear();n::draw_top_cover(f);const auto first=rects;
        if(ms>=200)assert(rects.empty());
        else {const int a=255-oracle(int(ms)-110,90);assert(rects.size()==unsigned(a>0));if(a)assert((rects[0]==Rect{0,0,400,240,a}));}
        rects.clear();n::draw_top_cover(f);assert(rects==first&&f.elapsed_ms==ms&&f.active);
        assert(n::detail::source_veil(int(ms))==oracle(int(ms),110));
        assert(n::detail::destination_alpha(int(ms))==oracle(int(ms)-110,90));
        f.active=false;rects.clear();n::draw_top_cover(f);assert(rects.empty());
    }
    rects.clear();n::draw_top_cover({n::Target::Chains,UINT_MAX,true});assert(rects.empty());
    n::detail::paper(320,0);assert(rects.empty());n::detail::paper(320,127);assert((rects.back()==Rect{0,0,320,240,127}));
    std::puts("pass nav handoff: 6 targets, all 200ms phases, exact easing/alpha, pure repeat, inactive/huge delta; no retired titles");
}
