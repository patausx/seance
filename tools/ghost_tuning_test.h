// Focused Ghost regression; included by the portable host suite.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "../core/engine/units.h"
#include "../core/engine/engine.h"
#include "../core/engine/patch_io.h"
#include <cmath>
#include <memory>
#include <algorithm>

static uint64_t ghost_legacy_fingerprint() {
    using namespace seance;
    uint64_t hash = 1469598103934665603ULL;
    for (int scenario = 0; scenario < 32; ++scenario) {
        GhostUnit g;
        g.long_tail = false; // installed legacy algorithm is the compatibility oracle
        for (int p = 0; p < g.num_params; ++p) g.params[p].effective = g.params[p].base;
        g.params[0].effective = (scenario & 1) ? 32767 : 0;
        g.params[1].effective = (scenario & 2) ? 32767 : 24000;
        g.params[2].effective = (scenario & 4) ? 32767 : 0;
        g.params[3].effective = (scenario & 8) ? 32767 : 4096;
        g.params[5].effective = 32767;
        uint32_t rng = 9173;
        for (int block = 0; block < 1000; ++block) {
            seance::q15 audio[128];
            for (int i = 0; i < 128; ++i) {
                rng = rng * 1664525u + 1013904223u;
                audio[i] = block < 100 ? (seance::q15)(rng >> 16) : 0;
            }
            g.params[4].effective = ((scenario & 16) && block >= 200 && block < 800) ? 32767 : 0;
            g.process(audio, 128);
            for (auto sample : audio) { hash ^= (uint16_t)sample; hash *= 1099511628211ULL; }
        }
    }
    return hash;
}

static bool test_ghost_tuning() {
    using namespace seance;
    #define GHOST_CHECK(x) do { if (!(x)) { std::printf("FAIL: ghost tuning line %d: %s\n", __LINE__, #x); return false; } } while (0)
    GHOST_CHECK(ghost_legacy_fingerprint() == 0x0cdc7d5bda817128ULL);
    GHOST_CHECK(GhostUnit::drift_increment(0) == 53687);
    GHOST_CHECK(GhostUnit::drift_period_ms(32767) == 120000);
    uint32_t previous = 0;
    for (int v=0;v<=32767;++v) {
        const auto period=GhostUnit::drift_period_ms((q15)v);
        GHOST_CHECK(period>=previous); previous=period;
        const double actual=4294967296.0/(32000*GhostUnit::drift_increment((q15)v));
        GHOST_CHECK(std::abs(actual-period/1000.0)<0.12);
    }
    for(int v=-32768;v<=32767;++v) {
        const double expected=std::pow(2.0,-(double)v/32767/24)*(1<<24);
        GHOST_CHECK(std::abs(GhostUnit::fine_delay_ratio((q15)v)-expected)<4);
    }
    // Same captured memory must ignore all six dormant controls and new input.
    GhostUnit a,b;
    q15 x[128],y[128];
    for (int p=0;p<a.num_params;++p) a.params[p].effective=b.params[p].effective=a.params[p].base;
    a.params[5].effective=b.params[5].effective=32767;
    for(int block=0;block<300;++block) {
        for(int i=0;i<128;++i)x[i]=y[i]=(block<30)?(q15)((i*197+block*971)%24000-12000):0;
        if(block>=40)a.params[4].effective=b.params[4].effective=32767;
        if(block>=50) {
            for(int p: {0,1,2,3,6,7})b.params[p].effective=(q15)((block&1)?32767:0);
            std::fill(y,y+128,32767);
        }
        a.process(x,128);b.process(y,128);
        // q15 wet mix leaves at most one LSB of input at 32767: compare identical input.
        if(block<50)GHOST_CHECK(!std::memcmp(x,y,sizeof x));
        else for(int i=0;i<128;++i)GHOST_CHECK(std::abs((int)x[i]-y[i])<=1);
    }
    // Reset a/b and test exact sealed output with matching inputs, changed controls.
    a.reset();b.reset();
    for(int p=0;p<a.num_params;++p)a.params[p].effective=b.params[p].effective=a.params[p].base;
    for(int block=0;block<180;++block){
        for(int i=0;i<128;++i)x[i]=y[i]=block<20?(q15)(i*113):0;
        if(block>=30)a.params[4].effective=b.params[4].effective=32767;
        if(block>=40)for(int p:{0,1,2,3,6,7})b.params[p].effective=(q15)((block&1)?32767:0);
        a.process(x,128);b.process(y,128);GHOST_CHECK(!std::memcmp(x,y,sizeof x));
    }
    // Distinct fine values change PCM; a one-pixel gesture is not lost in q8 delays.
    a.reset();b.reset();
    for(int p=0;p<a.num_params;++p)a.params[p].effective=b.params[p].effective=a.params[p].base;
    b.params[6].effective=64;bool changed=false;
    for(int block=0;block<50;++block){
        for(int i=0;i<128;++i)x[i]=y[i]=block<10?(q15)(i*113):0;
        a.process(x,128);b.process(y,128);changed|=std::memcmp(x,y,sizeof x)!=0;
    }
    GHOST_CHECK(changed);
    a.params[6].base=3000;a.params[6].snap();a.params[6].resolve(0);
    a.params[6].base=0;
    for(int i=0;i<100;++i){a.params[6].resolve(0);a.control_tick();}
    GHOST_CHECK(a.params[6].effective==0);
    // Exercise maximum decay/drift and continuous full-scale input under sanitizer.
    for (int fine : {-32767,0,32767}) for(int tone:{0,32767}) {
        GhostUnit g;
        for(auto& p:g.params)p.effective=32767;
        g.params[0].effective=(q15)tone;g.params[4].effective=0;g.params[6].effective=(q15)fine;
        for(int block=0;block<3000;++block){std::fill(x,x+128,32767);g.process(x,128);for(auto sample:x)GHOST_CHECK(std::abs((int)sample)<20000);}
    }
    auto e=std::make_unique<Engine>();auto loaded=std::make_unique<Engine>();
    auto* g=new GhostUnit;delete e->replace_unit(0,0,g);
    g->params[6].base=-21000;g->params[6].src=ModSource::SigilC;g->params[6].mod_depth=7000;
    g->params[7].base=29000;g->params[7].src=ModSource::EnvFollow;g->params[7].mod_depth=-1000;
    std::unique_ptr<Unit> clone(clone_unit(g));
    GHOST_CHECK(clone && clone->params[6].base==-21000 && clone->params[7].base==29000);
    const char* path="/tmp/seance_ghost_tuning.snc";
    GHOST_CHECK(patchio::save(*e,path) && patchio::load(*loaded,path));
    for(int p=0;p<8;++p){auto& got=loaded->chains[0].units[0]->params[p];GHOST_CHECK(got.base==g->params[p].base&&got.src==g->params[p].src&&got.mod_depth==g->params[p].mod_depth);}
    std::remove(path);
    // Old SNC7's unused slots were all zero: load must retain the old transfer.
    for(int p=6;p<PARAM_BANK_SIZE;++p){g->params[p].base=0;g->params[p].src=ModSource::None;g->params[p].mod_depth=0;}
    GHOST_CHECK(patchio::save(*e,path)&&patchio::load(*loaded,path));
    GHOST_CHECK(loaded->chains[0].units[0]->params[6].base==0&&loaded->chains[0].units[0]->params[7].base==0);
    std::remove(path);
    std::printf("ok: ghost old PCM / fine / slow drift / freeze / persistence\n");
    #undef GHOST_CHECK
    return true;
}
