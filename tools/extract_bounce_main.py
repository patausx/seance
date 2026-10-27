#!/usr/bin/env python3
"""Extract the actual new main routes; only unrelated controllers/HID are shimmed."""
from pathlib import Path
import hashlib
ROOT = Path(__file__).resolve().parents[1]
s = (ROOT/'platform/3ds/main.cpp').read_text()
out = Path('/tmp/seance_bounce_native'); out.mkdir(exist_ok=True)
def block(start):
    begin=s.index(start); opening=s.index('{',begin); depth=1; i=opening+1
    while depth:
        depth += (s[i]=='{')-(s[i]=='}'); i+=1
    return s[begin:i]
(out/'buttons.inc').write_text(block('        if (g_view == View::Bounce && g_overlay == Overlay::None && !nav_is_open())'))
(out/'touch.inc').write_text(block('    if (g_view == View::Bounce && g_overlay == Overlay::None)'))
(out/'bottom.inc').write_text(block('static void draw_bottom_surface(View view, Overlay overlay) {'))
(out/'source.sha256').write_text(hashlib.sha256(s.encode()).hexdigest())
print('extracted actual main bounce button ownership, touch dispatch and lower surface')
