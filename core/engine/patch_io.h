// patch_io - binary save/load of the whole patch state.
// versioned SNC7..SNCI chunk container with 12-parameter banks.
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

namespace seance {

class Engine;

namespace patchio {

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
    friend bool save(const PatchDocument&, const char*);
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
// Writes an already captured document atomically through path.tmp.
bool save(const PatchDocument& document, const char* path);
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
                             Engine* hydrated = nullptr);
// Convenience for single-threaded/offline users.
int restore_samples(SampleManifest& manifest, int target_sr, int max_frames,
                    Engine* hydrated = nullptr);

} // namespace patchio
} // namespace seance
