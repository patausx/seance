#!/usr/bin/env python3
"""One clean local MIC relief ARM build, exact focused host proofs required. No deploy."""
from datetime import datetime, timezone
from pathlib import Path
import hashlib, json, os, subprocess, time
import test_mic_relief_ui as ui
import test_mic_relief_core as core
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'release/mic-relief'
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def main():
    for module,name in ((ui,'ui-proof.json'),(core,'core-proof.json')):
        proof=json.loads((OUT/name).read_text())
        assert proof['checks']['status']=='passed'
        module.verify(proof)
    names=set(json.loads((ROOT/'release/rec-dictaphone/build.json').read_text())['source_hashes'])
    names.update(('platform/3ds/mic_relief.h','platform/3ds/mic_ui.h','platform/3ds/mic_type_data.h'))
    before={p:sha(ROOT/p) for p in sorted(names)}
    stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ');build='build-mic-relief-'+stamp
    assert not (ROOT/build).exists()
    assert not (OUT/'seance.3dsx').exists(),'never silently overwrite/rebuild a ready release'
    cmd=['make','-j2','BUILD='+build,'TARGET=release/mic-relief/seance']
    env=dict(os.environ,DEVKITPRO='/opt/devkitpro',DEVKITARM='/opt/devkitpro/devkitARM')
    env['PATH']='/opt/devkitpro/tools/bin:/opt/devkitpro/devkitARM/bin:'+env['PATH']
    start=time.monotonic();log=OUT/('build-'+stamp+'.log')
    with log.open('x') as f:result=subprocess.run(cmd,cwd=ROOT,env=env,stdout=f,stderr=subprocess.STDOUT)
    print(log.read_text()[-6000:]);seconds=round(time.monotonic()-start,2)
    receipt=dict(finished_at=datetime.now(timezone.utc).isoformat(),command=cmd,build_directory=build,clean_build=True,
        exit=result.returncode,seconds=seconds,source_hashes=before,source_inputs_unchanged=all(sha(ROOT/p)==h for p,h in before.items()),
        proofs={name:sha(OUT/name) for name in ('ui-proof.json','core-proof.json')},
        log=str(log.relative_to(ROOT)),deployed=False,hardware_tested=False)
    if result.returncode==0:
        receipt['artifacts']={str(p.relative_to(ROOT)):{'bytes':p.stat().st_size,'sha256':sha(p)} for p in (OUT/'seance.3dsx',OUT/'seance.elf',OUT/'seance.smdh')}
    (OUT/'build.json').write_text(json.dumps(receipt,indent=2)+'\n')
    assert result.returncode==0 and receipt['source_inputs_unchanged']
    print(json.dumps(receipt['artifacts'],indent=2));print('clean ARM local only:',seconds,'seconds')
if __name__=='__main__':main()
