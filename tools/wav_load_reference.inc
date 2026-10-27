#include "wav_loader.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>
#include <limits>

namespace trackr::synth {

// === RIFF helpers ===

static uint32_t read_u32_le(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t read_u16_le(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

// reads one chunk header (id 4b + size 4b)
static bool read_chunk_header(FILE* f, char id[5], uint32_t& size) {
    uint8_t buf[8];
    if (std::fread(buf, 1, 8, f) != 8) return false;
    std::memcpy(id, buf, 4);
    id[4] = 0;
    size = read_u32_le(buf + 4);
    return true;
}

// === sample → q15 conversion ===

// reads one PCM sample from buf by format/bits, returns q15
// pos - the sample index (channels already accounted for)
static int16_t pcm_to_q15(const uint8_t* buf, std::size_t pos,
                          uint16_t format, uint16_t bits) {
    if (format == 1) {     // PCM int
        if (bits == 8) {
            // unsigned 8-bit, center = 128
            int s = (int)buf[pos] - 128;
            return (int16_t)(s * 256);    // same mapping, no signed-negative shift
        } else if (bits == 16) {
            return (int16_t)read_u16_le(buf + pos * 2);
        } else if (bits == 24) {
            const uint8_t* p = buf + pos * 3;
            int32_t v = (int32_t)p[0] | ((int32_t)p[1] << 8) | ((int32_t)p[2] << 16);
            // sign-extend 24→32
            if (v & 0x800000) v |= 0xFF000000;
            return (int16_t)(v >> 8);     // 24→16
        } else if (bits == 32) {
            int32_t v = (int32_t)read_u32_le(buf + pos * 4);
            return (int16_t)(v >> 16);
        }
    } else if (format == 3 && bits == 32) {     // IEEE float
        uint32_t u = read_u32_le(buf + pos * 4);
        float f;
        std::memcpy(&f, &u, sizeof(f));
        // Reject NaN and clamp infinities/out-of-range values without relying on
        // undefined float-to-integer conversion behavior.
        if (!(f == f)) return 0;
        if (f >  1.0f) f =  1.0f;
        if (f < -1.0f) f = -1.0f;
        return (int16_t)(f * 32767.0f);
    }
    return 0;
}

WavLoadResult load_wav_to_sample(const char* path, Sample& dst,
                                  int target_sr, int max_frames,
                                  WavDecodeLimits limits) {
    if (!path || !*path || target_sr <= 0 || max_frames <= 0)
        return WavLoadResult::UnsupportedFormat;

    FILE* f = std::fopen(path, "rb");
    if (!f) return WavLoadResult::FileNotFound;

    // === RIFF header ===
    uint8_t hdr[12];
    if (std::fread(hdr, 1, 12, f) != 12) { std::fclose(f); return WavLoadResult::NotRiff; }
    if (std::memcmp(hdr, "RIFF", 4) != 0) { std::fclose(f); return WavLoadResult::NotRiff; }
    if (std::memcmp(hdr + 8, "WAVE", 4) != 0) { std::fclose(f); return WavLoadResult::NotWave; }

    if (std::fseek(f, 0, SEEK_END) != 0) { std::fclose(f); return WavLoadResult::NotRiff; }
    const long file_end = std::ftell(f);
    if (file_end < 12 || std::fseek(f, 12, SEEK_SET) != 0) {
        std::fclose(f);
        return WavLoadResult::NotRiff;
    }

    // === scan chunks: look for 'fmt ' and 'data' ===
    uint16_t format = 0, channels = 0, bits = 0;
    uint32_t sample_rate = 0;
    long data_offset = -1;
    uint32_t data_size = 0;
    std::vector<uint32_t> cue_src_offsets;

    char id[5];
    uint32_t csize;
    while (read_chunk_header(f, id, csize)) {
        const long chunk_start = std::ftell(f);
        if (chunk_start < 0 || (uint64_t)(unsigned long)chunk_start + csize > (uint64_t)(unsigned long)file_end)
            break;
        if (std::memcmp(id, "fmt ", 4) == 0) {
            // fmt chunk: min. 16 bytes
            if (csize < 16) { std::fclose(f); return WavLoadResult::NoFmtChunk; }
            uint8_t fmt_buf[40] = {};
            uint32_t to_read = csize > 40 ? 40 : csize;
            if (std::fread(fmt_buf, 1, to_read, f) != to_read) break;
            format       = read_u16_le(fmt_buf + 0);
            channels     = read_u16_le(fmt_buf + 2);
            sample_rate  = read_u32_le(fmt_buf + 4);
            // byte_rate at +8, block_align at +12
            bits         = read_u16_le(fmt_buf + 14);
            // skip the rest of the chunk (in case there was WAVEFORMATEX extra)
            if (csize > to_read) std::fseek(f, csize - to_read, SEEK_CUR);
            // align to an even byte (RIFF padding)
            if (csize & 1) std::fseek(f, 1, SEEK_CUR);
        } else if (std::memcmp(id, "data", 4) == 0) {
            data_offset = std::ftell(f);
            data_size   = csize;
            // don't read the body now - after fully decoding fmt
            std::fseek(f, csize, SEEK_CUR);
            if (csize & 1) std::fseek(f, 1, SEEK_CUR);
        } else if (std::memcmp(id, "cue ", 4) == 0) {
            // Only the first MAX_CHOPS cue records are useful; never allocate from
            // an untrusted chunk size.
            uint8_t count_buf[4];
            if (csize < 4 || std::fread(count_buf, 1, 4, f) != 4) break;
            uint32_t ncue = read_u32_le(count_buf);
            if (ncue > Sample::MAX_CHOPS) ncue = Sample::MAX_CHOPS;
            for (uint32_t i = 0; i < ncue; ++i) {
                uint8_t cue[24];
                if (4u + (i + 1u) * 24u > csize || std::fread(cue, 1, sizeof cue, f) != sizeof cue) break;
                cue_src_offsets.push_back(read_u32_le(cue + 20));
            }
            if (std::fseek(f, chunk_start + (long)csize + (csize & 1u), SEEK_SET) != 0) break;
        } else {
            // skip unknown chunk
            std::fseek(f, csize, SEEK_CUR);
            if (csize & 1) std::fseek(f, 1, SEEK_CUR);
        }
    }

    if (format == 0 || sample_rate == 0) { std::fclose(f); return WavLoadResult::NoFmtChunk; }
    if (data_offset < 0)                  { std::fclose(f); return WavLoadResult::NoDataChunk; }
    if (channels < 1 || channels > 2)     { std::fclose(f); return WavLoadResult::UnsupportedChannels; }

    // format/bits check
    bool fmt_ok = false;
    if (format == 1) {
        if (bits == 8 || bits == 16 || bits == 24 || bits == 32) fmt_ok = true;
    } else if (format == 3 && bits == 32) {
        fmt_ok = true;
    }
    if (!fmt_ok) {
        std::fclose(f);
        return (format == 1) ? WavLoadResult::UnsupportedBits : WavLoadResult::UnsupportedFormat;
    }

    // === read the data chunk ===
    int bytes_per_sample = bits / 8;
    int frame_bytes = bytes_per_sample * channels;
    if (frame_bytes <= 0) { std::fclose(f); return WavLoadResult::UnsupportedFormat; }

    uint32_t total_src_frames = data_size / (uint32_t)frame_bytes;
    if (total_src_frames == 0) { std::fclose(f); return WavLoadResult::NoDataChunk; }

    // === resample ratio ===
    // src_pos advances by ratio per output frame
    // ratio = src_sr / target_sr (in floats, q24.8 for speed)
    double ratio = (double)sample_rate / (double)target_sr;
    if (!(ratio > 0.0)) { std::fclose(f); return WavLoadResult::UnsupportedFormat; }
    // Integer arithmetic avoids unchecked floating-point-to-integer overflow.
    uint64_t expected64 = ((uint64_t)total_src_frames * (uint32_t)target_sr) / sample_rate;
    if (expected64 == 0) expected64 = 1;
    bool truncated = expected64 > (uint64_t)(uint32_t)max_frames;
    uint32_t out_frames_expected = (uint32_t)(truncated ? (uint64_t)(uint32_t)max_frames : expected64);

    // === bounded source window + separately staged output ===
    // Preserve the existing accumulated-double interpolation/endpoint policy.
    // Keep only a frame-aligned <=4 KiB source block; one overlapping frame is
    // retained when an interpolation pair straddles the next block.
    uint64_t needed_frames64 = ((uint64_t)(out_frames_expected - 1u) * sample_rate) / (uint32_t)target_sr + 2u;
    if (needed_frames64 > total_src_frames) needed_frames64 = total_src_frames;
    constexpr uint32_t RAW_BLOCK_BYTES = 4096;
    const uint32_t block_frames = RAW_BLOCK_BYTES / (uint32_t)frame_bytes;
    const uint32_t window_capacity = (uint32_t)(needed_frames64 < block_frames ? needed_frames64 : block_frames);
    const uint64_t raw_size64 = (uint64_t)window_capacity * (uint32_t)frame_bytes;
    const uint64_t decoded_elements64 = (uint64_t)out_frames_expected * channels;
    const uint64_t decoded_size64 = decoded_elements64 * sizeof(fx::q15);
    if (raw_size64 > std::numeric_limits<std::size_t>::max() ||
        decoded_elements64 > std::numeric_limits<std::size_t>::max() ||
        decoded_size64 > std::numeric_limits<std::size_t>::max() ||
        raw_size64 > UINT64_MAX - decoded_size64) {
        std::fclose(f);
        return WavLoadResult::UnsupportedFormat;
    }
    const uint64_t workspace64 = raw_size64 + decoded_size64;
    if (limits.workspace_bytes && workspace64 > limits.workspace_bytes) {
        std::fclose(f);
        return WavLoadResult::MemoryBudget;
    }

    std::vector<uint8_t> raw;
    Sample decoded;
    // Allocations are bounded by validated max_frames and source data. devkitARM
    // builds without C++ exceptions, so allocation failure follows the platform's
    // global OOM policy rather than a local catch.
    raw.resize((std::size_t)raw_size64);
    decoded.data.assign((std::size_t)decoded_elements64, 0);
    if (std::fseek(f, data_offset, SEEK_SET) != 0 ||
        std::fread(raw.data(), 1, raw.size(), f) != raw.size()) {
        std::fclose(f);
        return WavLoadResult::NoDataChunk;
    }
    uint32_t window_first = 0, window_count = window_capacity;
    auto ensure_frames = [&](uint32_t index, uint32_t count) {
        if ((uint64_t)index + count > needed_frames64) return false;
        const uint32_t end = window_first + window_count;
        if (index >= window_first && (uint64_t)index + count <= end) return true;
        uint32_t keep = 0;
        if (window_count && index == end - 1u) {
            std::memmove(raw.data(), raw.data() + (std::size_t)(window_count - 1u) * frame_bytes, frame_bytes);
            keep = 1;
        } else if (index != end && std::fseek(f,
                       data_offset + (long)((uint64_t)index * frame_bytes), SEEK_SET) != 0) {
            return false;
        }
        window_first = index;
        const uint32_t remaining = (uint32_t)needed_frames64 - index;
        window_count = remaining < window_capacity ? remaining : window_capacity;
        const std::size_t bytes = (std::size_t)(window_count - keep) * frame_bytes;
        return std::fread(raw.data() + keep * frame_bytes, 1, bytes, f) == bytes;
    };

    // === decode + resample into q15 ===

    double src_pos = 0.0;
    for (uint32_t i = 0; i < out_frames_expected; ++i) {
        uint32_t src_idx = (uint32_t)src_pos;
        double frac = src_pos - (double)src_idx;
        if (src_idx + 1 >= total_src_frames) {
            if (limits.preserve_native_endpoint && sample_rate == (uint32_t)target_sr && src_idx < total_src_frames) {
                if (!ensure_frames(src_idx, 1)) { std::fclose(f); return WavLoadResult::NoDataChunk; }
                for (int ch = 0; ch < channels; ++ch)
                    decoded.data[(std::size_t)i * channels + ch] = pcm_to_q15(raw.data(),
                        (std::size_t)(src_idx - window_first) * channels + ch, format, bits);
            }
            // Historical callers still leave the final interpolation frame zero.
            break;
        }
        if (!ensure_frames(src_idx, 2)) { std::fclose(f); return WavLoadResult::NoDataChunk; }
        for (int ch = 0; ch < channels; ++ch) {
            std::size_t pos_a = (std::size_t)(src_idx - window_first) * channels + ch;
            std::size_t pos_b = pos_a + channels;
            int16_t a = pcm_to_q15(raw.data(), pos_a, format, bits);
            int16_t b = pcm_to_q15(raw.data(), pos_b, format, bits);
            int32_t lerp = a + (int32_t)((b - a) * frac);
            if (lerp >  32767) lerp =  32767;
            if (lerp < -32768) lerp = -32768;
            decoded.data[(std::size_t)i * channels + ch] = (fx::q15)lerp;
        }
        src_pos += ratio;
    }
    std::fclose(f);

    // === fill in the Sample's metadata ===
    decoded.channels   = (uint8_t)channels;
    decoded.root_note  = 60;
    decoded.loop_start = 0;
    decoded.loop_end   = 0;
    decoded.reversed   = false;
    decoded.slice_reverse_mask = 0;
    decoded.reset_cut_speeds();
    decoded.exact_native_endpoint = limits.preserve_native_endpoint;
    for (int i = 0; i < Sample::MAX_CHOPS; ++i) decoded.chops[i] = 0xFFFFFFFFu;
    int chop_n = 0;
    for (uint32_t src_off : cue_src_offsets) {
        if (chop_n >= Sample::MAX_CHOPS) break;
        uint32_t out_off = (uint32_t)((double)src_off / ratio);
        if (out_off < out_frames_expected) decoded.chops[chop_n++] = out_off;
    }
    // sort imported cue markers and drop duplicates/out-of-range sentinels
    for (int i = 1; i < chop_n; ++i) {
        uint32_t v = decoded.chops[i];
        int j = i - 1;
        while (j >= 0 && decoded.chops[j] > v) { decoded.chops[j + 1] = decoded.chops[j]; --j; }
        decoded.chops[j + 1] = v;
    }
    int out = 0;
    for (int i = 0; i < chop_n; ++i) {
        uint32_t v = decoded.chops[i];
        if (v >= out_frames_expected) continue;
        if (out > 0 && decoded.chops[out - 1] == v) continue;
        decoded.chops[out++] = v;
    }
    while (out < Sample::MAX_CHOPS) decoded.chops[out++] = 0xFFFFFFFFu;

    dst = std::move(decoded);
    return truncated ? WavLoadResult::Truncated : WavLoadResult::Ok;
}

} // namespace trackr::synth
