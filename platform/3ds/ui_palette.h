#pragma once
#include <cstdint>
#include "theme_config.h"

namespace seance::platform::ui3ds::palette {
// Keep the two shipped palettes and their contrast-preserving transition exact.
namespace legacy {
// UI-thread-owned, header-only state. Destination is logical/persisted immediately;
// colors only advance at capture(), once BEFORE UI DTOs and drawing for both LCDs.
// No clock reads in color helpers, engine state, allocation or framebuffer filter.
enum class Theme { Pink, Graphite };
inline Theme theme = Theme::Pink;
constexpr unsigned kTransitionMs = 170;
namespace detail {
constexpr unsigned kOne = 65536;
inline unsigned shown = 0, from = 0;
inline uint64_t started = 0, serial = 0;
inline bool moving = false;
inline Theme valid(Theme value) { return value == Theme::Graphite ? Theme::Graphite : Theme::Pink; }
inline unsigned target() { return theme == Theme::Graphite ? kOne : 0; }
inline void publish(unsigned value) {
    if (shown != value) { shown = value; ++serial; }
}
}
inline Theme current() { return theme; }
inline bool is_graphite() { return current() == Theme::Graphite; }
// Exact synchronous boot/test path; also cancels an in-flight transition.
inline void set(Theme value) {
    theme = detail::valid(value);
    detail::moving = false;
    detail::publish(detail::target());
}
inline void animate_to(Theme value, uint64_t now_ms) {
    value = detail::valid(value);
    if (value == theme) return; // unrelated preferences must not restart the fade
    theme = value;
    // Retarget the last frozen frame, not an unpresented time extrapolation.
    // Calling this during input cannot change colors or mutate GPU storage.
    detail::from = detail::shown;
    detail::started = now_ms;
    detail::moving = detail::from != detail::target();
}
inline void capture(uint64_t now_ms) {
    if (!detail::moving) return;
    const uint64_t elapsed = now_ms < detail::started ? 0 : now_ms - detail::started;
    if (elapsed >= kTransitionMs) {
        detail::publish(detail::target()); detail::moving = false; return;
    }
    constexpr int64_t d = kTransitionMs;
    const int64_t t = int64_t(elapsed);
    const int64_t eased = (detail::kOne * t * t * (3 * d - 2 * t) + d * d * d / 2) / (d * d * d);
    const int64_t delta = int64_t(detail::target()) - detail::from;
    detail::publish(unsigned(int64_t(detail::from) + (delta * eased + (delta < 0 ? -32768 : 32768)) / 65536));
}
inline uint64_t revision() { return detail::serial; }
// Rendering-only polarity; labels/preferences must instead use is_graphite().
inline bool visual_graphite() { return detail::shown >= detail::kOne / 2; }
inline float paper_depth_scale() { return 1.25f - float(detail::shown) / (4.f * detail::kOne); }

// ABGR (Citro2D) packing; splash texture byte order has its own writer.
constexpr uint32_t rgb(unsigned r, unsigned g, unsigned b) {
    return 0xff000000u | r | (g << 8) | (b << 16);
}
constexpr uint32_t gray(unsigned value) { return rgb(value, value, value); }
namespace detail {
inline uint32_t mix(uint32_t a, uint32_t b, unsigned amount) {
    uint32_t out = 0xff000000u;
    for (unsigned shift = 0; shift < 24; shift += 8)
        out |= ((((a >> shift) & 255u) * (kOne - amount) +
                 ((b >> shift) & 255u) * amount + kOne / 2) / kOne) << shift;
    return out;
}
template<class Colors> inline uint32_t surface(Colors colors) {
    return mix(colors(false), colors(true), shown);
}
// A continuous light-to-dark background AND continuous dark-to-light ink must
// cross at zero contrast. Instead bridge toward black/white, then switch text,
// accent and inverse polarity TOGETHER at mid-paper. No disappearing glyphs.
template<class Colors> inline uint32_t foreground(Colors colors) {
    return shown < kOne / 2 ? mix(colors(false), gray(0), shown * 2)
                           : mix(gray(255), colors(true), shown * 2 - kOne);
}
}
inline uint32_t bg()       { return detail::surface([](bool g) { return g ? gray(36)  : rgb(226, 190, 206); }); }
inline uint32_t cell()     { return detail::surface([](bool g) { return g ? gray(48)  : rgb(242, 216, 226); }); }
inline uint32_t cell_dim() { return detail::surface([](bool g) { return g ? gray(28)  : rgb(207, 165, 184); }); }
inline uint32_t ink()      { return detail::foreground([](bool g) { return g ? gray(244) : rgb(31, 20, 31); }); }
// Muted lilac separates selection/live controls from near-white ink and rails.
// Both text on graphite paper and inverse glyphs on this fill remain readable.
inline uint32_t spot()     { return detail::foreground([](bool g) { return g ? rgb(181, 154, 232) : rgb(91, 49, 124); }); }
inline uint32_t ghost()    { return detail::foreground([](bool g) { return g ? gray(184) : rgb(174, 130, 151); }); }
// Branch is also a LIGHT fill behind dark pink scene text; keep it a surface
// interpolation, not a black polarity bridge (decorative rails may briefly dim).
inline uint32_t branch()   { return detail::surface([](bool g) { return g ? gray(255) : rgb(174, 145, 165); }); }
inline uint32_t subdued()  { return detail::foreground([](bool g) { return g ? gray(112) : rgb(207, 165, 184); }); }
inline uint32_t inverse()  {
    return visual_graphite() ? detail::mix(gray(0), gray(48), detail::shown * 2 - detail::kOne)
                             : detail::mix(rgb(242, 216, 226), gray(255), detail::shown * 2);
}
// Graphite deliberately merged some roles; retain their original pink colors.
inline uint32_t menu_ghost() { return detail::foreground([](bool g) { return g ? gray(184) : rgb(126, 100, 119); }); }
inline uint32_t shadow()     { return detail::foreground([](bool g) { return g ? gray(112) : rgb(174, 130, 151); }); }
inline uint32_t playhead()   { return detail::foreground([](bool g) { return g ? gray(255) : rgb(112, 139, 91); }); }

// Exact original integer smoothstep row gradient; graphite remains flat. Clamp
// at the shared public boundary before arithmetic, including extreme int inputs.
inline uint32_t paper_color(int y) {
    if (detail::shown == detail::kOne) return gray(36);
    y = y < 0 ? 0 : y > 239 ? 239 : y;
    constexpr int64_t d = 239LL * 239 * 239;
    const int64_t n = int64_t(y) * y * (717 - 2 * y);
    const uint32_t pink = rgb(unsigned((228 * d - 17 * n + d / 2) / d),
                              unsigned((194 * d - 7 * n + d / 2) / d),
                              unsigned((207 * d - n + d / 2) / d));
    return detail::mix(pink, gray(36), detail::shown);
}
} // namespace legacy

using Theme = theme_config::Id;
using legacy::rgb;
using legacy::gray;
constexpr unsigned kTransitionMs = legacy::kTransitionMs;
namespace detail {
inline theme_config::Settings selected{};
inline uint64_t serial = 0;
struct Roles {
    uint32_t paper, cell, cell_dim, ink, spot, ghost, branch, subdued, inverse;
    uint32_t menu_ghost, shadow, playhead;
    bool dark;
};
inline Roles extended{};
inline bool custom_path() { return !theme_config::legacy(selected.id); }
inline Roles derive(theme_config::Colors c) {
    using theme_config::mix;
    const bool dark = theme_config::luminance(c.paper) < .179;
    return {c.paper, mix(c.paper,c.ink,18), mix(c.paper,c.ink,10), c.ink, c.accent,
            mix(c.paper,c.ink,184), dark ? c.ink : mix(c.paper,c.ink,52),
            mix(c.paper,c.ink,79), theme_config::inverse(c.accent),
            mix(c.paper,c.ink,184), mix(c.paper,c.ink,92), c.accent, dark};
}
}
inline Theme current() { return detail::selected.id; }
inline theme_config::Settings settings() { return detail::selected; }
inline bool is_graphite() { return current() == Theme::Graphite; }
inline bool visual_graphite() { return detail::custom_path() ? detail::extended.dark : legacy::visual_graphite(); }
inline float paper_depth_scale() { return detail::custom_path() ? 1.f : legacy::paper_depth_scale(); }
inline uint64_t revision() { return detail::serial + legacy::revision(); }
inline void set(theme_config::Settings value) {
    if (!theme_config::valid(value)) value = {};
    if (value != detail::selected) ++detail::serial;
    detail::selected = value;
    if (detail::custom_path()) detail::extended = detail::derive(theme_config::colors(value));
    else legacy::set(value.id == Theme::Graphite ? legacy::Theme::Graphite : legacy::Theme::Pink);
}
inline void set(Theme value) { set(theme_config::Settings{value, detail::selected.custom}); }
inline void animate_to(theme_config::Settings value, uint64_t now_ms) {
    if (!theme_config::valid(value)) value = {};
    // Only the proven pink/gray pair fades. Other palettes commit atomically:
    // arbitrary RGB endpoints may have opposite/unpredictable text polarity.
    if (detail::custom_path() || !theme_config::legacy(value.id)) { set(value); return; }
    detail::selected = value;
    legacy::animate_to(value.id == Theme::Graphite ? legacy::Theme::Graphite : legacy::Theme::Pink, now_ms);
}
inline void animate_to(Theme value, uint64_t now_ms) {
    animate_to(theme_config::Settings{value, detail::selected.custom}, now_ms);
}
inline void capture(uint64_t now_ms) { if (!detail::custom_path()) legacy::capture(now_ms); }
inline uint32_t bg() { return detail::custom_path() ? detail::extended.paper : legacy::bg(); }
inline uint32_t cell() { return detail::custom_path() ? detail::extended.cell : legacy::cell(); }
inline uint32_t cell_dim() { return detail::custom_path() ? detail::extended.cell_dim : legacy::cell_dim(); }
inline uint32_t ink() { return detail::custom_path() ? detail::extended.ink : legacy::ink(); }
inline uint32_t spot() { return detail::custom_path() ? detail::extended.spot : legacy::spot(); }
inline uint32_t ghost() { return detail::custom_path() ? detail::extended.ghost : legacy::ghost(); }
inline uint32_t branch() { return detail::custom_path() ? detail::extended.branch : legacy::branch(); }
inline uint32_t subdued() { return detail::custom_path() ? detail::extended.subdued : legacy::subdued(); }
inline uint32_t inverse() { return detail::custom_path() ? detail::extended.inverse : legacy::inverse(); }
inline uint32_t menu_ghost() { return detail::custom_path() ? detail::extended.menu_ghost : legacy::menu_ghost(); }
inline uint32_t shadow() { return detail::custom_path() ? detail::extended.shadow : legacy::shadow(); }
inline uint32_t playhead() { return detail::custom_path() ? detail::extended.playhead : legacy::playhead(); }
inline uint32_t paper_color(int y) { return detail::custom_path() ? detail::extended.paper : legacy::paper_color(y); }
} // namespace seance::platform::ui3ds::palette
