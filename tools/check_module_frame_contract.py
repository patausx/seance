#!/usr/bin/env python3
"""Fast, standard-library-only admission guard for present and future module UI.

Discovers UnitKind/catalogue/title owners/host fixture coverage, never pins their
count. This is a source-policy guard, not a C++ control-flow or raster proof.
"""
import hashlib
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
UI = 'platform/3ds/'
UNIT = 'core/engine/unit.h'
CATALOGUE = UI + 'picker_catalogue.inc'
LABELS = UI + 'module_heading_data.h'
LAYOUT = UI + 'module_frame_layout.h'
FIXTURE = 'tools/fader_lancet_ui_test.h'
# The product contract is fixed; the number of modules is deliberately not.
METRICS = dict(kTopWidth=400, kBottomWidth=320, kScreenHeight=240, kLeft=14,
               kRightInset=14, kTopInset=8, kBottomInset=4, kHeaderHeight=26,
               kFooterHeight=12, kTextHeight=8, kTextAdvance=6,
               kHeaderTextY=14, kHeaderMarkY=26, kFooterTextY=228,
               kCaptionGap=8, kTitleMaxWidth=57, kTitleMaxHeight=25,
               kRouteY=35, kRouteCrossY=39, kRouteUnderlineY=45, kDetailY=49)
TOKENS = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*.*?\*/', re.S)


class ContractError(ValueError):
    pass


def require(ok, message):
    if not ok:
        raise ContractError(message)


def stripped(source, strings=False):
    def replace(match):
        token = match.group()
        if not strings and not token.startswith(('/',)):
            return token
        return ''.join('\n' if c == '\n' else ' ' for c in token)
    return TOKENS.sub(replace, source)


def kind_ids(source):
    match = re.search(r'enum\s+class\s+UnitKind\b[^{}]*\{([^}]*)\}', stripped(source))
    require(match, 'UnitKind enum missing')
    result = {}
    value = -1
    for entry in match[1].split(','):
        if not entry.strip():
            continue
        item = re.fullmatch(r'\s*(\w+)\s*(?:=\s*(\d+))?\s*', entry)
        require(item, 'unsupported UnitKind entry; update the guard explicitly: '+entry.strip())
        value = int(item[2]) if item[2] is not None else value + 1
        require(item[1] not in result, 'duplicate UnitKind '+item[1])
        result[item[1]] = value
    require(result.get('Empty') == 0 and list(result)[-1] == 'Count', 'invalid UnitKind sentinels')
    count = result.pop('Count')
    result.pop('Empty')
    require(len(result) > 0 and sorted(result.values()) == list(range(1, count)),
            'UnitKind must have unique contiguous persisted ids before Count')
    return result


def same_members(actual, expected, label):
    require(len(actual) == len(set(actual)), label+': duplicate entry')
    missing, extra = set(expected)-set(actual), set(actual)-set(expected)
    require(not missing and not extra, f'{label}: missing={sorted(missing)}, extra={sorted(extra)}')


def load_sources(root=ROOT):
    paths = [UNIT, CATALOGUE, LABELS, LAYOUT, FIXTURE]
    paths += [str(p.relative_to(root)) for p in (root/UI).glob('*') if p.suffix in ('.cpp', '.h')]
    return {name: (root/name).read_text() for name in set(paths)}


def load_masks(sources, kinds=None):
    """Decode current masks keyed by kind, with no historic catalogue-size limit."""
    kinds = kind_ids(sources[UNIT]) if kinds is None else kinds
    source = stripped(sources[LABELS])
    bitmap = re.search(r'bitmap\[\]\s*=\s*\{([^}]*)\}', source)
    require(bitmap, 'module bitmap array missing')
    data = bytes(int(v, 16) for v in re.findall(r'0x([0-9a-fA-F]{2})', bitmap[1]))
    rows = re.findall(r'\{\s*UnitKind::(\w+)\s*,\s*"([^"]+)"\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}', source)
    same_members([r[0] for r in rows], kinds, 'private title masks')
    result = {}
    for kind, name, offset, width, height in rows:
        off, w, h = map(int, (offset, width, height))
        require(0 < w <= METRICS['kTitleMaxWidth'] and 0 < h <= METRICS['kTitleMaxHeight'],
                kind+': title exceeds fixed 57x25; shorten its display label, not the frame')
        stride = (w+7)//8
        require(off+stride*h <= len(data), kind+': title bitmap outside data')
        bits = bytes(bool(data[off+y*stride+x//8] & (128 >> (x&7))) for y in range(h) for x in range(w))
        require(any(bits), kind+': empty title mask')
        result[kind] = (w, h, bits, kind, name)
    return result


def text_calls(code):
    """Small text APIs only; baseline-based bitmap words and hitboxes are not text cells."""
    pattern = r'(?<![\w:])(?P<name>ui::text|text|right|text_right|center|small_press::text)\s*\('
    for match in re.finditer(pattern, code):
        start = at = match.end()
        depth = 0
        args = []
        while at < len(code):
            char = code[at]
            if char == ')' and depth == 0:
                args.append(code[start:at].strip())
                break
            if char == ',' and depth == 0:
                args.append(code[start:at].strip())
                start = at+1
            elif char in '([{':
                depth += 1
            elif char in ')]}':
                depth -= 1
            at += 1
        yield match.group('name'), args, code.count('\n', 0, match.start())+1


def check_sources(sources):
    kinds = kind_ids(sources[UNIT])
    entries = re.findall(r'^\s*PICKER_ENTRY\(\s*(\w+)\s*,\s*"([^"]+)"', stripped(sources[CATALOGUE]), re.M)
    same_members([k for k, _ in entries], kinds, 'picker catalogue')
    labels = load_masks(sources, kinds)
    for kind, name in entries:
        require(labels[kind][4] == ('filter' if kind == 'Filter' else name), kind+': stale title label')
    digest = hashlib.sha256(sources[CATALOGUE].encode()).hexdigest()
    require('// Catalogue SHA256: '+digest in sources[LABELS], 'stale heading data; run tools/generate_module_heading.py')
    layout = stripped(sources[LAYOUT], strings=True)
    for name, value in METRICS.items():
        found = re.findall(r'\bconstexpr\s+int\s+'+name+r'\s*=\s*(\d+)\s*;', layout)
        require(found == [str(value)], f'fixed frame metric {name} must remain {value}')
    fixture = re.search(r'constexpr\s+Item\s+catalogue\[\]\s*=\s*\{(.*?)\};', stripped(sources[FIXTURE]), re.S)
    require(fixture, 'native fixture catalogue missing')
    same_members(re.findall(r'\{\s*UnitKind::(\w+)\s*,', fixture[1]), kinds, 'native frame fixture')

    owners = {kind: [] for kind in kinds}
    header_files = []
    for name, source in sorted(sources.items()):
        if not name.startswith(UI) or not name.endswith(('.cpp', '.h')):
            continue
        code = stripped(source, strings=True)
        # All real platform sliders use the confirmed lancet, including non-module
        # pages and future aliases. Historical needle primitives stay only in the
        # shared header for old offline studies; they are not a UI style option.
        if name == UI+'ui_performance.cpp':
            require(not re.search(r'\bmodule_fader\s*::', code),
                    name+': mixer is a user-protected exception; keep original handles and morph')
        if name != UI+'module_fader.h':
            require(not re.search(r'\b(?:needle_horizontal(?:_track)?|needle_vertical|needle_draw|hslider|vslider)\s*\(', code),
                    name+': slider must use the shared lancet, not needle/generic slider')
        require(not re.search(r'\bmodule_heading\s*::\s*draw\s*\(', code),
                name+': coordinate-taking title draw is forbidden; use module_heading::header')
        if not re.search(r'\bmodule_heading\s*::\s*header\s*\(', code):
            continue
        header_files.append(name)
        require(re.search(r'namespace\s+m\s*=\s*editor_style\s*;', code), name+': module chrome must use editor_style')
        require('m::kHeaderTextY' in code or 'm::header_text(' in code, name+': missing shared metadata row')
        require('m::kTextY' in code or 'm::footer(' in code, name+': missing shared footer')
        require(not re.search(r'\bmenu_style\s*::\s*footer\s*\(', code), name+': menu footer is not module chrome')
        for kind in re.findall(r'module_heading\s*::\s*header\s*\(\s*UnitKind::(\w+)', code):
            require(kind in owners, name+': unknown module title '+kind)
            if name not in owners[kind]:
                owners[kind].append(name)
        for function, args, line in text_calls(code):
            if len(args) < 2:
                continue
            x, y = args[4:6] if function == 'small_press::text' and len(args) >= 6 else args[:2]
            where = f'{name}:{line}'
            if re.fullmatch(r'-?\d+', y):
                require(not (0 <= int(y) < 26 or 228 <= int(y) < 240),
                        where+': literal chrome text y; use kHeaderTextY/kTextY')
            if y.endswith('::kTextY'):
                require(x not in {'0', '4', '10', '12', '14', '16', '20'}, where+': footer edge must use kLeft')
                if function in {'right', 'text_right'}:
                    require(not x.isdigit(), where+': footer right edge must use right_edge(width)')
    require(all(owners.values()), 'missing shared-frame renderer: '+', '.join(k for k, files in owners.items() if not files))
    return {'kinds': kinds, 'owners': owners, 'header_files': header_files}


def check(root=ROOT):
    result = check_sources(load_sources(root))
    print(f"module frame contract: {len(result['kinds'])} kinds; catalogue/masks/renderers/native fixtures complete; fixed chrome metrics passed")
    return result


if __name__ == '__main__':
    try:
        check()
    except (ContractError, OSError) as error:
        print('module frame contract: '+str(error), file=sys.stderr)
        sys.exit(1)
