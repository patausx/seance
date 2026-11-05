#pragma once
// Browser-local filesystem preflight. UI thread only; never call inspect/stat
// from capture, draw, or an AudioOut guard. FAT has no symlinks and exposes only
// coarse stat identity; this is a conservative check, not atomic unlink-by-inode.
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cstdint>

namespace seance::platform::ui3ds::browser_file_safety {
constexpr size_t kPath = 512;
inline bool sd_path(const char* path) { return path && !std::strncmp(path, "sdmc:", 5); }
inline bool fat_path(const char* path) {
#ifdef __3DS__
    (void)path; return true;
#else
    return sd_path(path);
#endif
}
inline char fold(char c, bool fat) { return fat && c >= 'A' && c <= 'Z' ? char(c + 'a' - 'A') : c; }
// Pure lexical normalization, with an already captured cwd. Provenance can
// contain ./, ../, duplicate slashes, or the equivalent sdmc:/ prefix.
inline bool normalize(const char* path, const char* cwd, char* out, bool fat) {
    if (!path || !path[0]) return false;
    if (sd_path(path)) path += 5;
    if (sd_path(cwd)) cwd += 5;
    if (path[0] != '/' && (!cwd || !cwd[0])) return false;
    char joined[kPath];
    const int n = path[0] == '/' ? std::snprintf(joined, sizeof joined, "%s", path)
        : std::snprintf(joined, sizeof joined, "%s/%s", cwd && cwd[0] ? cwd : "/", path);
    if (n < 0 || n >= int(sizeof joined)) return false;
    size_t ends[kPath / 2], depth = 0, used = 1;
    out[0] = '/'; out[1] = 0;
    const char* p = joined;
    while (*p) {
        while (*p == '/') ++p;
        const char* first = p; while (*p && *p != '/') ++p;
        const size_t length = size_t(p - first);
        if (!length || (length == 1 && first[0] == '.')) continue;
        if (length == 2 && first[0] == '.' && first[1] == '.') {
            if (!depth) continue; // provenance /../ resolves to the same filesystem root
            used = ends[--depth]; out[used] = 0; continue;
        }
        if (depth == kPath / 2 || used + length + 1 >= kPath) return false;
        ends[depth++] = used;
        if (used > 1) out[used++] = '/';
        for (size_t i = 0; i < length; ++i) {
            const unsigned char c = first[i];
            if (c < 32 || c == 127 || c == '\\' || c == ':') return false;
            out[used++] = fold(char(c), fat);
        }
        out[used] = 0;
    }
    return true;
}
// Destructive paths never accept traversal, even when normalization would keep
// them within the root. File listings retain their existing path/name limits.
inline bool clean(const char* path) {
    if (!path || !path[0]) return false;
    if (sd_path(path)) path += 5;
    while (*path) {
        while (*path == '/') ++path;
        const char* first = path;
        while (*path && *path != '/') {
            const unsigned char c = *path++;
            if (c < 32 || c == 127 || c == '\\' || c == ':') return false;
        }
        const size_t n = size_t(path - first);
        if ((n == 1 && first[0] == '.') || (n == 2 && first[0] == '.' && first[1] == '.')) return false;
    }
    return true;
}
inline bool within(const char* path, const char* root, const char* cwd) {
    char p[kPath], r[kPath]; const bool fat = fat_path(root);
    if (!clean(path) || !clean(root) || !normalize(path, cwd, p, fat) || !normalize(root, cwd, r, fat)) return false;
    const size_t n = std::strlen(r);
    return n == 1 ? p[1] != 0 : !std::strncmp(p, r, n) && p[n] == '/' && p[n + 1];
}
inline bool same_path(const char* a, const char* b, const char* cwd, bool fat) {
    char left[kPath], right[kPath];
    return normalize(a, cwd, left, fat) && normalize(b, cwd, right, fat) && !std::strcmp(left, right);
}
inline bool no_symlink_stat(const char* path, struct stat& st) {
#ifdef __3DS__
    return ::stat(path, &st) == 0; // SD FAT has no symlink objects/lstat support.
#else
    return ::lstat(path, &st) == 0 && !S_ISLNK(st.st_mode);
#endif
}
inline bool regular_tree(const char* path, struct stat& file) {
    if (!clean(path) || std::strlen(path) >= kPath) return false;
    char prefix[kPath]; std::snprintf(prefix, sizeof prefix, "%s", path);
    const size_t start = sd_path(path) ? 6 : (path[0] == '/' ? 1 : 0);
    for (size_t i = start; prefix[i]; ++i) if (prefix[i] == '/') {
        prefix[i] = 0; struct stat directory{};
        const bool ok = no_symlink_stat(prefix, directory) && S_ISDIR(directory.st_mode);
        prefix[i] = '/'; if (!ok) return false;
    }
    return no_symlink_stat(path, file) && S_ISREG(file.st_mode);
}
inline bool same_object(const struct stat& a, const struct stat& b) {
    return a.st_dev == b.st_dev && a.st_ino == b.st_ino;
}
inline bool same_identity(const struct stat& a, const struct stat& b) {
    if (!same_object(a, b) || a.st_size != b.st_size || a.st_mode != b.st_mode ||
        a.st_mtime != b.st_mtime || a.st_ctime != b.st_ctime) return false;
#if defined(__linux__) && !defined(__3DS__)
    if (a.st_mtim.tv_nsec != b.st_mtim.tv_nsec || a.st_ctim.tv_nsec != b.st_ctim.tv_nsec) return false;
#endif
    return true;
}
inline bool inspect(const char* path, const char* root, const char* cwd, struct stat& file) {
    if (!within(path, root, cwd) || !regular_tree(path, file)) return false;
    // Bounded RIFF/WAVE header check, not a full WAV decode/read.
    FILE* f = std::fopen(path, "rb"); if (!f) return false;
    unsigned char header[12]; const size_t n = std::fread(header, 1, sizeof header, f);
    std::fclose(f);
    if (n != sizeof header || std::memcmp(header, "RIFF", 4) || std::memcmp(header + 8, "WAVE", 4)) return false;
    struct stat after{};
    return regular_tree(path, after) && same_identity(file, after);
}
} // namespace seance::platform::ui3ds::browser_file_safety
