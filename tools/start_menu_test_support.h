#pragma once
// Current startup test support: actual C2D/font software raster and HID helpers.
// No archived controller, arrow or splash implementation is compiled here.
#include "theme_test_mode.h"
#include "../platform/3ds/ui_start_menu.h"
#include "../core/ui/draw.h"
#include "../core/ui/font.h"
#include <citro2d.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace seance::platform::ui3ds;
namespace {
struct Text { int x,y,w,h; std::string value; };
struct Image {
    int width=320, objects=0;
    bool fractional=false;
    std::vector<u32> pixels;
    std::vector<Text> texts;
    void reset(int w) { width=w;objects=0;fractional=false;pixels.assign(w*240,0);texts.clear(); }
};
Image top,bottom;
Image* surface=&bottom;
int peak=0,suites=0;
std::string dump_dir;
void require(bool condition,const char* reason) {
    if(!condition) {std::fprintf(stderr,"failed: %s\n",reason);std::abort();}
}
void none(StartMenuResult r) { require(r.action==StartMenuAction::None,"no external action"); }
void action(StartMenuResult r,StartMenuAction a,int slot=-1) {
    require(r.action==a,"expected action");require(r.slot==slot,"expected slot");
}
void neutral() { none(start_menu_input(0,0,0,0)); }
void open(const StartMenuData& data) { start_menu_open(data);neutral();start_menu_tick(1000); }
StartMenuResult key(u32 button) {
    const auto result=start_menu_input(button,button,0,0);
    neutral();start_menu_tick(1000);return result;
}
StartMenuResult tap(int x,int y) {
    none(start_menu_input(KEY_TOUCH,KEY_TOUCH,x,y));
    const auto result=start_menu_input(0,0,0,0); // actual HID lift has no coordinates
    neutral();start_menu_tick(1000);return result;
}
bool has(const char* text) {
    for(const auto& t:bottom.texts)if(t.value==text)return true;
    return false;
}
u32 expected_paper(int y) { return theme_test::paper(y); }
std::vector<u32> region(const Image& image,int x,int y,int w,int h) {
    std::vector<u32> pixels;
    for(int row=y;row<y+h;++row)
        pixels.insert(pixels.end(),image.pixels.begin()+row*image.width+x,
                      image.pixels.begin()+row*image.width+x+w);
    return pixels;
}
void ppm(const Image& image,const std::string& file) {
    std::ofstream out(file,std::ios::binary);out<<"P6\n"<<image.width<<" 240\n255\n";
    for(u32 p:image.pixels) {
        const char rgb[]={char(p&255),char((p>>8)&255),char((p>>16)&255)};out.write(rgb,3);
    }
}
void pass(const char* name) { std::printf("ok %s\n",name);++suites; }
} // namespace

extern "C" void __real__ZN6seance2ui4textEiiPKcji(int,int,const char*,u32,int);
extern "C" void __wrap__ZN6seance2ui4textEiiPKcji(int x,int y,const char* text,u32 color,int scale) {
    if(color>>24)surface->texts.push_back({x,y,int(std::strlen(text))*6*scale,8*scale,text});
    __real__ZN6seance2ui4textEiiPKcji(x,y,text,color,scale);
}
bool C2D_DrawRectSolid(float fx,float fy,float,float fw,float fh,u32 color) {
    surface->fractional|=std::floor(fx)!=fx||std::floor(fy)!=fy||std::floor(fw)!=fw||std::floor(fh)!=fh;
    require(fw>0&&fh>0&&fx>=0&&fy>=0&&fx+fw<=surface->width&&fy+fh<=240,"primitive bounds");
    const int x=int(std::ceil(fx-.5f)),y=int(std::ceil(fy-.5f));
    const int w=int(std::ceil(fx+fw-.5f))-x,h=int(std::ceil(fy+fh-.5f))-y;
    ++surface->objects;
    const unsigned alpha=color>>24;
    for(int py=y;py<y+h;++py)for(int px=x;px<x+w;++px) {
        auto& dst=surface->pixels[py*surface->width+px];u32 result=0;
        for(int shift=0;shift<24;shift+=8)
            result|=(((color>>shift&255)*alpha+(dst>>shift&255)*(255-alpha)+127)/255)<<shift;
        const unsigned da=dst>>24;
        dst=result|((alpha+(da*(255-alpha)+127)/255)<<24);
    }
    return true;
}
bool C2D_DrawLine(float,float,u32,float,float,u32,float,float) {
    require(false,"startup renderer uses deterministic rect coverage only");return false;
}
