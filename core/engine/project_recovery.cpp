#include "project_recovery.h"
#include "../io/file_publication.h"
#include <algorithm>
#ifdef __3DS__
#include <3ds.h>
#endif

namespace seance::recovery {
namespace {
constexpr unsigned FOOTER = 48;
uint64_t get(const unsigned char* p, int n) {
    uint64_t v = 0; for (int i = 0; i < n; ++i) v |= uint64_t(p[i]) << (8*i); return v;
}
void put(unsigned char* p, uint64_t v, int n) {
    for (int i = 0; i < n; ++i) p[i] = uint8_t(v >> (8*i));
}
uint32_t hash(uint32_t h, const unsigned char* p, unsigned n) {
    for (unsigned i = 0; i < n; ++i) { h = (h ^ p[i]) * 16777619u; } return h;
}
bool prefix_hash(const char* path, uint32_t length, uint32_t& sum) {
    FILE* f = std::fopen(path, "rb"); if (!f) return false;
    unsigned char buffer[1024]; bool ok = true; sum = 2166136261u;
    while (length && ok) {
        const unsigned n = std::min<uint32_t>(length, sizeof buffer);
        ok = std::fread(buffer, 1, n, f) == n;
        if (ok) { sum = hash(sum, buffer, n); length -= n; }
    }
    ok = ok && !std::ferror(f);
    const bool closed = std::fclose(f) == 0;
    return ok && closed;
}
// False = I/O/foreign/corrupt, not absence. No invalid bytes grant ownership.
bool read_record(const char* path, Record& r) {
    r = {}; bool exists = false;
    if (!fileio::regular_or_absent(path, exists)) return false;
    r.exists = exists; if (!exists) return true;
    FILE* f = std::fopen(path, "rb"); if (!f) return false;
    unsigned char tail[FOOTER] = {}, magic[5] = {};
    bool ok = std::fseek(f, 0, SEEK_END) == 0;
    const long size = ok ? std::ftell(f) : -1;
    ok = ok && size >= long(FOOTER) && size <= long(MAX_PAYLOAD + FOOTER) &&
        std::fseek(f, size - FOOTER, SEEK_SET) == 0 && std::fread(tail, 1, FOOTER, f) == FOOTER;
    if (ok) {
        r.generation = get(tail+16,8); r.epoch = get(tail+24,8);
        r.revision = uint32_t(get(tail+32,4)); r.length = uint32_t(get(tail+36,4));
        r.checksum = uint32_t(get(tail+40,4)); r.disposed = tail[13] == 1;
        ok = !std::memcmp(tail,"RCV1",4) && get(tail+4,4) == 40 &&
            !std::memcmp(tail+8,"SNRA",4) && tail[12] == 1 && tail[13] <= 1 &&
            !get(tail+14,2) && !get(tail+44,4) && r.generation && r.epoch &&
            r.length == uint32_t(size - FOOTER) &&
            (r.disposed ? r.length == 0 : r.length >= 5 && r.revision != 0);
        if (ok && !r.disposed) {
            ok = std::fseek(f,0,SEEK_SET) == 0 && std::fread(magic,1,5,f) == 5 &&
                !std::memcmp(magic,"SNC",3) && magic[4] >= 7 && magic[4] <= 22 &&
                magic[3] == (magic[4] < 10 ? '0'+magic[4] : 'A'+magic[4]-10);
        }
    }
    ok = ok && !std::ferror(f);
    const bool closed = std::fclose(f) == 0;
    uint32_t sum = 0;
    if (!ok || !closed || !prefix_hash(path,r.length,sum)) return false;
    std::memset(tail+40,0,4);
    r.valid = hash(sum,tail,FOOTER) == r.checksum;
    return r.valid;
}
bool same(const Record& a,const Record& b) {
    return a.exists == b.exists && a.valid == b.valid && a.disposed == b.disposed &&
        a.generation == b.generation && a.epoch == b.epoch && a.revision == b.revision &&
        a.length == b.length && a.checksum == b.checksum;
}
bool vacant_publish(fileio::OwnedFile& file,const char* destination) {
    if (!fileio::absent(destination)) return false;
#ifdef __3DS__
    if (std::strncmp(destination,"sdmc:/",6) || std::strncmp(file.path(),"sdmc:/",6)) return false;
    FS_Archive archive;
    if (R_FAILED(FSUSER_OpenArchive(&archive,ARCHIVE_SDMC,fsMakePath(PATH_EMPTY,"")))) return false;
    const Result result = FSUSER_RenameFile(archive,fsMakePath(PATH_ASCII,file.path()+5),
                                          archive,fsMakePath(PATH_ASCII,destination+5));
    (void)FSUSER_CloseArchive(archive);
    if (R_FAILED(result)) return false;
    file.retain(); return true;
#else
    return file.publish(destination); // serialized app directory, not hostile pathname races
#endif
}
}
Store::Store(const char* directory) {
    if (directory && *directory && std::strlen(directory) < sizeof directory_)
        std::strcpy(directory_,directory);
}
bool Store::path(int slot,char* out) const {
    return directory_[0] && std::snprintf(out,256,"%s/record%d.snr",directory_,slot) < 256;
}
Inventory Store::inspect() const {
    Inventory out;
    struct stat directory{};
    // Missing/unmounted namespace is not evidence that previous boot was empty.
    if (!directory_[0] || ::stat(directory_,&directory)!=0 || !S_ISDIR(directory.st_mode)) {
        out.blocked=true;return out;
    }
    for (int i=0;i<2;++i) {
        char p[256];
        if (!path(i,p) || !read_record(p,out.records[i])) out.blocked = true;
        if (out.records[i].valid && (out.latest < 0 ||
            out.records[i].generation > out.records[out.latest].generation)) out.latest = i;
    }
    if (out.records[0].valid && out.records[1].valid &&
        out.records[0].generation == out.records[1].generation) { out.latest = -1; out.blocked = true; }
    return out;
}
bool Store::candidate_path(const Inventory& inventory,char* out,unsigned capacity) const {
    char p[256];
    if (!inventory.offered() || !path(inventory.latest,p) || std::strlen(p) >= capacity) return false;
    Record check;
    if (!read_record(p,check) || !same(check,inventory.records[inventory.latest])) return false;
    std::strcpy(out,p); return true;
}
bool Store::checkpoint(const patchio::PatchDocument& document,uint64_t epoch,uint32_t revision,Record& committed) {
    return publish(&document,epoch,revision,0,committed);
}
bool Store::dispose(uint64_t generation) {
    const Inventory before = inspect();
    if (before.blocked || before.latest < 0) return false;
    const Record& latest=before.records[before.latest];
    if (latest.disposed) {
        // Retry after uncertain final readback: acknowledge only the exact next
        // tombstone plus its still-present covered snapshot, not an unrelated
        // later context/disposition. No new I/O publication or cleanup needed.
        const Record& covered=before.records[1-before.latest];
        return generation != UINT64_MAX && latest.generation == generation+1 &&
            covered.valid && !covered.disposed && covered.generation == generation &&
            covered.epoch == latest.epoch;
    }
    if (latest.generation != generation) return false;
    Record committed;
    return publish(nullptr,before.records[before.latest].epoch,0,generation,committed);
}
bool Store::publish(const patchio::PatchDocument* document,uint64_t epoch,uint32_t revision,
                    uint64_t expected_generation,Record& committed) {
    committed = {};
    if (!epoch || (document && !revision)) return false;
    const Inventory before = inspect(); if (before.blocked) return false;
    const uint64_t generation = before.latest < 0 ? 0 : before.records[before.latest].generation;
    if (generation == UINT64_MAX || (expected_generation && generation != expected_generation)) return false;
    const int target = before.latest < 0 ? 0 : 1-before.latest;
    char destination[256]; if (!path(target,destination)) return false;
    fileio::OwnedFile temporary;
    // Four fixed staging names per slot. Foreign/previous-crash staging is never
    // reclaimed; exhaustion disables writes rather than growing generations.
    bool reserved = false;
    for (int i=0;i<4;++i) {
        char p[256]; const int n = std::snprintf(p,sizeof p,"%s.stage%d",destination,i);
        if (n<0 || unsigned(n)>=sizeof p) return false;
        bool exists=false; if (!fileio::regular_or_absent(p,exists)) return false;
        if (exists) continue;
        reserved = temporary.create(p) == fileio::ReserveResult::Ok; break;
    }
    if (!reserved) return false;
    FILE* f = temporary.stream();
    if (document && !patchio::write_document(*document,f)) return false;
    const long size = std::ftell(f);
    if (size<0 || size>long(MAX_PAYLOAD) || std::fflush(f)!=0) return false;
    unsigned char tail[FOOTER] = {};
    std::memcpy(tail,"RCV1",4); put(tail+4,40,4); std::memcpy(tail+8,"SNRA",4);
    tail[12]=1; tail[13]=document?0:1;
    put(tail+16,generation+1,8); put(tail+24,epoch,8);
    put(tail+32,revision,4); put(tail+36,uint32_t(size),4);
    uint32_t sum=0;
    if (!prefix_hash(temporary.path(),uint32_t(size),sum)) return false;
    put(tail+40,hash(sum,tail,FOOTER),4);
    const bool wrote = std::fwrite(tail,1,FOOTER,f)==FOOTER;
    const bool flushed = std::fflush(f)==0;
    const bool synced = flushed && ::fsync(::fileno(f))==0;
    const bool closed = temporary.close(false);
    Record staged;
    if (!wrote || !flushed || !synced || !closed || !read_record(temporary.path(),staged) || !staged.valid ||
        staged.generation != generation+1 || staged.epoch != epoch || staged.revision != revision ||
        staged.length != uint32_t(size) || staged.checksum != get(tail+40,4)) return false;
    const Inventory check = inspect();
    if (check.blocked || check.latest != before.latest ||
        !same(check.records[0],before.records[0]) || !same(check.records[1],before.records[1])) return false;
    // Only the verified older committed record is reclaimable. Newest survives
    // remove/rename/readback failures; unknown data never enters this branch.
    if (before.records[target].exists && std::remove(destination)!=0) return false;
    const bool published = vacant_publish(temporary,destination);
    Record final;
    const bool exact = read_record(destination,final) && same(final,staged);
    if (!published && (!fileio::absent(temporary.path()) || !exact)) return false;
    if (!exact) return false; // uncertain actual commit retained, never deleted
    committed = final; return true;
}
void Controller::boot(const Inventory& inventory,uint64_t now) {
    *this = Controller{};
    if (inventory.latest>=0) epoch_ = inventory.records[inventory.latest].epoch;
    unresolved_ = inventory.offered() || inventory.blocked;
    disabled_ = inventory.blocked;
    context_changed(now,true);
}
void Controller::context_changed(uint64_t now,bool disposition_ok) {
    if (epoch_ == UINT64_MAX) disabled_=true; else ++epoch_;
    if (!disposition_ok) disabled_=true;
    covered_=0; observed_revision_=captured_revision_=0; observed_random_=0;
    last_attempt_=last_activity_=now; retry_after_=0;
}
void Controller::restored(const Record& record,uint32_t revision,uint64_t now) {
    unresolved_=false;
    covered_=record.generation; captured_revision_=revision;
    observed_revision_=revision; last_activity_=last_attempt_=now;
}
bool Controller::due(uint64_t now,const State& state) {
    if (now<last_attempt_ || now<last_activity_) {last_attempt_=last_activity_=now;return false;}
    if (!state.quiet || !state.stopped || !state.safe || state.revision!=observed_revision_ ||
        state.random_revision!=observed_random_) last_activity_=now;
    observed_revision_=state.revision; observed_random_=state.random_revision;
    return !unresolved_ && !disabled_ && state.dirty && state.quiet && state.stopped && state.safe &&
        state.revision!=captured_revision_ && now>=retry_after_ &&
        now-last_attempt_>=INTERVAL_MS && now-last_activity_>=IDLE_MS;
}
void Controller::attempted(uint64_t now,const State& captured,bool ok,const Record& record) {
    last_attempt_=now;
    if (ok) {covered_=record.generation;captured_revision_=captured.revision;retry_after_=0;}
    else retry_after_=now>UINT64_MAX-BACKOFF_MS?UINT64_MAX:now+BACKOFF_MS;
}
void Controller::retired(bool ok) {
    if (ok) {covered_=0;captured_revision_=0;} else disabled_=true;
}
} // namespace seance::recovery
