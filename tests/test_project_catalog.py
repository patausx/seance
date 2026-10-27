#!/usr/bin/env python3
"""Exact main.cpp catalogue functions against a temporary, counted filesystem.
No actual SD/save files, engine/audio build, rendering, ARM build, or deployment.
Case-folded enumeration is covered; host ext4 does not emulate FAT path lookup.
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def extract_function(text, name):
    match = re.search(
        r"^(?:static )?[\w:*&<>]+ " + re.escape(name)
        + r"\([^)]*\) \{.*?^}", text, re.M | re.S
    )
    assert match, f"missing runtime function: {name}"
    return match.group()


PREFIX = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <strings.h>
#include <sys/stat.h>
#include <vector>
#include "core/engine/project_library.h"
#include "core/engine/project_session.h"
#include "platform/3ds/project_names.h"
#include "platform/3ds/start_menu_store.h"
using namespace seance;
using namespace seance::platform::ui3ds;

struct Io {
    int dirs=0, entries=0, dir_closes=0, stats=0, opens=0, reads=0, closes=0;
    size_t requested_bytes=0;
    std::vector<std::string> paths;
    int total() const { return dirs+entries+dir_closes+stats+opens+reads+closes; }
};
static Io io;
static bool watching=false;
static int fail_opendir=0, fail_readdir_after=-1, fail_stat=0, fail_open=0;
static bool fail_read=false, fail_close=false, fail_dir_close=false, fail_stream=false;
extern "C" DIR* __real_opendir(const char*);
extern "C" struct dirent* __real_readdir(DIR*);
extern "C" int __real_closedir(DIR*);
extern "C" int __real_stat(const char*, struct stat*);
extern "C" FILE* __real_fopen(const char*, const char*);
extern "C" size_t __real_fread(void*, size_t, size_t, FILE*);
extern "C" int __real_fclose(FILE*);
extern "C" int __real_ferror(FILE*);
extern "C" DIR* __wrap_opendir(const char* path) {
    if (watching) {
        ++io.dirs; assert(std::string(path)=="sdmc:/3ds/seance");
        if (fail_opendir) { errno=fail_opendir; return nullptr; }
    }
    return __real_opendir(path);
}
extern "C" struct dirent* __wrap_readdir(DIR* dir) {
    if (watching) {
        ++io.entries;
        if (fail_readdir_after>=0 && io.entries>fail_readdir_after) {
            errno=EIO; return nullptr;
        }
    }
    return __real_readdir(dir);
}
extern "C" int __wrap_closedir(DIR* dir) {
    if (watching) ++io.dir_closes;
    const int result=__real_closedir(dir);
    if (watching && fail_dir_close) { errno=EIO; return -1; }
    return result;
}
extern "C" int __wrap_stat(const char* path, struct stat* st) {
    if (watching) {
        ++io.stats; io.paths.emplace_back(path);
        if (fail_stat) { errno=fail_stat; return -1; }
    }
    return __real_stat(path,st);
}
extern "C" FILE* __wrap_fopen(const char* path, const char* mode) {
    if (watching) {
        ++io.opens; io.paths.emplace_back(path); assert(!std::strcmp(mode,"rb"));
        if (fail_open) { errno=fail_open; return nullptr; }
    }
    return __real_fopen(path,mode);
}
extern "C" size_t __wrap_fread(void* out,size_t size,size_t count,FILE* file) {
    if (watching) {
        ++io.reads; io.requested_bytes+=size*count;
        if (fail_read) { errno=EIO; return 0; }
    }
    return __real_fread(out,size,count,file);
}
extern "C" int __wrap_fclose(FILE* file) {
    if (watching) ++io.closes;
    const int result=__real_fclose(file);
    if (watching && fail_close) { errno=EIO; return EOF; }
    return result;
}
extern "C" int __wrap_ferror(FILE* file) {
    if (watching && fail_stream) return 1;
    return __real_ferror(file);
}
template<class F> static void observe(F fn) {
    assert(!watching); io={}; watching=true; fn(); watching=false;
}
'''

TESTS = r'''
static const std::filesystem::path root="sdmc:/3ds/seance";
static void put(const std::string& name,const std::string& bytes=std::string("SNC7\7",5)) {
    assert(!watching);
    std::ofstream stream(root/name,std::ios::binary|std::ios::trunc);
    assert(stream); stream.write(bytes.data(),bytes.size()); stream.close(); assert(stream);
}
static void reset() {
    assert(!watching);
    std::filesystem::remove_all("sdmc:");
    std::filesystem::create_directories(root);
    g_patch_catalog_ready=g_patch_catalog_failed=false;
    g_start_store={}; g_project_names={}; g_project_session={};
    std::fill_n(g_patch_exists,PROJECT_SLOTS,false);
    std::fill_n(g_patch_valid,PROJECT_SLOTS,false);
    std::fill_n(g_patch_checked,PROJECT_SLOTS,false);
    std::fill_n(g_patch_saved_at,PROJECT_SLOTS,0);
    fail_opendir=fail_stat=fail_open=0; fail_readdir_after=-1;
    fail_read=fail_close=fail_dir_close=fail_stream=false; io={};
}
struct Cache {
    std::array<bool,PROJECT_SLOTS> exists, valid, checked;
    std::array<uint64_t,PROJECT_SLOTS> saved_at;
    Cache() {
        std::copy_n(g_patch_exists,PROJECT_SLOTS,exists.begin());
        std::copy_n(g_patch_valid,PROJECT_SLOTS,valid.begin());
        std::copy_n(g_patch_checked,PROJECT_SLOTS,checked.begin());
        std::copy_n(g_patch_saved_at,PROJECT_SLOTS,saved_at.begin());
    }
    void same_except(int skip=-1) const {
        for (int i=0;i<PROJECT_SLOTS;++i) if (i!=skip) {
            assert(exists[i]==g_patch_exists[i]); assert(valid[i]==g_patch_valid[i]);
            assert(checked[i]==g_patch_checked[i]); assert(saved_at[i]==g_patch_saved_at[i]);
        }
    }
};
static void check_scan_io() {
    assert(io.dirs==1 && io.dir_closes==1 && io.entries>0);
    assert(io.stats==0 && io.opens==0 && io.reads==0 && io.closes==0 && io.paths.empty());
    assert(g_patch_catalog_ready);
}
static void check_cached_scan() {
    const Cache before;
    observe([] { for (int i=0;i<4;++i) scan_patch_slots(); });
    assert(io.total()==0); before.same_except(); assert(g_patch_catalog_ready);
}
static void check_refresh_io(int slot,int opens=1,int reads=1,int closes=1) {
    assert(io.dirs==0 && io.entries==0 && io.dir_closes==0);
    assert(io.stats==1 && io.opens==opens && io.reads==reads && io.closes==closes);
    assert(io.requested_bytes==size_t(reads*5));
    const std::string expected=(root/("patch"+std::to_string(slot)+".snc")).string();
    assert(io.paths.size()==size_t(1+opens));
    for (const auto& path:io.paths) assert(path==expected);
    assert(g_patch_checked[slot]);
}
static void check_fail_closed() {
    assert(g_patch_catalog_ready && g_patch_catalog_failed);
    for (int i=0;i<PROJECT_SLOTS;++i)
        assert(g_patch_exists[i] && !g_patch_valid[i] && g_patch_checked[i] && !g_patch_saved_at[i]);
}
static void test_names() {
    for (int i=0;i<PROJECT_SLOTS;++i) {
        const std::string name="patch"+std::to_string(i)+".snc";
        assert(patch_slot_from_name(name.c_str())==i);
    }
    assert(patch_slot_from_name("PATCH0.SNC")==0);
    assert(patch_slot_from_name("pAtCh127.SnC")==127);
    for (const char* bad:{"", "patch.snc", "patch00.snc", "patch01.snc", "patch001.snc",
         "patch128.snc", "patch999999999999999999999999.snc", "patch-1.snc", "patch+1.snc",
         "patch1.snc.bak", "patch1.snc.tmp", "patch1.snc/", "patch1.SNC ", "patch1snc",
         " patch1.snc", "xpatch1.snc", "patch1..snc", "patch1.sncx", "patch1.snc\n"})
        assert(patch_slot_from_name(bad)==-1);
}
static void test_enumeration() {
    reset(); observe([] { scan_patch_slots(); }); check_scan_io();
    for (int i=0;i<PROJECT_SLOTS;++i)
        assert(!g_patch_exists[i] && g_patch_checked[i] && !g_patch_valid[i] && !g_patch_saved_at[i]);
    put("patch127.snc"); check_cached_scan(); assert(!g_patch_exists[127]);
    const Cache empty;
    observe([] { refresh_patch_slot(127); }); check_refresh_io(127); empty.same_except(127);
    assert(g_patch_exists[127] && g_patch_valid[127] && g_patch_saved_at[127]>0);
    check_cached_scan();

    reset();
    for (int i=0;i<PROJECT_SLOTS;++i) put("patch"+std::to_string(i)+".snc");
    observe([] { scan_patch_slots(); }); check_scan_io();
    assert(io.entries<=PROJECT_SLOTS+3); // entries + . / .. + EOF; no second enumeration
    for (int i=0;i<PROJECT_SLOTS;++i)
        assert(g_patch_exists[i] && !g_patch_valid[i] && !g_patch_checked[i] && !g_patch_saved_at[i]);
    check_cached_scan();
    const Cache full;
    observe([] { refresh_patch_slot(63); }); check_refresh_io(63); full.same_except(63);
    assert(g_patch_valid[63]);
    struct stat st{}; assert(!__real_stat((root/"patch63.snc").c_str(),&st));
    assert(g_patch_saved_at[63]==uint64_t(st.st_mtime)); check_cached_scan();
    std::filesystem::remove(root/"patch63.snc");
    const Cache removed;
    observe([] { refresh_patch_slot(63); }); check_refresh_io(63,0,0,0); removed.same_except(63);
    assert(!g_patch_exists[63] && !g_patch_valid[63] && !g_patch_saved_at[63]);
    check_cached_scan();

    reset();
    for (const char* name:{"patch0.snc","patch8.snc","PATCH63.SNC","pAtCh127.SnC",
         "patch00.snc","patch001.snc","patch128.snc","patch-1.snc","patch17.snc.bak",
         "patch18.snc.tmp","patch19.snc ","other.snc"}) put(name);
    std::filesystem::create_directory(root/"patch42.snc");
    observe([] { scan_patch_slots(); }); check_scan_io();
    for (int i=0;i<PROJECT_SLOTS;++i) {
        assert(g_patch_exists[i]==(i==0 || i==8 || i==42 || i==63 || i==127));
        assert(g_patch_checked[i]==(!g_patch_exists[i] || i==42));
        assert(!g_patch_valid[i] && !g_patch_saved_at[i]);
    }
    const Cache sparse;
    observe([] { refresh_patch_slot(42); }); check_refresh_io(42,0,0,0); sparse.same_except(42);
    assert(g_patch_exists[42] && !g_patch_valid[42]); check_cached_scan();
    std::puts("pass catalogue: empty/full128/sparse, canonical/case-fold names, zero header IO, cached repeat zero IO, target-only add/delete/directory refresh");
}
static void test_headers_and_errors() {
    reset(); put("patch31.snc"); observe([] { scan_patch_slots(); }); check_scan_io();
    for (int version=1;version<=7;++version) {
        std::string header="SNC"; header+=char('0'+version); header+=char(version);
        put("patch31.snc",header+std::string(100,'x'));
        const Cache before;
        observe([] { refresh_patch_slot(31); }); check_refresh_io(31); before.same_except(31);
        assert(g_patch_exists[31] && g_patch_valid[31]);
    }
    const std::string good("SNC7\7",5);
    for (int size=0;size<5;++size) {
        put("patch31.snc",good.substr(0,size));
        const Cache before;
        observe([] { refresh_patch_slot(31); }); check_refresh_io(31); before.same_except(31);
        assert(g_patch_exists[31] && !g_patch_valid[31]);
    }
    for (const std::string& bad:{std::string("BAD7\7",5),std::string("SNC0\0",5),
         std::string("SNC8\10",5),std::string("SNC7\6",5),std::string("SNC1\7",5)}) {
        put("patch31.snc",bad); const Cache before;
        observe([] { refresh_patch_slot(31); }); check_refresh_io(31); before.same_except(31);
        assert(g_patch_exists[31] && !g_patch_valid[31]);
    }
    put("patch31.snc");
    for (int error:{EACCES,EIO,ENOENT}) {
        fail_stat=error; const Cache before;
        observe([] { refresh_patch_slot(31); }); check_refresh_io(31,0,0,0); before.same_except(31);
        assert(g_patch_exists[31]==(error!=ENOENT) && !g_patch_valid[31] && !g_patch_saved_at[31]);
    }
    fail_stat=0; fail_open=EACCES;
    const Cache unreadable;
    observe([] { refresh_patch_slot(31); }); check_refresh_io(31,1,0,0); unreadable.same_except(31);
    assert(g_patch_exists[31] && !g_patch_valid[31]);
    fail_open=0; fail_read=true;
    const Cache read_error;
    observe([] { refresh_patch_slot(31); }); check_refresh_io(31); read_error.same_except(31);
    assert(g_patch_exists[31] && !g_patch_valid[31]);
    fail_read=false;
    for (bool* failure:{&fail_close,&fail_stream}) {
        *failure=true; const Cache before;
        observe([] { refresh_patch_slot(31); }); check_refresh_io(31); before.same_except(31);
        assert(g_patch_exists[31] && !g_patch_valid[31]); *failure=false;
    }
    const Cache repaired;
    observe([] { refresh_patch_slot(31); }); check_refresh_io(31); repaired.same_except(31);
    assert(g_patch_valid[31]);
    const Cache invalid;
    observe([] { refresh_patch_slot(-1); refresh_patch_slot(PROJECT_SLOTS); });
    assert(io.total()==0); invalid.same_except(); check_cached_scan();
    std::puts("pass catalogue: exact 5-byte SNC1-7 headers, truncation/bad header, stat/open/read errors, invalid targets, isolated cache updates");
}
static void test_scan_errors() {
    for (int error:{EACCES,EIO,ENOENT}) {
        reset(); put("patch7.snc"); fail_opendir=error;
        observe([] { scan_patch_slots(); });
        assert(io.dirs==1 && io.entries==0 && io.dir_closes==0 && io.stats==0 && io.opens==0);
        check_fail_closed(); fail_opendir=0; check_cached_scan();
        const Cache before;
        observe([] { refresh_patch_slot(7); }); check_refresh_io(7); before.same_except(7);
        assert(g_patch_valid[7]); check_cached_scan();
    }
    reset(); put("patch0.snc"); put("patch127.snc"); fail_readdir_after=3;
    observe([] { scan_patch_slots(); }); check_scan_io(); assert(io.entries==4);
    check_fail_closed(); fail_readdir_after=-1; check_cached_scan();
    reset(); put("patch7.snc"); fail_dir_close=true;
    observe([] { scan_patch_slots(); }); check_scan_io();
    check_fail_closed(); fail_dir_close=false; check_cached_scan();
    std::puts("pass catalogue: opendir/readdir/closedir failure closed, partial enumeration discarded, no repeated scan/retry, single-target recovery only");
}
static void test_startup() {
    reset(); put("patch0.snc"); put("patch63.snc","BAD!!"); put("patch127.snc");
    std::filesystem::create_directory(root/"patch42.snc");
    start_store_touch(g_start_store,0,100);
    start_store_touch(g_start_store,63,200);
    start_store_touch(g_start_store,127,300);
    g_project_session.reset_loaded(127);
    assert(project_name_set(g_project_names,127,"last project"));
    StartMenuData data;
    observe([&] { data=startup_data(); }); check_scan_io();
    assert(data.active_slot==127 && data.recent_count==3);
    assert(data.recent_slots[0]==127 && data.recent_slots[1]==63 && data.recent_slots[2]==0);
    assert(!std::strcmp(data.projects[127].name,"last project"));
    for (int i=0;i<PROJECT_SLOTS;++i) {
        assert(data.projects[i].exists==(i==0 || i==42 || i==63 || i==127));
        assert(data.projects[i].valid==(i==0 || i==63 || i==127));
    }
    observe([&] { data=startup_data(); }); assert(io.total()==0);
    observe([] { refresh_patch_slot(63); }); check_refresh_io(63);
    observe([&] { data=startup_data(); }); assert(io.total()==0);
    assert(!data.projects[63].valid && data.recent_count==2);
    assert(data.recent_slots[0]==127 && data.recent_slots[1]==0);
    reset(); put("patch127.snc"); fail_opendir=EIO;
    start_store_touch(g_start_store,127,300);
    observe([&] { data=startup_data(); }); assert(io.dirs==1 && io.stats==0 && io.opens==0);
    check_fail_closed(); assert(data.recent_count==0);
    for (const auto& project:data.projects) assert(project.exists && !project.valid);
    fail_opendir=0; observe([&] { data=startup_data(); }); assert(io.total()==0);
    // Legacy metadata is the only extra open: still no patch-header probing.
    reset(); put("patch127.snc"); put("last_patch.bin",std::string("SLOT",4)+char(127));
    observe([&] { data=startup_data(); });
    assert(io.dirs==1 && io.dir_closes==1 && io.stats==0 && io.opens==1 && io.reads==1);
    assert(io.requested_bytes==5 && io.paths.size()==1 && io.paths[0]==LAST_PATCH_PATH);
    assert(data.recent_count==1 && data.recent_slots[0]==127);
    std::puts("pass startup_data: exact runtime/real MRU+names, unchecked candidates, checked-invalid/directory/error rejection, repeated zero IO with MRU, legacy metadata only");
}
int main() {
    assert(!g_patch_catalog_ready);
    test_names(); test_enumeration(); test_headers_and_errors(); test_scan_errors(); test_startup();
    std::puts("project catalogue: host filesystem boundaries only; no controller/device latency or FAT case-lookup claim");
}
'''


def main():
    runtime = (ROOT / "platform/3ds/main.cpp").read_text()
    globals_ = []
    for name in ("g_patch_exists", "g_patch_valid", "g_patch_checked",
                 "g_patch_saved_at", "g_patch_catalog_ready", "g_patch_catalog_failed"):
        match = re.search(r"^(?:static )?(?:bool|uint64_t) " + name + r"\b[^;]+;", runtime, re.M)
        assert match, f"missing runtime global: {name}"
        globals_.append(match.group())
    for declaration in ("ProjectSession g_project_session;", "ProjectNames g_project_names;",
                        "StartMenuStore g_start_store;"):
        assert declaration in runtime
        globals_.append(declaration)
    match = re.search(r'^const char\* LAST_PATCH_PATH = [^;]+;', runtime, re.M)
    assert match
    globals_.append(match.group())
    header = (ROOT / "platform/3ds/ui_start_menu.h").read_text()
    for name in ("StartProject", "StartMenuData"):
        match = re.search(r'^struct ' + name + r' \{.*?^};', header, re.M | re.S)
        assert match
        globals_.append(match.group())
    functions = [extract_function(runtime, name) for name in (
        "patch_path", "patch_slot_from_name", "scan_patch_slots", "refresh_patch_slot",
        "start_timestamp", "load_last_patch_slot", "startup_data")]
    cpp = PREFIX + "\n".join(globals_ + functions) + TESTS
    with tempfile.TemporaryDirectory(prefix="seance-project-catalog-") as temp:
        source, binary = Path(temp) / "test.cpp", Path(temp) / "test"
        source.write_text(cpp)
        subprocess.run([
            "g++", "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-no-pie",
            "-I", str(ROOT), str(source), "-o", str(binary),
            str(ROOT / "core/engine/project_session.cpp"),
            str(ROOT / "platform/3ds/project_names.cpp"),
            str(ROOT / "platform/3ds/start_menu_store.cpp"),
            *["-Wl,--wrap=" + name for name in (
                "opendir", "readdir", "closedir", "stat", "fopen", "fread", "fclose", "ferror")],
        ], cwd=temp, check=True)
        subprocess.run([str(binary)], cwd=temp, check=True)


if __name__ == "__main__":
    main()
