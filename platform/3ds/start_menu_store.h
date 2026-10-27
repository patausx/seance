#pragma once
// Small app-owned sidecar: preferences and recent slots. Only v4 records can
// attest that a timestamp came from a successful open, rather than an old save.
// Independent of SNC project contents and the legacy last_patch.bin file.
#include <cstdint>
#include <cstddef>
#include "../../core/engine/project_library.h"
#include "theme_config.h"

namespace seance::platform::ui3ds {
struct StartHistoryEntry {
    int slot = -1;
    uint64_t used_at = 0; // raw historical value retained; meaningful as open time only below
    bool opened = false; // successful open recorded by v4, never inferred from mtime
};
struct StartMenuStore {
    bool show_intro = true; // compatibility-only SNMU bit 0; retained, not a runtime preference
    bool show_menu = true;
    bool menu_sound = true;
    theme_config::Settings theme; // app-only; v1 pink/gray migrate without touching projects
    int count = 0;
    StartHistoryEntry recent[8];
};
constexpr size_t START_STORE_V1_BYTES = 84;
constexpr size_t START_STORE_BYTES = 96; // v2/v3/v4: header/history + 3 opaque ABGR colors + checksum
bool start_store_decode(const uint8_t* bytes, size_t size, StartMenuStore& out);
bool start_store_encode(const StartMenuStore& store, uint8_t (&out)[START_STORE_BYTES]);
bool start_store_load(const char* path, StartMenuStore& out);
bool start_store_save(const char* path, const StartMenuStore& store);
// Call only after a project load succeeds, including explicit startup resume.
void start_store_touch(StartMenuStore& store, int slot, uint64_t now);
void start_store_forget(StartMenuStore& store, int slot);
// MRU slots first, then legacy last-slot (first migration only), then saved mtime.
// Missing/corrupt slots are filtered. Zero fallback stamps give stable slot order.
// Output capacity is independent of the eight stored entries; arrays stay bounded.
int start_store_recent(const StartMenuStore& store, const bool* valid,
                       const uint64_t* saved_at, size_t slot_count, int legacy_slot,
                       int* out, size_t capacity);
template<size_t M>
int start_store_recent(const StartMenuStore& store, const bool* valid,
                       const uint64_t* saved_at, size_t slot_count, int legacy_slot, int (&out)[M]) {
    static_assert(M <= PROJECT_SLOTS, "recent output exceeds catalogue capacity");
    return start_store_recent(store, valid, saved_at, slot_count, legacy_slot, out, M);
}
template<size_t N, size_t M>
int start_store_recent(const StartMenuStore& store, const bool (&valid)[N],
                       const uint64_t (&saved_at)[N], int legacy_slot, int (&out)[M]) {
    static_assert(N <= PROJECT_SLOTS, "project catalogue exceeds capacity");
    return start_store_recent(store, valid, saved_at, N, legacy_slot, out);
}
void start_store_age(uint64_t timestamp, uint64_t now, char* out, size_t size);
} // namespace seance::platform::ui3ds
