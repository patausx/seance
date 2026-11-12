#pragma once
#include "sampler.h"
#include "../io/file_publication.h"
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

// Writes PCM16 RIFF/WAVE through an exclusively-created temporary sibling.
// Replacement is permitted for offline callers; errors never unlink the target.
// This is atomic rename publication, NOT an fsync/power-loss guarantee.
// If frames is zero,
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
                              void* context,
                              seance::fileio::NewFileReservation* reservation = nullptr);
// New-name UI exports must pass their exclusive final+temp reservation. The
// writer closes/publishes that descriptor, and discards matching token-owned
// files on error, including invalid samples. A mismatched token is not consumed.
// Null means an intentional replacement save, never placeholder adoption.

} // namespace trackr::synth
