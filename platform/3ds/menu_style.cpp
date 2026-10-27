#include "menu_style.h"
#include "editor_style.h"
#include "ui_palette.h"
#include "paper_motion.h"
#include "patch_paper.h"
#include "paper_presence.h"
#include "menu_labels_data.h"
#include "../../core/ui/draw.h"
#include <cstring>
#ifdef __3DS__
#include <citro2d.h>
#endif

namespace seance::platform::ui3ds::menu_style {
namespace {
uint32_t paper_colors[240];
bool paper_ready = false;
uint64_t paper_revision = 0;
bool ambient_ready = false, ambient_painted = false;
uint64_t ambient_now = 0, ambient_bucket = 0, ambient_serial = 0;
uint64_t ambient_revision = 0;
bool ambient_patch = false;
paper_presence::Motion presence;
unsigned ambient_amount = 0;
uint8_t patch_light[patch_paper::Size*patch_paper::Size]{};
uint32_t patch_colors[256]{};
bool patch_ready = false;
unsigned patch_bucket = 0;
uint64_t patch_revision = 0;
int paper_screen = 0;
struct TitleVisit {
    const menu_labels::Label* label = nullptr;
    uint64_t entered = 0, last_frame = 0;
} title_visits[2];
#ifdef __3DS__
C3D_Tex ambient_texture{};
#endif

bool page_label(const menu_labels::Label* label) {
    switch (unsigned(label - menu_labels::labels)) {
        case 0: case 1: case 2: case 24: case 25: case 26: case 27: case 28:
        case 32: case 40: case 47: case 48: case 50: case 51: return true;
        default: return false; // module heroes retain their original solid ink
    }
}

void init_paper() {
    if (paper_ready && paper_revision == palette::revision()) return;
    for (int y = 0; y < 240; ++y) paper_colors[y] = palette::paper_color(y);
    paper_revision = palette::revision();
    paper_ready = true;
}

const menu_labels::Label* find_label(const char* text) {
    if (!text) return nullptr;
    for (const auto& label : menu_labels::labels)
        if (std::strcmp(text, label.text) == 0) return &label;
    return nullptr;
}
bool field_active() { return patch_ready && presence.amount != 0; }
uint32_t field_texel(int x, int y) {
    const uint32_t color = patch_colors[patch_light[y*patch_paper::Size+x]];
    if (presence.amount == 255) return color;
    // The one dual-LCD sheet has two copies of pink's native row gradient.
    // Both CPU cache sampling and GPU upload blend at the SAME texel centers.
    int row = int((y+.5f)*patch_paper::Height/patch_paper::Size);
    // Switch inside the hidden 16px hinge, not at the lower LCD's first row:
    // bilinear taps around y256 must both use its near-zero gradient color.
    if (row >= 248) row -= 256;
    return theme_config::mix(palette::paper_color(row),color,presence.amount);
}
uint32_t field_sample(float x, float y) {
    float tx=x*patch_paper::Size/patch_paper::Width-.5f;
    float ty=y*patch_paper::Size/patch_paper::Height-.5f;
    tx=tx<0?0:tx>63?63:tx; ty=ty<0?0:ty>63?63:ty;
    const int ix=int(tx), iy=int(ty), ex=ix<63?ix+1:ix, ey=iy<63?iy+1:iy;
    const float fx=tx-ix, fy=ty-iy;
    const uint32_t a=field_texel(ix,iy), b=field_texel(ex,iy);
    const uint32_t c=field_texel(ix,ey), d=field_texel(ex,ey);
    uint32_t out=0xff000000u;
    for(unsigned shift=0;shift<24;shift+=8) {
        const float upper=((a>>shift)&255)*(1-fx)+((b>>shift)&255)*fx;
        const float lower=((c>>shift)&255)*(1-fx)+((d>>shift)&255)*fx;
        out|=uint32_t(upper*(1-fy)+lower*fy+.5f)<<shift;
    }
    return out;
}
void capture_paper(paper_presence::Scene scene, bool frozen, uint64_t now_ms) {
    // Light startup paper needs the same shadow depth as Patch. The former 40%
    // blend washed it into the bright LCD; keep that quiet recipe for dark themes.
    // Retarget the shared envelope (not LUT endpoints), so page/theme changes
    // remain continuous and modal/partial-restoration sampling stays identical.
    presence.capture(scene,frozen,now_ms,palette::visual_graphite() ? 102u : 255u);
    if (!presence.amount) return;
    const unsigned bucket = presence.phase();
    if (!patch_ready || bucket != patch_bucket) {
        patch_paper::paint(patch_light, bucket);
        patch_bucket = bucket;
    }
    if (!patch_ready || patch_revision != palette::revision()) {
        uint32_t dark, pale;
        using theme_config::mix;
        if (theme_config::legacy(palette::current())) {
            // Exact graphite endpoints from the approved GIF, continuously
            // interpolated with pink's own paper hues through the legacy fade.
            const auto pink = theme_config::colors({palette::Theme::Pink,{}}).paper;
            dark = palette::legacy::detail::surface([pink](bool graphite) {
                return graphite ? palette::rgb(17,18,22) : mix(pink,palette::gray(0),64);
            });
            pale = palette::legacy::detail::surface([pink](bool graphite) {
                return graphite ? palette::rgb(66,61,75) : pink;
            });
        } else {
            // Extended/custom palettes keep their own paper hue. No graphite
            // lilac or accent pollution, and no tint changes within a frame.
            const bool dim = palette::visual_graphite();
            dark = mix(palette::bg(),palette::gray(0),dim?128:64);
            pale = dim ? mix(palette::bg(),palette::gray(255),40) : palette::bg();
        }
        patch_paper::colors(patch_colors,dark,pale);
        patch_revision = palette::revision();
    }
    patch_ready = true;
}
} // namespace

void patch_capture(bool enabled, bool frozen, uint64_t now_ms) {
    using paper_presence::Scene;
    capture_paper(enabled ? Scene::Patch : Scene::Still, frozen, now_ms);
}
void startup_capture(bool home, uint64_t now_ms, bool frozen) {
    using paper_presence::Scene;
    capture_paper(home ? Scene::Startup : Scene::Still, frozen, now_ms);
}
uint32_t patch_sample(bool top, float x, float y) {
    if (!field_active()) return paper_color(int(y));
    const int width = top ? 400 : 320;
    x = x<0?0:x>width?float(width):x; y = y<0?0:y>240?240:y;
    if (!ambient_ready) {
        // Match the globally anchored 20px software/texture-failure tiles,
        // including partial restorations and frozen blur-cache capture.
        constexpr int step = patch_paper::FallbackStep;
        const int ix = int(x)<width?int(x):width-1, iy=int(y)<240?int(y):239;
        x = float((ix/step)*step+step/2); y = float((iy/step)*step+step/2);
    }
    return field_sample(x+(top?0:40),y+(top?0:256));
}

bool ambient_init() {
#ifdef __3DS__
    if (ambient_ready) return true;
    ambient_ready = C3D_TexInit(&ambient_texture, paper_motion::SIZE, paper_motion::SIZE, GPU_RGBA8);
    if (ambient_ready) {
        C3D_TexSetFilter(&ambient_texture, GPU_LINEAR, GPU_LINEAR);
        C3D_TexSetWrap(&ambient_texture, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
        ambient_painted = false;
        title_visits[0] = {}; title_visits[1] = {};
    }
#endif
    return ambient_ready;
}
void ambient_shutdown() {
#ifdef __3DS__
    if (ambient_ready) C3D_TexDelete(&ambient_texture);
#endif
    ambient_ready = ambient_painted = ambient_patch = false;
    presence = {}; patch_ready = false;
    title_visits[0] = {}; title_visits[1] = {};
}
bool ambient_active() { return ambient_ready && ambient_painted; }
void ambient_frame(uint64_t now_ms) {
    if (!ambient_ready) return;
    ++ambient_serial;
    ambient_now = now_ms;
    const bool patch = field_active();
    if (!patch) {
        // Work pages are exact static paper: no texture upload, drift or pulse.
        // Keep the pre-existing title clock independent of the paper envelope.
        ambient_patch = false; ambient_painted = true; return;
    }
    const uint64_t bucket = patch_bucket;
    if (ambient_painted && ambient_patch && bucket == ambient_bucket &&
        ambient_amount == presence.amount && ambient_revision == palette::revision()) return;
    ambient_revision = palette::revision(); ambient_amount = presence.amount;
    ambient_bucket = bucket; ambient_patch = true;
#ifdef __3DS__
    // FrameBegin(SYNCDRAW) has fenced the preceding reader. No mutation while
    // queued draws use this texture, and no allocation after init/before shutdown.
    auto* data = static_cast<uint8_t*>(ambient_texture.data);
    for (unsigned y=0;y<patch_paper::Size;++y) for (unsigned x=0;x<patch_paper::Size;++x) {
        const auto color = field_texel(x,y);
        auto* pixel = data + paper_motion::offset(x,y);
        pixel[0]=255; pixel[1]=uint8_t(color>>16); pixel[2]=uint8_t(color>>8); pixel[3]=uint8_t(color);
    }
    C3D_TexFlush(&ambient_texture);
    C3D_TexBind(0, &ambient_texture); // invalidate the PICA cache after CPU upload
#endif
    ambient_painted = true;
}

uint32_t ink() { return palette::ink(); }
uint32_t spot() { return palette::spot(); }
uint32_t ghost() { return palette::menu_ghost(); }
uint32_t branch() { return palette::branch(); }

uint32_t paper_color(int y) {
    init_paper();
    return paper_colors[y < 0 ? 0 : y > 239 ? 239 : y];
}

void paper_rect(int x, int y, int width, int height, int alpha) {
    const int screen_width = field_active() && !paper_screen ? 320 : 400;
    if (width <= 0 || height <= 0 || alpha <= 0 || x >= screen_width || y >= 240) return;
    if (x < 0) { width += x; x = 0; }
    if (y < 0) { height += y; y = 0; }
    if (width > screen_width - x) width = screen_width - x;
    if (height > 240 - y) height = 240 - y;
    if (width <= 0 || height <= 0) return;
    if (alpha > 255) alpha = 255;
#ifdef __3DS__
    if (field_active() && ambient_active() && ambient_patch) {
        // Live, fading and startup paper share the same 400x496 UVs throughout
        // the transition; no switch back to unrelated 400x240 moving shadows.
        const int ox = !paper_screen ? 40 : 0;
        const int oy = !paper_screen ? 256 : 0;
        const float sh = 496.f;
        const Tex3DS_SubTexture crop{static_cast<unsigned short>(width), static_cast<unsigned short>(height),
            float(x+ox) / 400, 1 - float(y+oy) / sh,
            float(x+width+ox) / 400, 1 - float(y+height+oy) / sh};
        const C2D_Image image{&ambient_texture, &crop};
        C2D_ImageTint tint;
        C2D_AlphaImageTint(&tint, float(alpha) / 255);
        C2D_DrawImageAt(image, float(x), float(y), 0, &tint);
        return;
    }
#endif
    if (field_active()) {
        constexpr int step = patch_paper::FallbackStep;
        for (int yy=y; yy<y+height;) {
            const int next_y = ((yy/step)+1)*step;
            const int end_y = next_y<y+height?next_y:y+height;
            for (int xx=x; xx<x+width;) {
                const int next_x = ((xx/step)+1)*step;
                const int end_x = next_x<x+width?next_x:x+width;
                const uint32_t c = patch_sample(paper_screen!=0,xx+.5f,yy+.5f);
                ui::rect(xx,yy,end_x-xx,end_y-yy,(c&0xffffffu)|(uint32_t(alpha)<<24));
                xx=end_x;
            }
            yy=end_y;
        }
        return;
    }
    init_paper();
    const int y1 = y + height;
    for (; y < y1;) {
        int end = y + 1;
        while (end < y1 && paper_colors[end] == paper_colors[y]) ++end;
        ui::rect(x, y, width, end - y, (paper_colors[y] & 0xffffffu) | (uint32_t(alpha) << 24));
        y = end;
    }
}
void paper(int width, int y0, int y1) {
    paper_screen = width == 400 ? 1 : 0;
    if (y0 < 0) y0 = 0;
    if (y1 > 240) y1 = 240;
    if (y0 >= y1) return;
    paper_rect(0, y0, width, y1 - y0);
}

int heading_width(const char* text) {
    const auto* label = find_label(text);
    return label ? label->width : (text ? ui::text_w(text) : 0);
}

namespace {
void draw_label(int x, int y, const menu_labels::Label* label, uint32_t color) {
    const bool moving_ink = presence.scene != paper_presence::Scene::Patch && ambient_active() && page_label(label) && color == ink();
    auto& visit = title_visits[paper_screen];
    if (moving_ink) {
        if (visit.label != label || ambient_serial - visit.last_frame > 1 || ambient_now < visit.entered) {
            visit.label = label; visit.entered = ambient_now;
        }
        visit.last_frame = ambient_serial;
    }
    const int stride = (label->width + 7) / 8;
    const auto* bitmap = menu_labels::bitmap + label->offset;
    // Repeated bitmap rows share a rectangle height. Horizontal ink runs share
    // a rectangle width, retaining every original pixel without per-pixel cost.
    for (int row = 0; row < label->height;) {
        const auto* bits = bitmap + row * stride;
        int end = row + 1;
        while (end < label->height &&
               std::memcmp(bits, bitmap + end * stride, stride) == 0) ++end;
        int run = -1;
        for (int col = 0; col <= label->width; ++col) {
            const bool on = col < label->width && (bits[col / 8] & (0x80 >> (col & 7)));
            if (on && run < 0) run = col;
            if (!on && run >= 0) {
                const uint32_t shade = moving_ink
                    ? paper_motion::title_ink(color, ambient_now, visit.entered,
                        unsigned(label - menu_labels::labels), (run + col) * .5f, label->width)
                    : color;
                ui::rect(x + run, y + row, col - run, end - row, shade);
                run = -1;
            }
        }
        row = end;
    }
}

} // namespace

void heading(int x, int y, const char* text, uint32_t color) {
    if (!text) return;
    const auto* label = find_label(text);
    if (!label) { ui::text(x, y, text, color); return; }
    draw_label(x, y, label, color);
}

void page_heading(page_layout::Title title, uint32_t color) {
    const unsigned index = unsigned(title);
    if (index >= sizeof(menu_labels::labels) / sizeof(menu_labels::labels[0])) return;
    draw_label(page_layout::kLeft, page_layout::title_y(title), &menu_labels::labels[index], color);
}

void marker(int x, int y, uint32_t color) {
    // The study's 7-pixel chevron; integer rectangles avoid GPU line coverage.
    for (int dy = -3; dy <= 3; ++dy) {
        const int dx = dy < 0 ? dy : -dy;
        ui::rect(x + dx, y + dy, 1, 1, color);
    }
}

void footer(int width, const char* left, const char* right) {
    if (left) ui::text(20, module_footer_layout::kTextY, left, ghost());
    if (right) ui::text(width - 20 - ui::text_w(right), module_footer_layout::kTextY, right, ghost());
}

const char* module_name(UnitKind kind) {
    switch (kind) {
        case UnitKind::Empty:        return "empty";
        case UnitKind::Osc:          return "osc";
        case UnitKind::Noise:        return "noise";
        case UnitKind::Fm:           return "fm";
        case UnitKind::Motor:        return "motor";
        case UnitKind::SamplePlayer: return "play";
        case UnitKind::Grain:        return "gran";
        case UnitKind::MicIn:        return "mic";
        case UnitKind::Filter:       return "filt";
        case UnitKind::Vca:          return "vca";
        case UnitKind::Delay:        return "dly";
        case UnitKind::Recorder:     return "rec";
        case UnitKind::Fold:         return "fold";
        case UnitKind::Bitcrush:     return "bit";
        case UnitKind::Chorus:       return "chor";
        case UnitKind::Stranular:    return "swarm";
        case UnitKind::Reverb:       return "rev";
        case UnitKind::Ghost:        return "ghost";
        case UnitKind::Inst:         return "inst";
        case UnitKind::Glitch:       return "glitch";
        case UnitKind::Arp:          return "arp";
        case UnitKind::Mouth:        return "mouth";
        case UnitKind::Comp:         return "comp";
        case UnitKind::Fract:        return "fract";
        case UnitKind::Bird:         return "bird";
        case UnitKind::Shift:        return "shift";
        case UnitKind::Ring:         return "ring";
        case UnitKind::Tilt:         return "tilt";
        case UnitKind::Strain:       return "strain";
        default:                    return "?";
    }
}

const char* module_description(UnitKind kind) {
    switch (kind) {
        case UnitKind::Empty:        return "free slot";
        case UnitKind::Osc:          return "oscillator";
        case UnitKind::Noise:        return "noise source";
        case UnitKind::Fm:           return "fm operators";
        case UnitKind::Motor:        return "impulses";
        case UnitKind::SamplePlayer: return "sample";
        case UnitKind::Grain:        return "grains";
        case UnitKind::MicIn:        return "microphone";
        case UnitKind::Filter:       return "filter";
        case UnitKind::Vca:          return "gain / envelope";
        case UnitKind::Delay:        return "echo";
        case UnitKind::Recorder:     return "record to sample";
        case UnitKind::Fold:         return "wavefolder";
        case UnitKind::Bitcrush:     return "bits / rate";
        case UnitKind::Chorus:       return "chorus";
        case UnitKind::Stranular:    return "live grains";
        case UnitKind::Reverb:       return "reverb";
        case UnitKind::Ghost:        return "memory wells";
        case UnitKind::Inst:         return "instruments";
        case UnitKind::Glitch:       return "rhythmic rearranger";
        case UnitKind::Arp:          return "note arpeggiator";
        case UnitKind::Mouth:        return "formant filter";
        case UnitKind::Comp:         return "dynamic compression";
        case UnitKind::Fract:        return "buffer fracture";
        case UnitKind::Bird:         return "bird calls";
        case UnitKind::Shift:        return "frequency shift";
        case UnitKind::Ring:         return "ring modulation";
        case UnitKind::Tilt:         return "spectral tilt";
        case UnitKind::Strain:       return "moving distortion";
        default:                    return "unknown module";
    }
}
} // namespace seance::platform::ui3ds::menu_style

// Separate, explicitly selected editor material. The menu texture, its clocks,
// paper_screen and captured patch/startup presence above remain independent.
namespace seance::platform::ui3ds::editor_style {
namespace {
uint8_t light[patch_paper::Size * patch_paper::Size]{};
uint32_t colors[256]{};
bool initialized = false, colored = false, texture_ready = false, uploaded = false;
uint64_t color_revision = 0, upload_revision = 0;
uint32_t active_paper = 0xff000000u;
int screen_width = 320;
#ifdef __3DS__
C3D_Tex texture{};
#endif
uint32_t blend_percent(uint32_t a, uint32_t b, unsigned percent, bool round = true) {
    uint32_t result = 0xff000000u;
    for (unsigned shift = 0; shift < 24; shift += 8)
        result |= ((((a >> shift) & 255u) * (100 - percent) +
                    ((b >> shift) & 255u) * percent + (round ? 50 : 0)) / 100) << shift;
    return result;
}
} // namespace

bool init() {
    if (!initialized) {
        patch_paper::paint(light, 0); // the only noise work in this material's lifetime
        initialized = true;
    }
    capture();
#ifdef __3DS__
    if (!texture_ready) {
        texture_ready = C3D_TexInit(&texture, patch_paper::Size, patch_paper::Size, GPU_RGBA8);
        if (texture_ready) {
            C3D_TexSetFilter(&texture, GPU_LINEAR, GPU_LINEAR);
            C3D_TexSetWrap(&texture, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
            uploaded = false;
        }
    }
#endif
    return texture_ready;
}
void shutdown() {
#ifdef __3DS__
    if (texture_ready) C3D_TexDelete(&texture);
#endif
    texture_ready = uploaded = initialized = colored = false;
    screen_width = 320;
}
void capture() {
    if (colored && color_revision == palette::revision()) return;
    // Exact active tile recipe used by ui_chains, not an editor-specific tint.
    active_paper = blend_percent(palette::paper_color(120), palette::spot(), 7);
    uint32_t dark, pale;
    using theme_config::mix;
    if (theme_config::legacy(palette::current())) {
        const auto pink = theme_config::colors({palette::Theme::Pink,{}}).paper;
        dark = palette::legacy::detail::surface([pink](bool graphite) {
            return graphite ? palette::rgb(17,18,22) : mix(pink,palette::gray(0),64);
        });
        pale = palette::legacy::detail::surface([pink](bool graphite) {
            return graphite ? palette::rgb(66,61,75) : pink;
        });
    } else {
        const bool dim = palette::visual_graphite();
        dark = mix(palette::bg(),palette::gray(0),dim?128:64);
        pale = dim ? mix(palette::bg(),palette::gray(255),40) : palette::bg();
    }
    patch_paper::colors(colors, dark, pale);
    // The approved Pillow blend truncates its 68:32 byte-space result. Moving
    // this affine blend into the LUT keeps a single bilinearly filtered texture.
    for (auto& color : colors) color = blend_percent(active_paper, color, 32, false);
    color_revision = palette::revision();
    colored = true;
}
bool active() { return texture_ready && uploaded && upload_revision == color_revision; }
void frame() {
    if (!texture_ready || !initialized || !colored || active()) return;
#ifdef __3DS__
    // Called after FrameBegin(SYNCDRAW), never from paper/sample/draw. The previous
    // reader has been fenced and neither LCD can observe a partially written LUT.
    auto* data = static_cast<uint8_t*>(texture.data);
    for (unsigned y=0; y<patch_paper::Size; ++y) for (unsigned x=0; x<patch_paper::Size; ++x) {
        const uint32_t color = colors[light[y*patch_paper::Size+x]];
        auto* pixel = data + paper_motion::offset(x,y);
        pixel[0]=255; pixel[1]=uint8_t(color>>16); pixel[2]=uint8_t(color>>8); pixel[3]=uint8_t(color);
    }
    C3D_TexFlush(&texture);
    C3D_TexBind(0, &texture);
#endif
    uploaded = true;
    upload_revision = color_revision;
}
uint32_t paper_color(int) { return active_paper; }
uint32_t sample(bool top, float x, float y) {
    if (!initialized || !colored) return active_paper;
    const int width = top ? 400 : 320;
    x = x < 0 ? 0 : x > width ? float(width) : x;
    y = y < 0 ? 0 : y > 240 ? 240 : y;
    if (!active()) {
        constexpr int step = patch_paper::FallbackStep;
        const int ix = int(x) < width ? int(x) : width-1;
        const int iy = int(y) < 240 ? int(y) : 239;
        x = float((ix/step)*step+step/2);
        y = float((iy/step)*step+step/2);
    }
    return patch_paper::sample(light, colors, x+(top?0:40), y+(top?0:256));
}
void paper_rect(int x, int y, int width, int height, int alpha) {
    if (width <= 0 || height <= 0 || alpha <= 0 || x >= screen_width || y >= 240) return;
    // Use wide edges so even a fully off-screen/extreme restoration cannot wrap.
    const int64_t right = int64_t(x)+width, bottom = int64_t(y)+height;
    if (right <= 0 || bottom <= 0) return;
    const int left = x < 0 ? 0 : x, top = y < 0 ? 0 : y;
    const int end_x = right > screen_width ? screen_width : int(right);
    const int end_y = bottom > 240 ? 240 : int(bottom);
    if (left >= end_x || top >= end_y) return;
    if (alpha > 255) alpha = 255;
#ifdef __3DS__
    if (active()) {
        const int ox = screen_width == 400 ? 0 : 40;
        const int oy = screen_width == 400 ? 0 : 256;
        const Tex3DS_SubTexture crop{static_cast<unsigned short>(end_x-left), static_cast<unsigned short>(end_y-top),
            float(left+ox)/400, 1-float(top+oy)/496,
            float(end_x+ox)/400, 1-float(end_y+oy)/496};
        const C2D_Image image{&texture, &crop};
        C2D_ImageTint tint;
        C2D_AlphaImageTint(&tint, float(alpha)/255);
        C2D_DrawImageAt(image, float(left), float(top), 0, &tint);
        return;
    }
#endif
    // Fixed global tile anchors are essential: partial erase/alpha restoration
    // must reproduce the full sheet rather than restarting a local coarse grid.
    constexpr int step = patch_paper::FallbackStep;
    for (int yy=top; yy<end_y;) {
        const int next_y = ((yy/step)+1)*step;
        const int stop_y = next_y < end_y ? next_y : end_y;
        for (int xx=left; xx<end_x;) {
            const int next_x = ((xx/step)+1)*step;
            const int stop_x = next_x < end_x ? next_x : end_x;
            const uint32_t color = sample(screen_width==400,xx+.5f,yy+.5f);
            ui::rect(xx,yy,stop_x-xx,stop_y-yy,(color&0xffffffu)|(uint32_t(alpha)<<24));
            xx = stop_x;
        }
        yy = stop_y;
    }
}
void paper(int width, int y0, int y1) {
    screen_width = width == 400 ? 400 : 320;
    if (y0 < 0) y0 = 0;
    if (y1 > 240) y1 = 240;
    if (y0 < y1) paper_rect(0,y0,screen_width,y1-y0);
}
void heading(int x, int y, const char* text, uint32_t color) {
    if (!text) return;
    const auto* label = menu_style::find_label(text);
    if (!label) { ui::text(x,y,text,color); return; }
    const int stride = (label->width+7)/8;
    const auto* bitmap = menu_labels::bitmap+label->offset;
    // Exact same run-coalesced mask as menu heading, without its title visits.
    for (int row=0; row<label->height;) {
        const auto* bits = bitmap+row*stride;
        int end = row+1;
        while (end<label->height && std::memcmp(bits,bitmap+end*stride,stride)==0) ++end;
        int run = -1;
        for (int col=0; col<=label->width; ++col) {
            const bool on = col<label->width && (bits[col/8]&(0x80>>(col&7)));
            if (on && run<0) run=col;
            if (!on && run>=0) {
                ui::rect(x+run,y+row,col-run,end-row,color);
                run=-1;
            }
        }
        row=end;
    }
}
void caption(int x, int y, int max_width, const char* text, uint32_t color) {
    if (!text || !*text || max_width < module_frame_layout::kTextAdvance) return;
    constexpr int capacity = (module_frame_layout::kTopWidth - 2 * kLeft) /
                             module_frame_layout::kTextAdvance;
    const int requested = max_width / module_frame_layout::kTextAdvance;
    const int limit = requested < capacity ? requested : capacity;
    char clipped[capacity + 1];
    int n = 0;
    while (n < limit && text[n]) { clipped[n] = text[n]; ++n; }
    if (text[n]) {
        const int dots = n < 3 ? n : 3;
        for (int i = n - dots; i < n; ++i) clipped[i] = '.';
    }
    clipped[n] = '\0';
    ui::text(x, y, clipped, color);
}
void header_text(int x, int right, const char* text, uint32_t color) {
    caption(x, kHeaderTextY, right - x, text, color);
}
void footer(int width, const char* left, const char* right) {
    const int available = right_edge(width) - kLeft;
    if (available <= 0) return;
    int right_chars = 0;
    const int max_chars = available / module_frame_layout::kTextAdvance;
    if (right) while (right_chars < max_chars && right[right_chars]) ++right_chars;
    const int right_width = right_chars * module_frame_layout::kTextAdvance;
    caption(kLeft, kTextY,
            available - (right_width ? right_width + module_frame_layout::kCaptionGap : 0),
            left, ghost());
    if (right_width) caption(right_edge(width) - right_width, kTextY, right_width, right, ghost());
}
} // namespace seance::platform::ui3ds::editor_style
