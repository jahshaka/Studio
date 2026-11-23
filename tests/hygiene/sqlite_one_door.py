#!/usr/bin/env python3
"""source.sqlite_one_door - SQLITE THROUGH ONE DOOR, QTSQL NOWHERE (NOQT-DATABASE-1 stage 3).

THE LAWS:
  1. `sqlite3_` appears in C/C++ only under src/data/db/ (the layer: the ONE place that calls SQLite) and tests/db/
     (the layer's own suites and the suites' test-side failure seam, tests/db/dbseam.h);
  2. `QSql` appears in no C/C++ file under src/ or tests/;
  3. no link line of the configured build (build.ninja) carries Qt6Sql;
  4. (the reader's point) no `#include` under src/ names a `tests/` path, and `data/db/state.h` (the layer's private
     half) is included only under src/data/db/ and tests/db/ (dbseam.h is test-only by its include path).
THE CONTROLS (each must read above zero, else the row reads nothing and is red): the `sqlite3_` sites in src/data/db;
SQLite on the app's own link line (bin/Jahshaka: through jahshaka_db).
Prints `ok:`/`FAIL:` lines; the last line is the verdict. argv[1] = the source root, argv[2] = the build directory.
"""
import os
import re
import sys

CODE = ('.cpp', '.h', '.hpp', '.c', '.cc')


def files(root, top):
    for base, _, names in os.walk(os.path.join(root, top)):
        for name in sorted(names):
            if name.endswith(CODE):
                yield os.path.join(base, name)


def main():
    root, build = sys.argv[1], sys.argv[2]
    failures = []
    door_sites = 0
    for top in ('src', 'tests'):
        for path in files(root, top):
            rel = os.path.relpath(path, root).replace(os.sep, '/')
            text = open(path, encoding='utf-8', errors='replace').read()
            in_door = rel.startswith('src/data/db/') or rel.startswith('tests/db/')
            for i, line in enumerate(text.split('\n'), 1):
                if 'sqlite3_' in line:
                    if rel.startswith('src/data/db/'):
                        door_sites += 1
                    elif not in_door:
                        failures.append(f"{rel}:{i}: sqlite3_ outside the door: {line.strip()[:100]}")
                if re.search(r'\bQSql', line):
                    failures.append(f"{rel}:{i}: QtSql: {line.strip()[:100]}")
                if re.match(r'\s*#\s*include\s*[<"]', line):
                    if rel.startswith('src/') and re.search(r'[<"/]tests/', line):
                        failures.append(f"{rel}:{i}: a src/ file includes a tests/ path: {line.strip()[:100]}")
                    if 'data/db/state.h' in line and not in_door:
                        failures.append(f"{rel}:{i}: the layer's private half included outside the door: {line.strip()[:100]}")
    app_sqlite = False
    ninja = os.path.join(build, 'build.ninja')
    if not os.path.isfile(ninja):
        failures.append(f"no build.ninja in {build}: the link lines cannot be read")
    else:
        in_app = False
        for i, line in enumerate(open(ninja, encoding='utf-8', errors='replace'), 1):
            if 'Qt6Sql' in line or 'Qt6::Sql' in line:
                failures.append(f"build.ninja:{i}: a link line carries Qt6Sql")
            if line.startswith('build '):
                in_app = line.startswith('build bin/Jahshaka:')
            elif in_app and line.lstrip().startswith('LINK_LIBRARIES') and 'sqlite3' in line:
                app_sqlite = True
    print(f"ok:   {door_sites} sqlite3_ sites in src/data/db (CONTROL: above zero)" if door_sites else
          "FAIL: no sqlite3_ site in src/data/db - the row reads nothing (CONTROL)")
    print("ok:   SQLite is on the app's own link line (CONTROL)" if app_sqlite else
          "FAIL: SQLite is not on bin/Jahshaka's link line - the row reads nothing (CONTROL)")
    for f in failures:
        print("FAIL: " + f)
    if not failures:
        print("ok:   sqlite3_ only at the door, QtSql nowhere, no Qt6Sql link, no tests/ include in src, state.h private")
    n = len(failures) + (0 if door_sites else 1) + (0 if app_sqlite else 1)
    print(f"source.sqlite_one_door: {'FAILED (%d)' % n if n else 'PASSED'}")
    sys.stdout.flush()
    return 1 if n else 0


if __name__ == '__main__':
    sys.exit(main())
