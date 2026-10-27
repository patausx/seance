#pragma once
#include "module_frame_layout.h"
#include "../../core/engine/units.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>

namespace seance::platform::ui3ds::fold_editor_visual {
using C=FoldUnit;
constexpr int kColumns=353,kLeft=34,kRight=386,kZero=132,kHeight=55;
constexpr int kWarm=16000,kWindow=256,kTotal=kWarm+kWindow+1,kBudget=2048;
struct Control {int p,left,right,y,rail,x0,y0,x1,y1;const char* name;};
inline constexpr Control controls[C::P_COUNT]={
    {C::P_DRIVE,14,306,36,60,8,28,312,72,"drive"},
    {C::P_MODE,14,148,81,103,8,74,156,112,"morph"},
    {C::P_BIAS,172,306,81,103,164,74,312,112,"bias"},
    {C::P_SMOOTH,14,148,149,171,8,140,156,182,"smooth"},
    {C::P_TONE,172,306,149,171,164,140,312,182,"tone"},
    {C::P_TRIM,14,148,193,215,8,184,156,224,"trim"},
    {C::P_MIX,172,306,193,215,164,184,312,224,"mix"}
};
static_assert(module_frame_layout::kLeft==14&&module_frame_layout::kHeaderTextY==14&&module_frame_layout::kFooterTextY==228,"unchanged shared Fold frame");
inline int clamp(int v,int p,bool modern){return std::max(p==C::P_BIAS||(modern&&p==C::P_DRIVE)?-32767:0,std::min(32767,v));}
// The saved signed raw drive preserves every historical positive value. Only the
// UI travel is logarithmic: .25,1,8x at 0%,40%,100%; no negative-gain fiction.
inline double drive_fraction(q15 raw){return std::max(0.0,std::min(1.0,std::log2(double(C::drive_q16(raw,true))/16384.0)/5.0));}
inline q15 drive_raw(double fraction){
    fraction=std::max(0.0,std::min(1.0,fraction));
    const double gain=std::exp2(fraction*5.0-2.0);
    return q15(std::max(-32767,std::min(32767,int(std::lround(gain<1?(gain-1)*32767/.75:(gain-1)*32767/7)))));
}
inline int position(q15 raw,int p,int left,int right,bool modern){
    if(modern&&p==C::P_DRIVE)return left+int(std::lround(drive_fraction(raw)*(right-left)));
    const bool bi=p==C::P_BIAS;
    return left+(clamp(raw,p,modern)+(bi?32767:0))*(right-left)/(bi?65534:32767);
}
inline void value_text(char* out,size_t size,int p,q15 raw,bool modern){
    if(p<0||p>=(modern?C::P_COUNT:C::LEGACY_COUNT)){std::snprintf(out,size,"--");return;}
    const int v=std::max(0,int(raw));
    if(p==C::P_DRIVE)std::snprintf(out,size,"%.2fx",double(C::drive_q16(raw,modern))/65536.0);
    else if(p==C::P_TRIM){if(!v)std::snprintf(out,size,"mute");else{
        const int tenth=int(std::lround(200.0*std::log10(double(v)/(modern?32767.0:32768.0))));
        std::snprintf(out,size,"%.1fdb",double(tenth)/10);}}
    else if(p==C::P_BIAS){
        const int signed_value=std::max(-32767,int(raw));
        if(!signed_value)std::snprintf(out,size,"0.00fs");
        else if(std::abs(signed_value)<16)std::snprintf(out,size,"%+.6ffs",double(signed_value)/32768.0);
        else if(std::abs(signed_value)<164)std::snprintf(out,size,"%+.4ffs",double(signed_value)/32768.0);
        else std::snprintf(out,size,"%+.2ffs",double(signed_value)/32768.0);
    }else if(p==C::P_TONE){const unsigned hz=C::tone_hz(raw);
        if(!hz)std::snprintf(out,size,"off");else if(hz>=1000)std::snprintf(out,size,"%.1fkhz",double(hz)/1000);
        else std::snprintf(out,size,"%uhz",hz);
    }else std::snprintf(out,size,"%u%%",unsigned((v*100+16383)/32767));
}
struct Column {int8_t first=0,last=0,low=0,high=0;uint8_t clipped=0;};
struct Plot {Column source[kColumns]={},output[kColumns]={};};
static_assert(sizeof(Plot)==10*kColumns,"pointer-free fixed-scale reference");
struct Key {q15 values[C::P_COUNT]={};bool bypass=false,curve=false;};
inline bool same(const Key& a,const Key& b){return a.bypass==b.bypass&&a.curve==b.curve&&std::equal(a.values,a.values+C::P_COUNT,b.values);}
inline constexpr q15 kTone[128]={
    0,1206,2409,3606,4794,5971,7134,8279,9404,10507,11585,12634,13653,14639,15590,16504,
    17377,18209,18997,19739,20433,21079,21673,22216,22704,23138,23517,23839,24103,24309,24457,24545,
    24575,24545,24457,24309,24103,23839,23517,23138,22704,22216,21673,21079,20433,19739,18997,18209,
    17377,16504,15590,14639,13653,12634,11585,10507,9404,8279,7134,5971,4794,3606,2409,1206,
    0,-1206,-2409,-3606,-4794,-5971,-7134,-8279,-9404,-10507,-11585,-12634,-13653,-14639,-15590,-16504,
    -17377,-18209,-18997,-19739,-20433,-21079,-21673,-22216,-22704,-23138,-23517,-23839,-24103,-24309,-24457,-24545,
    -24575,-24545,-24457,-24309,-24103,-23839,-23517,-23138,-22704,-22216,-21673,-21079,-20433,-19739,-18997,-18209,
    -17377,-16504,-15590,-14639,-13653,-12634,-11585,-10507,-9404,-8279,-7134,-5971,-4794,-3606,-2409,-1206};
inline constexpr q15 source_sample(int n){return kTone[n%128];}
inline int8_t ordinate(double v){return int8_t(std::lround(std::max(-1.0,std::min(1.0,v))*kHeight));}
inline Column column(double a,double b,double lo,double hi){return {ordinate(a),ordinate(b),ordinate(lo),ordinate(hi),uint8_t((hi>1?1:0)|(lo< -1?2:0))};}
// 257 actual 32kHz samples span precisely two cycles, including the endpoint.
// Piecewise LINEAR DISPLAY interpolation is not extra audio oversampling. Each
// column retains endpoints and any native sample extrema within that interval.
inline void project(const q15* samples,Column* columns){
    const auto at=[&](int n){const int i=n/(kColumns-1),fraction=n%(kColumns-1);
        return (double(samples[i])*(kColumns-1-fraction)+double(samples[std::min(kWindow,i+1)])*fraction)/((kColumns-1)*32768.0);};
    for(int x=0;x<kColumns;++x){const int a=x*kWindow,b=std::min(kColumns-1,x+1)*kWindow;
        const double first=at(a),last=at(b);double lo=std::min(first,last),hi=std::max(first,last);
        for(int i=(a+kColumns-2)/(kColumns-1);i<=b/(kColumns-1);++i){const double v=double(samples[i])/32768;lo=std::min(lo,v);hi=std::max(hi,v);}
        columns[x]=column(first,last,lo,hi);
    }
}
struct Model {
    C unit{true};Key key{};Plot plot{};q15 samples[kWindow+1]={};int done=0,work=0,starts=0;bool valid=false;
    void invalidate(){valid=false;done=work=0;}
    bool ready()const{return valid&&done==kTotal;}
    void advance(const Key& next){
        work=0;
        if(!valid||!same(key,next)){
            key=next;plot={};done=0;valid=true;++starts;
            if(key.curve){
                for(int x=0;x<kColumns;++x){const double in=-1.0+2.0*x/(kColumns-1);
                    const double out=C::transfer(float(in),key.values); // before DC/tone/trim/mix, even when bypassed
                    plot.source[x]=column(in,in,in,in);plot.output[x]=column(out,out,out,out);}
                done=kTotal;return;
            }
            for(int p=0;p<C::P_COUNT;++p){unit.params[p].base=key.values[p];unit.params[p].snap();}
            unit.reset();
        }
        const int end=std::min(kTotal,done+kBudget);
        q15 scratch[CONTROL_INTERVAL];
        while(done<end){const int count=std::min(end-done,CONTROL_INTERVAL-done%CONTROL_INTERVAL);
            for(int i=0;i<count;++i)scratch[i]=source_sample(done+i);
            if(!key.bypass){if(done%CONTROL_INTERVAL==0)unit.control_tick();unit.process(scratch,count);}
            for(int i=0;i<count;++i)if(done+i>=kWarm)samples[done+i-kWarm]=scratch[i];
            done+=count;work+=count;
        }
        if(ready()&&work){q15 source[kWindow+1];for(int i=0;i<=kWindow;++i)source[i]=source_sample(kWarm+i);
            project(source,plot.source);project(samples,plot.output);}
    }
};
} // namespace seance::platform::ui3ds::fold_editor_visual
