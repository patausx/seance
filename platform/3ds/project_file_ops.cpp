#include "project_file_ops.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#ifdef __3DS__
#include <3ds.h>
#endif

namespace seance::platform::ui3ds::project_files {
namespace {
bool path_stat(const char* path, struct stat& st) {
    if (!path || !*path) return false;
#ifdef __3DS__
    return ::stat(path, &st) == 0; // SD FAT has no symlinks/lstat support.
#else
    return ::lstat(path, &st) == 0 && !S_ISLNK(st.st_mode);
#endif
}
bool same_identity(const struct stat& a, const struct stat& b) {
    if (!S_ISREG(a.st_mode) || !S_ISREG(b.st_mode) || a.st_dev != b.st_dev ||
        a.st_ino != b.st_ino || a.st_mode != b.st_mode || a.st_size != b.st_size ||
        a.st_mtime != b.st_mtime || a.st_ctime != b.st_ctime) return false;
#if defined(__linux__) && !defined(__3DS__)
    if (a.st_mtim.tv_nsec != b.st_mtim.tv_nsec || a.st_ctim.tv_nsec != b.st_ctim.tv_nsec) return false;
#endif
    return true;
}
bool at_path(const char* path, const struct stat& expected) {
    struct stat actual{};
    return path_stat(path, actual) && same_identity(expected, actual);
}
int open_source(const char* path, struct stat& identity) {
    int flags = O_RDONLY | O_NONBLOCK;
#if defined(O_NOFOLLOW) && !defined(__3DS__)
    flags |= O_NOFOLLOW;
#endif
    const int fd = path && *path ? ::open(path, flags) : -1;
    if (fd < 0) return -1;
    if (::fstat(fd, &identity) != 0 || !S_ISREG(identity.st_mode) ||
        identity.st_size < 0 || uint64_t(identity.st_size) > kMaxSavedBytes || !at_path(path, identity)) {
        ::close(fd); return -1;
    }
    return fd;
}
// Fixed 1 KiB scratch, finite original physical length, no growth-following loop.
// A short read/close/path change refuses publication, including same-size rewrites.
bool read_revision(int fd, const char* path, const struct stat& identity,
                   uint64_t& digest, FILE* output = nullptr) {
    unsigned char bytes[1024];
    uint64_t left = uint64_t(identity.st_size), hash = 14695981039346656037ull;
    while (left) {
        const size_t count = size_t(std::min<uint64_t>(left, sizeof bytes));
        const ssize_t got = ::read(fd, bytes, count);
        if (got < 0 || size_t(got) != count) return false;
        if (output && std::fwrite(bytes, 1, count, output) != count) return false;
        for (size_t i = 0; i < count; ++i) hash = (hash ^ bytes[i]) * 1099511628211ull;
        left -= count;
    }
    unsigned char extra;
    struct stat after{};
    if (::read(fd, &extra, 1) != 0 || ::fstat(fd, &after) != 0 ||
        !same_identity(identity, after) || !at_path(path, identity)) return false;
    digest = hash;
    return true;
}
Result publish_new(fileio::OwnedFile& temporary, const char* destination) {
#ifdef __3DS__
    // Do NOT call libc rename here: installed libctru archive_rename can delete
    // an occupied destination and retry. The direct SDMC service refuses a late
    // collision; only our verified vacant name is a publication destination.
    if (std::strncmp(temporary.path(), "sdmc:/", 6) || std::strncmp(destination, "sdmc:/", 6)) return Result::Error;
    FS_Archive archive{};
    if (R_FAILED(FSUSER_OpenArchive(&archive, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, "")))) return Result::Error;
    const ::Result moved = FSUSER_RenameFile(archive, fsMakePath(PATH_ASCII, temporary.path() + 5),
                                            archive, fsMakePath(PATH_ASCII, destination + 5));
    const bool closed = R_SUCCEEDED(FSUSER_CloseArchive(archive));
    if (R_SUCCEEDED(moved)) temporary.retain(); // never erase a published file after a close error
    return R_SUCCEEDED(moved) && closed ? Result::Ok : Result::Error;
#else
    // POSIX rename replaces targets, so use atomic no-replace link publication.
    // Both names are in one app-owned directory/filesystem. Cleanup owns only
    // our O_EXCL staging name; no preexisting tmp/bak/final name is unlinked.
    if (::link(temporary.path(), destination) != 0) return errno == EEXIST ? Result::Occupied : Result::Error;
    temporary.discard();
    return Result::Ok;
#endif
}
} // namespace

bool inspect(const char* path, Snapshot& out) {
    out = {};
    Snapshot next;
    const int fd = open_source(path, next.identity);
    if (fd < 0) return false;
    const bool read_ok = read_revision(fd, path, next.identity, next.digest);
    const bool closed = ::close(fd) == 0; // once, never retry an uncertain close
    if (!read_ok || !closed || !at_path(path, next.identity)) return false;
    next.ready = true; out = next; return true;
}
bool matches(const char* path, const Snapshot& expected) {
    if (!expected.ready) return false;
    Snapshot actual;
    return inspect(path, actual) && same_identity(actual.identity, expected.identity) &&
        actual.digest == expected.digest;
}
Result remove_saved(const char* path, const Snapshot& expected) {
    if (!matches(path, expected) || !at_path(path, expected.identity)) return Result::Changed;
    // Exactly the chosen SNC, after both revision checks. No unlink fallback,
    // sidecar cleanup, recursive removal, samples or recordings are involved.
    return std::remove(path) == 0 ? Result::Ok : Result::Error;
}

VacantTarget::~VacantTarget() { settle(); }
fileio::ReserveResult VacantTarget::reserve(const char* path) {
    if (reserved_) return fileio::ReserveResult::Error;
    const auto result = final_.create(path);
    if (result != fileio::ReserveResult::Ok) return result;
    const bool inspected = ::fstat(::fileno(final_.stream()), &placeholder_) == 0 &&
        S_ISREG(placeholder_.st_mode) && placeholder_.st_size == 0;
    const bool closed = final_.close(false);
    reserved_ = true;
    if (!inspected || !closed || !unchanged()) { settle(); return fileio::ReserveResult::Error; }
    return fileio::ReserveResult::Ok;
}
bool VacantTarget::unchanged() const {
    return reserved_ && at_path(final_.path(), placeholder_);
}
bool VacantTarget::written() const {
    struct stat actual{};
    return reserved_ && path_stat(final_.path(), actual) && S_ISREG(actual.st_mode) &&
        actual.st_size > 0 && !same_identity(actual, placeholder_);
}
bool VacantTarget::release_for_publication() {
    if (!unchanged() || std::remove(final_.path()) != 0) return false;
    // This is checked removal of OUR empty reservation, not a failed-rename
    // unlink fallback. Native FSUSER_RenameFile / host link cannot overwrite a
    // file arriving in the resulting gap. Relinquish the old name immediately.
    final_.retain(); reserved_ = false; return true;
}
void VacantTarget::settle() {
    if (!reserved_) return;
    // A failed/uncertain publication may have replaced the placeholder already.
    // Without positive evidence of OUR unchanged empty file, retain the name.
    if (!unchanged()) final_.retain();
    final_.discard(); reserved_ = false;
}

Result copy_saved(const char* source, const char* destination) {
    Snapshot before;
    if (!inspect(source, before) || before.identity.st_size < 5) return Result::Error;
    VacantTarget target;
    const auto reservation = target.reserve(destination);
    if (reservation == fileio::ReserveResult::Occupied) return Result::Occupied;
    if (reservation != fileio::ReserveResult::Ok) return Result::Error;
    fileio::OwnedFile temporary;
    if (!fileio::create_temporary(temporary, destination)) return Result::Error;
    struct stat identity{};
    const int fd = open_source(source, identity);
    if (fd < 0) return Result::Changed;
    uint64_t digest = 0;
    const bool read_ok = same_identity(identity, before.identity) &&
        read_revision(fd, source, identity, digest, temporary.stream());
    const bool input_closed = ::close(fd) == 0;
    const bool output_closed = temporary.close(true);
    if (!at_path(source, before.identity) || !target.unchanged() ||
        (read_ok && digest != before.digest)) return Result::Changed;
    if (!read_ok || !input_closed || !output_closed) return Result::Error;
    // Source and reservation are revalidated AFTER all copy/flush/close work.
    // Publish to a vacant name with no-replace semantics on BOTH host and SDMC;
    // the generic libc replacement path is deliberately not used for cloning.
    if (!target.release_for_publication()) return Result::Changed;
    return publish_new(temporary, destination);
}

} // namespace seance::platform::ui3ds::project_files
