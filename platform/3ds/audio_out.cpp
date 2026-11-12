// ndsp backend - straight port of descry's battle-tested audio path.
// callback -> LightEvent -> core-1 worker -> Engine::render under RecursiveLock.
// watchdog wakes every 100ms even if the ndsp callback goes missing (apt edge cases).
#include "audio_out.h"
#include "audio_profile.h"
#include <cstring>

namespace seance::platform {

// The overlay calls a block late against this constant; keep it the real one.
static_assert(uint64_t(audio_profile::BudgetUs) * ENGINE_SR == uint64_t(FRAMES_PER_BUF) * 1000000u,
              "diagnostic block budget must equal one block of audio");

static LightEvent* g_wake_for_cb = nullptr;

static void ndsp_callback(void*) {
    if (g_wake_for_cb) LightEvent_Signal(g_wake_for_cb);
}

static void apt_hook_cb(APT_HookType type, void* opaque) {
    switch (type) {
        case APTHOOK_ONRESTORE:
        case APTHOOK_ONWAKEUP:
            static_cast<AudioOut*>(opaque)->interrupt_recording();
            APT_SetAppCpuTimeLimit(80);
            if (static_cast<AudioOut*>(opaque)->alive()) ndspChnSetPaused(0, false);
            if (g_wake_for_cb) LightEvent_Signal(g_wake_for_cb);
            break;
        case APTHOOK_ONSUSPEND:
        case APTHOOK_ONSLEEP:
            static_cast<AudioOut*>(opaque)->interrupt_recording();
            if (static_cast<AudioOut*>(opaque)->alive()) ndspChnSetPaused(0, true);
            APT_SetAppCpuTimeLimit(10);
            break;
        default: break;
    }
}

static void worker_trampoline(void* arg) {
    static_cast<AudioOut*>(arg)->worker_loop();
}

void AudioOut::attach_lifecycle() {
    if (lifecycle_attached_) return;
    aptHook(&apt_cookie_, apt_hook_cb, this);
    lifecycle_attached_ = true;
}

bool AudioOut::init(Engine& engine) {
    attach_lifecycle();
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
    if (!worker_handle_) { thread_run_ = false; shutdown(); attach_lifecycle(); return false; }

    return true;
}

void AudioOut::interrupt_recording() {
    Guard guard(*this);
    if (engine_) {
        engine_->interrupt_recorders();
        engine_->stop_bounce(); // preserve a partial/finalized take; wake never resumes a writer
        // Cancel gesture-owned runtime layers, never musical gates/latch/HOLD,
        // stored bases or step locks. No UI/controller access on the hook thread.
        engine_->cancel_param_temp();
        engine_->cancel_scratch_gesture();
        engine_->cancel_relic_gesture();
        engine_->cancel_sequence_audition();
        engine_->cancel_rot_auditions(); // direct stopped A auditions, including silent preparation
        engine_->release_transition_fx();
        engine_->stop_play_preview(); // lifecycle-only private bus reset; normal lift retains its tail
        engine_->stop_sample_preview();
        engine_->stop_bounce_preview();
        // Audio may stop during sleep/suspend: a sample-count lease alone cannot
        // expire then. Cancel focused dry auditions under this existing guard;
        // wake restores the current mix with the normal short audio ramp.
        for (auto& chain : engine_->chains) for (Unit* unit : chain.units) {
            if (unit && unit->kind == UnitKind::Chorus)
                static_cast<ChorusUnit*>(unit)->set_dry_compare(false);
            if (unit && unit->kind == UnitKind::Fract)
                static_cast<FractUnit*>(unit)->set_manual_grab(false);
            if (unit && unit == lifecycle_grain_ && unit->kind == UnitKind::Grain)
                static_cast<GrainUnit*>(unit)->pad_end(lifecycle_grain_token_);
        }
    }
    lifecycle_grain_ = nullptr; lifecycle_grain_token_ = 0;
    // Release publication follows guarded cancellation. UI observes it BEFORE
    // polling dirty state, commands or any resumed HID/controller dispatch.
    __atomic_add_fetch(&lifecycle_epoch_, 1u, __ATOMIC_RELEASE);
}

void AudioOut::shutdown() {
    if (!initialized_ && !worker_handle_) {
        if (lifecycle_attached_) { aptUnhook(&apt_cookie_); lifecycle_attached_ = false; }
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

    if (lifecycle_attached_) { aptUnhook(&apt_cookie_); lifecycle_attached_ = false; }
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

void AudioOut::render_output(Engine& engine, int16_t* output, int frames) {
    if (recovery_inhibited_) std::memset(output,0,size_t(frames)*2*sizeof(int16_t));
    else engine.render(reinterpret_cast<q15*>(output),frames);
}

void AudioOut::worker_loop() {
    // Watchdog under one buffer period (16 ms): if the ndsp callback vanishes
    // (apt sleep/suspend edges), we still poll fast enough to keep the ~64 ms
    // queue from draining before the next wake.
    constexpr s64 WATCHDOG_NS = 12'000'000LL;

    while (thread_run_) {
        LightEvent_WaitTimeout(&wake_event_, WATCHDOG_NS);
        if (!thread_run_) break;

        // NDSP consumes buffers in add order, so refill the completed prefix of
        // our ring strictly in sequence. Do not search for arbitrary DONE slots:
        // mutating next_buf_ inside such a scan used to enqueue 0,2,1,... when
        // several buffers completed together, splicing non-adjacent audio blocks
        // and producing loud periodic crackle (most obvious on a steady OSC).
        for (int filled = 0; filled < num_buffers_; ++filled) {
            const int idx = next_buf_;
            auto& wb = wave_bufs_[idx];
            if (wb.status != NDSP_WBUF_DONE) break;

            // Diagnostic builds only: tick stamps splitting this block into guard
            // wait, render, snapshot and hand-off. `if constexpr` leaves a normal
            // build without the extra clock reads and without the shared feed.
            // Deliberately not initialized: a diagnostic build assigns all four
            // before the push below, and a normal build then compiles to exactly
            // the code it had before the stamps existed.
            [[maybe_unused]] u64 stamp[4];
            // Same rule: transport/core, sounding cards and voices of this block.
            [[maybe_unused]] uint8_t seen[3];
            {
                const u64 started = svcGetSystemTick();
                WorkerGuard _g(lock_);   // the same lock as AudioOut::Guard
                if constexpr (audio_profile::Enabled) { stamp[0] = started; stamp[1] = svcGetSystemTick(); }
                render_output(*engine_,buf_data_[idx],FRAMES_PER_BUF);
                if constexpr (audio_profile::Enabled) stamp[2] = svcGetSystemTick();

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
                const u64 finished = svcGetSystemTick();
                const u64 elapsed_ticks = finished - started;
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
                if constexpr (audio_profile::Enabled) {
                    // Still inside the guard and before the snapshot changes hands:
                    // what this block published, and what its sources were computing.
                    seen[0] = audio_profile::state(snapshot.transport_playing, snapshot.sequencer_enabled,
                                                   svcGetProcessorID());
                    unsigned units = 0, voices = 0;
                    for (int c = 0; c < NUM_CHAINS; ++c) for (int s = 0; s < UNITS_PER_CHAIN; ++s) {
                        if (snapshot.unit_kind[c][s] != (uint8_t)UnitKind::Empty && !snapshot.chain_mute[c] &&
                            !((snapshot.unit_bypass[c] >> s) & 1u)) ++units;
                        // A bypassed source is not processed and keeps its last count.
                        const Unit* unit = engine_->chains[c].units[s];
                        if (!unit || unit->bypass) continue;
                        if (unit->kind == UnitKind::Grain)
                            voices += static_cast<const GrainUnit*>(unit)->visual_active();
                        else if (unit->kind == UnitKind::Rule) {
                            const RuleUnit::Visual rule = static_cast<const RuleUnit*>(unit)->visual();   // scalar copy
                            if (rule.active) voices += rule.voices;
                        }
                    }
                    seen[1] = (uint8_t)units;
                    seen[2] = (uint8_t)(voices > 255 ? 255 : voices);
                }
                ui_snapshots_.publish();
                if constexpr (audio_profile::Enabled) stamp[3] = finished;
            }

            DSP_FlushDataCache(buf_data_[idx], FRAMES_PER_BUF * 2 * sizeof(int16_t));
            // Underrun: every buffer NDSP was given has finished before this one
            // reaches it, so it has been playing nothing. Counted here, where the
            // block is handed over, once per gap. The former test (all buffers
            // done when the worker wakes) could not fire while the worker was the
            // late one: it always ran right after a hand-over, with that buffer
            // still queued, so a render slower than real time or a guard held
            // against the worker left gaps in the sound and the counter at zero.
            // This buffer is done itself, so `held` counts the others.
            int held = 0;
            for (int n = 0; n < num_buffers_; ++n)
                if (wave_bufs_[n].status != NDSP_WBUF_DONE) ++held;
            if (primed_ && !held) ++xrun_count_;
            ndspChnWaveBufAdd(0, &wb);
            // Lock-free hand-off to the overlay; a full queue refuses and counts.
            if constexpr (audio_profile::Enabled) {
                audio_profile::Block block = audio_profile::stages(stamp, svcGetSystemTick(), xrun_count_);
                block.period = audio_profile::cadence.period(stamp[0]);
                // The same count for the overlay: its minimum over the window is
                // how close the queue came to empty, 0 = it was.
                block.queue = audio_profile::queue(held, num_buffers_);
                block.state = seen[0]; block.units = seen[1]; block.voices = seen[2];
                audio_profile::feed.push(block);
            }
            primed_ = true;
            next_buf_ = (idx + 1) % num_buffers_;
        }
    }
}

} // namespace seance::platform
