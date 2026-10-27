// Real temporary WAVs, frozen old decoder vs current decoder in separate objects.
// fread counts are C stdio calls, NOT SD/FS service calls or hardware timings.
#include "../core/synth/wav_loader.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace trackr::synth {
WavLoadResult reference_load_wav_to_sample(const char*, Sample&, int, int, WavDecodeLimits);
}
using namespace trackr::synth;
using Bytes = std::vector<uint8_t>;
using Loader = WavLoadResult (*)(const char*, Sample&, int, int, WavDecodeLimits);
constexpr int target_rate = 32000;
constexpr size_t old_block = 4096, new_block = 65536;
static uint64_t checks = 0, pairs = 0, compared_elements = 0;
static std::string case_name;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    std::fprintf(stderr, "FAIL %s line %d: %s\n", case_name.c_str(), __LINE__, #condition); \
    std::exit(1); } } while (false)

struct IoStats {
    size_t reads = 0, seeks = 0, payload_reads = 0, payload_seeks = 0;
    size_t payload_requested = 0, max_payload_request = 0;
    size_t allocation_bytes = 0, allocations = 0;
    bool fault_fired = false;
};
enum class SeekFault { None, First, InitialData, LateData };
struct Fault { size_t payload_read = 0; SeekFault seek = SeekFault::None; };
static bool tracking = false;
static IoStats current_io;
static Fault current_fault;
static long payload_begin = -1, payload_end = -1;

extern "C" {
size_t __real_fread(void*, size_t, size_t, FILE*);
int __real_fseek(FILE*, long, int);
void* __real__Znwm(size_t);
void* __wrap__Znwm(size_t bytes) {
    if (tracking) { current_io.allocation_bytes += bytes; ++current_io.allocations; }
    return __real__Znwm(bytes);
}
size_t __wrap_fread(void* buffer, size_t size, size_t count, FILE* file) {
    if (tracking) {
        ++current_io.reads;
        const long position = std::ftell(file);
        if (position >= payload_begin && position < payload_end && size && count) {
            ++current_io.payload_reads;
            current_io.payload_requested += size * count;
            current_io.max_payload_request = std::max(current_io.max_payload_request, size * count);
            if (current_fault.payload_read == current_io.payload_reads) {
                current_io.fault_fired = true;
                // An actual short fread, not an exception and not a destination write.
                return __real_fread(buffer, size, count - 1, file);
            }
        }
    }
    return __real_fread(buffer, size, count, file);
}
int __wrap_fseek(FILE* file, long offset, int whence) {
    if (tracking) {
        ++current_io.seeks;
        const bool late = current_io.payload_reads != 0;
        if (late) ++current_io.payload_seeks;
        const bool initial_data = !late && current_io.allocations >= 2 &&
            whence == SEEK_SET && offset == payload_begin;
        if (!current_io.fault_fired &&
            ((current_fault.seek == SeekFault::First && current_io.seeks == 1) ||
             (current_fault.seek == SeekFault::InitialData && initial_data) ||
             (current_fault.seek == SeekFault::LateData && late))) {
            current_io.fault_fired = true;
            return -1;
        }
    }
    return __real_fseek(file, offset, whence);
}
}

static void le(Bytes& bytes, uint32_t value, unsigned count) {
    for (unsigned i = 0; i < count; ++i) bytes.push_back(uint8_t(value >> (8u * i)));
}
static void tag(Bytes& bytes, const char* name) { bytes.insert(bytes.end(), name, name + 4); }
static void chunk(Bytes& bytes, const char* name, const Bytes& data) {
    tag(bytes, name); le(bytes, uint32_t(data.size()), 4);
    bytes.insert(bytes.end(), data.begin(), data.end());
    if (data.size() & 1u) bytes.push_back(0);
}
static void word_at(Bytes& bytes, size_t at, uint32_t value) {
    CHECK(at + 4 <= bytes.size());
    for (unsigned i = 0; i < 4; ++i) bytes[at + i] = uint8_t(value >> (8u * i));
}
struct Format { int tag, bits; const char* name; };
static constexpr Format formats[] = {{1,8,"pcm8"},{1,16,"pcm16"},{1,24,"pcm24"},
                                     {1,32,"pcm32"},{3,32,"float32"}};
struct Fixture {
    Bytes raw;
    std::vector<int16_t> native;
    Format format;
    int channels, rate;
    uint32_t frames;
};
static Fixture fixture(Format format, int channels, int rate, uint32_t frames) {
    Fixture f{{},{},format,channels,rate,frames};
    const size_t elements = size_t(frames) * unsigned(channels);
    f.raw.reserve(elements * unsigned(format.bits / 8)); f.native.reserve(elements);
    for (size_t i = 0; i < elements; ++i) {
        const uint32_t n = uint32_t(i);
        int16_t value = int16_t(int((n * 811u + 173u) & 65535u) - 32768);
        if (format.tag == 3) {
            const float values[] = {-1.0f,0.0f,0.375f,1.0f,-0.271f,2.0f,-2.0f,
                std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                -std::numeric_limits<float>::infinity(), -0.0f, 0.99999f, -0.99999f};
            const float input = values[i % (sizeof values / sizeof values[0])];
            uint32_t bits = 0; std::memcpy(&bits, &input, sizeof bits); le(f.raw, bits, 4);
            value = input == input ? int16_t(std::clamp(input,-1.0f,1.0f) * 32767.0f) : 0;
        } else if (format.bits == 8) {
            const uint8_t input = uint8_t(n * 37u + 19u);
            f.raw.push_back(input); value = int16_t((int(input) - 128) * 256);
        } else {
            const uint32_t input = format.bits == 16 ? uint16_t(value) : format.bits == 24 ?
                uint32_t(int32_t(value) * 256) + (n & 255u) :
                uint32_t(int64_t(value) * 65536) + (n & 65535u);
            le(f.raw, input, unsigned(format.bits / 8));
        }
        f.native.push_back(value);
    }
    return f;
}
struct Wave { Bytes bytes; long data = -1; size_t data_bytes = 0; };
static Wave wave(const Fixture& fixture, const std::vector<uint32_t>& cues = {},
                 bool extras = false, bool data_first = false) {
    Wave out;
    tag(out.bytes, "RIFF"); le(out.bytes, 0, 4); tag(out.bytes, "WAVE");
    Bytes fmt; const unsigned frame_bytes = unsigned(fixture.channels * fixture.format.bits / 8);
    le(fmt, unsigned(fixture.format.tag), 2); le(fmt, unsigned(fixture.channels), 2);
    le(fmt, unsigned(fixture.rate), 4); le(fmt, unsigned(fixture.rate) * frame_bytes, 4);
    le(fmt, frame_bytes, 2); le(fmt, unsigned(fixture.format.bits), 2);
    if (extras) { chunk(out.bytes, "JUNK", {1,2,3}); fmt.resize(41, 0); }
    auto data = [&]() {
        out.data = long(out.bytes.size() + 8); out.data_bytes = fixture.raw.size();
        chunk(out.bytes, "data", fixture.raw);
    };
    if (data_first) data();
    chunk(out.bytes, "fmt ", fmt);
    if (!data_first) data();
    if (!cues.empty()) {
        Bytes cue; le(cue, uint32_t(cues.size()), 4);
        for (uint32_t marker : cues) {
            for (int i = 0; i < 5; ++i) le(cue, 0, 4);
            le(cue, marker, 4);
        }
        chunk(out.bytes, "cue ", cue);
    }
    if (extras) chunk(out.bytes, "LIST", {8,6,7,5,3});
    word_at(out.bytes, 4, uint32_t(out.bytes.size() - 8));
    return out;
}
static void store(const std::string& path, const Wave& wav) {
    FILE* file = std::fopen(path.c_str(), "wb"); CHECK(file);
    CHECK(std::fwrite(wav.bytes.data(), 1, wav.bytes.size(), file) == wav.bytes.size());
    CHECK(std::fclose(file) == 0);
}
static Sample sentinel() {
    Sample out; out.data = {111,-222,333,-444,555,-666,777,-888}; out.data.reserve(29);
    out.channels = 2; out.root_note = 37; out.loop_start = 1; out.loop_end = 3;
    out.logical_frames = 3; out.reversed = true; out.slice_reverse_mask = 0x5555;
    out.exact_native_endpoint = true;
    for (int i = 0; i < Sample::MAX_CHOPS; ++i) {
        out.chops[i] = uint32_t(i * 7 + 3); out.slice_speed_q8[i] = uint16_t(64 + i * 53);
    }
    return out;
}
static void metadata_equal(const Sample& a, const Sample& b) {
    CHECK(a.channels == b.channels); CHECK(a.root_note == b.root_note);
    CHECK(a.loop_start == b.loop_start); CHECK(a.loop_end == b.loop_end);
    CHECK(a.logical_frames == b.logical_frames); CHECK(a.reversed == b.reversed);
    CHECK(a.slice_reverse_mask == b.slice_reverse_mask);
    CHECK(a.exact_native_endpoint == b.exact_native_endpoint);
    CHECK(std::equal(std::begin(a.chops),std::end(a.chops),std::begin(b.chops)));
    CHECK(std::equal(std::begin(a.slice_speed_q8),std::end(a.slice_speed_q8),std::begin(b.slice_speed_q8)));
}
static void equal(const Sample& a, const Sample& b) {
    CHECK(a.data == b.data); metadata_equal(a,b); compared_elements += a.data.size();
}
struct Decode { Sample sample; IoStats io; WavLoadResult result = WavLoadResult::NotRiff; };
static Decode decode(Loader loader, const char* path, const Wave& wav, int cap,
                     WavDecodeLimits limits, Fault fault = {}, int target = target_rate) {
    Decode out; out.sample = sentinel();
    const Sample before = out.sample; const auto* pointer = out.sample.data.data();
    const auto capacity = out.sample.data.capacity();
    current_io = {}; current_fault = fault;
    payload_begin = wav.data; payload_end = wav.data + long(wav.data_bytes);
    tracking = true;
    out.result = loader(path, out.sample, target, cap, limits);
    tracking = false; out.io = current_io;
    CHECK(!fault.payload_read && fault.seek == SeekFault::None ? !out.io.fault_fired : out.io.fault_fired);
    if (int(out.result) < 0) {
        equal(out.sample, before); CHECK(out.sample.data.data() == pointer);
        CHECK(out.sample.data.capacity() == capacity);
    }
    return out;
}
struct Pair { Decode old, now; };
static Pair pair(const std::string& path, const Wave& wav, int cap, WavDecodeLimits limits,
                 Fault fault = {}, int target = target_rate) {
    Pair out{decode(reference_load_wav_to_sample,path.c_str(),wav,cap,limits,fault,target),
             decode(load_wav_to_sample,path.c_str(),wav,cap,limits,fault,target)};
    CHECK(out.old.result == out.now.result); equal(out.old.sample,out.now.sample); ++pairs;
    CHECK(out.old.io.max_payload_request <= old_block);
    CHECK(out.now.io.max_payload_request <= new_block);
    return out;
}
static uint32_t expected_frames(const Fixture& f, int cap) {
    return uint32_t(std::min<uint64_t>(unsigned(cap),std::max<uint64_t>(1,uint64_t(f.frames) * target_rate / unsigned(f.rate))));
}
static void expected_metadata(const Sample& sample, const Fixture& f, int cap, bool exact,
                              const std::vector<uint32_t>& cues) {
    Sample expected; expected.channels = uint8_t(f.channels); expected.exact_native_endpoint = exact;
    std::vector<uint32_t> kept;
    const double ratio = double(f.rate) / target_rate;
    for (size_t i = 0; i < std::min<size_t>(cues.size(),Sample::MAX_CHOPS); ++i) {
        const uint32_t at = uint32_t(double(cues[i]) / ratio);
        if (at < expected_frames(f,cap)) kept.push_back(at);
    }
    std::sort(kept.begin(),kept.end()); kept.erase(std::unique(kept.begin(),kept.end()),kept.end());
    std::copy(kept.begin(),kept.end(),expected.chops); metadata_equal(sample,expected);
    CHECK(sample.num_frames() == expected_frames(f,cap));
}
static void native_oracle(const Sample& sample, const Fixture& f, int cap, bool exact) {
    CHECK(f.rate == target_rate);
    const uint32_t count = expected_frames(f,cap);
    CHECK(sample.data.size() == size_t(count) * unsigned(f.channels));
    for (uint32_t i = 0; i < count; ++i) for (int ch = 0; ch < f.channels; ++ch) {
        const size_t at = size_t(i) * unsigned(f.channels) + unsigned(ch);
        CHECK(sample.data[at] == ((!exact && i + 1 == f.frames) ? 0 : f.native[at]));
    }
}
static void matrix(const std::string& path) {
    const uint64_t first = pairs;
    for (const auto format : formats) for (int channels : {1,2})
        for (int rate : {32000,44100,48000,22050,96000,32003,47999,8001}) {
            const unsigned frame_bytes = unsigned(channels * format.bits / 8);
            const uint32_t frames = uint32_t(2 * (new_block / frame_bytes) + 17);
            const auto f = fixture(format,channels,rate,frames);
            // Includes duplicates/out of order markers, old/new seams and >16 records.
            std::vector<uint32_t> cues{frames-1,0,13,13,uint32_t(old_block/frame_bytes-1),
                uint32_t(new_block/frame_bytes-1),uint32_t(new_block/frame_bytes),frames+17};
            for (uint32_t i = 0; i < 16; ++i) cues.push_back(19 + i * 7);
            const auto wav = wave(f,cues,true,(rate & 1) != 0); store(path,wav);
            const int full = int(uint64_t(frames) * target_rate / unsigned(rate));
            for (bool exact : {false,true}) for (int cap : {full+3,std::max(1,full/2+5)}) {
                case_name = "matrix/" + std::string(format.name) + "/" + std::to_string(channels) +
                    "/" + std::to_string(rate) + "/cap" + std::to_string(cap) + "/exact" + std::to_string(exact);
                const auto result = pair(path,wav,cap,{WAV_DECODE_WORKSPACE_BYTES,exact});
                CHECK(result.now.result == (full > cap ? WavLoadResult::Truncated : WavLoadResult::Ok));
                expected_metadata(result.now.sample,f,cap,exact,cues);
                if (rate == target_rate) native_oracle(result.now.sample,f,cap,exact);
            }
        }
    std::printf("pass format/rate/seam matrix: %llu paired decodes, full PCM/all metadata exact\n",
                static_cast<unsigned long long>(pairs-first));
}
static void endpoints(const std::string& path) {
    const uint64_t first = pairs;
    for (const auto format : formats) for (int channels : {1,2})
        for (int rate : {32000,44100,22050,32003}) for (uint32_t frames : {1u,2u,3u}) {
            const auto f = fixture(format,channels,rate,frames); const auto wav = wave(f); store(path,wav);
            for (bool exact : {false,true}) for (int cap : {1,2,3,8}) {
                case_name = "endpoint/" + std::string(format.name) + "/" + std::to_string(channels) +
                    "/" + std::to_string(rate) + "/frames" + std::to_string(frames) +
                    "/cap" + std::to_string(cap) + "/exact" + std::to_string(exact);
                const auto result = pair(path,wav,cap,{WAV_DECODE_WORKSPACE_BYTES,exact});
                CHECK(int(result.now.result) >= 0); expected_metadata(result.now.sample,f,cap,exact,{});
                if (rate == target_rate) native_oracle(result.now.sample,f,cap,exact);
            }
        }
    std::printf("pass one/two/three-frame endpoints, both policies and caps: %llu pairs\n",
                static_cast<unsigned long long>(pairs-first));
}
static void budgets(const std::string& path) {
    const uint64_t first = pairs;
    for (const auto format : formats) for (int channels : {1,2}) for (int rate : {32000,44100,22050}) {
        const size_t frame_bytes = size_t(channels * format.bits / 8);
        const auto f = fixture(format,channels,rate,uint32_t(2 * new_block / frame_bytes + 9));
        const auto wav = wave(f); store(path,wav);
        const int full = int(uint64_t(f.frames) * target_rate / unsigned(rate));
        for (int cap : {full,full/2+1}) {
            const uint64_t needed = std::min<uint64_t>(f.frames,uint64_t(expected_frames(f,cap)-1) * unsigned(rate) / target_rate + 2);
            const size_t output = size_t(expected_frames(f,cap)) * unsigned(channels) * sizeof(int16_t);
            const size_t minimum = size_t(std::min<uint64_t>(needed,old_block/frame_bytes)) * frame_bytes;
            const size_t maximum = size_t(std::min<uint64_t>(needed,new_block/frame_bytes)) * frame_bytes;
            for (size_t budget : {size_t(0),WAV_DECODE_WORKSPACE_BYTES,output+minimum-1,
                    output+minimum,output+minimum+frame_bytes-1,output+16387,output+maximum-1,output+maximum,output,size_t(1)}) {
                case_name = "budget/" + std::string(format.name) + "/" + std::to_string(channels) +
                    "/" + std::to_string(rate) + "/cap" + std::to_string(cap) + "/" + std::to_string(budget);
                const auto result = pair(path,wav,cap,{budget,false});
                const bool accepted = !budget || budget >= output + minimum;
                CHECK((int(result.now.result) >= 0) == accepted);
                if (accepted) {
                    const size_t raw = budget ? std::min(maximum,(budget-output)/frame_bytes*frame_bytes) : maximum;
                    CHECK(result.old.io.allocations == 2 && result.now.io.allocations == 2);
                    CHECK(result.old.io.allocation_bytes == output + minimum);
                    CHECK(result.now.io.allocation_bytes == output + raw);
                    CHECK(result.old.io.max_payload_request == minimum);
                    CHECK(result.now.io.max_payload_request == raw);
                } else {
                    CHECK(result.now.result == WavLoadResult::MemoryBudget);
                    CHECK(result.old.io.allocations == 0 && result.now.io.allocations == 0);
                }
            }
        }
    }
    // Exactly exhaust the real 4 MiB default: the new decoder must use its old
    // minimum, not reject formerly accepted PCM because its preferred block grew.
    for (unsigned extra : {0u,1u}) {
        const auto f = fixture(formats[1],2,32000,uint32_t((WAV_DECODE_WORKSPACE_BYTES-old_block)/4+extra));
        const auto wav = wave(f); store(path,wav); case_name = "default-budget-boundary/" + std::to_string(extra);
        const auto result = pair(path,wav,int(f.frames),{WAV_DECODE_WORKSPACE_BYTES,true});
        CHECK(result.now.result == (extra ? WavLoadResult::MemoryBudget : WavLoadResult::Ok));
        if (!extra) CHECK(result.now.io.allocation_bytes == WAV_DECODE_WORKSPACE_BYTES);
    }
    std::printf("pass adaptive workspace/admission/allocation checks: %llu pairs\n",
                static_cast<unsigned long long>(pairs-first));
}
static void faults(const std::string& path) {
    const uint64_t first = pairs;
    for (int rate : {32000,44100,32000000}) {
        const auto f = fixture(formats[2],2,rate,210013); const auto wav = wave(f); store(path,wav);
        case_name = "fault-baseline/" + std::to_string(rate);
        const auto good = pair(path,wav,640000,{WAV_DECODE_WORKSPACE_BYTES,true});
        CHECK(good.old.io.payload_reads > 2 && good.now.io.payload_reads > 2);
        case_name = "late-fread-second/" + std::to_string(rate);
        const auto second = pair(path,wav,640000,{WAV_DECODE_WORKSPACE_BYTES,true},{2,SeekFault::None});
        CHECK(second.now.result == WavLoadResult::NoDataChunk);
        case_name = "late-fread-last/" + std::to_string(rate);
        const auto old = decode(reference_load_wav_to_sample,path.c_str(),wav,640000,
            {WAV_DECODE_WORKSPACE_BYTES,true},{good.old.io.payload_reads,SeekFault::None});
        const auto now = decode(load_wav_to_sample,path.c_str(),wav,640000,
            {WAV_DECODE_WORKSPACE_BYTES,true},{good.now.io.payload_reads,SeekFault::None});
        CHECK(old.result == WavLoadResult::NoDataChunk && now.result == old.result);
        equal(old.sample,now.sample); ++pairs;
        for (SeekFault where : {SeekFault::First,SeekFault::InitialData}) {
            case_name = "seek-failure/" + std::to_string(rate) + "/" + std::to_string(int(where));
            const auto seek = pair(path,wav,640000,{WAV_DECODE_WORKSPACE_BYTES,true},{0,where});
            CHECK(seek.now.result == (where == SeekFault::First ? WavLoadResult::NotRiff : WavLoadResult::NoDataChunk));
        }
        if (rate == 32000000) {
            case_name = "late-resampling-seek-failure";
            CHECK(good.old.io.payload_seeks && good.now.io.payload_seeks);
            const auto seek = pair(path,wav,640000,{WAV_DECODE_WORKSPACE_BYTES,true},{0,SeekFault::LateData});
            CHECK(seek.now.result == WavLoadResult::NoDataChunk);
        }
    }
    std::printf("pass injected late short-read/seek failures: %llu pairs; destination pointer, capacity and every field retained\n",
                static_cast<unsigned long long>(pairs-first));
}
static void malformed(const std::string& path) {
    const uint64_t first = pairs;
    const auto good = wave(fixture(formats[1],2,44100,31));
    for (size_t length = 0; length < good.bytes.size(); ++length) {
        Wave clipped = good; clipped.bytes.resize(length); store(path,clipped);
        case_name = "truncated-file/" + std::to_string(length);
        (void)pair(path,clipped,100,{WAV_DECODE_WORKSPACE_BYTES,false});
    }
    std::vector<Wave> invalid;
    Wave broken = good; broken.bytes[0] = 'X'; invalid.push_back(broken);
    broken = good; broken.bytes[8] = 'X'; invalid.push_back(broken);
    broken = good; word_at(broken.bytes,16,15); invalid.push_back(broken);
    broken = good; word_at(broken.bytes,16,UINT32_MAX); invalid.push_back(broken);
    broken = good; word_at(broken.bytes,size_t(good.data)-4,UINT32_MAX); invalid.push_back(broken);
    broken = good; broken.bytes[12] = 'x'; invalid.push_back(broken);
    broken = good; broken.bytes[size_t(good.data)-8] = 'x'; invalid.push_back(broken);
    for (const auto format : {Format{6,16,"unsupported"},Format{1,12,"unsupported-bits"},Format{3,16,"unsupported-float"}}) {
        auto f = fixture(formats[1],1,32000,10); f.format = format; invalid.push_back(wave(f));
    }
    for (int channels : {0,3}) { auto f = fixture(formats[1],1,32000,10); f.channels=channels; invalid.push_back(wave(f)); }
    auto f = fixture(formats[1],1,32000,10); f.rate=0; invalid.push_back(wave(f));
    f = fixture(formats[1],1,32000,0); invalid.push_back(wave(f));
    f.raw={1}; invalid.push_back(wave(f));
    for (size_t i=0;i<invalid.size();++i) {
        case_name="malformed/"+std::to_string(i); store(path,invalid[i]);
        const auto result=pair(path,invalid[i],100,{WAV_DECODE_WORKSPACE_BYTES,false});
        CHECK(int(result.now.result)<0);
    }
    case_name="invalid-api-arguments";
    for (Loader loader : {reference_load_wav_to_sample,load_wav_to_sample}) {
        CHECK(decode(loader,nullptr,good,100,{}).result==WavLoadResult::UnsupportedFormat);
        CHECK(decode(loader,"",good,100,{}).result==WavLoadResult::UnsupportedFormat);
        CHECK(decode(loader,path.c_str(),good,0,{}).result==WavLoadResult::UnsupportedFormat);
        CHECK(decode(loader,path.c_str(),good,100,{}, {},0).result==WavLoadResult::UnsupportedFormat);
        CHECK(decode(loader,(path+".missing").c_str(),good,100,{}).result==WavLoadResult::FileNotFound);
    }
    std::printf("pass malformed/empty/truncated headers and invalid arguments: %llu pairs + 10 guarded failure cases\n",
                static_cast<unsigned long long>(pairs-first));
}
struct Measurement { std::string name; IoStats old, now; double old_ms=0, now_ms=0; };
static std::vector<Measurement> measures;
static double elapsed(Loader loader, const std::string& path, Sample& out) {
    const auto start=std::chrono::steady_clock::now();
    const auto result=loader(path.c_str(),out,target_rate,target_rate*20,{WAV_DECODE_WORKSPACE_BYTES,false});
    const auto finish=std::chrono::steady_clock::now();
    CHECK(result==WavLoadResult::Ok);
    return std::chrono::duration<double,std::milli>(finish-start).count();
}
static void typical(const std::string& path, bool benchmark, unsigned repeats) {
    for (const auto format : {formats[1],formats[2],formats[4]}) for (int channels : {1,2})
        for (int rate : {32000,44100,48000}) {
            if (format.bits!=16 && channels==1) continue;
            const auto f=fixture(format,channels,rate,uint32_t(rate*10)); const auto wav=wave(f); store(path,wav);
            case_name="10s/"+std::string(format.name)+"/"+std::to_string(channels)+"ch/"+std::to_string(rate);
            const auto result=pair(path,wav,target_rate*20,{WAV_DECODE_WORKSPACE_BYTES,false});
            CHECK(result.now.result==WavLoadResult::Ok);
            CHECK(result.now.io.payload_reads*8 < result.old.io.payload_reads);
            Measurement m{case_name,result.old.io,result.now.io,0,0};
            std::printf("stdio %s: payload fread old=%zu new=%zu; max request old=%zu new=%zu bytes; NOT device/FS call counts\n",
                m.name.c_str(),m.old.payload_reads,m.now.payload_reads,m.old.max_payload_request,m.now.max_payload_request);
            if (benchmark) {
                std::vector<double> old_times,new_times;
                // Paired correctness above warms both paths. Alternate execution order.
                // Timed calls do not include the ftell/allocation instrumentation.
                for (unsigned i=0;i<repeats;++i) {
                    Sample a,b; double ta=0,tb=0;
                    if (i&1u) { tb=elapsed(load_wav_to_sample,path,b); ta=elapsed(reference_load_wav_to_sample,path,a); }
                    else { ta=elapsed(reference_load_wav_to_sample,path,a); tb=elapsed(load_wav_to_sample,path,b); }
                    equal(a,b); old_times.push_back(ta); new_times.push_back(tb);
                }
                std::sort(old_times.begin(),old_times.end()); std::sort(new_times.begin(),new_times.end());
                m.old_ms=old_times[old_times.size()/2]; m.now_ms=new_times[new_times.size()/2];
                std::printf("host-only warm-filesystem median (%u repeats) %s: old=%.3f new=%.3f ms; NOT 3DS timing\n",
                    repeats,m.name.c_str(),m.old_ms,m.now_ms);
            }
            measures.push_back(m);
        }
}
static void json_stats(FILE* file, const IoStats& s) {
    std::fprintf(file,"{\"fread_calls\":%zu,\"fseek_calls\":%zu,\"payload_fread_calls\":%zu,"
        "\"payload_fseek_calls\":%zu,\"max_payload_request_bytes\":%zu,\"payload_requested_bytes\":%zu,"
        "\"cpp_allocation_calls\":%zu,\"cpp_allocation_bytes\":%zu}",s.reads,s.seeks,s.payload_reads,
        s.payload_seeks,s.max_payload_request,s.payload_requested,s.allocations,s.allocation_bytes);
}
static void receipt(const char* path, bool benchmark, unsigned repeats) {
    FILE* file=std::fopen(path,"wb"); CHECK(file);
    std::fprintf(file,"{\"scope\":\"paired WAV loaders only; generated temporary PCM; no engine/UI/device\","
        "\"mode\":\"%s\",\"stdio_not_sd_calls\":true,\"hardware_timing\":false,"
        "\"checks\":%llu,\"paired_decodes\":%llu,\"compared_pcm_elements\":%llu,\"benchmark_repeats\":%u,\"measurements\":[",
        benchmark?"host-only-benchmark":"correctness",static_cast<unsigned long long>(checks),
        static_cast<unsigned long long>(pairs),static_cast<unsigned long long>(compared_elements),benchmark?repeats:0);
    for (size_t i=0;i<measures.size();++i) {
        const auto& m=measures[i]; std::fprintf(file,"%s{\"name\":\"%s\",\"old\":",i?",":"",m.name.c_str());
        json_stats(file,m.old); std::fputs(",\"optimized\":",file); json_stats(file,m.now);
        if (benchmark) std::fprintf(file,",\"old_host_median_ms\":%.6f,\"optimized_host_median_ms\":%.6f",m.old_ms,m.now_ms);
        std::fputc('}',file);
    }
    std::fputs("]}\n",file); CHECK(std::fclose(file)==0);
}
int main(int argc,char** argv) {
    if (argc!=4 && argc!=5) { std::fprintf(stderr,"usage: test temp-dir result.json correctness|benchmark [repeats]\n"); return 2; }
    const bool benchmark=std::strcmp(argv[3],"benchmark")==0;
    if (!benchmark && std::strcmp(argv[3],"correctness")) return 2;
    const unsigned repeats=argc==5?unsigned(std::strtoul(argv[4],nullptr,10)):5;
    if (!repeats || repeats>5) return 2;
    const std::string path=std::string(argv[1])+"/generated.wav";
    if (!benchmark) { matrix(path); endpoints(path); budgets(path); faults(path); malformed(path); }
    typical(path,benchmark,repeats);
    case_name="cleanup"; CHECK(std::remove(path.c_str())==0);
    receipt(argv[2],benchmark,repeats);
    std::printf("pass %s: %llu paired decodes, %llu checks, %llu exact PCM element comparisons; no device claim\n",
        benchmark?"host-only timing + per-fixture correctness":"focused loader correctness",
        static_cast<unsigned long long>(pairs),static_cast<unsigned long long>(checks),static_cast<unsigned long long>(compared_elements));
}
