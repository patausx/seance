#pragma once
#include "engine.h"
namespace seance::padio {
// Standalone portable macro bank. Transactional load; fixed bounded records.
bool save(const MacroPad pads[8], const char* path);
bool load(MacroPad pads[8], const char* path);
}
