#pragma once
// Control/UI only. Revision and companion range are explicit; no editor state.
#include "tilt_response_model.h"
#include "../../core/engine/tilt_extended.h"
#include <cmath>
#include <cstdio>

namespace seance::platform::ui3ds::tilt_readout {
using C=TiltUnit;
inline void db(char* out,size_t size,double value,bool sign=true) {
    if(value==0)std::snprintf(out,size,"0.0db");
    else if(std::abs(value)<.001)std::snprintf(out,size,"%s<.001db",value<0?"-":sign?"+":"");
    else if(std::abs(value)<.05)std::snprintf(out,size,sign?"%+.3fdb":"%.3fdb",value);
    else std::snprintf(out,size,sign?"%+.1fdb":"%.1fdb",value);
}
inline void hz(char* out,size_t size,double value) {
    if(value>=1000)std::snprintf(out,size,"%.2fkhz",value/1000);
    else std::snprintf(out,size,"%.0fhz",value);
}
inline void percent(char* out,size_t size,q15 value,bool sign=false) {
    if(value && std::abs(int(value))<17)std::snprintf(out,size,sign?"%+.3f%%":"%.3f%%",double(value)*100/32767);
    else if(value && std::abs(int(value))<164)std::snprintf(out,size,sign?"%+.2f%%":"%.2f%%",double(value)*100/32767);
    else std::snprintf(out,size,sign?"%+.1f%%":"%.0f%%",double(value)*100/32767);
}
inline void value_text(char* out,size_t size,int p,q15 value,C::Revision revision,q15 range=0,bool range_known=true) {
    if(!out||!size)return;
    if(p<0||p>=6||(p==C::P_TRIM&&revision==C::Revision::Legacy)){std::snprintf(out,size,"--");return;}
    const bool extended=revision==C::Revision::Extended,modern=revision!=C::Revision::Legacy;
    const int v=std::max(0,int(value));
    if(p==C::P_TILT){
        if(extended&&range_known)db(out,size,tilt_extended::amount_db(value,range));
        else percent(out,size,value,true); // score's companion range may differ per selected step
    }else if(p==C::P_FREQ){
        if(!modern&&!range_known)std::snprintf(out,size,"q15 %d",int(value));
        else hz(out,size,extended?tilt_extended::nominal_hz(value):tilt_response_model::nominal_hz(value,!modern&&range==0));
    }else if(p==C::P_DRIVE){
        if(extended){if(!v)std::snprintf(out,size,"clean");else db(out,size,tilt_extended::drive_db(value),false);}
        else if(!modern&&!range_known)std::snprintf(out,size,"q15 %d",int(value));
        else tilt_response_model::value_text(out,size,p,value,modern,!modern&&range==0);
    }else if(p==C::P_RANGE){
        if(extended)db(out,size,tilt_extended::range_db(value),false);
        else tilt_response_model::value_text(out,size,p,value,modern,!modern&&range==0);
    }else if(p==C::P_TRIM)db(out,size,tilt_eq::trim_db(value));
    else percent(out,size,q15(v));
}
}
