#!/usr/bin/env python3
"""core.guid - THE ONE GUID's FORM, AND THE STORED KEYS IT MUST MATCH (NOQT-CORE-1 stage 1; irisgl/core/guid.h).
  1. test_core_guid: 100,000 iris::newGuid() (4 threads) are unique and of the form: 36 characters, lowercase hex, dashes at
     8/13/18/23, version 4, variant 8-b.
  2. the seeded template library (<build>/tests/.seedhome, the warm-up's; tests/support/seed_home.sh): EVERY `assets` key
     has the 36-character lowercase 8-4-4-4-12 TEXT form (a guid is compared as text, never parsed), and every key that is
     not a shipped fixed id has the GENERATED form above - those counted == 17 (the 17 shipped meshes' members). The
     shipped fixed ids are READ from their one table, src/data/primitives.h (every guid-text literal of the file; > 0 read,
     or FAIL), never listed here. No template is a FAIL, never a pass over zero rows.
usage: guid_library.py <test_core_guid> <JahLibrary.db> <src/data/primitives.h>
"""
import re
import sqlite3
import subprocess
import sys

TEXT = re.compile(r'^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$')
GENERATED = re.compile(r'^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$')
LITERAL = re.compile(r'"([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12})"')
WANT_GENERATED = 17


def main():
    if len(sys.argv) != 4:
        print('usage: guid_library.py <test_core_guid> <JahLibrary.db> <src/data/primitives.h>')
        return 2
    fails = []
    shipped = set(LITERAL.findall(open(sys.argv[3], encoding='utf-8').read()))
    if not shipped:
        fails.append('no shipped fixed id read from %s (the table moved?)' % sys.argv[3])
    p = subprocess.run([sys.argv[1]], capture_output=True, text=True)
    print(p.stdout, end='', flush=True)
    if p.returncode != 0:
        fails.append('test_core_guid exited %d' % p.returncode)
    try:
        con = sqlite3.connect('file:%s?mode=ro' % sys.argv[2], uri=True)
        keys = [r[0] for r in con.execute('SELECT guid FROM assets')]
    except sqlite3.Error as e:
        print('FAIL no seeded template library at %s (%s) - the warm-up makes it (spec.sh test warms the tree; by hand '
              'tests/support/seed_home.sh make)' % (sys.argv[2], e))
        print('core.guid: FAILED (1)')
        return 1
    generated = [k for k in keys if k not in shipped]
    fails += ['the stored key %r is not the 36-character lowercase text form' % k for k in keys if not TEXT.match(k or '')]
    fails += ['the generated key %r is not of newGuid\'s form (version 4, variant 8-b)' % k
              for k in generated if not GENERATED.match(k or '')]
    if len(generated) != WANT_GENERATED:
        fails.append('%d generated keys in the template, want %d' % (len(generated), WANT_GENERATED))
    print('the template library: %d of %d keys of the text form; %d shipped fixed ids read from the table; %d generated '
          'keys of newGuid\'s form (want %d)' % (sum(1 for k in keys if TEXT.match(k or '')), len(keys), len(shipped),
                                                 sum(1 for k in generated if GENERATED.match(k or '')), WANT_GENERATED),
          flush=True)
    for f in fails:
        print('FAIL ' + f)
    print('core.guid: FAILED (%d)' % len(fails) if fails else 'core.guid: PASSED')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
