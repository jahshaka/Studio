#!/usr/bin/env python3
"""source.no_live_read_at_write - NO READ LEFT ACTIVE ON A CONNECTION WHEN IT WRITES (PREVIEWS-1 stage 1).

The library has a second writing connection (the preview writer, services/previewwriter.h). A read
statement that is still ACTIVE holds a WAL snapshot; once the writer has committed, the next write on
that connection fails AT ONCE with 517 SQLITE_BUSY_SNAPSHOT - no busy wait helps, and a BEGIN IMMEDIATE
behind it fails the same way (spikes/previews-1/stage1/probe.out, cases 2 and 6). Qt resets a statement
only when it has stepped to its end (case 1) or is finished/destroyed (case 3), so the live shape is a
SINGLE-ROW read: `if (q.next())`, `q.next();`, `!q.next()` - a COUNT, a LIMIT 1, a lookup by key.

THE CLAUSES:
  1. no QSqlQuery is a class member or returned by value from a function (a cursor that outlives its
     statement's scope);
  2. a query read ONE ROW AT A TIME (a next() outside a `while (q.next())` loop) is block-scoped,
     finish()ed or re-prepared before the next WRITE in its scope: another query's INSERT / UPDATE /
     DELETE / REPLACE, or a call whose name starts with a write verb (update, insert, create, delete,
     remove, write, set, ingest, move, store, commit, ...).
THE LIMITS, said: the SQL of a read must be a string literal (a query prepared from a variable is not
judged); a write reached through a callee whose name is no write verb is not seen; the connection is not
resolved, so a side connection's read is judged too (finishing it costs nothing); a write INSIDE a
`while (q.next())` body (the read is live mid-loop) is not judged - none in src/ today (the stage-1 audit's
loop scan).
Prints `ok:`/`FAIL:` lines; the last line is the verdict. argv[1] = the repo root.
"""
import os
import re
import sys

WRITE_SQL = re.compile(r'^\s*(INSERT|UPDATE|DELETE|REPLACE)\b', re.I)
WRITE_VERB = re.compile(
    r'(?:\b\w+\s*(?:->|::|\.)\s*|\b)(?:update|insert|create|delete|remove|write|ingest|move|add|set|store|pin|'
    r'unpin|record|stamp|rename|mint|publish|purge|flush|commit|touch|replace|register|clear|drop)[A-Z]\w*\s*\(')


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
    out = []
    for m in re.finditer(r'\bQSqlQuery\s+(\w+)\s*(\(|;|\{)', src):
        name = m.group(1)
        end = scope_end(src, m.start(), opens)
        body_start = m.end()
        body = src[body_start:end]
        for r in re.finditer(r'\b' + name + r'\s*\.\s*(?:prepare|exec)\s*\(\s*(?:QStringLiteral\s*\(\s*)?"([^"]*)', body):
            if not re.match(r'\s*(SELECT|PRAGMA|WITH)\b', r.group(1), re.I):
                continue
            rest = body[r.end():]
            # the statement ends at finish/clear, or when the variable takes a NEW statement (a prepare,
            # or an exec of its own SQL); a bare exec() runs the statement just prepared
            stop = re.search(r'\b' + name + r'\s*\.\s*(?:finish|prepare|clear)\s*\(|\b' + name
                             + r'\s*\.\s*exec\s*\(\s*(?:QStringLiteral|"|\w)', rest)
            window = rest[:stop.start()] if stop else rest
            nexts = [n for n in re.finditer(r'\b' + name + r'\s*\.\s*next\s*\(\s*\)', window)
                     if not re.search(r'while\s*\(\s*$', window[max(0, n.start() - 12):n.start()])]
            if not nexts:
                continue   # never read, or read to its end by a while loop: released at DONE
            after = window[nexts[0].end():]
            writes = []
            for w in re.finditer(r'\b(\w+)\s*\.\s*(?:exec|prepare)\s*\(\s*(?:QStringLiteral\s*\(\s*)?"([^"]*)', after):
                if w.group(1) != name and WRITE_SQL.match(w.group(2)):
                    writes.append(w.start())
            for w in WRITE_VERB.finditer(after):
                if 'addBindValue' in w.group(0) or re.search(r'QSqlQuery\s+$', after[max(0, w.start() - 12):w.start()]):
                    continue
                writes.append(w.start())
            if writes:
                at = body_start + r.end() + nexts[0].end() + min(writes)
                out.append(f"{path}:{line_of(body_start + r.start())}: `{name}` is read one row at a time and still "
                           f"active at the write on line {line_of(at)}")
    # clause 1: a member or a returned cursor
    # a member is declared in a header's class body; a function returning a cursor is defined or declared
    # at the start of a line (a local query is indented inside its function)
    member = r'^\s+QSqlQuery\s+\w+\s*;' if path.endswith('.h') else r'(?!)'
    returned = r'^(?:static\s+|inline\s+)?QSqlQuery\s+[\w:]+\s*\([^;{)]*\)\s*(?:const\s*)?[{;]'
    for m in re.finditer(member + '|' + returned, src, re.M):
        out.append(f"{path}:{line_of(m.start())}: a QSqlQuery member or a function returning one")
    return out


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else '.'
    files = []
    for base, _, names in os.walk(os.path.join(root, 'src')):
        files += [os.path.join(base, n) for n in names if n.endswith(('.cpp', '.h'))]
    hits = []
    scanned = 0
    for f in sorted(files):
        text = open(f, encoding='utf-8', errors='replace').read()
        if 'QSqlQuery' not in text:
            continue
        scanned += 1
        hits += [h.replace(root.rstrip('/') + '/', '') for h in scan(f)]
    if hits:
        print("FAIL: a read is left active where its connection writes (517 SQLITE_BUSY_SNAPSHOT behind the preview writer):")
        for h in hits:
            print("  " + h)
        print("      Scope the read in a block (or finish() it) before the write - see this script's header.")
    else:
        print(f"ok:   {scanned} files with a QSqlQuery: no member or returned cursor, no single-row read active at a write")
    print(f"source.no_live_read_at_write: {'FAILED (%d)' % len(hits) if hits else 'PASSED'}")
    return 1 if hits else 0


if __name__ == '__main__':
    sys.exit(main())
