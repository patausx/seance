#!/usr/bin/env python3
"""One focused host check: legacy SNMU, 128 slots/session, title format and recovery.
Temporary files only; no real SD, engine/audio build, media, or deployment.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = r'''
#define main legacy_start_store_test
#include "tests/start_menu_store_test.cpp"
#undef main
#include "platform/3ds/project_names.h"
#include "core/engine/project_session.h"
#include <vector>
#include <cerrno>
using namespace seance;
static bool fail_commit = false;
extern "C" int __real_rename(const char*, const char*);
extern "C" int __wrap_rename(const char* from, const char* to) {
    const size_t n = std::strlen(from);
    if (fail_commit && n >= 4 && !std::strcmp(from+n-4,".tmp")) {
        fail_commit=false; errno=EACCES; return -1;
    }
    return __real_rename(from,to);
}
static std::vector<uint8_t> read_bytes(const std::string& path) {
    FILE* f=std::fopen(path.c_str(),"rb"); assert(f);
    std::vector<uint8_t> b; int c;
    while ((c=std::fgetc(f))!=EOF) b.push_back(uint8_t(c));
    assert(!std::ferror(f)); assert(!std::fclose(f)); return b;
}
static void put(const std::string& path,const std::vector<uint8_t>& bytes) {
    FILE* f=std::fopen(path.c_str(),"wb"); assert(f);
    assert(std::fwrite(bytes.data(),1,bytes.size(),f)==bytes.size()); assert(!std::fclose(f));
}
static void reseal(std::vector<uint8_t>& b) {
    uint32_t h=2166136261u;
    for(size_t i=0;i<b.size()-4;++i) h=(h^b[i])*16777619u;
    for(int i=0;i<4;++i) b[b.size()-4+i]=uint8_t(h>>(8*i));
}
int main() {
    assert(legacy_start_store_test()==0);
    ProjectSession session;
    for(int slot : {0,7,8,31,63,96,127}) {
        session.mark_mutated(); session.mark_saved(slot);
        assert(!session.dirty()&&session.active_slot()==slot);
        session.mark_mutated(); assert(!session.request(DestructiveAction::LoadProject,slot));
        assert(session.take_pending().slot==slot);
        session.reset_loaded(slot); assert(!session.dirty()&&session.active_slot()==slot);
    }
    session.reset_loaded(128); assert(session.active_slot()==-1);
    session.mark_saved(-1); assert(session.active_slot()==-1);
    StartMenuStore store, decoded;
    for(int slot=0;slot<PROJECT_SLOTS;++slot) start_store_touch(store,slot,100+slot);
    assert(store.count==8&&store.recent[0].slot==127&&store.recent[7].slot==120);
    uint8_t bytes[START_STORE_BYTES]; assert(start_store_encode(store,bytes));
    assert(bytes[4]==1&&bytes[8]==127&&sizeof bytes==84);
    assert(start_store_decode(bytes,sizeof bytes,decoded)&&decoded.recent[7].slot==120);
    store.recent[1].slot=127; assert(!start_store_encode(store,bytes)); store=decoded;
    bool valid[PROJECT_SLOTS]={}; uint64_t times[PROJECT_SLOTS]={}; int out[3];
    valid[127]=valid[63]=valid[7]=true; times[63]=100; times[7]=200;
    assert(start_store_recent(store,valid,times,63,out)==3);
    assert(out[0]==127&&out[1]==63&&out[2]==7);
    bool old_valid[8]={}; uint64_t old_times[8]={}; old_valid[7]=true;
    assert(start_store_recent(store,old_valid,old_times,127,out)==1&&out[0]==7);
    start_store_forget(store,127); assert(store.recent[0].slot==126);

    ProjectNames names;
    char label[PROJECT_NAME_BYTES]; project_name_label(names,0,label,sizeof label);
    assert(!std::strcmp(label,"patch 001")); project_name_label(names,127,label,sizeof label);
    assert(!std::strcmp(label,"patch 128"));
    assert(project_name_set(names,127,"  LAST Breath  ")&&!std::strcmp(names.names[127],"last breath"));
    assert(project_name_set(names,0,"12345678901234567890123"));
    assert(project_name_set(names,1,"  A  B-9!?  ")&&!std::strcmp(names.names[1],"a  b-9!?"));
    const auto unchanged=names;
    for(const char* bad : {"", "   ", "new\nname", "a\t", "caf\xc3\xa9", "123456789012345678901234", "abcdefghijklmnopqrstuvw x"}) {
        assert(!project_name_set(names,127,bad)); assert(!std::memcmp(&names,&unchanged,sizeof names));
    }
    assert(!project_name_set(names,128,"bad")&&!project_name_set(names,-1,"bad")&&!project_name_set(names,0,nullptr));
    char tiny[2]={'x','x'}; project_name_label(names,127,tiny,sizeof tiny); assert(tiny[1]==0);
    project_name_label(names,128,label,sizeof label); assert(!std::strcmp(label,"unsaved"));
    char dir[]="/tmp/seance-project-names-XXXXXX"; assert(mkdtemp(dir));
    const std::string path=std::string(dir)+"/project_names.bin";
    assert(project_names_save(path.c_str(),names));
    auto raw=read_bytes(path); assert(raw.size()==3084&&!std::memcmp(raw.data(),"SNPN\1\30\200\0",8));
    ProjectNames loaded; assert(project_names_load(path.c_str(),loaded)&&!std::memcmp(&loaded,&names,sizeof names));
    // Exact file length and checksum/version/slot-count/canonical padding reject transactionally.
    for(int which=0;which<9;++which) {
        auto bad=raw;
        if(which==0) bad.pop_back();
        if(which==1) bad.push_back(0);
        if(which==2) bad[100]^=1;
        if(which==3) {bad[4]=2; reseal(bad);}
        if(which==4) {bad[6]=127; reseal(bad);}
        if(which==5) {bad[8]='A'; reseal(bad);}
        if(which==6) {bad[8]=' '; reseal(bad);}
        if(which==7) {bad[8+2*PROJECT_NAME_BYTES+1]='a'; reseal(bad);}
        if(which==8) {std::memset(bad.data()+8,'x',PROJECT_NAME_BYTES); reseal(bad);}
        put(path,bad); assert(!project_names_load(path.c_str(),loaded));
        assert(!std::memcmp(&loaded,&names,sizeof names));
    }
    put(path,raw);
    assert(project_name_set(names,127,"new version"));
    fail_commit=true; assert(!project_names_save(path.c_str(),names)&&!fail_commit);
    assert(project_names_load(path.c_str(),loaded)&&!std::strcmp(loaded.names[127],"last breath"));
    assert(project_names_save(path.c_str(),names));
    assert(project_names_load((path+".bak").c_str(),loaded)&&!std::strcmp(loaded.names[127],"last breath"));
    auto backup=read_bytes(path+".bak"); auto bad=raw; bad[0]='x'; put(path,bad);
    assert(project_names_save(path.c_str(),names)&&read_bytes(path+".bak")==backup);
    assert(project_names_load(path.c_str(),loaded)&&!std::strcmp(loaded.names[127],"new version"));
    // Blocked temp/backup paths and invalid caller state leave primary intact.
    const auto primary=read_bytes(path);
    assert(!mkdir((path+".tmp").c_str(),0700));
    assert(!project_names_save(path.c_str(),names)&&read_bytes(path)==primary);
    assert(!rmdir((path+".tmp").c_str()));
    assert(!std::remove((path+".bak").c_str())); assert(!mkdir((path+".bak").c_str(),0700));
    assert(!project_names_save(path.c_str(),names)&&read_bytes(path)==primary);
    assert(!rmdir((path+".bak").c_str()));
    names.names[0][0]='A'; assert(!project_names_save(path.c_str(),names)&&read_bytes(path)==primary);
    project_name_clear(names,127); project_name_label(names,127,label,sizeof label);
    assert(!std::strcmp(label,"patch 128"));
    assert(!project_names_load(nullptr,loaded)&&!project_names_save(nullptr,names));
    assert(!std::remove(path.c_str())); assert(!mkdir(path.c_str(),0700));
    names=unchanged; assert(!project_names_save(path.c_str(),names));
    struct stat st{}; assert(!stat(path.c_str(),&st)&&S_ISDIR(st.st_mode));
    assert(!rmdir(path.c_str())); assert(!rmdir(dir));
    std::puts("project library: 128-slot session/MRU, legacy8 caller, canonical titles, exact format, no-mutation decode, failed-commit rollback and backup protection passed");
}
'''


def main():
    with tempfile.TemporaryDirectory(prefix="seance-library-store-") as temp:
        source = Path(temp) / "test.cpp"
        binary = Path(temp) / "test"
        source.write_text(SOURCE)
        subprocess.run([
            "g++", "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-no-pie",
            "-I", str(ROOT), str(source),
            "platform/3ds/start_menu_store.cpp", "platform/3ds/project_names.cpp",
            "core/engine/project_session.cpp", "-Wl,--wrap=rename", "-o", str(binary),
        ], cwd=ROOT, check=True)
        subprocess.run([str(binary)], cwd=ROOT, check=True)


if __name__ == "__main__":
    main()
