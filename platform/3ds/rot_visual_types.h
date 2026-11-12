#pragma once
// UI-owned, pointer-free frozen COPY. No Engine/unit/bank/PCM references.
#include <cstdint>
#include <type_traits>
namespace seance::platform::ui3ds::rot_visual {
struct Artwork {
    uint8_t env[12][372]={},mask[12][372]={},scar[372]={},spark[372]={},source_env[292]={};
};
struct RotFrame {
    static constexpr int kCols=372,kHistory=12,kStrip=292,kKnobs=10;
    enum Mode : uint8_t { Rotting,Healing,Holding,Tail };
    enum Status : uint8_t { Ready,Empty,Bypass,Paused,Preparing,Fault };
    bool valid=false;
    Mode mode=Rotting;
    Status status=Empty;
    char source[40]={};
    // scoped status detail, e.g. prepare/allocation/source fault; never fabricated activity.
    char detail[32]={};
    int pass=0,passes=0,rot_percent=0,loop_ms=0,head=-1,history=0;
    // LINEAR captured source age /100ms, 0..256. Renderer applies mock smoothstep once.
    int drop_q8=256;
    int loop_x0=0,loop_x1=0,strip_head=-1,selected=2;
    struct Knob {
        char value[16]={};
        int edit_q8=0,live_q8=0;
        bool linked=false,disabled=false,quiet=false;
    } knobs[kKnobs];
    // source identity/generation protects same serials on replacement. Increment artwork
    // for actual pass/material change; spark for actual captured crackle/decay change.
    uint64_t source_serial=0,artwork_serial=0,spark_serial=0,material_revision=0;
    bool window_pending=false,lens=false; // controller disclosure only
    uint8_t law=0; // unit Config law: law 1 counts passes beyond `passes`
};
static_assert(sizeof(RotFrame)<=1024,"scalar UI frame budget");
static_assert(sizeof(Artwork)==12*372*2+372*2+292,"separate exact artwork shapes");
static_assert(std::is_trivially_copyable<RotFrame>::value && std::is_trivially_copyable<Artwork>::value,"guard-copy DTOs");
inline bool shows_art(const RotFrame& f){return f.valid&&(f.status==RotFrame::Ready||f.status==RotFrame::Paused||f.status==RotFrame::Bypass)&&f.history>0;}
inline bool moving(const RotFrame& f){return f.status==RotFrame::Ready;}
inline const char* status_text(const RotFrame& f){
    static const char* const states[]={"","empty","bypass","paused","preparing","fault"};
    return unsigned(f.status)<6?states[f.status]:"fault";
}
}
