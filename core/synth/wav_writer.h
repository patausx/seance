#pragma once
#include "sampler.h"
#include <cstddef>
#include <cstdint>

namespace trackr::synth {

enum class WavSaveResult : int8_t {
    Ok = 0,
    InvalidSample = -1,
    OpenFailed = -2,
    WriteFailed = -3,
    RenameFailed = -4,
};

// Writes PCM16 RIFF/WAVE atomically through path.tmp. If frames is zero,
// a valid loop_end is treated as the logical take length, otherwise all
// physical frames are written.
WavSaveResult save_sample_wav(const char* path, const Sample& sample,
                              int sample_rate, uint32_t frames = 0);

// Bounded/chunked source for realtime-owned PCM. read() must return exactly the
// requested frames; callers may briefly take their audio lock inside it.
using WavReadFrames = bool (*)(void* context, uint32_t first_frame,
                               uint32_t frames, int16_t* interleaved_out);
WavSaveResult save_wav_stream(const char* path, uint8_t channels, uint32_t frames,
                              int sample_rate, int16_t* scratch,
                              uint32_t scratch_frames, WavReadFrames read,
                              void* context);

} // namespace trackr::synth
