#pragma once
// Production selected RIGHT upper: frozen first-route data + post-guard model.
// Public prototype probe names/types remain here; arc storage/painter is private.
#include "sigils_flow.h"
#include "sigils_soft_upper_arc.h"
#include "filter_response_model.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

namespace seance::platform::ui3ds::soft_upper {
using T=trackr::dsp::FilterType;
namespace model=filter_response_model;
struct Capture {
    q15 base[3]={},live[3]={}; // cut, res, morph; published smooth cut/res
    q15 parameter_base=0,amount=0;
    T type=T::LPF;
    bool filter=false,bypass=false,mute=false;
};
static_assert(sizeof(Capture)<=24&&std::is_trivially_copyable<Capture>::value,
              "small pointer-free sigil upper capture, separate from original Frame");
inline Capture raw;
struct Result {
    uint8_t y[2][model::POINTS]={};
    char nominal[48]={};
    bool different=false;
};
inline Result result;
struct ModelCache {
    Capture key;
    uint8_t y[2][model::POINTS]={};
    bool valid=false;
    char nominal[48]={};
};
inline ModelCache cache;
inline model::Frequency frequencies[model::POINTS];
inline bool frequencies_ready=false;
constexpr unsigned FIXED_BYTES=sizeof(raw)+sizeof(coverage)+sizeof(result)+sizeof(cache)+
    sizeof(frequencies)+sizeof(frequencies_ready)+sizeof(columns)+sizeof(geometry)+sizeof(work);
static_assert(FIXED_BYTES<50000,"bounded fixed soft upper storage, no heap or texture");
inline void clear(){
    work={};raw={};result={};clear_arc();
    // Model cache/frequency table contain no route identity or live pointers.
    // Keeping them cannot leak content: result is empty until a valid new capture.
}
inline void project(const sigils_flow::Frame& f){
    work={};result={};
    if(!f.valid||f.count<2||f.count>97){clear();return;}
    // Only the selected physical projection's 84..168 domain enters the raster.
    // This also bounds all integer dot/cross products and the float error guard.
    for(unsigned i=0;i<f.count;++i)if(f.y[i]<84||f.y[i]>168){clear();return;}
    project_arc(f);
    if(!raw.filter||f.target.kind!=sigils_flow::TargetKind::Module)return;
    if(!frequencies_ready){
        for(int i=0;i<model::POINTS;++i)
            frequencies[i]=model::at_hz(model::MIN_HZ*std::pow(model::MAX_HZ/model::MIN_HZ,float(i)/116.f));
        frequencies_ready=true;work.frequency_points=model::POINTS;
    }
    const bool same=cache.valid&&cache.key.type==raw.type&&cache.key.bypass==raw.bypass;
    for(int curve=0;curve<2;++curve){
        const auto* src=curve?raw.live:raw.base;const auto* old=curve?cache.key.live:cache.key.base;
        if(!same||!std::equal(src,src+3,old)){
            if(curve&&std::equal(raw.base,raw.base+3,raw.live))
                std::copy(cache.y[0],cache.y[0]+model::POINTS,cache.y[1]);
            else for(int i=0;i<model::POINTS;++i){
                const float db=model::magnitude_db(src[0],src[1],src[2],raw.bypass?T::Off:raw.type,frequencies[i]);
                cache.y[curve][i]=uint8_t(190+int((12.f-std::max(-36.f,std::min(12.f,db)))*24.f/48.f+0.5f));
                ++work.model_points;
            }
        }
        std::copy(cache.y[curve],cache.y[curve]+model::POINTS,result.y[curve]);
    }
    result.different=!std::equal(result.y[0],result.y[0]+model::POINTS,result.y[1]);
    if(!cache.valid||cache.key.base[0]!=raw.base[0]||cache.key.live[0]!=raw.live[0]){
        std::fill(cache.nominal,cache.nominal+sizeof cache.nominal,char(0));
        std::snprintf(cache.nominal,sizeof cache.nominal,"%u > %u hz",unsigned(model::nominal_hz(raw.base[0])+0.5f),unsigned(model::nominal_hz(raw.live[0])+0.5f));
    }
    std::memcpy(result.nominal,cache.nominal,sizeof result.nominal);cache.key=raw;cache.valid=true;
}
template<class Source> void content(const sigil_surface::TopPainter& p,const Source& source){
    namespace m=menu_style;const auto& f=source.flow;if(!f.valid)return;
    char text[64];const char* state=sigils_flow::route_status(f,source.route_count);
    if(raw.filter&&f.target.kind==sigils_flow::TargetKind::Module){
        p.text(28,176,"nominal cut",m::ghost());p.text(106,176,result.nominal,m::ink());
        for(int curve=0;curve<2;++curve){
            if(!curve&&!result.different)continue;
            for(int i=1;i<model::POINTS;++i){
                if(!curve&&(i&3)>1)continue;
                p.line(28+((i-1)*148+58)/116,result.y[curve][i-1],28+(i*148+58)/116,result.y[curve][i],
                       curve&&f.connected?m::spot():m::ghost());
            }
        }
        p.text(28,216,raw.bypass||raw.type==T::Off?"dry / transfer model":"transfer / not spectrum",m::ghost());
        if(*state){std::snprintf(text,sizeof text,"%.26s",state);p.text(222,176,text,m::ghost());}
        else p.text(222,176,source.route_count>1?"other links":"one destination",m::ghost());
    }else{
        if(f.target.kind==sigils_flow::TargetKind::Module){
            std::snprintf(text,sizeof text,"base %d%% > live %d%%",int(raw.parameter_base)*100/32767,int(f.target.effective)*100/32767);
            p.text(28,188,text,m::ink());
            if(*state)p.text(28,202,state,m::ghost());
        }else if(*state)p.text(28,188,state,m::ghost());
        if(source.route_count>1)p.text(222,176,"other links",m::ghost());
    }
    const unsigned shown=std::min(unsigned(source.route_count),3u);
    for(unsigned i=1;i<shown;++i){
        const auto& r=source.routes[i];
        char name[24];std::snprintf(name,sizeof name,"%.19s",r.name);
        for(char* c=name;*c;++c)if(*c>='A'&&*c<='Z')*c=char(*c+'a'-'A');
        p.text(222,190+int(i-1)*13,name,m::ghost());
        // Original clock Route has no amount; don't mislabel its default q15 zero.
        if(std::strncmp(r.name,"project / ",10)==0)std::snprintf(text,sizeof text,"clock");
        else std::snprintf(text,sizeof text,r.enabled?"%+d%%":"off",int(r.depth)*100/32767);
        p.right(380,190+int(i-1)*13,text,m::ghost());
    }
    if(source.route_count>3){std::snprintf(text,sizeof text,"+%u more",unsigned(source.route_count)-3);p.text(222,216,text,m::ghost());}
}
} // namespace seance::platform::ui3ds::soft_upper
