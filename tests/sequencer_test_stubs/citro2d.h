#pragma once
#include "3ds.h"
inline constexpr u32 C2D_Color32(u8 r, u8 g, u8 b, u8 a) {
    return (u32)r | ((u32)g << 8) | ((u32)b << 16) | ((u32)a << 24);
}
// Matches installed libctru/include/c2d/base.h. main.cpp doubles this budget
// for BOTH displays together, so a bottom-only result is not total frame usage.
inline constexpr int C2D_DEFAULT_MAX_OBJECTS = 4096;
bool C2D_DrawRectSolid(float x, float y, float z, float w, float h, u32 color);
bool C2D_DrawLine(float x0, float y0, u32 c0, float x1, float y1, u32 c1,
                  float thickness, float z);
bool C2D_DrawTriangle(float x0, float y0, u32 c0, float x1, float y1, u32 c1,
                      float x2, float y2, u32 c2, float z);
