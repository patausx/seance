#!/usr/bin/env python3
"""Focused dead-UI proof; requires the preserved pre-cleanup snapshots, not git HEAD.

Compiles both actual main dispatchers in separate namespaces. The only shims are
page render calls; compare symbolic view/overlay inputs and ordered calls, not
numeric enums (these UI-only enums are deliberately not a storage format).
"""
from pathlib import Path
import hashlib
import json
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/resource_cleanup_runtime'
OLD_MAIN = ROOT / '.local-state/backups/20260912_162957_before-resource-cleanup_main.cpp'
OLD_STATE = ROOT / '.local-state/backups/20260912_163116_before-resource-cleanup_ui_state.h'
main = (ROOT / 'platform/3ds/main.cpp').read_text()
old_main = OLD_MAIN.read_text()
state = (ROOT / 'platform/3ds/ui_state.h').read_text()
old_state = OLD_STATE.read_text()
baseline = json.loads((OUT / 'baseline.json').read_text())['runtime']
for path, old in [('platform/3ds/main.cpp', OLD_MAIN), ('platform/3ds/ui_state.h', OLD_STATE)]:
    assert hashlib.sha256(old.read_bytes()).hexdigest() == baseline[path]

# Before removal the sole old curtain assignments were resets; no enable path.
assert re.findall(r'g_hinge\.active\s*=(?!=)\s*([^;]+);', old_main) == ['false', 'false']
assert 'bool active = false;' in old_main
# There was no entry into either old overlay. Backed-up affected files plus
# unchanged current files reconstruct the complete previous platform text.
old_paths = {
    'main.cpp': OLD_MAIN,
    **{name: ROOT / ('.local-state/backups/20260912_163116_before-resource-cleanup_' + name)
       for name in ('ui_state.h', 'ui_chains.h', 'ui_chains.cpp', 'ui_sigils.h', 'ui_sigils.cpp')},
}
for p in (ROOT / 'platform/3ds').iterdir():
    relative = str(p.relative_to(ROOT))
    if relative in baseline:
        original = old_paths.get(p.name, p)
        assert hashlib.sha256(original.read_bytes()).hexdigest() == baseline[relative], relative
old_platform = '\n'.join(old_paths.get(p.name, p).read_text() for p in (ROOT / 'platform/3ds').iterdir()
                         if p.suffix in ('.cpp', '.h'))
assert not re.search(r'(?<![=!<>])=(?!=)\s*Overlay::(?:Mixer|SigilTools)', old_platform)
assert not re.search(r'static_cast<Overlay>|\(Overlay\)', old_platform)
platform = '\n'.join(p.read_text() for p in (ROOT / 'platform/3ds').iterdir()
                     if p.suffix in ('.cpp', '.h'))
for name in ('g_hinge', 'HingeTransition', 'blackletter_gate', 'Overlay::Mixer',
             'Overlay::SigilTools', 'mixer_draw', 'mixer_set_value', 'mixer_touch_down',
             'sigil_tools_draw', 'sigil_tools_touch_down', 'Drag::ChainVolume', 'Drag::ChainPan'):
    assert name not in platform, name


def block(text, name):
    return re.search(r'^static void ' + name + r'\([^;{]+\) \{.*?^}', text, re.M | re.S)[0]


def enum(text, name):
    return re.search(r'enum class ' + name + r' : u8 \{[^}]+};', text)[0]

# The active ink compositor/input/time law is byte-identical, not reimplemented.
for name in ('begin_nav_ink_transition', 'nav_ink_input', 'draw_nav_ink_transition'):
    assert block(main, name) == block(old_main, name), name
for name in ('cancel_nav_ink_transition', 'capture_nav_ink_transition'):
    pattern = r'^static void ' + name + r'\(\) \{.*?^}'
    assert re.search(pattern, main, re.M | re.S)[0] == re.search(pattern, old_main, re.M | re.S)[0]

views = ('Chains', 'Sequencer', 'Performance', 'Sigils', 'Bounce')
overlays = ('None', 'Picker', 'Editor', 'SampleBrowser', 'PatchBrowser', 'ConfirmDiscard')
functions = ('patch_browser_draw', 'confirm_discard_draw', 'bounce_draw', 'sample_browser_draw',
             'picker_draw', 'chains_view_draw', 'editor_draw', 'sequencer_view_draw',
             'performance_view_draw', 'sigils_view_draw', 'mixer_draw', 'sigil_tools_draw')
cpp = '#include <cassert>\n#include <cstdio>\n#include <vector>\nusing u8=unsigned char;\n'
for space, text, header in [('before', old_main, old_state), ('after', main, state)]:
    cpp += f'namespace {space} {{\n' + enum(header, 'View') + '\n' + enum(header, 'Overlay') + '\n'
    cpp += 'std::vector<int> calls;\n'
    cpp += '\n'.join(f'void {fn}() {{ calls.push_back({i}); }}' for i, fn in enumerate(functions))
    cpp += '\n' + block(text, 'draw_bottom_surface') + '\n}\n'
cpp += 'int main() {\n'
for view in views:
    for overlay in overlays:
        for space in ('before', 'after'):
            cpp += (f'{space}::calls.clear(); {space}::draw_bottom_surface('
                    f'{space}::View::{view}, {space}::Overlay::{overlay});\n')
        cpp += 'assert(before::calls == after::calls);\n'
cpp += 'puts("pass cleanup: 30 actual old/new dispatch routes identical; retired input curtain/overlays absent; active ink functions exact");\n}\n'
OUT.mkdir(parents=True, exist_ok=True)
(OUT / 'dispatch_equivalence.cpp').write_text(cpp)
subprocess.run(['g++', '-std=c++17', '-O2', '-Wall', '-Wextra', str(OUT / 'dispatch_equivalence.cpp'),
                '-o', str(OUT / 'dispatch_equivalence')], check=True)
subprocess.run([str(OUT / 'dispatch_equivalence')], check=True)
(OUT / 'dispatch_verification.json').write_text(json.dumps({
    'routes': 30, 'result': 'pass', 'actual_source_extraction': True,
    'boundaries': 'page renderer calls only; no GPU or hardware timing claim',
    'sha256': {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
               for p in (OLD_MAIN, OLD_STATE, ROOT / 'platform/3ds/main.cpp',
                         ROOT / 'platform/3ds/ui_state.h', Path(__file__), OUT / 'dispatch_equivalence.cpp')},
}, indent=2) + '\n')
