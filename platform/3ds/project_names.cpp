#include "project_names.h"
#include "../../core/io/file_publication.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <sys/stat.h>

namespace seance::platform::ui3ds {
namespace {
constexpr size_t kPayload = PROJECT_SLOTS * PROJECT_NAME_BYTES;
constexpr size_t kBytes = 8 + kPayload + 4;
uint32_t checksum(const uint8_t* p, size_t size) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < size; ++i) h = (h ^ p[i]) * 16777619u;
    return h;
}
bool canonical(const char* name) {
    int end = 0;
    while (end < PROJECT_NAME_BYTES && name[end]) {
        const unsigned char c = static_cast<unsigned char>(name[end]);
        if (c < 32 || c > 126 || (c >= 'A' && c <= 'Z')) return false;
        ++end;
    }
    if (end == PROJECT_NAME_BYTES || (end && (name[0] == ' ' || name[end - 1] == ' '))) return false;
    for (int i = end; i < PROJECT_NAME_BYTES; ++i) if (name[i]) return false;
    return true;
}
bool encode(const ProjectNames& names, uint8_t (&bytes)[kBytes]) {
    for (const auto& name : names.names) if (!canonical(name)) return false;
    std::memcpy(bytes, "SNPN", 4);
    bytes[4] = 1;
    bytes[5] = PROJECT_NAME_BYTES;
    bytes[6] = PROJECT_SLOTS & 255;
    bytes[7] = PROJECT_SLOTS >> 8;
    std::memcpy(bytes + 8, names.names, kPayload);
    const uint32_t sum = checksum(bytes, kBytes - 4);
    for (int b = 0; b < 4; ++b) bytes[kBytes - 4 + b] = uint8_t(sum >> (8 * b));
    return true;
}
bool decode(const uint8_t* bytes, size_t size, ProjectNames& out) {
    if (size != kBytes || std::memcmp(bytes, "SNPN", 4) || bytes[4] != 1 ||
        bytes[5] != PROJECT_NAME_BYTES || (int(bytes[6]) | int(bytes[7]) << 8) != PROJECT_SLOTS) return false;
    uint32_t sum = 0;
    for (int b = 0; b < 4; ++b) sum |= uint32_t(bytes[kBytes - 4 + b]) << (8 * b);
    if (sum != checksum(bytes, kBytes - 4)) return false;
    // Validate directly in the bounded byte payload before changing caller state.
    for (int i = 0; i < PROJECT_SLOTS; ++i)
        if (!canonical(reinterpret_cast<const char*>(bytes + 8 + i * PROJECT_NAME_BYTES))) return false;
    std::memcpy(out.names, bytes + 8, kPayload);
    return true;
}
} // namespace

bool project_name_set(ProjectNames& names, int slot, const char* name) {
    if (!project_slot_valid(slot) || !name) return false;
    while (*name == ' ') ++name;
    char staged[PROJECT_NAME_BYTES] = {};
    size_t length = 0, significant = 0;
    // Continue over trailing spaces, but do not accept a subsequent character
    // beyond the 23-character bound. No truncation or partial caller mutation.
    for (; *name; ++name) {
        unsigned char c = static_cast<unsigned char>(*name);
        if (c < 32 || c > 126) return false;
        if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
        if (length < PROJECT_NAME_BYTES - 1) staged[length] = char(c);
        if (c != ' ') {
            if (length >= PROJECT_NAME_BYTES - 1) return false;
            significant = length + 1;
        }
        // Saturation avoids overflow for arbitrarily long trailing spaces.
        if (length < PROJECT_NAME_BYTES) ++length;
    }
    if (!significant) return false;
    std::memset(staged + significant, 0, sizeof staged - significant);
    std::memcpy(names.names[slot], staged, sizeof staged);
    return true;
}
void project_name_clear(ProjectNames& names, int slot) {
    if (project_slot_valid(slot)) std::memset(names.names[slot], 0, PROJECT_NAME_BYTES);
}
void project_name_label(const ProjectNames& names, int slot, char* out, size_t size) {
    if (!out || !size) return;
    if (!project_slot_valid(slot)) std::snprintf(out, size, "unsaved");
    else if (canonical(names.names[slot]) && names.names[slot][0])
        std::snprintf(out, size, "%s", names.names[slot]);
    else std::snprintf(out, size, "patch %03d", slot + 1);
}
bool project_names_load(const char* path, ProjectNames& out) {
    if (!path) return false;
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    uint8_t bytes[kBytes + 1];
    const size_t got = std::fread(bytes, 1, sizeof bytes, f);
    const bool read_ok = !std::ferror(f);
    const bool close_ok = std::fclose(f) == 0;
    return read_ok && close_ok && decode(bytes, got, out);
}
bool project_names_save(const char* path, const ProjectNames& names) {
    uint8_t bytes[kBytes];
    if (!path || !*path || !encode(names, bytes)) return false;
    char backup[256], unreadable[256];
    const int n = std::snprintf(backup, sizeof backup, "%s.bak", path);
    const int m = std::snprintf(unreadable, sizeof unreadable, "%s.old", path);
    if (n < 0 || size_t(n) >= sizeof backup || m < 0 || size_t(m) >= sizeof unreadable) return false;
    bool exists = false, recovery_exists = false;
    if (!fileio::regular_or_absent(path, exists) ||
        !fileio::regular_or_absent(backup, recovery_exists)) return false;
    (void)recovery_exists;
    fileio::OwnedFile temporary;
    if (!fileio::create_temporary(temporary, path)) return false;
    FILE* f = temporary.stream();
    const bool wrote = std::fwrite(bytes, 1, sizeof bytes, f) == sizeof bytes;
    const bool closed = temporary.close(true);
    ProjectNames verify;
    if (!wrote || !closed || !project_names_load(temporary.path(), verify) ||
        std::memcmp(names.names, verify.names, kPayload)) return false;
    if (!exists) return temporary.publish_vacant(path);
    // The SD card refuses rename onto an existing name, so the current file is
    // moved aside first: a valid one becomes the .bak recovery copy, an
    // unreadable one goes to .old and never displaces a good .bak.
    uint8_t prior[kBytes + 1]; size_t got = 0;
    if (!fileio::read_bounded(path, prior, sizeof prior, got)) return false;
    return temporary.publish_replacing(path, decode(prior, got, verify) ? backup : unreadable);
}
} // namespace seance::platform::ui3ds
