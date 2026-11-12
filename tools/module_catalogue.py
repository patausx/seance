"""Shared module-ID/catalogue validation for resource generators and host checks."""
import re

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


# Explicit retirement is not an arbitrary metadata omission. Persisted IDs
# remain contiguous; only these audited identities may be absent from active UI.
RETIRED_KINDS = {'Relic': 31}


def active_kind_ids(unit_source, catalogue_source):
    persisted = kind_ids(unit_source)
    retired = re.findall(r'^\s*PICKER_RETIRED\(\s*(\w+)\s*,\s*(\d+)\s*\)\s*$',
                         stripped(catalogue_source), re.M)
    require(len(retired) == len({k for k, _ in retired}), 'duplicate retired picker identity')
    require({k: int(v) for k, v in retired} == RETIRED_KINDS,
            'retired picker identities must be exactly Relic=31')
    for kind, value in RETIRED_KINDS.items():
        require(persisted.get(kind) == value, 'reserved persisted identity changed: '+kind)
    return {kind: value for kind, value in persisted.items() if kind not in RETIRED_KINDS}


def same_members(actual, expected, label):
    require(len(actual) == len(set(actual)), label+': duplicate entry')
    missing, extra = set(expected)-set(actual), set(actual)-set(expected)
    require(not missing and not extra, f'{label}: missing={sorted(missing)}, extra={sorted(extra)}')
