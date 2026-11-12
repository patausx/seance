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
    bool seen_fmt = false;
    uint32_t sample_rate = 0;
    long data_offset = -1;
    uint32_t data_size = 0;
    uint32_t cue_src_offsets[Sample::MAX_CHOPS] = {};
    uint32_t cue_count = 0;

    char id[5];
    uint32_t csize;
    while (true) {
        const long at = std::ftell(f);
        if (at == file_end) break;
        if (at < 0 || at > file_end || file_end - at < 8 || !read_chunk_header(f, id, csize)) {
            std::fclose(f); return WavLoadResult::InvalidChunks;
        }
        const long chunk_start = std::ftell(f);
        const uint64_t extent = uint64_t(csize) + (csize & 1u);
        if (chunk_start < 0 || chunk_start > file_end || extent > uint64_t(file_end - chunk_start)) {
            std::fclose(f); return WavLoadResult::InvalidChunks;
        }
        // Physical bounds prove the addition/narrowing fits long, also on ARM.
        const long chunk_end = chunk_start + long(extent);
        if (std::memcmp(id, "fmt ", 4) == 0) {
            if (seen_fmt || csize < 16) { std::fclose(f); return WavLoadResult::InvalidChunks; }
            seen_fmt = true;
            uint8_t fmt_buf[40] = {};
            const uint32_t to_read = csize > 40 ? 40 : csize;
            if (std::fread(fmt_buf, 1, to_read, f) != to_read) {
                std::fclose(f); return WavLoadResult::InvalidChunks;
            }
            format       = read_u16_le(fmt_buf + 0);
            channels     = read_u16_le(fmt_buf + 2);
            sample_rate  = read_u32_le(fmt_buf + 4);
            bits         = read_u16_le(fmt_buf + 14);
        } else if (std::memcmp(id, "data", 4) == 0) {
            if (data_offset >= 0) { std::fclose(f); return WavLoadResult::InvalidChunks; }
            data_offset = chunk_start;
            data_size = csize;
        } else if (std::memcmp(id, "cue ", 4) == 0) {
            uint8_t count_buf[4];
            if (csize < 4 || std::fread(count_buf, 1, 4, f) != 4) {
                std::fclose(f); return WavLoadResult::InvalidChunks;
            }
            const uint32_t declared = read_u32_le(count_buf);
            if (uint64_t(declared) * 24u + 4u > csize) {
                std::fclose(f); return WavLoadResult::InvalidChunks;
            }
            uint32_t ncue = Sample::MAX_CHOPS - cue_count;
            if (ncue > declared) ncue = declared;
            for (uint32_t i = 0; i < ncue; ++i) {
                uint8_t cue[24];
                if (std::fread(cue, 1, sizeof cue, f) != sizeof cue) {
                    std::fclose(f); return WavLoadResult::InvalidChunks;
                }
                cue_src_offsets[cue_count++] = read_u32_le(cue + 20);
            }
        } // Fully present unknown chunks (and excess cues) remain skippable.
        if (std::fseek(f, chunk_end, SEEK_SET) != 0) {
            std::fclose(f); return WavLoadResult::InvalidChunks;
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
    // SD reads cross the filesystem service on 3DS: use up to 64 KiB per read
    // instead of 4 KiB. Retain one overlapping frame at interpolation seams.
    // With a tight workspace budget shrink back to the old window size, so a
    // formerly accepted import never fails just because of this optimization.
    uint64_t needed_frames64 = ((uint64_t)(out_frames_expected - 1u) * sample_rate) / (uint32_t)target_sr + 2u;
    if (needed_frames64 > total_src_frames) needed_frames64 = total_src_frames;
    constexpr uint32_t RAW_BLOCK_BYTES = 64u * 1024u, MIN_BLOCK_BYTES = 4096;
    const uint32_t block_frames = RAW_BLOCK_BYTES / (uint32_t)frame_bytes;
    const uint32_t min_frames = MIN_BLOCK_BYTES / (uint32_t)frame_bytes;
    uint32_t window_capacity = (uint32_t)(needed_frames64 < block_frames ? needed_frames64 : block_frames);
    const uint32_t min_capacity = (uint32_t)(needed_frames64 < min_frames ? needed_frames64 : min_frames);
    const uint64_t decoded_elements64 = (uint64_t)out_frames_expected * channels;
    const uint64_t decoded_size64 = decoded_elements64 * sizeof(fx::q15);
    if (decoded_elements64 > std::numeric_limits<std::size_t>::max() ||
        decoded_size64 > std::numeric_limits<std::size_t>::max()) {
        std::fclose(f);
        return WavLoadResult::UnsupportedFormat;
    }
    if (limits.workspace_bytes) {
        if (decoded_size64 > limits.workspace_bytes ||
            (uint64_t)min_capacity * frame_bytes > limits.workspace_bytes - decoded_size64) {
            std::fclose(f);
            return WavLoadResult::MemoryBudget;
        }
        const uint64_t affordable = (limits.workspace_bytes - decoded_size64) / (uint32_t)frame_bytes;
        if (window_capacity > affordable) window_capacity = (uint32_t)affordable;
    }
    const std::size_t raw_size = (std::size_t)window_capacity * frame_bytes;

    std::vector<uint8_t> raw;
    Sample decoded;
    // Allocations are bounded by validated max_frames and source data. devkitARM
    // builds without C++ exceptions, so allocation failure follows the platform's
    // global OOM policy rather than a local catch.
    raw.resize(raw_size);
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

    if (sample_rate == (uint32_t)target_sr) {
        // No interpolation is needed at the engine rate. Preserve the historical
        // silent final frame unless the caller explicitly asks for the exact
        // native endpoint; a truncated import must NOT silence its last frame.
        const uint32_t available = total_src_frames - (limits.preserve_native_endpoint ? 0u : 1u);
        const uint32_t frames = out_frames_expected < available ? out_frames_expected : available;
        for (uint32_t first = 0; first < frames;) {
            if (!ensure_frames(first, 1)) { std::fclose(f); return WavLoadResult::NoDataChunk; }
            const uint32_t offset = first - window_first;
            const uint32_t left = window_count - offset;
            const uint32_t count = frames - first < left ? frames - first : left;
            const std::size_t elements = (std::size_t)count * channels;
            fx::q15* output = decoded.data.data() + (std::size_t)first * channels;
            const uint8_t* input = raw.data() + (std::size_t)offset * frame_bytes;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
            if (format == 1 && bits == 16) {
                std::memcpy(output, input, elements * sizeof(fx::q15));
            } else
#endif
            {
                for (std::size_t j = 0; j < elements; ++j)
                    output[j] = pcm_to_q15(input, j, format, bits);
            }
            first += count;
        }
    } else {
        // Keep the accumulated-double position and rounding exactly as before
        // for non-native rates; no lower-quality fast resampler.
        double src_pos = 0.0;
        for (uint32_t i = 0; i < out_frames_expected; ++i) {
            uint32_t src_idx = (uint32_t)src_pos;
            double frac = src_pos - (double)src_idx;
            // The historical non-native tail remains zero in both policies.
            if (src_idx + 1 >= total_src_frames) break;
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
    }
    if (std::ferror(f)) { std::fclose(f); return WavLoadResult::NoDataChunk; }
    if (std::fclose(f) != 0) return WavLoadResult::NoDataChunk;

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
    for (uint32_t i = 0; i < cue_count; ++i) {
        const uint32_t src_off = cue_src_offsets[i];
        if (src_off >= total_src_frames) continue; // validate BEFORE scaling/narrowing
        uint64_t out_off = (uint64_t(src_off) * uint32_t(target_sr)) / sample_rate;
        // Preserve the former double-ratio division's floor at exact-rate
        // boundaries (it can lie one ulp either side of the rational integer).
        // No float is narrowed; all accepted offsets fit the <=INT_MAX output.
        const double historical = double(src_off) / ratio;
        if (historical < double(out_off) && out_off) --out_off;
        else if (historical >= double(out_off + 1u)) ++out_off;
        if (out_off < out_frames_expected) decoded.chops[chop_n++] = uint32_t(out_off);
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
