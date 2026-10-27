// Focused pure-motion + native-primitive proof. Test buffers are not runtime assets.
#include "../platform/3ds/step_focus_ink.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
using namespace seance::platform::ui3ds;
namespace sf = step_focus;
static bool guarded=false;
static unsigned allocations=0;
extern "C" void* __real_malloc(size_t);
extern "C" void* __real_calloc(size_t,size_t);
extern "C" void* __real_realloc(void*,size_t);
extern "C" void* __wrap_malloc(size_t n){if(guarded)++allocations;return __real_malloc(n);}
extern "C" void* __wrap_calloc(size_t n,size_t s){if(guarded)++allocations;return __real_calloc(n,s);}
extern "C" void* __wrap_realloc(void* p,size_t n){if(guarded)++allocations;return __real_realloc(p,n);}
void* operator new(size_t n){if(guarded)++allocations;if(void* p=__real_malloc(n))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,size_t)noexcept{std::free(p);}
void operator delete[](void* p,size_t)noexcept{std::free(p);}
static unsigned long long checks=0,geometry_checks=0,glyph_pixels=0,line_points=0;
#define REQUIRE(x) do{++checks;if(!(x)){guarded=false;std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static uint32_t screen[64*64],base[64*64];
static unsigned objects=0,peak=0;
struct Tri {double x[3],y[3];};
static Tri triangles[64];static unsigned triangle_count=0;
bool C2D_DrawRectSolid(float x,float y,float,float w,float h,u32 color){
    ++objects;peak=std::max(peak,objects);
    REQUIRE(x==std::floor(x)&&y==std::floor(y)&&w==std::floor(w)&&h==std::floor(h));
    REQUIRE(x>=0&&y>=0&&x+w<=64&&y+h<=64&&w>0&&h>0);
    for(int yy=int(y);yy<int(y+h);++yy)for(int xx=int(x);xx<int(x+w);++xx)screen[yy*64+xx]=color;
    return true;
}
bool C2D_DrawLine(float,float,u32,float,float,u32,float,float){REQUIRE(false);return false;}
bool C2D_DrawTriangle(float x0,float y0,u32 c0,float x1,float y1,u32 c1,float x2,float y2,u32 c2,float){
    REQUIRE(c0==c1&&c1==c2);REQUIRE(triangle_count<64);
    triangles[triangle_count++]={{x0,x1,x2},{y0,y1,y2}};return true;
}
static bool covered(sf::Box b,int x,int y){
    int row=y-b.y;if(row<0||row>=b.h)return false;
    int inset=row==0||row==b.h-1?2:row==1||row==b.h-2?1:0;
    return x>=b.x+inset&&x<b.x+b.w-inset;
}
static void test_geometry(){
    for(int a=0;a<64;++a)for(int b=0;b<64;++b)for(unsigned ms=0;ms<=100;++ms){
        sf::Pose p=sf::target(a/16,a%16),q=sf::target(b/16,b%16),v=sf::geometry(p,q,ms);
        double t=ms/100.,e=1-std::pow(1-t,3),pulse=std::sin(std::acos(-1.)*t)*(1-t)*.7;
        double stretch=std::min(std::abs(double(q.x-p.x)),12.)*pulse,squash=3*pulse;
        const double ref[]={p.x+(q.x-p.x)*e-stretch/2,p.y+(q.y-p.y)*e+squash/2,18+stretch,18-squash};
        const float actual[]={v.x,v.y,v.w,v.h};
        for(int i=0;i<4;++i){REQUIRE(std::abs(actual[i]-ref[i])<.0001);++geometry_checks;}
        auto r=sf::raster(v);
        REQUIRE(r.x==int(std::floor(ref[0]+.5))&&r.y==int(std::floor(ref[1]+.5))&&r.w==int(std::floor(ref[2]+.5))&&r.h==int(std::floor(ref[3]+.5)));
        REQUIRE(r.x>=0&&r.x+r.w<=320&&r.y>=41&&r.y+r.h<=192);
    }
    sf::State s;auto same=[](sf::Box a,sf::Box b){return a.x==b.x&&a.y==b.y&&a.w==b.w&&a.h==b.h;};
    REQUIRE(same(s.update(1000,0,0,0),sf::raster(sf::target(0,0))));
    auto first=s.update(1001,0,3,15);REQUIRE(same(first,sf::raster(sf::target(0,0))));
    auto displayed=s.update(1040,0,3,15);auto now=s.update(1070,0,2,1);
    REQUIRE(same(now,displayed)); // not a hidden extrapolated pose at1070
    REQUIRE(same(s.update(1070,0,2,1),now));
    REQUIRE(same(s.update(1170,0,2,1),sf::raster(sf::target(2,1))));
    for(int i=0;i<20000;++i){
        uint64_t when=1200+i*2;s.update(when,0,i%4,i%16);
        auto box=s.update(when+1,0,i%4,i%16);
        REQUIRE(box.w<=30&&box.h>=12&&box.x>=0&&box.x+box.w<=320&&box.y>=41&&box.y+box.h<=192);
    }
    REQUIRE(same(s.update(50000,0,1,3),sf::raster(sf::target(1,3)))); // pause
    REQUIRE(same(s.update(49999,0,3,0),sf::raster(sf::target(3,0)))); // backwards
    REQUIRE(same(s.update(50000,1,1,12),sf::raster(sf::target(1,12)))); // changed context
    s.reset();REQUIRE(same(s.update(50000,1,2,4),sf::raster(sf::target(2,4))));
    REQUIRE(same(s.update(UINT64_MAX-1,1,0,3),sf::raster(sf::target(0,3))));
    REQUIRE(same(s.update(0,1,3,10),sf::raster(sf::target(3,10))));
}
static void test_glyphs(){
    for(int c=32;c<128;++c)for(int ox=-8;ox<=26;ox+=2)for(int oy=-8;oy<=18;oy+=2){
        char label[2]={char(c),0};std::memset(screen,0,sizeof screen);objects=0;
        // Real original core/ui/draw.cpp provides the independent glyph substrate.
        seance::ui::text(16+ox,16+oy,label,1);std::memcpy(base,screen,sizeof screen);
        sf::Box mask={16,16,18+(c%13),12+c%7};sf::fill(mask,2);
        sf::clipped_text(mask,16+ox,16+oy,label,3);
        for(int y=6;y<46;++y)for(int x=6;x<52;++x){
            const auto want=covered(mask,x,y)?(base[y*64+x]?3u:2u):base[y*64+x];
            REQUIRE(screen[y*64+x]==want);++glyph_pixels;
        }
    }
    // Multi-character fixed-origin notes, signs, octave, newline; corner/edge runs.
    const char* labels[]={"A#","C2","r-","100","--","??","!f","2\n-3"};
    for(const char* label:labels)for(int x=5;x<40;++x){
        std::memset(screen,0,sizeof screen);objects=0;seance::ui::text(x,15,label,1);std::memcpy(base,screen,sizeof screen);
        sf::Box mask={16,16,23,17};sf::fill(mask,2);sf::clipped_text(mask,x,15,label,3);
        for(int y=5;y<44;++y)for(int xx=0;xx<62;++xx){
            REQUIRE(screen[y*64+xx]==(covered(mask,xx,y)?(base[y*64+xx]?3u:2u):base[y*64+xx]));++glyph_pixels;
        }
    }
}
static bool in_triangle(const Tri& t,double x,double y){
    bool positive=false,negative=false;
    for(int i=0;i<3;++i){int j=(i+1)%3;double d=(t.x[j]-t.x[i])*(y-t.y[i])-(t.y[j]-t.y[i])*(x-t.x[i]);positive|=d>1e-8;negative|=d< -1e-8;}
    return !(positive&&negative);
}
static void test_lines(){
    for(int dx=-40;dx<=40;dx+=5)for(int dy=-40;dy<=40;dy+=5)for(int width=1;width<=3;++width){
        const int x0=25-dx/2,y0=25-dy/2,x1=x0+dx,y1=y0+dy;
        sf::Box mask={16,16,23,17};triangle_count=0;objects=0;std::memset(screen,0,sizeof screen);
        sf::clipped_line(mask,x0,y0,x1,y1,3,width);
        if(!dx || !dy) {
            REQUIRE(triangle_count==0 && objects<=5);
            const int left=dx?std::min(x0,x1):x0-width/2;
            const int right=dx?std::max(x0,x1):left+width-1;
            const int top=dy?std::min(y0,y1):y0-width/2;
            const int bottom=dy?std::max(y0,y1):top+width-1;
            for(int py=12;py<37;++py)for(int px=12;px<43;++px) {
                const bool wanted=covered(mask,px,py)&&px>=left&&px<=right&&py>=top&&py<=bottom;
                REQUIRE(screen[py*64+px]==(wanted?3u:0u));++line_points;
            }
            continue;
        }
        REQUIRE(triangle_count<=30);
        for(unsigned i=0;i<triangle_count;++i)for(int j=0;j<3;++j){
            REQUIRE(triangles[i].x[j]>=mask.x-.0001&&triangles[i].x[j]<=mask.x+mask.w+.0001&&triangles[i].y[j]>=mask.y-.0001&&triangles[i].y[j]<=mask.y+mask.h+.0001);
        }
        const double length2=double(dx)*dx+double(dy)*dy;
        for(int py=12;py<37;++py)for(int px=12;px<43;++px){
            double x=px+.371,y=py+.613,along=((x-x0)*dx+(y-y0)*dy)/length2;
            double cross=(x-x0)*dy-(y-y0)*dx;
            bool stroke=along>=0&&along<=1&&cross*cross<=width*width*.25*length2;
            bool wanted=stroke&&covered(mask,px,py),actual=false;
            for(unsigned i=0;i<triangle_count;++i)actual|=in_triangle(triangles[i],x,y);
            REQUIRE(actual==wanted);++line_points;
        }
    }
}
int main(){
    guarded=true;test_geometry();test_glyphs();test_lines();guarded=false;
    REQUIRE(allocations==0);
    std::printf("PASS %llu checks; %llu geometry scalars + exact rasters; %llu native glyph pixels; %llu independent quad/triangle coverage points; allocations %u; state %zu bytes; native glyph+plate peak %u rects\n",checks,geometry_checks,glyph_pixels,line_points,allocations,sizeof(sf::State),peak);
}
