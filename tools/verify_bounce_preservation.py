#!/usr/bin/env python3
"""Verify the bounded bounce change set and all inherited source identities."""
from pathlib import Path
import hashlib,json,re
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def load(p):return json.loads((ROOT/p).read_text())
base=load('ui_mockups/bounce_runtime/preservation-baseline.json')
changed={p for p,h in base['sha256'].items() if sha(ROOT/p)!=h}
expected={'core/engine/engine.h','core/engine/engine.cpp','platform/3ds/menu_labels_data.h','platform/3ds/ui_sample_library.cpp','platform/3ds/ui_nav.cpp','platform/3ds/ui_nav.h','platform/3ds/ui_state.h','platform/3ds/main.cpp','platform/3ds/ui_top.cpp','Makefile','tools/test_nav_instant.py','tools/generate_menu_labels.py'}
assert changed==expected,(changed-expected,expected-changed)
for receipt,key in [('ui_mockups/bounce_core/verification.json','linked_core_source_hashes'),('ui_mockups/bounce_runtime/verification.json','sha256')]:
    for p,h in load(receipt)[key].items():assert sha(ROOT/p)==h,('stale proof',receipt,p)
old=(ROOT/base['backup']/'platform/3ds/menu_labels_data.h').read_text();new=(ROOT/'platform/3ds/menu_labels_data.h').read_text()
def bitmap(s):return bytes(int(v,16) for v in re.findall(r'0x([0-9a-f]{2})',s.split('bitmap[] = {')[1].split('};')[0]))
def labels(s):return re.findall(r'\{"([^"]+)", (\d+), (\d+), (\d+)\}',s)
a,b=bitmap(old),bitmap(new);x,y=labels(old),labels(new)
assert b[:len(a)]==a and y[:len(x)]==x and len(x)==40
assert (ROOT/'platform/3ds/ui_sample_library.cpp').read_text().replace(' && a.bounce == b.bounce','')==(ROOT/'.local-state/backups/20260910_123529_before-bounce-refs_ui_sample_library.cpp').read_text()
assert (ROOT/'platform/3ds/ui_top.cpp').read_text().replace('        case View::Bounce:      break; // dedicated frozen page is dispatched by main\n','')==(ROOT/'.local-state/backups/20260910_123919_before-bounce-dispatch_ui_top.cpp').read_text()
kept={p:h for p,h in base['sha256'].items() if p not in changed}
result={'baseline':base['backup'],'unchanged_count':len(kept),'unchanged':kept,'intended_changed':sorted(changed),'shared_labels':{'old_count':len(x),'old_bytes':len(a),'old_metrics_and_bitmap_exact':True,'appended':len(y)-len(x)},'core_normalization':load('ui_mockups/bounce_core/verification.json')['preservation'],'ui_sample_library_only_refs_equality_added':True,'ui_top_only_new_enum_exhaustiveness_case':True,'latest_device_untouched':{'artifact':'release/bird/seance.3dsx','sha256':sha(ROOT/'release/bird/seance.3dsx')}}
(ROOT/'ui_mockups/bounce_runtime/preservation.json').write_text(json.dumps(result,indent=2)+'\n')
print(f"preserved {len(kept)} inherited files; {len(x)} original masks/{len(a)} bytes exact; core + UI receipts current")
