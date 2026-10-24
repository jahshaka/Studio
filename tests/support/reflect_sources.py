"""THE MODE LABEL (PHOTON-7 stage 6; the lead's urgent #28: a slide reported as Rays had run in Both) - a reflection
row reads the sources its reflection RAN with, prints them and asserts the mode it intends. A mode is never assumed.
The sources are the engine's (RayQueryStatus::reflectSources: what the trace ran, not what was asked), carried by
jahlab's LAB SHOT line ("... sources <name>") and by the stillness door's refl_sources column.
usage: reflect_sources.py shots <log>... -- <shot-name prefix>=<both|rays|screen>...   (every shot must match a prefix)
       reflect_sources.py tsv <door tsv>... -- <both|rays|screen>
Prints one line per shot or door run; exit 1 (with FAIL lines) on any mismatch or on nothing read."""
import csv
import os
import re
import sys

MODES = ('both', 'rays', 'screen')


def shots(logs, intends):
    fails, seen = [], {}
    for path in logs:
        for line in open(path):
            m = re.match(r'LAB SHOT (\S+) \S+ colours \d+ frame \d+ sources (\S+)', line)
            if m:
                seen[os.path.splitext(os.path.basename(m.group(1)))[0]] = m.group(2)
    if not seen:
        fails.append('no LAB SHOT line carries its sources in %s' % ', '.join(logs))
    for name, ran in sorted(seen.items()):
        want = next((mode for prefix, mode in intends if name.startswith(prefix)), None)
        print('  shot %s: the reflection traced with the sources %s (the row intends %s)' % (name, ran, want or '-'))
        if want is None:
            fails.append('shot %s: no intended mode names it' % name)
        elif ran != want:
            fails.append('shot %s ran the sources %s, the row intends %s' % (name, ran, want))
    return fails


def tsv(paths, want):
    fails = []
    for path in paths:
        ran = sorted({r.get('refl_sources', '-') for r in csv.DictReader(open(path), delimiter='\t')} - {'-'})
        print('  %s: the reflection traced with the sources %s (the row intends %s)' % (
            os.path.basename(path), '/'.join(ran) or 'none', want))
        if ran != [want]:
            fails.append('%s ran the sources %s, the row intends %s' % (os.path.basename(path), '/'.join(ran) or 'none',
                                                                          want))
    return fails


if __name__ == '__main__':
    args = sys.argv[1:]
    if len(args) < 4 or args[0] not in ('shots', 'tsv') or '--' not in args:
        print('FAIL: usage: reflect_sources.py shots|tsv <files>... -- <intended>')
        sys.exit(1)
    cut = args.index('--')
    files, rest = args[1:cut], args[cut + 1:]
    if args[0] == 'shots':
        intends = [tuple(x.split('=', 1)) for x in rest]
        bad = [x for x in intends if len(x) != 2 or x[1] not in MODES]
        fails = ['an intended mode %s is none of %s' % ('='.join(b), '/'.join(MODES)) for b in bad] or shots(files, intends)
    else:
        fails = ['the intended mode %s is none of %s' % (rest, '/'.join(MODES))] if len(rest) != 1 or rest[0] not in MODES \
            else tsv(files, rest[0])
    for f in fails:
        print('FAIL: the mode label: ' + f)
    sys.exit(1 if fails else 0)
