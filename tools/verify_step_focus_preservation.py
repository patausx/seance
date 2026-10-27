#!/usr/bin/env python3
"""Bind variant03 host proofs to reviewed runtime; no rebuild/deploy/material cleanup."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT/'ui_mockups/step_focus_runtime'
CHANGED = 'platform/3ds/ui_sequencer.cpp'
REVIEWED = '5b26e2e5018b65a175ad5c8289732629ba2081cc5fc337a4d4277d7f953c4c22'
NEW = {'platform/3ds/step_focus_motion.h', 'platform/3ds/step_focus_ink.h'}


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def load(p):
    return json.loads(Path(p).read_text())


def main():
    baseline = load(OUT/'baseline.json')
    current = {str(p.relative_to(ROOT)): sha(p) for d in ('core', 'platform') for p in (ROOT/d).rglob('*')
               if p.is_file() and p.suffix in ('.c', '.cpp', '.h', '.hpp')}
    assert len(baseline) == 146 and len(current) == 148
    assert set(current)-set(baseline) == NEW
    assert {p for p in baseline if baseline[p] != current.get(p)} == {CHANGED}
    assert current[CHANGED] == REVIEWED, 'unreviewed sequencer changes'
    backup = ROOT/'.local-state/backups/20260912_184506_before-step-focus-03_ui_sequencer.cpp'
    assert sha(backup) == baseline[CHANGED]
    assert 'g_seq_page + (SEQ_STEPS / PAGE_STEPS) * (g_seq_bank + NUM_PATTERNS * int(g_seq_mode))' in (ROOT/CHANGED).read_text()
    # Current native constants:64steps/16perpage, eightbanks/eightlayers. All
    # Cartesian contexts must be unique; overlays resetting is no substitute.
    context_keys = [page + 4 * (bank + 8 * layer) for page in range(4) for bank in range(8) for layer in range(8)]
    assert len(context_keys) == len(set(context_keys)) == 256
    evidence = [OUT/'baseline.json', OUT/'runtime.diff', OUT/'ui/verification.json',
                OUT/'ui-sanitize/verification.json', OUT/'ui/neighbor-comparison-final.json',
                OUT/'before-ui/neighbor-results.json', OUT/'nav.log']
    ui = load(OUT/'ui/verification.json')
    sanitized = load(OUT/'ui-sanitize/verification.json')
    for directory, proof in (('ui', ui), ('ui-sanitize', sanitized)):
        compiled_path = OUT/directory/'compiled-sources.json'
        compiled = load(compiled_path)
        assert sha(compiled_path) == proof['binary']['sources_sha256']
        assert compiled['binary_sha256'] == proof['binary']['sha256']
        for p, digest in compiled['sources'].items():
            assert sha(ROOT/p) == digest, p
        evidence.append(compiled_path)
        assert proof['runtime_hashes'] == current
        for p, digest in proof['tool_hashes'].items():
            assert sha(ROOT/p) == digest, p
        assert proof['runs'] and all(r['returncode'] == 0 for r in proof['runs'])
        for run in proof['runs']:
            log = ROOT/run['log']
            assert sha(log) == run['log_sha256']
            evidence.append(log)
        for entry in proof['screens'] + proof['animations']:
            assert sha(ROOT/entry['path']) == entry['sha256'], entry['path']
        for pair in proof['preservation']:
            for side in ('top', 'bottom'):
                for version in ('before', 'after'):
                    e = pair[side]
                    assert sha(ROOT/e[version]) == e[version+'_sha256']
    neighbors = load(OUT/'ui/neighbor-comparison-final.json')
    assert neighbors['runtime_hashes'] == current
    assert neighbors['binary']['sha256'] == ui['binary']['sha256']
    assert sha(ROOT/neighbors['baseline_binary']['path']) == neighbors['baseline_binary']['sha256']
    assert len(neighbors['runs']) == 6
    for run in neighbors['runs']:
        assert run['same_baseline_failure_signature']
        assert run['returncode'] == (1 if run['suite'] == 'slice' else 0)
        assert sha(ROOT/run['log']) == run['log_sha256']
        assert sha(ROOT/run['baseline_log']) == run['baseline_sha256']
        evidence.extend((ROOT/run['log'], ROOT/run['baseline_log']))
    motion_paths = sorted((OUT/'motion').glob('run-*/verification.json'))
    motion_path = next(p for p in reversed(motion_paths) if load(p)['sanitized'])
    motion = load(motion_path)
    assert motion['status'] == 'pass'
    for p, digest in motion['sources'].items():
        assert sha(ROOT/p) == digest, p
    evidence.append(motion_path)
    evidence.extend(motion_path.parent/r['log'] for r in motion['runs'])
    evidence.extend(ROOT/p for p in ('docs/step-focus-runtime.md', 'tools/test_step_focus_motion.py',
        'tools/step_focus_motion_test.cpp', 'tools/test_step_focus_ui.py', 'tools/step_focus_ui_test.h',
        'tools/verify_step_focus_preservation.py', 'tools/sequencer_test.cpp',
        'tools/sequencer_test_stubs/3ds.h', 'tools/sequencer_test_stubs/citro2d.h'))
    result = dict(status='focused host proof pass; preexisting slice fixture failure retained separately',
        runtime_hashes=current, unchanged_baseline_files=145, reviewed_changed_files=[CHANGED], new_files=sorted(NEW),
        sources_and_receipts={str(p.relative_to(ROOT)): sha(p) for p in evidence},
        native_screens=len(ui['screens']), native_duals=len(ui['frames']), native_pixels=sum(s['pixels'] for s in ui['screens']),
        native_animations=len(ui['animations']), peak_paired_objects=ui['peak_paired_objects'],
        before_after_duals=len(ui['preservation']), unique_page_bank_layer_contexts=len(context_keys),
        changed_pixels=sum(p[s]['changed_pixels'] for p in ui['preservation'] for s in ('top','bottom')),
        limitations=['no full suite, ARM build, FTP, CIA, listening or hardware/FPS claim',
                     'layer7 baseline capture omitted from before/after: earlier fixture selected unavailable FM row, final selects loaded PLAY',
                     '7,878 non-font When slash union pixels excluded per theme from host Bresenham-vs-quad oracle; geometry independently proved',
                     'both old/new slice suites 7/8 with same old assertion line259; no runtime workaround'])
    (OUT/'finalization.json').write_text(json.dumps(result, indent=2)+'\n')
    print('pass: 145 unchanged runtime files; one reviewed sequencer diff + two new headers; all native proof hashes exact')


if __name__ == '__main__':
    main()
