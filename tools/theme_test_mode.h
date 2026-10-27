#pragma once
// Test-only selection. Existing graphite fixtures retain their default; runtime
// itself defaults to pink. Independent expected RGB math, not renderer returns.
#include "../platform/3ds/ui_palette.h"
#include <cstdlib>
#include <cstring>
namespace theme_test {
namespace p = seance::platform::ui3ds::palette;
inline void select() {
    const char* env = std::getenv("SEANCE_TEST_THEME");
    p::set(env && std::strcmp(env, "pink") == 0 ? p::Theme::Pink : p::Theme::Graphite);
}
inline bool gray() { return p::is_graphite(); }
constexpr uint32_t rgb(int r,int g,int b) { return 0xff000000u | unsigned(r) | (unsigned(g)<<8) | (unsigned(b)<<16); }
inline uint32_t paper(int y) {
    if(gray()) return rgb(36,36,36);
    y=y<0?0:y>239?239:y;
    const int64_t d=239LL*239*239, n=int64_t(y)*y*(717-2*y);
    return rgb(int((228*d-17*n+d/2)/d),int((194*d-7*n+d/2)/d),int((207*d-n+d/2)/d));
}
inline uint32_t ink() { return gray()?rgb(244,244,244):rgb(31,20,31); }
inline uint32_t spot() { return gray()?rgb(181,154,232):rgb(91,49,124); }
inline uint32_t ghost() { return gray()?rgb(184,184,184):rgb(126,100,119); }
inline uint32_t branch() { return gray()?rgb(255,255,255):rgb(174,145,165); }
inline uint32_t shimmer(int a) {
    if(gray()){const int g=244+(11*a+127)/255;return rgb(g,g,g);}
    return rgb(31+(69*a+127)/255,20+(34*a+127)/255,31+(102*a+127)/255);
}
}
