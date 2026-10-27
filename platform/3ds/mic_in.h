// mic input driver - adapted from descry's mic_3ds (same author, gpl).
// drains MICU's hardware ring into caller-provided buffers; main loop
// forwards those to MicUnit::push under the audio lock.
// init() may fail (emulator without mic) - caller keeps running, no crash.
#pragma once
#include <3ds.h>
#include <cstdint>
#include <cstddef>
#include "../../core/audio/fixed.h"

namespace seance::platform {

class MicIn {
public:
    bool init();                // full bring-up: sysmod + start sampling
    void shutdown();
    bool ok() const { return recording_; }

    // drain new mic samples (q15 mono @ ~32.7k), returns frames read
    int poll(trackr::fx::q15* out, int max_frames);

private:
    static constexpr std::size_t MIC_BUF_BYTES = 0x30000;   // 192kb, 0x1000-aligned

    uint8_t* mic_buf_ = nullptr;
    uint32_t data_size_ = 0;    // aligned size from micGetSampleDataSize()
    uint32_t read_pos_ = 0;
    bool sysmod_ = false;
    bool recording_ = false;
};

} // namespace seance::platform
