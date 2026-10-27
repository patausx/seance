#include "mic_in.h"
#include <malloc.h>
#include <cstring>
#include <3ds/services/gspgpu.h>

namespace seance::platform {

bool MicIn::init() {
    mic_buf_ = static_cast<uint8_t*>(memalign(0x1000, MIC_BUF_BYTES));
    if (!mic_buf_) return false;
    std::memset(mic_buf_, 0, MIC_BUF_BYTES);
    // The mapping is handed to MICU by micInit; libctru/the service owns the
    // coherency setup. Treating GSP cache maintenance return codes here as a
    // capability probe falsely rejected working retail hardware.
    (void)GSPGPU_FlushDataCache(mic_buf_, MIC_BUF_BYTES);

    if (R_FAILED(micInit(mic_buf_, MIC_BUF_BYTES))) {
        free(mic_buf_);
        mic_buf_ = nullptr;
        return false;               // emulator / no mic sysmod - fine
    }
    sysmod_ = true;

    // buffer size for StartSampling must come from libctru (aligned)
    data_size_ = micGetSampleDataSize();

    // max sr is 32728.498 Hz signed 16-bit. MicUnit continuously resamples this
    // hardware clock into ENGINE_SR instead of periodically dropping FIFO chunks.
    if (R_FAILED(MICU_StartSampling(MICU_ENCODING_PCM16_SIGNED,
                                    MICU_SAMPLE_RATE_32730,
                                    0, data_size_, true))) {
        shutdown();
        return false;
    }
    if (!data_size_ || data_size_ + sizeof(uint32_t) > MIC_BUF_BYTES) {
        shutdown(); return false;
    }
    (void)GSPGPU_InvalidateDataCache(mic_buf_ + data_size_, sizeof(uint32_t));
    __dmb();
    read_pos_ = micGetLastSampleOffset();
    // A zero cursor is normal immediately after StartSampling. An odd/out-of-range
    // cursor is ignored until MICU publishes a sane value instead of killing input.
    if (read_pos_ >= data_size_ || (read_pos_ & 1u)) read_pos_ = 0;
    recording_ = true;
    return true;
}

void MicIn::shutdown() {
    if (recording_) { MICU_StopSampling(); recording_ = false; }
    if (sysmod_)    { micExit(); sysmod_ = false; }
    if (mic_buf_)   { free(mic_buf_); mic_buf_ = nullptr; }
    data_size_ = read_pos_ = 0;
}

int MicIn::poll(trackr::fx::q15* out, int max_frames) {
    if (!recording_ || !out || max_frames <= 0 || !data_size_) return 0;

    // libctru reads the producer cursor directly from the final shared-memory word;
    // invalidate it before reading or a cached startup zero can mean permanent silence.
    (void)GSPGPU_InvalidateDataCache(mic_buf_ + data_size_, sizeof(uint32_t));
    __dmb();
    uint32_t write_pos = micGetLastSampleOffset();
    // During startup/recovery retain the last good cursor and retry next UI tick.
    if (write_pos >= data_size_ || (write_pos & 1u)) return 0;
    uint32_t avail_bytes = write_pos >= read_pos_
        ? write_pos - read_pos_
        : (data_size_ - read_pos_) + write_pos;
    avail_bytes &= ~1u;                          // 16-bit alignment

    int frames = (int)(avail_bytes / 2);
    if (frames > max_frames) frames = max_frames;
    if (frames <= 0) return 0;

    // MICU writes through shared memory, outside the CPU cache hierarchy. Without
    // invalidation Old/New 3DS may keep returning the zero-filled startup cache
    // lines forever even while micGetLastSampleOffset advances.
    const uint32_t bytes = (uint32_t)frames * 2u;
    const uint32_t first = bytes < data_size_ - read_pos_ ? bytes : data_size_ - read_pos_;
    (void)GSPGPU_InvalidateDataCache(mic_buf_ + read_pos_, first);
    if (bytes > first) (void)GSPGPU_InvalidateDataCache(mic_buf_, bytes - first);
    __dmb();

    for (int i = 0; i < frames; ++i) {
        int16_t s;
        std::memcpy(&s, mic_buf_ + read_pos_, 2);
        out[i] = (trackr::fx::q15)s;
        read_pos_ = (read_pos_ + 2) % data_size_;
    }
    return frames;
}

} // namespace seance::platform
