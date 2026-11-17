#pragma once
// Built-in demo projects: copied ONCE into free project slots on first boot.
// Never overwrites a file, never touches an occupied slot, never reruns after the
// marker exists (deleting a demo keeps it deleted). Boot controller only, no audio.
#include "project_names.h"
#include "../../core/io/file_publication.h"
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace seance::platform::ui3ds {
struct DemoProject {
    const char* name;      // canonical title (lowercase printable ASCII)
    const uint8_t* data;   // exact SNC bytes
    size_t size;
};
constexpr int kMaxDemoProjects = 8;
struct DemoSeedResult {
    int slots[kMaxDemoProjects] = {-1, -1, -1, -1, -1, -1, -1, -1}; // newly published slots
    int count = 0;
    bool marked = false;   // marker written: later boots skip seeding entirely
};

inline bool demo_write_vacant(const char* path, const uint8_t* data, size_t size) {
    fileio::OwnedFile file;
    if (!fileio::create_temporary(file, path)) return false;
    const bool written = std::fwrite(data, 1, size, file.stream()) == size;
    if (!file.close(true) || !written) { file.discard(); return false; }
    if (!file.publish_vacant(path)) { file.discard(); return false; }
    return true;
}

// [STORAGE] demo projects seeded once into free slots with names and a marker
// `exists` is the boot catalogue (unknown counts as occupied). A demo whose title
// already names an existing slot counts as present. The marker is written only
// when every demo is present, published or has no free slot left.
inline DemoSeedResult demo_projects_seed(const char* dir, const DemoProject* demos, int count,
                                         const bool (&exists)[PROJECT_SLOTS], ProjectNames& names) {
    DemoSeedResult result;
    char marker[96];
    if (!dir || std::snprintf(marker, sizeof marker, "%s/demo_projects.bin", dir) >= int(sizeof marker))
        return result;
    if (!fileio::absent(marker)) return result; // done before, or unreadable: do nothing
    bool taken[PROJECT_SLOTS];
    std::memcpy(taken, exists, sizeof taken);
    bool complete = true;
    for (int d = 0; d < count && d < kMaxDemoProjects; ++d) {
        bool present = false;
        for (int s = 0; s < PROJECT_SLOTS && !present; ++s)
            present = taken[s] && !std::strncmp(names.names[s], demos[d].name, PROJECT_NAME_BYTES);
        if (present) continue;
        int slot = -1;
        for (int s = 0; s < PROJECT_SLOTS && slot < 0; ++s) if (!taken[s]) slot = s;
        if (slot < 0) continue; // card full: skip quietly, never replace
        char path[96];
        std::snprintf(path, sizeof path, "%s/patch%d.snc", dir, slot);
        if (!demo_write_vacant(path, demos[d].data, demos[d].size)) { complete = false; continue; }
        taken[slot] = true;
        (void)project_name_set(names, slot, demos[d].name);
        result.slots[result.count++] = slot;
    }
    static const uint8_t kMarker[5] = {'S', 'N', 'D', 'M', 1};
    if (complete) result.marked = demo_write_vacant(marker, kMarker, sizeof kMarker);
    return result;
}
} // namespace seance::platform::ui3ds
