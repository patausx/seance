// Master post-limiter or chain post-module bounce. Runtime only, not a UnitKind.
#pragma once
#include "../synth/sampler.h"
#include "../synth/wav_loader.h"
#include <cstdint>

namespace seance {
class Engine;
class Unit;
class PlayUnit;
// Existing restore decodes stereo PCM16 as raw + q15 vectors (8 bytes/frame).
// Stay inside both the 4 MiB decoder workspace and legacy 20-second restore cap.
constexpr uint32_t BOUNCE_DECODE_MAX_FRAMES = (uint32_t)(trackr::synth::WAV_DECODE_WORKSPACE_BYTES / 8u);
constexpr uint32_t BOUNCE_MAX_FRAMES = BOUNCE_DECODE_MAX_FRAMES < 20u * trackr::synth::SAMPLER_SR
    ? BOUNCE_DECODE_MAX_FRAMES : 20u * trackr::synth::SAMPLER_SR;
enum class BounceSource : uint8_t { Master, Chain1, Chain2, Chain3, Chain4 };
inline constexpr bool bounce_source_valid(BounceSource source) {
    return (uint8_t)source <= (uint8_t)BounceSource::Chain4;
}
inline constexpr uint8_t bounce_channels(BounceSource source) {
    return source == BounceSource::Master ? 2 : (bounce_source_valid(source) ? 1 : 0);
}
enum class BounceStatus : uint8_t { Empty, Armed, Recording, Complete, Partial, Cancelled };
enum class BounceReason : uint8_t { None, Stopped, Reset, TempoChanged, MeterChanged, SampleChanged, ChannelsChanged };
enum class BounceResult : uint8_t {
    Ok, Busy, Invalid, NoFreeSlot, MemoryBudget, TempoModulated, Stale, NotSaved, UnsafeChain
};
struct BounceSnapshot {
    BounceStatus status = BounceStatus::Empty;
    BounceReason reason = BounceReason::None;
    BounceSource source = BounceSource::Master;
    uint8_t channels = 0; // latched actual width; zero only for old synthetic caller tokens
    int8_t slot = -1;
    uint8_t bars = 1, beats_per_bar = 4;
    bool saved = false;
    uint32_t generation = 1, slot_generation = 0;
    uint32_t frames = 0, target_frames = 0;
    int32_t bpm_q16 = 120 * 65536;
    uint64_t start_frame = 0, end_frame = 0;
    bool active() const { return status == BounceStatus::Armed || status == BounceStatus::Recording; }
    bool take() const { return frames && (status == BounceStatus::Complete || status == BounceStatus::Partial); }
};
inline constexpr uint8_t bounce_channels(const BounceSnapshot& take) {
    return take.channels ? take.channels : bounce_channels(take.source);
}
// Create/destroy outside guard. plan/commit are bounded guarded scalar operations;
// only stage allocates. Reusing a staged plan requires destruction outside guard.
struct BouncePlan {
    const Engine* owner = nullptr;
    uint32_t generation = 0, slot_generation = 0, capacity_frames = 0;
    int8_t slot = -1;
    uint8_t bars = 1, beats_per_bar = 4;
    int32_t bpm_q16 = 0;
    BounceSource source = BounceSource::Master;
    uint8_t channels = 0;
    uint32_t clock_increment = 0;
    bool phase_clock = false;
    trackr::synth::Sample staged;
};
// Noncopyable ownership carrier. Lifetime must enclose (not end inside) guard.
// `prepared` and `retired` are reclaimed by the destructor on the controller.
struct BounceSendPlan {
    const Engine* owner = nullptr;
    BounceSnapshot take;
    int8_t chain = -1;
    uint8_t length = 0;
    bool alternate = false;
    Unit* expected[6] = {};
    uint32_t rec_generation[6] = {};
    PlayUnit* prepared = nullptr;
    Unit* retired[6] = {};
    BounceSendPlan() = default;
    BounceSendPlan(const BounceSendPlan&) = delete;
    BounceSendPlan& operator=(const BounceSendPlan&) = delete;
    ~BounceSendPlan();
};
struct BounceState {
    BounceSnapshot view;
    uint32_t clock_increment = 0;
    bool phase_clock = false;
};
} // namespace seance
