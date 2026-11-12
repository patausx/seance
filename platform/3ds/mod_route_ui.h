#pragma once
#include "../../core/engine/relic_routes.h"
#include <cstdio>
namespace seance::platform::ui3ds {
inline bool operator==(SourceRef a,SourceRef b){return a.type==b.type&&a.legacy==b.legacy&&a.instance==b.instance&&a.port==b.port;}
inline bool operator!=(SourceRef a,SourceRef b){return !(a==b);}
inline bool operator==(SourceRef a,ModSource b){return a==SourceRef::from_legacy(b);}
inline bool operator!=(SourceRef a,ModSource b){return !(a==b);}
inline const char* relic_port_name(unsigned p){constexpr const char* names[]={"pull","compress","bend","wave"};return p<4?names[p]:"invalid";}
inline void mod_route_tag(char* out,size_t n,const ParamInspector& d){
    if(d.relic_source)std::snprintf(out,n,"relic.%s",relic_port_name(d.relic_port));
    else if(d.source==ModSource::EnvFollow)std::snprintf(out,n,"env ch%d",d.env_chain+1);
    else if(d.source==ModSource::None)std::snprintf(out,n,"mod off");
    else std::snprintf(out,n,"sg.%c",'a'+mod_source_sigil(d.source));
}
} // namespace seance::platform::ui3ds
