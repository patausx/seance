// ndsp backend - straight port of descry's battle-tested audio path.
// callback -> LightEvent -> core-1 worker -> Engine::render under RecursiveLock.
// watchdog wakes every 100ms even if the ndsp callback goes missing (apt edge cases).
#include "audio_out.h"
#include <cstring>

namespace seance::platform {

static LightEvent* g_wake_for_cb = nullptr;

static void ndsp_callback(void*) {
    if (g_wake_for_cb) LightEvent_Signal(g_wake_for_cb);
}

static void apt_hook_cb(APT_HookType type, void* opaque) {
    switch (type) {
        case APTHOOK_ONRESTORE:
        case APTHOOK_ONWAKEUP:
            APT_SetAppCpuTimeLimit(80);
            ndspChnSetPaused(0, false);
            if (g_wake_for_cb) LightEvent_Signal(g_wake_for_cb);
            break;
        case APTHOOK_ONSUSPEND:
        case APTHOOK_ONSLEEP:
            static_cast<AudioOut*>(opaque)->interrupt_recording();
            ndspChnSetPaused(0, true);
            APT_SetAppCpuTimeLimit(10);
            break;
        default: break;
    }
}

static void worker_trampoline(void* arg) {
    static_cast<AudioOut*>(arg)->worker_loop();
}

bool AudioOut::init(Engine& engine) {
    engine_ = &engine;
    ui_focus_ = 0xffffffffu;
    ui_snapshots_.reset_quiescent();
    engine.capture_ui_snapshot(ui_snapshots_.producer_buffer());
    ui_snapshots_.publish();

    // Hardware-dependent queue depth. New 3DS (804 MHz, speedup enabled below)
    // renders a 16 ms block with wide margin -> short queue for playable latency.
    // Old 3DS (268 MHz) needs the deep queue: A/B testing showed underrun
    // crackle at 64 ms under real workloads (grains/delay + UI lock holds).
    bool is_new = false;
    APT_CheckNew3DS(&is_new);
    if (is_new) osSetSpeedupEnable(true);   // New 3DS always uses its full CPU speed.
    num_buffers_ = is_new ? 4 : NUM_BUFFERS;

    if (R_FAILED(ndspInit())) return false;   // no dspfirm.cdc -> ui-only mode

    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnReset(0);
    ndspChnSetInterp(0, NDSP_INTERP_POLYPHASE);
    ndspChnSetRate(0, ENGINE_SR);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);

    float mix[12] = {0};
    mix[0] = 1.0f; mix[1] = 1.0f;
    ndspChnSetMix(0, mix);

    const std::size_t bytes = FRAMES_PER_BUF * 2 * sizeof(int16_t);
    for (int i = 0; i < num_buffers_; ++i) {
        buf_data_[i] = static_cast<int16_t*>(linearAlloc(bytes));
        if (!buf_data_[i]) {
            for (int j = 0; j < i; ++j) {
                linearFree(buf_data_[j]);
                buf_data_[j] = nullptr;
            }
            ndspExit();
            return false;
        }
        std::memset(buf_data_[i], 0, bytes);
        std::memset(&wave_bufs_[i], 0, sizeof(ndspWaveBuf));
        wave_bufs_[i].data_vaddr = buf_data_[i];
        wave_bufs_[i].nsamples   = FRAMES_PER_BUF;
        wave_bufs_[i].status     = NDSP_WBUF_DONE;
    }

    LightEvent_Init(&wake_event_, RESET_ONESHOT);
    g_wake_for_cb   = &wake_event_;
    ndspSetCallback(ndsp_callback, nullptr);
    aptHook(&apt_cookie_, apt_hook_cb, this);

    s32 main_prio = 0x30;
    svcGetThreadPriority(&main_prio, CUR_THREAD_HANDLE);
    // Slightly above main: the audio worker must win the race for a free
    // buffer slot. It sleeps on the LightEvent between refills, so it cannot
    // starve the UI; equal priority let a busy UI frame delay refills into
    // the shrunken queue. (Lower value = higher priority on 3DS.)
    s32 worker_prio = main_prio - 1;
    if (worker_prio < 0x18) worker_prio = 0x18;

    APT_SetAppCpuTimeLimit(80);   // descry lesson: 30% ceiling = underruns

    thread_run_ = true;
    // From this point shutdown() must unwind callbacks, hook, NDSP and buffers
    // even when both worker thread creation attempts fail.
    initialized_ = true;
    worker_handle_ = threadCreate(worker_trampoline, this, 0x6000, worker_prio, 1, false);
    if (!worker_handle_)   // old-3ds fallback: default core
        worker_handle_ = threadCreate(worker_trampoline, this, 0x6000, worker_prio, -1, false);
    if (!worker_handle_) { thread_run_ = false; shutdown(); return false; }

    return true;
}

void AudioOut::interrupt_recording() {
    Guard guard(*this);
    if (engine_) {
        engine_->interrupt_recorders();
        // Audio may stop during sleep/suspend: a sample-count lease alone cannot
        // expire then. Cancel focused dry auditions under this existing guard;
        // wake restores the current mix with the normal short audio ramp.
        for (auto& chain : engine_->chains) for (Unit* unit : chain.units)
            if (unit && unit->kind == UnitKind::Chorus)
                static_cast<ChorusUnit*>(unit)->set_dry_compare(false);
    }
}

void AudioOut::shutdown() {
    if (!initialized_ && !worker_handle_) {
        // partial init cleanup
        for (int i = 0; i < NUM_BUFFERS; ++i)
            if (buf_data_[i]) { linearFree(buf_data_[i]); buf_data_[i] = nullptr; }
        return;
    }

    thread_run_ = false;
    LightEvent_Signal(&wake_event_);
    if (worker_handle_) {
        threadJoin(worker_handle_, U64_MAX);
        threadFree(worker_handle_);
        worker_handle_ = nullptr;
    }

    aptUnhook(&apt_cookie_);
    ndspSetCallback(nullptr, nullptr);
    g_wake_for_cb = nullptr;

    APT_SetAppCpuTimeLimit(10);   // give core1 back (hbmenu inherits the limit)

    ndspChnReset(0);
    for (int i = 0; i < NUM_BUFFERS; ++i) {
        if (buf_data_[i]) linearFree(buf_data_[i]);
        buf_data_[i] = nullptr;
    }
    ndspExit();
    initialized_ = false;
}

bool AudioOut::read_ui_snapshot(EngineUiSnapshot& out) const {
    return ui_snapshots_.read_latest(out);
}

void AudioOut::set_ui_focus(int chain, int slot, int sigil) {
    uint32_t packed = 0xffffffffu;
    if ((chain >= 0 && chain < NUM_CHAINS && slot >= 0 && slot < UNITS_PER_CHAIN) ||
        (sigil >= 0 && sigil < NUM_SIGILS)) {
        const uint32_t packed_chain = chain >= 0 ? (uint32_t)chain : 0xffu;
        const uint32_t packed_slot = slot >= 0 ? (uint32_t)slot : 0xffu;
        const uint32_t packed_sigil = sigil >= 0 ? (uint32_t)sigil : 0xffu;
        packed = packed_chain | (packed_slot << 8) | (packed_sigil << 16);
    }
    __atomic_store_n(&ui_focus_, packed, __ATOMIC_RELAXED);
}

void AudioOut::worker_loop() {
    // Watchdog under one buffer period (16 ms): if the ndsp callback vanishes
    // (apt sleep/suspend edges), we still poll fast enough to keep the ~64 ms
    // queue from draining before the next wake.
    constexpr s64 WATCHDOG_NS = 12'000'000LL;

    while (thread_run_) {
        LightEvent_WaitTimeout(&wake_event_, WATCHDOG_NS);
        if (!thread_run_) break;

        if (primed_) {
            int done_count = 0;
            for (int n = 0; n < num_buffers_; ++n)
                if (wave_bufs_[n].status == NDSP_WBUF_DONE) ++done_count;
            if (done_count == num_buffers_) ++xrun_count_;
        }

        // NDSP consumes buffers in add order, so refill the completed prefix of
        // our ring strictly in sequence. Do not search for arbitrary DONE slots:
        // mutating next_buf_ inside such a scan used to enqueue 0,2,1,... when
        // several buffers completed together, splicing non-adjacent audio blocks
        // and producing loud periodic crackle (most obvious on a steady OSC).
        for (int filled = 0; filled < num_buffers_; ++filled) {
            const int idx = next_buf_;
            auto& wb = wave_bufs_[idx];
            if (wb.status != NDSP_WBUF_DONE) break;

            {
                const u64 started = svcGetSystemTick();
                Guard _g(*this);
                engine_->render(reinterpret_cast<q15*>(buf_data_[idx]), FRAMES_PER_BUF);

                auto& snapshot = ui_snapshots_.producer_buffer();
                const uint32_t focus = __atomic_load_n(&ui_focus_, __ATOMIC_RELAXED);
                const int focus_chain = focus == 0xffffffffu || (focus & 0xffu) == 0xffu
                    ? -1 : (int)(focus & 0xffu);
                const int focus_slot = focus == 0xffffffffu || ((focus >> 8) & 0xffu) == 0xffu
                    ? -1 : (int)((focus >> 8) & 0xffu);
                const int focus_sigil = focus == 0xffffffffu || ((focus >> 16) & 0xffu) == 0xffu
                    ? -1 : (int)((focus >> 16) & 0xffu);
                engine_->capture_ui_snapshot(snapshot, focus_chain, focus_slot, focus_sigil);

                // Include snapshot capture in the worker cost. Publishing the old
                // render-only number hid telemetry overhead from the load meter.
                // SYSCLOCK_ARM11 ticks -> microseconds without 64-bit division:
                // us = ticks * 64 / (268123480 * 64 / 1e6) ~= ticks / 268.
                const u64 elapsed_ticks = svcGetSystemTick() - started;
                const uint32_t elapsed_us = (uint32_t)(elapsed_ticks / (SYSCLOCK_ARM11 / 1000000u));
                engine_->render_last_us = elapsed_us;
                if (elapsed_us > engine_->render_peak_us) engine_->render_peak_us = elapsed_us;
                const uint32_t budget_us = (uint32_t)((uint64_t)FRAMES_PER_BUF * 1000000u / ENGINE_SR);
                const uint32_t load = budget_us ? elapsed_us * 1000u / budget_us : 0;
                engine_->dsp_load_permille = (uint16_t)(load > 999 ? 999 : load);
                // capture_ui_snapshot ran before the final total was known, so
                // patch these scalars before transferring ownership to the UI.
                snapshot.render_peak_us = engine_->render_peak_us;
                snapshot.dsp_load_permille = engine_->dsp_load_permille;
                snapshot.xrun_count = xrun_count_;
                ui_snapshots_.publish();
            }

            DSP_FlushDataCache(buf_data_[idx], FRAMES_PER_BUF * 2 * sizeof(int16_t));
            ndspChnWaveBufAdd(0, &wb);
            primed_ = true;
            next_buf_ = (idx + 1) % num_buffers_;
        }
    }
}

} // namespace seance::platform
