#!/usr/bin/env python3
"""THE CLEAN-STATE LAW (TEST-STREAMLINE-1 stage 3a item 4; SPECS/TEST_STREAMLINE_1_SPEC.md stage 3): every grouped row
gives the same verdict and the same numbers whichever order its group runs in, and alone. An arm's result is a function
of the arm alone - nothing an earlier arm did may reach it - so a pool's one process is a saving, never a second test.

The check: the group's arms run through the pool driver (tests/support/run_pool.py) in ONE process per order - forward,
reversed, and alone for every arm that is first in neither order (an arm run first starts on the boot's state: that IS
its alone run) - each order in a fresh home. Per arm, every run must give:
  * the same VERDICT (PASS/FAIL/CRASH/TIMEOUT), and
  * the same LAW LINES - the arm's own `ok: ` / `FAIL: ` assertion lines, in order, to the character, after the one
    normalisation a run cannot avoid: the run's home path, a guid, a 13-digit Date.now() stamp and a `<n> ms` wall are
    the run's, never the law's (each replaced by a token).
A difference is printed with the two orders and the first differing line, and the check exits 1 ("VIOLATED"); equal in
every order, exit 0 ("HOLDS"). --expect violated inverts the exit (the law fed a violation: a pair of arms that shares
state must be caught).

Usage: clean_state.py --app <Jahshaka> --driver <run_pool.py> --pool <name> [--baseline <js>] [--expect holds|violated]
                      [--headless] --arm <name> <script> <budget-s> [--arm ...] [-- <app args>]
cwd: a scratch directory of the row's (each order's home is made under it).
"""
import os
import re
import shutil
import subprocess
import sys


def parse(argv):
    o = {"app": None, "driver": None, "pool": None, "baseline": None, "expect": "holds", "headless": False,
         "arms": [], "extra": []}
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--":
            o["extra"] = argv[i + 1:]
            break
        if a in ("--app", "--driver", "--pool", "--baseline", "--expect"):
            o[a[2:]] = argv[i + 1]; i += 2
        elif a == "--headless":
            o["headless"] = True; i += 1
        elif a == "--arm":
            o["arms"].append((argv[i + 1], argv[i + 2], argv[i + 3])); i += 4
        else:
            sys.exit("clean_state.py: unknown argument '%s'" % a)
    if not (o["app"] and o["driver"] and o["pool"] and len(o["arms"]) >= 2) or o["expect"] not in ("holds", "violated"):
        sys.exit("clean_state.py: --app, --driver, --pool and two or more --arm are required; --expect holds|violated")
    return o


def orders(arms):
    """[(label, [arm...])]: forward, reversed, then alone for every arm first in neither."""
    fwd, rev = list(arms), list(reversed(arms))
    out = [("forward", fwd), ("reversed", rev)]
    for a in arms:
        if a not in (fwd[0], rev[0]):
            out.append(("alone:" + a[0], [a]))
    return out


GUID = re.compile(r"\{?[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}\}?")
STAMP = re.compile(r"\b1[0-9]{12}\b")
WALL = re.compile(r"\b[0-9]+(\.[0-9]+)? ?ms\b")


def normalise(line, home):
    line = line.replace(home, "<HOME>")
    line = GUID.sub("<GUID>", line)
    line = STAMP.sub("<STAMP>", line)
    return WALL.sub("<MS>", line)


def run_order(o, label, arms, top):
    """{arm: (verdict, [law lines])} for one order, in its own fresh home."""
    home = os.path.join(top, re.sub(r"[^a-z0-9_]", "_", label))
    shutil.rmtree(home, ignore_errors=True)
    os.makedirs(os.path.join(home, "run"))
    env = dict(os.environ)
    env.update({"HOME": home, "JAHSHAKA_DATA_ROOT": os.path.join(home, ".local", "share", "Jahshaka"),
                "JAH_POOL_ARMS": "", "JAH_KEEP_HOME": "1"})
    cmd = [sys.executable, o["driver"], "--pool", o["pool"], "--app", o["app"]]
    if o["headless"]:
        cmd.append("--headless")
    if o["baseline"]:
        cmd += ["--baseline", o["baseline"]]
    for n, s, b in arms:
        cmd += ["--arm", n, s, b]
    if o["extra"]:
        cmd += ["--"] + o["extra"]
    r = subprocess.run(cmd, cwd=os.path.join(home, "run"), env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       text=True, errors="replace")
    verdicts, lines, cur = {}, {}, None
    # THE ONE CHANNEL: the driver's `ARM <pool>.<arm> <verdict> <ms>` line; an arm's own output is echoed `| ...`
    # (JAH_KEEP_HOME=1: a green arm's too) between its ARM-BEGIN and its ARM line
    for raw in r.stdout.splitlines():
        line = raw[2:] if raw.startswith("| ") else raw
        m = re.match(r"ARM-BEGIN %s\.(\S+)" % re.escape(o["pool"]), line)
        if m:
            cur = m.group(1); lines.setdefault(cur, []); continue
        m = re.match(r"ARM %s\.(\S+) (\S+) " % re.escape(o["pool"]), raw)
        if m:
            verdicts[m.group(1)] = m.group(2); cur = None; continue
        if cur is not None and re.match(r"(ok|FAIL): ", line):
            lines[cur].append(normalise(line, home))
    return {n: (verdicts.get(n, "NONE"), lines.get(n, [])) for n, _, _ in arms}, r.stdout


def self_test():
    """The orders' rule and the normalisation, without an app (clean_state.py --self-test)."""
    a, b, c = ("a", "a.js", "1"), ("b", "b.js", "1"), ("c", "c.js", "1")
    got = [(l, [x[0] for x in arms]) for l, arms in orders([a, b, c])]
    assert got == [("forward", ["a", "b", "c"]), ("reversed", ["c", "b", "a"]), ("alone:b", ["b"])], got
    assert [l for l, _ in orders([a, b])] == ["forward", "reversed"]   # each arm first once: its alone run
    n = normalise("ok: /h/x made 2fd4e1c6-7a2d-4b1e-9c3f-0a1b2c3d4e5f at 1762190000123 in 12 ms (3 rows)", "/h/x")
    assert n == "ok: <HOME> made <GUID> at <STAMP> in <MS> (3 rows)", n
    print("clean_state.py --self-test: ok (the orders: forward, reversed, alone for an arm first in neither; the "
          "normalisation keeps every number but a home, a guid, a stamp and a wall)")
    return 0


def main(argv):
    if argv == ["--self-test"]:
        return self_test()
    o = parse(argv)
    top = os.getcwd()
    results = {}
    for label, arms in orders(o["arms"]):
        res, out = run_order(o, label, arms, top)
        print("order %-12s %s" % (label, "  ".join("%s=%s(%d lines)" % (n, v, len(l)) for n, (v, l) in res.items())))
        for n, v in res.items():
            results.setdefault(n, []).append((label, v))
    violated = []
    for n, runs in results.items():
        (l0, (v0, lines0)) = runs[0]
        for label, (v, lines) in runs[1:]:
            if v != v0:
                violated.append("%s: verdict %s in %s, %s in %s" % (n, v0, l0, v, label))
            elif lines != lines0:
                k = next((i for i in range(max(len(lines), len(lines0)))
                          if i >= len(lines) or i >= len(lines0) or lines[i] != lines0[i]), 0)
                a = lines0[k] if k < len(lines0) else "(none)"
                b = lines[k] if k < len(lines) else "(none)"
                violated.append("%s: law line %d differs - %s: %r / %s: %r" % (n, k + 1, l0, a[:160], label, b[:160]))
    for v in violated:
        print("VIOLATION " + v)
    held = not violated
    print("THE CLEAN-STATE LAW %s for pool %s over %d arm(s), %d order(s) (expected: %s)"
          % ("HOLDS" if held else "VIOLATED", o["pool"], len(o["arms"]), len(orders(o["arms"])), o["expect"]))
    ok = held == (o["expect"] == "holds")
    if not held and o["expect"] == "holds":
        print("  (an arm read what another arm left: the pool's baseline must put it back - its BASELINE script, or the "
              "arm's own scene made explicitly)")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
