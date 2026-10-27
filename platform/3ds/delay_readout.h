#pragma once
// DLY-only, scalar presentation mapping. Do not substitute effective blindly:
// discrete modern controls and clean time are consumed from resolved targets.
#include "../../core/engine/units.h"
#include "../../core/engine/delay_time.h"
#include <algorithm>
namespace seance::platform::ui3ds::delay_readout {
using D=DelayUnit;
inline q15 resolved(bool modern,int p,q15 target,q15 effective,bool clean) {
    const bool immediate=modern && (p==D::P_SYNC || p==D::P_MODE || p==D::P_DIV ||
        p==D::P_FEEL || p==D::P_HOLD || (p==D::P_TIME && clean));
    return q15(std::max(0,int(immediate?target:effective)));
}
inline q15 resolved(const D& u,int p) {
    return resolved(u.modern(),p,u.params[p].target,u.params[p].effective,u.params[D::P_MODE].target>16384);
}
inline uint32_t interval(bool modern,const q15* values,int32_t bpm,bool* limited=nullptr) {
    if(limited)*limited=false;
    if(!modern)return 32+((uint32_t(std::max(0,int(values[D::P_TIME])))*31967u)>>15);
    if(values[D::P_SYNC]>16384)return delay_time::sync_frames(values[D::P_DIV],values[D::P_FEEL],bpm,limited);
    return delay_time::free_frames(values[D::P_TIME]);
}
} // namespace seance::platform::ui3ds::delay_readout
