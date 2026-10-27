#pragma once
// Deliberately not __3DS__: the real portable engine and real 3DS UI share this
// single-thread host harness. No audio worker/hardware behavior is emulated.
#include <cassert>
#include <cstdint>
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using s16 = std::int16_t;
// Explicit wall clock for elapsed-time UI tests. Other host harnesses remain
// parked at time zero and never depend on host scheduling or real sleeping.
inline u64 host_time_ms = 0;
inline u64 osGetTime() { return host_time_ms; }
struct circlePosition { s16 dx = 0, dy = 0; };
void hidCircleRead(circlePosition* position);
enum : u32 {
    KEY_A = 1u << 0, KEY_B = 1u << 1, KEY_SELECT = 1u << 2,
    KEY_START = 1u << 3, KEY_DRIGHT = 1u << 4, KEY_DLEFT = 1u << 5,
    KEY_DUP = 1u << 6, KEY_DDOWN = 1u << 7, KEY_R = 1u << 8,
    KEY_L = 1u << 9, KEY_X = 1u << 10, KEY_Y = 1u << 11,
    KEY_TOUCH = 1u << 20, KEY_CPAD_RIGHT = 1u << 28,
    KEY_CPAD_LEFT = 1u << 29, KEY_CPAD_UP = 1u << 30,
    KEY_CPAD_DOWN = 1u << 31,
    KEY_RIGHT = KEY_DRIGHT | KEY_CPAD_RIGHT,
    KEY_LEFT = KEY_DLEFT | KEY_CPAD_LEFT,
    KEY_UP = KEY_DUP | KEY_CPAD_UP,
    KEY_DOWN = KEY_DDOWN | KEY_CPAD_DOWN
};
struct RecursiveLock { int depth = 0; };
inline void RecursiveLock_Init(RecursiveLock* lock) { lock->depth = 0; }
inline void RecursiveLock_Lock(RecursiveLock* lock) { ++lock->depth; }
inline void RecursiveLock_Unlock(RecursiveLock* lock) {
    assert(lock->depth > 0);
    --lock->depth;
}
struct ndspWaveBuf {};
struct LightEvent {};
struct aptHookCookie {};
using Thread = void*;
