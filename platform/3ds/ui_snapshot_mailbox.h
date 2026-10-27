#pragma once
#include "../../core/engine/engine.h"
#include <atomic>
#include <cstring>
#include <type_traits>

namespace seance::platform {

// Exactly one producer (audio worker) and one consumer (main UI thread).
// Each owns a slot; the third slot is exchanged atomically. An unread frame may
// be superseded, but the producer can never overwrite the consumer's copy source.
// No heap, engine lock or payload retry loop. Do not share between UI readers.
class UiSnapshotMailbox {
public:
    // Only before starting the worker / after joining it, with no active reader.
    // Old payloads are inaccessible until the producer fills and publishes again.
    void reset_quiescent() noexcept {
        producer_ = 0;
        consumer_ = 1;
        middle_.store(2, std::memory_order_relaxed);
    }

    // Producer only. This reference must not be used after publish().
    EngineUiSnapshot& producer_buffer() noexcept { return slots_[producer_]; }

    void publish() noexcept {
        // Release the complete frame; acquire the returned slot before reusing it
        // (it may be a former consumer slot, released after that copy finished).
        producer_ = middle_.exchange(producer_ | kReady, std::memory_order_acq_rel) & kIndex;
    }

    // Consumer only. False means no newer publication: out remains byte-identical.
    // The initial snapshot is published before the worker starts, even in no-DSP
    // mode. Repeated reads without a new audio block do not copy the payload.
    bool read_latest(EngineUiSnapshot& out) const noexcept {
        if (!(middle_.load(std::memory_order_relaxed) & kReady)) return false;
        // Only this consumer clears kReady. A concurrent publish can replace the
        // middle slot before exchange, but still hands us a complete, newer frame.
        consumer_ = middle_.exchange(consumer_, std::memory_order_acq_rel) & kIndex;
        std::memcpy(&out, &slots_[consumer_], sizeof out);
        // Retain ownership through the copy and until the next successful read.
        return true;
    }

private:
    static_assert(std::atomic<uint32_t>::is_always_lock_free,
                  "snapshot handoff requires native lock-free 32-bit atomics");
    static_assert(std::is_trivially_copyable<EngineUiSnapshot>::value,
                  "snapshot payload must stay safe to memcpy");
    static constexpr uint32_t kReady = 4, kIndex = 3;
    EngineUiSnapshot slots_[3];
    uint32_t producer_ = 0;                 // audio thread only
    mutable uint32_t consumer_ = 1;          // UI thread only
    mutable std::atomic<uint32_t> middle_{2};
};
static_assert(sizeof(UiSnapshotMailbox) <= 3 * sizeof(EngineUiSnapshot) + 16,
              "snapshot mailbox must remain bounded to three payloads + indices");

} // namespace seance::platform
