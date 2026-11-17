// Embedded demo projects: sample-free SNC files exactly as saved on the owner's console
// (2026-11-15), under the titles they have there. Origins in experiments/full_tracks:
// icytype = ghost_arithmetic, noise = burnt_offering, some chiiiiptune = blast_furnace,
// smth = moth_hours edited on the console, dambient = black_water.
#include <3ds.h>
#include "demo_projects.h"
#include "demo_projects_seed.h"
#include "demo_icytype_bin.h"
#include "demo_noise_bin.h"
#include "demo_some_chiiiiptune_bin.h"
#include "demo_smth_bin.h"
#include "demo_dambient_bin.h"

namespace seance::platform::ui3ds {
DemoSeedResult seed_builtin_demo_projects(const char* dir, const bool (&exists)[PROJECT_SLOTS],
                                          ProjectNames& names) {
    const DemoProject demos[] = {
        {"icytype", demo_icytype_bin, size_t(demo_icytype_bin_size)},
        {"noise", demo_noise_bin, size_t(demo_noise_bin_size)},
        {"some chiiiiptune", demo_some_chiiiiptune_bin, size_t(demo_some_chiiiiptune_bin_size)},
        {"smth", demo_smth_bin, size_t(demo_smth_bin_size)},
        {"dambient", demo_dambient_bin, size_t(demo_dambient_bin_size)},
    };
    static_assert(sizeof demos / sizeof demos[0] <= kMaxDemoProjects, "raise kMaxDemoProjects");
    return demo_projects_seed(dir, demos, int(sizeof demos / sizeof demos[0]), exists, names);
}
} // namespace seance::platform::ui3ds
