#!/usr/bin/env python3
"""Mutation tests for new-module admission. All edits are to in-memory fixtures."""
import hashlib
import re
import sys
import unittest

sys.dont_write_bytecode = True
import check_module_frame_contract as contract


def replace_once(sources, path, old, new):
    assert old in sources[path], (path, old)
    sources[path] = sources[path].replace(old, new, 1)


def future_fixture():
    """Synthetic source catalogue, not a real new DSP module or raster proof."""
    sources = contract.load_sources()
    # Locate this enum only: ModSource has its own Count earlier in the file.
    start = sources[contract.UNIT].index('enum class UnitKind')
    before, enum = sources[contract.UNIT][:start], sources[contract.UNIT][start:]
    sources[contract.UNIT] = before+enum.replace('    Count\n', '    FutureUnit,\n    Count\n', 1)
    sources[contract.CATALOGUE] += 'PICKER_ENTRY(FutureUnit, "future", "future", Tone, "test fixture")\n'
    digest = hashlib.sha256(sources[contract.CATALOGUE].encode()).hexdigest()
    labels = sources[contract.LABELS]
    labels = re.sub(r'// Catalogue SHA256: [0-9a-f]+', '// Catalogue SHA256: '+digest, labels)
    bitmap = re.search(r'bitmap\[\]\s*=\s*\{([^}]*)\}', labels)
    offset = len(re.findall(r'0x[0-9a-fA-F]{2}', bitmap[1]))
    labels = labels[:bitmap.end(1)]+'    0x80,\n'+labels[bitmap.end(1):]
    labels = labels.replace('inline constexpr Label labels[] = {',
                            'inline constexpr Label labels[] = {\n'+
                            f'    {{UnitKind::FutureUnit, "future", {offset}, 1, 1}},', 1)
    sources[contract.LABELS] = labels
    replace_once(sources, contract.FIXTURE, 'constexpr Item catalogue[]={',
                 'constexpr Item catalogue[]={\n    {UnitKind::FutureUnit,"future",0},')
    sources[contract.UI+'ui_module_focus_future.cpp'] = '''
namespace m = editor_style;
void future_draw() {
    module_heading::header(UnitKind::FutureUnit,m::spot());
    m::header_text(84,m::right_edge(320),"state",m::ghost());
    m::footer(320,"b back","l: mod >");
}
'''
    return sources


class FrameContractTests(unittest.TestCase):
    def reject(self, sources, reason):
        with self.assertRaisesRegex(contract.ContractError, reason):
            contract.check_sources(sources)

    def test_current(self):
        info = contract.check_sources(contract.load_sources())
        self.assertEqual(set(info['kinds']), set(info['owners']))

    def test_lancet_slider_required(self):
        for function in ('needle_horizontal', 'needle_horizontal_track', 'needle_vertical',
                         'needle_draw', 'hslider', 'vslider'):
            for file in ('ui_module_focus_future.cpp', 'ui_other_future.cpp'):
                with self.subTest(function=function, file=file):
                    sources = future_fixture()
                    sources[contract.UI+file] = sources.get(contract.UI+file, '') + f'\nvoid old_slider(){{alias::{function}(0);}}'
                    self.reject(sources, 'slider must use the shared lancet')

    def test_mixer_original_style_is_protected(self):
        sources = contract.load_sources()
        sources[contract.UI+'ui_performance.cpp'] += '\nvoid accidental_migration(){module_fader::vertical(1,2,3,4,5);}'
        self.reject(sources, 'mixer is a user-protected exception')

    def test_lancet_comments_and_canonical_calls(self):
        sources = future_fixture()
        sources[contract.UI+'ui_other_future.cpp'] = '''
// Historical needle_horizontal( is not a call.
const char* label="hslider( needle_vertical(";
void draw(){module_fader::horizontal(14,306,60,25,40);}
'''
        contract.check_sources(sources)

    def test_new_complete_kind_not_rejected_by_old_count(self):
        before = contract.kind_ids(contract.load_sources()[contract.UNIT])
        after = contract.check_sources(future_fixture())
        self.assertEqual(len(after['kinds']), len(before)+1)
        self.assertIn('FutureUnit', after['owners'])

    def test_missing_new_catalogue_entry(self):
        sources = future_fixture()
        sources[contract.CATALOGUE] = contract.load_sources()[contract.CATALOGUE]
        self.reject(sources, 'picker catalogue: missing=.*FutureUnit')

    def test_missing_new_mask(self):
        sources = future_fixture()
        sources[contract.LABELS] = contract.load_sources()[contract.LABELS]
        self.reject(sources, 'private title masks: missing=.*FutureUnit')

    def test_missing_new_editor(self):
        sources = future_fixture()
        del sources[contract.UI+'ui_module_focus_future.cpp']
        self.reject(sources, 'missing shared-frame renderer: FutureUnit')

    def test_missing_new_native_fixture(self):
        sources = future_fixture()
        sources[contract.FIXTURE] = contract.load_sources()[contract.FIXTURE]
        self.reject(sources, 'native frame fixture: missing=.*FutureUnit')

    def test_duplicates(self):
        sources = future_fixture()
        replace_once(sources, contract.CATALOGUE, 'PICKER_ENTRY(Arp,', 'PICKER_ENTRY(FutureUnit,')
        self.reject(sources, 'picker catalogue: duplicate')

    def test_geometry_drift(self):
        for name, value in contract.METRICS.items():
            with self.subTest(metric=name):
                sources = contract.load_sources()
                replace_once(sources, contract.LAYOUT, f'{name} = {value};', f'{name} = {value+1};')
                self.reject(sources, 'fixed frame metric '+name)

    def test_title_too_large(self):
        sources = future_fixture()
        sources[contract.LABELS] = re.sub(r'(UnitKind::FutureUnit, "future", \d+, )1, 1', r'\g<1>58, 1', sources[contract.LABELS])
        self.reject(sources, 'FutureUnit: title exceeds')

    def test_private_title_coordinates(self):
        sources = future_fixture()
        replace_once(sources, contract.UI+'ui_module_focus_future.cpp',
                     'module_heading::header(', 'module_heading::draw(14,2,')
        self.reject(sources, 'coordinate-taking title draw')

    def test_missing_shared_row_and_footer(self):
        for old, new, reason in [('m::header_text(', 'other_text(', 'missing shared metadata row'),
                                 ('m::footer(', 'other_footer(', 'missing shared footer')]:
            sources = future_fixture()
            replace_once(sources, contract.UI+'ui_module_focus_future.cpp', old, new)
            self.reject(sources, reason)

    def test_numeric_header_and_footer_anchors(self):
        for call in ['ui::text(84,2,"state",m::ghost());',
                     'ui::text(14,232,"back",m::ghost());',
                     'ui::text(12,m::kTextY,"back",m::ghost());',
                     'right(308,m::kTextY,"back",m::ghost());']:
            sources = future_fixture()
            sources[contract.UI+'ui_module_focus_future.cpp'] += '\n'+call
            self.reject(sources, 'literal chrome text y|footer edge|footer right edge')

    def test_comment_cannot_claim_renderer_coverage(self):
        sources = future_fixture()
        name = contract.UI+'ui_module_focus_future.cpp'
        sources[name] = '// '+sources[name].replace('\n', '\n// ')
        self.reject(sources, 'missing shared-frame renderer: FutureUnit')

    def test_stale_catalogue_hash(self):
        sources = contract.load_sources()
        sources[contract.CATALOGUE] += '// metadata changed\n'
        self.reject(sources, 'stale heading data')


if __name__ == '__main__':
    unittest.main(verbosity=2)
