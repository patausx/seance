// WAV loader: parses RIFF/WAVE -> q15 PCM -> resamples to 32kHz -> writes to Sample
//
// supported formats:
//   - 8-bit unsigned PCM (mono/stereo)
//   - 16-bit signed PCM (mono/stereo) - the main use case
//   - 24/32-bit signed PCM (mono/stereo)
//   - 32-bit float PCM (mono/stereo)
// ADPCM/MP3/OGG are not supported - convert via Audacity if needed
//
// resample: linear interpolation to target_sr (usually 32000)
// hard limit: max_frames (truncate if longer)
#pragma once
#include "sampler.h"

namespace trackr::synth {

enum class WavLoadResult : int8_t {
    Ok                 = 0,
    FileNotFound       = -1,
    NotRiff            = -2,
    NotWave            = -3,
    NoFmtChunk         = -4,
    NoDataChunk        = -5,
    UnsupportedFormat  = -6,    // ADPCM / unknown formatTag
    UnsupportedBits    = -7,    // 4-bit, 64-bit etc
    UnsupportedChannels = -8,   // >2 channels
    MemoryBudget       = -9,    // validated decode workspace limit exceeded
    Truncated          = 1,     // ok but truncated to max_frames
};

constexpr std::size_t WAV_DECODE_WORKSPACE_BYTES = 4u * 1024u * 1024u;

struct WavDecodeLimits {
    // PCM workspace: frame-aligned source window (<=64 KiB) + staged q15 output.
    // Tight budgets shrink the window down to the former <=4 KiB requirement;
    // increasing preferred IO size does not reject previously fitting imports.
    // Does not include the caller's retained destination or stdio/metadata.
    // Zero disables this cap; max_frames still bounds the output allocation.
    std::size_t workspace_bytes = 0;
    // New exact editors/export explicitly retain the final native-rate frame.
    // Default false preserves the historical interpolation endpoint/silence.
    bool preserve_native_endpoint = false;
};


// loads a WAV into a Sample (data + channels + root_note + chops/loops cleared)
// returns Ok or Truncated on success, negative on error
WavLoadResult load_wav_to_sample(const char* path, Sample& dst,
                                  int target_sr, int max_frames,
                                  WavDecodeLimits limits = {});

} // namespace trackr::synth
