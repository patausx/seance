// Audio-side timing for the opt-in diagnostic overlay (docs/debug-overlay.md):
//   * the worker's stages per 512-frame block: waiting for AudioOut's guard,
//     Engine::render, UI snapshot capture, hand-off to NDSP;
//   * what else the worker saw at each block: how far it started after the previous
//     one, how many buffers NDSP still held, transport, core, sounding load;
//   * the other side of the same guard: how long UI-side callers waited to enter it.
// The worker and the UI thread only meet in lock-free storage; the overlay keeps
// the same rolling 3 s statistics it keeps for frames. No engine, SDK or heap
// dependency, so host tests use this header directly.
#pragma once
#include "frame_profile.h"
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace seance::platform::audio_profile {

// Same switch as the ZR overlay. A normal build reads no extra clock and never
// references the shared objects below; this header is still compiled and checked.
#ifdef SEANCE_DIAGNOSTIC_OVERLAY
constexpr bool Enabled = true;
#else
constexpr bool Enabled = false;
#endif

// A span longer than 2^32 ticks (about 16 s) saturates instead of wrapping.
inline uint32_t span(uint64_t ticks) { return frame_profile::duration(ticks); }

// One block of audio lasts this long (512 frames at 32 kHz); audio_out.cpp checks
// it against the real constants. A block whose stages take longer is late.
constexpr uint32_t BudgetUs = 16000;

// Block::state: transport and sequencer as the block's own snapshot published
// them, and the core the worker was running on (CoreUnknown: outside 0..14).
constexpr uint8_t Playing = 1, Sequencing = 2, CoreShift = 4, CoreUnknown = 15;
inline uint8_t state(bool playing, bool sequencing, int core) {
    return uint8_t((playing ? Playing : 0) | (sequencing ? Sequencing : 0) |
                   ((core >= 0 && core < CoreUnknown ? core : CoreUnknown) << CoreShift));
}
// Block::queue: low nibble = OTHER buffers NDSP still had queued or playing when
// this block was handed over (0 = it had run dry), high nibble = queue depth in use.
inline uint8_t queue(int held, int depth) {
    return uint8_t((held < 0 ? 0 : held > 15 ? 15 : held) | ((depth < 0 ? 0 : depth > 15 ? 15 : depth) << 4));
}

// One output block as the worker saw it, in raw system ticks (the UI converts).
struct Block {
    uint32_t lock = 0;    // asked for the guard -> entered it
    uint32_t dsp = 0;     // Engine::render (or the recovery-inhibited silence fill)
    uint32_t snap = 0;    // UI snapshot capture
    uint32_t out = 0;     // publish, leave the guard, cache flush, NDSP queue submit
    uint32_t xruns = 0;   // cumulative underrun counter when this block was rendered
    uint32_t period = 0;  // previous block asked for the guard -> this one did; 0 = no previous block
    uint8_t queue = 0;    // queue(held, depth) at this hand-off
    uint8_t state = 0;    // Playing | Sequencing | core << CoreShift
    uint8_t units = 0;    // installed cards, not bypassed, on unmuted chains
    uint8_t voices = 0;   // GRAN heads + RULE voices computed at the end of this block, audible or not
};
static_assert(sizeof(Block) == 28, "one block of the stage feed");
// stamp: asked for the guard, entered it, render returned, snapshot captured.
inline Block stages(const uint64_t (&stamp)[4], uint64_t done, uint32_t xruns) {
    return {span(stamp[1] - stamp[0]), span(stamp[2] - stamp[1]), span(stamp[3] - stamp[2]),
            span(done - stamp[3]), xruns};
}

// Bounded single-producer / single-consumer queue. The audio worker never waits
// and never overwrites a block the UI has not taken: while nobody drains (overlay
// hidden, long SD stall) new blocks are counted in dropped() instead.
class Feed {
public:
    static constexpr uint32_t Capacity = 64;   // about one second of 16 ms blocks

    bool push(const Block& block) noexcept {   // audio worker only
        const uint32_t head = head_.load(std::memory_order_relaxed);
        if (head - tail_.load(std::memory_order_acquire) >= Capacity) {
            dropped_.store(dropped_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
            return false;
        }
        slots_[head % Capacity] = block;        // outside [tail, head): the reader cannot be here
        head_.store(head + 1, std::memory_order_release);
        return true;
    }
    bool pop(Block& out) noexcept {             // UI thread only
        const uint32_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) return false;
        out = slots_[tail % Capacity];
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }
    uint32_t dropped() const noexcept { return dropped_.load(std::memory_order_relaxed); }

private:
    static_assert(std::atomic<uint32_t>::is_always_lock_free,
                  "stage feed requires native lock-free 32-bit atomics");
    // Counters wrap at 2^32; a power-of-two capacity keeps slot indices continuous there.
    static_assert((Capacity & (Capacity - 1)) == 0, "capacity must be a power of two");
    Block slots_[Capacity]{};
    std::atomic<uint32_t> head_{0};      // written by the worker
    std::atomic<uint32_t> tail_{0};      // written by the UI thread
    std::atomic<uint32_t> dropped_{0};   // written by the worker
};
inline Feed feed;

// Audio worker only: the distance between two blocks, taken where each one asks
// for the guard. A worker that keeps up reports one block of audio (16 ms); a
// worker that never sleeps reports its own block time.
class Cadence {
public:
    uint32_t period(uint64_t asked) noexcept {
        const uint32_t ticks = have_ ? span(asked - last_) : 0;
        last_ = asked;
        have_ = true;
        return ticks;
    }
private:
    uint64_t last_ = 0;
    bool have_ = false;
};
inline Cadence cadence;

// AudioOut::lock() callers (everything except the worker itself) add the time they
// spent waiting for the guard; the overlay takes the totals once per frame.
class GuardWait {
public:
    void waited(uint64_t ticks) noexcept {
        const uint32_t add = span(ticks);
        uint32_t old = ticks_.load(std::memory_order_relaxed), sum;
        do sum = old + add < old ? UINT32_MAX : old + add;   // saturate, never wrap to a small total
        while (!ticks_.compare_exchange_weak(old, sum, std::memory_order_relaxed));
        entries_.fetch_add(1, std::memory_order_relaxed);
    }
    void take(uint32_t& ticks, uint32_t& entries) noexcept {
        ticks = ticks_.exchange(0, std::memory_order_relaxed);
        entries = entries_.exchange(0, std::memory_order_relaxed);
    }
private:
    std::atomic<uint32_t> ticks_{0}, entries_{0};
};
inline GuardWait guard_wait;

// UI thread only. Rolling window with the frame profiler's span, capacity and
// nearest-rank p95, so every group of the overlay reads the same way. `counter`
// is an optional cumulative count carried with each sample (underruns); `Payload`
// is whatever else a sample carries, read back through each().
struct Nothing {};
template <size_t Metrics, typename Payload = Nothing>
class Rolling {
public:
    struct Summary {
        frame_profile::Stats metric[Metrics]{};
        size_t samples = 0;
        uint32_t counter_window = 0, counter_total = 0;
    };
    void reset() {
        head_ = count_ = 0;
        base_ = last_ = 0;
        have_base_ = false;
        summary_ = {};
    }
    void add(uint64_t at, const uint32_t (&values)[Metrics], uint32_t counter = 0, const Payload& payload = {}) {
        if (!have_base_) { base_ = counter; have_base_ = true; }
        if (count_ == frame_profile::Capacity) drop_oldest();
        Sample& sample = samples_[(head_ + count_) % frame_profile::Capacity];
        sample.at = at;
        for (size_t m = 0; m < Metrics; ++m) sample.value[m] = values[m];
        sample.counter = last_ = counter;
        sample.payload = payload;
        ++count_;
    }
    // The samples the last refresh() kept, oldest first: visit(values, payload).
    template <typename Visit>
    void each(Visit visit) const {
        for (size_t i = 0; i < count_; ++i) {
            const Sample& sample = samples_[(head_ + i) % frame_profile::Capacity];
            visit(sample.value, sample.payload);
        }
    }
    void refresh(uint64_t now) {
        while (count_ && now >= samples_[head_].at && now - samples_[head_].at >= frame_profile::WindowUs)
            drop_oldest();
        summary_ = {};
        summary_.samples = count_;
        summary_.counter_total = last_;
        summary_.counter_window = last_ - base_;   // unsigned: also right across a counter wrap
        for (size_t m = 0; m < Metrics && count_; ++m) {
            uint64_t sum = 0;
            for (size_t i = 0; i < count_; ++i) {
                const uint32_t value = samples_[(head_ + i) % frame_profile::Capacity].value[m];
                scratch_[i] = value;
                sum += value;
            }
            std::sort(scratch_, scratch_ + count_);   // bounded, no allocation; 4 Hz at most
            summary_.metric[m] = {uint32_t(sum / count_),
                scratch_[(95 * count_ + 99) / 100 - 1], scratch_[count_ - 1]};
        }
    }
    const Summary& summary() const { return summary_; }

private:
    struct Sample { uint64_t at; uint32_t value[Metrics]; uint32_t counter; Payload payload; };
    void drop_oldest() {
        base_ = samples_[head_].counter;   // counts up to here are no longer in the window
        head_ = (head_ + 1) % frame_profile::Capacity;
        --count_;
    }
    Sample samples_[frame_profile::Capacity]{};
    uint32_t scratch_[frame_profile::Capacity]{};
    Summary summary_{};
    size_t head_ = 0, count_ = 0;
    uint32_t base_ = 0, last_ = 0;
    bool have_base_ = false;
};

// Per audio block, microseconds. Total is the per-block sum, so its p95 and max
// belong to one real block rather than to four unrelated ones.
enum Stage : size_t { Lock, Dsp, Snap, Out, Total, StageCount };
// The rest of a block, kept beside its stage times in the same window.
struct Seen {
    uint32_t period_us = 0;   // 0 = unknown
    uint8_t queue = 0, state = 0, units = 0, voices = 0;
};
using Blocks = Rolling<StageCount, Seen>;
inline void add_block(Blocks& window, uint64_t at, uint32_t lock_us, uint32_t dsp_us,
                      uint32_t snap_us, uint32_t out_us, uint32_t xruns, const Seen& seen = {}) {
    const uint32_t values[StageCount] = {lock_us, dsp_us, snap_us, out_us,
        frame_profile::duration(uint64_t(lock_us) + dsp_us + snap_us + out_us)};
    window.add(at, values, xruns, seen);
}

// What the blocks of the window say besides their stage times. Call after refresh().
struct Observed {
    uint32_t blocks = 0;
    uint32_t late = 0;         // blocks whose stages took longer than BudgetUs
    uint32_t rate_x10 = 0;     // blocks produced per second, tenths; 0 = unknown
    uint8_t queued_min = 0;    // fewest other buffers NDSP held at a hand-off; 0 = it ran dry
    uint8_t depth = 0;         // queue depth in use (4 or 8)
    uint8_t voices_max = 0;
    Seen last{};               // newest block: transport, core, units, voices
};
inline Observed observe(const Blocks& window) {
    Observed seen;
    uint64_t periods = 0;
    uint32_t timed = 0;
    window.each([&](const uint32_t (&value)[StageCount], const Seen& block) {
        if (value[Total] > BudgetUs) ++seen.late;
        const uint8_t held = block.queue & 15u;
        if (!seen.blocks || held < seen.queued_min) seen.queued_min = held;
        seen.depth = block.queue >> 4;
        if (block.voices > seen.voices_max) seen.voices_max = block.voices;
        // A second or more between two blocks is a pause (sleep, suspend), not a rate.
        if (block.period_us && block.period_us < 1000000u) { periods += block.period_us; ++timed; }
        seen.last = block;
        ++seen.blocks;
    });
    if (periods) seen.rate_x10 = uint32_t((uint64_t(timed) * 10000000u + periods / 2) / periods);
    return seen;
}

// Per UI frame: microseconds spent waiting for the guard, and how many entries.
enum Guard : size_t { Waited, Taken, GuardCount };
using Frames = Rolling<GuardCount>;

static_assert(sizeof(Blocks) < 10240 && sizeof(Frames) < 8192 && sizeof(Feed) < 2048,
              "audio profiler storage must stay bounded");

} // namespace seance::platform::audio_profile
