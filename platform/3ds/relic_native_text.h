#pragma once
#include "../../core/ui/draw.h"
#include "../../core/ui/font.h"
#include <cstring>
#include <algorithm>
namespace seance::platform::ui3ds::relic_ui {
// Exact native 6x8 glyphs, merged vertical rectangles. Same immutable font ink,
// no stencil/atlas/timer/heap; keeps the no-atlas path inside the1024 budget.
inline void text(int x,int y,const char* s,uint32_t color){
    for(int i=0;s&&s[i];++i){struct Run{int x,w,y,h;};Run runs[24];int count=0;
        for(int row=0;row<8;++row){const auto bits=trackr::ui::glyph_row(s[i],row);
            for(int col=0;col<6;){if(!(bits&(1u<<(5-col)))){++col;continue;}
                const int first=col++;while(col<6&&(bits&(1u<<(5-col))))++col;
                const int width=col-first;bool extended=false;
                for(int k=0;k<count;++k)if(runs[k].x==first&&runs[k].w==width&&runs[k].y+runs[k].h==row){++runs[k].h;extended=true;break;}
                if(!extended)runs[count++]={first,width,row,1};
            }
        }
        for(int k=0;k<count;++k){const auto r=runs[k];ui::rect(x+i*6+r.x,y+r.y,r.w,r.h,color);}
    }
}
inline void mask(int x,int y,const uint8_t* bits,int w,int h,uint32_t color){
    struct Run{int x,w,y,h;};Run active[32],next[32];int count=0;const int stride=(w+7)/8;
    for(int row=0;row<=h;++row){int n=0;bool used[32]={};
        if(row<h)for(int col=0;col<w;){if(!(bits[row*stride+col/8]&(128u>>(col&7)))){++col;continue;}
            const int first=col++;while(col<w&&(bits[row*stride+col/8]&(128u>>(col&7))))++col;
            Run r{first,col-first,row,1};for(int k=0;k<count;++k)if(active[k].x==r.x&&active[k].w==r.w){r.y=active[k].y;r.h=active[k].h+1;used[k]=true;break;}
            next[n++]=r;
        }
        for(int k=0;k<count;++k)if(!used[k]){const auto r=active[k];ui::rect(x+r.x,y+r.y,r.w,r.h,color);}
        count=n;for(int k=0;k<n;++k)active[k]=next[k];
    }
}
inline void caption(int x,int y,int width,const char* s,uint32_t color){if(!s||width<6)return;
    char out[63];const int n=std::min(62,width/6),length=int(std::strlen(s));
    if(length<=n){text(x,y,s,color);return;}std::memcpy(out,s,n);out[n]=0;
    for(int i=std::max(0,n-3);i<n;++i)out[i]='.';
    text(x,y,out,color);
}
} // namespace seance::platform::ui3ds::relic_ui
