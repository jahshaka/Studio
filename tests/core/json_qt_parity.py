#!/usr/bin/env python3
"""core.json_qt_parity - THE ONE CODEC WRITES QT's TEXT (NOQT-CORE-1 stage 3; test_core_json_qt_parity.cpp says the laws).
This driver extracts the seeded template library's JSON blobs (<build>/tests/.seedhome, the warm-up's: every non-empty
`assets.asset` - a scene-node blob - and `assets.properties`, each written by Qt's Indented writer) into its own temporary
directory and runs the comparison binary on them. No template is a FAIL by name, never a pass over zero blobs.
usage: json_qt_parity.py <test_core_json_qt_parity> <JahLibrary.db>
"""
import os
import sqlite3
import subprocess
import sys
import tempfile


def main():
    if len(sys.argv) != 3:
        print('usage: json_qt_parity.py <test_core_json_qt_parity> <JahLibrary.db>')
        return 2
    try:
        con = sqlite3.connect('file:%s?mode=ro' % sys.argv[2], uri=True)
        rows = con.execute('SELECT guid, asset, properties FROM assets').fetchall()
    except sqlite3.Error as e:
        print('FAIL no seeded template library at %s (%s) - the warm-up makes it (spec.sh test warms the tree; by hand '
              'tests/support/seed_home.sh make)' % (sys.argv[2], e))
        print('core.json_qt_parity: FAILED (1)')
        return 1
    with tempfile.TemporaryDirectory() as tmp:
        files = []
        for guid, asset, props in rows:
            for kind, blob in (('asset', asset), ('properties', props)):
                if blob is None:
                    continue
                data = blob if isinstance(blob, bytes) else blob.encode('utf-8')
                if not data.lstrip()[:1] in (b'{', b'['):
                    continue
                path = os.path.join(tmp, '%s.%s.json' % (guid, kind))
                with open(path, 'wb') as f:
                    f.write(data)
                files.append(path)
        print('the seeded library: %d JSON blob(s) extracted from %d asset row(s)' % (len(files), len(rows)), flush=True)
        p = subprocess.run([sys.argv[1]] + sorted(files), capture_output=True, text=True)
        print(p.stdout, end='', flush=True)
        if p.stderr:
            print(p.stderr, end='', flush=True)
    fails = 0 if (p.returncode == 0 and files) else 1
    print('core.json_qt_parity: FAILED (%d)' % fails if fails else 'core.json_qt_parity: PASSED')
    return fails


if __name__ == '__main__':
    sys.exit(main())
