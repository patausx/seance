#pragma once
#include <cstdint>
#include <cmath>

namespace seance::platform::ui3ds::theme_config {
// Explicit persisted ids; independent of project/engine serialization.
enum class Id : uint8_t { Pink = 0, Graphite = 1, Grimoire = 2, BlackMass = 3, Custom = 4 };
constexpr int kCount = 5;
constexpr uint32_t rgb(unsigned r, unsigned g, unsigned b) {
    return 0xff000000u | r | (g << 8) | (b << 16);
}
struct Colors { uint32_t paper, ink, accent; };
constexpr Colors kGrimoire{rgb(232,220,192), rgb(81,57,40), rgb(127,29,44)};
constexpr Colors kBlackMass{rgb(39,35,31), rgb(223,205,164), rgb(236,105,85)};
struct Settings {
    Id id = Id::Pink;
    Colors custom = kGrimoire;
};
constexpr bool operator==(Colors a, Colors b) {
    return a.paper == b.paper && a.ink == b.ink && a.accent == b.accent;
}
constexpr bool operator!=(Colors a, Colors b) { return !(a == b); }
constexpr bool operator==(Settings a, Settings b) { return a.id == b.id && a.custom == b.custom; }
constexpr bool operator!=(Settings a, Settings b) { return !(a == b); }
constexpr bool valid(Id id) { return unsigned(id) < unsigned(kCount); }
constexpr bool valid(Colors c) { return (c.paper >> 24) == 255 && (c.ink >> 24) == 255 && (c.accent >> 24) == 255; }
constexpr bool valid(Settings s) { return valid(s.id) && valid(s.custom); }
constexpr bool legacy(Id id) { return id == Id::Pink || id == Id::Graphite; }
constexpr Id next(Id id) { return Id(valid(id) ? (unsigned(id) + 1) % kCount : 0); }
constexpr const char* name(Id id) {
    switch (id) {
    case Id::Pink: return "pink";
    case Id::Graphite: return "gray";
    case Id::Grimoire: return "old grimoire";
    case Id::BlackMass: return "black mass";
    case Id::Custom: return "custom";
    }
    return "pink";
}
constexpr Colors colors(Settings s) {
    switch (s.id) {
    case Id::Pink: return {rgb(226,190,206), rgb(31,20,31), rgb(91,49,124)};
    case Id::Graphite: return {rgb(36,36,36), rgb(244,244,244), rgb(181,154,232)};
    case Id::Grimoire: return kGrimoire;
    case Id::BlackMass: return kBlackMass;
    case Id::Custom: return valid(s.custom) ? s.custom : kGrimoire;
    }
    return colors(Settings{});
}
constexpr uint32_t mix(uint32_t a, uint32_t b, unsigned amount) {
    const unsigned t = amount > 255 ? 255 : amount;
    uint32_t out = 0xff000000u;
    for (unsigned shift = 0; shift < 24; shift += 8)
        out |= (((((a >> shift) & 255) * (255 - t) + ((b >> shift) & 255) * t + 127) / 255) << shift);
    return out;
}
// Called only when editing/applying a theme, never from audio or per primitive.
inline double luminance(uint32_t color) {
    double result = 0;
    constexpr double weights[] = {.2126, .7152, .0722};
    for (int i = 0; i < 3; ++i) {
        const double c = ((color >> (8 * i)) & 255) / 255.;
        result += weights[i] * (c <= .04045 ? c / 12.92 : std::pow((c + .055) / 1.055, 2.4));
    }
    return result;
}
inline double contrast(uint32_t a, uint32_t b) {
    const double x = luminance(a), y = luminance(b);
    return (x > y ? (x + .05) / (y + .05) : (y + .05) / (x + .05));
}
inline uint32_t inverse(uint32_t fill) {
    return luminance(fill) > .179 ? rgb(0,0,0) : rgb(255,255,255);
}
inline bool low_contrast(Colors c) {
    return contrast(c.paper, c.ink) < 4.5 || contrast(c.paper, c.accent) < 3.;
}
constexpr int kSwatchCount = 24;
inline constexpr uint32_t swatches[kSwatchCount] = {
    rgb(232,220,192), rgb(223,205,164), rgb(231,206,131), rgb(239,188,153),
    rgb(226,190,206), rgb(240,216,238), rgb(212,223,176), rgb(244,244,244),
    rgb(81,57,40), rgb(39,35,31), rgb(36,36,36), rgb(31,20,31),
    rgb(56,40,71), rgb(53,39,62), rgb(37,59,57), rgb(72,46,54),
    rgb(127,29,44), rgb(162,30,43), rgb(236,105,85), rgb(169,39,80),
    rgb(91,49,124), rgb(181,154,232), rgb(38,61,192), rgb(203,232,121),
};
} // namespace seance::platform::ui3ds::theme_config
