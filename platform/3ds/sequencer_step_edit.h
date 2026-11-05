#pragma once
#include "../../core/engine/engine.h"
#include <cstdint>

namespace seance::platform::ui3ds::seq_edit {
constexpr uint64_t bit(int step) { return uint64_t(1) << step; }
inline int count(uint64_t mask) { return __builtin_popcountll(mask); }
inline int first(uint64_t mask) { return mask ? __builtin_ctzll(mask) : 0; }
inline uint64_t range(int a, int b) {
    if (a > b) { const int t=a; a=b; b=t; }
    return (~uint64_t(0) << a) & (~uint64_t(0) >> (63-b));
}
enum class Scope : uint8_t { Steps, Locks, Parameter };
inline const char* scope_name(Scope s) {
    return s==Scope::Steps ? "steps" : s==Scope::Locks ? "locks" : "param";
}
struct Row {
    SeqStep steps[SEQ_STEPS]{};
    int8_t timing[SEQ_STEPS]{};
    void read(const Pattern& p, int chain) {
        for (int s=0;s<SEQ_STEPS;++s) { steps[s]=p.steps[chain][s]; timing[s]=p.microtiming[chain][s]; }
    }
    void write(Pattern& p, int chain) const {
        for (int s=0;s<SEQ_STEPS;++s) { p.steps[chain][s]=steps[s]; p.microtiming[chain][s]=timing[s]; }
    }
};
inline int find(const SeqStep& s, int slot, int param) {
    for (int i=0;i<s.lock_count && i<SEQ_LOCKS;++i)
        if (s.locks[i].slot==slot && s.locks[i].param==param) return i;
    return -1;
}
inline bool equal(const Row& a, const Row& b) {
    for (int s=0;s<SEQ_STEPS;++s) {
        const auto& x=a.steps[s]; const auto& y=b.steps[s];
        if (x.flags!=y.flags || x.lock_count!=y.lock_count || a.timing[s]!=b.timing[s]) return false;
        for (int i=0;i<x.lock_count && i<SEQ_LOCKS;++i)
            if (x.locks[i].slot!=y.locks[i].slot || x.locks[i].param!=y.locks[i].param || x.locks[i].value!=y.locks[i].value) return false;
    }
    return true;
}
inline void remove(SeqStep& s, int slot, int param) {
    const int i=find(s,slot,param);
    if (i<0) return;
    for (int j=i+1;j<s.lock_count && j<SEQ_LOCKS;++j) s.locks[j-1]=s.locks[j];
    --s.lock_count; s.locks[s.lock_count]={};
}
// Caller works on a draft row; false never touches the live pattern.
inline bool put(SeqStep& s, int slot, int param, q15 value, q15 base, bool explicit_base=false) {
    const int i=find(s,slot,param);
    if (i>=0) { s.locks[i].value=value; return true; }
    // A written pitch equal to base returns from the preceding held note.
    // Ordinary parameter no-ops still consume no lock capacity.
    if (value==base && !explicit_base) return true;
    if (s.lock_count>=SEQ_LOCKS) return false;
    s.locks[s.lock_count++]={(uint8_t)slot,(uint8_t)param,value}; return true;
}
struct Selection {
    uint64_t mask=0, seed=0;
    int anchor=-1;
    bool touch=false, adding=false;
    uint64_t targets(int focus) const { return mask ? mask : bit(focus); }
    void clear() { mask=seed=0; anchor=-1; touch=false; }
    void begin_touch(int step) {
        seed=mask; anchor=step; adding=!(mask&bit(step)); touch=true;
        mask=adding ? seed|bit(step) : seed&~bit(step);
    }
    void drag(int step) { const auto r=range(anchor,step); mask=adding ? seed|r : seed&~r; }
    void extend(int old_focus, int new_focus) {
        if (anchor<0) anchor=old_focus;
        mask=range(anchor,new_focus);
    }
};
struct Clipboard {
    Row data;
    uint64_t mask=0;
    int chain=-1, slot=-1,param=-1;
    uint64_t signature=0;
    Scope scope=Scope::Steps;
    void clear() { mask=0; }
    bool destinations(uint64_t selection, int focus, uint64_t& out) const {
        out=0;
        if (!mask) return false;
        const int n=count(mask), m=count(selection);
        if (selection) {
            if (n!=1 && n!=m) return false;
            out=selection; return true;
        }
        const int start=first(mask);
        for(int s=start;s<SEQ_STEPS;++s) if(mask&bit(s)) {
            const int dst=focus+s-start;
            if(dst>=SEQ_STEPS) { out=0; return false; }
            out|=bit(dst);
        }
        return true;
    }
    bool paste(Row& dest, uint64_t selection, int focus, uint64_t& out) const {
        if (!destinations(selection,focus,out)) return false;
        int src=first(mask);
        for(int d=0;d<SEQ_STEPS;++d) if(out&bit(d)) {
            const auto& from=data.steps[src]; auto& to=dest.steps[d];
            if(scope==Scope::Steps) { to=from; dest.timing[d]=data.timing[src]; }
            else if(scope==Scope::Locks) {
                to.lock_count=from.lock_count;
                for(int i=0;i<SEQ_LOCKS;++i) to.locks[i]=from.locks[i];
            } else {
                const int f=find(from,slot,param), t=find(to,slot,param);
                if(f<0) remove(to,slot,param); // explicit parameter replace, absence clears only this target
                else if(t>=0) to.locks[t]=from.locks[f];
                else { if(to.lock_count>=SEQ_LOCKS) return false; to.locks[to.lock_count++]=from.locks[f]; }
            }
            if(count(mask)>1) do { ++src; } while(src<SEQ_STEPS && !(mask&bit(src)));
        }
        return true;
    }
};
struct History {
    static constexpr int DEPTH=4;
    struct Entry { Row before,after; } entries[DEPTH];
    Row before;
    int size=0,cursor=0;
    bool active=false;
    void clear() { size=cursor=0; active=false; }
    void begin(const Row& row) { if(!active) { before=row; active=true; } }
    bool finish(const Row& row) {
        if(!active) return false;
        active=false;
        if(equal(before,row)) return false;
        size=cursor;
        if(size==DEPTH) {
            for(int i=1;i<DEPTH;++i) entries[i-1]=entries[i];
            --size; --cursor;
        }
        entries[size++]={before,row}; cursor=size; return true;
    }
    bool undo(Row& row) {
        if(!cursor) return false;
        if(!equal(row,entries[cursor-1].after)) { clear(); return false; }
        row=entries[--cursor].before; return true;
    }
    bool redo(Row& row) {
        if(cursor>=size) return false;
        if(!equal(row,entries[cursor].before)) { clear(); return false; }
        row=entries[cursor++].after; return true;
    }
};
static_assert(sizeof(History)+sizeof(Clipboard)+sizeof(Row)*2 < 16384, "bounded score edit storage");
} // namespace seance::platform::ui3ds::seq_edit
