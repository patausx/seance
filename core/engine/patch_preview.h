#pragma once
#include <cstdint>

namespace seance::patchio {

enum class PreviewStatus : uint8_t { Unavailable, Ready, Unsupported, Invalid };

struct PatchPreview {
    PreviewStatus status = PreviewStatus::Unavailable;
    uint8_t version = 0;
    uint8_t kinds[4][6] = {};
};

// Read-only saved chain metadata, NOT load/admissibility or sample/file integrity
// validation. SNC1..6 use their fixed six-param prefix; SNC7..SNCM22 accept the
// exact historical eight-param or current twelve-param CORE shape. Empty slots
// remain zero, including holes. Only currently available UnitKind ids are shown.
//
// No allocation, Engine, factories, manifest, decode, or process-wide cache.
// Controller I/O only: call on selection/explicit cache invalidation, not in a
// draw/sample loop or under the audio guard. The caller owns its cache and path.
//
// Returns true iff Ready. Every failure clears ALL kinds. Unavailable means no
// path/open; Unsupported means an unknown version or unavailable kind; Invalid
// means a malformed metadata envelope/shape, exhausted scan budget, or I/O/close
// failure. version is diagnostic: zero until a complete SNC header is read.
// Chunked files check at most 64 headers, physical payload bounds and duplicate
// CORE; other payload semantics, required chunks and scene kinds are not checked.
// Concurrent same-size rewrites are not an atomic filesystem snapshot.
bool read_preview(const char* path, PatchPreview& out);

} // namespace seance::patchio
