#include "ui_theme.h"
#include "ui_palette.h"
#include <citro2d.h>
#include "../../core/ui/draw.h"
namespace seance::platform::ui3ds {
u32 rgb(u8 r, u8 g, u8 b) { return C2D_Color32(r, g, b, 255); }
u32 rgba(u8 r, u8 g, u8 b, u8 a) { return C2D_Color32(r, g, b, a); }

u32 col_bg()      { return palette::bg(); }
u32 col_panel()   { return palette::bg(); }
u32 col_cell()    { return palette::cell(); }
u32 col_cell_d()  { return palette::cell_dim(); }
u32 col_on()      { return palette::spot(); }
u32 col_line()    { return palette::ink(); }
u32 col_hi()      { return palette::cell(); }
u32 col_hi2()     { return palette::ghost(); }
u32 col_shadow()  { return palette::shadow(); }
u32 col_shadow2() { return palette::subdued(); }
u32 col_text()    { return palette::ink(); }
u32 col_text_d()  { return palette::ghost(); }
u32 col_text_i()  { return palette::inverse(); }
u32 col_yellow()  { return palette::spot(); }
u32 col_green()   { return palette::ink(); }
u32 col_red()     { return palette::spot(); }

void panel_cell(int x, int y, int w, int h, u32 fill) {
    const u32 f = fill ? fill : col_cell();
    ui::rect(x, y, w, h, f);
    ui::frame(x, y, w, h, f == col_cell_d() ? col_shadow() : col_line());
}

void panel_cell_down(int x, int y, int w, int h, u32 fill) {
    const u32 f = fill ? fill : col_on();
    ui::rect(x, y, w, h, f);
    ui::frame(x, y, w, h, col_on());
}

bool hit(int tx, int ty, int x, int y, int w, int h) {
    return tx >= x && tx < x + w && ty >= y && ty < y + h;
}

void help_bar(const char* s) {
    ui::rect(0, 228, 320, 12, col_text());
    ui::text(4, 230, s, col_text_i());
}


}
