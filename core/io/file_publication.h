#pragma once
// Controller/offline filesystem transactions only; never use under an audio guard.
// O_EXCL is the ownership boundary, not a stat/read preflight. No fsync/power-cut
// promise, and no unlink-then-rename fallback on filesystems refusing replacement.
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __3DS__
#include <3ds.h>
#endif

namespace seance::fileio {
enum class ReserveResult { Ok, Occupied, Error };

inline bool absent(const char* path) {
    struct stat st{};
    return ::stat(path, &st) != 0 && errno == ENOENT;
}

inline bool regular_or_absent_path(const char* path, bool& exists) {
    struct stat st{};
    if (path && ::stat(path, &st) == 0) { exists = true; return S_ISREG(st.st_mode); }
    exists = false;
    return path && errno == ENOENT;
}

// [STORAGE] SD card rename only onto a vacant name
// The 3DS SD card (FAT) refuses to rename onto an existing name, and libctru's
// rename() answers one of its "already exists" codes by DELETING the target and
// retrying. Neither is a safe replacement. Publication therefore renames only
// onto vacant names; on the console it calls FSUSER_RenameFile directly so the
// destructive fallback is never entered. Host: plain rename after the check.
inline bool rename_vacant(const char* from, const char* to) {
    if (!from || !to || !*from || !*to || !absent(to)) return false;
#ifdef __3DS__
    if (std::strncmp(from, "sdmc:/", 6) || std::strncmp(to, "sdmc:/", 6)) return false;
    FS_Archive archive{};
    if (R_FAILED(FSUSER_OpenArchive(&archive, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, "")))) return false;
    const Result moved = FSUSER_RenameFile(archive, fsMakePath(PATH_ASCII, from + 5),
                                           archive, fsMakePath(PATH_ASCII, to + 5));
    (void)FSUSER_CloseArchive(archive); // cannot revoke a completed rename
    return R_SUCCEEDED(moved);
#else
    return std::rename(from, to) == 0;
#endif
}

// Replace `destination` by `from` without ever renaming onto an existing name:
// the current file moves to `previous` (an older file there is removed first),
// then `from` takes the vacant name. If that last step fails the current file is
// moved back. Afterwards `previous` holds the version that was replaced.
inline bool replace_via_previous(const char* from, const char* destination, const char* previous) {
    if (!previous || !*previous) return false;
    bool exists = false;
    if (!regular_or_absent_path(destination, exists)) return false;
    if (!exists) return rename_vacant(from, destination);
    bool old = false;
    if (!regular_or_absent_path(previous, old)) return false;
    if (old && std::remove(previous) != 0) return false;
    if (!rename_vacant(destination, previous)) {
        // An applied-but-reported-failed move still leaves a usable state.
        if (!(absent(destination) && !absent(previous))) return false;
    }
    if (rename_vacant(from, destination)) return true;
    if (!absent(destination)) return absent(from); // reported failure, but it did move
    (void)rename_vacant(previous, destination); // put the replaced version back
    return false;
}

// Owns only a newly-created name. A failed open gives NO cleanup authority.
// Callers serialize access to the app-owned directory: this does not defend
// against a hostile process unlinking/replacing names during publication.
class OwnedFile {
public:
    OwnedFile() = default;
    ~OwnedFile() { discard(); }
    OwnedFile(const OwnedFile&) = delete;
    OwnedFile& operator=(const OwnedFile&) = delete;
    ReserveResult create(const char* path) {
        if (owned_ || !path || !*path || std::strlen(path) >= sizeof path_) return ReserveResult::Error;
        const int fd = ::open(path, O_WRONLY | O_CREAT | O_EXCL, 0666);
        if (fd < 0) return errno == EEXIST ? ReserveResult::Occupied : ReserveResult::Error;
        std::strcpy(path_, path); owned_ = true;
        stream_ = ::fdopen(fd, "wb"); // retain the exclusive descriptor; NEVER reopen with wb
        if (!stream_) { ::close(fd); discard(); return ReserveResult::Error; }
        return ReserveResult::Ok;
    }
    FILE* stream() const { return stream_; }
    bool owned() const { return owned_; }
    const char* path() const { return path_; }
    bool close(bool flush) {
        if (!stream_) return false;
        bool ok = !std::ferror(stream_);
        if (flush && std::fflush(stream_) != 0) ok = false;
        FILE* f = stream_; stream_ = nullptr;
        if (std::fclose(f) != 0) ok = false;
        return ok;
    }
    bool publish(const char* destination) {
        if (!owned_ || stream_ || std::rename(path_, destination) != 0) return false;
        owned_ = false;
        return true;
    }
    // SD-safe publication onto a name that must not exist yet.
    bool publish_vacant(const char* destination) {
        if (!owned_ || stream_ || !rename_vacant(path_, destination)) return false;
        owned_ = false;
        return true;
    }
    // SD-safe replacement: the current file is kept under `previous`.
    bool publish_replacing(const char* destination, const char* previous) {
        if (!owned_ || stream_ || !replace_via_previous(path_, destination, previous)) return false;
        owned_ = false;
        return true;
    }
    void retain() { owned_ = false; }
    void discard() {
        if (stream_) { std::fclose(stream_); stream_ = nullptr; }
        if (owned_) { std::remove(path_); owned_ = false; }
    }
private:
    char path_[256] = {};
    FILE* stream_ = nullptr;
    bool owned_ = false;
};

// Replacement saves leave earlier .tmp recovery siblings intact. File collisions
// advance through a bounded namespace; directories and all non-EEXIST failures
// fail closed. Exhaustion is an ordinary save refusal, not recovery deletion.
inline bool create_temporary(OwnedFile& file, const char* destination) {
    if (!destination || !*destination) return false;
    char path[256];
    for (unsigned i = 0; i < 100; ++i) {
        const int n = i ? std::snprintf(path, sizeof path, "%s.tmp-%02u", destination, i)
                        : std::snprintf(path, sizeof path, "%s.tmp", destination);
        if (n < 0 || size_t(n) >= sizeof path) return false;
        const auto result = file.create(path);
        if (result == ReserveResult::Ok) return true;
        if (result != ReserveResult::Occupied) return false;
        struct stat st{};
        if (::stat(path, &st) != 0 || !S_ISREG(st.st_mode)) return false;
    }
    return false;
}

// New WAVs reserve BOTH names like PLAY's historical placeholders, but keep the
// temporary descriptor until writing. The token is noncopyable and explicit;
// an empty pre-existing file is never inferred to be someone else's reservation.
class NewFileReservation {
public:
    ~NewFileReservation() { discard(); }
    ReserveResult reserve(const char* destination) {
        const auto result = final_.create(destination);
        if (result != ReserveResult::Ok) return result;
        if (!final_.close(false)) { final_.discard(); return ReserveResult::Error; }
        char path[256];
        const int n = std::snprintf(path, sizeof path, "%s.tmp", destination);
        if (n < 0 || size_t(n) >= sizeof path) { final_.discard(); return ReserveResult::Error; }
        const auto temp = temporary_.create(path);
        if (temp != ReserveResult::Ok) { final_.discard(); return temp; }
        return ReserveResult::Ok;
    }
    bool matches(const char* destination) const {
        return temporary_.stream() && !std::strcmp(final_.path(), destination);
    }
    OwnedFile& temporary() { return temporary_; }
    bool publish() {
        // The placeholder is OUR empty file: remove it, then publish onto the
        // vacant name (renaming onto it is refused on the SD card).
        struct stat st{};
        if (!temporary_.owned() || temporary_.stream() || !final_.owned() ||
            ::stat(final_.path(), &st) != 0 || !S_ISREG(st.st_mode) || st.st_size != 0 ||
            std::remove(final_.path()) != 0) return false;
        final_.retain(); // the name is no longer ours to clean up
        return temporary_.publish_vacant(final_.path());
    }
    void discard() {
        temporary_.discard();
        if (final_.owned()) {
            struct stat st{};
            // A rename can have an uncertain result on a remote/device FS. Never
            // erase a nonempty published WAV just because the reply was failure;
            // final cleanup authority is limited to OUR empty placeholder.
            if (::stat(final_.path(), &st) != 0 || !S_ISREG(st.st_mode) || st.st_size != 0)
                final_.retain();
        }
        final_.discard();
    }
private:
    OwnedFile final_, temporary_;
};

inline bool regular_or_absent(const char* path, bool& exists) {
    return regular_or_absent_path(path, exists);
}

inline bool read_bounded(const char* path, unsigned char* bytes, size_t capacity, size_t& got) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    got = std::fread(bytes, 1, capacity, f);
    // A short read with neither EOF nor error is also incomplete (fault wrappers
    // and non-file streams must not silently turn it into a valid record).
    const bool ok = !std::ferror(f) && (got == capacity || std::feof(f));
    const bool closed = std::fclose(f) == 0;
    return ok && closed;
}

// Bounded byte-for-byte copy to an already exclusively-owned staging stream.
// Read/write/flush/close failures abort BEFORE any final/recovery publication.
inline bool copy_file(const char* source, OwnedFile& output) {
    FILE* input = std::fopen(source, "rb");
    if (!input) return false;
    unsigned char bytes[4096];
    bool ok = true;
    while (ok) {
        const size_t n = std::fread(bytes, 1, sizeof bytes, input);
        if (n && std::fwrite(bytes, 1, n, output.stream()) != n) ok = false;
        if (n < sizeof bytes) { ok = ok && !std::ferror(input) && std::feof(input); break; }
    }
    if (std::fclose(input) != 0) ok = false;
    return output.close(true) && ok;
}
} // namespace seance::fileio
