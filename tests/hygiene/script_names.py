#!/usr/bin/env python3
"""source.script_names - THE SCRIPT's NAMES ARE THE ENGINE's TOKENS, BY CONSTRUCTION (SCRIPT-IN-ENGINE-1, the tail; the
lead's ruling 2026-11-23, after builder R's audit (a): a field tabled under another name, or two enumerators' names
swapped, passes both halves of a table's guard - inOrder reads offsets, the round trip is symmetric).

THE LAW, over irisgl/engine/src/script/ (comments excluded):
  N1  no fields_<family>.cpp holds a hand-typed `field("`: every table entry is JAH_FIELD(R, m) (= field(#m, &R::m));
  N2  no records_*.h holds a hand-typed `{E::V, "` pair: every enumerator's name is JAH_ENUMERATOR(E, V) (= {E::V,
      lowerFirst(#V)}). The two macros' one home is records_shared.h.
THE CONTROL (above zero): the JAH_FIELD and JAH_ENUMERATOR uses counted in the same files (> 0 each), and the files read
(> 0 of each kind).
Its self-test first, on its own toy copies (never the live tree): a clean toy passes; ONE planted hand-typed field and
ONE planted hand-typed enum pair each red it; a commented one does not.

Run: script_names.py <source-root>
"""
import os
import re
import sys
import tempfile

FIELD = re.compile(r'\bfield\(\s*"')
PAIR = re.compile(r'\{\s*[A-Za-z_][\w:]*::\w+\s*,\s*"')


def code_of(text):
    """The text with // and /* */ comments blanked (line numbers kept)."""
    text = re.sub(r'/\*.*?\*/', lambda m: re.sub(r'[^\n]', ' ', m.group(0)), text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


def judge(script_dir):
    """(faults, fields files, records files, JAH_FIELD uses, JAH_ENUMERATOR uses)"""
    faults, nf, nr, uf, ue = [], 0, 0, 0, 0
    for fn in sorted(os.listdir(script_dir)):
        path = os.path.join(script_dir, fn)
        if fn.startswith('fields_') and fn.endswith('.cpp'):
            kind, pat = 'N1', FIELD
            nf += 1
        elif fn.startswith('records') and fn.endswith('.h'):
            kind, pat = 'N2', PAIR
            nr += 1
        else:
            continue
        with open(path, errors='replace') as f:
            code = code_of(f.read())
        uf += code.count('JAH_FIELD(')
        ue += code.count('JAH_ENUMERATOR(')
        for m in pat.finditer(code):
            line = code.count('\n', 0, m.start()) + 1
            faults.append('%s %s:%d a hand-typed %s: write it %s' % (
                kind, fn, line, 'field("' if kind == 'N1' else '{E::V, "..."} pair',
                'JAH_FIELD(R, m)' if kind == 'N1' else 'JAH_ENUMERATOR(E, V)'))
    return faults, nf, nr, uf, ue


TOY_FIELDS = 'const Table<A> &t() { static const Table<A> t({JAH_FIELD(A, x), JAH_FIELD(A, y).handle(k)}); return t; }\n'
TOY_RECORDS = 'JAH_ENUM_NAMES(K, {JAH_ENUMERATOR(K, One), JAH_ENUMERATOR(K, TwoWords)});\n'


def toy(base, fields, records):
    d = tempfile.mkdtemp(dir=base)
    with open(os.path.join(d, 'fields_toy.cpp'), 'w') as f:
        f.write(fields)
    with open(os.path.join(d, 'records_toy.h'), 'w') as f:
        f.write(records)
    with open(os.path.join(d, 'commands_toy.cpp'), 'w') as f:   # not a table file: never read
        f.write('r.add({"x", field("y", &A::y)});\n')
    return d


def selftest():
    ok = True

    def expect(what, cond):
        nonlocal ok
        print('  %s %s' % ('ok:  ' if cond else 'FAIL:', what), flush=True)
        ok = ok and cond

    with tempfile.TemporaryDirectory(prefix='script_names_toy_') as base:
        faults, nf, nr, uf, ue = judge(toy(base, TOY_FIELDS, TOY_RECORDS))
        expect('the clean toy passes, its control above zero (%d JAH_FIELD, %d JAH_ENUMERATOR)' % (uf, ue),
               not faults and nf == 1 and nr == 1 and uf == 2 and ue == 2)
        faults = judge(toy(base, TOY_FIELDS + 'static const auto z = field("z", &A::z);\n', TOY_RECORDS))[0]
        expect('N1 red: ONE planted hand-typed field("z", &A::z) (%s)' % faults,
               len(faults) == 1 and faults[0].startswith('N1 fields_toy.cpp:2'))
        faults = judge(toy(base, TOY_FIELDS, TOY_RECORDS + 'JAH_ENUM_NAMES(L, {{L::Up, "up"}});\n'))[0]
        expect('N2 red: ONE planted hand-typed {L::Up, "up"} pair (%s)' % faults,
               len(faults) == 1 and faults[0].startswith('N2 records_toy.h:2'))
        faults = judge(toy(base, TOY_FIELDS + '// field("z", &A::z) in a comment\n',
                           TOY_RECORDS + '/* {L::Up, "up"} in a comment */\n'))[0]
        expect('quiet on a commented field( and pair', not faults)
    return ok


def main():
    root = sys.argv[1]
    print('the laws catch (toy copies):', flush=True)
    toys = selftest()
    faults, nf, nr, uf, ue = judge(os.path.join(root, 'irisgl', 'engine', 'src', 'script'))
    print('the tree: %d fields_*.cpp, %d records*.h read; %d JAH_FIELD, %d JAH_ENUMERATOR (THE CONTROL: each above zero)' %
          (nf, nr, uf, ue), flush=True)
    for f in faults:
        print('  ' + f, flush=True)
    control = nf > 0 and nr > 0 and uf > 0 and ue > 0
    if not control:
        print('  the control read zero: the walk read no table (a moved folder?)', flush=True)
    bad = len(faults) + (0 if toys else 1) + (0 if control else 1)
    print('source.script_names: PASSED' if not bad else 'source.script_names: FAILED (%d)' % bad, flush=True)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
