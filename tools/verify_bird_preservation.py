#!/usr/bin/env python3
"""Normalize only explicit BIRD integration additions; compare every old platform byte."""
from pathlib import Path
import hashlib,json,re,difflib
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'ui_mockups/bird_runtime'
BACKUP=ROOT/(OUT/'backup.txt').read_text().strip()

def erase_block(text,start,comment=False):
    at=text.index(start);brace=text.index('{',at);depth=1;end=brace+1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    if not start.startswith(' else if'):
        assert text[end]=='\n';end+=1
    return text[:at]+text[end:]

def normalize(path,text):
    text=text.replace('unit->kind == UnitKind::Bird || ','').replace('u->kind == UnitKind::Bird || ','')
    text=text.replace(', UnitKind::Bird','').replace(', BirdParam','')
    if path.endswith('ui_module_focus_internal.h'):
        a=text.index('bool bird_focus_touch_body(');b=text.index('bool shift_focus_touch_body(',a);text=text[:a]+text[b:]
    if path.endswith('module_inspector.cpp'):
        text=erase_block(text,'    if (kind == UnitKind::Bird) {\n')
        a=text.index('// BIRD shows resolved');b=text.index('// SHIFT reuses',a);text=text[:a]+text[b:]
        text=text.replace('    if (unit->kind == UnitKind::Bird && g_view != View::Chains &&\n        !(g_view == View::Sequencer && g_seq_lock_armed)) return;\n','')
    if path.endswith('ui_sequencer.cpp'):
        for _ in range(2):text=erase_block(text,'    if (kind == UnitKind::Bird) {\n')
        text=erase_block(text,' else if (kind == UnitKind::Bird) {')
    if path.endswith('ui_chains.cpp'):
        text=erase_block(text,' else if (source->kind == UnitKind::Bird) {')
    if path.endswith('ui_top.cpp'):
        text=erase_block(text,' else if (pitch_kind == UnitKind::Bird && sequencer_snapshot().sequence_pitch_slot[chain] >= 0) {')
    text=re.sub(r'^\s*else if \(unit->kind == UnitKind::Bird\)\n        bird_focus_drag\([^\n]*\n','',text,flags=re.M)
    text=re.sub(r'^        case UnitKind::Bird:\n            (?:return bird_focus_touch_body[^\n]*|bird_focus_draw[^\n]*)\n(?:            break;\n)?','',text,flags=re.M)
    text=text.replace('    case UnitKind::Bird:\n        return "bird";\n','')
    # Remaining one-line additions are explicit Bird dispatch or local no-op capture/release.
    text=re.sub(r'^.*(?:case UnitKind::Bird:|case Drag::BirdParam:|if\s*\([^\n]*UnitKind::Bird|bird_focus_(?:capture|release)\(\);).*(?:\n|$)','',text,flags=re.M)
    return text

def main():
    baseline=json.loads((OUT/'baseline.json').read_text());normalized=[];unchanged=[]
    for path,before in baseline.items():
        if not path.startswith('platform/'):continue
        original=(BACKUP/path).read_bytes();assert hashlib.sha256(original).hexdigest()==before
        now=(ROOT/path).read_bytes()
        if now==original:unchanged.append(path);continue
        old=original.decode();result=normalize(path,now.decode())
        if result!=old:
            print(''.join(difflib.unified_diff(old.splitlines(True),result.splitlines(True),fromfile=path+' baseline',tofile='normalized')))
        assert result==old,path
        normalized.append(path)
    ui=(ROOT/'platform/3ds/ui_module_focus_bird.cpp').read_text()
    assert 'sizeof(Frame)<=256' in ui and 'void bird_focus_draw(const B&)' in ui
    assert not re.search(r'\.process\(|->process\(|SampleBank|\.render\(',ui)
    data={'scope':'every pre-existing platform file; all bytes outside explicit BIRD additions exact',
        'backup':str(BACKUP.relative_to(ROOT)),'normalized':normalized,'unchanged':unchanged,
        'current_sha256':{p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in normalized+unchanged},
        'lower_frame_limit':256,'ui_DSP_PCM_calls':False}
    (OUT/'preservation.json').write_text(json.dumps(data,indent=2)+'\n')
    print(len(normalized),'additive platform files;',len(unchanged),'other platform files byte-exact')
if __name__=='__main__':main()
