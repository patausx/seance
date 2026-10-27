#pragma once
// FM editor-only projection and typography. No audio, engine state, heap or clock.
// Stored parameter laws stay in FmUnit; the decay transfer below is an editing aid.
#include <algorithm>
#include <cstdint>
#include <cstring>
#include "note_display_data.h"
#include "../../core/ui/draw.h"

namespace seance::platform::ui3ds::fm_visual {
constexpr int kColumns=372, kEnvelopeHeight=76;
using Curve = uint8_t[kColumns];
inline int uni(int value) {return std::max(0,std::min(32767,value));}
inline int decay_step(int raw) {return 16+(32767-uni(raw))*496/32767;}
inline int nominal_decay_ms(int raw) {return 7073/decay_step(raw);}
// 33 points of round((512-512*2^(-5*i/32))*32767/496). Integer interpolation
// spreads the nominal time over the rail, without exp/log or audio remapping.
inline constexpr int decay_knots[]={
    0,3472,6587,9383,11892,14143,16163,17976,19603,21063,22372,
    23548,24603,25549,26399,27161,27845,28458,29009,29503,29947,
    30345,30702,31022,31310,31568,31800,32007,32194,32361,32511,32646,32767
};
inline int decay_position(int raw) {
    raw=uni(raw);if(raw==32767)return 32767;
    int i=0;while(i<31&&raw>=decay_knots[i+1])++i;
    const int width=decay_knots[i+1]-decay_knots[i];
    return int((int64_t(i)*width+raw-decay_knots[i])*32767/(32*width));
}
inline int16_t decay_from_position(int position) {
    position=uni(position);if(position==32767)return 32767;
    const int phase=position*32,i=phase/32767,part=phase%32767;
    return int16_t(decay_knots[i]+(int64_t(decay_knots[i+1]-decay_knots[i])*part+16383)/32767);
}
inline int16_t decay_drag(int16_t origin,int pixels,int extent=292) {
    // An untouched control and a return to the touchdown coordinate are exact;
    // never round-trip an arbitrary saved q15 base merely to select it.
    return pixels==0?origin:decay_from_position(decay_position(origin)+pixels*32767/extent);
}
inline int decay_tick(int milliseconds) {
    const int step=std::max(16,std::min(512,7073/std::max(1,milliseconds)));
    return decay_position(32767-(step-16)*32767/496);
}
inline void amplitude(Curve& out,int attack,int decay) {
    // Same scalar recurrence as the existing strike envelope, not output PCM.
    // Fixed 300ms includes the integer floor tail even at maximum decay/attack.
    const int attack_samples=1+uni(attack)*639/32767;
    const int attack_step=(32766+attack_samples-1)/attack_samples;
    const int step=decay_step(decay);
    bool attacking=attack>0;int envelope=attacking?1:32767,sample=0;
    for(int col=0;col<kColumns;++col) {
        const int target=col*9600/(kColumns-1);
        while(sample<target) {
            if(attacking) {envelope+=attack_step;if(envelope>=32767){envelope=32767;attacking=false;}}
            else if(envelope>0)envelope=std::max(0,envelope-std::max(1,(envelope*step)>>15));
            ++sample;
        }
        out[col]=uint8_t((envelope*kEnvelopeHeight+16383)/32767);
    }
}
inline void falling(Curve& out,int raw) {
    // Index/feedback fall: zero means off (sustained), not an instantaneous fall.
    const int step=raw>0?8+uni(raw)*248/32767:0;
    int envelope=32767,sample=0;
    for(int col=0;col<kColumns;++col) {
        const int target=col*19200/(kColumns-1); // fixed 600ms
        while(sample<target) {
            if(step&&envelope>0)envelope=std::max(0,envelope-std::max(1,(envelope*step)>>15));
            ++sample;
        }
        out[col]=uint8_t((envelope*kEnvelopeHeight+16383)/32767);
    }
}

// Body-only 82% raster treatment, approved in the lighter-voice study. The
// original Note/title bitmaps, shared renderers and global frame are unchanged.
inline const note_display::Glyph* glyph(char ch,bool small=false) {
    for(const auto& g:note_display::glyphs)if(g.character==ch&&g.small==small)return &g;
    return nullptr;
}
inline int scaled(int value,int scale) {
    return value<0?-((-value*scale+50)/100):(value*scale+50)/100;
}
inline int draw_glyph(int x,int baseline,const note_display::Glyph& g,uint32_t color,int scale) {
    const int width=scaled(g.width,scale),height=scaled(g.height,scale),top=scaled(g.top,scale);
    const int stride=(g.width+7)/8;
    for(int y=0;y<height;++y) {
        const int row=(2*y+1)*g.height/(2*height);int run=-1;
        for(int col=0;col<=width;++col) {
            const int source=col<width?(2*col+1)*g.width/(2*width):0;
            const bool on=col<width&&(note_display::bitmap[g.offset+row*stride+source/8]&(128>>(source&7)));
            if(on&&run<0)run=col;
            if(!on&&run>=0){ui::rect(x+run,baseline+top+y,col-run,1,color);run=-1;}
        }
    }
    return x+width+2;
}
inline int numeric_width(const char* text,bool lighter=true) {
    const int scale=lighter?82:100;int width=0;
    for(const char* p=text;*p;++p) {
        if(*p=='.')width+=lighter?5:6;
        else if(*p=='+'||*p=='-')width+=lighter?15:18;
        else if(const auto* g=glyph(*p);g&&*p>='0'&&*p<='9')width+=scaled(g->width,scale)+2;
        else return 10000;
    }
    return width;
}
inline int number(int x,int baseline,const char* text,uint32_t ink,uint32_t accent,bool lighter=true) {
    const int scale=lighter?82:100;
    for(const char* p=text;*p;++p) {
        if(*p=='.'){ui::rect(x,baseline-3,3,3,ink);x+=lighter?5:6;}
        else if(*p=='+'||*p=='-') {
            const int width=lighter?11:13;ui::rect(x,baseline-scaled(14,scale),width,2,accent);
            if(*p=='+')ui::rect(x+width/2,baseline-scaled(20,scale),2,scaled(13,scale),accent);
            x+=lighter?15:18;
        } else if(const auto* g=glyph(*p))x=draw_glyph(x,baseline,*g,ink,scale);
    }
    return x;
}
inline bool note(int x,int baseline,const char* pitch,uint32_t ink,uint32_t accent) {
    if(!pitch||pitch[0]<'a'||pitch[0]>'g')return false;
    const bool sharp=pitch[1]=='#';const int digit=sharp?2:1;
    if(pitch[digit]<'0'||pitch[digit]>'9'||pitch[digit+1])return false;
    const auto* letter=glyph(pitch[0]);const auto* octave=glyph(pitch[digit],true);
    if(!letter||!octave)return false;
    x=draw_glyph(x,baseline,*letter,ink,82);
    if(sharp) {
        ui::rect(x+3,baseline-24,1,15,accent);ui::rect(x+8,baseline-24,1,15,accent);
        ui::rect(x,baseline-20,12,1,accent);ui::rect(x,baseline-14,12,1,accent);x+=15;
    }
    draw_glyph(x+3,baseline+3,*octave,accent,82);return true;
}
} // namespace seance::platform::ui3ds::fm_visual
