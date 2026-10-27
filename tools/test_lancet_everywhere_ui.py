#!/usr/bin/env python3
"""Current lancet module proof, independent stencil + real full catalogue/captures.

No old receipts overwritten, no historical module-coordinate suites invoked,
no ARM/FTP/device action. Non-module pages have their own focused runner.
"""
import argparse
import json
from pathlib import Path
import sys
sys.dont_write_bytecode = True
import test_module_frame as frame
import test_module_headings_ui as host
import test_fader_lancet_ui as common

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT/'release/lancet-everywhere/modules'
BASE = ROOT/'.local-state/backups/20261005_101142_before-lancet-everywhere_3ds'
OWN = ROOT/'tools/lancet_everywhere_ui_cases.inc'


def preservation():
    original=json.loads((ROOT/'release/lancet-everywhere/before.json').read_text())
    kept={};modules={}
    for name,digest in original.items():
        path=ROOT/name
        if name.startswith(('core/','assets/')) or name in ('Makefile','VERSION'):
            assert host.sha(path)==digest, ('protected input changed',name)
            kept[name]=digest
        if name.startswith('platform/3ds/ui_module_focus_') or name=='platform/3ds/module_inspector.cpp':
            before=(BASE/path.name).read_text()
            assert host.sha_text(before)==digest
            expected=before.replace('module_fader::needle_horizontal','module_fader::horizontal').replace('module_fader::needle_vertical','module_fader::vertical')
            assert path.read_text()==expected, ('module changed beyond exact renderer name',name)
            modules[name]=host.sha(path)
    return dict(protected=kept,module_mapping_and_controllers_exact=modules,
                baseline='release/lancet-everywhere/before.json')


def cases():
    text=frame.current_cases()
    assert text.count('\nint run(')==1
    text=text.replace('\nint run(','\n'+OWN.read_text()+'\nint run(')
    text=text.replace('f::sample_fixture();CHECK(editor_style::init());',
                      'f::sample_fixture();CHECK(editor_style::init());f::primitives();')
    assert text.count('frame_header_actions(p);')==1
    text=text.replace('frame_header_actions(p);','frame_header_actions(p);lancet_stress(p);')
    text=text.replace('editor_style::shutdown();CHECK(editor_gpu::textures.empty());manifest.close();',
                      'std::printf("lancet independent primitive cases: %llu\\n",(unsigned long long)f::primitive_cases);\n editor_style::shutdown();CHECK(editor_gpu::textures.empty());manifest.close();')
    return text


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitize',action='store_true')
    args=parser.parse_args()
    guard=preservation();frame.fast_check()
    OUT.mkdir(parents=True,exist_ok=True)
    source=cases();private=OUT/('cases-'+host.stamp()+'.inc')
    with private.open('x') as stream:stream.write(source)
    # Pin potential support inputs BEFORE build, not merely after dependency discovery.
    pre={str(p.resolve()):host.sha(p) for directory in ('core','platform/3ds','tools')
         for p in (ROOT/directory).rglob('*') if p.is_file() and '__pycache__' not in p.parts}
    host.OUT=OUT;host.CASES=private;host.supplement=lambda:{}
    receipt,proof=host.build(ROOT,'sanitized' if args.sanitize else 'optimized',sanitized=args.sanitize,capture=True)
    for name,digest in proof['dependencies'].items():
        p=Path(name)
        if p.is_relative_to(receipt.parent):continue
        assert name in pre and pre[name]==digest, ('dependency changed during compilation',name)
    assert guard==preservation()
    frame.current_cases=lambda:source
    frame.verify(receipt)
    verification=json.loads((receipt.parent/'frame-verification.json').read_text())
    verification['limits']=[x for x in verification['limits'] if not x.startswith('optimized software host only')]
    verification['limits'].append(('asan/ubsan/leaks' if args.sanitize else 'optimized')+' native host; not hardware/FPS/audio or full functional suite')
    host.json_write(receipt.parent/'frame-verification.json',verification)
    log=Path(proof['run']['log']).read_text()
    assert log.count('lancet: all current modules extrema')==2
    assert 'lancet independent primitive cases:' in log
    host.json_write(receipt.parent/'lancet-verification.json',dict(status='passed',receipt=str(receipt),receipt_sha256=host.sha(receipt),
        renderer=host.sha(ROOT/'platform/3ds/module_fader.h'),preservation=guard,
        native_duals=proof['native_duals'],primitive_summary=next(x for x in log.splitlines() if x.startswith('lancet independent primitive cases:')),
        actual_compiler_dependencies_pinned_before_build=len(proof['dependencies']),
        sanitizer=bool(args.sanitize),geometry=verification))
    print('lancet proof:',receipt,flush=True)


if __name__=='__main__':main()
