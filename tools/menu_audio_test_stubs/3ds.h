#pragma once
// Narrow single-worker NDSP shim. Consumption occurs at the worker's wait point;
// no physical DSP, performance, speaker sound or device sleep claims.
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>
using s32 = int32_t;
using s64 = int64_t;
constexpr uint64_t U64_MAX = UINT64_MAX;
constexpr int CUR_THREAD_HANDLE = 0, RESET_ONESHOT = 0;
constexpr int NDSP_OUTPUT_STEREO = 0, NDSP_INTERP_POLYPHASE = 0, NDSP_FORMAT_STEREO_PCM16 = 6;
constexpr int NDSP_WBUF_DONE = 0;
#define R_FAILED(x) ((x) < 0)
struct ndspWaveBuf {
    void* data_vaddr = nullptr;
    unsigned nsamples = 0;
    volatile int status = NDSP_WBUF_DONE;
};
struct LightEvent { std::mutex mutex; std::condition_variable cv; bool signaled = false; };
using Thread = std::thread*;
enum APT_HookType { APTHOOK_ONRESTORE, APTHOOK_ONWAKEUP, APTHOOK_ONSUSPEND, APTHOOK_ONSLEEP };
using AptCallback = void(*)(APT_HookType, void*);
struct aptHookCookie {};
namespace fake_ndsp {
inline std::atomic<int> refs{0}, allocations{0}, queued{0}, waits{0}, nonzero{0}, max_queue{0};
inline std::atomic<bool> paused{false};
inline std::vector<ndspWaveBuf*> pending;
inline bool init_fail = false, thread_fail = false;
inline int allocation_fail_at = -1, alloc_calls = 0;
inline AptCallback hook = nullptr;
inline void* hook_arg = nullptr;
inline void consume() {
    if (paused) return;
    for (auto* b : pending) b->status = NDSP_WBUF_DONE;
    pending.clear();
}
}
inline int ndspInit() { if (fake_ndsp::init_fail) return -1; ++fake_ndsp::refs; return 0; }
inline void ndspExit() { assert(fake_ndsp::refs > 0); --fake_ndsp::refs; }
inline void ndspSetOutputMode(int) {}
inline void ndspChnReset(int channel) {
    assert(channel == 1); // this feature must never reset channel0
    fake_ndsp::paused = false;
    fake_ndsp::consume();
}
inline void ndspChnSetRate(int c, float rate) { assert(c == 1 && rate == 32000); }
inline void ndspChnSetInterp(int c, int) { assert(c == 1); }
inline void ndspChnSetFormat(int c, int format) { assert(c == 1 && format == NDSP_FORMAT_STEREO_PCM16); }
inline void ndspChnSetMix(int c, float* mix) { assert(c == 1 && mix[0] == 1 && mix[1] == 1); }
inline void ndspChnSetPaused(int c, bool p) { assert(c == 1); fake_ndsp::paused = p; }
inline void ndspChnWaveBufAdd(int c, ndspWaveBuf* b) {
    assert(c == 1 && b->status == NDSP_WBUF_DONE && b->nsamples == 512);
    for (unsigned i = 0; i < b->nsamples * 2; ++i)
        if (static_cast<int16_t*>(b->data_vaddr)[i]) { ++fake_ndsp::nonzero; break; }
    b->status = 1;
    fake_ndsp::pending.push_back(b);
    assert(fake_ndsp::pending.size() <= 4);
    fake_ndsp::max_queue = int(fake_ndsp::pending.size());
    ++fake_ndsp::queued;
}
inline void* linearAlloc(size_t n) {
    if (fake_ndsp::alloc_calls++ == fake_ndsp::allocation_fail_at) return nullptr;
    assert(n == 2048);
    ++fake_ndsp::allocations;
    return std::malloc(n);
}
inline void linearFree(void* p) { --fake_ndsp::allocations; std::free(p); }
inline void DSP_FlushDataCache(void*, size_t n) { assert(n == 2048); }
inline void LightEvent_Init(LightEvent* e, int) { e->signaled = false; }
inline void LightEvent_Signal(LightEvent* e) {
    std::lock_guard<std::mutex> lock(e->mutex);
    e->signaled = true; e->cv.notify_one();
}
inline void LightEvent_WaitTimeout(LightEvent* e, s64 ns) {
    ++fake_ndsp::waits;
    fake_ndsp::consume();
    std::unique_lock<std::mutex> lock(e->mutex);
    if (ns == INT64_MAX) e->cv.wait(lock, [&] { return e->signaled; });
    else e->cv.wait_for(lock, std::chrono::milliseconds(1), [&] { return e->signaled; });
    e->signaled = false;
}
inline void aptHook(aptHookCookie*, AptCallback hook, void* arg) {
    assert(!fake_ndsp::hook); fake_ndsp::hook = hook; fake_ndsp::hook_arg = arg;
}
inline void aptUnhook(aptHookCookie*) { fake_ndsp::hook = nullptr; fake_ndsp::hook_arg = nullptr; }
inline void svcGetThreadPriority(s32* p, int) { *p = 0x30; }
inline Thread threadCreate(void(*fn)(void*), void* arg, size_t stack, s32 priority, int core, bool detached) {
    assert(stack == 0x3000 && priority == 0x30 && core == -1 && !detached);
    return fake_ndsp::thread_fail ? nullptr : new std::thread(fn, arg);
}
inline void threadJoin(Thread t, uint64_t) { t->join(); }
inline void threadFree(Thread t) { delete t; }
