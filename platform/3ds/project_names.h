#pragma once
// App-owned title metadata only; never modifies SNC project files.
#include "../../core/engine/project_library.h"
#include <cstddef>

namespace seance::platform::ui3ds {
struct ProjectNames {
    char names[PROJECT_SLOTS][PROJECT_NAME_BYTES] = {};
};
// Exact version/length/checksum/canonical strings; failure leaves out unchanged.
// The caller explicitly tries path + ".bak" when primary recovery is needed.
bool project_names_load(const char* path, ProjectNames& out);
// Staged write/readback, previous valid primary -> .bak, commit with rollback.
// A corrupt primary never replaces a verified backup. No filesystem work in draw.
bool project_names_save(const char* path, const ProjectNames& names);
// Trim outer ASCII spaces, lowercase A-Z; preserve internal printable ASCII.
// Reject empty/non-ASCII/control/over-23-character names without mutation.
bool project_name_set(ProjectNames& names, int slot, const char* name);
void project_name_clear(ProjectNames& names, int slot);
// Unset title -> "patch 001" ... "patch 128"; invalid slot -> "unsaved".
void project_name_label(const ProjectNames& names, int slot, char* out, size_t size);
} // namespace seance::platform::ui3ds
