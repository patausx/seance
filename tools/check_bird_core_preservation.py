#!/usr/bin/env python3
"""Exact old-core preservation; only six additive BIRD integration regions allowed."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / 'ui_mockups/bird_runtime/baseline.json'
BACKUP = ROOT / '.local-state/backups/bird-start_20260910_140657'

def sha(data):
    return hashlib.sha256(data).hexdigest()

def main():
    baseline = json.loads(BASE.read_text())
    additions = {
        'core/engine/unit.h': [b'    Bird,           // monophonic triggered whistle / bird call source\n'],
        'core/engine/units.cpp': [b'#include "bird_tables.h"\n', b'        case UnitKind::Bird:         unit = new(std::nothrow) BirdUnit(); break;\n'],
        'core/engine/engine.cpp': [b'        case UnitKind::Bird:          return "BIRD";\n'],
    }
    text = (ROOT / 'core/engine/units.h').read_bytes()
    start = text.index(b'// ---------- BIRD: one latched, finite call; gain alone remains live ----------\n')
    end = text.index(b'// ---------- processors ----------', start)
    additions['core/engine/units.h'] = [text[start:end]]
    units = (ROOT / 'core/engine/units.cpp').read_bytes()
    marker = b'\n// ============================ BIRD (additive implementation) ============================\n'
    body = units[units.index(marker) + len(marker):]
    packaging = json.loads((ROOT / 'ui_mockups/bird_core/packaging.json').read_text())
    original = (ROOT / packaging['former_source_backup']).read_bytes()
    prefix = packaging['stripped_include_prefix'].encode()
    assert original.startswith(prefix) and body == original[len(prefix):], 'packaged DSP body exact'
    assert sha(original) == packaging['former_source_sha256']
    assert sha(body) == packaging['exact_appended_body_sha256']
    additions['core/engine/units.cpp'].append(marker + body)
    unchanged, additive = {}, {}
    for name, original_hash in baseline.items():
        if not name.startswith('core/'):
            continue  # parallel platform work belongs to the parent, not this proof
        old = (BACKUP / name).read_bytes()
        now = (ROOT / name).read_bytes()
        assert sha(old) == original_hash, ('baseline backup integrity', name)
        if name in additions:
            normalized = now
            for added in additions[name]:
                assert normalized.count(added) == 1, ('single additive region', name)
                normalized = normalized.replace(added, b'', 1)
            assert normalized == old, ('all old bytes exact', name)
            additive[name] = {'baseline_sha256': original_hash, 'current_sha256': sha(now),
                              'added_regions': len(additions[name]),
                              'added_bytes': sum(map(len, additions[name])),
                              'addition_sha256': [sha(added) for added in additions[name]],
                              'normalized_equals_backup': True}
        else:
            assert now == old, ('old core changed', name)
            unchanged[name] = original_hash
    assert set(additive) == set(additions)
    assert len(additive) == 4 and sum(map(len, additions.values())) == 6
    new = {'core/engine/bird_tables.h'}
    current_core = {str(p.relative_to(ROOT)) for p in (ROOT / 'core').rglob('*') if p.is_file()}
    assert current_core - set(baseline) == new, ('unexpected new core files', current_core - set(baseline))
    receipt = {'baseline': str(BASE.relative_to(ROOT)), 'backup': str(BACKUP.relative_to(ROOT)),
               'scope': 'core only; platform belongs to parallel parent implementation',
               'baseline_core_count': len(unchanged) + len(additive),
               'unchanged_core_count': len(unchanged), 'unchanged_core_sha256': unchanged,
               'six_additive_regions_across_four_files': additive,
               'exact_relocated_implementation_body_sha256': sha(body),
               'new_core_sha256': {name: sha((ROOT / name).read_bytes()) for name in sorted(new)},
               'serializer_unchanged': True, 'all_old_dsp_runtime_bytes_preserved': True}
    out = ROOT / 'ui_mockups/bird_core/preservation.json'
    out.write_text(json.dumps(receipt, indent=2) + '\n')
    print(f'bird core preservation: {len(unchanged)} old files exact; six additive regions across four old files; one new table header; exact relocated BIRD body; serializer and every old DSP implementation exact')

if __name__ == '__main__':
    main()
