// Fixed-size struck modal source: six damped complex rotations, two tail slots.
// Promoted from the listened standalone prototype; no dependency on experiments.
// Coefficients use libm only on strike. Per-frame state is fixed-point, bounded,
// heap-free and finite-lived. Two tails are overlap, not a chord allocator.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace seance::inst_modal {
class Instrument {
public:
    static constexpr int sample_rate=32000, voice_count=2, mode_count=6;
    struct Strike {
        int note=60;
        uint16_t hardness=18000, position=11000, body=0, damping=15000, strength=26000;
        unsigned decay_ms=2300, attack_ms=1, length_ms=4000, release_ms=800;
    };
    struct ModeInfo { double hz=0,t60=0; int32_t c=0,s=0; };
    struct Info {
        ModeInfo modes[mode_count]{};
        unsigned age=0,end=0,active_modes=0;
        int note=0; uint16_t envelope=0; uint8_t stage=0; bool active=false;
        uint16_t decay_ms=0,damping=0,hardness=0,position=0,body=0,strength=0;
    };
    Instrument() { reset(); }
    void reset() {
        for(auto& v:voices_)v=Voice{};
        cursor_=0;level_=target_=32767;level_left_=0;clips_=limits_=steals_=0;
    }
    // All other controls latch on strike. This gain alone is live.
    void set_level(uint16_t q15) {
        const int target=std::min<int>(q15,32767);
        if(target==target_)return;
        target_=target;level_left_=128;
        level_step_=(target_-level_)/128;level_rem_=std::abs(target_-level_)%128;
        level_sign_=target_>=level_?1:-1;level_error_=0;
    }
    void strike(Strike p) {
        p.note=std::clamp(p.note,24,96);
        p.hardness=std::min<int>(p.hardness,32767);p.position=std::min<int>(p.position,32767);
        p.body=std::min<int>(p.body,32767);p.damping=std::min<int>(p.damping,32767);
        p.strength=std::min<int>(p.strength,32767);
        p.decay_ms=std::clamp(p.decay_ms,120u,6000u);p.attack_ms=std::clamp(p.attack_ms,1u,1000u);
        p.length_ms=std::clamp(p.length_ms,20u,8000u);p.release_ms=std::clamp(p.release_ms,5u,3000u);
        int slot=-1;
        for(int i=0;i<voice_count;++i)if(!voices_[i].active){slot=i;break;}
        if(slot<0){slot=cursor_;cursor_=(cursor_+1)%voice_count;++steals_;}
        const int32_t old=voices_[slot].active?voices_[slot].last:0;
        const bool replacing=voices_[slot].active;
        Voice& v=voices_[slot];v=Voice{};v.old=old;v.crossfade=replacing?64:0;
        v.active=true;v.note=p.note;v.hold=p.length_ms*32;v.release=p.release_ms*32;
        v.decay_ms=uint16_t(p.decay_ms);v.damping=p.damping;v.hardness=p.hardness;
        v.position=p.position;v.body=p.body;v.strength=p.strength;
        const unsigned attack=p.attack_ms*32;
        v.attack_step=32768u/attack;v.attack_rem=32768u%attack;v.attack=attack;
        const unsigned release_start=unsigned(std::min<uint64_t>(32768,uint64_t(v.hold)*32768/attack));
        v.release_step=release_start/v.release;v.release_rem=release_start%v.release;
        v.end=std::min(v.hold+v.release,p.decay_ms*48u);
        constexpr double pi=3.14159265358979323846;
        constexpr double bar[mode_count]={1,4,10,18.5,28.5,40};
        constexpr double bell[mode_count]={1,2.706,3.84,5.31,6.74,8.82};
        constexpr double gain[mode_count]={1,.48,.31,.22,.16,.12};
        const double f0=440*std::pow(2.,(p.note-69)/12.);
        const double b=p.body/32767.,h=p.hardness/32767.,pos=.06+.88*p.position/32767.;
        const double cutoff=950+12500*h*h;
        double weights[mode_count]{},sum=0;
        for(int i=0;i<mode_count;++i){
            auto& m=v.modes[i];const double ratio=bar[i]+(bell[i]-bar[i])*b,hz=f0*ratio;
            if(hz>=sample_rate*.45)continue;
            const double fade=std::clamp((sample_rate*.45-hz)/(sample_rate*.05),0.,1.);
            weights[i]=gain[i]*std::sin(pi*(i+1)*pos)*fade/(1+(hz/cutoff)*(hz/cutoff));
            sum+=std::abs(weights[i]);
            const double t60=(p.decay_ms/1000.)/(1+(p.damping/32767.)*.23*(ratio-1));
            const double r=std::exp(-std::log(1000.)/(sample_rate*t60)),w=2*pi*hz/sample_rate;
            m.c=int32_t(std::llround(r*std::cos(w)*Q));m.s=int32_t(std::llround(r*std::sin(w)*Q));
            m.hz=hz;m.t60=t60;++v.active_modes;
        }
        // Initial L1 excitation is <=42% full-scale per voice; roundoff adds <4 PCM LSB.
        for(int i=0;i<mode_count;++i)v.modes[i].x=sum>0?
            int32_t(std::llround(.42*32767*65536*(p.strength/32767.)*weights[i]/sum)):0;
    }
    int16_t next(){
        int64_t mix=0;
        for(auto& v:voices_){
            if(!v.active)continue;
            int64_t sum=0;
            for(auto& m:v.modes){
                if(m.c==0&&m.s==0)continue;
                const int64_t x=round(int64_t(m.c)*m.x-int64_t(m.s)*m.y,Q);
                const int64_t y=round(int64_t(m.s)*m.x+int64_t(m.c)*m.y,Q);
                m.x=bounded(x);m.y=bounded(y);sum+=m.y;
            }
            if(v.age<v.hold){
                unsigned step=v.attack_step;v.attack_error+=v.attack_rem;
                if(v.attack_error>=v.attack){v.attack_error-=v.attack;++step;}
                v.env=std::min(32768u,v.env+step);
            }else{
                unsigned step=v.release_step;v.release_error+=v.release_rem;
                if(v.release_error>=v.release){v.release_error-=v.release;++step;}
                v.env=v.env>step?v.env-step:0;
            }
            int32_t value=int32_t(round(round(sum,65536)*v.env,32768));
            if(v.crossfade){
                value=int32_t(round(int64_t(value)*(64-v.crossfade)+int64_t(v.old)*v.crossfade,64));
                --v.crossfade;
            }
            v.last=value;mix+=value;
            if(++v.age>=v.end)v=Voice{};
        }
        if(level_left_){
            level_+=level_step_;level_error_+=level_rem_;
            if(level_error_>=128){level_error_-=128;level_+=level_sign_;}
            if(--level_left_==0)level_=target_;
        }
        mix=round(mix*level_,32768);
        if(mix>32767||mix<-32768)++clips_;
        return int16_t(std::clamp<int64_t>(mix,-32768,32767));
    }
    void process(int16_t* out,int frames){if(out&&frames>0)for(int i=0;i<frames;++i)out[i]=next();}
    unsigned active()const{unsigned n=0;for(const auto& v:voices_)n+=v.active;return n;}
    Info info(int i)const{
        Info o{};if(i<0||i>=voice_count)return o;const auto& v=voices_[i];
        o.age=v.age;o.end=v.end;o.active_modes=v.active_modes;o.active=v.active;o.note=v.note;
        o.envelope=uint16_t(std::min(32767u,v.env));
        o.stage=!v.active?0:v.age>=v.hold?3:v.age<v.attack?1:2;
        o.decay_ms=v.decay_ms;o.damping=v.damping;o.hardness=v.hardness;
        o.position=v.position;o.body=v.body;o.strength=v.strength;
        for(int j=0;j<mode_count;++j)o.modes[j]={v.modes[j].hz,v.modes[j].t60,v.modes[j].c,v.modes[j].s};
        return o;
    }
    uint32_t clips()const{return clips_;}uint32_t state_limits()const{return limits_;}
    uint32_t steals()const{return steals_;}
private:
    static constexpr int64_t Q=int64_t(1)<<30;
    static int64_t round(int64_t v,int64_t scale){return(v+(v>=0?scale/2:-scale/2))/scale;}
    int32_t bounded(int64_t v){if(v>INT32_MAX||v<INT32_MIN)++limits_;return int32_t(std::clamp<int64_t>(v,INT32_MIN,INT32_MAX));}
    struct Mode{int32_t x=0,y=0,c=0,s=0;double hz=0,t60=0;};
    struct Voice{
        Mode modes[mode_count]{};
        unsigned age=0,end=0,hold=0,release=0,attack=0,env=0;
        unsigned attack_step=0,attack_rem=0,attack_error=0;
        unsigned release_step=0,release_rem=0,release_error=0,crossfade=0,active_modes=0;
        int32_t old=0,last=0;int note=0;bool active=false;
        uint16_t decay_ms=0,damping=0,hardness=0,position=0,body=0,strength=0;
    };
    Voice voices_[voice_count]{};
    int cursor_=0,level_=32767,target_=32767,level_step_=0,level_sign_=0;
    unsigned level_left_=0,level_rem_=0,level_error_=0;
    uint32_t clips_=0,limits_=0,steals_=0;
};
static_assert(sizeof(Instrument)<=768,"bounded modal state");
} // namespace seance::inst_modal
