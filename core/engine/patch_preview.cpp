#include "patch_preview.h"
#include "unit.h"
#include <cstddef>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace seance::patchio {
namespace {

// Fixed on-wire geometry without pulling Engine/DSP headers into this reader.
constexpr unsigned kChains = sizeof(PatchPreview{}.kinds) / sizeof(PatchPreview{}.kinds[0]);
constexpr unsigned kSlots = sizeof(PatchPreview{}.kinds[0]) / sizeof(PatchPreview{}.kinds[0][0]);
constexpr unsigned kLegacyParams = 6;
constexpr unsigned kOldCoreParams = 8;
constexpr unsigned kCoreParams = 12;
constexpr unsigned kMaxHeaders = 64;
constexpr uint32_t kMaxChunkBytes = 64u * 1024u; // patch_io's supported chunk bound
constexpr unsigned core_bytes(unsigned chain_header, unsigned params) {
    return kChains * (chain_header + kSlots * (3u + params * 5u));
}
constexpr unsigned kMaxCoreBytes = core_bytes(5, kCoreParams);
static_assert(kChains == 4 && kSlots == 6 && PARAM_BANK_SIZE == kCoreParams,
              "review the fixed preview wire layout when core geometry changes");
static_assert(uint8_t(UnitKind::Empty) == 0 && uint8_t(UnitKind::Relic) == 31 &&
              uint8_t(UnitKind::Rot) == 34 && uint8_t(UnitKind::Count) == 35,
              "review preview catalogue/version support when persisted ids change");

// Descriptor-only I/O avoids fopen's heap-backed FILE/buffer. Every read/seek is
// checked against fresh fstat on this SAME open descriptor, never a path stat or
// a declared chunk end. lseek END/CUR supplies physical EOF/position checks (the
// descriptor equivalents of fseek/ftell). No retry loop or hostile-sized buffer.
class BoundedDescriptor {
public:
    explicit BoundedDescriptor(int fd) : fd_(fd) {}

    bool initialize() {
        struct stat st{};
        if (::fstat(fd_, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0) return false;
        length_ = st.st_size;
        return ::lseek(fd_, 0, SEEK_END) == length_ &&
               ::lseek(fd_, 0, SEEK_SET) == 0 && bounds(0);
    }

    bool read_bytes(void* destination, size_t count) {
        if (!bounds(count)) return false;
        const ssize_t got = ::read(fd_, destination, count);
        if (got < 0 || static_cast<size_t>(got) != count) return false;
        position_ += static_cast<off_t>(count); // bounds proves representability
        return ::lseek(fd_, 0, SEEK_CUR) == position_;
    }

    bool skip(uint32_t count) {
        if (!bounds(count)) return false;
        const off_t next = position_ + static_cast<off_t>(count);
        if (::lseek(fd_, next, SEEK_SET) != next) return false;
        position_ = next;
        return true;
    }

    bool unchanged() const {
        if (!bounds(0) || ::lseek(fd_, 0, SEEK_END) != length_) return false;
        struct stat st{};
        return ::fstat(fd_, &st) == 0 && S_ISREG(st.st_mode) && st.st_size == length_;
    }

    off_t position() const { return position_; }
    off_t length() const { return length_; }

private:
    bool bounds(size_t count) const {
        struct stat st{};
        return ::fstat(fd_, &st) == 0 && S_ISREG(st.st_mode) && st.st_size == length_ &&
               position_ >= 0 && position_ <= length_ &&
               uint64_t(count) <= uint64_t(length_ - position_) &&
               ::lseek(fd_, 0, SEEK_CUR) == position_;
    }

    int fd_;
    off_t length_ = 0;
    off_t position_ = 0;
};

uint32_t little_u32(const uint8_t* bytes) {
    return uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) |
           (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
}

bool copy_kinds(const uint8_t* core, unsigned chain_header, unsigned params,
                PatchPreview& preview) {
    const unsigned unit_bytes = 3u + params * 5u;
    const unsigned chain_bytes = chain_header + kSlots * unit_bytes;
    bool supported = true;
    for (unsigned chain = 0; chain < kChains; ++chain) {
        for (unsigned slot = 0; slot < kSlots; ++slot) {
            const uint8_t raw = core[chain * chain_bytes + chain_header + slot * unit_bytes];
            preview.kinds[chain][slot] = raw;
            if (raw != uint8_t(UnitKind::Empty) && !unit_kind_available(UnitKind(raw)))
                supported = false;
        }
    }
    // Format-specific UNIT/config/scene rules belong to load(), not this preview.
    return supported;
}

PreviewStatus read_metadata(BoundedDescriptor& file, PatchPreview& preview) {
    uint8_t header[5];
    if (!file.read_bytes(header, sizeof header) || std::memcmp(header, "SNC", 3))
        return PreviewStatus::Invalid;
    preview.version = header[4];
    if (preview.version < 1 || preview.version > 22) return PreviewStatus::Unsupported;
    const uint8_t suffix = preview.version < 10 ? uint8_t('0' + preview.version)
                                               : uint8_t('A' + preview.version - 10);
    if (header[3] != suffix) return PreviewStatus::Invalid;

    uint8_t core[kMaxCoreBytes];
    if (preview.version <= 6) {
        const unsigned chain_header = preview.version == 1 ? 3 : 5;
        if (!file.read_bytes(core, core_bytes(chain_header, kLegacyParams)))
            return PreviewStatus::Invalid;
        // The remainder (sigils, sample paths, optional STRN) is NOT validated.
        return copy_kinds(core, chain_header, kLegacyParams, preview)
            ? PreviewStatus::Ready : PreviewStatus::Unsupported;
    }

    bool seen_core = false, supported = true;
    unsigned headers = 0;
    while (file.position() < file.length()) {
        if (headers++ == kMaxHeaders) return PreviewStatus::Invalid;
        uint8_t chunk[8];
        if (!file.read_bytes(chunk, sizeof chunk)) return PreviewStatus::Invalid;
        const uint32_t length = little_u32(chunk + 4);
        if (length > kMaxChunkBytes) return PreviewStatus::Invalid;
        if (!std::memcmp(chunk, "CORE", 4)) {
            if (seen_core) return PreviewStatus::Invalid;
            unsigned params = 0;
            if (length == core_bytes(5, kCoreParams)) params = kCoreParams;
            else if (length == core_bytes(5, kOldCoreParams)) params = kOldCoreParams;
            else return PreviewStatus::Invalid;
            // Only these two fixed sizes can influence read count, never a wire
            // parameter count. The COMPLETE needed CORE, including last padding,
            // must exist physically even though only kind bytes are interpreted.
            if (!file.read_bytes(core, length)) return PreviewStatus::Invalid;
            supported = copy_kinds(core, 5, params, preview);
            seen_core = true;
        } else if (!file.skip(length)) {
            return PreviewStatus::Invalid;
        }
    }
    if (!seen_core) return PreviewStatus::Invalid;
    return supported ? PreviewStatus::Ready : PreviewStatus::Unsupported;
}

} // namespace

bool read_preview(const char* path, PatchPreview& out) {
    out = {};
    if (!path || !*path) return false;
    // NONBLOCK also makes a FIFO/non-file path fail promptly at descriptor stat.
    const int fd = ::open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) return false;

    PatchPreview candidate;
    candidate.status = PreviewStatus::Invalid;
    BoundedDescriptor file(fd);
    if (file.initialize()) {
        candidate.status = read_metadata(file, candidate);
        if (!file.unchanged()) candidate.status = PreviewStatus::Invalid;
    }
    // Close is attempted exactly once on every successful open, even failures.
    // Do not retry an uncertain close: its descriptor may already be released.
    if (::close(fd) != 0) candidate.status = PreviewStatus::Invalid;
    if (candidate.status != PreviewStatus::Ready)
        std::memset(candidate.kinds, 0, sizeof candidate.kinds);
    out = candidate;
    return out.status == PreviewStatus::Ready;
}

} // namespace seance::patchio
