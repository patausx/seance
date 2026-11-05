// One focused INST observer; records post-level, actual module output, not the
// downstream/master mix. No per-unit storage, audio mutation or allocation.
#pragma once
#include "unit.h"
#include <cstring>

namespace seance::inst_scope {
constexpr int SAMPLE_COUNT=2048;

// Synchronization contract: all select/release/reset/copy operations and unit
// lifetime changes must be serialized against process/write by the caller's
// existing AudioOut::Guard. No internal locks/atomics; never retain owner beyond
// its guarded destruction. Pointers are identity only and never dereferenced.
// PCM inputs/outputs must be non-overlapping valid arrays for their sizes.
class Recorder {
public:
    void select(const void* owner) {
        if(owner_==owner)return;
        owner_=owner;
        clear();
    }
    void release(const void* owner) {
        if(owner_==owner)select(nullptr);
    }
    void reset(const void* owner) {
        if(owner && owner_==owner)clear();
    }
    void write(const void* owner,const q15* pcm,int frames) {
        // The unselected path does no PCM reads, copies or per-sample work.
        if(!owner || owner_!=owner || !pcm || frames<=0)return;
        const uint32_t n=uint32_t(frames);
        age_=age_>UINT32_MAX-n ? UINT32_MAX : age_+n;
        if(frames>=SAMPLE_COUNT){
            std::memcpy(pcm_,pcm+(frames-SAMPLE_COUNT),sizeof(pcm_));
            next_=0;count_=SAMPLE_COUNT;
            return;
        }
        const int first=frames<SAMPLE_COUNT-next_ ? frames : SAMPLE_COUNT-next_;
        std::memcpy(pcm_+next_,pcm,size_t(first)*sizeof(q15));
        if(frames>first)std::memcpy(pcm_,pcm+first,size_t(frames-first)*sizeof(q15));
        next_=(next_+frames)&(SAMPLE_COUNT-1);
        count_=count_+frames<SAMPLE_COUNT ? count_+frames : SAMPLE_COUNT;
    }
    // Exactly 2048 chronological samples, oldest first, left-zero padded during
    // warmup. A mismatched/disarmed owner returns zeros and age 0. Age counts all
    // accepted processed samples since selection/reset, saturating at UINT32_MAX.
    // A null output is a safe no-op returning 0 (not an age-only query).
    uint32_t copy(const void* owner,q15* out) const {
        if(!out)return 0;
        if(!owner || owner_!=owner){
            std::memset(out,0,sizeof(pcm_));
            return 0;
        }
        const int padding=SAMPLE_COUNT-count_;
        if(padding)std::memset(out,0,size_t(padding)*sizeof(q15));
        if(count_){
            const int oldest=count_==SAMPLE_COUNT ? next_ : 0;
            const int first=count_<SAMPLE_COUNT-oldest ? count_ : SAMPLE_COUNT-oldest;
            std::memcpy(out+padding,pcm_+oldest,size_t(first)*sizeof(q15));
            if(count_>first)std::memcpy(out+padding+first,pcm_,size_t(count_-first)*sizeof(q15));
        }
        return age_;
    }
private:
    void clear() {std::memset(pcm_,0,sizeof(pcm_));next_=count_=0;age_=0;}
    const void* owner_=nullptr;
    q15 pcm_[SAMPLE_COUNT]{};
    uint32_t age_=0;
    int next_=0,count_=0;
};
static_assert((SAMPLE_COUNT&(SAMPLE_COUNT-1))==0,"scope ring must be a power of two");
// C++17 inline variable: one shared ring across engine and UI translation units.
inline Recorder recorder;
} // namespace seance::inst_scope
