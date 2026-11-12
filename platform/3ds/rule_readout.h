#pragma once
// RULE parameter text shared by the editor and the chains card. Pure formatting:
// no Engine, PCM, GPU or allocation.
#include "../../core/engine/rule_unit.h"
#include <algorithm>
#include <cstdio>
namespace seance::platform::ui3ds::rule_readout {
inline constexpr const char* scales[]={"hirajoshi","major pent","minor pent","minor","chromatic"};
inline void value_text(char* out,size_t n,int p,q15 v){
    if(p<0||p>=12){std::snprintf(out,n,"--");return;}
    const unsigned raw=unsigned(std::max(0,int(v)));
    if(p==RuleUnit::P_ROOT)std::snprintf(out,n,"%+dst",RuleUnit::transpose(v));
    else if(p==RuleUnit::P_RATE){const uint32_t ratio=RuleUnit::period_frames(v,65536);const unsigned hundredths=unsigned((uint64_t(ratio)*100+32768)/65536);std::snprintf(out,n,"%u.%02ub",hundredths/100,hundredths%100);}
    else if(p==RuleUnit::P_ATTACK||p==RuleUnit::P_RELEASE){const unsigned ms=unsigned((p==RuleUnit::P_ATTACK?RuleUnit::attack_frames(v):RuleUnit::release_frames(v))*1000u/32000u);
        if(ms<1000)std::snprintf(out,n,"%ums",ms);else std::snprintf(out,n,"%u.%02us",ms/1000,(ms%1000)/10);}
    else if(p==RuleUnit::P_VOICES)std::snprintf(out,n,"%u",unsigned(RuleUnit::voice_limit(v)));
    else if(p==RuleUnit::P_RULE)std::snprintf(out,n,"%u",unsigned(RuleUnit::rule_number(v)));
    else if(p==RuleUnit::P_SCALE)std::snprintf(out,n,"%s",scales[RuleUnit::scale_index(v)]);
    else if(p==RuleUnit::P_MUTATE){const unsigned gens=raw*64u/32767u;if(gens)std::snprintf(out,n,"%ugen",gens);else std::snprintf(out,n,"off");}
    else std::snprintf(out,n,"%u%%",(raw*100u+16383u)/32767u);
}
}
