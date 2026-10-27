#!/usr/bin/env python3
"""Recheck current focused authorities and protected material without rebuilding."""
from datetime import datetime, timezone
import difflib
import hashlib
import json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'ui_mockups/playhead_press_runtime'
CORE=OUT/'core/run-20260912T202805.630493Z/verification.json'
SCALAR=OUT/'scalar/run-20260912T203836.870967Z/verification.json'
def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def load(path):
    return json.loads(Path(path).read_text())
def verify(values, root=ROOT):
    for name,digest in values.items():
        assert sha(root/name)==digest, ('hash mismatch',str(root/name))
    return len(values)
def runtime():
    return {str(p.relative_to(ROOT)):sha(p) for directory in ('core','platform')
            for p in (ROOT/directory).rglob('*') if p.is_file() and p.suffix in ('.h','.hpp','.cpp','.c','.inc')}
def main():
    baseline=load(OUT/'baseline.json');current=runtime()
    assert set(current)-set(baseline['runtime'])=={'platform/3ds/step_feedback.h'}
    assert not set(baseline['runtime'])-set(current)
    changed={p for p,h in baseline['runtime'].items() if current[p]!=h}
    assert changed=={'core/engine/engine.h','core/engine/engine.cpp','platform/3ds/ui_sequencer.cpp'}, changed
    verify(baseline['protected']);verify(baseline['studies'])
    inherited=load(OUT/'inherited-protected.json')
    assert sha(ROOT/inherited['source'])==inherited['source_sha256']
    old=load(ROOT/inherited['source'])['protected_unchanged']
    assert all(old[p]==h for p,h in baseline['runtime'].items())
    verify(inherited['artifacts'])
    assert sha(OUT/'before-ui_sequencer.cpp')==baseline['runtime']['platform/3ds/ui_sequencer.cpp']
    differences=[]
    for name,old_path in [('core/engine/engine.h',OUT/'core/baseline/core/engine/engine.h'),
                          ('core/engine/engine.cpp',OUT/'core/baseline/core/engine/engine.cpp'),
                          ('platform/3ds/ui_sequencer.cpp',OUT/'before-ui_sequencer.cpp')]:
        assert sha(old_path)==baseline['runtime'][name]
        differences.extend(difflib.unified_diff(old_path.read_text().splitlines(True),
                           (ROOT/name).read_text().splitlines(True),fromfile='before/'+name,tofile='after/'+name))
    name='platform/3ds/step_feedback.h'
    differences.extend(difflib.unified_diff([],(ROOT/name).read_text().splitlines(True),fromfile='/dev/null',tofile='after/'+name))
    assert (OUT/'runtime.diff').read_text()==''.join(differences)
    before=load(OUT/'before-ui-host.sources.json')
    assert sha(OUT/'before-ui-host')==before['binary_sha256']
    matched={p:h for p,h in baseline['runtime'].items() if p in before['sources']}
    assert all(before['sources'][p]==h for p,h in matched.items())
    core=load(CORE);assert core['result']=='pass'
    verify(core['current_core']);verify(core['harness']);verify(core['evidence'],CORE.parent)
    assert core['baseline_core']=={p:h for p,h in baseline['runtime'].items() if p.startswith('core/')}
    scalar=load(SCALAR);assert scalar['status']=='pass'
    verify(scalar['sources']);verify(scalar['evidence'])
    assert all(run['returncode']==0 for run in scalar['runs'])
    receipts=[CORE,SCALAR];ui_counts={}
    for mode in ('ui','ui-sanitize'):
        authority=OUT/mode/'verification.json';ui=load(authority);assert ui['status']=='pass'
        assert ui['sanitized']==(mode=='ui-sanitize')
        verify(ui['sources']);verify(ui['tools']);verify(ui['originals'])
        binary=ROOT/ui['binary']['path'];run_dir=binary.parent
        assert sha(binary)==ui['binary']['sha256']
        stamp=load(run_dir/'compiled-sources.json')
        assert sha(run_dir/'compiled-sources.json')==ui['binary']['source_stamp_sha256']
        assert stamp['sources']==ui['sources'] and stamp['binary_sha256']==sha(binary)
        assert all(ui['sources'][p]==h for p,h in current.items())
        verify(ui['evidence'],run_dir)
        assert ui['core_receipt_sha256']==sha(CORE)
        assert len(ui['neighbors'])==8, 'final proof must include both-theme neighbors'
        for row in ui['neighbors']:
            if row['suite']=='slice':
                assert row['before_exit']==row['after_exit']==1
                assert row['before_failures']==row['after_failures'] and len(row['after_failures'])==1
                assert '[line 259]' in row['after_failures'][0]
            else:assert row['result']=='pass' and row['after_exit']==0
        if mode=='ui':
            assert ui['screens'] and ui['animations'] and 0<ui['peak_paired_objects']<8192
            for screen in ui['screens']:assert sha(ROOT/screen['path'])==screen['sha256']
            for animation in ui['animations']:
                assert sha(ROOT/animation['path'])==animation['sha256']
                assert animation.get('native_rgb_exact',animation.get('palette_rgb_exact')) is True
            assert len(ui['before_after'])==2 and all(row['unchanged_screens']>0 for row in ui['before_after'])
        else:
            builds=[run for run in ui['runs'] if '-fsanitize=address,undefined' in run['command']]
            assert len(builds)==1 and builds[0]['returncode']==0
        ui_counts[mode]=dict(screens=len(ui['screens']),animations=len(ui['animations']),
                            peak_paired_objects=ui['peak_paired_objects'],compiled_sources=len(ui['sources']))
        receipts.append(authority)
    equivalence=load(OUT/'ui-build-equivalence.json');assert equivalence['status']=='pass'
    for field in ('tools','authorities','binaries','logs'):verify(equivalence[field])
    assert equivalence['sanitizer_diagnostics']==0 and equivalence['raw_screen_pairs']>0
    assert set(equivalence['authorities'])=={str((OUT/mode/'verification.json').relative_to(ROOT)) for mode in ('ui','ui-sanitize')}
    review=load(OUT/'review.json');verify(review['files'])
    assert sha(ROOT/review['authority'])==review['authority_sha256']
    documents=[ROOT/'docs/playhead-press-runtime.md',ROOT/'tools/verify_playhead_press_preservation.py',
               OUT/'checks/main-nav.log',OUT/'baseline.json',OUT/'inherited-protected.json',
               OUT/'before-ui_sequencer.cpp',OUT/'before-ui-host',OUT/'before-ui-host.sources.json']
    documents.extend(OUT/name for name in ('runtime.diff','review.json','ui-build-equivalence.json'))
    final=dict(status='pass',created_utc=datetime.now(timezone.utc).isoformat(),scope='local runtime only; no ARM or deployment',
               runtime=current,changed_existing=sorted(changed),new_runtime=['platform/3ds/step_feedback.h'],
               unchanged_runtime=len(baseline['runtime'])-len(changed),protected_artifacts=inherited['artifacts'],
               protected_studies=baseline['studies'],old_host_runtime_baseline_matches=len(matched),
               evidence={str(p.relative_to(ROOT)):sha(p) for p in receipts+documents},ui_counts=ui_counts,
               limitations=['not full-suite success; exact old slice fixture line259 failure retained in both themes',
                            'no hardware/GPU/FPS/listening proof, ARM build, FTP, CIA or material cleanup'])
    (OUT/'finalization.json').write_text(json.dumps(final,indent=2)+'\n')
    print('pass final preservation:',len(current),'runtime;',final['unchanged_runtime'],'unchanged;',
          len(inherited['artifacts']),'protected root/release artifacts;',len(baseline['studies']),'preserved study files')
    print(json.dumps(ui_counts,indent=2));print(OUT/'finalization.json')
if __name__=='__main__':main()
