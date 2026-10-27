#include "menu_audio.h"
#include "menu_sound_player.h"
// bin2s emits aligned immutable PCM and constexpr sizes in generated headers.
// Sizes are not linker symbols in current devkitPro.
#include "menu_drone_bin.h"
#include "menu_entry_bin.h"

namespace seance::platform {
bool MenuAudio::init(bool enabled) {
    if (initialized_) return true;
    // libctru reference-counts NDSP. AudioOut later acquires its own reference,
    // owns channel0/callback, and can start/stop without resetting this channel.
    if (R_FAILED(ndspInit())) return false;
    initialized_ = true;
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnReset(kChannel);
    ndspChnSetRate(kChannel, menu_sound::kRate);
    ndspChnSetInterp(kChannel, NDSP_INTERP_POLYPHASE);
    ndspChnSetFormat(kChannel, NDSP_FORMAT_STEREO_PCM16);
    float mix[12] = {};
    mix[0] = mix[1] = 1;
    ndspChnSetMix(kChannel, mix);
    LightEvent_Init(&wake_, RESET_ONESHOT);
    for (int i = 0; i < kBuffers; ++i) {
        pcm_[i] = static_cast<int16_t*>(linearAlloc(kFrames * 2 * sizeof(int16_t)));
        if (!pcm_[i]) { shutdown(); return false; }
        buffers_[i] = {};
        buffers_[i].data_vaddr = pcm_[i];
        buffers_[i].nsamples = kFrames;
        buffers_[i].status = NDSP_WBUF_DONE;
    }
    ui_command_ = enabled ? 1u : 0u;
    __atomic_store_n(&command_, ui_command_, __ATOMIC_RELEASE);
    __atomic_store_n(&suspended_, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&running_, 1u, __ATOMIC_RELEASE);
    aptHook(&hook_, apt_hook, this);
    hooked_ = true;
    s32 priority = 0x30;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    // Deliberately below the instrument worker's priority, on the default core.
    // Only two PCM voices, no locks/allocations and no engine work here.
    thread_ = threadCreate(trampoline, this, 0x3000, priority, -1, false);
    if (!thread_) { shutdown(); return false; }
    return true;
}

void MenuAudio::shutdown() {
    if (!initialized_) return;
    if (hooked_) { aptUnhook(&hook_); hooked_ = false; }
    __atomic_store_n(&running_, 0u, __ATOMIC_RELEASE);
    LightEvent_Signal(&wake_);
    if (thread_) {
        threadJoin(thread_, U64_MAX);
        threadFree(thread_);
        thread_ = nullptr;
    }
    ndspChnReset(kChannel); // remove every DSP reference before freeing buffers
    for (auto& p : pcm_) {
        if (p) linearFree(p);
        p = nullptr;
    }
    ndspExit();
    initialized_ = false;
}

void MenuAudio::publish(uint32_t command) {
    if (ui_command_ == command) return;
    ui_command_ = command;
    __atomic_store_n(&command_, command, __ATOMIC_RELEASE);
    if (initialized_) LightEvent_Signal(&wake_);
}
void MenuAudio::set_enabled(bool enabled) { publish((ui_command_ & ~1u) | (enabled ? 1u : 0u)); }
void MenuAudio::open_menu() { publish((ui_command_ & ~4u) | 2u); }
void MenuAudio::enter_patch() {
    // Serial increments only on a committed new/load, never on transport START.
    publish(((ui_command_ + 8u) & ~7u) | (ui_command_ & 1u));
}
void MenuAudio::set_patch_audible(bool audible) {
    publish((ui_command_ & ~4u) | (audible ? 4u : 0u));
}
void MenuAudio::trampoline(void* self) { static_cast<MenuAudio*>(self)->worker(); }
void MenuAudio::apt_hook(APT_HookType type, void* self) {
    auto& a = *static_cast<MenuAudio*>(self);
    if (type == APTHOOK_ONSUSPEND || type == APTHOOK_ONSLEEP) {
        __atomic_store_n(&a.suspended_, 1u, __ATOMIC_RELEASE);
        ndspChnSetPaused(kChannel, true);
    } else if (type == APTHOOK_ONRESTORE || type == APTHOOK_ONWAKEUP) {
        ndspChnSetPaused(kChannel, false);
        __atomic_store_n(&a.suspended_, 0u, __ATOMIC_RELEASE);
        LightEvent_Signal(&a.wake_);
    }
}
void MenuAudio::worker() {
    menu_sound::Player player(
        {reinterpret_cast<const int16_t*>(menu_drone_bin), menu_drone_bin_size / 4},
        {reinterpret_cast<const int16_t*>(menu_entry_bin), menu_entry_bin_size / 4});
    int next = 0;
    while (__atomic_load_n(&running_, __ATOMIC_ACQUIRE)) {
        if (__atomic_load_n(&suspended_, __ATOMIC_ACQUIRE)) {
            LightEvent_WaitTimeout(&wake_, 50'000'000LL);
            continue;
        }
        player.command(__atomic_load_n(&command_, __ATOMIC_ACQUIRE));
        // Own no global NDSP callback: the Engine's callback is untouched.
        // Refill completed buffers in order; 4ms poll stays below a 16ms block.
        for (int filled = 0; filled < kBuffers && player.active(); ++filled) {
            auto& b = buffers_[next];
            if (b.status != NDSP_WBUF_DONE) break;
            player.render(pcm_[next], kFrames);
            DSP_FlushDataCache(pcm_[next], kFrames * 2 * sizeof(int16_t));
            ndspChnWaveBufAdd(kChannel, &b);
            next = (next + 1) % kBuffers;
        }
        bool queued = false;
        for (const auto& b : buffers_) queued |= b.status != NDSP_WBUF_DONE;
        // After the tail drains there is no polling, rendering or DSP queue.
        LightEvent_WaitTimeout(&wake_, player.active() || queued ? 4'000'000LL : INT64_MAX);
    }
}
} // namespace seance::platform
