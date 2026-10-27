#pragma once
// App-only sample-browser preferences; never part of a project or sample bank.
// Controller-thread API: load uses one static decode State and is not reentrant.
// No application heap allocations. All I/O is synchronous and outside AudioOut.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cerrno>

namespace seance::platform::ui3ds::sample_browser_store {
constexpr int kCapacity = 64, kPath = 192;
struct Entry { char path[kPath]{}; bool directory = false; };
struct State { Entry entries[kCapacity]{}; int count = 0; bool auto_preview = true; };

namespace detail {
// v1: 16-byte header, 64 * (flag + 192-byte path), little-endian CRC32.
// Header: magic[8], version u16, count u16, auto-preview u8, reserved[3].
// Unused records and bytes after the first path NUL must be zero.
constexpr size_t kHeader = 16, kRecord = 1 + kPath;
constexpr size_t kBytes = kHeader + kCapacity * kRecord + 4;
constexpr uint8_t kMagic[8] = {'S','N','C','F','A','V','S',0};

inline size_t length(const char* path) {
    if (!path) return kPath;
    size_t n = 0;
    while (n < kPath && path[n]) ++n;
    return n;
}
inline bool path_valid(const char* path, bool directory) {
    const size_t n = length(path);
    if (n == 0 || n >= kPath) return false;
    size_t first = 0;
    if (path[0] == '/') first = 1;
    else if (n >= 6 && std::memcmp(path, "sdmc:/", 6) == 0) first = 6;
    else return false;
    if (first == n) return directory; // filesystem roots are directories
    for (size_t i = first, start = first; i <= n; ++i) {
        const unsigned char c = static_cast<unsigned char>(path[i]);
        if (i == n || c == '/') {
            const size_t segment = i - start;
            if (!segment || (segment == 1 && path[start] == '.') ||
                (segment == 2 && path[start] == '.' && path[start + 1] == '.'))
                return false;
            start = i + 1;
        } else if (c < 32 || c == 127 || c == '\\' || c == ':') return false;
    }
    return true;
}
inline bool same_path(const char* a, const char* b) {
    const size_t n = length(a), m = length(b);
    return n < kPath && n == m && std::memcmp(a, b, n) == 0;
}
inline bool valid(const State& state) {
    if (state.count < 0 || state.count > kCapacity) return false;
    for (int i = 0; i < state.count; ++i) {
        if (!path_valid(state.entries[i].path, state.entries[i].directory)) return false;
        for (int j = 0; j < i; ++j)
            if (same_path(state.entries[i].path, state.entries[j].path)) return false;
    }
    return true;
}
inline uint32_t checksum(uint32_t crc, const uint8_t* data, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return crc;
}
inline uint32_t get32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
           (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
inline void put32(uint8_t* p, uint32_t n) {
    for (int i = 0; i < 4; ++i) p[i] = uint8_t(n >> (8 * i));
}
inline void header(const State& state, uint8_t* p) {
    std::memset(p, 0, kHeader);
    std::memcpy(p, kMagic, sizeof kMagic);
    p[8] = 1;
    p[10] = uint8_t(state.count);
    p[12] = state.auto_preview ? 1 : 0;
}
inline void record(const State& state, int index, uint8_t* p) {
    std::memset(p, 0, kRecord);
    if (index >= state.count) return;
    const Entry& entry = state.entries[index];
    p[0] = entry.directory ? 1 : 0;
    std::memcpy(p + 1, entry.path, length(entry.path));
}
inline bool read_part(FILE* file, uint8_t* p, size_t n, uint32_t& crc) {
    if (std::fread(p, 1, n, file) != n) return false;
    crc = checksum(crc, p, n);
    return true;
}
inline bool eof(FILE* file) {
    return std::fgetc(file) == EOF && !std::ferror(file);
}
inline bool decode(FILE* file, State& scratch) {
    uint8_t bytes[kRecord];
    uint32_t crc = 0xffffffffu;
    if (!read_part(file, bytes, kHeader, crc) ||
        std::memcmp(bytes, kMagic, sizeof kMagic) != 0 || bytes[8] != 1 ||
        bytes[9] != 0 || bytes[10] > kCapacity || bytes[11] != 0 ||
        bytes[12] > 1 || bytes[13] || bytes[14] || bytes[15]) return false;
    scratch.count = bytes[10];
    scratch.auto_preview = bytes[12] != 0;
    for (int i = 0; i < kCapacity; ++i) {
        if (!read_part(file, bytes, kRecord, crc) || bytes[0] > 1) return false;
        const char* path = reinterpret_cast<const char*>(bytes + 1);
        const size_t n = length(path);
        if (n >= kPath) return false;
        for (size_t j = n; j < kPath; ++j) if (bytes[1 + j]) return false;
        if (i >= scratch.count) {
            if (bytes[0] || n) return false;
        } else {
            if (!path_valid(path, bytes[0] != 0)) return false;
            for (int j = 0; j < i; ++j)
                if (same_path(scratch.entries[j].path, path)) return false;
        }
        std::memcpy(scratch.entries[i].path, path, kPath);
        scratch.entries[i].directory = bytes[0] != 0;
    }
    return std::fread(bytes, 1, 4, file) == 4 &&
           get32(bytes) == (crc ^ 0xffffffffu) && eof(file);
}
// The same canonical encoder writes or verifies a stream using <400B scratch.
inline bool transfer(FILE* file, const State& state, bool verify) {
    uint8_t bytes[kRecord], actual[kRecord];
    uint32_t crc = 0xffffffffu;
    for (int i = -1; i <= kCapacity; ++i) {
        size_t size;
        if (i == -1) { header(state, bytes); size = kHeader; }
        else if (i == kCapacity) { put32(bytes, crc ^ 0xffffffffu); size = 4; }
        else { record(state, i, bytes); size = kRecord; }
        if (i < kCapacity) crc = checksum(crc, bytes, size);
        if (verify) {
            if (std::fread(actual, 1, size, file) != size ||
                std::memcmp(actual, bytes, size) != 0) return false;
        } else if (std::fwrite(bytes, 1, size, file) != size) return false;
    }
    return !verify || eof(file);
}
} // namespace detail

// Paths are exact/case-sensitive, not normalized or resolved against the SD.
// A path cannot be saved twice, even with a different file/directory flag.
inline int find(const State& state, const char* path, bool directory) {
    if (state.count < 0 || state.count > kCapacity || !detail::path_valid(path, directory)) return -1;
    for (int i = 0; i < state.count; ++i)
        if (state.entries[i].directory == directory && detail::same_path(state.entries[i].path, path))
            return i;
    return -1;
}
// false is failure; true is successful addition OR removal. Failure is inert.
inline bool toggle(State& state, const char* path, bool directory) {
    if (!detail::valid(state) || !detail::path_valid(path, directory)) return false;
    const int index = find(state, path, directory);
    if (index >= 0) {
        for (int i = index; i + 1 < state.count; ++i) state.entries[i] = state.entries[i + 1];
        state.entries[--state.count] = Entry{};
        return true;
    }
    if (state.count == kCapacity) return false;
    for (int i = 0; i < state.count; ++i)
        if (detail::same_path(state.entries[i].path, path)) return false;
    Entry entry{}; // also safe when path aliases an unused entry in state
    std::memcpy(entry.path, path, detail::length(path));
    entry.directory = directory;
    state.entries[state.count++] = entry;
    return true;
}
// Missing, damaged or incompatible files leave the destination exactly intact.
inline bool load(const char* sidecar, State& out) {
    if (!detail::path_valid(sidecar, false)) return false;
    FILE* file = std::fopen(sidecar, "rb");
    if (!file) return false;
    static State scratch;
    const bool ok = detail::decode(file, scratch);
    const bool closed = std::fclose(file) == 0;
    if (!ok || !closed) return false;
    out = scratch;
    return true;
}
// Exclusive sibling creation never truncates an existing temp/user file.
// After write/flush/close, verify every canonical byte before publication.
// Rename failure is fail-closed: no unlink/backup shuffle of the prior target.
// This is not a power-loss/fsync guarantee; rename-over-existing must be
// supported by the mounted filesystem. Otherwise the old target is retained.
inline bool save(const char* sidecar, const State& state) {
    if (!detail::path_valid(sidecar, false) || !detail::valid(state)) return false;
    char temporary[kPath + 16];
    FILE* file = nullptr;
    for (unsigned i = 0; i < 16; ++i) {
        const int n = std::snprintf(temporary, sizeof temporary, "%s.tmp-%02u", sidecar, i);
        if (n < 0 || size_t(n) >= sizeof temporary) return false;
        // C11 exclusive mode, supported by newlib and host libc; no POSIX API.
        file = std::fopen(temporary, "wbx");
        if (file) break;
        if (errno != EEXIST) return false;
    }
    if (!file) return false;
    bool ok = detail::transfer(file, state, false);
    if (std::fflush(file) != 0) ok = false;
    if (std::fclose(file) != 0) ok = false;
    if (ok) {
        file = std::fopen(temporary, "rb");
        if (!file) ok = false;
        else {
            ok = detail::transfer(file, state, true);
            if (std::fclose(file) != 0) ok = false;
        }
    }
    if (ok && std::rename(temporary, sidecar) == 0) return true;
    std::remove(temporary); // only the exclusively-created file owned by this call
    return false;
}
} // namespace seance::platform::ui3ds::sample_browser_store
