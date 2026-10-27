// Narrow standalone host test; no engine/audio/UI dependencies.
// Build with C++17 and -fsanitize=address,undefined -fno-omit-frame-pointer.
#include "../platform/3ds/sample_browser_store.h"
#include <array>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>
#include <sys/resource.h>
#include <unistd.h>

namespace store = seance::platform::ui3ds::sample_browser_store;
namespace fs = std::filesystem;
static int checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); std::abort(); } } while (0)
constexpr size_t header_bytes = 16, record_bytes = 193, file_bytes = 12372;
using Bytes = std::vector<uint8_t>;
using Image = std::array<uint8_t, sizeof(store::State)>;

static Image image(const store::State& state) {
    Image result;
    std::memcpy(result.data(), &state, result.size());
    return result;
}
static Bytes read_file(const std::string& path) {
    FILE* file = std::fopen(path.c_str(), "rb"); CHECK(file);
    CHECK(std::fseek(file, 0, SEEK_END) == 0);
    const long size = std::ftell(file); CHECK(size >= 0);
    CHECK(std::fseek(file, 0, SEEK_SET) == 0);
    Bytes bytes(static_cast<size_t>(size));
    CHECK(std::fread(bytes.data(), 1, bytes.size(), file) == bytes.size());
    CHECK(std::fclose(file) == 0);
    return bytes;
}
static void write_file(const std::string& path, const Bytes& bytes) {
    FILE* file = std::fopen(path.c_str(), "wb"); CHECK(file);
    CHECK(std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size());
    CHECK(std::fclose(file) == 0);
}
static uint32_t crc32(const uint8_t* bytes, size_t size) {
    uint32_t crc = ~uint32_t(0);
    for (size_t i = 0; i < size; ++i) {
        crc ^= bytes[i];
        for (int j = 0; j < 8; ++j) crc = (crc & 1) ? (crc >> 1) ^ 0xedb88320u : crc >> 1;
    }
    return ~crc;
}
static void seal(Bytes& bytes) {
    CHECK(bytes.size() == file_bytes);
    const uint32_t crc = crc32(bytes.data(), bytes.size() - 4);
    for (int i = 0; i < 4; ++i) bytes[bytes.size() - 4 + i] = uint8_t(crc >> (8 * i));
}
struct Fixture {
    std::string root;
    Fixture() {
        char path[] = "/tmp/seance-browser-store-XXXXXX";
        const char* made = ::mkdtemp(path); CHECK(made); root = made;
    }
    ~Fixture() { fs::remove_all(root); } // only this test's exclusively-created directory
    std::string path(const char* suffix) const { return root + "/" + suffix; }
};
static void same(const store::State& a, const store::State& b) {
    CHECK(a.count == b.count && a.auto_preview == b.auto_preview);
    for (int i = 0; i < a.count; ++i) {
        CHECK(a.entries[i].directory == b.entries[i].directory);
        CHECK(std::strcmp(a.entries[i].path, b.entries[i].path) == 0);
    }
}
static void roundtrip(const Fixture& fixture) {
    store::State state, out;
    CHECK(crc32(reinterpret_cast<const uint8_t*>("123456789"), 9) == 0xcbf43926u);
    CHECK(store::kCapacity == 64 && store::kPath == 192);
    const std::string path = fixture.path("favorites.bin");
    CHECK(store::save(path.c_str(), state));
    CHECK(read_file(path).size() == file_bytes);
    CHECK(store::load(path.c_str(), out)); same(state, out);
    CHECK(store::toggle(state, "/", true));
    CHECK(store::toggle(state, "sdmc:/", true));
    CHECK(store::toggle(state, "sdmc:/samples/Drums", true));
    CHECK(store::toggle(state, "/nonexistent/кик 01.WAV", false));
    const std::string longest = "/" + std::string(190, 'a');
    CHECK(store::toggle(state, longest.c_str(), false));
    state.auto_preview = false;
    CHECK(store::save(path.c_str(), state)); // replacement must work on host
    CHECK(store::load(path.c_str(), out)); same(state, out);
    CHECK(store::find(out, "/nonexistent/кик 01.WAV", false) == 3); // stale is retained
    const Bytes bytes = read_file(path);
    CHECK(bytes.size() == file_bytes && bytes[8] == 1 && bytes[10] == 5 && bytes[12] == 0);
    CHECK(std::memcmp(bytes.data(), "SNCFAVS\0", 8) == 0);
    for (int i = out.count; i < store::kCapacity; ++i) {
        CHECK(!out.entries[i].directory);
        for (char c : out.entries[i].path) CHECK(c == 0);
    }
    const Image before = image(out);
    CHECK(!store::load(fixture.path("missing.bin").c_str(), out));
    CHECK(image(out) == before);
    CHECK(!store::load(nullptr, out) && image(out) == before);
    CHECK(!store::save(nullptr, out) && image(out) == before);
    std::puts("roundtrip/default/stale: pass");
}
static void paths_and_capacity() {
    store::State state;
    const char* rejected[] = {"", "relative/a.wav", "sdmc:", "sdmc:a.wav", "SDMC:/a", "//a", "/a/", "/a//b", "/./a", "/a/./b", "/../a", "/a/..", "/a/../b", "sdmc://a", "sdmc:/a/", "sdmc:/../a", "/a\\b", "/a:b", "/a\nb", "/a\x7f"};
    for (const char* path : rejected) {
        const Image before = image(state);
        CHECK(!store::toggle(state, path, false));
        CHECK(!store::toggle(state, path, true));
        CHECK(store::find(state, path, false) == -1);
        CHECK(image(state) == before);
    }
    char unterminated[store::kPath]; std::memset(unterminated, 'a', sizeof unterminated);
    unterminated[0] = '/';
    CHECK(!store::toggle(state, unterminated, false));
    CHECK(!store::toggle(state, nullptr, false));
    CHECK(!store::toggle(state, "/", false));
    CHECK(!store::toggle(state, "sdmc:/", false));
    CHECK(store::toggle(state, "/a/.hidden", false));
    CHECK(store::toggle(state, "/a/..allowed", false));
    CHECK(store::toggle(state, "/Case.wav", false));
    CHECK(store::find(state, "/case.wav", false) == -1);
    CHECK(store::find(state, "/Case.wav", true) == -1);
    const Image before = image(state);
    CHECK(!store::toggle(state, "/Case.wav", true));
    CHECK(image(state) == before);
    CHECK(store::toggle(state, "/a/.hidden", false));
    CHECK(state.count == 2 && store::find(state, "/Case.wav", false) == 1);
    CHECK(state.entries[2].path[0] == 0 && !state.entries[2].directory);
    // Exercise aliases inside the State, including a previously unused entry.
    std::strcpy(state.entries[state.count].path, "/aliased.wav");
    CHECK(store::toggle(state, state.entries[state.count].path, false));
    CHECK(store::find(state, "/aliased.wav", false) == 2);
    CHECK(store::toggle(state, state.entries[0].path, false));
    while (state.count < store::kCapacity) {
        const std::string path = "/sample-" + std::to_string(state.count) + ".wav";
        CHECK(store::toggle(state, path.c_str(), false));
    }
    const Image full = image(state);
    CHECK(!store::toggle(state, "/overflow.wav", false) && image(state) == full);
    CHECK(store::toggle(state, state.entries[32].path, false));
    CHECK(state.count == 63);
    CHECK(store::toggle(state, "/new.wav", false) && state.count == 64);
    CHECK(store::find(state, "/new.wav", false) == 63);
    std::puts("paths/toggle/capacity: pass");
}
static void malformed(const Fixture& fixture) {
    store::State state, destination;
    CHECK(store::toggle(state, "/one.wav", false));
    CHECK(store::toggle(state, "/folder", true));
    CHECK(store::toggle(destination, "/keep-this.wav", false));
    destination.auto_preview = false;
    const Image before = image(destination);
    const std::string good_path = fixture.path("good.bin"), bad_path = fixture.path("bad.bin");
    CHECK(store::save(good_path.c_str(), state));
    const Bytes good = read_file(good_path);
    auto reject = [&](Bytes bytes, bool checksum = true) {
        if (checksum) seal(bytes);
        write_file(bad_path, bytes);
        CHECK(!store::load(bad_path.c_str(), destination));
        CHECK(image(destination) == before);
    };
    for (size_t n : {size_t(0), size_t(15), size_t(16), size_t(208), file_bytes - 1})
        reject(Bytes(good.begin(), good.begin() + n), false);
    Bytes bytes = good; bytes.push_back(0); reject(bytes, false);
    bytes = good; bytes.back() ^= 1; reject(bytes, false);
    bytes = good; bytes[header_bytes + 2] ^= 1; reject(bytes, false);
    for (size_t index : {size_t(0), size_t(8), size_t(9), size_t(11), size_t(13), size_t(14), size_t(15)}) {
        bytes = good; bytes[index] ^= 1; reject(bytes);
    }
    for (uint8_t count : {uint8_t(0), uint8_t(1), uint8_t(3), uint8_t(65), uint8_t(255)}) {
        bytes = good; bytes[10] = count; reject(bytes);
    }
    bytes = good; bytes[12] = 2; reject(bytes); // boolean
    bytes = good; bytes[header_bytes] = 2; reject(bytes); // directory boolean
    bytes = good; bytes[header_bytes + 100] = 'x'; reject(bytes); // noncanonical NUL tail
    bytes = good; std::memset(bytes.data() + header_bytes + 1, 'a', store::kPath); reject(bytes);
    bytes = good; bytes[header_bytes + record_bytes * 2] = 1; reject(bytes); // unused flag
    bytes = good; bytes[header_bytes + record_bytes * 2 + 1] = 'x'; reject(bytes); // unused path
    for (bool different_type : {false, true}) {
        bytes = good;
        std::memcpy(bytes.data() + header_bytes + record_bytes, bytes.data() + header_bytes, record_bytes);
        if (different_type) bytes[header_bytes + record_bytes] = 1;
        reject(bytes);
    }
    for (const char* invalid : {"", "relative.wav", "/a//b", "/a/..", "/a/.", "/a/", "/a\nb", "sdmc:/../a"}) {
        bytes = good;
        std::memset(bytes.data() + header_bytes + 1, 0, store::kPath);
        std::memcpy(bytes.data() + header_bytes + 1, invalid, std::strlen(invalid));
        reject(bytes);
    }
    CHECK(store::load(good_path.c_str(), destination)); same(state, destination); // recover after dirty scratch
    std::puts("malformed/truncated/corrupt/duplicate: pass");
}
static void save_failures(const Fixture& fixture) {
    store::State old, next;
    CHECK(store::toggle(old, "/old.wav", false));
    CHECK(store::toggle(next, "/next.wav", false));
    next.auto_preview = false;
    const Image next_image = image(next);
    const std::string path = fixture.path("atomic.bin");
    CHECK(store::save(path.c_str(), old));
    const Bytes previous = read_file(path);
    const Bytes sentinel = {'n','o','t',' ','o','u','r','s'};
    write_file(path + ".tmp-00", sentinel);
    CHECK(store::save(path.c_str(), next)); // collision skips, never truncates
    CHECK(read_file(path + ".tmp-00") == sentinel);
    CHECK(image(next) == next_image);
    CHECK(store::save(path.c_str(), old));
    for (int i = 1; i < 16; ++i) {
        char suffix[32]; std::snprintf(suffix, sizeof suffix, ".tmp-%02d", i);
        write_file(path + suffix, sentinel);
    }
    CHECK(!store::save(path.c_str(), next)); // bounded exclusive collisions
    CHECK(read_file(path) == previous && image(next) == next_image);
    for (int i = 0; i < 16; ++i) {
        char suffix[32]; std::snprintf(suffix, sizeof suffix, ".tmp-%02d", i);
        CHECK(read_file(path + suffix) == sentinel);
        CHECK(fs::remove(path + suffix));
    }
    CHECK(!store::save(fixture.path("absent/file.bin").c_str(), next));
    CHECK(image(next) == next_image && read_file(path) == previous);
    // Actual buffered write/flush failure must leave the existing target intact.
    struct rlimit limit{}; CHECK(::getrlimit(RLIMIT_FSIZE, &limit) == 0);
    struct rlimit small = limit; small.rlim_cur = 1024;
    const auto old_signal = std::signal(SIGXFSZ, SIG_IGN); CHECK(old_signal != SIG_ERR);
    CHECK(::setrlimit(RLIMIT_FSIZE, &small) == 0);
    const bool saved = store::save(path.c_str(), next);
    CHECK(::setrlimit(RLIMIT_FSIZE, &limit) == 0);
    CHECK(std::signal(SIGXFSZ, old_signal) != SIG_ERR);
    CHECK(!saved && read_file(path) == previous && image(next) == next_image);
    CHECK(!fs::exists(path + ".tmp-00"));
    // Rename error: existing directory and its contents remain untouched.
    const std::string directory = fixture.path("directory.bin");
    CHECK(fs::create_directory(directory));
    write_file(directory + "/marker", sentinel);
    CHECK(!store::save(directory.c_str(), next));
    CHECK(read_file(directory + "/marker") == sentinel);
    CHECK(!fs::exists(directory + ".tmp-00"));
    CHECK(image(next) == next_image);
    // Invalid in-memory state is rejected before opening any staged file.
    store::State invalid = next; invalid.count = 65;
    CHECK(!store::save(path.c_str(), invalid));
    invalid.count = -1; CHECK(!store::save(path.c_str(), invalid));
    invalid = next; invalid.entries[1] = invalid.entries[0]; invalid.count = 2;
    CHECK(!store::save(path.c_str(), invalid));
    invalid = next; std::memset(invalid.entries[0].path, 'x', store::kPath);
    CHECK(!store::save(path.c_str(), invalid));
    CHECK(read_file(path) == previous);
    CHECK(!fs::exists(path + ".tmp-00"));
    std::puts("staging/failed-save-preservation: pass");
}
int main() {
    Fixture fixture;
    roundtrip(fixture);
    paths_and_capacity();
    malformed(fixture);
    save_failures(fixture);
    std::printf("sample browser store: 4/4 groups, %d checks\n", checks);
}
