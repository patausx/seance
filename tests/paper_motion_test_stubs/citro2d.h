#pragma once
// Shapes/signatures checked against installed libctru c2d/base.h and c3d/texture.h.
#include <cstdint>
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
struct C3D_Tex { void* data = nullptr; u16 width = 0, height = 0; };
struct Tex3DS_SubTexture { u16 width, height; float left, top, right, bottom; };
struct C2D_Image { C3D_Tex* tex; const Tex3DS_SubTexture* subtex; };
struct C2D_Tint { u32 color; float blend; };
struct C2D_ImageTint { C2D_Tint corners[4]; };
enum GPU_TEXCOLOR { GPU_RGBA8 };
enum GPU_TEXTURE_FILTER_PARAM { GPU_NEAREST, GPU_LINEAR };
enum GPU_TEXTURE_WRAP_PARAM { GPU_CLAMP_TO_EDGE };
constexpr u32 C2D_Color32(u8 r, u8 g, u8 b, u8 a) {
    return u32(r) | (u32(g) << 8) | (u32(b) << 16) | (u32(a) << 24);
}
inline void C2D_PlainImageTint(C2D_ImageTint* tint, u32 color, float blend) {
    for (auto& corner : tint->corners) corner = {color, blend};
}
inline void C2D_AlphaImageTint(C2D_ImageTint* tint, float alpha) {
    C2D_PlainImageTint(tint, C2D_Color32(0, 0, 0, u8(alpha * 255.0f)), 0.0f);
}
bool C3D_TexInit(C3D_Tex*, u16, u16, GPU_TEXCOLOR);
void C3D_TexSetFilter(C3D_Tex*, GPU_TEXTURE_FILTER_PARAM, GPU_TEXTURE_FILTER_PARAM);
void C3D_TexSetWrap(C3D_Tex*, GPU_TEXTURE_WRAP_PARAM, GPU_TEXTURE_WRAP_PARAM);
void C3D_TexFlush(C3D_Tex*);
void C3D_TexBind(int, C3D_Tex*);
void C3D_TexDelete(C3D_Tex*);
bool C2D_DrawImageAt(C2D_Image, float, float, float, const C2D_ImageTint* = nullptr,
                     float = 1.0f, float = 1.0f);
bool C2D_DrawRectSolid(float, float, float, float, float, u32);
bool C2D_DrawLine(float, float, u32, float, float, u32, float, float);
