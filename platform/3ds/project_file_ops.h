#pragma once
// PATCHES controller I/O only. No Engine, sample bank, draw or audio guard here.
#include "../../core/io/file_publication.h"
#include <cstdint>
#include <sys/stat.h>

namespace seance::platform::ui3ds::project_files {

// FAT's coarse times alone cannot identify a saved revision. Destructive choices
// also compare the complete file's read-only digest, on the same descriptor.
// This is conservative revalidation, not atomic unlink-by-inode / hostile-FS proof.
struct Snapshot {
    struct stat identity{};
    uint64_t digest = 0;
    bool ready = false;
};
enum class Result { Ok, Occupied, Changed, Error };
// UI work bound, not a loader/file-integrity guarantee. Current containers fit
// within this ceiling; a hostile oversized .snc cannot stall two full digest reads.
constexpr uint64_t kMaxSavedBytes = 8u * 1024u * 1024u;
bool inspect(const char* path, Snapshot& out);
bool matches(const char* path, const Snapshot& expected);
Result remove_saved(const char* path, const Snapshot& expected);
Result copy_saved(const char* source, const char* destination);

// The unchanged SNC writer owns its staging descriptor. This extra token reserves
// only a NEW final name before calling it; a late REC/rand refusal must NEVER
// delete an SNC that was already published. Only our unchanged empty placeholder
// may be cleaned up. Existing files, .tmp/.bak siblings and WAVs stay untouched.
class VacantTarget {
public:
    ~VacantTarget();
    fileio::ReserveResult reserve(const char* path);
    bool unchanged() const;
    bool written() const;
    bool release_for_publication(); // remove only our checked empty placeholder, before no-replace publication
    void settle();
private:
    fileio::OwnedFile final_;
    struct stat placeholder_{};
    bool reserved_ = false;
};

} // namespace seance::platform::ui3ds::project_files
