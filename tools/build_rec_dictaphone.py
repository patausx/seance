#!/usr/bin/env python3
"""One clean local ARM release, gated by exact current REC host proofs. Never deploys."""
from datetime import datetime,timezone
from pathlib import Path
import hashlib,json,os,subprocess,time
import test_rec_dictaphone_ui as ui
import test_rec_dictaphone_io as disk
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'release/rec-dictaphone'
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def main():
    ui_proof=json.loads((OUT/'ui-proof.json').read_text());assert ui_proof['checks']['status']=='passed';ui.verify(ui_proof)
    io=json.loads((OUT/'io-proof.json').read_text());assert io['status']=='passed' and io['inputs_still_exact'];assert disk.inputs()==io['inputs'];assert sha(io['binary'])==io['binary_sha256']
    summary=json.loads((OUT/'core-proof.json').read_text());assert summary['passed'];report=ROOT/summary['report'];assert sha(report)==summary['report_sha256'];core=json.loads(report.read_text());assert core['passed'] and core['source_inputs_unchanged']
    for name,v in core['inputs'].items():assert sha(ROOT/name)==v['sha256'],name
    names=set(json.loads((ROOT/'release/gran-perform/build.json').read_text())['source_hashes'])
    names.update(('platform/3ds/rec_ui.h','platform/3ds/rec_type_data.h'))
    before={p:sha(ROOT/p) for p in sorted(names)}
    stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ');build='build-rec-dictaphone-'+stamp
    assert not (ROOT/build).exists();assert not (OUT/'seance.3dsx').exists(),'do not silently rebuild/overwrite a ready release'
    cmd=['make','-j2','BUILD='+build,'TARGET=release/rec-dictaphone/seance'];env=dict(os.environ,DEVKITPRO='/opt/devkitpro',DEVKITARM='/opt/devkitpro/devkitARM')
    env['PATH']='/opt/devkitpro/tools/bin:/opt/devkitpro/devkitARM/bin:'+env['PATH']
    start=time.monotonic();log=OUT/('build-'+stamp+'.log')
    with log.open('x') as f:result=subprocess.run(cmd,cwd=ROOT,env=env,stdout=f,stderr=subprocess.STDOUT)
    print(log.read_text()[-6000:]);seconds=round(time.monotonic()-start,2)
    receipt=dict(finished_at=datetime.now(timezone.utc).isoformat(),command=cmd,build_directory=build,clean_build=True,exit=result.returncode,seconds=seconds,source_hashes=before,source_inputs_unchanged=all(sha(ROOT/p)==h for p,h in before.items()),log=str(log.relative_to(ROOT)),deployed=False,hardware_tested=False)
    if result.returncode==0:receipt['artifacts']={str(p.relative_to(ROOT)):{'bytes':p.stat().st_size,'sha256':sha(p)} for p in (OUT/'seance.3dsx',OUT/'seance.elf',OUT/'seance.smdh')}
    (OUT/'build.json').write_text(json.dumps(receipt,indent=2)+'\n');assert result.returncode==0 and receipt['source_inputs_unchanged']
    print(json.dumps(receipt['artifacts'],indent=2));print('clean ARM local only:',seconds,'seconds')
if __name__=='__main__':main()
