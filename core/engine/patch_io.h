// patch_io - binary save/load of the whole patch state.
// versioned SNC7..SNCM chunk container with 12-parameter banks.
// Actual/scene-only ROT34 selects SNCM22; sparse ROTSv1 entries:
// {u8 chain,slot,u32 seed,u16 passes,u8 mode} while every law is 0, else ROTSv2
// with a trailing u8 law (0..1) per entry. Required per actual card,
// never empty/orphan for scene-only. Source=CORE.extra0..63,flags0.
// Private tapes/masks/RNG/readers/artwork never serialize or clone.
// Actual/scene-only RULE33 requires SNCL21. Required sparse RULEv1 actual-card
// records: {u8 chain,slot,u32 seed,u16 seed_row,u8 phrase,source_root}, count1..24.
// Source slot uses CORE.extra (<64), flags0; no DSP/RNG/voice/visual persistence.
// No-RULE documents retain their former exact container and bytes.
// Actual or scene-only PENT32 requires SNCK20. PENTv1 is required per actual
// card: u8 version1/count1..24, then {chain,slot,width0..1}; extra/flags=0.
// Twelve bases/routes/cable disables use CORE/USIG. Runtime DSP/visual state
// never persists. Retained scene kinds count even when scene.valid is false.
// Relic31 remains reserved but unavailable. Actual/scene-only retired kinds or
// nonempty RELC/RLMO data refuse before factories/sample/live patch mutation.
// Existing files are untouched; no fallback. Non-Relic saves keep exact bytes.
// Nondefault clean GRAN reading/percentage requires SNCI18 + GRNX v1 and
// matching UNIT bit4; default SignedPitch/50 keeps former container bytes.
// PAUL requires SNCH17: all-forward v1 remains exact; any reverse/ping-pong
// card uses required PAUL v2 per-card mode/direction. DSP/loop leg stays runtime-only.
// Current INST and append-only SCRT29 require SNCG16; pre16 instances reject.
// SCRT config is required per card: v1 preset bytes, v2 independent axes.
// Latch stays runtime-only; files without SCRT retain their format.
// Other legacy SNC1-SNC6 modules retain their existing loader behavior.
// plain fopen/fwrite, no exceptions. load() parses transactionally into a staging
// Engine and swaps only after the complete file and sample manifest validate.
#pragma once
#include "../synth/sampler.h"
#include "workspace_layout.h"
#include <cstdio>

namespace seance {

class Engine;

namespace patchio {

enum class SaveFailure : uint8_t { None, Invalid, Create, Write, Publish };

// Heap-backed immutable save DTO. Construct it before taking the platform audio
// guard, capture the small persistent state while excluded, then perform all
// serialization and SD I/O from the document after releasing the guard.
class PatchDocument {
public:
    struct Impl;
    PatchDocument();
    ~PatchDocument();
    PatchDocument(const PatchDocument&) = delete;
    PatchDocument& operator=(const PatchDocument&) = delete;
    bool valid() const { return impl_ != nullptr; }
private:
    Impl* impl_ = nullptr;
    friend bool capture(const Engine&, PatchDocument&, const WorkspaceLayout*);
    friend bool write_document(const PatchDocument&, FILE*);
    friend bool save(const PatchDocument&, const char*);
    friend bool save(const PatchDocument&, const char*, SaveFailure*);
};

struct SampleManifest {
    static constexpr int SLOT_COUNT = 64;
    // SNC1-SNC4 predate persisted sample paths. Platform loaders may supply
    // their conventional s00..s15 fallback only when this remains false.
    bool has_sample_manifest = false;
    // Optional SEXT source policy, also implied by validated PEDT records.
    bool exact_native[SLOT_COUNT] = {};
    // Optional SNC7 CHOP chunk. Manual PLAY cuts are slot-shared sample metadata;
    // keeping them beside provenance makes save/reload behavior truthful.
    bool has_chops = false;
    uint32_t chops[SLOT_COUNT][trackr::synth::Sample::MAX_CHOPS] = {};
    // Optional SNC7 SREV chunk. A set bit reverses the interval beginning at
    // chops[slot][bit]; absent data defaults every slice to forward playback.
    bool has_slice_reverse = false;
    uint16_t slice_reverse[SLOT_COUNT] = {};
    // Optional PSPD, normalized 64..1024 q8. Absent => unity.
    bool has_slice_speed = false;
    uint16_t slice_speed_q8[SLOT_COUNT][trackr::synth::Sample::MAX_CHOPS] = {};
    // Optional SPITv1: independent signed musical semitones, absent => zero.
    bool has_slice_pitch = false;
    int8_t slice_pitch_semitones[SLOT_COUNT][trackr::synth::Sample::MAX_CHOPS] = {};
    struct Entry {
        unsigned char origin = 0;
        bool missing = false;
        char path[192] = {};
    } slots[SLOT_COUNT];
};

struct RestoredSamples {
    trackr::synth::Sample decoded[SampleManifest::SLOT_COUNT];
    bool ready[SampleManifest::SLOT_COUNT] = {};
};

// capture() performs no allocation and no file I/O. The destination must have
// been successfully constructed before entering the audio guard.
bool capture(const Engine& eng, PatchDocument& out, const WorkspaceLayout* workspace = nullptr);
// Writes an already captured document through an exclusive temporary sibling.
// Existing .tmp recovery files are retained; rename refusal leaves the target.
// Atomic publication is not an fsync/power-cut guarantee.
bool save(const PatchDocument& document, const char* path);
// Same, reporting which step refused. An existing project moves to `<path>.bak`
// before the new file takes its name (the SD card refuses rename-over-existing).
bool save(const PatchDocument& document, const char* path, SaveFailure* failure);
// Same exact SNC serializer, on a caller-owned seekable stream positioned at 0.
// Does not close, flush or publish. Recovery adds its envelope after these bytes.
bool write_document(const PatchDocument& document, FILE* stream);
// Convenience for single-threaded/offline callers. Realtime controllers should
// use capture() + save(PatchDocument, path) so I/O never runs under their guard.
bool save(const Engine& eng, const char* path);
bool load(Engine& eng, const char* path, SampleManifest* manifest = nullptr,
          WorkspaceLayout* workspace = nullptr);
// Decode performs file I/O and allocation without touching the live SampleBank.
// commit_restored_samples must run while the audio engine is excluded.
int decode_samples(SampleManifest& manifest, int target_sr, int max_frames, RestoredSamples& out);
// Returns false without mutation when the staged aggregate exceeds the bank budget.
// Optional hydrated engine receives exact PLAY source binding and SCRT runtime
// invalidation only after the bank swap. SCRT/GRAN share the committed slot bank;
// missing sources become empty rather than borrowing PCM from a previous patch.
// PLAY's pending metadata remains available for a safe later resave.
bool commit_restored_samples(const SampleManifest& manifest, RestoredSamples& decoded,
                             Engine* hydrated = nullptr, Engine* live_before_swap = nullptr);
// Convenience for single-threaded/offline users.
int restore_samples(SampleManifest& manifest, int target_sr, int max_frames,
                    Engine* hydrated = nullptr);

} // namespace patchio
} // namespace seance
