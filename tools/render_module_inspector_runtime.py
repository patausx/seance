#!/usr/bin/env python3
"""Package actual C++ VCA/CHORUS inspector frames; no UI pixels are authored here.

Optional --baseline-source-dir recompiles the *original*, saved lower .cpp files
with selection-access adapters only, then byte-compares every unchanged lower
PPM and text dump. It does not simulate the old drawing or silently update goldens.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import shlex
import subprocess

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(command):
    subprocess.run([str(x) for x in command], cwd=ROOT, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=Path('/tmp/seance_module_inspector_test'))
    parser.add_argument('--output', type=Path, default=ROOT / 'ui_mockups/module_inspector_runtime')
    parser.add_argument('--baseline-source-dir', type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    raw = output / 'raw'
    raw.mkdir(parents=True, exist_ok=True)
    run([args.binary, '--dump-module-inspector', raw])
    rows = list(csv.DictReader((raw / 'manifest.tsv').open(), delimiter='\t'))
    audit = {'origin': 'actual C++ software raster; not hardware/PICA acceptance',
             'budget_scope': 'new top + real lower module + existing facade footer helpers; excludes hidden module shell and main Chains underlay',
             'frames': [],
             'baseline': None, 'peak_shared_objects': 0}
    cards = []
    for row in rows:
        name = row['frame']
        top_path, bottom_path = raw / f'{name}_top.ppm', raw / f'{name}_bottom.ppm'
        top, bottom = Image.open(top_path).convert('RGB'), Image.open(bottom_path).convert('RGB')
        assert top.size == (400, 240) and bottom.size == (320, 240)
        count = int(row['shared_objects'])
        assert 0 < count <= 8192
        # Blank outer margins only: both native screen rasters remain untouched.
        dual = Image.new('RGB', (400, 480), (0, 0, 0))
        dual.paste(top, (0, 0))
        dual.paste(bottom, (40, 240))
        dual.save(output / f'{name}_dual_native.png')
        dual.resize((800, 960), Image.Resampling.NEAREST).save(output / f'{name}_dual_2x.png')
        cards.append(dual)
        audit['peak_shared_objects'] = max(audit['peak_shared_objects'], count)
        audit['frames'].append(dict(row, top_sha256=sha(top_path), bottom_sha256=sha(bottom_path)))
    if args.baseline_source_dir:
        original = args.baseline_source_dir.resolve()
        baseline = output / 'original_lower'
        baseline.mkdir(exist_ok=True)
        # Obtain exactly the integration target's flags and portable source set.
        command = subprocess.check_output(['make', '-n', 'test-module-inspector'], cwd=ROOT, text=True).splitlines()[0]
        command = shlex.split(command)
        original_hashes = {}
        for module in ('vca', 'chorus'):
            source = original / f'ui_module_focus_{module}.cpp'
            assert source.is_file(), source
            text = source.read_text()
            assert f'int {module}_focus_selected(' not in text, 'baseline must predate the new API'
            original_hashes[source.name] = sha(source)
            # Source protection covers every lower drawing/gesture state, not just fixtures.
            accessors = (f'int {module}_focus_selected() {{ return g_{module}_mod_param; }}\n'
                         f'void {module}_focus_select(int param) {{\n'
                         f'    if (param >= 0 && param < 3) g_{module}_mod_param = param;\n'
                         '}\n\n')
            current_source = (ROOT / 'platform/3ds' / source.name).read_text()
            assert current_source.count(accessors) == 1, 'expected validated selection accessors only'
            assert current_source.replace(accessors, '', 1) == text, f'lower source changed: {source.name}'
            adapter = baseline / f'{module}_original_adapter.cpp'
            adapter.write_text(f'#include {json.dumps(str(source))}\n'
                               'namespace seance::platform::ui3ds {\n'
                               f'int {module}_focus_selected() {{ return g_{module}_mod_param; }}\n'
                               f'void {module}_focus_select(int p) {{ if(p>=0&&p<3)g_{module}_mod_param=p; }}\n'
                               '}\n')
            at = command.index(f'platform/3ds/ui_module_focus_{module}.cpp')
            command[at] = str(adapter)
        binary = baseline / 'original_lower_test'
        command[command.index('-o') + 1] = str(binary)
        command.append('-Iplatform/3ds')
        run(command)
        run([binary, '--dump-module-inspector', baseline / 'raw'])
        compared = []
        for row in rows:
            name = row['frame']
            if name.endswith(('_mod', '_temp')):
                continue  # Shared modal lower is explicitly permitted to migrate.
            for suffix in ('_bottom.ppm', '_bottom_text.tsv'):
                filename = name + suffix
                current, previous = raw / filename, baseline / 'raw' / filename
                assert current.read_bytes() == previous.read_bytes(), f'original lower parity failed: {filename}'
                compared.append({'file': filename, 'sha256': sha(current)})
        audit['baseline'] = {'original_sources': original_hashes, 'compared': compared,
                             'source_only_added_validated_accessors': True,
                             'method': 'original .cpp renderers + accessor-only adapters, same real C++ fixture/raster'}
    columns = 4
    sheet = Image.new('RGB', (400 * columns, 480 * ((len(cards) + columns - 1) // columns)), (0, 0, 0))
    for i, card in enumerate(cards):
        sheet.paste(card, ((i % columns) * 400, (i // columns) * 480))
    sheet.save(output / 'contact_sheet.png')
    (output / 'verification.json').write_text(json.dumps(audit, indent=2) + '\n')
    print(json.dumps({'output': str(output), 'frames': len(rows),
                      'peak_shared_objects': audit['peak_shared_objects'],
                      'original_lower_compared_files': len(audit['baseline']['compared']) if audit['baseline'] else 0}))


if __name__ == '__main__':
    main()
