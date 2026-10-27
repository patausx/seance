#!/usr/bin/env python3
"""Private native header study: a small rand caption near existing bypass actions.

The current composed module UI is drawn first, then a private C++ caption uses
its real font and palette. No production TU, input, DSP, route or patch changes.
This is a placement proposal, NOT a functional randomizer or sigil assignment.
Cached objects are reused only when all compiler dependencies still match their
receipt; changed objects and the current scene driver are rebuilt locally.
"""
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
import csv
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys

sys.dont_write_bytecode = True
from PIL import Image, ImageChops, PngImagePlugin
import test_lancet_nonmodule as harness
from render_module_geometry_study import Canvas, protected_hashes

ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / 'release/score-selection/capture-optimized-20261005T184023_932973Z'
BG = (18, 18, 21)
# Deliberately local anchors: dense delay/osc headers keep hold/delete/qnt intact.
ANCHORS = {'filter': 212, 'rev': 212, 'delay': 150, 'osc': 134}

DRIVER = r'''
#include "platform/3ds/editor_style.h"
namespace module_rand_study {
namespace f=fader_lancet_cases;
namespace m=editor_style;
struct Scene:f::Scene {
 explicit Scene(UnitKind kind):f::Scene(kind) {
  editor_cancel_input();param_lens_cancel();input::nav_opened=false;
  host_time_ms=10000;g_ui_frame=100;
  g_overlay=Overlay::None;capture();g_overlay=Overlay::Editor;
  select(f::item(kind).param);capture();
 }
};
int anchor(UnitKind kind) {
 switch(kind){case UnitKind::Filter:case UnitKind::Reverb:return 212;
 case UnitKind::Delay:return 150;case UnitKind::Osc:return 134;default:CHECK(false);return 0;}
}
// Private presentation overlay only. No touch target, RNG or sigil state exists.
void caption(UnitKind kind) {ui::text(anchor(kind),m::kHeaderTextY,"rand",m::ghost());}
void take(Scene& s,const std::filesystem::path& dir,const std::string& id,std::ostream& states,int& peak) {
 s.capture();m::capture();editor_gpu::begin();CHECK(m::active());
 const auto inits=editor_gpu::inits,uploads=editor_gpu::flushes;
 const auto clock=s.engine->transport_frame,now=host_time_ms;
 const int dirty=mutations;
 const auto ring=ring_process_calls,strain=strain_process_calls,rev=rev_process_calls;
 const bool bypass=s.unit->bypass;
 std::array<q15,MAX_PARAMS> bases{};
 states<<id<<'\t'<<bypass<<'\t'<<clock;
 for(int p=0;p<s.unit->num_params;++p){bases[p]=s.unit->params[p].base;states<<'\t'<<bases[p];}
 states<<'\n';
 for(bool proposal:{false,true}) {
  int sum=0;
  for(bool top:{true,false}) {
   module_inspector_surface(top);
   {f::AudioHidden hidden;if(top)top_draw();else editor_draw();}
   if(proposal&&!top)caption(s.kind);
   if(outside_rects||outside_lines||text_overlaps)
    std::fprintf(stderr,"%s proposal%d top%d bounds %d/%d overlaps%d\n",id.c_str(),proposal,top,outside_rects,outside_lines,text_overlaps);
   CHECK(objects>0&&objects<=8192&&!outside_rects&&!outside_lines&&!text_overlaps);
   // The independent bilinear texture emulator can round half bytes by one.
   for(unsigned shift:{0u,8u,16u})CHECK(std::abs(int((pixels[30*width+3]>>shift)&255)-
       int((m::sample(top,3.5f,30.5f)>>shift)&255))<=1);
   const auto first=pixels;const int count=objects;sum+=count;
   const auto folder=dir/(proposal?"proposal":"current");
   dump_surface(folder,id+(top?"_top":"_bottom"));
   module_inspector_surface(top);
   {ModuleInspectorNoEngine detached;if(top)top_draw();else f::lower(*s.unit);}
   if(proposal&&!top)caption(s.kind);
   CHECK(pixels==first&&objects==count);
  }
  CHECK(sum<=8192);peak=std::max(peak,sum);
 }
 CHECK(clock==s.engine->transport_frame&&now==host_time_ms&&dirty==mutations&&bypass==s.unit->bypass);
 CHECK(ring==ring_process_calls&&strain==strain_process_calls&&rev==rev_process_calls);
 CHECK(inits==editor_gpu::inits&&uploads==editor_gpu::flushes);
 for(int p=0;p<s.unit->num_params;++p)CHECK(bases[p]==s.unit->params[p].base);
}
int run(const std::filesystem::path& dir) {
 CHECK(!std::filesystem::exists(dir));
 std::filesystem::create_directories(dir/"current");std::filesystem::create_directory(dir/"proposal");
 std::ofstream states(dir/"states.tsv");states<<"scene\tbypass\ttransport\tbase_values...\n";
 CHECK(m::init());int peak=0,scenes=0;
 for(auto theme:{palette::Theme::Graphite,palette::Theme::Pink}) {
  palette::set(theme);host_time_ms=10000;lancet_nonmodule::begin_page();m::capture();
  const std::string prefix=theme==palette::Theme::Graphite?"graphite_":"pink_";
  for(auto kind:{UnitKind::Filter,UnitKind::Reverb,UnitKind::Delay,UnitKind::Osc}) {
   Scene s(kind);
   for(bool bypass:{false,true}) {
    s.unit->bypass=bypass;
    const auto id=prefix+f::item(kind).name+(bypass?"_bypass":"_default");
    take(s,dir,id,states,peak);++scenes;
   }
   if(kind==UnitKind::Delay) {
    s.unit->bypass=false;s.set(DelayUnit::P_HOLD,32767);s.select(DelayUnit::P_HOLD);
    take(s,dir,prefix+"delay_held",states,peak);++scenes;
   }
  }
 }
 m::shutdown();CHECK(editor_gpu::textures.empty());
 std::printf("rand placement: %d frozen scenes, current/proposal and both screens; peak %d/8192 host objects, not fps\n",scenes,peak);
 std::puts("current native module draw + private caption only; no randomization/input/sigil integration or deployment");
 return 0;
}
}
int main(int argc,char**argv) {
 try{CHECK(argc==2);return module_rand_study::run(argv[1]);}
 catch(const std::exception&e){std::fprintf(stderr,"rand study failed: %s\n",e.what());return 1;}
}
'''


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def save_json(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n')


def run(command, log):
    with log.open('x') as stream:
        stream.write('$ ' + shlex.join(map(str, command)) + '\n');stream.flush()
        result = subprocess.run(command, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT, timeout=240)
    if result.returncode:
        raise RuntimeError(str(log) + '\n' + log.read_text()[-6000:])


def dependencies(obj):
    text = obj.with_suffix('.d').read_text().replace('\\\n', ' ')
    return [(ROOT / p).resolve() for p in shlex.split(text.split(':', 1)[1])]


def png(path, image, scale=2):
    large = image.resize((image.width*scale, image.height*scale), Image.Resampling.NEAREST)
    metadata = PngImagePlugin.PngInfo();metadata.add_text('provenance', __doc__)
    assert not path.exists()
    large.save(path, pnginfo=metadata)
    with Image.open(path) as decoded:
        assert decoded.size == large.size and decoded.tobytes() == large.tobytes()


def image(folder, version, stem, screen):
    with Image.open(folder/'capture'/version/(stem+'_'+screen+'.ppm')) as im:
        return im.convert('RGB')


def package(folder):
    comparisons = []
    for baseline in sorted((folder/'capture/current').glob('*.ppm')):
        stem, screen = baseline.stem.rsplit('_', 1)
        module = stem.split('_')[1];x = ANCHORS[module]
        a, b = (image(folder, v, stem, screen) for v in ('current', 'proposal'))
        delta = ImageChops.difference(a, b)
        changed = delta.getbbox()
        # Entire upper screen and every pixel outside the new caption are exact.
        if screen == 'bottom':
            assert changed is not None
            delta.paste((0, 0, 0), (x, 14, x+24, 22))
        assert delta.getbbox() is None, ('outside caption changed', stem, screen)
        old_text = list(csv.DictReader(baseline.with_name(baseline.stem+'_text.tsv').open(), delimiter='\t'))
        new_text = list(csv.DictReader((folder/'capture/proposal'/(baseline.stem+'_text.tsv')).open(), delimiter='\t'))
        if screen == 'top':
            assert old_text == new_text
        else:
            assert new_text[:-1] == old_text and new_text[-1]['text'] == 'rand'
            # Existing header labels retain positions/values; the new 24x8 cell
            # must not overlap their ink or text cells, not merely overwrite them.
            for t in old_text:
                tx, ty, w, h = (int(t[k]) for k in ('x', 'y', 'w', 'h'))
                assert tx+w <= x or tx >= x+24 or ty+h <= 14 or ty >= 22, (stem, t)
        comparisons.append(dict(scene=stem, screen=screen, changed_bbox=changed, outside_changes=0))
    assert len(comparisons) == 36
    for theme in ('graphite', 'pink'):
        board = Canvas(672, 560, BG)
        board.text(8, 8, 'rand / header placement', (244, 244, 244))
        board.text(8, 548, 'native preview only / no randomization yet', (177, 174, 184))
        headers = Canvas(656, 220, BG)
        headers.text(8, 8, 'current', (177, 174, 184));headers.text(336, 8, 'with rand', (181, 154, 232))
        for i, module in enumerate(ANCHORS):
            stem = theme+'_'+module+'_default'
            bottom = image(folder, 'proposal', stem, 'bottom')
            x, y = 8+(i%2)*336, 40+(i//2)*256
            board.text(x, y-12, module, (177, 174, 184));board.im.paste(bottom, (x, y))
            top = image(folder, 'proposal', stem, 'top')
            dual = Image.new('RGB', (400, 496), BG);dual.paste(top, (0, 0));dual.paste(bottom, (40, 256))
            png(folder/(theme+'-'+module+'-dual-2x.png'), dual)
            png(folder/(theme+'-'+module+'-bottom-2x.png'), bottom)
            png(folder/(theme+'-'+module+'-bottom-1x.png'), bottom, 1)
            for dx, version in ((8, 'current'), (336, 'proposal')):
                crop = image(folder, version, stem, 'bottom').crop((0, 0, 320, 28))
                headers.im.paste(crop, (dx, 30+i*46))
        png(folder/(theme+'-modules-2x.png'), board.im)
        png(folder/(theme+'-headers-before-after-2x.png'), headers.im)
    return comparisons


def main():
    folder = ROOT/'ui_mockups/module_rand_study'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')
    native = folder/'native';native.mkdir(parents=True, exist_ok=False)
    before = protected_hashes()
    old = json.loads((CACHE/'receipt.json').read_text());assert old['status'] == 'passed'
    proof = dict(status='building', proposal_only=True, protected=before, runner_sha256=sha(__file__),
                 cache_receipt_sha256=sha(CACHE/'receipt.json'), anchors=ANCHORS,
                 changed_production=[], input_implemented=False, randomizer_implemented=False,
                 sigil_binding_implemented=False, hardware=False, deployed=False, inherited_suites_run=False)
    receipt = folder/'proposal.json';save_json(receipt, proof)
    source = harness.driver(native)
    text = source.read_text();marker = 'int main(int argc,char**argv)'
    assert text.count(marker) == 1
    # Established texture emulator only, not its historical module tests/main.
    texture_boundary = (ROOT/'tools/editor_fullscreen_ui_cases.inc').read_text().split('namespace editor_fullscreen_cases {', 1)[0]
    texture_boundary = texture_boundary.replace('#include "../', '#include "'+str(ROOT)+'/')
    header = (ROOT/'tools/sequencer_test_stubs/citro2d.h').read_text()
    texture_header = (ROOT/'tools/paper_motion_test_stubs/citro2d.h').read_text().splitlines()
    (native/'citro2d.h').write_text(header+'\n'+'\n'.join(texture_header[6:14]+texture_header[17:31])+'\n')
    source.write_text(text.replace(marker, 'int unused_nonmodule_main(int argc,char**argv)')+'\n'+texture_boundary+'\n'+DRIVER)
    commands = old['commands'];first = commands[0]
    compiler = os.environ.get('CXX', first[0])
    assert compiler == first[0], 'cached objects require the same compiler'
    flags = [f if f != '-I'+str(CACHE) else '-I'+str(native) for f in first[1:first.index('-MMD')]]
    flags += ['-I'+str(ROOT/'platform/3ds')]
    objects, rebuilds, reused, used_deps = [], [], {}, {}
    for i, command in enumerate(commands):
        old_obj = Path(command[command.index('-o')+1])
        name = command[command.index('-c')+1]
        deps = dependencies(old_obj) if i else []
        textured = name == 'platform/3ds/menu_style.cpp'
        valid = i and not textured and all(p.exists() and old['dependencies'].get(str(p)) == sha(p) for p in deps)
        if valid:
            assert old_obj.is_file()
            objects.append(old_obj);reused[str(old_obj)] = sha(old_obj)
            used_deps.update({str(p):sha(p) for p in deps})
            continue
        obj = native/(f'{i:02d}_'+Path(name).stem+'.o');objects.append(obj)
        if i == 0:name = str(source)
        cmd = [compiler, *flags, *(['-D__3DS__'] if textured else []), '-MMD', '-MF', str(obj.with_suffix('.d')), '-c', name, '-o', str(obj)]
        rebuilds.append((i, cmd, obj))
    proof.update(commands=[c for _, c, _ in rebuilds], reused_objects=reused)
    save_json(receipt, proof)
    def compile_one(item):
        i, cmd, _ = item;run(cmd, native/f'compile-{i:02d}.log');print('fresh object', i, flush=True)
    with ThreadPoolExecutor(max_workers=2) as pool:list(pool.map(compile_one, rebuilds))
    for _, _, obj in rebuilds:used_deps.update({str(p):sha(p) for p in dependencies(obj)})
    binary = native/'rand-study';link = [compiler, *flags, *map(str, objects), '-o', str(binary)]
    run(link, native/'link.log');run([str(binary), str(folder/'capture')], native/'capture.log')
    comparisons = package(folder)
    assert before == protected_hashes(), 'production or root artifact changed'
    assert reused == {p:sha(p) for p in reused}
    assert all(sha(p) == h for p, h in used_deps.items())
    proof.update(status='passed; visual proposal only', fresh_objects=len(rebuilds),
                 dependencies=used_deps, binary_sha256=sha(binary), link=link,
                 native_comparisons=comparisons, full_upper_screens_exact=18,
                 bounds_and_text_overlaps='passed', frozen_redraw='passed',
                 media={str(p.relative_to(folder)):sha(p) for p in sorted(folder.rglob('*'))
                        if p.is_file() and p.suffix in ('.png', '.ppm', '.tsv')},
                 limits=['four modules only; dense headers use local placement',
                         'private caption overlay, no production integration or touch hitboxes',
                         'no sigil UI, animation, functional randomizer, ARM, audio listening or hardware acceptance'])
    save_json(receipt, proof)
    print((native/'capture.log').read_text())
    print(folder)


if __name__ == '__main__':
    main()
