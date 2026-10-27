#!/usr/bin/env python3
"""Extract the exact current main touch dispatcher, as in patch_menu_runtime.py."""
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / 'platform/3ds/main.cpp').read_text()
match = re.search(r'^static void handle_touch\(u32 down, u32 held\) \{.*?^}', source, re.M | re.S)
assert match, 'main touch dispatcher not found'
output = Path('/tmp/seance_osc_input_native')
output.mkdir(parents=True, exist_ok=True)
(output / 'main_touch.inc').write_text('// Exact main.cpp handle_touch; generated, do not edit.\n' + match.group() + '\n')
def section(name, start, end):
    begin = source.index(start)
    finish = source.index(end, begin)
    (output / name).write_text('// Exact main.cpp input section; generated, do not edit.\n' + source[begin:finish] + '\n')

section('main_lens_keys.inc', '        // Held MOD owns entry/release', '        const bool discard_modal_had_frame')
section('main_lens_nav.inc', '        // Old-3DS global navigation', '        // START is the sequencer transport everywhere.')
section('main_lens_touch.inc', '        if (!stage_skip_touch && !param_skip_touch', '\n\n        if (g_exit_requested)')
section('main_trigger.inc', '        if (!stage_modal_frame && !nav_is_open() && (down & KEY_A)', '\n\n        if (g_view == View::Sequencer')
print('extracted actual main.cpp touch dispatcher + MOD key ownership/navigation/touch gate + manual trigger')
