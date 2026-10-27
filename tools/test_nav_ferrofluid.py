#!/usr/bin/env python3
"""Focused real-header ferrofluid host check; no GPU, media, ARM or other suites."""
from pathlib import Path
import argparse
import importlib.util
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CPP = r'''
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include "platform/3ds/nav_ferrofluid.h"
namespace ff = seance::platform::ui3ds::nav_ferrofluid;
static bool no_heap = false;
extern "C" void* __real_malloc(size_t);
extern "C" void* __real_calloc(size_t,size_t);
extern "C" void* __real_realloc(void*,size_t);
extern "C" void* __wrap_malloc(size_t n) { assert(!no_heap); return __real_malloc(n); }
extern "C" void* __wrap_calloc(size_t a,size_t b) { assert(!no_heap); return __real_calloc(a,b); }
extern "C" void* __wrap_realloc(void* p,size_t n) { assert(!no_heap); return __real_realloc(p,n); }
static uint32_t pixels[ff::kCells];
static unsigned commands, max_commands, area;
static uint16_t tone_bits;
static uint64_t hash_bytes(const void* bytes, size_t size) {
    auto p=static_cast<const unsigned char*>(bytes);
    uint64_t hash=14695981039346656037ull;
    for (size_t i=0;i<size;++i) hash=(hash^p[i])*1099511628211ull;
    return hash;
}
namespace seance::ui {
void rect(float x,float y,float w,float h,uint32_t color) {
    assert(x==int(x) && y==int(y) && w==int(w) && h==4 && w>=4);
    assert(int(x)%4==0 && int(y)%4==0 && int(w)%4==0);
    assert(x>=84 && x+w<=316 && y>=4 && y+h<=236);
    assert((color>>24)==255);
    bool found=false;
    for (unsigned i=0;i<16;++i) if (ff::detail::frame.colors[i]==color) {
        found=true; tone_bits|=uint16_t(1u<<i);
    }
    assert(found && ++commands<=ff::kMaxDrawRects);
    const unsigned row=unsigned(y)/4, begin=(unsigned(x)-80)/4;
    for (unsigned col=begin;col<begin+unsigned(w)/4;++col) {
        assert(!pixels[row*60+col]); // disjoint opaque row runs
        pixels[row*60+col]=color;
        ++area;
    }
}
}
static uint64_t draw(bool check_core=true) {
    std::memset(pixels,0,sizeof pixels); commands=area=0;
    const auto before_motion=ff::detail::motion;
    const auto before_frame=ff::detail::frame;
    const uint64_t scratch_hash=hash_bytes(&ff::detail::scratch,sizeof(ff::detail::scratch));
    no_heap=true; ff::draw(); no_heap=false;
    assert(!std::memcmp(&before_motion,&ff::detail::motion,sizeof before_motion));
    assert(!std::memcmp(&before_frame,&ff::detail::frame,sizeof before_frame));
    assert(scratch_hash==hash_bytes(&ff::detail::scratch,sizeof(ff::detail::scratch)));
    max_commands=std::max(max_commands,commands);
    if (check_core && ff::detail::frame.ready) {
        assert(area>380 && area<2200);
        // Loud needles leave a compact closed core (the quiet core stays larger).
        for (int y=0;y<60;++y) for (int x=0;x<60;++x) {
            const int dx=4*x+2-120, dy=4*y+2-118;
            if (dx*dx+dy*dy<38*38) assert(pixels[y*60+x]);
        }
    }
    return hash_bytes(pixels,sizeof pixels);
}
static void capture(uint64_t time, uint16_t peak=0, bool valid=true, uint32_t spot=0xffe89ab5) {
    no_heap=true; ff::capture(time,peak,valid,spot); no_heap=false;
}
static void reset(uint64_t time) { no_heap=true; ff::reset(time); no_heap=false; }
static float length(ff::detail::Vec v) { return std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z); }
// Undo only the fixed ellipsoid scale; rotation preserves vector length.
static float radius(unsigned i) {
    const auto v=ff::data::kVertices[i];
    const float scale=length({v.x*1.045f/16384.f,v.y*.94f/16384.f,v.z/16384.f});
    return length(ff::detail::scratch.points[i])/scale;
}
static float mean_tips() {
    float sum=0;
    for(unsigned i:ff::data::kTips) sum+=radius(i);
    return sum/30.f;
}
static float mean_shoulders() {
    float sum=0; unsigned count=0;
    for(unsigned i=0;i<ff::data::kVertexCount;++i) {
        const float p=ff::data::kVertices[i].spike/32768.f;
        if(p>.25f && p<.65f) { sum+=radius(i); ++count; }
    }
    assert(count>30); return sum/count;
}
static void geometry() {
    using namespace ff;
    unsigned spike_tips=0;
    for (unsigned i=0;i<data::kVertexCount;++i) {
        const auto v=data::kVertices[i];
        const float n=length({v.x/16384.f,v.y/16384.f,v.z/16384.f});
        assert(std::fabs(n-1.f)<.0001f);
        assert(v.bulk>14000 && v.bulk<19000 && v.spike<=32768);
        bool tip=false; for(unsigned t:data::kTips) tip|=t==i;
        if(tip) {
            assert(data::kVertices[i].spike>=27197); // original tips start at .83
        } else {
            assert(data::kVertices[i].spike<22937); // non-tips stay below .70
        }
    }
    for (unsigned i=0;i<30;++i) {
        assert(data::kTips[i]<data::kVertexCount);
        for (unsigned j=0;j<i;++j) assert(data::kTips[i]!=data::kTips[j]);
        assert(data::kVertices[data::kTips[i]].spike>27000); ++spike_tips;
    }
    for (const auto& f:data::kFaces) {
        assert(f.a<data::kVertexCount && f.b<data::kVertexCount && f.c<data::kVertexCount);
        const auto vec=[](unsigned i) {
            auto v=data::kVertices[i]; return detail::Vec{float(v.x),float(v.y),float(v.z)};
        };
        auto a=vec(f.a), b=vec(f.b), c=vec(f.c);
        auto n=detail::cross(detail::sub(b,a),detail::sub(c,a));
        assert(n.x*a.x+n.y*a.y+n.z*a.z>0); // relocated tips retain outward winding
    }
    assert(spike_tips==30);
}
static void palette(uint32_t spot) {
    for (unsigned i=0;i<8;++i) {
        assert(ff::detail::frame.colors[i]==ff::detail::kGray[i]);
        assert((ff::detail::frame.colors[8+i]>>24)==255);
        for (unsigned s=0;s<24;s+=8) {
            const unsigned expected=((((ff::detail::kGray[i]>>s)&255)*65)+(((spot>>s)&255)*35)+50)/100;
            assert(((ff::detail::frame.colors[8+i]>>s)&255)==expected);
        }
    }
}
int main() {
    geometry(); reset(1000); assert(draw()==hash_bytes(pixels,sizeof pixels) && commands==0);
    capture(1000,0,false); const uint64_t idle=draw(); const float idle_tips=mean_tips();
    palette(0xffe89ab5); assert(ff::detail::motion.level==0 && ff::detail::motion.attack==0);
    const auto initial=ff::detail::frame;
    assert(draw()==idle); // repeated draw never advances rotation, colors or DSP
    capture(1010,32767,true,0xff123456); assert(draw()==idle);
    capture(1049,0,true,0xff123456); assert(draw()==idle);
    assert(!std::memcmp(&initial,&ff::detail::frame,sizeof initial));
    capture(1050,0,true,0xff123456); // short real peak latched between render updates
    const uint64_t impulse=draw(); palette(0xff123456);
    assert(impulse!=idle && mean_tips()>idle_tips*1.45f);
    assert(ff::detail::motion.level>.7f && ff::detail::motion.attack==1.f);
    float previous=ff::detail::motion.level;
    for (unsigned i=1;i<=40;++i) {
        capture(1050+i*50,0,false); draw();
        assert(ff::detail::motion.level<previous); previous=ff::detail::motion.level;
    }
    assert(ff::detail::motion.level<.0004f && ff::detail::motion.attack<.00001f);
    std::puts("pass real peak latch / attack / invalid-source release / 20Hz frozen draw");

    // Peak drive is calibrated to the real limiter ceiling, not unattainable
    // clipping. Long thin tips + smaller shoulders, never whole-ball inflation.
    reset(0); capture(0,16384);
    const float medium_tips=mean_tips(), medium_shoulders=mean_shoulders();
    for(unsigned i=0;i<ff::data::kVertexCount;++i) {
        const auto v=ff::data::kVertices[i];
        const auto m=ff::detail::motion;
        const float gentle=(60.f+5.f*m.level)*(v.bulk/16384.f)+
            (6.f+23.f*m.level+6.f*m.attack)*(v.spike/32768.f);
        assert(std::fabs(radius(i)-gentle)<.00005f); // quiet/moderate recipe intact
    }
    capture(50,30000); const float loud_tips=mean_tips(), loud_shoulders=mean_shoulders();
    draw();
    assert(loud_tips>medium_tips*1.20f && loud_shoulders<medium_shoulders*.80f);
    assert(loud_tips-loud_shoulders>3.f*(medium_tips-medium_shoulders));
    for(unsigned i=0;i<ff::data::kVertexCount;++i) {
        bool tip=false; for(unsigned t:ff::data::kTips) tip|=t==i;
        const float extension=radius(i)-48.f*(ff::data::kVertices[i].bulk/16384.f);
        if(tip) assert(extension>48.f && extension<=58.01f);
        else assert(std::fabs(extension)<.00005f); // no broad spike shoulders
    }
    for(unsigned ms=100;ms<=500;ms+=50) capture(ms,30000);
    assert(std::fabs(mean_tips()-loud_tips)<.00005f); // sustained loud stays extended
    capture(550,0);
    assert(mean_tips()<loud_tips && mean_tips()>medium_tips);
    for(unsigned ms=600;ms<=2500;ms+=50) {capture(ms,0,false);draw();}
    assert(std::fabs(mean_tips()-idle_tips)<.02f);
    std::printf("pass loud needles: tip/shoulder %.1f/%.1f -> %.1f/%.1f; "
                "30 narrow tips, real 30000 limiter peak, immediate attack / sustained / release\n",
                medium_tips,medium_shoulders,loud_tips,loud_shoulders);

    // No breathing/false attacks at silence: rigid slow turn preserves every
    // radius. A full turn, rather than a generated image, exercises all normals.
    reset(0); capture(0,0,false); float radii[ff::data::kVertexCount];
    for (unsigned i=0;i<ff::data::kVertexCount;++i) radii[i]=length(ff::detail::scratch.points[i]);
    bool rotated=false;
    const uint64_t opening=draw();
    for (unsigned ms=50;ms<=ff::kTurnMs;ms+=50) {
        capture(ms,0,false);
        assert(ff::detail::motion.level==0 && ff::detail::motion.attack==0);
        rotated|=draw()!=opening;
        for (unsigned i=0;i<ff::data::kVertexCount;++i)
            assert(std::fabs(length(ff::detail::scratch.points[i])-radii[i])<.00003f);
    }
    assert(rotated && draw()==opening);
    std::puts("pass silent 8s rigid turn / opaque body / native 4px bounds / bounded row runs");

    // Frame-rate independent envelope (the initial capture is one 50ms step).
    reset(0); capture(0,32767); capture(50,32767); capture(100,32767);
    const float regular=ff::detail::motion.level, regular_attack=ff::detail::motion.attack;
    reset(0); capture(0,32767); capture(100,32767);
    assert(std::fabs(regular-ff::detail::motion.level)<.00001f);
    assert(std::fabs(regular_attack-ff::detail::motion.attack)<.00001f);
    for (unsigned ms=150;ms<=16000;ms+=50) {
        // Real peak varying/sustained/zero inputs; never a production synthetic clock.
        const unsigned phase=(ms/400)%4;
        capture(ms,uint16_t(phase==0 ? 65535 : phase==1 ? 16384 : phase==2 ? 32767 : 0));
        draw();
        assert(ff::detail::motion.level>=0 && ff::detail::motion.level<=1);
        assert(ff::detail::motion.attack>=0 && ff::detail::motion.attack<=1);
        for (auto p:ff::detail::scratch.points) assert(length(p)<ff::data::kRadiusBound);
    }
    assert((tone_bits&0xff)==0xff && (tone_bits&0xff00));
    assert(max_commands<900); // measured host regression guard, not a GPU/FPS claim
    std::puts("pass elapsed envelope / clamped peak / all eight metal tones + spot reflection");

    // Spot colors are caller-owned frozen values, not lilac hardcoded in draw.
    reset(17000); capture(17000,32767,true,0x002aff10); const auto theme_a=ff::detail::frame;
    palette(0x002aff10); const uint64_t tint_a=draw();
    reset(17000); capture(17000,32767,true,0xfff01160); palette(0xfff01160);
    assert(!std::memcmp(theme_a.cells,ff::detail::frame.cells,sizeof theme_a.cells));
    assert(draw()!=tint_a); // alpha ignored, both outputs remain fully opaque
    reset(18000); assert(draw()==hash_bytes(pixels,sizeof pixels) && commands==0);
    capture(18000,0,false); assert(draw()==idle);
    capture(18010,32767); // pending peak must not survive missing/suspended frames
    capture(19000,0,false); assert(draw()==idle && ff::detail::motion.level==0);
    capture(19050,32767); assert(draw()!=idle);
    capture(1,0,false); assert(draw()==idle && ff::detail::motion.opened==1);
    capture(std::numeric_limits<uint64_t>::max(),0,false); assert(draw()==idle);
    capture(0,0,false); assert(draw()==idle);
    reset(50); capture(50,32767); capture(60,32767); capture(100,0,false);
    assert(ff::detail::motion.level<.7f); // invalid current source clears stale latch
    draw();
    std::printf("pass theme substitution / close-reopen / long-gap / backwards-clock / wrap reset; "
                "fixed RAM %u bytes, ROM data %u bytes; max observed %u rects, structural cap %u; "
                "no capture/draw heap; host only (no hardware timing claim)\n",
                ff::kMutableBytes,ff::kReadOnlyBytes,max_commands,ff::kMaxDrawRects);
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location('ferrofluid_data', ROOT/'tools/generate_nav_ferrofluid_data.py')
    generator = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(generator)
    assert generator.generate() == (ROOT/'platform/3ds/nav_ferrofluid_data.h').read_text(), 'generated data drift'
    header = (ROOT/'platform/3ds/nav_ferrofluid.h').read_text()
    drawing = header.split('inline void draw() {',1)[1]
    for forbidden in ('osGetTime', 'svcGet', 'palette::', 'engine', 'std::sin', 'capture(', 'render('):
        # comments describe the forbidden reads, so inspect only code text
        code = '\n'.join(line.split('//')[0] for line in drawing.splitlines())
        assert forbidden not in code
    flags = ['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie'] if args.sanitize else ['-O2']
    with tempfile.TemporaryDirectory(prefix='seance-nav-ferrofluid-') as tmp:
        unit, binary = Path(tmp)/'test.cpp', Path(tmp)/'test'
        unit.write_text(CPP)
        subprocess.run(['g++','-std=c++17',*flags,'-Wall','-Wextra','-Werror','-I'+str(ROOT),str(unit),
                        '-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc','-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)


if __name__ == '__main__':
    main()
