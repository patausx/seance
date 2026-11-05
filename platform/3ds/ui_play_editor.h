#pragma once
#include <3ds.h>
#include "../../core/engine/units.h"
namespace seance::platform::ui3ds {
void play_editor_capture();
void play_editor_draw();
bool play_editor_top();
bool play_editor_touch(PlayUnit&,int,int);
void play_editor_drag(PlayUnit&,int,int);
void play_editor_release(bool touch_up=true); // false cancels navigation/lens gestures without activating a menu row
bool play_editor_back();
bool play_editor_word_modal();
bool play_editor_word_input(u32& down,u32 held,u32 up,bool blocked);
bool play_editor_word_touch(int x,int y); // modal preflight before rand/lens
bool play_editor_word_drag(int x,int y);
bool play_editor_word_release();
void play_editor_word_cancel();
int play_editor_selected();
void play_editor_select(int);
bool play_editor_button(unsigned down); // consumes A only in its editor context
unsigned play_editor_context_keys(unsigned down,unsigned held); // seq-origin ownership before global keys
void play_editor_open_cuts(int chain,int slot,bool from_sequencer);
}
