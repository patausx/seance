// The production mailbox and real DTO. No NDSP/GPU/audio-engine simulation.
#include "../platform/3ds/ui_snapshot_mailbox.h"
#include <cassert>
#include <cstdio>
#include <thread>

using seance::EngineUiSnapshot;
using seance::platform::UiSnapshotMailbox;

// Force a real producer lap halfway through the mailbox's exact memcpy, without
// adding a test hook to runtime code. Thread-local instrumentation is race-free.
static thread_local unsigned copies = 0;
static thread_local void (*copy_hook)(const void*) = nullptr;
extern "C" void* __real_memcpy(void*, const void*, size_t);
extern "C" void* __wrap_memcpy(void* dst, const void* src, size_t n) {
    if (n == sizeof(EngineUiSnapshot)) {
        ++copies;
        if (copy_hook) {
            const auto hook = copy_hook;
            copy_hook = nullptr;
            __real_memcpy(dst, src, n / 2);
            hook(src);
            __real_memcpy(static_cast<unsigned char*>(dst) + n / 2,
                          static_cast<const unsigned char*>(src) + n / 2, n - n / 2);
            return dst;
        }
    }
    return __real_memcpy(dst, src, n);
}

static void fill(EngineUiSnapshot& s, uint32_t generation) {
    // Defined representations (including bools), deterministic padding, and
    // generation invariants at the beginning, middle and end of the actual DTO.
    std::memset(static_cast<void*>(&s), 0, sizeof s);
    s.master_serial = generation;
    s.transport_playing = (generation & 1) != 0;
    s.beat_count = (uint64_t(generation) << 32) | ~generation;
    s.bpm = 60 + generation % 240;
    s.frame_in_beat = generation ^ 0x12345678u;
    for (int c = 0; c < seance::NUM_CHAINS; ++c) {
        s.chain_peak[c] = (generation + c * 23u) % 32768;
        s.sequence_pitch_lock_mask[c] = s.beat_count ^ uint64_t(c);
        for (int u = 0; u < seance::UNITS_PER_CHAIN; ++u)
            s.rec_frames[c][u] = generation ^ (c * 31u + u * 47u);
        for (int i = 0; i < seance::SEQ_STEPS; ++i) {
            s.sequence_meta[c][i] = (generation + c * 19u + i * 13u) & 0xffffu;
            s.sequence_pitch[c][i] = int((generation + i * 31u) % 32768) - 16384;
            s.sequence_value[c][i] = int((generation + c * 7u + i * 17u) % 32768) - 16384;
        }
    }
    for (int i = 0; i < 200; ++i) s.scope[i] = int((generation + i * 137u) % 32768) - 16384;
    s.render_peak_us = generation ^ 0x55aa33ccu;
    s.dsp_load_permille = generation % 1000;
    s.xrun_count = ~generation;
    for (int i = 0; i < seance::NUM_SIGILS; ++i) s.sigil_phase[i] = generation ^ (i * 73u);
}

static void verify(const EngineUiSnapshot& s, uint32_t generation) {
    EngineUiSnapshot expected;
    fill(expected, generation);
    assert(std::memcmp(&s, &expected, sizeof s) == 0);
}
static void publish(UiSnapshotMailbox& m, uint32_t generation) {
    fill(m.producer_buffer(), generation);
    m.publish();
}
static void read(UiSnapshotMailbox& m, EngineUiSnapshot& out, bool fresh, uint32_t generation) {
    const auto before = copies;
    assert(m.read_latest(out) == fresh);
    assert(copies - before == unsigned(fresh)); // zero or exactly one payload copy
    verify(out, generation);
}

static void lifecycle() {
    UiSnapshotMailbox m;
    EngineUiSnapshot out;
    fill(out, 999);
    read(m, out, false, 999);
    fill(m.producer_buffer(), 1);
    read(m, out, false, 999); // writer is still filling; nothing published
    m.publish();
    read(m, out, true, 1);
    for (int i = 0; i < 8; ++i) read(m, out, false, 1);
    for (uint32_t i = 2; i <= 257; ++i) publish(m, i);
    fill(m.producer_buffer(), 258); // unpublished next frame must not leak
    read(m, out, true, 257);
    read(m, out, false, 257);
    m.publish();
    read(m, out, true, 258);
    publish(m, UINT32_MAX); read(m, out, true, UINT32_MAX);
    publish(m, 0); read(m, out, true, 0); // no generation counter / wrap ambiguity
    publish(m, 123);
    m.reset_quiescent();
    read(m, out, false, 0); // no stale unread frame survives reset
    publish(m, 7); read(m, out, true, 7);
    std::puts("pass snapshot lifecycle: empty/start/stalled producer/latest/reset/wrap; 0-or-1 copy");
}

static UiSnapshotMailbox* interrupted_mailbox = nullptr;
static void lap_reader(const void* protected_slot) {
    for (uint32_t i = 8; i <= 263; ++i) {
        assert(&interrupted_mailbox->producer_buffer() != protected_slot);
        publish(*interrupted_mailbox, i);
    }
}
static void interrupted_copy() {
    UiSnapshotMailbox m;
    EngineUiSnapshot out;
    publish(m, 7);
    interrupted_mailbox = &m;
    copy_hook = lap_reader;
    read(m, out, true, 7); // 256 publications during memcpy; still one whole frame
    assert(!copy_hook);
    interrupted_mailbox = nullptr;
    read(m, out, true, 263);
    read(m, out, false, 263);
    std::puts("pass snapshot interrupted memcpy: 256 producer laps never touch the reader-owned slot");
}

static void concurrent() {
    UiSnapshotMailbox m;
    EngineUiSnapshot out;
    fill(out, 0);
    constexpr uint32_t count = 40000;
    std::atomic<bool> start{false};
    std::thread writer([&] {
        while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
        for (uint32_t i = 1; i <= count; ++i) {
            publish(m, i);
            if (!(i & 127u)) std::this_thread::yield();
        }
    });
    start.store(true, std::memory_order_release);
    uint32_t last = 0, received = 0;
    while (last < count) {
        const auto before = copies;
        if (m.read_latest(out)) {
            assert(copies == before + 1);
            assert(out.master_serial > last && out.master_serial <= count);
            last = out.master_serial;
            ++received;
            verify(out, last);
            if (!(received & 31u)) std::this_thread::yield();
        } else {
            assert(copies == before);
            verify(out, last); // byte-identical on every empty read, not just serial
            std::this_thread::yield();
        }
    }
    writer.join();
    read(m, out, false, count);
    std::printf("pass snapshot threads: %u publications, %u coherent increasing reads, final delivered\n", count, received);
}

int main() {
    lifecycle();
    interrupted_copy();
    concurrent();
    std::printf("snapshot bytes=%zu, three-slot mailbox bytes=%zu; no hardware timing claims\n",
                sizeof(EngineUiSnapshot), sizeof(UiSnapshotMailbox));
}
