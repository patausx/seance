#!/usr/bin/env python3
"""Non-shipping ARP study: note columns + articulated pitch contour.

Only a private copy of two drawing fragments changes. Production input, capture,
engine, module frame, six parameter controls and installed artifacts stay intact.
Fresh scene driver + private drawing object use hash-verified current host objects.
This is a visual proposal, not an installed UI or a hardware/FPS/audio review.
"""
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess

from PIL import Image, ImageChops, PngImagePlugin
from render_module_geometry_study import Canvas, protected_hashes

ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / 'release/score-edit-marks/capture-optimized-20261005T150414_298119Z'
SOURCE = ROOT / 'platform/3ds/ui_module_focus_arp.cpp'
BG = (18, 18, 21)

LOWER = r'''    for(int i=0;i<4;++i) {
        const int p=A::P_NOTE1+i,x=14+76*i,note=A::note_semitones(frame.edit[p]);
        const bool off=note==A::NOTE_OFF, chosen=frame.selected==p;
        // Original label hit-box and every parameter rail stay where they are.
        ui::text(x,46,NAMES[p],chosen?m::spot():m::ghost());source_tag(p,x+62,46);
        char text[12];if(off)std::snprintf(text,sizeof text,"--");else figure_note(text,sizeof text,note);
        ui::text(x+31-ui::text_w(text),66,text,off?m::ghost():m::ink(),2);
        if(chosen) {
            // Open edit focus; no plate or inversion, not a playback indicator.
            for(int side:{0,1})for(int end:{0,1}) {
                const int xx=x+(side?62:0),yy=end?85:60;
                ui::line(xx,yy,xx+(side?-4:4),yy,m::spot());
                ui::line(xx,yy,xx,yy+(end?-4:4),m::spot());
            }
        }
        rail(p,x,94,62);
        // Same toggle target. A state mark replaces the repeated action word.
        ui::frame(x+15,111,5,5,m::ghost());
        if(!off)ui::rect(x+16,112,3,3,m::ink());
        ui::text(x+25,110,off?"off":"on",m::ghost());
    }
'''

UPPER = r'''    const int count=std::min(int(frame.visual.count),A::MAX_NOTES);
    if(count) {
        int low=frame.visual.notes[0],high=low;
        for(int i=1;i<count;++i) {low=std::min(low,int(frame.visual.notes[i]));high=std::max(high,int(frame.visual.notes[i]));}
        const auto y=[&](int note) {return high==low?136:169-(note-low)*65/(high-low);};
        const auto x=[&](int i) {return count==1?208:48+i*316/(count-1);};
        const int half=count<=12?5:2;
        const u32 guide=(m::branch()&0x00ffffffu)|0x55000000u;
        // A faint pitch reference, not a full grid or a second transport lane.
        if(low<=0 && high>=0) {
            const int zero=y(0);
            for(int xx=32;xx<379;xx+=8)ui::line(xx,zero,std::min(xx+2,379),zero,guide);
            ui::text(16,zero-3,"0",m::ghost());
        }
        // Diagonal thread connects discrete note marks: scheduled order, no
        // interpolated pitch, new modulation, synthetic waveform or fake trail.
        for(int i=1;i<count;++i)
            ui::line(x(i-1)+half+2,y(frame.visual.notes[i-1]),x(i)-half-2,y(frame.visual.notes[i]),guide);
        for(int i=0;i<count;++i) {
            const int xx=x(i),yy=y(frame.visual.notes[i]);
            const bool current=frame.visual.active && frame.visual.current==i && !frame.bypass && !frame.mute;
            const u32 color=current?m::spot():m::ink();
            ui::line(xx-half,yy,xx+half,yy,color);
            // Short upright marks keep each pitch discrete. Open during gate gap.
            if(current) {
                ui::line(xx-half-3,yy-4,xx-half-3,yy+4,m::spot());
                ui::line(xx+half+3,yy-4,xx+half+3,yy+4,m::spot());
                if(frame.visual.gate)ui::rect(xx-1,yy-2,3,5,m::spot());
            } else ui::line(xx,yy-1,xx,yy+1,color);
            if(count<=12 || current) {
                figure_note(text,sizeof text,frame.visual.notes[i],true);
                ui::text(xx-ui::text_w(text)/2,179,text,current?m::spot():m::ghost());
            }
        }
    } else ui::text(40,135,"no active figure",m::ghost());
'''

DRIVER = r'''
#include "editor_style.h"
namespace arp_contour_study {
using A=ArpUnit;
void capture(ArpFixture& f,const std::filesystem::path& out,const std::string& name,std::ostream& data) {
    f.capture();arp_focus_capture();
    const auto bases=f.bases();const auto visual=f.unit().visual();const int dirty=mutations;
    data<<name<<'\t'<<int(visual.count)<<'\t'<<int(visual.current)<<'\t'<<visual.gate<<'\t'<<visual.active;
    for(int i=0;i<int(visual.count);++i)data<<'\t'<<int(visual.notes[i]);
    data<<'\n';
    int total=0;
    for(bool top:{true,false}) {
        module_inspector_surface(top);
        {ModuleInspectorNoEngine detached;
            if(top)CHECK(arp_focus_draw_top());else arp_focus_draw(f.unit());
        }
        cleanup_geometry();total+=objects;
        CHECK(pixels[30*width+3]==editor_style::sample(top,3.5f,30.5f));
        CHECK(pixels[30*width+3]!=0xff000000u); // material initialized in both themes
        const auto first=pixels;
        dump_surface(out,name+(top?"_top":"_bottom"));
        module_inspector_surface(top);
        {ModuleInspectorNoEngine detached;
            if(top)CHECK(arp_focus_draw_top());else arp_focus_draw(f.unit());
        }
        cleanup_geometry();CHECK(pixels==first);
    }
    CHECK(total<=8192);maximum_shared_objects=std::max(maximum_shared_objects,total);
    CHECK(f.bases()==bases && dirty==mutations);
    const auto after=f.unit().visual();
    CHECK(after.events==visual.events && after.current==visual.current && after.gate==visual.gate);
}
void setup(ArpFixture& f,int order,int octaves,std::initializer_list<int> notes) {
    f.engine->transport_stop();
    auto& u=f.unit();int p=A::P_NOTE1;
    for(int note:notes){u.params[p].base=A::note_value(note);u.params[p++].snap();}
    u.params[A::P_ORDER].base=A::order_value(order);
    u.params[A::P_OCTAVES].base=A::octaves_value(octaves);
    for(auto& value:u.params)value.snap();
    u.reset();f.engine->set_sequencer_enabled(false);f.engine->transport_play();f.feed(256);
}
int run(const std::filesystem::path& out) {
    CHECK(!std::filesystem::exists(out));std::filesystem::create_directories(out);
    std::ofstream data(out/"states.tsv");data<<"scene\tcount\tcurrent\tgate\tactive\tnotes...\n";
    for(auto theme:{palette::Theme::Graphite,palette::Theme::Pink}) {
        palette::set(theme);host_time_ms=10000;lancet_nonmodule::begin_page();
        editor_style::init();editor_style::capture();
        ArpFixture f;
        const std::string prefix=theme==palette::Theme::Graphite?"graphite_":"pink_";
        setup(f,0,1,{0,4,7,A::NOTE_OFF});arp_focus_select(A::P_NOTE2);f.feed(4608);
        capture(f,out,prefix+"three",data);
        setup(f,2,1,{0,3,7,12});arp_focus_select(A::P_NOTE3);f.feed(9216);
        capture(f,out,prefix+"four",data);
        setup(f,2,3,{-12,0,7,12});arp_focus_select(A::P_NOTE4);f.feed(13824);
        CHECK(f.unit().visual().count==22);capture(f,out,prefix+"dense",data);
        setup(f,0,1,{A::NOTE_OFF,A::NOTE_OFF,A::NOTE_OFF,A::NOTE_OFF});arp_focus_select(A::P_NOTE1);
        CHECK(!f.unit().visual().active);capture(f,out,prefix+"off",data);
        setup(f,0,1,{A::NOTE_OFF,1,A::NOTE_OFF,A::NOTE_OFF});arp_focus_select(A::P_NOTE2);
        capture(f,out,prefix+"one",data);
        f.engine->transport_stop();f.feed(256);capture(f,out,prefix+"stopped",data);
        setup(f,2,1,{0,3,7,12});arp_focus_select(A::P_NOTE3);
        for(int i=0;i<18;++i) {
            char name[40];std::snprintf(name,sizeof name,"motion_%02d",i);
            capture(f,out,prefix+name,data);f.feed(3072);
        }
    }
    std::printf("native proposal scenes: bounded geometry, frozen redraw, unmutated params / peak %d objects (not fps)\n",maximum_shared_objects);
    return 0;
}
}
int main(int argc,char**argv) {
    try {CHECK(argc==2);return arp_contour_study::run(argv[1]);}
    catch(const std::exception&e){std::fprintf(stderr,"arp study failed: %s\n",e.what());return 1;}
}
'''


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run(command, log):
    with log.open('x') as output:
        output.write('$ ' + ' '.join(map(str, command)) + '\n');output.flush()
        result = subprocess.run(command, cwd=ROOT, stdout=output, stderr=subprocess.STDOUT, timeout=240)
    if result.returncode:
        raise RuntimeError(f'{log}\n{log.read_text()[-6000:]}')


def private_source():
    source = SOURCE.read_text()
    start = source.index('    for(int i=0;i<4;++i) {', source.index('void arp_focus_draw('))
    stop = source.index('    ui::line(14,126,304,126,m::branch());', start)
    source = source[:start] + LOWER + source[stop:]
    start = source.index('    const int count=std::min(int(frame.visual.count),A::MAX_NOTES);')
    stop = source.index('    ui::text(16,197,status()', start)
    source = source[:start] + UPPER + source[stop:]
    # All non-drawing code is the exact production prefix, not a parallel model.
    assert source.split('void arp_focus_draw(')[0] == SOURCE.read_text().split('void arp_focus_draw(')[0]
    return source.replace('#include "../../', '#include "' + str(ROOT) + '/')


def load(out, version, theme, case, screen):
    with Image.open(out / version / f'{theme}_{case}_{screen}.ppm') as image:
        return image.convert('RGB')


def pair(top, bottom):
    image = Image.new('RGB', (400, 496), BG)
    image.paste(top, (0, 0));image.paste(bottom, (40, 256))
    return image


def save(out, name, image):
    result = image.resize((image.width*2, image.height*2), Image.Resampling.NEAREST)
    metadata = PngImagePlugin.PngInfo();metadata.add_text('provenance', __doc__)
    path = out / name
    assert not path.exists()
    result.save(path, pnginfo=metadata)
    with Image.open(path) as decoded:
        assert decoded.tobytes() == result.tobytes()
    return dict(path=str(path.relative_to(ROOT)), sha256=sha(path))


def main():
    out = ROOT / 'ui_mockups/arp_contour_study' / datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')
    native = out / 'native';native.mkdir(parents=True, exist_ok=False)
    before = protected_hashes()
    receipt = json.loads((CACHE / 'receipt.json').read_text())
    assert receipt['status'] == 'passed'
    for path, expected in receipt['dependencies'].items():
        assert Path(path).is_file() and sha(path) == expected, ('stale dependency', path)
    driver_text = (CACHE / 'driver.cpp').read_text()
    marker = 'int main(int argc,char**argv)'
    assert driver_text.count(marker) == 1
    driver = native / 'driver.cpp'
    driver.write_text(driver_text.replace(marker, 'int unused_score_study_main(int argc,char**argv)') + '\n' + DRIVER)
    drawing = native / 'arp_proposal.cpp';drawing.write_text(private_source())
    first = receipt['commands'][0]
    compiler, flags = first[0], first[1:first.index('-MMD')] + ['-I' + str(ROOT / 'platform/3ds')]
    driver_o, drawing_o = native / 'driver.o', native / 'arp_proposal.o'
    run([compiler, *flags, '-c', str(driver), '-o', str(driver_o)], native / 'driver-build.log')
    run([compiler, *flags, '-c', str(drawing), '-o', str(drawing_o)], native / 'drawing-build.log')
    commands = receipt['commands']
    objects = [Path(c[c.index('-o')+1]) for c in commands]
    arp_index = next(i for i, c in enumerate(commands) if c[c.index('-c')+1] == 'platform/3ds/ui_module_focus_arp.cpp')
    object_pins = {str(p): sha(p) for p in objects[1:]}
    for version in ('current', 'proposal'):
        links = list(objects);links[0] = driver_o
        if version == 'proposal':links[arp_index] = drawing_o
        binary = native / version
        run([compiler, *flags, *map(str, links), '-o', str(binary)], native / (version + '-link.log'))
        run([str(binary), str(out / version)], native / (version + '-capture.log'))
    assert (out/'current/states.tsv').read_bytes() == (out/'proposal/states.tsv').read_bytes()
    records, outputs = [], []
    cases = ('three', 'four', 'dense', 'off', 'one', 'stopped', *(f'motion_{i:02d}' for i in range(18)))
    for theme in ('graphite', 'pink'):
        for case in cases:
            for screen in ('top', 'bottom'):
                a, b = (load(out, v, theme, case, screen) for v in ('current', 'proposal'))
                delta = ImageChops.difference(a, b)
                if screen == 'bottom':
                    assert a.crop((0, 0, 320, 40)).tobytes() == b.crop((0, 0, 320, 40)).tobytes()
                    assert a.crop((0, 124, 320, 240)).tobytes() == b.crop((0, 124, 320, 240)).tobytes()
                    # All four actual lancet rasters remain byte-for-byte original.
                    assert a.crop((0, 89, 320, 101)).tobytes() == b.crop((0, 89, 320, 101)).tobytes()
                    delta.paste((0, 0, 0), (0, 40, 320, 124))
                else:
                    delta.paste((0, 0, 0), (0, 98, 400, 191))
                assert delta.getbbox() is None, ('outside visual area', theme, case, screen)
                records.append(dict(theme=theme, case=case, screen=screen, outside_changes=0))
            if not case.startswith('motion_'):
                image = pair(*(load(out, 'proposal', theme, case, s) for s in ('top', 'bottom')))
                outputs.append(save(out, f'{theme}-{case}-2x.png', image))
        comparison = Canvas(832, 550, BG)
        comparison.text(8, 12, 'current', (184, 184, 184))
        comparison.text(424, 12, 'proposal / note contour', (181, 154, 232))
        for x, version in ((8, 'current'), (424, 'proposal')):
            comparison.im.paste(pair(*(load(out, version, theme, 'three', s) for s in ('top', 'bottom'))), (x, 32))
        comparison.text(8, 536, 'visual study only / same controls and sound / native pixels', (184, 184, 184))
        outputs.append(save(out, f'{theme}-comparison-2x.png', comparison.im))
        # Static states only: no claimed real-time replay from the host render.
        states = Canvas(1248, 538, BG)
        for i, case in enumerate(('four', 'dense', 'off')):
            states.text(8+i*416, 12, {'four':'updown / four notes', 'dense':'22 scheduled notes', 'off':'all notes disabled'}[case], (184, 184, 184))
            states.im.paste(pair(*(load(out, 'proposal', theme, case, s) for s in ('top', 'bottom'))), (8+i*416, 32))
        outputs.append(save(out, f'{theme}-states-2x.png', states.im))
    assert before == protected_hashes(), 'production or installed artifact drift'
    assert object_pins == {str(p): sha(p) for p in objects[1:]}
    assert all(sha(p) == h for p, h in receipt['dependencies'].items())
    proof = dict(status='proposal only', source_sha256=sha(SOURCE), protected_sha256=before,
                 private_drawing_sha256=sha(drawing), driver_sha256=sha(driver), runner_sha256=sha(__file__),
                 cache_receipt_sha256=sha(CACHE/'receipt.json'), cached_object_sha256=object_pins,
                 dependency_sha256=receipt['dependencies'], states_sha256=sha(out/'proposal/states.tsv'),
                 rasters=records, outputs=outputs, changed_production=[], inherited_suites_run=False,
                 note_rails_exact=True, six_parameter_controls_exact=True, hardware=False, deployed=False)
    (out/'proposal.json').write_text(json.dumps(proof, indent=2, sort_keys=True) + '\n')
    print(out)
    print(f'{len(records)} native rasters; drawing-only private copy; unchanged source and artifacts; no device operation')


if __name__ == '__main__':
    main()
