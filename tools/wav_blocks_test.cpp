// Focused WAV block IO: scalar PCM oracle, actual allocation/stdio boundaries,
// short-read/write/callback failures and atomic destination preservation.
#include "../core/synth/wav_loader.h"
#include "../core/synth/wav_writer.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>
using namespace trackr::synth;
using Bytes = std::vector<uint8_t>;

enum class Mode { Off, Decode, Encode };
static Mode mode = Mode::Off;
struct Stats { size_t allocated=0, allocations=0, max_read=0, max_write=0, reads=0, writes=0; } stats;
static int fail_read=0, fail_write=0;
static bool fail_flush=false, fail_close=false, fail_rename=false;
extern "C" void* __real__Znwm(size_t);
extern "C" void* __wrap__Znwm(size_t n) {
    if (mode == Mode::Decode) { stats.allocated += n; ++stats.allocations; }
    return __real__Znwm(n);
}
extern "C" size_t __real_fread(void*, size_t, size_t, FILE*);
extern "C" size_t __wrap_fread(void* p, size_t size, size_t count, FILE* f) {
    if (mode == Mode::Decode && size * count > 40) {
        stats.max_read = std::max(stats.max_read, size * count);
        if (++stats.reads == unsigned(fail_read)) return __real_fread(p, size, count - 1, f);
    }
    return __real_fread(p, size, count, f);
}
extern "C" size_t __real_fwrite(const void*, size_t, size_t, FILE*);
extern "C" size_t __wrap_fwrite(const void* p, size_t size, size_t count, FILE* f) {
    if (mode == Mode::Encode) {
        stats.max_write = std::max(stats.max_write, size * count);
        if (++stats.writes == unsigned(fail_write)) return __real_fwrite(p, size, count - 1, f);
    }
    return __real_fwrite(p, size, count, f);
}
extern "C" int __real_fflush(FILE*);
extern "C" int __wrap_fflush(FILE* f) { return mode == Mode::Encode && fail_flush ? EOF : __real_fflush(f); }
extern "C" int __real_fclose(FILE*);
extern "C" int __wrap_fclose(FILE* f) {
    const int result = __real_fclose(f);
    return mode == Mode::Encode && fail_close ? EOF : result;
}
extern "C" int __real_rename(const char*, const char*);
extern "C" int __wrap_rename(const char* a, const char* b) {
    return mode == Mode::Encode && fail_rename ? -1 : __real_rename(a, b);
}

static void le(Bytes& b, uint32_t value, int bytes) {
    for (int i=0; i<bytes; ++i) b.push_back(uint8_t(value >> (8*i)));
}
static void tag(Bytes& b, const char* id) { b.insert(b.end(), id, id+4); }
static void chunk(Bytes& b, const char* id, const Bytes& data) {
    tag(b,id); le(b,data.size(),4); b.insert(b.end(),data.begin(),data.end());
    if (data.size() & 1) b.push_back(0);
}
static Bytes wave(const Bytes& raw, int format, int bits, int channels, uint32_t rate,
                  bool extra=false, const std::vector<uint32_t>& cues={}) {
    Bytes out, fmt;
    tag(out,"RIFF"); le(out,0,4); tag(out,"WAVE");
    le(fmt,format,2); le(fmt,channels,2); le(fmt,rate,4);
    le(fmt,rate*channels*(bits/8),4); le(fmt,channels*(bits/8),2); le(fmt,bits,2);
    if (extra) chunk(out,"JUNK",{1,2,3}); // odd RIFF padding and cues after data
    chunk(out,"fmt ",fmt); chunk(out,"data",raw);
    if (!cues.empty()) {
        Bytes cue; le(cue,cues.size(),4);
        for (uint32_t c : cues) { for (int i=0;i<5;++i) le(cue,0,4); le(cue,c,4); }
        chunk(out,"cue ",cue);
    }
    const uint32_t length = out.size()-8;
    for (int i=0;i<4;++i) out[4+i] = uint8_t(length >> (i*8));
    return out;
}
static void store(const std::string& path, const Bytes& b) {
    FILE* f=std::fopen(path.c_str(),"wb"); assert(f);
    assert(std::fwrite(b.data(),1,b.size(),f)==b.size()); assert(!std::fclose(f));
}
static Bytes contents(const std::string& path) {
    FILE* f=std::fopen(path.c_str(),"rb"); assert(f);
    assert(!std::fseek(f,0,SEEK_END)); const long size=std::ftell(f); assert(size>=0);
    std::rewind(f); Bytes b(size);
    assert(std::fread(b.data(),1,b.size(),f)==b.size()); assert(!std::fclose(f)); return b;
}
struct Fixture { Bytes raw; std::vector<int16_t> pcm; int channels, rate; };
static Fixture fixture(int format, int bits, int channels, int rate, int frames) {
    Fixture f{{},{},channels,rate};
    f.raw.reserve(size_t(frames)*channels*(bits/8)); f.pcm.reserve(size_t(frames)*channels);
    for (uint32_t i=0;i<uint32_t(frames*channels);++i) {
        int16_t q=int(i*811u % 65536u)-32768;
        if (format==3) {
            const float values[]={-1.0f,0.0f,0.375f,1.0f,-0.271f,2.0f,-2.0f,
                                  std::numeric_limits<float>::quiet_NaN(),
                                  std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()};
            const float value=values[i%10]; uint32_t word; std::memcpy(&word,&value,4); le(f.raw,word,4);
            q=value==value ? int16_t(std::clamp(value,-1.0f,1.0f)*32767.0f) : 0;
        } else if (bits==8) {
            const uint8_t byte=uint8_t(i*37u); f.raw.push_back(byte); q=(int(byte)-128)*256;
        } else {
            const uint32_t word=bits==16 ? uint16_t(q) : bits==24 ? uint32_t(int32_t(q)*256)+(i&255u)
                                                               : uint32_t(int64_t(q)*65536)+(i&65535u);
            le(f.raw,word,bits/8);
        }
        f.pcm.push_back(q);
    }
    return f;
}
static std::vector<int16_t> oracle(const Fixture& f, int target, int cap, bool exact) {
    const uint32_t frames=f.pcm.size()/f.channels;
    const uint32_t count=std::min<uint64_t>(cap,std::max<uint64_t>(1,uint64_t(frames)*target/f.rate));
    std::vector<int16_t> out(size_t(count)*f.channels,0);
    const double ratio=double(f.rate)/target; double pos=0;
    for (uint32_t i=0;i<count;++i,pos+=ratio) {
        const uint32_t index=uint32_t(pos); const double frac=pos-index;
        if (index+1>=frames) {
            if (exact && f.rate==target && index<frames)
                for (int c=0;c<f.channels;++c) out[size_t(i)*f.channels+c]=f.pcm[size_t(index)*f.channels+c];
            break;
        }
        for (int c=0;c<f.channels;++c) {
            const size_t a=size_t(index)*f.channels+c;
            out[size_t(i)*f.channels+c]=f.pcm[a]+int32_t((f.pcm[a+f.channels]-f.pcm[a])*frac);
        }
    }
    return out;
}
static void same(const Sample& a, const Sample& b) {
    assert(a.data==b.data && a.channels==b.channels && a.root_note==b.root_note);
    assert(a.loop_start==b.loop_start && a.loop_end==b.loop_end && a.reversed==b.reversed);
    assert(a.logical_frames==b.logical_frames && a.slice_reverse_mask==b.slice_reverse_mask);
    assert(a.exact_native_endpoint==b.exact_native_endpoint);
    assert(std::equal(std::begin(a.chops),std::end(a.chops),std::begin(b.chops)));
    assert(std::equal(std::begin(a.slice_speed_q8),std::end(a.slice_speed_q8),std::begin(b.slice_speed_q8)));
}
static WavLoadResult decode(const std::string& path, Sample& out, int cap, WavDecodeLimits limits) {
    stats={}; mode=Mode::Decode;
    const auto result=load_wav_to_sample(path.c_str(),out,32000,cap,limits);
    mode=Mode::Off; assert(stats.max_read<=65536); return result;
}
static void decode_case(const std::string& path, int format, int bits, int channels,
                        int rate, int frames, int cap, bool exact=false) {
    const auto f=fixture(format,bits,channels,rate,frames);
    const std::vector<uint32_t> cues={uint32_t(frames-1),10,0,10,uint32_t(frames+100)};
    store(path,wave(f.raw,format,bits,channels,rate,true,cues));
    Sample out;
    const auto result=decode(path,out,cap,{WAV_DECODE_WORKSPACE_BYTES,exact});
    assert(result==(uint64_t(frames)*32000/rate>uint32_t(cap) ? WavLoadResult::Truncated : WavLoadResult::Ok));
    Sample expected; expected.data=oracle(f,32000,cap,exact); expected.channels=channels;
    expected.exact_native_endpoint=exact;
    std::vector<uint32_t> markers;
    for (auto c : cues) {
        const auto offset=uint32_t(double(c)/(double(rate)/32000));
        if (offset<expected.num_frames()) markers.push_back(offset);
    }
    std::sort(markers.begin(),markers.end()); markers.erase(std::unique(markers.begin(),markers.end()),markers.end());
    std::copy(markers.begin(),markers.end(),expected.chops);
    same(out,expected);
}
static void loader_tests(const std::string& path) {
    // Each supported conversion crosses source-block boundaries at fractional
    // rates. Native endpoints, one-frame clips, capped output and skipped blocks
    // are separate cases rather than a whole-project test matrix.
    for (int bits : {8,16,24,32}) for (int c : {1,2})
        decode_case(path,1,bits,c,c==1?44100:22050,5003,20000);
    for (int c : {1,2}) decode_case(path,3,32,c,c==1?44100:22050,5003,20000);
    for (bool exact : {false,true}) {
        decode_case(path,1,16,1,32000,4099,5000,exact);
        decode_case(path,1,24,2,32000,1,5,exact);
    }
    decode_case(path,1,32,2,96000,20000,777);
    decode_case(path,1,32,2,32000000,9000,30);
    const auto f=fixture(1,24,2,48000,12*48000);
    store(path,wave(f.raw,1,24,2,48000));
    Sample out;
    assert(decode(path,out,20*32000,{WAV_DECODE_WORKSPACE_BYTES,false})==WavLoadResult::Ok);
    assert(out.data==oracle(f,32000,20*32000,false));
    const size_t workspace=1536000+65532, minimum_workspace=1536000+4092;
    assert(stats.allocated==workspace && stats.allocations==2 && stats.reads>1);
    assert(decode(path,out,20*32000,{minimum_workspace,false})==WavLoadResult::Ok);
    assert(stats.allocated==minimum_workspace && stats.allocations==2 && stats.max_read==4092);
    assert(out.data==oracle(f,32000,20*32000,false));
    std::printf("pass WAV import: formats/channels/block seams/endpoints/cues/caps; 12s stereo24 workspace=%zu, tight-budget fallback=%zu\n",workspace,minimum_workspace);
    Sample keep; keep.data={17,-29,901}; keep.root_note=42; keep.loop_start=1; keep.loop_end=2;
    keep.logical_frames=2; keep.reversed=true; keep.chops[0]=1;
    keep.slice_speed_q8[0]=512; keep.slice_reverse_mask=1; keep.exact_native_endpoint=true;
    const Sample before=keep; const auto* original=keep.data.data();
    assert(decode(path,keep,20*32000,{minimum_workspace-1,false})==WavLoadResult::MemoryBudget);
    assert(!stats.allocated); same(keep,before); assert(keep.data.data()==original);
    fail_read=2;
    assert(decode(path,keep,20*32000,{WAV_DECODE_WORKSPACE_BYTES,false})==WavLoadResult::NoDataChunk);
    fail_read=0; same(keep,before); assert(keep.data.data()==original);
    store(path,wave({1,0,2,0,3,0,4,0},1,16,1,32000));
    assert(decode(path,out,4,{16,false})==WavLoadResult::Ok);
    assert((out.data==std::vector<int16_t>{1,2,3,0})); // tiny legacy budget still works
    std::puts("pass WAV import failures: budget before allocation and late source read preserve PCM pointer + all metadata");
}

struct Source { const std::vector<int16_t>* data; uint32_t channels, chunk, frames, next=0, calls=0; bool fail=false; };
static bool read_source(void* context, uint32_t first, uint32_t frames, int16_t* out) {
    auto& s=*static_cast<Source*>(context);
    assert(first==s.next && frames==std::min(s.chunk,s.frames-first));
    s.next+=frames; ++s.calls;
    if (s.fail && s.calls==2) return false;
    std::memcpy(out,s.data->data()+size_t(first)*s.channels,size_t(frames)*s.channels*2);
    return true;
}
static WavSaveResult encode(const std::string& path, Source& source) {
    std::vector<int16_t> scratch(size_t(source.chunk)*source.channels);
    stats={}; mode=Mode::Encode;
    const auto result=save_wav_stream(path.c_str(),source.channels,source.frames,32000,
                                    scratch.data(),source.chunk,read_source,&source);
    mode=Mode::Off; assert(stats.max_write<=4096); return result;
}
static void writer_tests(const std::string& path) {
    for (int c : {1,2}) {
        const auto f=fixture(1,16,c,32000,5003);
        for (uint32_t chunk : {7u,2048u,3001u}) {
            Source source{&f.pcm,uint32_t(c),chunk,5003};
            assert(encode(path,source)==WavSaveResult::Ok);
            assert(source.next==5003 && source.calls==(5003+chunk-1)/chunk);
            assert(contents(path)==wave(f.raw,1,16,c,32000));
        }
    }
    const auto f=fixture(1,16,1,32000,20*32000);
    Sample sample; sample.data=f.pcm;
    stats={}; mode=Mode::Encode;
    assert(save_sample_wav(path.c_str(),sample,32000)==WavSaveResult::Ok);
    mode=Mode::Off;
    assert(stats.writes==325 && stats.max_write==4096);
    assert(contents(path)==wave(f.raw,1,16,1,32000));
    sample.loop_start=2; sample.loop_end=17;
    assert(save_sample_wav(path.c_str(),sample,32000)==WavSaveResult::Ok);
    assert(contents(path)==wave(Bytes(f.raw.begin(),f.raw.begin()+34),1,16,1,32000));
    std::puts("pass WAV export: exact LE RIFF/PCM, mono/stereo/chunk/tail callbacks; 20s mono 325 fwrite calls (was 640012)");
    for (int fault=0;fault<5;++fault) {
        const Bytes old={'k','e','e','p'}; store(path,old);
        Source source{&f.pcm,1,2048,5003};
        fail_write=fault==0 ? 14 : 0; // after a complete header and one data block
        fail_flush=fault==1; fail_close=fault==2; fail_rename=fault==3; source.fail=fault==4;
        assert(encode(path,source)==(fault==3 ? WavSaveResult::RenameFailed : WavSaveResult::WriteFailed));
        fail_write=0; fail_flush=fail_close=fail_rename=false;
        assert(contents(path)==old);
        FILE* temp=std::fopen((path+".tmp").c_str(),"rb"); assert(!temp);
    }
    std::puts("pass WAV export rollback: short write/flush/close/rename/callback leave final file intact and remove temp");
}
int main(int argc, char** argv) {
    assert(argc==2); const std::string path=std::string(argv[1])+"/fixture.wav";
    loader_tests(path); writer_tests(path);
}
