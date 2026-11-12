#include "start_menu_store.h"
#include "../../core/io/file_publication.h"
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <cerrno>
#ifdef __3DS__
#include <3ds.h>
#endif

namespace seance::platform::ui3ds {
static_assert(PROJECT_SLOTS == 128, "SNMU v4 slot/provenance packing requires 128 slots");
namespace {
uint32_t checksum(const uint8_t* p, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 16777619u;
    return h;
}
bool valid_store(const StartMenuStore& s) {
    if (s.count < 0 || s.count > 8 || !theme_config::valid(s.theme)) return false;
    bool seen[PROJECT_SLOTS] = {};
    for (int i = 0; i < s.count; ++i) {
        const int slot = s.recent[i].slot;
        if (!project_slot_valid(slot) || seen[slot]) return false;
        seen[slot] = true;
    }
    return true;
}
}

bool start_store_encode(const StartMenuStore& s, uint8_t (&out)[START_STORE_BYTES]) {
    if (!valid_store(s)) return false;
    std::memset(out, 0, sizeof out);
    std::memcpy(out, "SNMU", 4);
    out[4] = 4;
    out[5] = (s.show_intro ? 1 : 0) | (s.show_menu ? 2 : 0) | (s.menu_sound ? 4 : 0);
    out[6] = (uint8_t)s.count;
    out[7] = uint8_t(s.theme.id);
    for (int i = 0; i < 8; ++i) {
        const size_t offset = 8 + i * 9;
        // In occupied entries bit7 attests a successful open. Count distinguishes
        // known-open slot127 (0xff) from canonical trailing empty entries.
        out[offset] = i < s.count
            ? uint8_t(s.recent[i].slot | (s.recent[i].opened ? 0x80 : 0)) : 255;
        const uint64_t stamp = i < s.count ? s.recent[i].used_at : 0;
        for (int b = 0; b < 8; ++b) out[offset + 1 + b] = (uint8_t)(stamp >> (8 * b));
    }
    const uint32_t custom[] = {s.theme.custom.paper, s.theme.custom.ink, s.theme.custom.accent};
    for (int i = 0; i < 3; ++i)
        for (int b = 0; b < 4; ++b) out[80 + i * 4 + b] = uint8_t(custom[i] >> (8 * b));
    const uint32_t sum = checksum(out, START_STORE_BYTES - 4);
    for (int b = 0; b < 4; ++b) out[START_STORE_BYTES - 4 + b] = (uint8_t)(sum >> (8 * b));
    return true;
}

bool start_store_decode(const uint8_t* p, size_t size, StartMenuStore& out) {
    if (!p || (size != START_STORE_V1_BYTES && size != START_STORE_BYTES)) return false;
    const bool v1 = size == START_STORE_V1_BYTES;
    const bool v3 = !v1 && p[4] == 3;
    const bool v4 = !v1 && p[4] == 4;
    if (std::memcmp(p, "SNMU", 4) || (v1 ? p[4] != 1 : (p[4] != 2 && !v3 && !v4)) ||
        (p[5] & ~((v3 || v4) ? 7u : 3u)) || p[6] > 8 || p[7] >= (v1 ? 2 : theme_config::kCount)) return false;
    uint32_t sum = 0;
    for (int b = 0; b < 4; ++b) sum |= uint32_t(p[size - 4 + b]) << (8 * b);
    if (sum != checksum(p, size - 4)) return false;
    StartMenuStore staged;
    staged.show_intro = (p[5] & 1) != 0;
    staged.show_menu = (p[5] & 2) != 0;
    if (v3 || v4) staged.menu_sound = (p[5] & 4) != 0; // v1/v2 retain the default on
    staged.theme.id = theme_config::Id(p[7]);
    if (!v1) {
        uint32_t custom[3] = {};
        for (int i = 0; i < 3; ++i)
            for (int b = 0; b < 4; ++b) custom[i] |= uint32_t(p[80 + i * 4 + b]) << (8 * b);
        staged.theme.custom = {custom[0], custom[1], custom[2]};
    }
    staged.count = p[6];
    for (int i = 0; i < 8; ++i) {
        const size_t offset = 8 + i * 9;
        uint64_t stamp = 0;
        for (int b = 0; b < 8; ++b) stamp |= uint64_t(p[offset + 1 + b]) << (8 * b);
        if (i < staged.count) {
            const int slot = v4 ? (p[offset] & 0x7f) : p[offset];
            // Legacy used_at was also refreshed by SAVE. Preserve it losslessly,
            // but never advertise it as a last-open timestamp after migration.
            staged.recent[i] = {slot, stamp, v4 && (p[offset] & 0x80) != 0};
        } else if (p[offset] != 255 || stamp) return false;
    }
    if (!valid_store(staged)) return false;
    out = staged;
    return true;
}

bool start_store_load(const char* path, StartMenuStore& out) {
    if (!path) return false;
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    uint8_t bytes[START_STORE_BYTES + 1];
    const size_t got = std::fread(bytes, 1, sizeof bytes, f);
    const bool io_ok = !std::ferror(f);
    const bool close_ok = std::fclose(f) == 0;
    return io_ok && close_ok && start_store_decode(bytes, got, out);
}

bool start_store_save(const char* path, const StartMenuStore& store) {
    uint8_t bytes[START_STORE_BYTES];
    if (!path || !start_store_encode(store, bytes)) return false;
    char backup[256];
    const int n = std::snprintf(backup, sizeof backup, "%s.bak", path);
    if (n < 0 || size_t(n) >= sizeof backup) return false;
    bool exists = false, recovery_exists = false;
    if (!fileio::regular_or_absent(path, exists) ||
        !fileio::regular_or_absent(backup, recovery_exists)) return false;
    (void)recovery_exists;
    fileio::OwnedFile temporary, recovery;
    if (!fileio::create_temporary(temporary, path)) return false;
    FILE* f = temporary.stream();
    const bool wrote = std::fwrite(bytes, 1, sizeof bytes, f) == sizeof bytes;
    const bool closed = temporary.close(true);
    StartMenuStore verify;
    uint8_t actual[START_STORE_BYTES];
    if (!wrote || !closed || !start_store_load(temporary.path(), verify) ||
        !start_store_encode(verify, actual) || std::memcmp(bytes, actual, sizeof bytes)) return false;
    // Stage a valid primary's recovery COPY without moving/unlinking either
    // existing name. An I/O refusal is not evidence that the primary is corrupt.
    if (exists) {
        uint8_t prior[START_STORE_BYTES + 1]; size_t got = 0;
        if (!fileio::read_bounded(path, prior, sizeof prior, got)) return false;
        if (start_store_decode(prior, got, verify)) {
            if (!fileio::create_temporary(recovery, backup) || !fileio::copy_file(path, recovery)) return false;
            if (!start_store_load(recovery.path(), verify)) return false;
        }
    }
    if (!temporary.publish(path)) return false; // commit point: never remove primary
    // Recovery rotation is best-effort AFTER successful primary publication.
    // A second rename refusal keeps the old .bak; it cannot undo a committed save
    // or destroy either valid version. FAT replacement refusal is fail-closed.
    if (recovery.path()[0]) (void)recovery.publish(backup);
    return true;
}

namespace {
// SNMJ v1 is a publication envelope, NOT a replacement SNMU version. The inner
// 96-byte SNMU v4 serialization stays byte-exact. Generation establishes commit
// order independently of clock/mtime, recent-slot timestamps or filename order.
constexpr size_t JOURNAL_BYTES = 16 + START_STORE_BYTES + 4;
struct PreferenceJournal {
    char path[256] = {};
    bool exists = false;
    uint64_t generation = 0;
    uint8_t bytes[JOURNAL_BYTES] = {};
};
bool sibling(char (&out)[256], const char* path, const char* suffix) {
    if (!path || !*path) return false;
    const int n = std::snprintf(out, sizeof out, "%s%s", path, suffix);
    return n >= 0 && size_t(n) < sizeof out;
}
bool read_journal(PreferenceJournal& journal) {
    if (!fileio::regular_or_absent(journal.path, journal.exists)) return false;
    if (!journal.exists) return true;
    uint8_t raw[JOURNAL_BYTES + 1]; size_t got = 0;
    if (!fileio::read_bounded(journal.path, raw, sizeof raw, got) || got != JOURNAL_BYTES ||
        std::memcmp(raw, "SNMJ\1\0\0\0", 8)) return false;
    uint32_t sum = 0;
    for (int b = 0; b < 4; ++b) sum |= uint32_t(raw[JOURNAL_BYTES - 4 + b]) << (8 * b);
    if (sum != checksum(raw, JOURNAL_BYTES - 4)) return false;
    StartMenuStore decoded; uint8_t canonical[START_STORE_BYTES];
    if (!start_store_decode(raw + 16, START_STORE_BYTES, decoded) ||
        !start_store_encode(decoded, canonical) || std::memcmp(canonical, raw + 16, sizeof canonical)) return false;
    for (int b = 0; b < 8; ++b) journal.generation |= uint64_t(raw[8 + b]) << (8 * b);
    if (!journal.generation) return false;
    std::memcpy(journal.bytes, raw, JOURNAL_BYTES);
    return true;
}
bool journals(const char* path, PreferenceJournal (&records)[2], int& latest) {
    if (!sibling(records[0].path, path, ".prefs0") ||
        !sibling(records[1].path, path, ".prefs1") ||
        !read_journal(records[0]) || !read_journal(records[1])) return false;
    latest = records[0].exists ? 0 : records[1].exists ? 1 : -1;
    if (records[0].exists && records[1].exists) {
        // Two distinct commits must never have the same generation. Preserve
        // both ambiguous names rather than guessing an ordering or erasing one.
        if (records[0].generation == records[1].generation) return false;
        latest = records[1].generation > records[0].generation ? 1 : 0;
    }
    return true;
}
enum class LegacyRead { Absent, Invalid, Valid, Error };
LegacyRead legacy_read(const char* path, StartMenuStore& out) {
    bool exists = false;
    if (!fileio::regular_or_absent(path, exists)) return LegacyRead::Error;
    if (!exists) return LegacyRead::Absent;
    uint8_t bytes[START_STORE_BYTES + 1]; size_t got = 0;
    if (!fileio::read_bounded(path, bytes, sizeof bytes, got)) return LegacyRead::Error;
    return start_store_decode(bytes, got, out) ? LegacyRead::Valid : LegacyRead::Invalid;
}
bool create_journal_temporary(fileio::OwnedFile& file, const char* destination) {
    // libctru's error table does not normalize every FS already-exists result.
    // Skip known occupied regular siblings BEFORE O_EXCL; a race/open error
    // still grants no cleanup authority and fails closed, never reopen with wb.
    char path[256];
    for (unsigned i = 0; i < 100; ++i) {
        const int n = i ? std::snprintf(path, sizeof path, "%s.tmp-%02u", destination, i)
                        : std::snprintf(path, sizeof path, "%s.tmp", destination);
        if (n < 0 || size_t(n) >= sizeof path) return false;
        bool exists = false;
        if (!fileio::regular_or_absent(path, exists)) return false;
        if (exists) continue;
        return file.create(path) == fileio::ReserveResult::Ok;
    }
    return false;
}
// A vacant destination avoids both FAT replacement refusal AND libctru's
// delete-target fallback. On 3DS call the FS service directly: even a late
// competing name must not enter archive_rename's destructive replacement path.
bool publish_preference(fileio::OwnedFile& temporary, const char* destination) {
    if (!fileio::absent(destination)) return false;
#ifdef __3DS__
    if (std::strncmp(destination, "sdmc:/", 6) || std::strncmp(temporary.path(), "sdmc:/", 6)) return false;
    FS_Archive archive;
    const Result opened = FSUSER_OpenArchive(&archive, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""));
    if (R_FAILED(opened)) { errno = int(opened); return false; }
    const Result renamed = FSUSER_RenameFile(archive, fsMakePath(PATH_ASCII, temporary.path() + 5),
                                           archive, fsMakePath(PATH_ASCII, destination + 5));
    // Closing this extra archive handle cannot revoke a completed publication.
    (void)FSUSER_CloseArchive(archive);
    if (R_FAILED(renamed)) { errno = int(renamed); return false; }
    temporary.retain();
    return true;
#else
    return temporary.publish(destination); // host oracle; serialized directory
#endif
}
}

bool start_store_load_preferences(const char* path, StartMenuStore& out) {
    PreferenceJournal records[2]; int latest = -1;
    if (!journals(path, records, latest)) return false;
    if (latest >= 0) return start_store_decode(records[latest].bytes + 16, START_STORE_BYTES, out);
    // Import legacy only when NEITHER journal exists. Read/close errors are not
    // corruption; they never authorize falling back to an older/default store.
    const auto state = legacy_read(path, out);
    if (state == LegacyRead::Valid) return true;
    if (state == LegacyRead::Error) return false;
    char backup[256];
    return sibling(backup, path, ".bak") && legacy_read(backup, out) == LegacyRead::Valid;
}

bool start_store_save_preferences(const char* path, const StartMenuStore& store) {
    uint8_t payload[START_STORE_BYTES];
    if (!start_store_encode(store, payload)) return false;
    PreferenceJournal records[2]; int latest = -1;
    if (!journals(path, records, latest)) return false;
    // Before first import, inspect old names without moving/truncating anything.
    // A readable malformed legacy record can be left archived; an I/O error
    // cannot. In particular, we do not claim it is corrupt to justify deletion.
    if (latest < 0) {
        StartMenuStore legacy; char backup[256];
        if (!sibling(backup, path, ".bak") || legacy_read(path, legacy) == LegacyRead::Error ||
            legacy_read(backup, legacy) == LegacyRead::Error) return false;
    }
    const uint64_t previous = latest < 0 ? 0 : records[latest].generation;
    if (previous == UINT64_MAX) return false;
    const int target = latest < 0 ? 0 : 1 - latest;
    uint8_t bytes[JOURNAL_BYTES] = {};
    std::memcpy(bytes, "SNMJ\1\0\0\0", 8);
    for (int b = 0; b < 8; ++b) bytes[8 + b] = uint8_t((previous + 1) >> (8 * b));
    std::memcpy(bytes + 16, payload, sizeof payload);
    const uint32_t sum = checksum(bytes, JOURNAL_BYTES - 4);
    for (int b = 0; b < 4; ++b) bytes[JOURNAL_BYTES - 4 + b] = uint8_t(sum >> (8 * b));
    fileio::OwnedFile temporary;
    if (!create_journal_temporary(temporary, records[target].path)) return false;
    FILE* f = temporary.stream();
    const bool wrote = std::fwrite(bytes, 1, sizeof bytes, f) == sizeof bytes;
    const bool flushed = std::fflush(f) == 0;
    const bool synced = flushed && ::fsync(::fileno(f)) == 0;
    const bool closed = temporary.close(false);
    PreferenceJournal verified;
    std::strcpy(verified.path, temporary.path());
    if (!wrote || !flushed || !synced || !closed || !read_journal(verified) ||
        !verified.exists || std::memcmp(verified.bytes, bytes, sizeof bytes)) return false;
    if (records[target].exists) {
        // Recheck both snapshots before reclaiming only the older COMMITTED
        // envelope. Cleanup of old SNMU/.bak/.tmp and unrecognized journal data
        // is forbidden. Newest committed prefs survive even if remove is EIO.
        PreferenceJournal check[2]; int now = -1;
        if (!journals(path, check, now) || now != latest ||
            std::memcmp(check[0].bytes, records[0].bytes, JOURNAL_BYTES) ||
            std::memcmp(check[1].bytes, records[1].bytes, JOURNAL_BYTES) ||
            std::remove(records[target].path) != 0) return false;
    }
    const bool published = publish_preference(temporary, records[target].path);
    // Resolve an applied-but-error reply only by exact generation+payload
    // readback AND vanished source. Never erase the destination on uncertainty.
    PreferenceJournal committed;
    std::strcpy(committed.path, records[target].path);
    const bool exact = read_journal(committed) && committed.exists &&
        !std::memcmp(committed.bytes, bytes, sizeof bytes);
    if (!published && (!fileio::absent(temporary.path()) || !exact)) return false;
    return exact;
}

bool last_patch_store_save(const char* path, int slot) {
    if (!path || !*path || !project_slot_valid(slot)) return false;
    bool exists = false;
    if (!fileio::regular_or_absent(path, exists)) return false;
    fileio::OwnedFile temporary;
    if (!fileio::create_temporary(temporary, path)) return false;
    const uint8_t bytes[5] = {'S','L','O','T', uint8_t(slot)};
    const bool wrote = std::fwrite(bytes, 1, sizeof bytes, temporary.stream()) == sizeof bytes;
    const bool closed = temporary.close(true);
    uint8_t actual[6]; size_t got = 0;
    if (!wrote || !closed || !fileio::read_bounded(temporary.path(), actual, sizeof actual, got) ||
        got != sizeof bytes || std::memcmp(actual, bytes, sizeof bytes)) return false;
    return temporary.publish(path); // rename failure retains old SLOT and old .tmp
}

void start_store_forget(StartMenuStore& store, int slot) {
    if (!valid_store(store)) { store = {}; return; }
    for (int i = 0; i < store.count; ++i) {
        if (store.recent[i].slot != slot) continue;
        for (int j = i; j + 1 < store.count; ++j) store.recent[j] = store.recent[j + 1];
        store.recent[--store.count] = {};
        return;
    }
}

void start_store_touch(StartMenuStore& store, int slot, uint64_t now) {
    if (!project_slot_valid(slot)) return;
    start_store_forget(store, slot);
    if (store.count < 8) ++store.count;
    for (int i = store.count - 1; i > 0; --i) store.recent[i] = store.recent[i - 1];
    store.recent[0] = {slot, now, true};
}

int start_store_recent(const StartMenuStore& store, const bool* valid,
                       const uint64_t* saved_at, size_t slot_count, int legacy_slot,
                       int* out, size_t capacity) {
    if (!out) return 0;
    for (size_t i = 0; i < capacity; ++i) out[i] = -1;
    if (!valid || !saved_at || !capacity) return 0;
    if (slot_count > PROJECT_SLOTS) slot_count = PROJECT_SLOTS;
    int count = 0;
    bool used[PROJECT_SLOTS] = {};
    const auto append = [&](int slot) {
        if (size_t(count) == capacity || slot < 0 || size_t(slot) >= slot_count || !valid[slot] || used[slot]) return;
        out[count++] = slot; used[slot] = true;
    };
    if (valid_store(store)) for (int i = 0; i < store.count; ++i) append(store.recent[i].slot);
    append(legacy_slot);
    for (size_t n = 0; n < slot_count && size_t(count) < capacity; ++n) {
        int best = -1;
        for (int i = 0; size_t(i) < slot_count; ++i)
            if (valid[i] && !used[i] && (best < 0 || saved_at[i] > saved_at[best])) best = i;
        if (best < 0) break;
        append(best);
    }
    return count;
}

void start_store_age(uint64_t timestamp, uint64_t now, char* out, size_t size) {
    if (!out || !size) return;
    // Bad/unknown clocks must not wrap into fabricated multi-billion-day ages.
    if (!timestamp || timestamp > now || !now) { std::snprintf(out, size, "--"); return; }
    const uint64_t elapsed = now - timestamp;
    if (elapsed < 60) std::snprintf(out, size, "now");
    else if (elapsed < 3600) std::snprintf(out, size, "%u min", (unsigned)(elapsed / 60));
    else if (elapsed < 86400) std::snprintf(out, size, "%u hr", (unsigned)(elapsed / 3600));
    else if (elapsed < 172800) std::snprintf(out, size, "yesterday");
    else if (elapsed / 86400 < 10000) std::snprintf(out, size, "%u days", (unsigned)(elapsed / 86400));
    else std::snprintf(out, size, "long ago");
}
} // namespace seance::platform::ui3ds
