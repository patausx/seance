#pragma once
#include <3ds.h>
namespace seance::platform::ui3ds {
u32 rgb(u8,u8,u8); u32 rgba(u8,u8,u8,u8);
u32 col_bg(); u32 col_panel(); u32 col_cell(); u32 col_cell_d(); u32 col_on(); u32 col_line(); u32 col_hi(); u32 col_hi2(); u32 col_shadow(); u32 col_shadow2(); u32 col_text(); u32 col_text_d(); u32 col_text_i(); u32 col_yellow(); u32 col_green(); u32 col_red();
void panel_cell(int,int,int,int,u32 fill=0); void panel_cell_down(int,int,int,int,u32 fill=0); bool hit(int,int,int,int,int,int); void help_bar(const char*);
}
