#!/usr/bin/env python3
"""real c++ strn menu-style captures; software citro2d lines, not hardware shots."""
from pathlib import Path
import argparse, csv, hashlib, json, subprocess
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
OUTSIDE = (48, 36, 48)
INKS = {(31,20,31), (91,49,124), (126,100,119), (174,145,165)}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def paper_row(y):
    den = 239 ** 3
    n = y * y * (717 - 2 * y)
    return tuple((a * den + (b - a) * n + den // 2) // den
                 for a, b in zip((228,194,207), (211,187,206)))


def check_runtime(im, width):
    assert im.size == (width, 240)
    for y in range(240):
        row = paper_row(y)
        for x in range(width):
            assert im.getpixel((x,y)) == row or im.getpixel((x,y)) in INKS
        assert all(im.getpixel((x,y)) == row for x in range(2))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--binary', default='/tmp/seance_strn_test')
    ap.add_argument('--output', default='ui_mockups/strn_menu_runtime')
    args = ap.parse_args()
    out = ROOT / args.output
    out.mkdir(parents=True, exist_ok=True)
    subprocess.run([args.binary, '--dump-strn', str(out)], check=True, cwd=ROOT)
    rows = list(csv.DictReader((out/'manifest.tsv').open(), delimiter='\t'))
    duals = {}
    for row in rows:
        name = row['frame']; screens = []
        for screen, width in [('top',400), ('bottom',320)]:
            im = Image.open(out/f'{name}_{screen}.ppm').convert('RGB')
            check_runtime(im, width)
            im.save(out/f'{name}_{screen}_native.png')
            big = im.resize((width*2,480), Image.Resampling.NEAREST)
            big.save(out/f'{name}_{screen}_2x.png')
            assert big.resize(im.size, Image.Resampling.NEAREST).tobytes() == im.tobytes()
            screens.append(im)
        dual = Image.new('RGB',(400,496),OUTSIDE)
        dual.paste(screens[0],(0,0)); dual.paste(screens[1],(40,256))
        dual.save(out/f'{name}_dual_native.png')
        dual.resize((800,992),Image.Resampling.NEAREST).save(out/f'{name}_dual_2x.png')
        duals[name] = dual
    chosen = ['param_0','param_1','param_3','pitch_mod','pitch_fine','more_long','frozen','frozen_hold']
    sheet = Image.new('RGB',(4*416,2*526),OUTSIDE); draw = ImageDraw.Draw(sheet)
    for i,name in enumerate(chosen):
        x=(i%4)*416; y=(i//4)*526
        draw.text((x+8,y+4),name,fill=(228,194,207)); sheet.paste(duals[name],(x,y+22))
    sheet.save(out/'comparison_native.png')
    sheet.resize((sheet.width*2,sheet.height*2),Image.Resampling.NEAREST).save(out/'comparison_2x.png')
    report = {
        'status': __doc__, 'frames': len(rows),
        'peak_shared': max(int(r['shared_objects']) for r in rows),
        'checks': {'exact_menu_smoothstep_paper': True, 'menu_inks_only': True,
                   'full_height_paper_strip': True, 'nearest_2x': True,
                   'actual_cpp_top_and_bottom': True},
        'source_sha256': {p: sha(ROOT/p) for p in [
            'platform/3ds/ui_module_focus_stranular.cpp','platform/3ds/strn_focus.h',
            'platform/3ds/menu_style.cpp','tools/strn_focus_test.h','tools/sequencer_test.cpp']}
    }
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    (out/'README.txt').write_text(__doc__+'\nrun: make test-strn && python3 tools/render_strn_menu_runtime.py\nall instrument geometry and inspector values are emitted by the current c++ controller from real deterministic strn engine telemetry.\nno python ui redraw, ftp, cia, subjective hardware acceptance, or audio-quality claim.\n')
    print(out/'comparison_2x.png'); print(json.dumps(report['checks']))


if __name__ == '__main__':
    main()
