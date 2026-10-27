#!/usr/bin/env python3
"""Package three actual main project-overlay LCD pairs; author no UI pixels."""
import csv
import hashlib
import json
import subprocess
from pathlib import Path
from PIL import Image
import patch_menu_runtime as patch

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/legacy_project_runtime'
BASE = ROOT / '.local-state/backups/20260910_092942_before-legacy-project-upper_main.cpp'

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    source = patch.MAIN.read_text()
    assert (patch.GEN / 'source.sha256').read_text() == sha(patch.MAIN), 'rebuild stale main extraction'
    OUT.mkdir(parents=True, exist_ok=True)
    result = subprocess.run(['/tmp/seance_patch_menu_test', '--dump-legacy-project', str(OUT)], cwd=ROOT, capture_output=True, text=True)
    (OUT / 'capture.log').write_text(result.stdout + result.stderr)
    result.check_returncode()
    rows = list(csv.DictReader((OUT / 'frames.tsv').open(), delimiter='\t'))
    assert len(rows) == 3
    lower_identity = {}
    for row, old_name in zip(rows, ['01_slots', '02_replace', '03_unsaved']):
        assert int(row['out_of_bounds']) == 0 and int(row['shared_objects']) < 8192
        name = row['name']
        images = []
        for side, width in [('top',400),('bottom',320)]:
            path = OUT / f'{name}_{side}.ppm'
            im = Image.open(path).convert('RGB')
            assert im.size == (width,240)
            im.save(OUT / f'{name}_{side}.png')
            images.append(im)
        # Presentation only: center the actual untouched LCD pixels on a black gap.
        dual = Image.new('RGB',(400,488),(0,0,0))
        dual.paste(images[0],(0,0)); dual.paste(images[1],(40,248))
        dual.save(OUT / f'{name}_dual.png')
        dual.resize((800,976),Image.Resampling.NEAREST).save(OUT / f'{name}_dual_2x.png')
        old = ROOT / 'ui_mockups/patch_menu_runtime' / f'{old_name}.ppm'
        assert old.read_bytes() == (OUT / f'{name}_bottom.ppm').read_bytes(), 'historical lower pixels changed'
        lower_identity[name] = {'historical':str(old.relative_to(ROOT)), 'ppm_sha256':sha(old)}
    old_source = BASE.read_text()
    old_functions = patch.FUNCTIONS[:8]
    function_hashes = {}
    for name in old_functions:
        current = patch.function(source,name)
        assert current == patch.function(old_source,name), name
        function_hashes[name] = hashlib.sha256(current.encode()).hexdigest()
    # All main bytes outside the bounded presentation additions stay exact.
    begin = source.index('// Project overlays own both LCDs,')
    end = source.index('void patch_browser_buttons(',begin)
    normalized = source[:begin] + source[end:]
    normalized = normalized.replace('    // Both project surfaces are opaque; don\'t spend the shared object pool on\n    // hidden chains, sequencer, performance or sigils underneath them.\n    if (overlay == Overlay::PatchBrowser) { patch_browser_draw(); return; }\n    if (overlay == Overlay::ConfirmDiscard) { confirm_discard_draw(); return; }\n','',1)
    normalized = normalized.replace('        project_overlay_capture();\n','',1)
    normalized = normalized.replace('        else if (!project_overlay_draw_top()) top_draw();','        else top_draw();',1)
    normalized = normalized.replace('        if (nav_is_open() && g_overlay == Overlay::ConfirmDiscard) confirm_discard_draw();','        if (g_overlay == Overlay::ConfirmDiscard) confirm_discard_draw();',1)
    assert normalized == old_source, 'non-project main bytes changed'
    paths = ['platform/3ds/main.cpp','tools/menu_stage1_test.cpp','tools/patch_menu_test.h',
             'tools/patch_menu_runtime.py','tools/render_legacy_project_runtime.py','tools/test_nav_instant.py',
             'platform/3ds/menu_style.cpp','platform/3ds/menu_labels_data.h','core/ui/font.cpp',
             'core/engine/project_session.cpp','core/engine/project_session.h']
    summary = json.loads((OUT / 'summary.json').read_text())
    receipt = {
        'origin':'3 actual native main project-overlay LCD pairs; existing C++ raster and font, exact normal app dispatch + draw_bottom_surface',
        'check':'make test-patch-ui; python3 tools/test_nav_instant.py',
        'summary':summary,'frames':rows,'historical_lower_pixel_identity':lower_identity,
        'preservation':{'backup':str(BASE.relative_to(ROOT)), 'backup_sha256':sha(BASE),
                        'all_non_project_main_bytes_exact':True,'old_patch_functions_exact':function_hashes},
        'source_sha256':{p:sha(ROOT/p) for p in paths},
        'binary_sha256':sha(Path('/tmp/seance_patch_menu_test')),
        'limits':'real ProjectSession and extracted project UI/normal app draw dispatch; SD/transaction outcomes and unrelated page/nav/clock draw boundaries shimmed. No persistence, engine audio, complete app integration, ARM or hardware claim. Frozen UI-only upper; existing lower reads same UI globals without mutation. Main opaque ownership skips hidden-view GPU objects; pre-GPU unrelated captures otherwise unchanged.',
        'deployment':'none'
    }
    (OUT / 'verification.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(f'3 native duals; historical lower pixels exact; shared peak {summary["peak_shared_objects"]}/8192; {OUT}')

if __name__ == '__main__':
    main()
