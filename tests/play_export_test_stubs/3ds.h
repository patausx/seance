#pragma once
#include <cassert>
#include <cstdint>
using u8=uint8_t;using u16=uint16_t;using u32=uint32_t;using u64=uint64_t;
inline int play_export_guard_depth=0;
inline unsigned play_export_unlocks=0;
inline void (*play_export_after_unlock)()=nullptr;
struct RecursiveLock{int depth=0;};
inline void RecursiveLock_Init(RecursiveLock* l){l->depth=0;}
inline void RecursiveLock_Lock(RecursiveLock* l){++l->depth;++play_export_guard_depth;}
inline void RecursiveLock_Unlock(RecursiveLock* l){assert(l->depth>0);--l->depth;--play_export_guard_depth;++play_export_unlocks;if(play_export_after_unlock)play_export_after_unlock();}
struct ndspWaveBuf{};struct LightEvent{};struct aptHookCookie{};using Thread=void*;
