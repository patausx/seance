#pragma once
// Separate NDSP channel: never enters Engine, REC, bounce, meters or exports.
#include <3ds.h>
#include <cstdint>

namespace seance::platform {
class MenuAudio {
public:
    MenuAudio() = default;
    ~MenuAudio() { shutdown(); }
    MenuAudio(const MenuAudio&) = delete;
    MenuAudio& operator=(const MenuAudio&) = delete;
    bool init(bool enabled);
    void shutdown();
    void set_enabled(bool enabled); // main thread, only after preference commit
    void open_menu();
    void enter_patch();             // main thread, successful transaction only
    void set_patch_audible(bool audible);
private:
    static constexpr int kChannel = 1, kBuffers = 4, kFrames = 512;
    void publish(uint32_t command);
    void worker();
    static void trampoline(void* self);
    static void apt_hook(APT_HookType type, void* self);
    ndspWaveBuf buffers_[kBuffers] = {};
    int16_t* pcm_[kBuffers] = {};
    LightEvent wake_;
    aptHookCookie hook_;
    Thread thread_ = nullptr;
    bool initialized_ = false, hooked_ = false;
    uint32_t ui_command_ = 0; // UI sole writer; worker sees only atomic handoff
    uint32_t command_ = 0, running_ = 0, suspended_ = 0;
};
} // namespace seance::platform
