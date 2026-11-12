#include "ui_play_editor.h"
#include "ui_module_focus_internal.h"
#include "play_readout.h"

namespace seance::platform::ui3ds {
using C=PlayUnit;

int play_focus_selected(){return play_editor_selected();}
void play_focus_select(int p){play_editor_select(p);}
void play_focus_release(){play_editor_release(false);}
void play_focus_touch_release(C&){play_editor_release();}
bool play_focus_back(C&){return play_editor_back();}
bool play_focus_touch_header(C&,int,int){return false;}
void play_focus_draw_header(const C&){}
void play_focus_value_text(char* out,size_t size,int p,q15 raw){
    // Default (legacy, unquantized) readout law; the inline PLAY editor owns its DTO.
    play_readout::value_text(out,size,p,raw,false,false,C::PitchMode::Legacy,MusicalScale{});
}
bool play_focus_touch_body(C& u,int tx,int ty){return play_editor_touch(u,tx,ty);}
void play_focus_drag(C& u,int tx,int ty){play_editor_drag(u,tx,ty);}
void play_focus_capture(){play_editor_capture();}
void play_focus_draw(const C&){play_editor_draw();}
bool play_focus_draw_top(){return play_editor_top();}
} // namespace seance::platform::ui3ds
