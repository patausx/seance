// 3ds audio output via ndsp for the seance Engine.
// architecture inherited from descry v2 (no-crackle):
//   ndsp_callback (ISR) -> LightEvent -> worker thread on core 1 -> Engine::render
// main loop never touches audio. ui talks to the engine under lock().
#pragma once
#include "ui_snapshot_mailbox.h"
#include <3ds.h>
#ifdef SEANCE_DIAGNOSTIC_OVERLAY
#include "audio_profile.h"
#endif

namespace seance::platform {

constexpr int FRAMES_PER_BUF = 512;    // 16 ms blocks: live-instrument latency
constexpr int NUM_BUFFERS    = 8;      // MAX queue depth (static arrays). The depth
                                       // actually used is chosen at runtime in init():
                                       //   New 3DS (804 MHz): 4 -> ~64 ms, responsive
                                       //   Old 3DS (268 MHz): 8 -> ~128 ms, underrun-proof
                                       // A/B on hardware showed Old 3DS underruns at 64 ms.

class AudioOut {
public:
    AudioOut() { RecursiveLock_Init(&lock_); }
    // returns false when ndsp can't start (no dspfirm.cdc etc). caller decides
    // whether to continue without sound (ui-only mode) - seance does NOT exit.
    bool init(Engine& engine);
    void shutdown();
    // Lifecycle interruption finalizes open takes; wake never resumes implicitly.
    void interrupt_recording();
    // APT authority is independent of NDSP availability and covers startup too.
    void attach_lifecycle();
    // Recovery startup only: runtime output permission, not stored mutes/bases.
    // Guard-owned; init/wake never reset this. Only a routed fresh play/chain A
    // authorizes output. While inhibited no engine sample loop is advanced.
    void inhibit_recovered_output() { recovery_inhibited_ = true; }
    void begin_recovery_input(uint32_t observed_epoch) { recovery_input_epoch_ = observed_epoch; }
    void authorize_recovered_output() {
        if (recovery_input_epoch_ == lifecycle_epoch()) recovery_inhibited_ = false;
    }
    bool recovered_output_inhibited() const { return recovery_inhibited_; }
    // Guard-owned private GRAN pad lease; callback never reads UI state.
    void track_grain_pad(const Unit* unit, uint32_t token) { lifecycle_grain_ = unit; lifecycle_grain_token_ = token; }
    uint32_t lifecycle_epoch() const { return __atomic_load_n(&lifecycle_epoch_, __ATOMIC_ACQUIRE); }

    bool     alive() const     { return initialized_; }
    uint32_t underruns() const { return xrun_count_; }
    int      queue_depth() const { return num_buffers_; }   // buffers in use (hw-dependent)

    // Lock-free SPSC telemetry: audio worker writes, main UI thread reads.
    // At most one copy; false means no newer frame and leaves out unchanged.
    bool read_ui_snapshot(EngineUiSnapshot& out) const;
    // Controller hints for focused-only unit telemetry and the selected-Sigil
    // aggregate patch map. Atomic packed storage avoids the audio lock per frame.
    void set_ui_focus(int chain, int slot, int sigil);

    // ui-side critical section around any engine mutation
#ifdef SEANCE_DIAGNOSTIC_OVERLAY
    // Diagnostic builds time every wait for the guard from this side; the worker
    // enters the same guard directly and reports its own wait per block.
    void lock() {
        const u64 asked = svcGetSystemTick();
        RecursiveLock_Lock(&lock_);
        audio_profile::guard_wait.waited(svcGetSystemTick() - asked);
    }
#else
    void lock()   { RecursiveLock_Lock(&lock_); }
#endif
    void unlock() { RecursiveLock_Unlock(&lock_); }
    struct Guard {
        AudioOut& a;
        explicit Guard(AudioOut& x) : a(x) { a.lock(); }
        ~Guard() { a.unlock(); }
    };

    void worker_loop();   // internal (trampoline target)
    void render_output(Engine& engine, int16_t* output, int frames); // internal; Guard held

private:
    // The worker's own entry into the same lock. Kept apart from lock() so a
    // diagnostic build times the two sides separately: UI callers waiting for
    // audio in lock(), the worker waiting for the UI in worker_loop().
    struct WorkerGuard {
        RecursiveLock& held;
        explicit WorkerGuard(RecursiveLock& lock) : held(lock) { RecursiveLock_Lock(&held); }
        ~WorkerGuard() { RecursiveLock_Unlock(&held); }
    };

    Engine*       engine_ = nullptr;
    ndspWaveBuf   wave_bufs_[NUM_BUFFERS];
    int16_t*      buf_data_[NUM_BUFFERS] = {};
    int           next_buf_ = 0;
    int           num_buffers_ = NUM_BUFFERS;   // runtime depth, set in init()

    LightEvent    wake_event_;
    Thread        worker_handle_ = nullptr;
    RecursiveLock lock_;
    aptHookCookie apt_cookie_;
    bool lifecycle_attached_ = false;
    uint32_t lifecycle_epoch_ = 0;
    const Unit* lifecycle_grain_ = nullptr;
    uint32_t lifecycle_grain_token_ = 0;
    volatile bool thread_run_ = false;
    bool          initialized_ = false;
    bool          recovery_inhibited_ = false;
    uint32_t      recovery_input_epoch_ = 0;
    volatile uint32_t xrun_count_ = 0;
    volatile bool primed_ = false;

    UiSnapshotMailbox ui_snapshots_;
    volatile uint32_t ui_focus_ = 0xffffffffu;
};

} // namespace seance::platform
