"""Mutation operators over translation TOOL OUTPUTS (#623, translation-v1.md).

Each operator takes the text a tool emitted for one unit and returns the mutated text, or None when the
operator has no site in that text. Operators never read tool sources; they model the faults a lowering or
export implementation could make. validate_g1.py runs every mutant through the same comparisons as the
real outputs and counts a mutant as killed only when at least one comparison fails on it.

Lowered unit grammar (section 5): `put v <k> <rhs>` lines, `v = new ulong <count> 0`, `ret get v <count-1>`.
Exported C grammar (section 6): one `return <expr>;` with `i<s>`, `<decimal>ULL`, ` + `, ` - `, ` * `, `(`, `)`.
"""
import re

MASK = (1 << 64) - 1
SWAP = {'add': 'sub', 'sub': 'add', 'mul': 'add'}
C_SWAP = {'+': '-', '-': '+', '*': '+'}


def _om_lines(text):
    return text.split('\n')


def om_op_swap(text):
    """First arithmetic result computes a different operation (add->sub, sub->add, mul->add)."""
    lines = _om_lines(text)
    for i, ln in enumerate(lines):
        m = re.match(r'^put v (\d+) (add|sub|mul) (.*)$', ln)
        if m:
            lines[i] = 'put v %s %s %s' % (m.group(1), SWAP[m.group(2)], m.group(3))
            return '\n'.join(lines)
    return None


def om_const_off_by_one(text):
    """First constant result is one larger (modulo 2^64)."""
    lines = _om_lines(text)
    for i, ln in enumerate(lines):
        m = re.match(r'^put v (\d+) (\d+)$', ln)
        if m:
            lines[i] = 'put v %s %d' % (m.group(1), (int(m.group(2)) + 1) & MASK)
            return '\n'.join(lines)
    return None


def om_slot_swap(text):
    """Input slots 0 and 1 exchanged in the SSA input records (signature unchanged)."""
    lines = _om_lines(text)
    a = [i for i, ln in enumerate(lines) if re.match(r'^put v \d+ i0$', ln)]
    b = [i for i, ln in enumerate(lines) if re.match(r'^put v \d+ i1$', ln)]
    if len(a) != 1 or len(b) != 1:
        return None
    lines[a[0]] = lines[a[0]][:-2] + 'i1'
    lines[b[0]] = lines[b[0]][:-2] + 'i0'
    return '\n'.join(lines)


def _om_shape(lines):
    puts = [i for i, ln in enumerate(lines) if ln.startswith('put v ')]
    new = [i for i, ln in enumerate(lines) if re.match(r'^v = new ulong \d+ 0$', ln)]
    ret = [i for i, ln in enumerate(lines) if re.match(r'^ret get v \d+$', ln)]
    if len(new) != 1 or len(ret) != 1 or not puts:
        return None
    return puts, new[0], ret[0]


def om_plus0_strip(text):
    """The trailing `+0` pair (const 0, add(r, const)) removed; returns r directly."""
    lines = _om_lines(text)
    shape = _om_shape(lines)
    if not shape:
        return None
    puts, new, ret = shape
    n = len(puts)
    if n < 3:
        return None
    zero = re.match(r'^put v %d 0$' % (n - 2), lines[puts[-2]])
    last = re.match(r'^put v %d add get v (\d+) get v %d$' % (n - 1, n - 2), lines[puts[-1]])
    if not zero or not last or int(last.group(1)) >= n - 2:
        return None
    r = int(last.group(1))
    lines[new] = 'v = new ulong %d 0' % (n - 2)
    lines[ret] = 'ret get v %d' % r
    del lines[puts[-1]]
    del lines[puts[-2]]
    return '\n'.join(lines)


def om_plus0_dup(text):
    """A second `+0` pair appended after the last result."""
    lines = _om_lines(text)
    shape = _om_shape(lines)
    if not shape:
        return None
    puts, new, ret = shape
    n = len(puts)
    if n + 2 > 64:
        return None
    lines[new] = 'v = new ulong %d 0' % (n + 2)
    lines[ret] = 'ret get v %d' % (n + 1)
    lines[puts[-1] + 1:puts[-1] + 1] = ['put v %d 0' % n, 'put v %d add get v %d get v %d' % (n + 1, n - 1, n)]
    return '\n'.join(lines)


def _c_split(text):
    m = re.match(r'^(.*\{ return )(.*)(; \}\n)$', text, re.S)
    return (m.group(1), m.group(2), m.group(3)) if m else None


def c_op_swap(text):
    """First binary operator of the returned expression replaced (+->-, -->+, *->+)."""
    s = _c_split(text)
    if not s:
        return None
    m = re.search(r' ([+*-]) ', s[1])
    if not m:
        return None
    e = s[1][:m.start(1)] + C_SWAP[m.group(1)] + s[1][m.end(1):]
    return s[0] + e + s[2]


def c_paren_drop(text):
    """First (outermost-leftmost) parenthesis pair removed, e.g. `i0 - (i1 - i1)` -> `i0 - i1 - i1`."""
    s = _c_split(text)
    if not s:
        return None
    e = s[1]
    o = e.find('(')
    if o < 0:
        return None
    depth = 0
    for j in range(o, len(e)):
        depth += {'(': 1, ')': -1}.get(e[j], 0)
        if depth == 0:
            return s[0] + e[:o] + e[o + 1:j] + e[j + 1:] + s[2]
    return None


def c_const_off_by_one(text):
    """First decimal constant one larger (modulo 2^64)."""
    s = _c_split(text)
    if not s:
        return None
    m = re.search(r'\b(\d+)ULL\b', s[1])
    if not m:
        return None
    e = s[1][:m.start(1)] + str((int(m.group(1)) + 1) & MASK) + s[1][m.end(1):]
    return s[0] + e + s[2]


def c_slot_swap(text):
    """Parameters i0 and i1 exchanged inside the returned expression (signature unchanged)."""
    s = _c_split(text)
    if not s or not re.search(r'\bi1\b', s[0]) or not re.search(r'\bi[01]\b', s[1]):
        return None
    e = re.sub(r'\bi([01])\b', lambda m: 'i' + ('1' if m.group(1) == '0' else '0'), s[1])
    return s[0] + e + s[2]


def c_plus0_strip(text):
    """A trailing source-written ` + 0ULL` removed from the returned expression."""
    s = _c_split(text)
    if not s or not s[1].endswith(' + 0ULL'):
        return None
    return s[0] + s[1][:-len(' + 0ULL')] + s[2]


def c_plus0_dup(text):
    """An extra ` + 0ULL` appended to the returned expression (a duplicated normalization pair)."""
    s = _c_split(text)
    if not s:
        return None
    return s[0] + s[1] + ' + 0ULL' + s[2]


OM_OPERATORS = [('om-op-swap', om_op_swap), ('om-const-off-by-one', om_const_off_by_one),
                ('om-slot-swap', om_slot_swap), ('om-plus0-strip', om_plus0_strip),
                ('om-plus0-dup', om_plus0_dup)]
C_OPERATORS = [('c-op-swap', c_op_swap), ('c-paren-drop', c_paren_drop),
               ('c-const-off-by-one', c_const_off_by_one), ('c-slot-swap', c_slot_swap),
               ('c-plus0-strip', c_plus0_strip), ('c-plus0-dup', c_plus0_dup)]
