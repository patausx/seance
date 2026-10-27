#!/usr/bin/env python3
"""New focused module-header proof: frozen-before/current-after actual composed host UI.

No historical proof runner/main/suite executes. Existing native raster + texture
boundary is reused as source only. Two compiler workers maximum. No ARM/network,
project/sample IO, deployment, old receipt replacement, or original-source edits.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import csv
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
sys.dont_write_bytecode = True
import test_fader_lancet_ui as common
import test_editor_fullscreen_ui as host
import test_mod_minimal_ui as extract

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'release/module-headings'
BASE = OUT / 'baseline'
CASES = ROOT / 'tests/module_headings_ui_cases.inc'
LOWER = {'bit','chorus','delay','fold','noise','ring','strain','tilt','vca','ghost','rev','strn','bird','shift','fract','filter'}
TOP = (LOWER - {'filter'}) | {'fm','motor'}
ALLOWED = {'platform/3ds/module_inspector.cpp': {'module_inspector_draw_top'},
           'platform/3ds/fm_context.cpp': {'header'}}
for name in ('bit','chorus','delay','fold','noise','ring','strain','tilt','vca','ghost','reverb','stranular','bird','shift','fract','filter'):
    ALLOWED['platform/3ds/ui_module_focus_'+name+'.cpp'] = {name+'_focus_draw',name+'_focus_draw_top'}
ALLOWED['platform/3ds/ui_module_focus_ghost.cpp'] = {'draw_bottom_frame','draw_top_frame'}
ALLOWED['platform/3ds/ui_module_focus_stranular.cpp'] = {'stranular_focus_draw_bottom','stranular_focus_draw_top'}
NEW_PRODUCTION = {'platform/3ds/module_heading.h','platform/3ds/module_heading_data.h'}

def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def sha_text(t): return hashlib.sha256(t.encode()).hexdigest()
def json_write(p, obj): p.write_text(json.dumps(obj, indent=2, sort_keys=True)+'\n')
def stamp(): return datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')
def pins(root):
    return {str(p.relative_to(root)):sha(p) for folder in ('core','platform/3ds') for p in sorted((root/folder).rglob('*')) if p.is_file()}

def supplement():
    original=json.loads((BASE/'sources.json').read_text())
    for n,h in original.items(): assert sha(BASE/n)==h, ('frozen original changed',n)
    paths = list((ROOT/'tests').glob('*.h')) + list((ROOT/'tests').glob('*.inc'))
    paths += [ROOT/'tests/osc_input_dispatch_test.cpp']
    paths += [p for d in ('sequencer_test_stubs','menu_stage1_test_stubs','paper_motion_test_stubs') for p in (ROOT/'tests'/d).rglob('*') if p.is_file()]
    result={}
    for p in paths:
        if p==CASES: continue
        n=p.relative_to(ROOT); dest=BASE/n;dest.parent.mkdir(parents=True,exist_ok=True)
        if dest.exists(): assert sha(dest)==sha(p), ('supplement drift',n)
        else:
            with dest.open('xb') as f: f.write(p.read_bytes())
        result[str(n)]=sha(dest)
    json_write(OUT/'baseline-supplement.json', {'original_manifest_sha256':sha(BASE/'sources.json'),'additional_host_boundary_inputs':result})
    return result

def driver(root, directory):
    # The imported builders only emit a PRIVATE driver. Their checks/main are not called.
    common.ROOT=host.ROOT=extract.ROOT=root
    path=host.driver(directory)
    text=path.read_text(); entry='int main(int argc,char**argv){'
    assert text.count(entry)==1
    text=text.replace(entry,'int unused_module_headings_fullscreen_main(int argc,char**argv){')+'\n'+CASES.read_text()
    path.write_text(text)
    return path

def run(cmd, cwd, log, timeout=600):
    env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    with log.open('x') as f:
        f.write('$ '+shlex.join(map(str,cmd))+'\n');f.flush()
        r=subprocess.run(cmd,cwd=cwd,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=timeout)
    return r.returncode

def build(root, tag, sanitized=False, capture=True):
    folder=OUT/(tag+'-'+stamp());folder.mkdir(parents=True)
    (folder/'runner-source.py').write_bytes(Path(__file__).read_bytes())
    (folder/'cases-source.inc').write_bytes(CASES.read_bytes())
    before=pins(root); support=supplement(); source=driver(root,folder)
    common.ROOT=root
    units=common.make_variable('OSC_INPUT_SOURCES');units[units.index('tests/osc_input_dispatch_test.cpp')]=str(source)
    flags=['-O1' if sanitized else '-O2','-g1','-I'+str(folder),*common.make_variable('SEQUENCER_TEST_FLAGS'),'-DSEANCE_REAL_EDITOR','-Itests',*common.WRAP]
    if sanitized: flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
    compiler=os.environ.get('CXX','g++');objects=[];commands=[]
    for i,name in enumerate(units):
        obj=folder/('%02d_%s.o'%(i,Path(name).stem));dep=obj.with_suffix('.d');objects.append(obj)
        command=[compiler,*flags,*(['-D__3DS__'] if name=='platform/3ds/menu_style.cpp' else []),'-MMD','-MF',str(dep),'-c',name,'-o',str(obj)]
        commands.append(command)
    proof={'root':str(root),'tag':tag,'source_sha256':before,'support_sha256':support,'case_sha256':sha(CASES),'runner_sha256':sha(__file__),
           'build_commands':commands,'workers':2,'host_only':True,'inherited_suites_run':0,'hardware':False,'deployment':False,
           'limitations':['native software raster and texture emulation, not device/GPU/FPS','synthetic process-memory sample; no physical MIC',
              'host glyph recording allocates: no general draw-heap claim; texture init/upload and pure-source guards checked',
              'scoped composed state/visual proof, not full functional regression suite'], 'status':'building'}
    receipt=folder/'receipt.json';json_write(receipt,proof)
    def compile_one(item):
        i,command=item;log=folder/('compile-%02d.log'%i);code=run(command,root,log)
        print(tag,'compile',i,code,flush=True);return {'source':units[i],'exit':code,'log':str(log)}
    with ThreadPoolExecutor(max_workers=2) as pool: results=list(pool.map(compile_one,enumerate(commands)))
    proof['compiles']=results
    if any(r['exit'] for r in results):
        proof['status']='compile failed';json_write(receipt,proof);raise RuntimeError(str(receipt))
    binary=folder/'module_headings_ui';link=[compiler,*flags,*map(str,objects),'-o',str(binary)]
    code=run(link,root,folder/'link.log');proof['link']={'command':link,'exit':code}
    if code:
        proof['status']='link failed';json_write(receipt,proof);raise RuntimeError(str(receipt))
    assert before==pins(root),'production changed during build'
    deps={}
    for obj in objects:
        content=obj.with_suffix('.d').read_text().replace('\\\n',' ')
        for name in shlex.split(content.split(':',1)[1]):
            p=(root/name).resolve();deps[str(p)]=sha(p)
    proof.update(binary=str(binary),binary_sha256=sha(binary),dependencies=deps,status='built')
    json_write(receipt,proof)
    captures=folder/'capture';log=folder/'run.log';code=run([str(binary),'--capture' if capture else '--proof',str(captures)],root,log)
    proof['run']={'exit':code,'log':str(log),'sanitizers':'ASAN/UBSAN/leaks' if sanitized else None}
    proof['status']='passed' if code==0 else 'run failed';json_write(receipt,proof)
    print(log.read_text()[-6000:],flush=True)
    if code: raise RuntimeError(str(receipt))
    rows=list(csv.DictReader((captures/'surfaces.tsv').open(),delimiter='\t'))
    proof['surfaces']=rows;proof['native_duals']=len(rows)//2;proof['capture']=str(captures)
    if capture: proof['media']=package(captures)
    for n,h in deps.items(): assert sha(n)==h, ('dependency changed during run',n)
    assert before==pins(root),'production changed during run'
    json_write(receipt,proof);json_write(OUT/(tag+'-latest.json'),{'receipt':str(receipt),'sha256':sha(receipt)})
    return receipt,proof

def package(folder):
    from PIL import Image,ImageDraw
    pairs=[]
    for top in sorted(folder.glob('*_top.ppm')):
        name=top.name[:-8]
        with Image.open(top) as a, Image.open(folder/(name+'_bottom.ppm')) as b:
            pair=Image.new('RGB',(400,496),(18,18,21));pair.paste(a,(0,0));pair.paste(b,(40,256))
        pair.save(folder/(name+'.png'))
        if name.endswith('_default'): pairs.append((name,pair))
    for theme in ('graphite','pink'):
        selected=[p for p in pairs if p[0].startswith(theme)]
        for start in range(0,len(selected),8):
            board=Image.new('RGB',(1600,1040),(18,18,21));draw=ImageDraw.Draw(board)
            for i,(name,pair) in enumerate(selected[start:start+8]):
                x=(i%4)*400;y=(i//4)*520;board.paste(pair,(x,y+20));draw.text((x+8,y+3),name,fill='white')
            board.save(folder/(theme+'-defaults-'+str(start//8)+'.png'))
    return {p.name:sha(p) for p in sorted(folder.iterdir()) if p.is_file()}

def preservation():
    original=json.loads((BASE/'sources.json').read_text());changed=[];kept={};draw=[];outer={}
    for name,digest in original.items():
        assert sha(BASE/name)==digest,('baseline drift',name)
        now=ROOT/name;assert now.exists(),('removed',name)
        if sha(now)==digest: kept[name]=digest;continue
        assert name in ALLOWED,('unexpected production change',name)
        changed.append(name);old=(BASE/name).read_text();new=now.read_text();a=common.functions(old);b=common.functions(new)
        assert a.keys()==b.keys(),('function set changed',name)
        for fn,(_,_,body) in a.items():
            if body==b[fn][2]:kept[name+':'+fn]=sha_text(body);continue
            assert fn in ALLOWED[name],('non-draw function changed',name,fn)
            assert not re.search(r'\b(?:g_eng|g_audio|osGetTime|malloc|calloc|realloc|new|delete|C3D_TexInit)\b',common.masked(b[fn][2])),('nonpure draw',name,fn)
            draw.append(name+':'+fn)
        rest=[]
        for source,defs in ((old,a),(new,b)):
            for start,end,_ in sorted(defs.values(),reverse=True):source=source[:start]+source[end:]
            source=re.sub(r'^\s*#include\s+"module_heading.h"\s*$', '', source,flags=re.M)
            rest.append(common.tokens(source))
        assert rest[0]==rest[1],('non-draw declarations changed',name)
        outer[name]=sha_text(' '.join(rest[0]))
    existing=set(original);extra=set(pins(ROOT))-existing
    assert extra<=NEW_PRODUCTION,('new unexpected production',extra)
    return {'changed_files':changed,'changed_draw_functions':draw,'byteexact_files_and_functions':kept,'outside_function_tokens':outer,
            'core_byteexact_count':sum(n.startswith('core/') for n in original),'new_production':{n:sha(ROOT/n) for n in extra}}

def limits(row):
    if row['surface']=='bottom':return 28 if row['module'] in LOWER else 0
    if row['module']=='fm':return 26
    return 41 if row['module'] in TOP or (row['module']=='osc' and row['variant'].startswith('legacy')) else 0

def compare(before,after):
    from PIL import Image,ImageChops
    old={r['id']+'_'+r['surface']:r for r in before['surfaces']};new={r['id']+'_'+r['surface']:r for r in after['surfaces']}
    assert old.keys()==new.keys(),'case inventory differs'
    result=[];failures=[]
    for name,row in new.items():
        with Image.open(Path(before['capture'])/(name+'.ppm')) as a,Image.open(Path(after['capture'])/(name+'.ppm')) as b:
            assert a.size==b.size
            difference=ImageChops.difference(a,b);area=difference.getbbox();cut=limits(row)
            outside=ImageChops.difference(a.crop((0,cut,a.width,240)),b.crop((0,cut,b.width,240))).getbbox()
            item={'surface':name,'allowed_y_lt':cut,'changed_bbox':area,'outside_changed_bbox':outside}
            if outside:failures.append(item)
            result.append(item)
    return {'status':'passed' if not failures else 'failed','surfaces':len(result),'unchanged_surfaces':sum(r['changed_bbox'] is None for r in result),
            'changed_surfaces':sum(r['changed_bbox'] is not None for r in result),'failures':failures,'details':result}

def mask_validation(current):
    """Independently decode packed bits; verify native raster shape, not text submissions."""
    from PIL import Image, ImageDraw, ImageFont
    text=(ROOT/'platform/3ds/module_heading_data.h').read_text()
    raw=bytes(int(h,16) for h in re.findall(r'0x([0-9a-fA-F]{2})',text.split('bitmap[] = {',1)[1].split('};',1)[0]))
    rows=re.findall(r'\{UnitKind::(\w+), "([^"]+)", (\d+), (\d+), (\d+)\}',text)
    catalogue=dict(re.findall(r'^PICKER_ENTRY\((\w+), "([^"]+)"',(ROOT/'platform/3ds/picker_catalogue.inc').read_text(),re.M))
    assert len(rows)==len(catalogue)==28 and {r[0] for r in rows}==set(catalogue)
    common_data=(BASE/'platform/3ds/menu_labels_data.h').read_text()
    oldraw=bytes(int(h,16) for h in re.findall(r'0x([0-9a-fA-F]{2})',common_data.split('bitmap[] = {',1)[1].split('};',1)[0]))
    oldlabels={n:(int(o),int(w),int(h)) for n,o,w,h in re.findall(r'\{"([^"]+)", (\d+), (\d+), (\d+)\}',common_data)}
    def decode(data,o,w,h):
        stride=(w+7)//8;assert 0<=o and o+stride*h<=len(data)
        return bytes(255 if data[o+y*stride+x//8]&(128>>(x%8)) else 0 for y in range(h) for x in range(w))
    masks={};exact_shared=[];new_font=[]
    face=ImageFont.truetype(str(ROOT/'assets/fonts/Gossip-MedPixel.otf'),28)
    for kind,name,offset,w,h in rows:
        offset,w,h=map(int,(offset,w,h));assert name==('filter' if kind=='Filter' else catalogue[kind])
        assert 0<w<=57 and 0<h<=25
        bits=decode(raw,offset,w,h);masks[name]=(w,h,bits)
        if name in oldlabels:
            o,ow,oh=oldlabels[name];assert (w,h,bits)==(ow,oh,decode(oldraw,o,ow,oh)),('shared mask differs',kind)
            exact_shared.append(kind)
        else:
            # Independently rasterize the unmodified face at another origin, then threshold/crop.
            image=Image.new('L',(192,96));ImageDraw.Draw(image).text((31,17),name,font=face,fill=255)
            image=image.point(lambda v:255 if v>=128 else 0);image=image.crop(image.getbbox())
            assert image.size==(w,h) and image.tobytes()==bits,('new mask not Gossip28',kind)
            new_font.append(kind)
    alias={'delay':'dly','chorus':'chor','filter':'filter'}
    proofs=[];colors={}
    for r in current['surfaces']:
        if r['variant'] not in {'default','bypass','long6','holes','lock','legacy','legacy-long6'}:continue
        module=r['module'];top=r['surface']=='top'
        if top:
            if module not in TOP and not(module=='osc' and r['variant'].startswith('legacy')):continue
            x,y=(24,8) if module=='fm' else (16,10) if module in {'bird','shift'} else (14,10) if module=='vca' else (14,3) if module=='fract' else (14,2)
        else:
            if module not in LOWER:continue
            x,y=(12,2) if module in {'ghost','rev','strn','filter'} else (14,2)
        label=alias.get(module,module);w,h,bits=masks[label]
        path=Path(current['capture'])/(r['id']+'_'+r['surface']+'.ppm')
        with Image.open(path) as im:
            assert x+w<=im.width and y+h<=limits(r)
            crop=list(im.crop((x,y,x+w,y+h)).getdata())
            ink={c for c,b in zip(crop,bits) if b};assert len(ink)==1,('title overwritten/clipped',str(path),len(ink))
            color=next(iter(ink));actual=bytes(255 if c==color else 0 for c in crop)
            assert actual==bits,('native title shape differs',str(path))
            theme=r['id'].split('_')[0]
            if theme in colors:assert colors[theme]==color,('title foreground drift',r['id'])
            colors[theme]=color
        proofs.append({'surface':r['id']+'_'+r['surface'],'kind':r['kind'],'label':label,'box':[x,y,w,h],'ink_pixels':sum(bool(v) for v in bits)})
    assert proofs
    return {'coverage':28,'exact_unchanged_shared_masks':exact_shared,'new_original_face_masks':new_font,'native_shape_checks':proofs,'spot_rgb':colors,
            'font_sha256':sha(ROOT/'assets/fonts/Gossip-MedPixel.otf'),'data_sha256':sha(ROOT/'platform/3ds/module_heading_data.h')}

def breadcrumbs(before,after,colors):
    """Native context remains real slot order; selected underline/bypass line stay visible."""
    from PIL import Image
    tested=[]
    owners={'bit','chorus','delay','fold','noise','ring','strain','tilt','motor','fm','ghost','rev','strn'}
    for r in after['surfaces']:
        if r['surface']!='top' or r['variant'] not in {'long6','holes'} or r['module'] not in owners:continue
        name=r['id']+'_top';module=r['module'];oldy=38 if module=='fm' else 28 if module=='rev' else 10;newy=38 if module=='fm' else 28 if module=='rev' else 29
        records=[]
        for proof,y in ((before,oldy),(after,newy)):
            p=Path(proof['capture'])/(name+'_text.tsv');entries=list(csv.DictReader(p.open(),delimiter='\t'))
            # TSV gives ink bounding boxes, not the ui::text baseline: e.g. 'rev' begins y+2.
            records.append([e for e in entries if y<=int(e['y']) and int(e['y'])+int(e['h'])<=y+8 and not e['text'].startswith(('ch01','chain '))])
        assert [e['text'] for e in records[0]]==[e['text'] for e in records[1]],('breadcrumb content/order changed',name)
        nodes=[e for e in records[1] if e['text'] not in {'/','out'}];assert len(nodes)==(6 if r['variant']=='long6' else 4),name
        ink=tuple(colors[r['id'].split('_')[0]]);selected=nodes[-1];x=int(selected['x']);end=x+6*len(selected['text'])-2
        with Image.open(Path(after['capture'])/(name+'.ppm')) as im:
            underline=50 if module=='fm' else 39
            assert all(im.getpixel((px,underline))==ink for px in range(x,end)),('selected underline missing',name)
            # Compare the actual crossed line colors against frozen baseline, including nonselected bypass.
            slots=[0,1,2,3,4,5] if r['variant']=='long6' else [0,2,4,5]
            oldnodes=[e for e in records[0] if e['text'] not in {'/','out'}]
            oldcross=48 if module=='fm' else 32 if module=='rev' else 14
            crossed=48 if module=='fm' else 32 if module=='rev' else 33
            with Image.open(Path(before['capture'])/(name+'.ppm')) as oldim:
                for slot,node,oldnode in zip(slots,nodes,oldnodes):
                    if slot not in ({2,4,5} if r['variant']=='holes' else {2,4}):continue
                    nx,ox=int(node['x']),int(oldnode['x']);extent=6*len(node['text'])-2
                    previous=[oldim.getpixel((ox+j,oldcross)) for j in range(extent)]
                    present=[im.getpixel((nx+j,crossed)) for j in range(extent)]
                    assert len(set(previous))==1 and present==previous,('bypass crossing changed',name,slot)
        tested.append(name)
    return {'states':len(tested),'surfaces':tested}

def review(before,after):
    from PIL import Image,ImageDraw
    folder=OUT/('review-'+stamp());folder.mkdir()
    selected=[]
    for row in after['surfaces']:
        if row['surface']!='top' or row['variant']!='default':continue
        name=row['id'];board=Image.new('RGB',(800,520),(18,18,21));draw=ImageDraw.Draw(board)
        for i,(label,proof) in enumerate((('before frozen',before),('after current',after))):
            with Image.open(Path(proof['capture'])/(name+'.png')) as im:board.paste(im,(400*i,24))
            draw.text((400*i+8,5),name+' / '+label,fill='white')
        board.resize((1600,1040),Image.Resampling.NEAREST).save(folder/(name+'-before-after-2x.png'))
        selected.append((name,board))
    for theme in ('graphite','pink'):
        pairs=[p for p in selected if p[0].startswith(theme)]
        for start in range(0,len(pairs),6):
            contact=Image.new('RGB',(2400,1040),(18,18,21))
            for i,(_,pair) in enumerate(pairs[start:start+6]):contact.paste(pair,((i%3)*800,(i//3)*520))
            contact.save(folder/(theme+'-before-after-'+str(start//6)+'.png'))
    return {'path':str(folder),'sha256':{p.name:sha(p) for p in sorted(folder.iterdir())}}

def latest(tag):
    p=json.loads((OUT/(tag+'-latest.json')).read_text());assert sha(p['receipt'])==p['sha256'];return json.loads(Path(p['receipt']).read_text())

def verify_built(proof):
    assert proof['status']=='passed' and proof['run']['exit']==0
    assert sha(proof['binary'])==proof['binary_sha256']
    assert pins(Path(proof['root']))==proof['source_sha256'],('build root drift',proof['root'])
    assert proof['case_sha256']==sha(CASES),'case inputs changed since capture'
    for name,digest in proof['dependencies'].items():assert sha(name)==digest,('dependency drift',name)
    for name,digest in proof.get('media',{}).items():assert sha(Path(proof['capture'])/name)==digest,('media drift',name)
    defaults=[r for r in proof['surfaces'] if r['surface']=='top' and r['variant']=='default']
    assert len(defaults)==56
    for theme in ('graphite','pink'):
        assert {int(r['kind']) for r in defaults if r['id'].startswith(theme+'_')}==set(range(1,29))

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--baseline',action='store_true');p.add_argument('--after',action='store_true');p.add_argument('--compare-only',action='store_true');args=p.parse_args()
    assert sum((args.baseline,args.after,args.compare_only))==1
    OUT.mkdir(exist_ok=True,parents=True)
    if args.baseline:build(BASE,'baseline-optimized');return
    guards=preservation();a=latest('baseline-optimized')
    if args.after:
        _,b=build(ROOT,'current-optimized');_,c=build(ROOT,'current-sanitized',True,False)
    else:b=latest('current-optimized');c=latest('current-sanitized')
    dest=OUT/('verification-'+stamp()+'.json')
    proof={'status':'pending','runner_sha256':sha(__file__),'case_sha256':sha(CASES),'preservation':guards,
           'build_receipts':{tag:json.loads((OUT/(tag+'-latest.json')).read_text()) for tag in ('baseline-optimized','current-optimized','current-sanitized')},
           'initial_failure':{'path':'release/module-headings/initial-breadcrumb-oracle-failure.json','sha256':sha(OUT/'initial-breadcrumb-oracle-failure.json')},
           'host_only':True,'hardware':False,'deployment':False}
    json_write(dest,proof)
    try:
        for built in (a,b,c):verify_built(built)
        assert a['support_sha256']==b['support_sha256']==c['support_sha256'],'host boundary changed'
        assert b['source_sha256']==c['source_sha256'],'optimized/sanitized source drift'
        proof['pixels']=compare(a,b);json_write(dest,proof)
        proof['typography']=mask_validation(b);json_write(dest,proof)
        proof['breadcrumbs']=breadcrumbs(a,b,proof['typography']['spot_rgb']);json_write(dest,proof)
        hashes=lambda built:{(r['id'],r['surface']):r['raster_fnv64'] for r in built['surfaces']}
        proof['sanitizer_matches_optimized_rasters']=hashes(b)==hashes(c)
        proof['all28_default_pairs_both_themes']=True;proof['native_duals_per_run']=b['native_duals']
        proof['review']=review(a,b)
        assert proof['pixels']['status']=='passed','outside-header pixels changed'
        assert proof['sanitizer_matches_optimized_rasters'],'optimized/sanitized raster mismatch'
        proof['status']='passed'
    except Exception as e:
        import traceback
        proof['status']='failed';proof['failure']=str(e);proof['traceback']=traceback.format_exc();json_write(dest,proof);raise
    json_write(dest,proof)
    json_write(OUT/'verification-latest.json',{'receipt':str(dest),'sha256':sha(dest)});print(dest)

if __name__=='__main__':main()
