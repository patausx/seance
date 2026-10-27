#!/usr/bin/env python3
"""Narrow sequencer study after rejection of the labelled ribbon redesign.

Capture the CURRENT sequencer and an isolated C++ drawing-only variant, using
verified current cached host objects. No production file is edited. Preserve
all existing layout, upper information, scope, typography, controls and data.
Only show centered 2x2 square retrigger pips and unfilled edit corner marks.
The variant is a visual proposal, not an implemented controller or device build.
"""
from datetime import datetime, timezone
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from PIL import Image, ImageDraw, ImageChops, PngImagePlugin
from render_module_geometry_study import Canvas, protected_hashes

ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT/'release/lancet-everywhere/nonmodule/optimized-20261005T103812_540148Z'
OUTSIDE = (18, 18, 21)
INK, MUTED, ACCENT = (244, 244, 244), (184, 184, 184), (181, 154, 232)

DRIVER = r'''
namespace score_mark_study {
void capture(const std::filesystem::path& out, const std::string& name) {
 const auto before=pattern_hash(g_eng->pattern);
 for(bool top:{true,false}) {
  draw(top);
  CHECK(!outside_rects&&!outside_lines&&!text_overlaps);
  CHECK(pattern_hash(g_eng->pattern)==before);
  dump_surface(out,name+(top?"_top":"_bottom"));
 }
}
int run(const std::filesystem::path& out) {
 CHECK(!std::filesystem::exists(out));std::filesystem::create_directories(out);
 for(auto theme:{palette::Theme::Graphite,palette::Theme::Pink}) {
  palette::set(theme);host_time_ms=10000;lancet_nonmodule::begin_page();
  Fixture f;
  delete f.engine->replace_unit(1,1,nullptr); // standalone OSC is flow, no VCA
  for(int c=0;c<4;++c) {f.engine->clear_track(c);f.engine->set_sequence_length(c,16);}
  const int first[]={0,3,4,7,8,11,12,15};
  for(int i=0;i<8;++i) {auto&e=f.engine->pattern.steps[0][first[i]];e.flags=SeqStep::Trig;e.set_retrig_count(i%4+1);}
  for(int i=0;i<4;++i) {auto&e=f.engine->pattern.steps[2][1+i*4];e.flags=SeqStep::Trig;e.set_retrig_count(i+1);}
  const int value[]={32,32,48,32,72,72,72,32,24,36,54,80,32,32,56,32};
  for(int c:{1,3}) {
   auto&gain=f.engine->chains[c].units[0]->params[OscUnit::P_GAIN];
   gain.base=q15((c==1?32:44)*32767/100);gain.snap();
   for(int s=0;s<16;++s) {
    const int v=c==1?value[s]:(s<4?44:s<7?68:s<12?18:44);
    if(v!=(c==1?32:44))CHECK(f.engine->set_step_lock(c,s,0,OscUnit::P_GAIN,q15(v*32767/100)));
   }
  }
  // Real private-engine output supplies the already-existing scope, not a fake
  // waveform. Nothing is exported, played through a device or saved as a patch.
  std::array<q15,1024> pcm{};for(int i=0;i<8;++i)f.engine->render(pcm.data(),512);
  f.refresh();f.mode(1);f.touch(1,4);f.mode(0);host_time_ms+=300;f.frame();
  const std::string prefix=theme==palette::Theme::Graphite?"graphite_":"pink_";
  capture(out,prefix+"flow");
  f.mode(3);f.touch(0,7);host_time_ms+=300;f.frame();capture(out,prefix+"dice");
  f.mode(2);f.touch(1,4);host_time_ms+=300;f.frame();capture(out,prefix+"level");
 }
 return 0;
}
}
int main(int argc,char**argv) {
 try {CHECK(argc==2);return score_mark_study::run(argv[1]);}
 catch(const std::exception&e){std::fprintf(stderr,"study failed: %s\n",e.what());return 1;}
}
'''

SQUARE_SELECTOR = r'''void draw_retrig_dots(int cx, int cy, int count, u32 color) {
    static const int8_t xy[4][8] = {
        {0, 0}, {-4, -4, 4, 4}, {-4, -4, 0, 0, 4, 4}, {-4, -4, 4, -4, -4, 4, 4, 4}};
    for (int i = 0; i < count; ++i)
        ui::rect(cx - 1 + xy[count - 1][i * 2], cy - 1 + xy[count - 1][i * 2 + 1], 2, 2, color);
}
'''
SQUARE_CELL = r'''    case SeqMode::Retrig: {
        const int count = meta_retrig(meta);
        // Even 18px cell => center 8.5px, so even 2px pips center EXACTLY.
        static const int8_t xy[4][8] = {
            {0, 0}, {-4, -4, 4, 4}, {-4, -4, 0, 0, 4, 4}, {-4, -4, 4, -4, -4, 4, 4, 4}};
        for (int i = 0; i < count; ++i)
            ui::rect(x + (CELL_W-2)/2 + xy[count-1][i*2],
                     y + (BADGE_H-2)/2 + xy[count-1][i*2+1], 2, 2, color);
        break;
    }
'''
CORNERS = r'''    // ISOLATED STUDY ONLY: static unfilled focus, not an approved animation.
    if (g_overlay == Overlay::None && g_view == View::Sequencer) {
        const int x = pattern_x(g_seq_col % PAGE_STEPS), y = PATTERN_ROWS[g_seq_chain];
        const bool flow = flow_chain(g_seq_chain) && g_seq_mode == SeqMode::Trig;
        const int cy = y + 26 - value_percent(g_seq_chain, g_seq_col) * 23 / 100;
        const int left = flow ? x + 2 : x, right = flow ? x + 14 : x + CELL_W - 1;
        const int top = flow ? cy - 6 : y, bottom = flow ? cy + 6 : y + BADGE_H - 1;
        for (int side : {0,1}) for (int end : {0,1}) {
            const int xx=side?right:left, yy=end?bottom:top;
            ui::line(xx,yy,xx+(side?-3:3),yy,seq_spot());
            ui::line(xx,yy,xx,yy+(end?-3:3),seq_spot());
        }
    }
    g_step_focus.reset();
'''


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def run(cmd, log):
    result = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, timeout=180)
    log.write_text('$ '+' '.join(map(str, cmd))+'\n'+result.stdout+result.stderr)
    if result.returncode:
        raise RuntimeError(f'{log}\n{result.stdout[-1000:]}\n{result.stderr[-5000:]}')


def variant_source():
    source = (ROOT/'platform/3ds/ui_sequencer.cpp').read_text()
    start = source.index('void draw_retrig_dots(')
    stop = source.index('// Center actual native ink', start)
    source = source[:start]+SQUARE_SELECTOR+source[stop:]
    start = source.index('    case SeqMode::Retrig: {', source.index('void draw_event_content('))
    stop = source.index('    case SeqMode::When:', start)
    source = source[:start]+SQUARE_CELL+source[stop:]
    start = source.index('    // One moving fill, then inverse-only foreground')
    stop = source.index('    for(int local=0;local<PAGE_STEPS;++local)', start)
    source = source[:start]+CORNERS+source[stop:]
    old = 'score_ink::point(x, yy, selected ? 3 : 1, selected ? seq_spot() : seq_ghost());'
    assert source.count(old) == 1
    source = source.replace(old, 'score_ink::point(x, yy, 1, selected ? seq_spot() : seq_ghost());')
    return source.replace('#include "../../', '#include "'+str(ROOT)+'/')


def native_capture(out):
    receipt = json.loads((CACHE/'receipt.json').read_text())
    for name, expected in receipt['dependencies'].items():
        assert Path(name).is_file() and sha(name) == expected, ('stale cached source', name)
    # Reuse current native production objects; never execute archived test mains.
    folder = out/'native'
    folder.mkdir()
    source = (CACHE/'driver.cpp').read_text()
    marker = 'int main(int argc,char**argv)'
    assert source.count(marker) == 1
    source = source.replace(marker, 'int archived_nonmodule_main(int argc,char**argv)')+'\n'+DRIVER
    driver = folder/'driver.cpp'
    driver.write_text(source)
    drawing = folder/'study_sequencer.cpp'
    drawing.write_text(variant_source())
    flags = receipt['commands'][0][1:receipt['commands'][0].index('-MMD')]
    flags += ['-I'+str(ROOT/'platform/3ds')]
    compiler = receipt['commands'][0][0]
    driver_o, variant_o = folder/'driver.o', folder/'study_sequencer.o'
    run([compiler, *flags, '-c', str(driver), '-o', str(driver_o)], folder/'driver-build.log')
    run([compiler, *flags, '-c', str(drawing), '-o', str(variant_o)], folder/'drawing-build.log')
    objects = [Path(command[command.index('-o')+1]) for command in receipt['commands']]
    assert objects[0].name == '00_driver.o' and objects[1].name == '01_ui_sequencer.o'
    object_pins = {str(p): sha(p) for p in objects[1:]}
    for version, seq in (('current', objects[1]), ('proposal', variant_o)):
        binary = folder/version
        run([compiler, *flags, str(driver_o), str(seq), *map(str, objects[2:]), '-o', str(binary)], folder/(version+'-link.log'))
        run([str(binary), str(out/version)], folder/(version+'-capture.log'))
    assert object_pins == {str(p): sha(p) for p in objects[1:]}
    return dict(cache_receipt_sha256=sha(CACHE/'receipt.json'), dependencies=receipt['dependencies'],
                cached_objects=object_pins, driver_sha256=sha(driver), proposal_source_sha256=sha(drawing))


def load(out, version, theme, case, screen):
    with Image.open(out/version/f'{theme}_{case}_{screen}.ppm') as image:
        return image.convert('RGB')


def save(out, name, image, scale=2):
    path = out/name
    assert not path.exists()
    scaled = image.resize((image.width*scale, image.height*scale), Image.Resampling.NEAREST)
    meta = PngImagePlugin.PngInfo();meta.add_text('provenance', __doc__)
    scaled.save(path, pnginfo=meta)
    with Image.open(path) as decoded:
        assert decoded.tobytes() == scaled.tobytes()
        assert decoded.resize(image.size, Image.Resampling.NEAREST).tobytes() == image.tobytes()
    return dict(path=str(path.relative_to(ROOT)), sha256=sha(path), size=list(scaled.size))


def verify(out):
    records = []
    pips = {1: ((0,0),), 2: ((-4,-4),(4,4)), 3: ((-4,-4),(0,0),(4,4)),
            4: ((-4,-4),(4,-4),(-4,4),(4,4))}
    for theme in ('graphite', 'pink'):
        for case in ('flow', 'dice', 'level'):
            a = load(out, 'current', theme, case, 'top')
            b = load(out, 'proposal', theme, case, 'top')
            assert a.tobytes() == b.tobytes(), ('upper changed', theme, case)
            a = load(out, 'current', theme, case, 'bottom')
            b = load(out, 'proposal', theme, case, 'bottom')
            allowed = Image.new('1', (320,240));d = ImageDraw.Draw(allowed)
            if case == 'dice':
                for row in (45,83,121,159):
                    for column in range(16):
                        x = 8+19*column;d.rectangle((x,row,x+17,row+17),fill=1)
                for x in (100,160,220,280):
                    d.rectangle((x-6,212,x+6,224),fill=1)
            elif case == 'level':
                d.rectangle((84,83,101,100),fill=1)
            else:
                d.rectangle((84,83,102,114),fill=1)
            changed = ImageChops.difference(a,b).convert('L').point(lambda v:255 if v else 0)
            assert ImageChops.multiply(changed, ImageChops.invert(allowed.convert('L'))).getbbox() is None
            assert a.crop((0,0,320,40)).tobytes() == b.crop((0,0,320,40)).tobytes()
            # Real C++ pixels, not a separately drawn swatch: bounding box and
            # centroid of every 2x2 pip group exactly coincide with 18px cell.
            if case == 'dice':
                ink = (244,244,244) if theme == 'graphite' else (31,20,31)
                for column, count in ((0,1),(3,2),(4,3),(7,4)):
                    x,y = 8+19*column,45
                    cell = b.crop((x,y,x+18,y+18))
                    actual = {(xx,yy) for yy in range(18) for xx in range(18) if cell.getpixel((xx,yy)) == ink}
                    expected = {(8+dx+i,8+dy+j) for dx,dy in pips[count] for i in range(2) for j in range(2)}
                    assert actual == expected, (theme,count,actual,expected)
                    assert sum(x for x,y in actual)*2 == len(actual)*17
                    assert sum(y for x,y in actual)*2 == len(actual)*17
                    assert min(x for x,y in actual)+max(x for x,y in actual)==17
                    assert min(y for x,y in actual)+max(y for x,y in actual)==17
            records.append(dict(theme=theme,case=case,upper_exact=True,scope_exact=True,
                                changed_bbox=changed.getbbox(),outside_allowed=0))
    return records


def main():
    ap = argparse.ArgumentParser(description=__doc__);ap.add_argument('--out',type=Path)
    args = ap.parse_args()
    out = (args.out or ROOT/'ui_mockups/score_edit_marks_study'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')).resolve()
    out.mkdir(parents=True,exist_ok=False)
    (out/'renderer.py').write_bytes(Path(__file__).read_bytes())
    before = protected_hashes()
    native = native_capture(out)
    checks = verify(out)
    outputs = []
    board = Canvas(688,618,OUTSIDE)
    board.text(16,12,'same score / square pips + open focus',INK,scale=2)
    board.text(16,40,'native c++ study / no new labels, panels or layout',MUTED)
    for row,(case,caption) in enumerate((('dice','retrig / centered squares'),('flow','flow / edit point without a plate'))):
        y=64+row*274
        for column,(version,title) in enumerate((('current','current'),('proposal',caption))):
            x=16+336*column
            board.text(x,y,title,ACCENT)
            board.im.paste(load(out,version,'graphite',case,'bottom'),(x,y+18))
    board.text(16,606,'upper screen, scope, grid and editor strip remain unchanged',MUTED)
    outputs.append(save(out,'current-vs-proposal-2x.png',board.im))
    # Two native theme examples, with the unchanged current upper companion.
    for theme in ('graphite','pink'):
        for case in ('flow','dice','level'):
            top=load(out,'proposal',theme,case,'top');bottom=load(out,'proposal',theme,case,'bottom')
            pair=Image.new('RGB',(400,496),OUTSIDE);pair.paste(top,(0,0));pair.paste(bottom,(40,256))
            outputs.append(save(out,f'{theme}-{case}-dual-2x.png',pair))
            outputs.append(save(out,f'{theme}-{case}-bottom-2x.png',bottom))
    close = Canvas(464,148,OUTSIDE)
    close.text(16,12,'square pips / native cells enlarged 4x',INK)
    source=load(out,'proposal','graphite','dice','bottom')
    for i,column in enumerate((0,3,4,7)):
        x=8+column*19;cell=source.crop((x,45,x+18,63))
        close.im.paste(cell.resize((72,72),Image.Resampling.NEAREST),(16+i*112,38))
        close.text(16+i*112,122,f'{i+1} hit'+('s' if i else ''),MUTED)
    outputs.append(save(out,'centered-square-pips.png',close.im,2))
    assert before == protected_hashes(),'production/assets/artifacts changed'
    report=dict(status='visual study only; user has NOT approved implementation',provenance=__doc__,
                created_at=datetime.now(timezone.utc).isoformat(),native=native,outputs=outputs,
                checks=checks,square_pip_centers='8.5,8.5 exactly in all 18x18 cells, verified from actual pixels',
                protected_sha256=before,script_sha256=sha(__file__),
                changes=['2x2 centered square pips instead of bars / diagonal 3 instead of triangle',
                         'unfilled corner marks instead of selected ink plate',
                         'small flow edit point instead of oversized selected dot'],
                unchanged=['upper pixels','scope pixels','step grid','row spacing','typography','existing editor/footer',
                           'flow values/line','engine/controller/persistence','production source and console binaries'],
                limitations=['isolated C++ draw variant; NOT shipped or installed',
                             'static selection study; no replacement motion/gesture behaviour implemented',
                             'host raster not PICA hardware acceptance; no broad inherited suites run'])
    (out/'proposal.json').write_text(json.dumps(report,indent=2)+'\n')
    (out/'README.md').write_text('# square pips and open edit focus\n\n'+__doc__+'\n\n'
        'the earlier flow_ribbon_study labelled lower layout and filled selection were REJECTED.\n'
        'this proposal keeps the actual current sequencer; only local pip/focus drawing changes in private copies.\n'
        'production source, upper screens and all pixels outside the stated control regions are verified unchanged.\n')
    print(out.relative_to(ROOT))
    print(f'{len(outputs)} images / {len(checks)} native before-after pairs / 8 exact square-pip center checks')
    print(out/'current-vs-proposal-2x.png')


if __name__=='__main__':main()
