// PAUL-only display helpers: no engine/audio/source pointer dereferences.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
namespace seance::platform::ui3ds::paul_ui {
inline constexpr unsigned spectrum_groups=96,spectrum_columns=372;
struct SourceRegion {uint32_t first=0,end=0;}; // end exclusive, exactly like PAUL control_tick
inline SourceRegion effective_region(uint32_t frames,int start,int end) {
    if(!frames)return {};
    const auto first=uint32_t(uint64_t(std::clamp(start,0,32767))*(frames-1)/32767);
    const auto last=uint32_t(uint64_t(std::clamp(end,0,32767))*frames/32767);
    return {first,std::max(first+1,std::min(frames,last))};
}
// The strip remains full-source. Only its WRITE is inverted into persisted,
// region-relative POS; reversed START/END collapses at START, never swaps ends.
inline int16_t strip_position(int x,uint32_t frames,int start,int end,int left=14,int travel=291) {
    const auto region=effective_region(frames,start,end);
    if(!frames||travel<1||region.end<=region.first+1)return 0;
    // Keep the full-source rational unrounded until the final q15 conversion.
    // For START=0/END=32767 this is bit-identical to the original x->POS law.
    const int64_t source=int64_t(std::clamp(x-left,0,travel))*(frames-1);
    const int64_t first=int64_t(region.first)*travel,span=int64_t(region.end-region.first-1)*travel;
    return int16_t(std::clamp(source-first,int64_t(0),span)*32767/span);
}
struct SpectrumKey {
    uintptr_t engine=0,unit=0;
    int chain=-1,slot=-1,sample=-1;
    uint32_t generation=0,frames=0,window=0,serial=0;
    uint8_t mode=0,direction=0;
    bool operator==(const SpectrumKey& k) const {
        return engine==k.engine&&unit==k.unit&&chain==k.chain&&slot==k.slot&&sample==k.sample&&
            generation==k.generation&&frames==k.frames&&window==k.window&&serial==k.serial&&mode==k.mode&&direction==k.direction;
    }
};
#ifdef SEANCE_PAUL_UI_TEST
struct QolStats {uint64_t spectrum_conversions=0,cache_hits=0,cache_invalidations=0;unsigned spectrum_rectangles=0,waveform_lines=0;};
#endif
class SpectrumCache {
    bool valid_=false;SpectrumKey key_{};
    uint32_t magnitudes_[spectrum_groups]={};uint8_t heights_[spectrum_groups]={};
public:
#ifdef SEANCE_PAUL_UI_TEST
    QolStats stats{};
#endif
    void invalidate() {
#ifdef SEANCE_PAUL_UI_TEST
        if(valid_)++stats.cache_invalidations;
#endif
        valid_=false;
    }
    void copy(const SpectrumKey& key,const uint32_t* magnitudes,uint8_t* columns) {
        // A serial alone is insufficient: reset/replacement can reuse it. Compare
        // the complete binding AND magnitudes even within an unchanged epoch.
        if(valid_&&key_==key&&std::memcmp(magnitudes_,magnitudes,sizeof magnitudes_)==0) {
#ifdef SEANCE_PAUL_UI_TEST
            ++stats.cache_hits;
#endif
        } else {
            for(unsigned g=0;g<spectrum_groups;++g) {
                // Preserve the old double conversion exactly; just do it once
                // per distinct consumed picture, outside the audio guard.
                const double db=20.0*std::log10(std::max(double(magnitudes[g])/32768.0,1e-12));
                heights_[g]=uint8_t(std::floor(std::clamp((db+72.0)/72.0,0.0,1.0)*40.0));
            }
            key_=key;std::memcpy(magnitudes_,magnitudes,sizeof magnitudes_);valid_=true;
#ifdef SEANCE_PAUL_UI_TEST
            stats.spectrum_conversions+=spectrum_groups;
#endif
        }
        for(unsigned x=0;x<spectrum_columns;++x)columns[x]=heights_[x*spectrum_groups/spectrum_columns];
    }
};
// Opaque, disjoint columns become identical disjoint rectangles. Equal adjacent
// LINEAR96 groups may merge too; no alpha overlap, interpolation or invented bins.
template<class Rect> inline unsigned spectrum_runs(const uint8_t* columns,Rect rect) {
    unsigned count=0;
    for(unsigned x=0;x<spectrum_columns;) {
        unsigned end=x+1;while(end<spectrum_columns&&columns[end]==columns[x])++end;
        if(columns[x]){rect(14+int(x),210-int(columns[x]),int(end-x),int(columns[x]));++count;}
        x=end;
    }
    return count;
}
}
