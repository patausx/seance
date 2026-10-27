// Real menu_style + core draw/font TUs. Font oracle is independently rasterized
// from the OTF by Python; old heading trace is extracted from a pinned backup.
#include "../platform/3ds/menu_style.h"
#include "../platform/3ds/page_layout.h"
#include "../platform/3ds/ui_palette.h"
#include "../core/ui/draw.h"
#include "page_headings_reference.h"
#include "page_headings_legacy.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <new>

namespace m = seance::platform::ui3ds::menu_style;
namespace p = seance::platform::ui3ds::palette;
namespace l = seance::platform::ui3ds::page_layout;
namespace r = page_heading_reference;
namespace old = seance::platform::ui3ds::old_heading_reference;
static bool allocation_guard = false;
static unsigned long long guarded_allocations = 0;
extern "C" void* __real_malloc(size_t);
extern "C" void* __real_calloc(size_t, size_t);
extern "C" void* __real_realloc(void*, size_t);
extern "C" void* __wrap_malloc(size_t n) { if (allocation_guard) ++guarded_allocations; return __real_malloc(n); }
extern "C" void* __wrap_calloc(size_t n, size_t s) { if (allocation_guard) ++guarded_allocations; return __real_calloc(n, s); }
extern "C" void* __wrap_realloc(void* p, size_t n) { if (allocation_guard) ++guarded_allocations; return __real_realloc(p, n); }
void* operator new(size_t n) { if (allocation_guard) ++guarded_allocations; if (void* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new[](size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }

namespace {
unsigned long long checks=0, oracle_pixels=0, legacy_pairs=0, page_pairs=0, fallback_pairs=0, captures=0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"FAIL page-headings line %d: %s\n",__LINE__,#x); std::abort(); } } while (0)
using Image = std::array<uint32_t,400*240>;
struct Rect {
    int x,y,w,h; uint32_t color;
    bool operator==(const Rect& b) const { return x==b.x && y==b.y && w==b.w && h==b.h && color==b.color; }
};
Image image;
std::array<Rect,8192> objects;
size_t object_count=0, peak_legacy=0, peak_page=0;
int width=320;
constexpr uint32_t rgb(unsigned red,unsigned green,unsigned blue) { return 0xff000000u|red|(green<<8)|(blue<<16); }
uint32_t paper(bool graphite,int y) {
    if (graphite) return rgb(36,36,36);
    constexpr long long d=239LL*239*239;
    const long long t=1LL*y*y*(717-2*y);
    return rgb(unsigned((228*d-17*t+d/2)/d),unsigned((194*d-7*t+d/2)/d),unsigned((207*d-t+d/2)/d));
}
uint32_t blend(uint32_t foreground,uint32_t background) {
    const unsigned alpha=foreground>>24;
    uint32_t result=0xff000000u;
    for (int shift=0;shift<24;shift+=8)
        result|=((((foreground>>shift)&255)*alpha+((background>>shift)&255)*(255-alpha)+127)/255)<<shift;
    return result;
}
void clear(int w,bool graphite) {
    width=w; object_count=0;
    for (int y=0;y<240;++y) for (int x=0;x<400;++x) image[y*400+x]=x<w?paper(graphite,y):0x76543210u;
}
void reset_trace() { object_count=0; }
template<class F> void guarded(F draw) {
    allocation_guard=true;
    draw();
    allocation_guard=false;
    CHECK(guarded_allocations==0);
}
bool bit(const r::Mask& mask,int x,int y) {
    return x>=0 && x<mask.width && y>=0 && y<mask.height && r::pixels[mask.offset+y*mask.width+x];
}
void oracle(const r::Mask& mask,int left,int top,uint32_t color,bool graphite) {
    for (int y=0;y<240;++y) for (int x=0;x<400;++x) {
        const uint32_t bg=x<width?paper(graphite,y):0x76543210u;
        const uint32_t expected=x<width && bit(mask,x-left,y-top)?blend(color,bg):bg;
        CHECK(image[y*400+x]==expected); ++oracle_pixels;
    }
}
void save(const std::filesystem::path& path) {
    std::ofstream out(path,std::ios::binary); out<<"P6\n"<<width<<" 240\n255\n";
    for (int y=0;y<240;++y) for (int x=0;x<width;++x) {
        const uint32_t color=image[y*400+x]; const char bytes[]={char(color),char(color>>8),char(color>>16)}; out.write(bytes,3);
    }
    CHECK(out.good()); ++captures;
}
void metrics() {
    CHECK(l::kLeft==20 && l::kBaseline==32 && l::kTop==12 && l::kBottom==40 && l::kRightInset==20);
    CHECK(sizeof(l::Title)==1);
    for (const auto& title:r::titles) {
        const auto& mask=r::masks[title.index];
        CHECK(static_cast<unsigned>(title.title)==unsigned(title.index));
        CHECK(l::title_y(title.title)==32+mask.top);
        CHECK(32+mask.top>=12 && 32+mask.bottom<=40);
        CHECK(20+mask.width<=320-20);
        CHECK(mask.ppem==28 && mask.left==1);
        CHECK(m::heading_width(mask.text)==mask.width);
    }
}
void pages(bool graphite,const std::filesystem::path& out) {
    const uint32_t ink=graphite?rgb(244,244,244):rgb(31,20,31);
    const uint32_t spot=graphite?rgb(181,154,232):rgb(91,49,124);
    CHECK(m::ink()==ink && m::spot()==spot);
    for (int w:{320,400}) for (const auto& title:r::titles) for (uint32_t color:{ink,spot,0x7b563412u,0x00563412u}) {
        const auto& mask=r::masks[title.index];
        clear(w,graphite);
        guarded([&]{m::page_heading(title.title,color);});
        CHECK(object_count>0); peak_page=std::max(peak_page,object_count);
        oracle(mask,20,32+mask.top,color,graphite);
        const Image native=image; const auto trace=objects; const size_t count=object_count;
        if (w==320 && color==ink && !out.empty()) save(out/((graphite?"gray_":"pink_")+std::to_string(title.index)+".ppm"));
        clear(w,graphite);
        guarded([&]{old::heading(20,32+mask.top,mask.text,color);});
        CHECK(image==native && count<=object_count);
        // Current renderer deliberately retains the old merger exactly, not just
        // its budget. This catches hidden duplicate/overlapping submissions.
        CHECK(count==object_count && std::equal(trace.begin(),trace.begin()+count,objects.begin()));
        ++page_pairs;
    }
}
void legacy(bool graphite) {
    const uint32_t ink=graphite?rgb(244,244,244):rgb(31,20,31);
    constexpr int positions[][2]={{0,0},{7,73},{37,121},{101,180}};
    for (const auto& mask:r::masks) for (const auto& xy:positions) for (uint32_t color:{ink,0x915a3017u,0x005a3017u}) {
        const int x=xy[0],y=xy[1]; CHECK(x+mask.width<=400 && y+mask.height<=240);
        clear(400,graphite);
        guarded([&]{m::heading(x,y,mask.text,color);});
        CHECK(object_count>0); peak_legacy=std::max(peak_legacy,object_count);
        oracle(mask,x,y,color,graphite);
        const Image native=image; const auto trace=objects; const size_t count=object_count;
        clear(400,graphite);
        guarded([&]{old::heading(x,y,mask.text,color);});
        CHECK(image==native && count==object_count && std::equal(trace.begin(),trace.begin()+count,objects.begin()));
        CHECK(m::heading_width(mask.text)==old::heading_width(mask.text) && m::heading_width(mask.text)==mask.width);
        ++legacy_pairs;
    }
}
void fallback(bool graphite) {
    for (const char* text:{"bird","shift","HEADING?","x\ny","","not-a-baked-label"}) {
        clear(320,graphite);
        guarded([&]{m::heading(7,73,text,m::ink());});
        const Image native=image; const auto trace=objects; const size_t count=object_count;
        clear(320,graphite);
        guarded([&]{seance::ui::text(7,73,text,m::ink());});
        CHECK(image==native && count==object_count && std::equal(trace.begin(),trace.begin()+count,objects.begin()));
        CHECK(m::heading_width(text)==int(std::strlen(text))*6);
        clear(320,graphite);
        guarded([&]{old::heading(7,73,text,m::ink());});
        CHECK(image==native && count==object_count && std::equal(trace.begin(),trace.begin()+count,objects.begin()));
        ++fallback_pairs;
    }
    clear(320,graphite); const Image blank=image;
    guarded([&]{m::heading(20,13,nullptr,m::ink());});
    CHECK(image==blank && object_count==0 && m::heading_width(nullptr)==0);
    for (unsigned invalid:{52u,99u,255u}) {
        guarded([&]{m::page_heading(static_cast<l::Title>(invalid),m::ink());});
        CHECK(image==blank && object_count==0);
    }
    // No new page-heading strings or fonts enter the ordinary text fallback.
}
} // namespace

// Intercept the same Citro2D rectangle boundary used by the real core draw TU.
bool C2D_DrawRectSolid(float x,float y,float,float w,float h,uint32_t color) {
    CHECK(x==int(x) && y==int(y) && w==int(w) && h==int(h));
    CHECK(x>=0 && y>=0 && w>0 && h>0 && x+w<=width && y+h<=240);
    CHECK(object_count<objects.size());
    objects[object_count++]={int(x),int(y),int(w),int(h),color};
    for (int yy=int(y);yy<int(y+h);++yy) for (int xx=int(x);xx<int(x+w);++xx)
        image[yy*400+xx]=blend(color,image[yy*400+xx]);
    return true;
}

int main(int argc,char** argv) {
    CHECK(argc==1 || argc==2);
    const std::filesystem::path out=argc==2?argv[1]:"";
    if (!out.empty()) std::filesystem::create_directories(out);
    metrics();
    for (bool graphite:{false,true}) {
        p::set(graphite?p::Theme::Graphite:p::Theme::Pink);
        clear(320,graphite);
        guarded([&]{m::paper(320);});
        for (int y=0;y<240;++y) for (int x=0;x<320;++x) CHECK(image[y*400+x]==paper(graphite,y));
        reset_trace(); pages(graphite,out); legacy(graphite); fallback(graphite);
    }
    p::set(p::Theme::Pink); clear(320,false);
    guarded([&]{m::page_heading(l::Title::PatchTable,m::ink());});
    oracle(r::masks[0],20,13,rgb(31,20,31),false);
    CHECK(guarded_allocations==0);
    std::printf("pass page-headings: %llu checks; %llu independent font/native oracle pixels; %llu page old/new trace pairs; %llu legacy arbitrary-coordinate trace pairs; %llu fallback pairs; %llu actual native captures; peak page %zu / legacy %zu rectangles; %llu guarded allocations; 0 bounds; 14 titles / 52 masks / both themes\n",checks,oracle_pixels,page_pairs,legacy_pairs,fallback_pairs,captures,peak_page,peak_legacy,guarded_allocations);
}
