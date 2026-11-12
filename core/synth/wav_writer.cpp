#include "wav_writer.h"
#include <cstdio>
#include <cstring>
#include <limits>

namespace trackr::synth {

static bool put_bytes(FILE* f, const void* p, std::size_t n) {
    return std::fwrite(p, 1, n, f) == n;
}
static bool put_u16(FILE* f, uint16_t v) {
    uint8_t b[2] = { (uint8_t)(v & 0xff), (uint8_t)(v >> 8) };
    return put_bytes(f, b, sizeof b);
}
static bool put_u32(FILE* f, uint32_t v) {
    uint8_t b[4] = {
        (uint8_t)(v & 0xff), (uint8_t)((v >> 8) & 0xff),
        (uint8_t)((v >> 16) & 0xff), (uint8_t)(v >> 24)
    };
    return put_bytes(f, b, sizeof b);
}

WavSaveResult save_wav_stream(const char* path, uint8_t channels, uint32_t frames,
                              int sample_rate, int16_t* scratch,
                              uint32_t scratch_frames, WavReadFrames read,
                              void* context, seance::fileio::NewFileReservation* reservation) {
    const auto invalid = [&]() {
        if (reservation && path && reservation->matches(path)) reservation->discard();
        return WavSaveResult::InvalidSample;
    };
    if (!path || !*path || sample_rate <= 0 || (channels != 1 && channels != 2) ||
        !frames || !scratch || !scratch_frames || !read)
        return invalid();
    const uint64_t data64 = (uint64_t)frames * channels * sizeof(int16_t);
    if (data64 > std::numeric_limits<uint32_t>::max() - 36u)
        return invalid();
    const uint32_t data_bytes = (uint32_t)data64;

    seance::fileio::OwnedFile local;
    if (reservation ? !reservation->matches(path) : !seance::fileio::create_temporary(local, path))
        return WavSaveResult::OpenFailed;
    auto& temporary = reservation ? reservation->temporary() : local;
    FILE* f = temporary.stream();

    bool ok = put_bytes(f, "RIFF", 4) && put_u32(f, 36u + data_bytes) &&
              put_bytes(f, "WAVEfmt ", 8) && put_u32(f, 16) &&
              put_u16(f, 1) && put_u16(f, channels) &&
              put_u32(f, (uint32_t)sample_rate) &&
              put_u32(f, (uint32_t)sample_rate * channels * 2u) &&
              put_u16(f, channels * 2u) && put_u16(f, 16) &&
              put_bytes(f, "data", 4) && put_u32(f, data_bytes);
    // Keep callback/lock granularity unchanged; only batch the subsequent stdio
    // writes. Explicit LE packing works on either host endianness and leaves the
    // caller's scratch PCM untouched. No whole-take allocation.
    uint8_t encoded[4096];
    for (uint32_t first = 0; ok && first < frames;) {
        uint32_t count = frames - first;
        if (count > scratch_frames) count = scratch_frames;
        ok = read(context, first, count, scratch);
        const std::size_t samples = (std::size_t)count * channels;
        for (std::size_t offset = 0; ok && offset < samples;) {
            std::size_t batch = samples - offset;
            if (batch > sizeof encoded / 2) batch = sizeof encoded / 2;
            for (std::size_t i = 0; i < batch; ++i) {
                const uint16_t value = (uint16_t)scratch[offset + i];
                encoded[i * 2] = (uint8_t)value;
                encoded[i * 2 + 1] = (uint8_t)(value >> 8);
            }
            ok = put_bytes(f, encoded, batch * 2);
            offset += batch;
        }
        first += count;
    }
    const bool closed = temporary.close(ok);
    if (!ok || !closed) {
        if (reservation) reservation->discard();
        return WavSaveResult::WriteFailed;
    }
    if (!(reservation ? reservation->publish() : temporary.publish(path))) {
        if (reservation) reservation->discard();
        return WavSaveResult::RenameFailed;
    }
    return WavSaveResult::Ok;
}

struct SampleReadContext { const Sample* sample; };
static bool read_sample_frames(void* opaque, uint32_t first_frame, uint32_t frames,
                               int16_t* interleaved_out) {
    const auto& sample = *static_cast<SampleReadContext*>(opaque)->sample;
    const size_t first = (size_t)first_frame * sample.channels;
    const size_t count = (size_t)frames * sample.channels;
    if (first > sample.data.size() || count > sample.data.size() - first) return false;
    std::memcpy(interleaved_out, sample.data.data() + first, count * sizeof(int16_t));
    return true;
}

WavSaveResult save_sample_wav(const char* path, const Sample& sample,
                              int sample_rate, uint32_t frames) {
    if (!path || !*path || sample_rate <= 0 ||
        (sample.channels != 1 && sample.channels != 2))
        return WavSaveResult::InvalidSample;

    const uint32_t physical = sample.num_frames();
    if (frames == 0) {
        frames = physical;
        if (sample.loop_end > sample.loop_start && sample.loop_end <= physical)
            frames = sample.loop_end;
    }
    if (frames == 0 || frames > physical) return WavSaveResult::InvalidSample;

    const uint64_t data64 = (uint64_t)frames * sample.channels * sizeof(int16_t);
    if (data64 > std::numeric_limits<uint32_t>::max() - 36u)
        return WavSaveResult::InvalidSample;

    int16_t scratch[2048 * 2];
    SampleReadContext context{&sample};
    return save_wav_stream(path, sample.channels, frames, sample_rate,
                           scratch, 2048, read_sample_frames, &context);
}

} // namespace trackr::synth
