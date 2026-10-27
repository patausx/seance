#pragma once
// Interruptible ink disclosure. Draw is read-only; tick owns bounded fields.
// Independent title/caption opacity keeps the approved 55ms metadata delay even
// when a transition reverses. No texture, heap, engine or filesystem access.
#include <cstdint>
#include "start_menu_ink.h"
namespace seance::platform::ui3ds::cabinet_motion {
constexpr int Step=5, Columns=33, Rows=49, Cells=Columns*Rows;
inline int ease(int t,int d) {
    if(t<=0)return 0;
    if(t>=d)return 255;
    const int64_t n=t,den=d;
    return int((255*n*n*(3*den-2*n)+den*den*den/2)/(den*den*den));
}
struct Reveal {
    static constexpr int kOpenMs=420,kCloseMs=260,kSwitchMs=110;
    static constexpr int kRowMs=24,kCaptionMs=55;
    static constexpr int kOpenEndMs=kOpenMs+5*kRowMs+kCaptionMs;
    static constexpr int kDurationMs=kSwitchMs+kOpenEndMs;
    uint8_t alpha[2][Cells]{},from[2][Cells]{};
    uint8_t caption_alpha[2][Cells]{},caption_from[2][Cells]{};
    int target=-1,age=kDurationMs,delay=0;
    bool settled() const { return age==kDurationMs; }
    bool ready() const { return target>=0&&settled(); }
    void retarget(int next) {
        if(next==target)return;
        bool other=false;
        for(int p=0;p<2;++p)for(int i=0;i<Cells;++i) {
            from[p][i]=alpha[p][i];caption_from[p][i]=caption_alpha[p][i];
            if(p!=next&&(alpha[p][i]||caption_alpha[p][i]))other=true;
        }
        target=next;age=0;delay=other&&next>=0?kSwitchMs:0;
    }
    void settle(int next) {
        target=next;age=kDurationMs;delay=0;
        for(int p=0;p<2;++p)for(int i=0;i<Cells;++i)
            from[p][i]=alpha[p][i]=caption_from[p][i]=caption_alpha[p][i]=uint8_t(p==next?255:0);
    }
    void tick(unsigned ms) {
        if(settled()||!ms)return;
        const int end=target<0?kCloseMs:delay+kOpenEndMs;
        const unsigned remaining=unsigned(end-age);
        if(ms>=remaining) { settle(target);return; }
        age+=int(ms);
        for(int p=0;p<2;++p)for(int y=0;y<Rows;++y)for(int x=0;x<Columns;++x) {
            const int i=y*Columns+x,xx=144+x*Step,yy=y*Step;
            const float rank=cabinet_ink::rank(p,xx,yy);
            if(p!=target) {
                // Rank-shaped drying; hard zero at260ms prevents a lingering
                // ghost after a close or an interleaved library/settings switch.
                const float a=age>=kCloseMs?0:1-cabinet_ink::smooth((float(age)/kCloseMs*1.5f-(1-rank)*.55f)/.62f);
                alpha[p][i]=uint8_t(float(from[p][i])*a+.5f);
                caption_alpha[p][i]=uint8_t(float(caption_from[p][i])*a+.5f);
                continue;
            }
            const int local=age-delay-cabinet_ink::row_at(p,yy)*kRowMs;
            const float a=cabinet_ink::smooth((float(local)/kOpenMs*1.35f-rank)/.39f);
            const float b=cabinet_ink::smooth((float(local-kCaptionMs)/kOpenMs*1.35f-rank)/.39f);
            alpha[p][i]=uint8_t(from[p][i]+float(255-from[p][i])*a+.5f);
            caption_alpha[p][i]=uint8_t(caption_from[p][i]+float(255-caption_from[p][i])*b+.5f);
        }
    }
    int sample(int p,int x,int y,bool caption=false) const {
        if(p<0||p>1)return 255;
        // Fixed5px bilinear cells overx144..304/y0..240. Caption and main fields
        // are sampled independently, so interruption cannot swap their opacity.
        int xx=x-144,yy=y;
        xx=xx<0?0:xx>160?160:xx;yy=yy<0?0:yy>240?240:yy;
        const int col=xx/Step,row=yy/Step,fx=xx%Step,fy=yy%Step;
        const int cx=col<Columns-1?col+1:col,ry=row<Rows-1?row+1:row;
        const auto* a=caption?caption_alpha[p]:alpha[p];
        const int u=a[row*Columns+col]*(Step-fx)+a[row*Columns+cx]*fx;
        const int v=a[ry*Columns+col]*(Step-fx)+a[ry*Columns+cx]*fx;
        return (u*(Step-fy)+v*fy+Step*Step/2)/(Step*Step);
    }
};
static_assert(sizeof(Reveal)<=13000,"two interruptible5px ink/caption fields; no heap");
} // namespace seance::platform::ui3ds::cabinet_motion
