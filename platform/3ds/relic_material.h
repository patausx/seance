#pragma once
// Detached lower material only. Surface is copied from RU::Visual AFTER its
// audio guard; no Engine, mechanics clock, gesture ownership or texture in Frame.
#include "../../core/engine/relic_types.h"
namespace seance::platform::ui3ds::relic_material {
constexpr int grid_x=12,grid_y=19,width=135,height=78;
constexpr unsigned max_spans=512; // leaves 512 native objects for unchanged chrome
struct Stats {
    unsigned meshes=0, recolors=0, uploads=0, spans=0;
    bool bitmap=false, portable=false, texture=false, geometry_rejected=false;
};
bool init(); // one optional 256x128 RGBA8 nearest texture, BEFORE audio starts
void shutdown(); // only after the final reader is fenced
void capture(const relic::Surface&,uint32_t instance); // CPU-only; no heap/trig/GPU
void upload(); // ONLY after successful FrameBegin(SYNCDRAW), before submission
// False means no admissible portable bitmap: caller may say unavailable, never
// fall back to flat triangles. Draw performs no upload/model/palette observations.
bool draw();
Stats stats();
// Focused read-only bitmap/binding boundary for renderer oracles (not Engine).
uint32_t pixel(unsigned x,unsigned y);
relic::Vertex bound_vertex(const relic::Surface&,unsigned sample);
} // namespace seance::platform::ui3ds::relic_material
