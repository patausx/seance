#pragma once
#include "patch_io.h"
#include <cstdint>

namespace seance::recovery {
// Separate bounded namespace. SNC prefix is exactly the manual serializer's
// payload; an unknown RCV1 chunk authenticates length/metadata/bytes, not PCM.
constexpr uint32_t MAX_PAYLOAD = 1024 * 1024;
struct Record {
    bool exists = false, valid = false, disposed = false;
    uint64_t generation = 0, epoch = 0;
    uint32_t revision = 0, length = 0, checksum = 0;
};
struct Inventory {
    Record records[2];
    int latest = -1;
    bool blocked = false; // foreign/corrupt/unreadable: never cleanup authority
    bool offered() const { return latest >= 0 && !records[latest].disposed; }
};
class Store {
public:
    explicit Store(const char* directory);
    Inventory inspect() const;
    bool candidate_path(const Inventory&, char* out, unsigned capacity) const;
    bool checkpoint(const patchio::PatchDocument&, uint64_t epoch, uint32_t revision, Record& committed);
    // Conditional durable tombstone; never clears an unrelated/newer record.
    bool dispose(uint64_t covered_generation);
private:
    char directory_[192] = {};
    bool path(int slot, char* out) const;
    bool publish(const patchio::PatchDocument*, uint64_t epoch, uint32_t revision,
                 uint64_t expected_generation, Record& committed);
};

struct State {
    uint32_t revision = 0;
    uint64_t random_revision = 0;
    bool dirty = false, quiet = false, stopped = false, safe = false;
};
class Controller {
public:
    static constexpr uint64_t INTERVAL_MS = 30000, IDLE_MS = 5000, BACKOFF_MS = 60000;
    void boot(const Inventory&, uint64_t now);
    bool unresolved() const { return unresolved_; }
    bool disabled() const { return disabled_; }
    uint64_t epoch() const { return epoch_; }
    uint64_t covered_generation() const { return covered_; }
    // Called only after a successful actual new/load. Cancellation/failed load
    // never calls this; caller must durably dispose owned coverage first.
    void context_changed(uint64_t now, bool disposition_ok);
    void restored(const Record&, uint32_t revision, uint64_t now);
    bool due(uint64_t now, const State&);
    void attempted(uint64_t now, const State& captured, bool ok, const Record&);
    void retired(bool ok);
private:
    uint64_t epoch_ = 0, last_attempt_ = 0, last_activity_ = 0, retry_after_ = 0;
    uint64_t covered_ = 0, observed_random_ = 0;
    uint32_t observed_revision_ = 0, captured_revision_ = 0;
    bool unresolved_ = false, disabled_ = false;
};
} // namespace seance::recovery
