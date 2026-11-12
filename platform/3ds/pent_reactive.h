#pragma once
// One connected source-driven blood film; five physical junction responses.
// UI projection only: no DSP/PCM/parameter ownership and no renderer clock.
#include "../../core/engine/pent_unit.h"
#include "word_menu.h"
#include <algorithm>
#include <cstdint>
#include <type_traits>

namespace seance::platform::ui3ds::pent_reactive {
using P=PentUnit;
inline constexpr uint8_t path_voice[5]={0,2,4,1,3}; // actual star edges, not circular voice order
inline constexpr int16_t vertex_x[5]={240,293,153,327,187};
inline constexpr int16_t vertex_y[5]={37,202,100,100,202};
inline constexpr uint32_t edge_turn=65536, cycle_turn=5*edge_turn;
// Compatibility name: x/y are source-front diagnostics, NOT rendered ball centers.
// Alpha/feedback belong to the physical path vertex and its adjoining ribbons.
struct Head {int16_t x=0,y=0;uint8_t alpha=56,feedback=0;};
struct Frame {
    Head heads[5]{};
    uint8_t glow=71;
    bool audible=false;
    uint32_t flow=0; // q16 EDGE lengths along the complete closed five-edge walk
    int16_t warp[5]{}; // shared junction UV displacement, q8 strip texels
    uint8_t breadth[5]={128,128,128,128,128}; // q4 full strip height; frozen in quiet
};
static_assert(sizeof(Frame)<=64&&std::is_trivially_copyable<Frame>::value,"small frozen PENT flow, no live pointers");
inline uint8_t parameter_mask(int p){
    return p>=P::P_RATIO_A&&p<=P::P_RATIO_E?uint8_t(1u<<(p-P::P_RATIO_A)):uint8_t(31);
}
inline int positive(q15 value){return std::max(0,int(value));}
inline uint8_t raised(uint8_t base,uint8_t feedback){
    return uint8_t(base+(unsigned(255-base)*feedback+127)/255);
}
// One quarter-wave table, interpolated in actual slow turns. Never sample the
// fast oscillator phase at LCD rate: that aliases and can appear to run backward.
inline int sine(uint16_t phase){
    constexpr uint16_t quarter[]={0,3212,6393,9512,12539,15446,18204,20787,
        23170,25329,27245,28898,30273,31356,32137,32609,32767};
    const unsigned quadrant=phase>>14;
    unsigned p=phase&16383;
    if(quadrant&1)p=16384-p;
    const unsigned index=p>>10,fraction=p&1023;
    const int value=index==16?quarter[16]:quarter[index]+int((unsigned(quarter[index+1]-quarter[index])*fraction+512)>>10);
    return quadrant>=2?-value:value;
}
// Monotone bounded visual speed from the frequency ACTUALLY applied to the
// source: 1/8 .. 1.5 EDGE lengths/s. Never a literal PM/phase or fluid trace.
inline uint32_t rate(uint32_t frequency_millihz){
    const uint32_t f=std::min<uint32_t>(frequency_millihz,12000000u);
    return 8192u+uint32_t(uint64_t(f)*90112u/(uint64_t(f)+160000u));
}
struct Feedback {
    uint64_t at=0,last=0;
    uint8_t mask=0;
    void cancel(){*this={};}
    void acknowledge(uint8_t voices,uint64_t now){at=last=now;mask=voices&31;}
    uint8_t capture(unsigned voice,uint64_t now){
        if(now<last||now<at){cancel();return 0;}
        last=now;
        if(!mask||now-at>=word_menu::kFocusMs){mask=0;return 0;}
        return mask&(1u<<voice)?uint8_t((1.f-word_menu::ease(now,at,word_menu::kFocusMs))*255.f+.5f):0;
    }
};
struct State {
    // Each physical voice's monotone source front completes ALL five edges.
    // The shared film advances by their mean rate, with independent carry, so
    // adjacent edges have the SAME UV at a junction even when ratios differ.
    uint32_t turns[5]={0,edge_turn,2*edge_turn,3*edge_turn,4*edge_turn};
    uint32_t remainder[5]{};
    uint32_t flow=0,flow_remainder=0;
    uint64_t frames=0;
    int16_t warp[5]{};
    uint8_t breadth[5]={128,128,128,128,128};
    bool observed=false,shaped=false;
    void reset(){*this={};}
    Frame capture(const P::Visual& visual,bool fresh,bool eligible,const uint8_t (&feedback)[5]){
        Frame out;out.audible=eligible;
        uint64_t delta=0;
        if(visual.frames<frames)reset();
        if(!observed){observed=true;}
        else if(fresh&&eligible){
            // Constant work. No catch-up loops or UI-time extrapolation.
            delta=std::min(visual.frames-frames,uint64_t(3200)); // source100ms
        }
        frames=visual.frames;
        const int gain=positive(visual.effective[P::P_GAIN]);
        const int drive=positive(visual.effective[P::P_DRIVE]);
        const int bond=positive(visual.effective[P::P_BOND]);
        const int spread=positive(visual.effective[P::P_SPREAD]);
        const int depth=positive(visual.effective[P::P_DRIFT_DEPTH]);
        unsigned activity_sum=0,excursion_sum=0,feedback_max=0,rate_sum=0;
        for(unsigned i=0;i<5;++i){
            const unsigned voice=path_voice[i];
            const uint32_t speed=rate(visual.frequency_millihz[voice]);rate_sum+=speed;
            if(delta){
                const uint64_t increment=delta*speed+remainder[i];
                turns[i]=(turns[i]+uint32_t(increment/32000u))%cycle_turn;
                remainder[i]=uint32_t(increment%32000u);
            }
            const unsigned edge=turns[i]/edge_turn,next=(edge+1)%5;
            const uint32_t position=turns[i]%edge_turn;
            auto& head=out.heads[i];
            const auto point=[&](const int16_t (&vertices)[5]){
                const int64_t at=int64_t(vertices[edge])*edge_turn+
                    int64_t(vertices[next]-vertices[edge])*position;
                return int16_t((at+edge_turn/2)/edge_turn);
            };
            head.x=point(vertex_x);head.y=point(vertex_y);
            const unsigned activity=eligible?(unsigned(visual.level[voice])*unsigned(gain)+16383)/32767:0;
            const unsigned excursion=eligible?visual.bond[voice]:0;
            activity_sum+=activity;excursion_sum+=excursion;
            const unsigned alpha=eligible?48+activity*150/32767+excursion*45/32767:56;
            head.feedback=feedback[voice];feedback_max=std::max(feedback_max,unsigned(head.feedback));
            head.alpha=raised(uint8_t(std::min(alpha,255u)),head.feedback);
            // Deform the longitudinal film/its breadth, not the star centerline.
            // Small shared vertex warps cannot turn a strip into a detached bead.
            // Geometry changes only from a fresh admitted source (or cold shape).
            if(!shaped||(fresh&&eligible)){
                const int breathing=int(int64_t(sine(uint16_t(turns[i])))*128/32767);
                const int drift=int(int64_t(sine(visual.drift_phase[voice]))*depth*24/(32767ll*32767));
                const int separation=(int(voice)-2)*spread*32/32767;
                warp[i]=int16_t(breathing+drift+separation);
                breadth[i]=uint8_t(128+bond*32/32767+excursion*16/32767+activity*16/32767);
            }
            out.warp[i]=warp[i];out.breadth[i]=breadth[i];
        }
        shaped=true;
        if(delta){
            const uint64_t increment=delta*rate_sum+flow_remainder;
            flow=(flow+uint32_t(increment/(5u*32000u)))%cycle_turn;
            flow_remainder=uint32_t(increment%(5u*32000u));
        }
        out.flow=flow;
        if(eligible){
            const unsigned activity=activity_sum/5,excursion=excursion_sum/5;
            const unsigned response=112+unsigned(drive)*100/32767+excursion*78/32767;
            out.glow=uint8_t(std::min(255u,(response*activity+16383)/32767));
        }
        // UI acknowledgement can brighten a QUIET film but never rearm the
        // audio lease, advance flow/turns/warp, or claim a sounding trigger.
        out.glow=raised(out.glow,uint8_t(feedback_max));
        return out;
    }
};
static_assert(sizeof(State)<=96&&std::is_trivially_copyable<State>::value,"bounded nonpersistent PENT flow observer");
} // namespace seance::platform::ui3ds::pent_reactive
