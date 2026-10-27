// Real common drawing + inverse replay at the GPU boundary. No image exports.
#include "../core/ui/draw.h"
#include "../platform/3ds/step_focus_ink.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
namespace ui = seance::ui;
namespace sf = seance::platform::ui3ds::step_focus;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"pixel strokes:%d: %s\n",__LINE__,#x); std::abort(); } } while (0)
constexpr int W=400,H=240;
static std::array<u32,W*H> pixels{};
static std::array<unsigned,W*H> writes{};
static int objects=0,lines=0;
struct Rect { float x,y,w,h; } last{};
static void reset() { pixels.fill(0); writes.fill(0); objects=lines=0; }
bool C2D_DrawRectSolid(float x,float y,float z,float w,float h,u32 c) {
    CHECK(z==0 && x==std::floor(x) && y==std::floor(y));
    CHECK(w==std::floor(w) && h==std::floor(h) && w>0 && h>0);
    ++objects; last={x,y,w,h};
    for(int yy=std::max(0,int(y));yy<std::min(H,int(y+h));++yy)
        for(int xx=std::max(0,int(x));xx<std::min(W,int(x+w));++xx) {
            pixels[yy*W+xx]=c; ++writes[yy*W+xx];
        }
    return true;
}
bool C2D_DrawLine(float x0,float y0,u32 c0,float x1,float y1,u32 c1,float t,float z) {
    CHECK(x0!=x1 && y0!=y1 && c0==c1 && t>=1 && z==0);
    CHECK(x0==7 && y0==11 && x1==31 && y1==27 && t==2);
    ++objects; ++lines; return true; // non-axis geometry remains untouched
}
bool C2D_DrawTriangle(float,float,u32,float,float,u32,float,float,u32,float) {
    CHECK(false); return false; // axis inverse replay must use matching rectangles
}
static bool in(sf::Box b,int x,int y) {
    if(y<b.y || y>=b.y+b.h)return false;
    const int row=y-b.y,inset=(row==0||row==b.h-1)?2:(row==1||row==b.h-2)?1:0;
    return x>=b.x+inset && x<b.x+b.w-inset;
}
static void axis_and_inverse() {
    const int cases[][4]={{7,20,390,20},{390,20,7,20},{20,7,20,230},
        {20,230,20,7},{20,20,20,20},{-12,20,30,20},{20,-12,20,30}};
    for(const auto& a:cases)for(int requested:{0,1,2,3}) {
        const int t=std::max(1,requested);
        int left=std::min(a[0],a[2]),right=std::max(a[0],a[2]);
        int top=std::min(a[1],a[3]),bottom=std::max(a[1],a[3]);
        if(a[0]==a[2]) {left-=t/2;right=left+t-1;}
        if(a[1]==a[3]) {top-=t/2;bottom=top+t-1;}
        reset();ui::line(a[0],a[1],a[2],a[3],0x80605040,requested);
        CHECK(objects==1 && lines==0);
        for(int y=0;y<H;++y)for(int x=0;x<W;++x)
            CHECK(writes[y*W+x]==unsigned(x>=left&&x<=right&&y>=top&&y<=bottom));
        reset();const sf::Box mask={16,16,23,17};
        sf::clipped_line(mask,a[0],a[1],a[2],a[3],0x80605040,requested);
        CHECK(objects<=5 && lines==0);
        for(int y=0;y<40;++y)for(int x=0;x<45;++x)
            CHECK(writes[y*W+x]==unsigned(in(mask,x,y)&&x>=left&&x<=right&&y>=top&&y<=bottom));
    }
    reset();ui::line(7,11,31,27,0xff060504,2);CHECK(objects==1&&lines==1);
}
static void frames() {
    for(float phase:{0.f,.25f,.5f,.75f})for(int w:{1,2,18})for(int h:{1,2,33}) {
        reset();const float x=10+phase,y=12+phase;
        ui::frame(x,y,w,h,0x80605040);CHECK(objects<=4 && lines==0);
        const int left=int(std::floor(double(x)+.5)),top=int(std::floor(double(y)+.5));
        for(int yy=0;yy<50;++yy)for(int xx=0;xx<40;++xx) {
            const bool inside=xx>=left&&xx<left+w&&yy>=top&&yy<top+h;
            const bool edge=xx==left||xx==left+w-1||yy==top||yy==top+h-1;
            CHECK(writes[yy*W+xx]==unsigned(inside&&edge)); // includes alpha corners
        }
    }
    reset();ui::frame(-.75f,-.75f,3,3,7);
    CHECK(writes[0]==0 && writes[W+1]==1 && writes[1]==1 && writes[W]==1);
    reset();ui::frame(4,4,0,20,7);ui::frame(4,4,20,-1,7);CHECK(objects==0);
    // Exact sequencer selection size/pitch: no doubled edges or closed inter-cell gap.
    reset();ui::frame(8,43,18,33,7);ui::frame(27,43,18,33,7);
    for(int y=43;y<76;++y)CHECK(writes[y*W+25]==1&&writes[y*W+26]==0&&writes[y*W+27]==1);
}
static void rails_and_sliders() {
    for(bool bi:{false,true})for(int n:{-32768,-32767,0,12001,32767,50000}) {
        reset();ui::hslider(10.25f,20.25f,31,9,n,bi,7,9);
        CHECK(objects<=4 && lines==0 && last.w==3 && last.h==11);
        if(n==0)CHECK(last.x==(bi?25:9));
        if(n<=-32767)CHECK(last.x==9);
        if(n>=32767)CHECK(last.x==40);
    }
    reset();ui::hslider(0,0,0,8,0,false,7,9);CHECK(objects==0);
}
int main() {
    axis_and_inverse();frames();rails_and_sliders();
    std::puts("pass pixel strokes: inclusive/reversed/zero/1-3px axes; clipped parity; snapped disjoint frames; odd/fractional horizontal slider endpoints; bounded objects");
    std::puts("host primitive contract only; not PICA coverage, device FPS or audio evidence; no exports");
}
