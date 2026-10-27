// Focused dual-screen blur/clip contract. No image exports or GPU claims.
/* g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined
     -fno-omit-frame-pointer -fno-pie -no-pie tests/sigil_surface_test.cpp
     core/ui/font.cpp -o /tmp/seance-surface-test && /tmp/seance-surface-test */
#include "../platform/3ds/sigil_surface.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <type_traits>
#include <vector>
namespace sf=seance::platform::ui3ds::sigil_surface;
namespace ms=seance::platform::ui3ds::menu_style;
namespace labels=seance::platform::ui3ds::menu_labels;
namespace ui=seance::ui;
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"surface:%d: %s\n",__LINE__,#x);std::abort();}} while(0)
constexpr int NativeW=400,NativeH=240;
static std::array<uint32_t,NativeW*NativeH> pixels{};
static std::array<unsigned,NativeW*NativeH> writes{};
static unsigned text_calls=0,heading_calls=0,line_calls=0;
static void reset(){pixels.fill(0);writes.fill(0);text_calls=heading_calls=line_calls=0;}
namespace seance::ui {
void rect(float x,float y,float w,float h,uint32_t c){
    CHECK(x==int(x)&&y==int(y)&&w==int(w)&&h==int(h)&&w>0&&h>0);
    for(int yy=std::max(0,int(y));yy<std::min(NativeH,int(y+h));++yy)
        for(int xx=std::max(0,int(x));xx<std::min(NativeW,int(x+w));++xx){pixels[yy*NativeW+xx]=c;++writes[yy*NativeW+xx];}
}
int text_w(const char* s,int scale){return s?int(std::strlen(s))*6*scale:0;}
void text(int x,int y,const char* s,uint32_t c,int scale){
    CHECK(scale==1);++text_calls;
    for(;*s;++s,x+=6)for(int row=0;row<8;++row)for(int col=0;col<6;++col)
        if(trackr::ui::glyph_row(*s,row)&(1<<(5-col)))rect(x+col,y+row,1,1,c);
}
void line(int x0,int y0,int x1,int y1,uint32_t c,int thickness){
    CHECK(thickness==1);++line_calls;
    int dx=std::abs(x1-x0),sx=x0<x1?1:-1,dy=-std::abs(y1-y0),sy=y0<y1?1:-1,err=dx+dy;
    for(;;){rect(x0,y0,1,1,c);if(x0==x1&&y0==y1)break;const int e=err*2;if(e>=dy){err+=dy;x0+=sx;}if(e<=dx){err+=dx;y0+=sy;}}
}
}
namespace seance::platform::ui3ds::menu_style {
uint32_t paper_color(int y){return 0xffc0d0e0u+unsigned(y/16);}
void heading(int x,int y,const char* s,uint32_t c){
    ++heading_calls;
    for(const auto& l:menu_labels::labels)if(!std::strcmp(l.text,s)){
        const int stride=(l.width+7)/8;const auto* bits=menu_labels::bitmap+l.offset;
        for(int row=0;row<l.height;++row)for(int col=0;col<l.width;++col)
            if(bits[row*stride+col/8]&(0x80>>(col&7)))ui::rect(x+col,y+row,1,1,c);
        return;
    }
    ui::text(x,y,s,c);
}
}
static void native_clips(){
    static_assert(std::is_aggregate<sf::Painter>::value&&std::is_aggregate<sf::TopPainter>::value,"aggregate API");
    sf::TopPainter all;
    const sf::TopPainter crop{nullptr,31,23,79,48};
    auto check_crop=[&](auto draw){
        reset();draw(all);const auto expected=pixels;
        reset();draw(crop);
        for(int y=0;y<NativeH;++y)for(int x=0;x<NativeW;++x)
            CHECK(pixels[y*NativeW+x]==((x>=31&&x<79&&y>=23&&y<48)?expected[y*NativeW+x]:0));
    };
    check_crop([](const auto& p){p.rect(10,12,140,75,0x80fefdfc);});
    for(const auto& a:std::array<std::array<int,4>,5>{{{10,30,110,30},{110,30,10,30},{45,60,45,10},{45,30,45,30},{10,14,100,60}}})
        check_crop([&](const auto& p){p.line(a[0],a[1],a[2],a[3],0x80fefdfc);});
    check_crop([](const auto& p){p.frame(21,20,63,35,0x80fefdfc);});
    check_crop([](const auto& p){p.text(27,21,"ring chorus",0xfffefdfc);});
    check_crop([](const auto& p){p.heading(24,19,"sigils",0xfffefdfc);});
    check_crop([](const auto& p){p.heading(24,43,"unknown fallback",0xfffefdfc);});
    reset();all.text(50,50,"fast text",1);all.heading(50,70,"sigils",1);all.line(50,100,75,115,1);
    CHECK(text_calls==1&&heading_calls==1&&line_calls==1);
    reset();crop.text(27,21,"partial",1);crop.heading(24,19,"sigils",1);crop.line(10,14,100,60,1);
    CHECK(text_calls==0&&heading_calls==0&&line_calls==0);
    reset();sf::Painter{}.rect(315,235,20,20,1);
    CHECK(pixels[239*NativeW+319]==1&&pixels[239*NativeW+320]==0);
    reset();sf::TopPainter{}.rect(395,235,20,20,1);CHECK(pixels[239*NativeW+399]==1);
    reset();sf::TopPainter{nullptr,80,50,30,10}.rect(0,0,400,240,1);
    CHECK(std::all_of(pixels.begin(),pixels.end(),[](auto v){return v==0;}));
    reset();crop.frame(31,23,10,10,0x80fefdfc);
    CHECK(*std::max_element(writes.begin(),writes.end())==1);
    int heading_width=0;for(const auto& l:labels::labels)if(!std::strcmp(l.text,"sigils"))heading_width=l.width;
    CHECK(heading_width>0);reset();CHECK(crop.heading(24,19,"sigils",1)==24+heading_width);
    all.text(0,0,nullptr,1);CHECK(all.heading(5,5,nullptr,1)==5);
}
static uint32_t blend(uint32_t a,uint32_t b,unsigned weight,unsigned total){
    uint32_t out=0xff000000;
    for(unsigned s=0;s<24;s+=8)out|=((((a>>s)&255)*(total-weight)+((b>>s)&255)*weight+total/2)/total)<<s;
    return out;
}
template<int Width> static void cache_contract(sf::BasicCache<Width>& cache){
    using P=sf::BasicPainter<Width>;
    cache.clear();
    for(int y=0;y<sf::H;++y)for(int x=0;x<Width;++x)CHECK(cache.pixels[y*Width+x]==ms::paper_color(y*2));
    // Odd tile edges constrain exact quarter-pixel box coverage on both screens.
    P{&cache,3,5,9,11}.rect(-10,-10,500,300,0x80907030);
    for(int y=0;y<sf::H;++y)for(int x=0;x<Width;++x){
        const unsigned area=std::max(0,std::min(9,x*2+2)-std::max(3,x*2))*std::max(0,std::min(11,y*2+2)-std::max(5,y*2));
        CHECK(cache.pixels[y*Width+x]==blend(ms::paper_color(y*2),0x80907030,area*128,1020));
    }
    // Old sigils line raster blended individual pixels; retain it byte-for-byte.
    cache.clear();P{&cache}.line(2,2,3,2,0xff000000);
    CHECK(cache.pixels[Width+1]==blend(blend(ms::paper_color(2),0xff000000,255,1020),0xff000000,255,1020));
    const std::vector<uint32_t> before(cache.pixels,cache.pixels+Width*sf::H);
    P{&cache,5,5,7,7}.text(-5,0,"clip",0xff030201);
    P{&cache,5,5,7,7}.heading(-10,-10,"sigils",0xff030201);
    for(int y=0;y<sf::H;++y)for(int x=0;x<Width;++x)
        if(x<2||x>3||y<2||y>3)CHECK(cache.pixels[y*Width+x]==before[y*Width+x]);
    // Reference exact separable integer rounding, replicated borders, 24% paper.
    for(int y=0;y<sf::H;++y)for(int x=0;x<Width;++x)
        cache.pixels[y*Width+x]=0xff000000u|unsigned((x*7919+y*65521)&0xffffff);
    const std::vector<uint32_t> source(cache.pixels,cache.pixels+Width*sf::H);
    cache.finish();CHECK(cache.valid&&cache.pending);
    for(int y=0;y<sf::H;++y)for(int x=0;x<Width;++x){
        uint32_t expected=0xff000000;
        for(unsigned s=0;s<24;s+=8){
            unsigned rows[3]={};
            for(int k=0;k<3;++k){const int yy=std::clamp(y+k-1,0,sf::H-1);
                const unsigned l=(source[yy*Width+std::max(0,x-1)]>>s)&255;
                const unsigned m=(source[yy*Width+x]>>s)&255;
                const unsigned r=(source[yy*Width+std::min(Width-1,x+1)]>>s)&255;
                rows[k]=(l+2*m+r+2)/4;
            }
            const unsigned channel=(rows[0]+2*rows[1]+rows[2]+2)/4;
            const unsigned paper=(ms::paper_color(y*2)>>s)&255;
            expected|=((channel*76+paper*24+50)/100)<<s;
        }
        CHECK(cache.pixels[y*Width+x]==expected);
    }
    cache.upload();CHECK(!cache.pending&&cache.valid);
    reset();CHECK(cache.draw());CHECK(pixels[(NativeH-1)*NativeW+Width*2-1]==cache.pixels[Width*sf::H-1]);
}
int main(){
    native_clips();cache_contract(sf::bottom_cache);
    const std::vector<uint32_t> bottom(sf::bottom_cache.pixels,sf::bottom_cache.pixels+sf::W*sf::H);
    cache_contract(sf::top_cache);
    CHECK(std::equal(bottom.begin(),bottom.end(),sf::bottom_cache.pixels));
    CHECK(sizeof(sf::bottom_cache.pixels)+sizeof(sf::top_cache.pixels)+sizeof(sf::scratch)==268800);
    sf::bottom_cache.shutdown();sf::top_cache.shutdown();
    CHECK(!sf::bottom_cache.valid&&!sf::top_cache.valid&&!sf::bottom_cache.pending&&!sf::top_cache.pending);
    std::puts("pass dual-screen surface: exact kernel/tint/legacy line coverage; shared scratch independence; native/cache clipping; glyph fast paths; 268800-byte CPU pixel budget");
    std::puts("host ASan/UBSan only; no GPU allocation, FPS, device audio or media evidence");
}
