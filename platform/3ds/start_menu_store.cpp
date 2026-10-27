#include "start_menu_store.h"
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <cerrno>

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
    char temp[256], backup[256];
    if (std::snprintf(temp, sizeof temp, "%s.tmp", path) >= (int)sizeof temp ||
        std::snprintf(backup, sizeof backup, "%s.bak", path) >= (int)sizeof backup) return false;
    FILE* f = std::fopen(temp, "wb");
    if (!f) return false;
    bool ok = std::fwrite(bytes, 1, sizeof bytes, f) == sizeof bytes;
    if (std::fflush(f) != 0) ok = false;
    if (std::fclose(f) != 0) ok = false;
    StartMenuStore verify;
    uint8_t actual[START_STORE_BYTES];
    ok = ok && start_store_load(temp, verify) && start_store_encode(verify, actual) &&
         std::memcmp(bytes, actual, sizeof bytes) == 0;
    if (!ok) { std::remove(temp); return false; }
    // FAT implementations may refuse rename-over-existing. Keep an on-disk
    // prior version before replacing; boot can recover it after an interrupted swap.
    struct stat st{};
    const int status = stat(path, &st);
    const bool exists = status == 0;
    if ((exists && !S_ISREG(st.st_mode)) || (status != 0 && errno != ENOENT)) {
        std::remove(temp); return false;
    }
    // Never replace a verified recovery copy with a corrupt primary.
    StartMenuStore previous;
    const bool backup_primary = exists && start_store_load(path, previous);
    if (backup_primary) {
        std::remove(backup);
        if (std::rename(path, backup) != 0) { std::remove(temp); return false; }
    } else if (exists && std::remove(path) != 0) {
        std::remove(temp); return false;
    }
    if (std::rename(temp, path) != 0) {
        if (backup_primary) std::rename(backup, path);
        std::remove(temp);
        return false;
    }
    return true;
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
