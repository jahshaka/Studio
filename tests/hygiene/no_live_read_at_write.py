#!/usr/bin/env python3
"""source.no_live_read_at_write - NO READ LEFT ACTIVE ON A CONNECTION WHEN IT WRITES (PREVIEWS-1 stage 1; re-pointed at
the library's layer by NOQT-DATABASE-1 stage 3, call A).

The library has a second writing connection (the preview writer, services/previewwriter.h). A read statement that is
still ACTIVE holds a WAL snapshot; once the writer has committed, the next write on that connection fails AT ONCE
with 517 SQLITE_BUSY_SNAPSHOT - no busy wait helps, and a BEGIN IMMEDIATE behind it fails the same way
(spikes/previews-1/stage1/probe.out, cases 2 and 6). The layer leaves a statement active until it is reset()
(Statement::reset) or finalized (its scope ends) or has stepped to its end (data/db/statement.cpp), so the live shape
is a SINGLE-ROW read: `if (q.step(&e))`, `q.next()` once, `!q.step(&e)` - a COUNT, a LIMIT 1, a lookup by key.

THE CLAUSES:
  1. no db::Statement or DbQuery is a class member or returned by value from a function (a cursor that outlives
     its statement's scope);
  2. a db::Statement / DbQuery whose SQL is a read (SELECT, PRAGMA, WITH) and that is stepped ONCE (a `step(` or a
     `next()` outside a `while (...)` loop; the funnel's first step is `executeAndCheckQuery(q, ...)`) is
     block-scoped or reset() before the next WRITE in its scope: another statement whose SQL is an INSERT / UPDATE /
     DELETE / REPLACE, or a call whose name starts with a write verb (update, insert, create, delete, remove, write,
     set, ingest, move, store, commit, ...).
THE LIMITS, said: the SQL of a read must be a string literal in the declaration (a statement prepared from a variable
is not judged); a write reached through a callee whose name is no write verb is not seen; the connection is not
resolved, so a side connection's read is judged too (resetting it costs nothing); a write INSIDE a
`while (q.step(...))` body (the read is live mid-loop) is not judged.
Prints `ok:`/`FAIL:` lines; the last line is the verdict. argv[1] = the repo root.
"""
import os
import re
import sys

WRITE_SQL = re.compile(r'^\s*(INSERT|UPDATE|DELETE|REPLACE)\b', re.I)
WRITE_VERB = re.compile(
    r'(?:\b\w+\s*(?:->|::|\.)\s*|\b)(?:update|insert|create|delete|remove|write|ingest|move|add|set|store|pin|'
    r'unpin|record|stamp|rename|mint|publish|purge|flush|commit|touch|replace|register|clear|drop)[A-Z]\w*\s*\(')
TYPES = r'(?:db::Statement|DbQuery)'
SQL_ARG = r'\s*,\s*(?:dbqt::utf8\(\s*)?(?:QStringLiteral\(\s*)?(?:QString\(\s*)?"([^"]*)'


def strip(src):
    src = re.sub(r'//[^\n]*', '', src)
    return re.sub(r'/\*.*?\*/', lambda m: re.sub(r'[^\n]', ' ', m.group(0)), src, flags=re.S)


def scope_end(src, pos, opens):
    best = None
    for o, c in opens:
        if o < pos < c and (best is None or o > best[0]):
            best = (o, c)
    return best[1] if best else len(src)


def scan(path):
    src = strip(open(path, encoding='utf-8', errors='replace').read())
    line_of = lambda p: src.count('\n', 0, p) + 1
    stack, opens = [], []
    for i, ch in enumerate(src):
        if ch == '{':
            stack.append(i)
        elif ch == '}' and stack:
            opens.append((stack.pop(), i))
    out, judged = [], 0
    for m in re.finditer(TYPES + r'\s+(\w+)\s*\(\s*[^,;()]*(?:\([^()]*\))?' + SQL_ARG, src):
        name, sql = m.group(1), m.group(2)
        if not re.match(r'\s*(SELECT|PRAGMA|WITH)\b', sql, re.I):
            continue
        end = scope_end(src, m.start(), opens)
        body = src[m.end():end]
        stop = re.search(r'\b' + name + r'\s*\.\s*reset\s*\(', body)
        window = body[:stop.start()] if stop else body
        steps = [s for s in re.finditer(r'\b' + name + r'\s*\.\s*(?:step\s*\(|next\s*\(\s*\))|executeAndCheckQuery\s*\(\s*'
                                        + name + r'\b', window)
                 if not re.search(r'while\s*\([^;{)]*$', window[window.rfind('\n', 0, s.start()) + 1:s.start()])]
        loops = re.search(r'while\s*\([^;{)]*\b' + name + r'\s*\.\s*(?:step|next)\s*\(', window)
        if not steps or (loops and len(steps) == 1 and 'executeAndCheckQuery' in steps[0].group(0)):
            continue   # never read, or the funnel's first step read to its end by a while loop: released at DONE
        judged += 1
        after = window[steps[0].end():]
        writes = []
        for w in re.finditer(TYPES + r'\s+(\w+)\s*\(\s*[^,;()]*(?:\([^()]*\))?' + SQL_ARG, after):
            if w.group(1) != name and WRITE_SQL.match(w.group(2)):
                writes.append(w.start())
        for w in re.finditer(r'\.\s*exec\s*\(\s*(?:QStringLiteral\(\s*)?"([^"]*)', after):
            if WRITE_SQL.match(w.group(1)):
                writes.append(w.start())
        for w in WRITE_VERB.finditer(after):
            writes.append(w.start())
        if writes:
            at = m.end() + steps[0].end() + min(writes)
            out.append(f"{path}:{line_of(m.start())}: `{name}` is read one row at a time and still active at the "
                       f"write on line {line_of(at)}")
    # clause 1: a member or a returned cursor
    member = r'^\s+' + TYPES + r'\s+\w+\s*;' if path.endswith('.h') else r'(?!)'
    returned = r'^(?:static\s+|inline\s+)?' + TYPES + r'\s+[\w:]+\s*\([^;{)]*\)\s*(?:const\s*)?[{;]'
    for m in re.finditer(member + '|' + returned, src, re.M):
        out.append(f"{path}:{line_of(m.start())}: a db::Statement / DbQuery member or a function returning one")
    return out, judged


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else '.'
    files = []
    for base, _, names in os.walk(os.path.join(root, 'src')):
        files += [os.path.join(base, n) for n in names if n.endswith(('.cpp', '.h'))]
    hits, judged = [], 0
    for f in sorted(files):
        text = open(f, encoding='utf-8', errors='replace').read()
        if 'db::Statement' not in text and 'DbQuery' not in text:
            continue
        h, n = scan(f)
        judged += n
        hits += [x.replace(root.rstrip('/') + '/', '') for x in h]
    print(f"ok:   {judged} single-row reads judged (CONTROL: above zero)" if judged else
          "FAIL: no single-row read was judged - the rule reads nothing (CONTROL)")
    if hits:
        print("FAIL: a read is left active where its connection writes (517 SQLITE_BUSY_SNAPSHOT behind the preview writer):")
        for h in hits:
            print("  " + h)
        print("      Scope the read in a block (or reset() it) before the write - see this script's header.")
    else:
        print("ok:   no db::Statement / DbQuery member or returned cursor, no single-row read active at a write")
    failed = len(hits) + (0 if judged else 1)
    print(f"source.no_live_read_at_write: {'FAILED (%d)' % failed if failed else 'PASSED'}")
    sys.stdout.flush()
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
