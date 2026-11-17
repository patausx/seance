#pragma once
#include "demo_projects_seed.h"

namespace seance::platform::ui3ds {
// Copies the embedded demos into free slots of `dir` once (see demo_projects_seed.h).
DemoSeedResult seed_builtin_demo_projects(const char* dir, const bool (&exists)[PROJECT_SLOTS],
                                          ProjectNames& names);
} // namespace seance::platform::ui3ds
