#pragma once
#include <cstdint>
using u32 = uint32_t;
enum GPU_TEXCOLOR { GPU_RGBA8 };
enum GPU_TEXTURE_FILTER_PARAM { GPU_LINEAR };
enum GPU_TEXTURE_WRAP_PARAM { GPU_CLAMP_TO_EDGE };
enum GPU_BLENDEQUATION { GPU_BLEND_ADD };
enum GPU_BLENDFACTOR { GPU_ZERO, GPU_ONE, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
                       GPU_ONE_MINUS_DST_COLOR, GPU_ONE_MINUS_SRC_COLOR };
enum C2D_TintMode { C2D_TintSolid, C2D_TintMult };
struct C3D_Tex { void* data = nullptr; };
struct Tex3DS_SubTexture { unsigned short width, height; float left, top, right, bottom; };
struct C2D_Image { C3D_Tex* tex; const Tex3DS_SubTexture* subtex; };
struct C2D_ImageTint { u32 color; float blend; };
bool C3D_TexInit(C3D_Tex*, unsigned, unsigned, GPU_TEXCOLOR);
void C3D_TexDelete(C3D_Tex*);
void C3D_TexSetFilter(C3D_Tex*, GPU_TEXTURE_FILTER_PARAM, GPU_TEXTURE_FILTER_PARAM);
void C3D_TexSetWrap(C3D_Tex*, GPU_TEXTURE_WRAP_PARAM, GPU_TEXTURE_WRAP_PARAM);
void C3D_TexFlush(C3D_Tex*);
void C3D_TexBind(int, C3D_Tex*);
void C3D_AlphaBlend(GPU_BLENDEQUATION, GPU_BLENDEQUATION, GPU_BLENDFACTOR,
                  GPU_BLENDFACTOR, GPU_BLENDFACTOR, GPU_BLENDFACTOR);
void C2D_Flush();
bool C2D_SetTintMode(C2D_TintMode);
inline void C2D_PlainImageTint(C2D_ImageTint* t, u32 c, float b) { t->color=c; t->blend=b; }
bool C2D_DrawImageAt(C2D_Image, float, float, float, const C2D_ImageTint* = nullptr,
                     float = 1.f, float = 1.f);
bool C2D_DrawCircleSolid(float, float, float, float, u32);
