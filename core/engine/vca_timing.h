#pragma once
// Extended VCA only. Integer stage durations; no allocation or per-sample math.
#include "vca_envelope.h"

namespace seance::vca_timing {
// Quarter-note beat = 192 ticks. Ordered lengths include straight, dotted (d)
// and triplet (t) notes. 4/1 is sixteen beats, NOT bars in the current meter.
struct Division { uint16_t ticks; const char* name; };
inline constexpr Division DIVISIONS[] = {
    {4,"1/128t"},{6,"1/128"},{8,"1/64t"},{9,"1/128d"},
    {12,"1/64"},{16,"1/32t"},{18,"1/64d"},{24,"1/32"},
    {32,"1/16t"},{36,"1/32d"},{48,"1/16"},{64,"1/8t"},
    {72,"1/16d"},{96,"1/8"},{128,"1/4t"},{144,"1/8d"},
    {192,"1/4"},{256,"1/2t"},{288,"1/4d"},{384,"1/2"},
    {512,"1/1t"},{576,"1/2d"},{768,"1/1"},{1024,"2/1t"},
    {1152,"1/1d"},{1536,"2/1"},{2048,"4/1t"},{2304,"2/1d"},{3072,"4/1"}
};
inline constexpr int COUNT = sizeof(DIVISIONS)/sizeof(DIVISIONS[0]);
inline int raw(q15 v) { return v<0?0:v; }
inline bool enabled(q15 v) { return v>=16384; }
inline int retrigger(q15 v) { return (raw(v)*3)>>15; }
inline q15 retrigger_value(int i) { i=i<0?0:i>2?2:i;return q15((i*32768+16384)/3); }
inline int division(q15 v,bool hold=false) {
    const int n=raw(v);
    if(hold&&!n)return -1;
    return hold?((n-1)*COUNT/32767):(n*COUNT/32768);
}
inline q15 division_value(int i,bool hold=false) {
    if(hold&&i<0)return 0;
    i=i<0?0:i>=COUNT?COUNT-1:i;
    return q15(hold?1+((2*i+1)*32767)/(2*COUNT):(i*32768+16384)/COUNT);
}
inline const char* division_name(int i) { return i<0?"off":DIVISIONS[i>=COUNT?COUNT-1:i].name; }
inline int32_t bpm(int32_t q16) { return q16<(20<<16)?(20<<16):q16>(300<<16)?(300<<16):q16; }
inline uint32_t sync_frames(q15 v,int32_t bpm_q16,bool hold=false) {
    const int i=division(v,hold);if(i<0)return 0;
    const uint64_t numerator=uint64_t(ENGINE_SR)*60*65536*DIVISIONS[i].ticks;
    const uint64_t denominator=uint64_t(bpm(bpm_q16))*192;
    return uint32_t((numerator+denominator/2)/denominator);
}
inline uint32_t frames(q15 v,bool hold,bool sync,int32_t bpm_q16) {
    return sync?sync_frames(v,bpm_q16,hold):vca_envelope::time_frames(v,hold);
}
// Only the extended card resolves the shared integer slew's last 1..3 raw units.
// The old AD/AHD PCM paths intentionally retain the old effective values.
inline q15 resolved(const Param& p,bool discrete=false) {
    if(!p.slew_ready)return p.effective;
    const int d=int(p.target)-p.effective;
    return discrete||(d>=-3&&d<=3)?p.target:p.effective;
}
} // namespace seance::vca_timing
