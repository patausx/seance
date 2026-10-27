#!/usr/bin/env python3
"""Current-only scoped native 28-module identity review; not a historical baseline comparison."""
import json
from pathlib import Path
import sys

sys.dont_write_bytecode = True
import test_module_headings_ui as headings
import verify_module_identity_current as verifier

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'release' / 'module-identity-current'

def main():
    assert len(sys.argv)==1 or sys.argv[1:]==['--verify-capture'], 'usage: test_module_identity_current_ui.py [--verify-capture]'
    # Reuse the scoped host driver/compiler only; never its frozen baseline,
    # preservation comparator, historical output directory or broad suite.
    headings.OUT=OUT
    headings.supplement=lambda: {}
    if len(sys.argv)>1:
        pointer=json.loads((OUT/'current-optimized-latest.json').read_text())
        receipt=Path(pointer['receipt'])
        assert headings.sha(receipt)==pointer['sha256']
    else:
        # A private copy adds MOUTH selected-detail states to existing 28-module
        # fixture; both inputs stay unmodified and the exact combined case text
        # is saved by the native builder inside its new dated receipt directory.
        OUT.mkdir(parents=True,exist_ok=True)
        cases=OUT/('current-cases-'+headings.stamp()+'.inc')
        with cases.open('x') as f:f.write(verifier.current_cases())
        headings.CASES=cases
        receipt,_=headings.build(ROOT,'current-optimized',capture=True)
    path,proof=verifier.inspect(receipt)
    print(json.dumps({'verification':str(path),'receipt':str(receipt),
                      'default_exact':proof['private_mask_exact_in_default_surfaces'],
                      'additional_exact':len(proof['extra_play_gran_workflows_mouth_selected_views']),
                      'inherited_lens_overlap_surfaces':proof['lens_text_overlaps_remaining']},indent=2))

if __name__=='__main__': main()
