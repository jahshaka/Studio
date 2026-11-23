#!/usr/bin/env python3
"""source.db_error_looks - A LOG LINE NEVER LOOKS (NOQT-DATABASE-1 stage 3, the layer guard; the reader's FIX 2).

THE LAYER GUARD (src/data/db/connection.h): a READ that failed marks its connection, and TESTING the db::Error it
answered - its truth test, `if (error)` - is the LOOK that clears the mark. A truth test whose branch ONLY LOGS decides
nothing, yet it clears the mark: the caller then acts on the failed read's empty answer and its next write is not
refused. So a branch that only logs reads the error's CODE (`if (error.code)`: never a look), and a branch that
decides (a return, a readOk, a refusal, a count) may test it.
THE RULE: in src/, a truth test of a db::Error variable (`if (e)` / `if (!e)`, the variable declared `db::Error x`
in the same file) whose branch (the statement after it, or its braced block up to 15 lines) logs (JahLog, qWarning,
qInfo, qDebug, fprintf) and decides nothing is RED.
THE LIMITS, said (A GREEN IS NOT "NO LOG-ONLY LOOK ANYWHERE"): the row sees ONE form, `if (<a local db::Error>)` /
`if (!<it>)` with the bare variable, and reads only the branch right after the head (15 lines deep), never an `else`.
It does NOT see: the `else` of `if (!e)` (where that form's log sits); a compound condition; a ternary, `return e ? ...`;
an Error tested inside a helper or lambda it was passed to; an `auto` copy or any variable not declared `db::Error x`
in the same file (matched by name only); a struct member; a log branch that also counts (`++` reads as a decision).
Prints `ok:`/`FAIL:` lines; the last line is the verdict. argv[1] = the source root.
"""
import os
import re
import sys

DECIDES = re.compile(r"return|readOk|\*ok|= false|=false|refus|failed\(|out\.|result\.|error =|break|continue|throw|"
                     r"fail\(|\+\+|\*\w+ = ")
LOGS = re.compile(r"JahLog|qWarning|qInfo|fprintf|qDebug|sayFailed")
DECL = re.compile(r"db::Error\s+(\w+)\s*[;{=(]")


def judge(path):
    src = open(path, encoding='utf-8', errors='replace').read()
    names = set(DECL.findall(src))
    if not names:
        return 0, []
    lines = src.split('\n')
    head = re.compile(r"if \(\s*!?\s*(" + "|".join(map(re.escape, sorted(names))) + r")\s*\)")
    judged, hits = 0, []
    for i, line in enumerate(lines):
        m = head.search(line)
        if not m or line.lstrip().startswith('//'):
            continue
        judged += 1
        rest = line[m.end():].strip()
        if rest.startswith('{'):
            block, depth, j = [], rest.count('{') - rest.count('}'), i + 1
            while depth > 0 and j < len(lines) and j < i + 15:
                block.append(lines[j])
                depth += lines[j].count('{') - lines[j].count('}')
                j += 1
            body = ' '.join(block)
        else:
            body = rest or (lines[i + 1] if i + 1 < len(lines) else '')
        if LOGS.search(body) and not DECIDES.search(body):
            hits.append(f"{path}:{i + 1}: {line.strip()[:120]}")
    return judged, hits


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else '.'
    judged, hits = 0, []
    for base, _, names in os.walk(os.path.join(root, 'src')):
        for name in sorted(names):
            if name.endswith(('.cpp', '.h')):
                n, h = judge(os.path.join(base, name))
                judged += n
                hits += [x.replace(root.rstrip('/') + '/', '') for x in h]
    print(f"ok:   {judged} truth tests of a db::Error judged (CONTROL: above zero)" if judged else
          "FAIL: no truth test of a db::Error was judged - the reader saw nothing (CONTROL)")
    if hits:
        print("FAIL: a db::Error truth test whose branch only LOGS - a look that decides nothing clears the layer "
              "guard's mark; read `.code` there:")
        for h in hits:
            print("  " + h)
    else:
        print("ok:   no log-only look")
    failed = len(hits) + (0 if judged else 1)
    print(f"source.db_error_looks: {'FAILED (%d)' % failed if failed else 'PASSED'}")
    sys.stdout.flush()
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
