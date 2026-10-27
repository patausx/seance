#!/usr/bin/env python3
"""Three non-shipping INST visual directions on current native editor surfaces.

Uses fresh private rendering/scene objects with a pinned, isolated host baseline.
The INST renderer/material still match current; unrelated concurrent WIP is not
compiled or repaired. Input, DSP and lower editor are unchanged by this study.
This is a visual choice, not approval,
an animation, a full regression run, an ARM build, or device sound/FPS evidence.
"""
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shlex
import subprocess

from PIL import Image, ImageChops, PngImagePlugin
from render_module_geometry_study import Canvas, protected_hashes

ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / 'release/score-selection/capture-optimized-20261005T184023_932973Z'
SOURCE = ROOT / 'platform/3ds/ui_module_focus_inst.cpp'
HELPERS = ROOT / 'tools/inst_directions_study.inc'
BG = (18, 18, 21)
VERSIONS = ('current', 'voices', 'keys', 'engraving')
FAMILIES = ('piano', 'violin', 'brass', 'flute', 'bass', 'drums')
LABELS = ('00 / current', '01 / voices', '02 / keys and hits', '03 / engraving')

DRIVER = r'''
#include "editor_style.h"
namespace seance::platform::ui3ds { int inst_study_variant=0; }
namespace inst_directions_capture {
std::filesystem::path output;
std::ofstream states[4];
int first_variant=0,last_variant=0;
const char* versions[]={"current","voices","keys","engraving"};
void capture(InstFixture& f,const std::string& name) {
    f.capture();inst_focus_capture();
    const auto before=f.unit().visual();const auto bases=f.bases();const int dirty=mutations;
    for(int variant=first_variant;variant<=last_variant;++variant) {
        inst_study_variant=variant;
        auto& data=states[variant];data<<name<<'\t'<<int(before.active)<<'\t'<<before.strikes;
        for(const auto& v:before.voices)data<<'\t'<<int(v.instrument)<<','<<int(v.note)<<','<<int(v.stage)<<','<<v.age<<','<<v.envelope;
        for(auto q:bases)data<<'\t'<<q;
        data<<'\n';
        int total=0;
        for(bool top:{true,false}) {
            module_inspector_surface(top);
            {ModuleInspectorNoEngine detached;
                if(top)CHECK(inst_focus_draw_top());else inst_focus_draw(f.unit());
            }
            cleanup_geometry();total+=objects;
            CHECK(pixels[30*width+3]==editor_style::sample(top,3.5f,30.5f));
            const auto first=pixels;
            dump_surface(output/versions[variant],name+(top?"_top":"_bottom"));
            module_inspector_surface(top);
            {ModuleInspectorNoEngine detached;
                if(top)CHECK(inst_focus_draw_top());else inst_focus_draw(f.unit());
            }
            cleanup_geometry();CHECK(pixels==first);
        }
        CHECK(total<=8192);maximum_shared_objects=std::max(maximum_shared_objects,total);
        CHECK(f.bases()==bases && dirty==mutations);
        const auto after=f.unit().visual();CHECK(before.active==after.active && before.strikes==after.strikes);
        for(int i=0;i<Inst::VOICE_COUNT;++i) {
            const auto& a=before.voices[i];const auto& b=after.voices[i];
            CHECK(a.age==b.age && a.note==b.note && a.stage==b.stage && a.envelope==b.envelope && a.instrument==b.instrument);
        }
    }
}
int run(const std::filesystem::path& out,bool baseline) {
    output=out;first_variant=baseline?0:1;last_variant=baseline?0:3;
    for(int i=first_variant;i<=last_variant;++i) {
        CHECK(!std::filesystem::exists(out/versions[i]));std::filesystem::create_directories(out/versions[i]);
        states[i].open(out/versions[i]/"states.tsv");
    }
    for(auto theme:{palette::Theme::Graphite,palette::Theme::Pink}) {
        palette::set(theme);host_time_ms=10000;lancet_nonmodule::begin_page();
        editor_style::init();editor_style::capture();
        const std::string prefix=theme==palette::Theme::Graphite?"graphite_":"pink_";
        for(int family=0;family<Inst::INSTRUMENT_COUNT;++family) {
            InstFixture f;
            f.set(Inst::P_INSTRUMENT,Inst::instrument_value(family));
            f.set(Inst::P_NOTE,Inst::note_value(family==5?38:60));
            f.set(Inst::P_LENGTH,Inst::length_value(180));
            f.set(Inst::P_ATTACK,Inst::attack_value(18));
            f.set(Inst::P_RELEASE,Inst::release_value(640));
            f.settle();f.unit().reset();inst_focus_select(Inst::P_RELEASE);
            const std::string name=prefix+Inst::instrument_name(family);
            capture(f,name+"_idle");
            f.unit().trigger();f.feed(2560);capture(f,name+"_hit");
            f.unit().reset();
            const int pitches[]={72,67,64,60};
            for(int i=0;i<4;++i) {
                f.set(Inst::P_NOTE,Inst::note_value(family==5?Inst::drum_note(i):pitches[i]));
                f.settle();f.unit().trigger();f.feed(2560);
            }
            capture(f,name+"_overlap");
            if(family==1) {
                f.unit().bypass=true;capture(f,name+"_bypass");f.unit().bypass=false;
                for(int note:{24,96}) {
                    f.unit().reset();f.set(Inst::P_NOTE,Inst::note_value(note));f.settle();
                    f.unit().trigger();f.feed(2560);capture(f,name+"_note"+std::to_string(note));
                }
            }
        }
    }
    std::printf("study: frozen redraw and unchanged source state; host dual peak %d objects, not fps\n",maximum_shared_objects);
    return 0;
}
}
int main(int argc,char**argv) {
    try {CHECK(argc==3);return inst_directions_capture::run(argv[1],std::string(argv[2])=="current");}
    catch(const std::exception&e){std::fprintf(stderr,"inst directions: %s\n",e.what());return 1;}
}
'''


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run(command, log):
    with log.open('x') as stream:
        stream.write('$ ' + shlex.join(map(str, command)) + '\n');stream.flush()
        result = subprocess.run(command, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT, timeout=240)
    if result.returncode:
        raise RuntimeError(f'{log}\n{log.read_text()[-6500:]}')


def private_source():
    source = SOURCE.read_text()
    source = source.replace('namespace seance::platform::ui3ds {',
                            'namespace seance::platform::ui3ds {\nextern int inst_study_variant;', 1)
    marker = '} // namespace\n\nint inst_focus_selected()'
    assert source.count(marker) == 1
    source = source.replace(marker, HELPERS.read_text() + '\n' + marker)
    source = source.replace('ui::text(16,43,FAMILIES[frame.hero],m::ghost());',
                            'if(!inst_study_variant)ui::text(16,43,FAMILIES[frame.hero],m::ghost());')
    start = source.index('    const u32 colors[]={m::branch(),m::ghost(),frame.audible?')
    stop = source.index('    ui::text(m::kLeft,m::kTextY,frame.route', start)
    old = source[start:stop]
    source = source[:start] + '    if(inst_study_variant)inst_directions::draw(inst_study_variant);\n    else {\n' + old + '    }\n' + source[stop:]
    return source.replace('#include "../../', '#include "' + str(ROOT) + '/')


def load(out, version, theme, family, state, screen='top'):
    with Image.open(out/version/f'{theme}_{family}_{state}_{screen}.ppm') as image:
        return image.convert('RGB')


def pair(top, bottom):
    image = Image.new('RGB', (400, 496), BG)
    image.paste(top, (0, 0));image.paste(bottom, (40, 256))
    return image


def save(out, name, image):
    path = out/name
    assert not path.exists()
    scaled = image.resize((image.width*2, image.height*2), Image.Resampling.NEAREST)
    metadata = PngImagePlugin.PngInfo();metadata.add_text('provenance', __doc__)
    scaled.save(path, pnginfo=metadata)
    with Image.open(path) as decoded:
        assert decoded.convert('RGB').tobytes() == scaled.tobytes()
    return {'path': str(path.relative_to(ROOT)), 'sha256': sha(path)}


def main():
    out = ROOT/'ui_mockups/inst_directions_study'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')
    native = out/'native';native.mkdir(parents=True, exist_ok=False)
    before = protected_hashes()
    receipt = json.loads((CACHE/'receipt.json').read_text())
    assert receipt['status'] == 'passed'
    commands = receipt['commands']
    stale = {p for p, h in receipt['dependencies'].items() if not Path(p).exists() or sha(p) != h}
    # Another task is changing core/editor randomization concurrently. Do not
    # compile mixed ABIs or change its unfinished production code for a preview.
    # Freeze the exact existing host dependencies; current INST and material must
    # still match that baseline. Old input fragments are never invoked here.
    pinned = ('platform/3ds/ui_module_focus_inst.cpp','platform/3ds/menu_style.cpp',
              'platform/3ds/editor_style.h','platform/3ds/menu_style.h',
              'core/engine/inst_impl.inc','core/engine/inst_unit.h')
    for name in pinned:
        assert sha(ROOT/name) == receipt['dependencies'][str(ROOT/name)], name
    frozen = native/'inputs'
    harness = native/'harness';harness.mkdir()
    def relocate(text):
        return text.replace(str(CACHE)+'/', '@@HARNESS@@/').replace(str(ROOT)+'/',str(frozen)+'/').replace('@@HARNESS@@/',str(harness)+'/')
    retained = ROOT/'release/score-buttons/optimized-20261005T211157_221481Z/inputs'
    frozen_origins = {}
    for name, expected in receipt['dependencies'].items():
        src = Path(name)
        if src.is_relative_to(CACHE):
            assert sha(src) == expected
            target = harness/src.relative_to(CACHE)
            target.parent.mkdir(parents=True,exist_ok=True)
            target.write_text(relocate(src.read_text()))
            continue
        relative = src.relative_to(ROOT)
        candidates = (src, retained/relative)
        match = next((p for p in candidates if p.exists() and sha(p) == expected), None)
        if match is None:
            assert src.suffix == '.cpp', ('missing pinned header', name)
            continue  # already-built unrelated TU, never included by this driver
        target = frozen/relative;target.parent.mkdir(parents=True,exist_ok=True)
        target.write_bytes(match.read_bytes());assert sha(target) == expected
        frozen_origins[str(relative)] = {'source':str(match),'sha256':expected}
    driver = native/'driver.cpp'
    driver_text = (CACHE/'driver.cpp').read_text()
    marker = 'int main(int argc,char**argv)'
    assert driver_text.count(marker) == 1
    driver.write_text(relocate(driver_text.replace(marker, 'int unused_score_capture_main(int argc,char**argv)') + '\n' + DRIVER))
    proposal = native/'inst_proposal.cpp'
    proposal.write_text(private_source().replace(str(ROOT)+'/',str(frozen)+'/'))
    first = commands[0];compiler = first[0]
    flags = []
    for flag in first[1:first.index('-MMD')]:
        if flag == '-I'+str(CACHE):
            flag = '-I'+str(harness)
        elif flag.startswith('-I'):
            path = (ROOT/flag[2:]).resolve()
            flag = '-I'+str(frozen/path.relative_to(ROOT))
        flags.append(flag)
    flags += ['-I'+str(frozen/'platform/3ds')]
    objects = [Path(c[c.index('-o')+1]) for c in commands]
    reused = {str(p):sha(p) for p in objects[1:]}
    objects[0] = native/'driver.o'
    proposal_o = native/'inst_proposal.o'
    jobs = [([compiler,*flags,'-MMD','-MF',str(obj.with_suffix('.d')),'-c',str(src),'-o',str(obj)],native/(obj.stem+'-build.log'))
            for src,obj in ((driver,objects[0]),(proposal,proposal_o))]
    with ThreadPoolExecutor(max_workers=2) as pool:
        list(pool.map(lambda job: run(*job), jobs))
    for obj in (objects[0],proposal_o):
        deps=shlex.split(obj.with_suffix('.d').read_text().replace('\\\n',' '))[1:]
        assert all(Path(p).resolve().is_relative_to(native) or Path(p).resolve().is_relative_to(CACHE) for p in deps), 'non-isolated dependency'
    inst_index = next(i for i,c in enumerate(commands) if c[c.index('-c')+1] == 'platform/3ds/ui_module_focus_inst.cpp')
    for mode in ('current', 'proposals'):
        links = list(objects)
        if mode == 'proposals':links[inst_index] = proposal_o
        binary = native/mode
        run([compiler, *flags, *map(str, links), '-o', str(binary)], native/(mode+'-link.log'))
        run([str(binary), str(out), mode], native/(mode+'-capture.log'))
    for version in VERSIONS[1:]:
        assert (out/version/'states.tsv').read_bytes() == (out/'current/states.tsv').read_bytes()
    records = []
    for baseline in sorted((out/'current').glob('*.ppm')):
        for version in VERSIONS[1:]:
            with Image.open(baseline) as original, Image.open(out/version/baseline.name) as proposal_image:
                a,b = original.convert('RGB'),proposal_image.convert('RGB')
            delta = ImageChops.difference(a,b)
            if '_top.' in baseline.name:
                # Only old family-metaphor caption and the previous figure area.
                delta.paste((0,0,0),(16,43,128,51))
                delta.paste((0,0,0),(128,43,384,218))
            assert delta.getbbox() is None, ('unexpected pixels', version, baseline.name)
            records.append({'version':version,'scene':baseline.name,'outside_changes':0})
    outputs=[]
    for theme in ('graphite','pink'):
        comparison=Canvas(832,552,BG)
        for i,version in enumerate(VERSIONS):
            x=8+(i%2)*416;y=8+(i//2)*272
            comparison.text(x,y,LABELS[i],(181,154,232) if i else (184,184,184))
            comparison.im.paste(load(out,version,theme,'violin','overlap'),(x,y+20))
        outputs.append(save(out,f'{theme}-directions-2x.png',comparison.im))
        for v,version in enumerate(VERSIONS[1:],1):
            gallery=Canvas(1240,540,BG)
            for i,family in enumerate(FAMILIES):
                x=8+(i%3)*412;y=8+(i//3)*268
                gallery.text(x,y,f'{v:02d} / {version} / {family}',(181,154,232))
                gallery.im.paste(load(out,version,theme,family,'overlap'),(x,y+16))
            outputs.append(save(out,f'{theme}-{version}-six-2x.png',gallery.im))
            outputs.append(save(out,f'{theme}-{version}-dual-2x.png',pair(
                load(out,version,theme,'violin','overlap'),load(out,version,theme,'violin','overlap','bottom'))))
        duals=Canvas(1240,528,BG)
        for i,version in enumerate(VERSIONS[1:]):
            x=8+i*412;duals.text(x,8,LABELS[i+1],(181,154,232))
            duals.im.paste(pair(load(out,version,theme,'piano','overlap'),load(out,version,theme,'piano','overlap','bottom')),(x,24))
        outputs.append(save(out,f'{theme}-piano-full-2x.png',duals.im))
    after = protected_hashes()
    concurrent_changes = {p:after.get(p) for p,h in before.items() if after.get(p) != h}
    for name in pinned:assert after[name] == before[name], ('INST surface changed concurrently', name)
    assert all(sha(p) == h for p,h in reused.items()), 'cached objects changed'
    html='''<!doctype html><html lang="en"><meta charset="utf-8"><title>inst / visual directions</title>
<style>body{background:#121215;color:#ddd;font:16px monospace;margin:32px}img{max-width:100%;image-rendering:pixelated;margin:12px 0 32px}h2{color:#b59ae8}a{color:#b59ae8}</style>
<h1>inst / three directions</h1><p>studies only. the current lower panel, sound and controls are unchanged. not approved or deployed.</p>
'''
    captions=(('directions','current / voices / keys / engraving'),('piano-full','three full variants / piano'),('voices-six','01 / four voices — notes and actual envelopes'),('keys-six','02 / keys and hits — one-octave view, not an input keyboard'),('engraving-six','03 / engraving — static illustration; sounding voice envelope below'))
    for theme in ('graphite','pink'):
        for file,label in captions:html+=f'<h2>{theme} / {label}</h2><img src="{theme}-{file}-2x.png">\n'
    (out/'index.html').write_text(html)
    evidence={str(p.relative_to(ROOT)):sha(p) for p in out.rglob('*') if p.is_file()}
    report={'status':'visual proposals only / not approved / not deployed','runner_sha256':sha(__file__),
            'helper_sha256':sha(HELPERS),'fresh_objects':len(jobs),'cached_objects':reused,
            'baseline_receipt':str(CACHE/'receipt.json'),'baseline_receipt_sha256':sha(CACHE/'receipt.json'),
            'excluded_unrelated_wip':sorted(stale),'frozen_origins':frozen_origins,
            'protected_before':before,'protected_after':after,'concurrent_changes_not_by_this_study':concurrent_changes,
            'native_comparisons':records,'media':outputs,'evidence_sha256':evidence,
            'limits':['static host previews, not animation or hardware','no full tests or new input proof','not a full app/ARM build','no production edits or deployment',
                      'engraving is illustration, envelope lanes are not audio peak meters','keyboard is one-octave observation window, outside tails counted','no copied proposal control surface']}
    (out/'proposal.json').write_text(json.dumps(report,indent=2,sort_keys=True)+'\n')
    print(out.relative_to(ROOT))
    print(f'pass: {len(records)} native comparisons; unchanged lower panels/other top pixels; frozen redraws; isolated pinned host dependencies')
    print(f'production edits by this study: zero; concurrent unrelated changes: {len(concurrent_changes)}')
    print('visual proposals only; inspect images before choosing; no deployment')


if __name__ == '__main__':
    main()
