#include "stylus_feedback.h"
#include "stylus_liquid_model.h"
#include <citro2d.h>

namespace seance::platform::ui3ds::stylus_feedback {
namespace {
stylus_liquid::State state;
stylus_liquid::Frame frame;
C3D_Tex texture{};
const Tex3DS_SubTexture subtex{64, 64, 0.f, 1.f, 1.f, 0.f};
bool attempted = false, ready = false, painted = false;
uint32_t pigment = 0xff000000u, fallback = 0xffffffffu;

uint32_t tint_attenuation(uint32_t accent) {
    unsigned peak = 1;
    for (unsigned shift = 0; shift < 24; shift += 8) {
        const unsigned c = (accent >> shift) & 255u;
        if (c > peak) peak = c;
    }
    // Normalize chroma: a dark accent must not black out the inverse on pink.
    // Grayscale custom themes stay neutral, with no invented purple hue.
    uint32_t out = 0xff000000u;
    for (unsigned shift = 0; shift < 24; shift += 8) {
        const unsigned c = (accent >> shift) & 255u;
        out |= ((140u * (peak-c) + peak/2) / peak) << shift;
    }
    return out;
}
}

bool init() {
    if (attempted) return ready;
    attempted = true;
    ready = C3D_TexInit(&texture, stylus_liquid::kSize, stylus_liquid::kSize, GPU_RGBA8);
    if (ready) {
        C3D_TexSetFilter(&texture, GPU_LINEAR, GPU_LINEAR);
        C3D_TexSetWrap(&texture, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    }
    return ready;
}

void shutdown() {
    if (ready) C3D_TexDelete(&texture);
    texture = {};
    state = {}; frame = {};
    attempted = ready = painted = false;
}

void capture(bool held, int x, int y, uint64_t now_ms, uint32_t accent) {
    stylus_liquid::capture(state, frame, held, x, y, now_ms);
    pigment = tint_attenuation(accent);
    fallback = accent & 0x00ffffffu;
    painted = false;
}

void upload() {
    if (!ready || !frame.active || painted) return;
    // One reusable texture: main's SYNCDRAW fence has completed its last reader.
    stylus_liquid::paint(static_cast<uint8_t*>(texture.data), frame);
    C3D_TexFlush(&texture);
    painted = true;
}

void draw() {
    if (!frame.active) return;
    if (!ready) {
        // Allocation failure only. No lazy init, oversized software field, or
        // pretend adaptive background sample. The input remains fully usable.
        const unsigned alpha = unsigned(200.f * frame.opacity);
        if (alpha) C2D_DrawCircleSolid(float(frame.x+32), float(frame.y+32), 0.f,
                                      3.f, fallback | (alpha << 24));
        return;
    }
    if (!painted) return; // never draw a stale texture after capture/failed fence
    const C2D_Image image{&texture, &subtex};
    C2D_Flush(); // ordinary queued UI must use ordinary blending
    C3D_TexBind(0, &texture); // invalidate PICA texture cache even for same pointer
    C2D_SetTintMode(C2D_TintSolid);
    // Texture RGBA = (a,a,a,a), so premultiplied difference against CURRENT D:
    // inverse = a*(1-D) + D*(1-a). a=0 is exactly the original destination.
    // GPU destination blending makes rails/letters react locally without readback.
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD,
                  GPU_ONE_MINUS_DST_COLOR, GPU_ONE_MINUS_SRC_ALPHA, GPU_ZERO, GPU_ONE);
    C2D_DrawImageAt(image, float(frame.x), float(frame.y), 0.f);
    C2D_Flush();

    // A second masked multiply adds the current theme hue on both polarities:
    // result = inverse * (1-a*attenuation). Transparent texels remain untouched.
    C2D_ImageTint tint;
    C2D_PlainImageTint(&tint, pigment, 1.f);
    C2D_SetTintMode(C2D_TintMult);
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD,
                  GPU_ZERO, GPU_ONE_MINUS_SRC_COLOR, GPU_ZERO, GPU_ONE);
    C2D_DrawImageAt(image, float(frame.x), float(frame.y), 0.f, &tint);
    C2D_Flush(); // submit before restoring effects for reveal/debug/next screen
    C2D_SetTintMode(C2D_TintSolid);
    // Exact Citro3D default factors (including alpha); no caller uses another
    // blend/tint mode. Do not rely on SceneBegin/Prepare to restore AlphaBlend.
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD,
                  GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
                  GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
}
}
