#!/usr/bin/env python3
"""Actual picker-motion C++ header: geometry, time and reversal; no ARM/device IO."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
CODE=r'''
#include "platform/3ds/picker_motion.h"
#include "platform/3ds/chain_columns.h"
#include <cassert>
#include <cstdio>
using namespace seance::platform::ui3ds;
namespace p=picker_motion;
int main(){
    int geometry=0,origins=0;
    const int order[]={0,1,2,3,4,5};
    for(int count=0;count<=6;++count)for(int selected=0;selected<=6;++selected){
        if(selected>=count && !(selected==6&&count<6))continue;
        const auto layout=chain_columns::layout(order,count,selected);
        const auto b=layout.box[selected];
        const bool active=selected<6;
        const int center=b.x+b.w/2;
        const p::Bounds origin=active?p::Bounds{b.x+2,0,b.x+b.w-3,197}:p::Bounds{center-1,22,center+1,48};
        int width=0;
        for(int i=0;i<=1000;++i){
            const float t=i/1000.f;
            const auto lo=p::bounds(origin,t,false),hi=p::bounds(origin,t,true);
            assert(lo.left>=0&&lo.right<320&&lo.top>=0&&lo.bottom<240);
            assert(hi.left==lo.left+40&&hi.right==lo.right+40&&hi.top>=8&&hi.bottom==239);
            assert(lo.right-lo.left+1>=width);width=lo.right-lo.left+1;
            if(!i)assert(lo.left==origin.left&&lo.right==origin.right&&lo.top==origin.top&&lo.bottom==origin.bottom);
            if(i==1000)assert(lo.left==20&&lo.right==299&&lo.top==0&&lo.bottom==216&&hi.top==8);
            ++geometry;
        }
        ++origins;
    }
    assert(origins==27);
    int reversals=0;
    for(int elapsed=0;elapsed<=200;++elapsed){
        p::begin(1000);assert(p::state.active&&!p::state.closing&&p::state.progress==0);
        p::sample(1000+elapsed);const float previous=p::state.progress;
        assert(std::abs(previous-elapsed/200.f)<.00001f);
        p::close(1000+elapsed);assert(p::state.progress==previous&&p::state.closing);
        float last=previous;bool done=false;
        for(int age=0;age<=160;++age){
            done=p::sample(1000+elapsed+age)||done;
            assert(p::state.progress<=last&&p::state.progress>=0);last=p::state.progress;
        }
        assert(done&&!p::state.active&&p::state.progress==0);
        p::begin(2000);p::sample(2007);p::snap();assert(p::state.progress==1&&!p::state.closing);
        p::sample(2200);assert(p::state.progress==1);p::close(2200);p::sample(2280);assert(p::state.progress==.5f);
        p::snap();assert(p::state.progress==.5f);p::sample(2360);assert(!p::state.active);
        p::reset();assert(!p::state.active&&!p::state.closing&&p::state.progress==0);
        ++reversals;
    }
    p::begin(500);p::sample(499);assert(p::state.progress==0);
    assert(geometry==27027&&reversals==201);
    printf("picker motion: %d origins / %d dual bounds / %d timed reversals; pass\n",origins,geometry,reversals);
}
'''
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def main():
    sources={str(p.relative_to(ROOT)):sha(p) for p in [Path(__file__).resolve(),ROOT/'platform/3ds/picker_motion.h',ROOT/'platform/3ds/chain_columns.h']}
    with tempfile.TemporaryDirectory(prefix='seance-picker-motion-') as tmp:
        source=Path(tmp)/'test.cpp';source.write_text(CODE)
        binary=Path(tmp)/'test'
        command=['g++','-std=c++17','-Wall','-Wextra','-Werror','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie','-I'+str(ROOT),str(source),'-o',str(binary)]
        subprocess.run(command,check=True,cwd=ROOT)
        result=subprocess.run([str(binary)],check=True,capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
        print(result.stdout,end='')
        assert all(sha(ROOT/p)==h for p,h in sources.items())
        out=ROOT/'release/module-picker/motion-proof.json'
        out.parent.mkdir(exist_ok=True,parents=True)
        out.write_text(json.dumps(dict(source_sha256=sources,command=command,summary=result.stdout.strip(),sanitizer='ASAN/UBSAN/leaks',hardware=False),indent=2)+'\n')

if __name__=='__main__':main()
