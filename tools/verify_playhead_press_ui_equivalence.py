#!/usr/bin/env python3
"""Cross-check final optimized/sanitized native captures and all sanitizer logs."""
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / 'ui_mockups/playhead_press_runtime'

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def main():
    paths = [BASE / mode / 'verification.json' for mode in ('ui', 'ui-sanitize')]
    proofs = [json.loads(p.read_text()) for p in paths]
    a, b = proofs
    assert a['status'] == b['status'] == 'pass'
    assert not a['sanitized'] and b['sanitized']
    assert a['sources'] == b['sources'] and a['tools'] == b['tools']
    roots = [(ROOT / p['binary']['path']).parent for p in proofs]
    for proof, root in zip(proofs, roots):
        assert sha(ROOT / proof['binary']['path']) == proof['binary']['sha256']
        for name, digest in proof['sources'].items():
            assert sha(ROOT / name) == digest, ('current source', name)
        for name, digest in proof['tools'].items():
            assert sha(ROOT / name) == digest, ('current runner', name)
        for name, digest in proof['evidence'].items():
            assert sha(root / name) == digest, ('evidence', root, name)
    raw = [{name: digest for name, digest in p['evidence'].items() if name.endswith('.ppm')} for p in proofs]
    assert raw[0] and raw[0] == raw[1], 'optimized/sanitized raw native captures differ'
    pixels = 0
    for name in raw[0]:
        header = (roots[0] / name).read_bytes().split(b'\n', 3)
        assert header[0] == b'P6' and header[2] == b'255'
        width, height = map(int, header[1].split())
        assert width in (320, 400) and height == 240
        assert len(header[3]) == width * height * 3
        pixels += width * height
    diagnostics = re.compile(r'ERROR: (?:AddressSanitizer|LeakSanitizer)|SUMMARY: (?:AddressSanitizer|UndefinedBehaviorSanitizer|LeakSanitizer)|runtime error:|AddressSanitizer:DEADLYSIGNAL')
    logs = {}
    for proof, root in zip(proofs, roots):
        for run in proof['runs']:
            log = ROOT / run['log']
            assert sha(log) == run['sha256']
            assert not diagnostics.search(log.read_text()), ('sanitizer diagnostic', log)
            logs[str(log.relative_to(ROOT))] = sha(log)
    result = dict(status='pass', created_utc=datetime.now(timezone.utc).isoformat(),
                  tools={str(Path(__file__).relative_to(ROOT)): sha(__file__)},
                  authorities={str(p.relative_to(ROOT)): sha(p) for p in paths},
                  binaries={p['binary']['path']: p['binary']['sha256'] for p in proofs},
                  compiled_sources=len(a['sources']), raw_screen_pairs=len(raw[0]),
                  exact_rgb_pixels=pixels, raw_capture_sha256=raw[0],
                  sanitizer_diagnostics=0, logs=logs,
                  limitations=['native host rasters only, not GPU or hardware acceptance',
                               'known old slice line259 failure remains; this adds no full-suite claim'])
    out = BASE / 'ui-build-equivalence.json'
    out.write_text(json.dumps(result, indent=2) + '\n')
    print('pass optimized/sanitized native equivalence:', len(raw[0]), 'raw screen pairs,', pixels,
          'exact RGB pixels,', len(logs), 'logs without sanitizer diagnostics')
    print(out)

if __name__ == '__main__':
    main()
