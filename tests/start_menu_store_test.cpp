#include "../platform/3ds/start_menu_store.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <string>
#include <sys/stat.h>
using namespace seance::platform::ui3ds;

template<size_t N> static void seal(uint8_t (&p)[N]) {
    uint32_t sum = 2166136261u;
    for (size_t i = 0; i < sizeof p - 4; ++i) sum = (sum ^ p[i]) * 16777619u;
    for (int b = 0; b < 4; ++b) p[sizeof p - 4 + b] = (uint8_t)(sum >> (8 * b));
}
template<size_t N> static void legacy_slots(uint8_t (&p)[N]) {
    for (int i = 0; i < p[6]; ++i) p[8 + i * 9] &= 0x7f;
}

static void same_history(const StartMenuStore& a, const StartMenuStore& b) {
    assert(a.count == b.count && a.theme == b.theme);
    assert(a.show_intro == b.show_intro && a.show_menu == b.show_menu && a.menu_sound == b.menu_sound);
    for (int i = 0; i < a.count; ++i) {
        assert(a.recent[i].slot == b.recent[i].slot);
        assert(a.recent[i].used_at == b.recent[i].used_at);
        assert(a.recent[i].opened == b.recent[i].opened);
    }
}

static void test_v4_history() {
    uint8_t bytes[START_STORE_BYTES], encoded[START_STORE_BYTES];
    // All 128 slot bytes in both provenance states, including occupied 0xff.
    for (int slot = 0; slot < seance::PROJECT_SLOTS; ++slot) for (bool opened : {false, true}) {
        StartMenuStore store, loaded;
        store.count = 1; store.recent[0] = {slot, UINT64_MAX - uint64_t(slot), opened};
        assert(start_store_encode(store, bytes));
        assert(bytes[8] == (slot | (opened ? 0x80 : 0)));
        assert(bytes[17] == 255);
        assert(start_store_decode(bytes, sizeof bytes, loaded));
        same_history(store, loaded);
        assert(start_store_encode(loaded, encoded) && !std::memcmp(bytes, encoded, sizeof bytes));
    }
    // v3 retains sound/theme/raw ages, but no timestamp becomes trusted through
    // migration or a subsequent preference-only re-encode. Only touch attests it.
    for (int flags = 0; flags < 8; ++flags) for (int theme = 0; theme < theme_config::kCount; ++theme) {
        StartMenuStore store, migrated;
        store.theme = {theme_config::Id(theme), theme_config::kBlackMass};
        store.show_intro = bool(flags & 1); store.show_menu = bool(flags & 2); store.menu_sound = bool(flags & 4);
        for (int slot = 120; slot < 128; ++slot) start_store_touch(store, slot, 1000 + slot);
        assert(start_store_encode(store, bytes));
        bytes[4] = 3; legacy_slots(bytes); seal(bytes);
        assert(start_store_decode(bytes, sizeof bytes, migrated));
        for (auto& entry : store.recent) entry.opened = false;
        same_history(store, migrated);
        assert(start_store_encode(migrated, encoded) && encoded[4] == 4);
        assert(!std::memcmp(bytes + 5, encoded + 5, START_STORE_BYTES - 9));
        assert(start_store_decode(encoded, sizeof encoded, migrated));
        same_history(store, migrated);
        start_store_touch(migrated, 123, 99999);
        assert(migrated.recent[0].slot == 123 && migrated.recent[0].opened && migrated.recent[0].used_at == 99999);
        for (int i = 1; i < 8; ++i) assert(!migrated.recent[i].opened);
        start_store_forget(migrated, 123);
        assert(migrated.count == 7 && migrated.recent[7].slot == -1 && !migrated.recent[7].opened && !migrated.recent[7].used_at);
    }
    StartMenuStore store, loaded;
    start_store_touch(store, 127, 0);
    assert(store.recent[0].opened && !store.recent[0].used_at);
    start_store_touch(store, 3, 500);
    assert(start_store_encode(store, bytes));
    for (int version = 1; version <= 4; ++version) {
        uint8_t raw[START_STORE_BYTES]; std::memcpy(raw, bytes, sizeof raw);
        size_t length = sizeof raw;
        raw[4] = uint8_t(version);
        if (version < 4) legacy_slots(raw);
        if (version < 3) raw[5] &= 3;
        if (version == 1) {
            uint8_t old[START_STORE_V1_BYTES]; std::memcpy(old, raw, sizeof old); seal(old);
            std::memcpy(raw, old, sizeof old); length = sizeof old;
        } else seal(raw);
        assert(start_store_decode(raw, length, loaded));
        const auto prior = loaded;
        for (size_t byte = 0; byte < length; ++byte) for (int bit = 0; bit < 8; ++bit) {
            raw[byte] ^= uint8_t(1u << bit);
            assert(!start_store_decode(raw, length, loaded)); same_history(prior, loaded);
            raw[byte] ^= uint8_t(1u << bit);
        }
        for (size_t n = 0; n < length; ++n) {
            assert(!start_store_decode(raw, n, loaded)); same_history(prior, loaded);
        }
    }
    // Recomputed checksum cannot legitimize duplicate slots with different known
    // bits, an unknown version/flags or a noncanonical trailing empty entry.
    assert(start_store_encode(store, bytes));
    for (int which = 0; which < 4; ++which) {
        std::memcpy(encoded, bytes, sizeof bytes);
        if (which == 0) encoded[17] = encoded[8] ^ 0x80;
        if (which == 1) encoded[26] = 127;
        if (which == 2) encoded[27] = 1;
        if (which == 3) encoded[6] = 1; // occupied slot127/0xff is no empty when its timestamp is nonzero
        if (which == 3) encoded[18] = 1;
        seal(encoded); assert(!start_store_decode(encoded, sizeof encoded, loaded));
    }
    // The new provenance bit must never be accepted in a legacy slot byte.
    for (int version = 1; version <= 3; ++version) {
        std::memcpy(encoded, bytes, sizeof bytes);
        encoded[4] = uint8_t(version); encoded[5] &= 3; legacy_slots(encoded);
        encoded[8] = 128;
        if (version == 1) {
            uint8_t old[START_STORE_V1_BYTES]; std::memcpy(old, encoded, sizeof old); seal(old);
            assert(!start_store_decode(old, sizeof old, loaded));
        } else {
            seal(encoded); assert(!start_store_decode(encoded, sizeof encoded, loaded));
        }
    }
    for (int version = 3; version <= 4; ++version) for (int bit = 3; bit < 8; ++bit) {
        std::memcpy(encoded, bytes, sizeof bytes); encoded[4] = uint8_t(version);
        if (version == 3) legacy_slots(encoded);
        encoded[5] |= uint8_t(1u << bit); seal(encoded);
        assert(!start_store_decode(encoded, sizeof encoded, loaded));
    }
    for (int slot : {-1, 128, 255}) {
        auto invalid = store; invalid.recent[0].slot = slot;
        assert(!start_store_encode(invalid, encoded));
    }
    for (int count : {-1, 9}) {
        auto invalid = store; invalid.count = count;
        assert(!start_store_encode(invalid, encoded));
    }
    auto duplicate = store; duplicate.recent[1].slot = duplicate.recent[0].slot;
    duplicate.recent[1].opened = !duplicate.recent[0].opened;
    assert(!start_store_encode(duplicate, encoded));
    std::puts("v4 history: 256 slot/provenance roundtrips, 40 v3 preference migrations, all-version single-bit corruption/truncations, provenance/slot127 canonical padding passed");
}

static void test_recent_capacities() {
    bool valid[seance::PROJECT_SLOTS] = {};
    uint64_t times[seance::PROJECT_SLOTS] = {};
    StartMenuStore store;
    for (int slot = 0; slot < seance::PROJECT_SLOTS; ++slot) {
        valid[slot] = true; times[slot] = uint64_t(slot);
        start_store_touch(store, slot, uint64_t(100 + slot));
    }
    int three[3], five[5], eight[8], all[seance::PROJECT_SLOTS];
    assert(start_store_recent(store, valid, times, -1, three) == 3);
    assert(start_store_recent(store, valid, times, -1, five) == 5);
    assert(start_store_recent(store, valid, times, -1, eight) == 8);
    for (int i = 0; i < 8; ++i) {
        assert(eight[i] == 127 - i);
        if (i < 5) assert(five[i] == eight[i]);
        if (i < 3) assert(three[i] == eight[i]);
    }
    assert(start_store_recent(store, valid, times, 999, -1, all) == 128);
    for (int i = 0; i < 128; ++i) assert(all[i] == 127 - i);
    // All candidate masks, independent 3/5 bounded outputs and null/error paths.
    bool small[8] = {}; uint64_t stamps[8] = {};
    for (int mask = 0; mask < 256; ++mask) {
        int count = 0;
        for (int i = 0; i < 8; ++i) { small[i] = (mask & (1 << i)) != 0; count += small[i]; }
        const int n3 = start_store_recent(store, small, stamps, -1, three);
        const int n5 = start_store_recent(store, small, stamps, -1, five);
        assert(n3 == (count < 3 ? count : 3) && n5 == (count < 5 ? count : 5));
        for (int i = 0; i < 5; ++i) {
            if (i >= n5) assert(five[i] == -1);
            else { assert(small[five[i]]); if (i) assert(five[i] > five[i-1]); }
            if (i < n3) assert(three[i] == five[i]);
            else if (i < 3) assert(three[i] == -1);
        }
    }
    int guarded[7] = {77, 0, 0, 0, 0, 0, 88};
    assert(start_store_recent(store, valid, times, 128, -1, guarded + 1, 5) == 5);
    assert(guarded[0] == 77 && guarded[6] == 88);
    assert(start_store_recent(store, valid, times, 128, -1, guarded + 1, 0) == 0 && guarded[1] == 127);
    assert(start_store_recent(store, valid, times, 128, -1, nullptr, 5) == 0);
    assert(start_store_recent(store, nullptr, times, 128, -1, five) == 0);
    for (int slot : five) assert(slot == -1);
    assert(start_store_recent(store, valid, nullptr, 128, -1, five) == 0);
    std::puts("recent capacity: eight MRU -> three/five/eight, 128 slots, 256 masks, explicit pointer capacity/canaries/null inputs passed");
}

int main() {
    test_v4_history();
    test_recent_capacities();
    StartMenuStore state;
    assert(state.count == 0 && state.show_intro && state.show_menu && state.menu_sound && state.theme.id == theme_config::Id::Pink);
    uint8_t bytes[START_STORE_BYTES];
    assert(start_store_encode(state, bytes));
    assert(bytes[4] == 4 && bytes[5] == 7 && sizeof bytes == 96);
    StartMenuStore loaded;
    assert(start_store_decode(bytes, sizeof bytes, loaded));
    assert(loaded.count == 0 && loaded.show_menu && loaded.show_intro && loaded.menu_sound && loaded.theme.id == theme_config::Id::Pink);
    for (int i = 0; i < 1000; ++i) start_store_touch(state, i % 8, 1700000000u + i);
    assert(state.count == 8 && state.recent[0].slot == 7);
    start_store_touch(state, 3, 123);
    assert(state.count == 8 && state.recent[0].slot == 3 && state.recent[0].used_at == 123 && state.recent[0].opened);
    start_store_touch(state, -1, 0); start_store_touch(state, seance::PROJECT_SLOTS, 0);
    assert(state.count == 8 && state.recent[0].slot == 3);
    state.show_menu = false; state.show_intro = false; state.menu_sound = false;
    state.theme.id = theme_config::Id::Graphite;
    assert(start_store_encode(state, bytes));
    assert(start_store_decode(bytes, sizeof bytes, loaded));
    uint8_t again[START_STORE_BYTES];
    assert(start_store_encode(loaded, again) && !std::memcmp(bytes, again, sizeof bytes));
    assert(!loaded.show_intro && !loaded.show_menu && !loaded.menu_sound && loaded.theme.id == theme_config::Id::Graphite);
    // Exact old v1 84-byte layout, including all eight MRU entries and flags.
    uint8_t v1[START_STORE_V1_BYTES] = {};
    std::memcpy(v1, bytes, 80); v1[4] = 1; legacy_slots(v1);
    for (int id = 0; id < 2; ++id) {
        v1[7] = uint8_t(id); seal(v1);
        StartMenuStore migrated; migrated.menu_sound = false;
        assert(start_store_decode(v1, sizeof v1, migrated));
        assert(migrated.menu_sound);
        assert(migrated.theme.id == theme_config::Id(id));
        assert(migrated.theme.custom == theme_config::kGrimoire);
        assert(!migrated.show_intro && !migrated.show_menu && migrated.count == 8);
        for (int i = 0; i < 8; ++i)
            assert(migrated.recent[i].slot == state.recent[i].slot && migrated.recent[i].used_at == state.recent[i].used_at && !migrated.recent[i].opened);
    }
    v1[7] = 2; seal(v1); assert(!start_store_decode(v1, sizeof v1, loaded));
    v1[7] = 1;
    uint8_t v2[START_STORE_BYTES]; std::memcpy(v2, bytes, sizeof v2); v2[4] = 2; legacy_slots(v2);
    // All legacy flag combinations migrate to sound on, regardless of destination state.
    for (int flags = 0; flags < 4; ++flags) {
        v1[5] = v2[5] = uint8_t(flags); seal(v1); seal(v2);
        for (int version = 1; version <= 2; ++version) {
            StartMenuStore migrated; migrated.menu_sound = false;
            assert(start_store_decode(version == 1 ? v1 : v2, version == 1 ? sizeof v1 : sizeof v2, migrated));
            assert(migrated.menu_sound && migrated.show_intro == bool(flags & 1) && migrated.show_menu == bool(flags & 2));
            assert(migrated.theme == state.theme && migrated.count == state.count);
            for (int i = 0; i < migrated.count; ++i)
                assert(migrated.recent[i].slot == state.recent[i].slot && migrated.recent[i].used_at == state.recent[i].used_at && !migrated.recent[i].opened);
        }
    }
    for (int bit = 2; bit < 8; ++bit) {
        v1[5] = v2[5] = uint8_t(1u << bit); seal(v1); seal(v2);
        assert(!start_store_decode(v1, sizeof v1, loaded));
        assert(!start_store_decode(v2, sizeof v2, loaded));
    }
    for (int id = 0; id < theme_config::kCount; ++id) {
        auto proposal = state; proposal.theme = {theme_config::Id(id), theme_config::kBlackMass};
        uint8_t encoded[START_STORE_BYTES]; StartMenuStore roundtrip;
        for (bool sound : {false, true}) {
            proposal.menu_sound = sound;
            assert(start_store_encode(proposal, encoded) && start_store_decode(encoded, sizeof encoded, roundtrip));
            assert(encoded[4] == 4 && encoded[5] == (sound ? 4 : 0));
            assert(roundtrip.menu_sound == sound && roundtrip.theme == proposal.theme && roundtrip.count == 8);
            for (int i = 0; i < roundtrip.count; ++i)
                assert(roundtrip.recent[i].slot == state.recent[i].slot && roundtrip.recent[i].used_at == state.recent[i].used_at && roundtrip.recent[i].opened);
        }
        // v2 retains every preset/custom color while enabling the new default.
        encoded[4] = 2; encoded[5] &= 3; legacy_slots(encoded); seal(encoded);
        roundtrip.menu_sound = false;
        assert(start_store_decode(encoded, sizeof encoded, roundtrip));
        assert(roundtrip.menu_sound && roundtrip.theme == proposal.theme && roundtrip.count == 8);
    }
    const StartMenuStore prior = loaded;
    // Every bit corrupted, every truncation, oversized and malformed fields reject
    // transactionally, without poisoning the caller's previous valid preferences.
    for (size_t byte = 0; byte < sizeof bytes; ++byte) {
        for (int bit = 0; bit < 8; ++bit) {
            bytes[byte] ^= uint8_t(1u << bit);
            assert(!start_store_decode(bytes, sizeof bytes, loaded));
            assert(start_store_encode(loaded, again));
            uint8_t want[START_STORE_BYTES]; assert(start_store_encode(prior, want));
            assert(!std::memcmp(again, want, sizeof again));
            bytes[byte] ^= uint8_t(1u << bit);
        }
    }
    for (size_t n = 0; n < sizeof bytes; ++n) assert(!start_store_decode(bytes, n, loaded));
    assert(!start_store_decode(bytes, sizeof bytes + 1, loaded));
    assert(!start_store_decode(nullptr, sizeof bytes, loaded));
    uint8_t invalid[START_STORE_BYTES];
    for (int which = 0; which < 7; ++which) {
        std::memcpy(invalid, bytes, sizeof invalid);
        if (which == 0) invalid[4] = 5;
        if (which == 1) invalid[5] |= 8;
        if (which == 2) invalid[6] = 9;
        if (which == 3) invalid[7] = 5;
        if (which == 4) { invalid[4] = 3; legacy_slots(invalid); invalid[8] = seance::PROJECT_SLOTS; }
        if (which == 5) invalid[8] = invalid[17];
        if (which == 6) invalid[83] = 0; // non-opaque custom colors rejected even when inactive
        seal(invalid);
        assert(!start_store_decode(invalid, sizeof invalid, loaded));
    }
    for (int bit = 3; bit < 8; ++bit) {
        std::memcpy(invalid, bytes, sizeof invalid); invalid[5] |= uint8_t(1u << bit); seal(invalid);
        assert(!start_store_decode(invalid, sizeof invalid, loaded));
    }
    start_store_forget(state, 3);
    assert(state.count == 7 && !state.menu_sound);
    for (int i = 0; i < 8; ++i) start_store_forget(state, i);
    assert(state.count == 0);
    assert(start_store_encode(state, bytes));
    bytes[9] = 1; seal(bytes);
    assert(!start_store_decode(bytes, sizeof bytes, loaded));
    bool valid[8] = {true, true, true, true, true, true, true, true};
    uint64_t times[8] = {10, 70, 70, 30, 20, 50, 40, 90};
    int recent[3];
    assert(start_store_recent(state, valid, times, 0, recent) == 3);
    assert(recent[0] == 0 && recent[1] == 7 && recent[2] == 1);
    start_store_touch(state, 3, 0); start_store_touch(state, 2, 0);
    assert(start_store_recent(state, valid, times, -1, recent) == 3);
    assert(recent[0] == 2 && recent[1] == 3 && recent[2] == 7);
    for (int mask = 0; mask < 256; ++mask) {
        int n = 0;
        for (int i = 0; i < 8; ++i) { valid[i] = (mask & (1 << i)) != 0; n += valid[i]; }
        const int count = start_store_recent(state, valid, times, -1, recent);
        assert(count == (n > 3 ? 3 : n));
        for (int i = 0; i < 3; ++i) {
            if (i >= count) assert(recent[i] == -1);
            else {
                assert(valid[recent[i]]);
                for (int j = 0; j < i; ++j) assert(recent[i] != recent[j]);
            }
        }
    }
    char age[16];
    struct Age { uint64_t when, now; const char* text; } cases[] = {
        {0, 1000, "--"}, {1001, 1000, "--"}, {1000, 1000, "now"},
        {1000, 1059, "now"}, {1000, 1060, "1 min"}, {1000, 4600, "1 hr"},
        {1000, 87400, "yesterday"}, {1000, 173800, "2 days"},
        {1, UINT64_MAX, "long ago"}, {UINT64_MAX, 1000, "--"}
    };
    for (const auto& c : cases) {
        start_store_age(c.when, c.now, age, sizeof age); assert(!std::strcmp(age, c.text));
        char tiny[2] = {'x','x'}; start_store_age(c.when, c.now, tiny, sizeof tiny); assert(tiny[1] == 0);
    }
    start_store_age(0, 0, nullptr, 0);
    char temp[] = "/tmp/seance-start-store-XXXXXX";
    assert(mkdtemp(temp));
    const std::string path = std::string(temp) + "/start_menu.bin";
    assert(start_store_save(path.c_str(), state));
    assert(start_store_load(path.c_str(), loaded));
    assert(loaded.count == 2 && loaded.recent[0].slot == 2 && !loaded.menu_sound);
    state.show_intro = true; state.menu_sound = true;
    assert(start_store_save(path.c_str(), state));
    assert(start_store_load((path + ".bak").c_str(), loaded) && !loaded.show_intro && !loaded.menu_sound);
    assert(start_store_load(path.c_str(), loaded) && loaded.show_intro && loaded.menu_sound);
    // Exact file length; trailing garbage is rejected.
    FILE* f = std::fopen(path.c_str(), "ab"); assert(f); std::fputc(1, f); std::fclose(f);
    assert(!start_store_load(path.c_str(), loaded));
    assert(start_store_load((path + ".bak").c_str(), loaded));
    assert(start_store_save(path.c_str(), state));
    assert(start_store_load(path.c_str(), loaded) && loaded.show_intro && loaded.menu_sound);
    assert(start_store_load((path + ".bak").c_str(), loaded) && !loaded.show_intro && !loaded.menu_sound);
    assert(!start_store_save((std::string(temp) + "/missing/file").c_str(), state));
    std::remove(path.c_str());
    assert(std::rename((path + ".bak").c_str(), path.c_str()) == 0);
    // A blocked temp path can't destroy the primary file.
    assert(mkdir((path + ".tmp").c_str(), 0700) == 0);
    assert(!start_store_save(path.c_str(), state));
    assert(start_store_load(path.c_str(), loaded));
    rmdir((path + ".tmp").c_str());
    std::remove(path.c_str());
    assert(mkdir(path.c_str(), 0700) == 0);
    assert(!start_store_save(path.c_str(), state));
    struct stat directory{}; assert(stat(path.c_str(), &directory) == 0 && S_ISDIR(directory.st_mode));
    rmdir(path.c_str()); rmdir(temp);
    std::puts("startup store: v1/v2/v3 migration, v4 known-open+sound+themes+custom, strict flags/corruptions+lengths, MRU256 masks, ages, replacement/backup/failure passed");
    return 0;
}
