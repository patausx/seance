#!/usr/bin/env python3
"""Read-only revalidation of retained REV core/UI evidence; writes one new receipt.

No compiler, audio rendering, historical suite, user data, device or deployment.
Only existing before/current inputs and recorded artifacts are hashed/compared.
"""
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'release/reverb-field'
BEFORE=OUT/'pre-edit-20261004T091002_493450Z'
CHANGED={'core/engine/reverb_dsp.h','core/engine/reverb_impl.inc','core/engine/units.h','core/engine/patch_io.cpp',
         'platform/3ds/ui_module_focus_reverb.cpp','platform/3ds/rev_readout.h','platform/3ds/sequencer_lock_format.h'}
NEW={'platform/3ds/reverb_field.h','platform/3ds/reverb_field_data.h'}


def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def load(p):return json.loads(Path(p).read_text())
def rel(p):return str(Path(p).resolve().relative_to(ROOT))
def current():
    paths=[p for folder in ('core','platform/3ds','assets/fonts') for p in (ROOT/folder).rglob('*') if p.is_file()]+[ROOT/'Makefile']
    return {rel(p):sha(p) for p in sorted(paths)}
def identity(p,item):
    assert p.stat().st_size==item['bytes'] and sha(p)==item['sha256'],p


def main():
    original=load(BEFORE/'sources.json');now=current()
    for name,digest in original.items():assert sha(BEFORE/'inputs'/name)==digest,name
    assert not set(original)-set(now),'removed production'
    changed={n for n in original if now[n]!=original[n]}
    assert changed==CHANGED,(changed,CHANGED)
    assert set(now)-set(original)==NEW,set(now)-set(original)
    core_path=sorted(OUT.glob('core-final-verification-*.json'))[-1]
    core=load(core_path);assert core['passed']
    core_runs=[]
    for name,digest in core['receipts'].items():
        path=ROOT/name;assert sha(path)==digest,name
        proof=load(path);assert proof['passed'] and proof['run']['exit']==proof['old_reader_run']['exit']==0
        for n,item in proof['inputs'].items():
            identity(path.parent/'inputs'/n,item);identity(ROOT/n,item)
        for n,item in proof['fixtures'].items():identity(path.parent/'fixtures'/n,item)
        core_runs.append(proof)
    assert core_runs[0]['inputs']==core_runs[1]['inputs'] and core_runs[0]['fixtures']==core_runs[1]['fixtures']
    pointer=load(OUT/'core-pre-edit-baseline.json')
    base=ROOT/pointer['directory'];assert sha(base/'receipt.json')==pointer['receipt_sha256']
    for name,item in pointer['fixtures'].items():
        identity(base/'fixtures'/name,item)
        assert core_runs[0]['fixtures'][name]==item,name
    ui=[];ui_refs={};inspections={};retained={}
    expected_case=(ROOT/'tools/module_headings_ui_cases.inc').read_text().replace('int main(', 'int unused_rev_field_header_main(',1)+'\n'+(ROOT/'tools/reverb_field_ui_cases.inc').read_text()
    for mode in ('optimized','sanitized'):
        ref=load(OUT/'ui'/(mode+'-latest.json'));path=Path(ref['receipt']);assert sha(path)==ref['sha256']
        p=load(path);assert p['status']=='passed' and p['run']['exit']==0
        assert p['case_sha256']==hashlib.sha256(expected_case.encode()).hexdigest()
        assert sha(p['binary'])==p['binary_sha256']
        for n,h in p['source_sha256'].items():assert now[n]==h,n
        for n,h in p['dependencies'].items():assert sha(n)==h,n
        for n,h in p['media'].items():assert sha(Path(p['capture'])/n)==h,n
        matches=[]
        for ip in (OUT/'ui').glob('inspection-'+mode+'-*.json'):
            q=load(ip)
            if q['receipt']==str(path) and q['status']=='passed':matches.append((ip,q))
        assert len(matches)==1,(mode,matches)
        ip,q=matches[0];assert q['receipt_sha256']==sha(path)
        assert q['runner_sha256']==sha(ROOT/'tools/test_reverb_field_ui.py')
        manifest=path.parent/'retained-inputs.json';assert sha(manifest)==q['retained']['manifest_sha256']
        payload=load(manifest);assert len(payload)==q['retained']['files']
        for n,item in payload.items():
            assert sha(path.parent/'inputs'/n)==item['sha256'],n
            assert sha(item['origin'])==item['sha256'],n
        for n,h in q['inspection']['presentation'].items():assert sha(Path(p['capture'])/n)==h,n
        inspections[rel(ip)]=sha(ip);ui_refs[rel(path)]=sha(path);retained[mode]=len(payload);ui.append((p,q['inspection']))
    a,b=ui[0][0],ui[1][0]
    surfaces=lambda p:{(r['id'],r['surface']):r for r in p['surfaces']}
    aa,bb=surfaces(a),surfaces(b);assert aa.keys()==bb.keys()
    for (name,side),r in aa.items():
        assert all(int(r[k])==0 for k in ('outside_rects','outside_lines','text_overlaps'))
        assert r['raster_fnv64']==bb[name,side]['raster_fnv64']
        assert sha(Path(a['capture'])/(name+'_'+side+'.ppm'))==sha(Path(b['capture'])/(name+'_'+side+'.ppm'))
    assert a['source_sha256']==b['source_sha256']
    assert ui[0][1]['behavior']==ui[1][1]['behavior']
    protected=load(OUT/'protected-artifacts.json')
    for name,digest in protected.items():assert sha(ROOT/'release'/name/'seance.3dsx')==digest
    nav=OUT/'navigation-check.log';assert 'pass nav-instant:' in nav.read_text() and 'pass stylus seam:' in nav.read_text()
    # Old bodies of these shared files remain exact outside the explicit REV additions.
    before=(BEFORE/'inputs/platform/3ds/sequencer_lock_format.h').read_text()
    after=(ROOT/'platform/3ds/sequencer_lock_format.h').read_text().replace('#include "rev_readout.h"\n','')
    extra='        if (param >= ReverbUnit::P_PREDELAY && param < ReverbUnit::P_COUNT) {\n            rev_readout::value(out,capacity,param,value,true);return;\n        }\n'
    assert after.replace(extra,'')==before
    proof=dict(status='passed',created_utc=datetime.now(timezone.utc).isoformat(),
        kind='rev-field-first-utility-package-host-verification',changed_production=sorted(changed),new_production=sorted(NEW),
        unchanged_production_count=len(original)-len(changed),production_sha256=now,
        before_inventory_sha256=sha(BEFORE/'sources.json'),core_receipt=rel(core_path),core_receipt_sha256=sha(core_path),
        core_groups_per_run=core['groups'],core_checks_per_run=core['current_tests_per_run'],
        frozen_fixture_count=core['frozen_fixtures'],frozen_pcm_samples=core['frozen_pcm_samples'],
        ui_receipts=ui_refs,ui_inspections=inspections,ui_retained_input_files=retained,
        ui_behavior_per_run=ui[0][1]['behavior'],exact_optimized_sanitized_surfaces=len(aa),
        headers_per_run=ui[0][1]['fixed_header_masks'],numeric_checks_per_run=ui[0][1]['numeric_base_source_checks'],
        presentation=ui[0][1]['presentation'],capture=rel(Path(a['capture'])),
        protected_3dsx=protected,navigation_log_sha256=sha(nav),verifier_sha256=sha(Path(__file__)),
        no_arm=True,no_deployment=True,no_user_data=True,
        limitations=['synthetic audio input, native software-host UI, not physical capture',
                     'no listening/hardware touch/performance/sleep/soak claims',
                     'not a broad regression suite or every user patch/control schedule'])
    assert now==current(),'production changed while verifying'
    stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')
    path=OUT/('final-verification-'+stamp+'.json');path.write_text(json.dumps(proof,indent=2,sort_keys=True)+'\n')
    print(rel(path),sha(path));print('changed:',len(changed),'+',len(NEW),'new; exact surfaces:',len(aa))


if __name__=='__main__':main()
