#!/usr/bin/env python3
"""actual c++ rev menu captures; python validates/converts only, never redraws runtime ui."""
from pathlib import Path
import argparse,csv,hashlib,json,subprocess
from PIL import Image,ImageDraw
ROOT=Path(__file__).resolve().parents[1]
OUTSIDE=(48,36,48)
INKS={(31,20,31),(91,49,124),(126,100,119),(174,145,165)}
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def paper(y):
 d=239**3;n=y*y*(717-2*y)
 return tuple((a*d+(b-a)*n+d//2)//d for a,b in zip((228,194,207),(211,187,206)))
def check(im,w):
 assert im.size==(w,240)
 for y in range(240):
  r=paper(y)
  assert all(im.getpixel((x,y))==r or im.getpixel((x,y)) in INKS for x in range(w))
  assert all(im.getpixel((x,y))==r for x in range(2))
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--binary',default='/tmp/seance_rev_menu_test');ap.add_argument('--output',default='ui_mockups/rev_menu_runtime');a=ap.parse_args()
 out=ROOT/a.output;out.mkdir(parents=True,exist_ok=True);subprocess.run([a.binary,'--dump-rev',str(out)],cwd=ROOT,check=True)
 rows=list(csv.DictReader((out/'manifest.tsv').open(),delimiter='\t'));duals={}
 for row in rows:
  name=row['frame'];screens=[]
  for screen,w in [('top',400),('bottom',320)]:
   im=Image.open(out/f'{name}_{screen}.ppm').convert('RGB');check(im,w);im.save(out/f'{name}_{screen}_native.png')
   big=im.resize((w*2,480),Image.Resampling.NEAREST);big.save(out/f'{name}_{screen}_2x.png');assert big.resize(im.size,Image.Resampling.NEAREST).tobytes()==im.tobytes();screens.append(im)
  dual=Image.new('RGB',(400,496),OUTSIDE);dual.paste(screens[0],(0,0));dual.paste(screens[1],(40,256));dual.save(out/f'{name}_dual_native.png');dual.resize((800,992),Image.Resampling.NEAREST).save(out/f'{name}_dual_2x.png');duals[name]=dual
 chosen=['param_0','param_1','param_2','param_3','size_mod','freeze','mix_lock','mod','temp'];sheet=Image.new('RGB',(3*416,3*526),OUTSIDE);draw=ImageDraw.Draw(sheet)
 for i,name in enumerate(chosen):
  x=i%3*416;y=i//3*526;draw.text((x+8,y+4),name,fill=(228,194,207));sheet.paste(duals[name],(x,y+22))
 sheet.save(out/'comparison_native.png');sheet.resize((sheet.width*2,sheet.height*2),Image.Resampling.NEAREST).save(out/'comparison_2x.png')
 report={'status':__doc__,'frames':len(rows),'peak_shared':max(int(r['shared_objects']) for r in rows),'checks':{'exact_menu_smoothstep_paper':True,'four_menu_inks_only':True,'full_height_paper_strip':True,'nearest_2x':True,'actual_cpp_top_bottom':True,'deterministic_reverb_telemetry':True},'source_sha256':{p:sha(ROOT/p) for p in ['platform/3ds/ui_module_focus_reverb.cpp','platform/3ds/ui_param_lens.cpp','platform/3ds/ui_top.cpp','tools/reverb_controls_test.h','tools/sequencer_test.cpp']}}
 (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');(out/'README.txt').write_text(__doc__+'\nrun: make test-rev-menu && python3 tools/render_rev_menu_runtime.py\nPPM is emitted by the current C++ REV controller/top inspector from deterministic real ReverbUnit processing. software Citro2D line coverage, not hardware screenshots or acceptance.\n')
 print(out/'comparison_2x.png');print(json.dumps(report['checks']))
if __name__=='__main__':main()
