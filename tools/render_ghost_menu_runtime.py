#!/usr/bin/env python3
"""run the real c++ ghost runtime dump; validate ppm; make only nearest-2x/contact sheets."""
from pathlib import Path
import hashlib,json,subprocess
from PIL import Image,ImageDraw
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'ui_mockups/ghost_menu_runtime'; RAW=OUT/'native'
INKS={(31,20,31),(91,49,124),(126,100,119),(174,145,165)}
def paper(y):
    t=y/239; s=t*t*(3-2*t)
    return tuple(round(a+(b-a)*s) for a,b in zip((228,194,207),(211,187,206)))
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def check(im):
    assert im.size in ((400,240),(320,240))
    pix=im.load()
    for y in range(240):
        assert pix[0,y] == paper(y) and pix[1,y] == paper(y)
        allowed=INKS|{paper(y)}
        for x in range(im.width): assert pix[x,y] in allowed,(x,y,pix[x,y])
def main():
    OUT.mkdir(parents=True,exist_ok=True)
    if RAW.exists():
        for p in RAW.glob('*'): p.unlink()
    else: RAW.mkdir()
    for p in OUT.glob('*_dual_2x.png'): p.unlink()
    exe=Path('/tmp/seance_ghost_menu_test')
    subprocess.run(['make','test-ghost-menu'],cwd=ROOT,check=True)
    subprocess.run([str(exe),'--dump-ghost-menu',str(RAW)],cwd=ROOT,check=True)
    pairs=[]
    for top in sorted(RAW.glob('*_top.ppm')):
        name=top.name[:-8]; bot=RAW/(name+'_bottom.ppm')
        a,b=Image.open(top).convert('RGB'),Image.open(bot).convert('RGB');check(a);check(b)
        canvas=Image.new('RGB',(800,960),(31,20,31));canvas.paste(a.resize((800,480),Image.Resampling.NEAREST),(0,0));canvas.paste(b.resize((640,480),Image.Resampling.NEAREST),(80,480))
        canvas.save(OUT/(name+'_dual_2x.png'),optimize=False);pairs.append((name,a,b))
    cols=3; cellw,cellh=800,990; sheet=Image.new('RGB',(cols*cellw,((len(pairs)+cols-1)//cols)*cellh),(31,20,31));d=ImageDraw.Draw(sheet)
    for i,(name,a,b) in enumerate(pairs):
        x=(i%cols)*cellw;y=(i//cols)*cellh;sheet.paste(a.resize((800,480),Image.Resampling.NEAREST),(x,y));sheet.paste(b.resize((640,480),Image.Resampling.NEAREST),(x+80,y+480));d.text((x+12,y+964),name,fill=(228,194,207))
    sheet.save(OUT/'comparison_2x.png',optimize=False)
    manifest=(RAW/'manifest.tsv').read_text().splitlines()
    assert manifest and manifest[0]=='frame\tparam\ttop_objects\tbottom_objects\tshared_objects'
    rows=[line.split('\t') for line in manifest[1:]]
    assert len(rows)==len(pairs) and {r[0] for r in rows}=={n for n,_,_ in pairs}
    required={*(f'param_{i}' for i in range(8)),'tuning_note','tuning_free','fine','drift_rate_fast','drift_rate_slow',
              'tail_old','tail_long','freeze_transition','freeze_full','freeze_release','freeze_off','mod_signed','mod_env',
              'p_lock','lens_mod','lens_temp','extrema_low','extrema_high'}
    assert required <= {n for n,_,_ in pairs}
    object_counts={r[0]:{'param':int(r[1]),'top':int(r[2]),'bottom':int(r[3]),'shared':int(r[4])} for r in rows}
    assert all(v['top']>0 and v['bottom']>0 and v['shared']==v['top']+v['bottom'] and v['shared']<8192 for v in object_counts.values())
    sources=['platform/3ds/ui_module_focus_ghost.cpp','platform/3ds/ui_module_focus_internal.h','platform/3ds/ui_param_lens.cpp','platform/3ds/ui_top.cpp','platform/3ds/ui_editor.cpp','tools/sequencer_test.cpp']
    report={'frames':len(pairs),'pairs':[n for n,_,_ in pairs],'palette':sorted(INKS),'paper':{'top':[228,194,207],'bottom':[211,187,206],'smoothstep':True},'nearest_2x':True,'object_counts':object_counts,'peak_shared_objects':max(v['shared'] for v in object_counts.values()),'source_sha256':{p:sha(ROOT/p) for p in sources}}
    (OUT/'report.json').write_text(json.dumps(report,indent=2,sort_keys=True)+'\n')
    (OUT/'manifest.tsv').write_text('\n'.join(manifest)+'\n')
    print(f"ghost menu renders: {len(pairs)} pairs -> {OUT/'comparison_2x.png'}")
if __name__=='__main__':main()
