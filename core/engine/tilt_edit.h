#pragma once
// Caller owns the audio guard. No allocations, file conversion or UI globals.
#include "engine.h"
#include "units.h"
#include "tilt_eq.h"
#include <algorithm>
namespace seance::tilt_edit {
// Use after all load chunks, and before exposing historical padding on extend.
inline void clear_inactive(Engine& engine,int chain,int slot,int first){
    auto* unit=static_cast<TiltUnit*>(engine.chains[chain].units[slot]);
    for(int p=first;p<PARAM_BANK_SIZE;++p){const char* name=unit->params[p].name;unit->params[p]=Param{};unit->params[p].name=name;}
    if(first==TiltUnit::P_TRIM){unit->params[first].name="TRIM";unit->params[first].base=tilt_eq::TRIM_UNITY;unit->params[first].snap();}
    for(auto& scene:engine.scenes[chain])if(scene.kind[slot]==UnitKind::Tilt)
        for(int p=first;p<MAX_PARAMS;++p)scene.base[slot][p]=0;
    for(auto& pad:engine.macro_pads){
        const int count=std::min(int(pad.route_count),MacroPad::MAX_ROUTES);int kept=0;
        for(int i=0;i<count;++i){const auto route=pad.routes[i];
            if(route.chain==chain && route.slot==slot && route.param>=first)continue;
            pad.routes[kept++]=route;
        }
        for(int i=kept;i<MacroPad::MAX_ROUTES;++i)pad.routes[i]=MacroPadRoute{};
        pad.route_count=uint8_t(kept);
    }
    auto strip=[=](Pattern& pattern){for(auto& step:pattern.steps[chain]){
        const int count=std::min(int(step.lock_count),SEQ_LOCKS);int kept=0;
        for(int i=0;i<count;++i){const auto lock=step.locks[i];
            if(lock.slot==slot && lock.param>=first)continue;
            step.locks[kept++]=lock;
        }
        for(int i=kept;i<SEQ_LOCKS;++i)step.locks[i]=StepLock{};
        step.lock_count=uint8_t(kept);
    }};
    strip(engine.pattern);for(auto& bank:engine.pattern_banks)strip(bank);
}
inline bool extend(Engine& engine,int chain,int slot){
    if(chain<0 || chain>=NUM_CHAINS || slot<0 || slot>=UNITS_PER_CHAIN)return false;
    Unit* raw=engine.chains[chain].units[slot];
    if(!raw || raw->kind!=UnitKind::Tilt)return false;
    auto& unit=*static_cast<TiltUnit*>(raw);if(unit.extended())return false;
    const int old_count=unit.num_params;
    clear_inactive(engine,chain,slot,old_count);
    if(old_count==TiltUnit::P_TRIM)for(auto& scene:engine.scenes[chain])
        if(scene.kind[slot]==UnitKind::Tilt)scene.base[slot][TiltUnit::P_TRIM]=tilt_eq::TRIM_UNITY;
    unit.set_revision(TiltUnit::Revision::Extended);
    return true;
}
}
