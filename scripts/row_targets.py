#!/usr/bin/env python3
"""THE TARGETS A ROW NEEDS BUILT, AND WHETHER A BUILD's STAMP COVERS THEM (ENGINE-HEADERS-1 stage 0: BUILD BY REACH; CODING_RULES
§ 4 "WHICH BUILD": before a sub-step's commit the build is the targets the changed code's rows need).

A row's TARGETS (CMake target names) are read from ctest's own listing and build.ninja, never guessed:
  * every word of its command (or the path after `--opt=`) that is the artefact of a target of the build - its executable,
    a library it is handed;
  * its property JAH_BUILD_TARGETS: target names, `all` (the row reads every binary of the default build), or `none` (its
    script runs nothing the build makes beyond its command line). A row whose command is NOT an artefact (a python, sh,
    bash or cmake script) and that declares nothing is UNDECLARED: it needs `all` (a script may run any binary of the build:
    it is never run against a binary the call did not build). source.on_demand_rows holds every CODE row declared;
  * the same, transitively, for the setup and cleanup rows of every fixture it requires (ctest runs them with it).
COVERED: a target named in the stamp's `targets=` (cmake/BuiltFrom.cmake: what is current at the stamp's state), or `all` there
and the target in the closure of the default build (its order-only inputs included: ninja builds them). A record whose row's
targets are not all covered is never its tip's (gate_runlog.py `built.uncovered`); the lead tools build exactly the targets of
the rows they run (util.build_rows, `spec.sh test --code`).

usage: row_targets.py <build dir> < rows (one a line)   -> JSON {"targets": [...], "rows": {row: [...]}, "undeclared": [...],
       "unknown": [...]} on stdout; exit 0, 2 when the build cannot be read."""
import json
import os
import pickle
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
_VERSION = 2


def _names(v):
    """A property's values: ctest's listing gives a list property as a list or as one `;`-joined string."""
    return [x for w in ([] if v is None else [v] if isinstance(v, str) else list(v)) for x in str(w).split(";") if x]


class Ninja:
    """The build's target names (phony edges without a `/`), each one's inputs, and the closure of `all` - order-only inputs
    included (ninja builds them for `all`). Cached beside build.ninja on its mtime."""

    def __init__(self, build):
        self.build = os.path.realpath(build)
        self.target_of = {}     # artefact (realpath) -> target name
        self.targets = set()    # every target name
        self.default = set()    # target names inside the closure of `all`

    @classmethod
    def load(cls, build):
        bn = os.path.join(build, "build.ninja")
        if not os.path.isfile(bn):
            return None
        key = (_VERSION, os.path.realpath(build), os.path.getmtime(bn))
        cache = os.path.join(build, ".row-targets.pickle")
        try:
            with open(cache, "rb") as f:
                k, n = pickle.load(f)
            if k == key:
                return n
        except Exception:
            pass
        n = cls(build)
        n._parse(bn)
        try:
            with open(cache + ".tmp", "wb") as f:
                pickle.dump((key, n), f, protocol=pickle.HIGHEST_PROTOCOL)
            os.replace(cache + ".tmp", cache)
        except OSError:
            pass
        return n

    def _abs(self, p):
        return os.path.normpath(p if os.path.isabs(p) else os.path.join(self.build, p))

    def _parse(self, path):
        sys.path.insert(0, HERE)
        from gate_graph import _find_unescaped, _split_ninja
        ins_of, phony, linked = {}, {}, []
        for line in open(path, errors="replace").read().replace("$\n", "").split("\n"):
            if not line.startswith("build "):
                continue
            body = line[6:]
            c = _find_unescaped(body, ":")
            if c < 0:
                continue
            outs_s, rest = body[:c], body[c + 1:]
            bar = _find_unescaped(outs_s, "|")
            outs = _split_ninja(outs_s[:bar] if bar >= 0 else outs_s) + (_split_ninja(outs_s[bar + 1:]) if bar >= 0 else [])
            words = _split_ninja(rest)
            if not words:
                continue
            rule, ins = words[0], [w for w in words[1:] if w not in ("|", "||")]
            expl = words[1:words.index("|")] if "|" in words else words[1:words.index("||")] if "||" in words else words[1:]
            for o in outs:
                ins_of[o] = ins
            if "_LINKER" in rule and outs:
                linked.append(outs[0])
            if rule == "phony" and len(outs) == 1 and "/" not in outs[0] and outs[0] != "all" \
                    and not outs[0].startswith("cmake_object_order_depends_target_"):
                phony[outs[0]] = expl
        seen, stack = set(), ["all"]
        while stack:
            for i in ins_of.get(stack.pop(), ()):
                if i not in seen:
                    seen.add(i)
                    stack.append(i)
        self.targets = set(phony)
        self.default = {t for t, ins in phony.items() if t in seen or (ins and all(i in seen for i in ins))}
        # an artefact's target: the phony edge naming it alone (`build Jahshaka: phony bin/Jahshaka`); of two names (a library's
        # target and its file name), the one without a dot
        for t in sorted(phony, key=lambda t: ("." in t, t)):
            ins = phony[t]
            if len(ins) == 1:
                self.target_of.setdefault(os.path.realpath(self._abs(ins[0])), t)
        # an artefact at the build's top whose path IS its target's name has no phony edge (CMake writes none): its link edge
        for o in linked:
            if "/" not in o and o not in phony:
                self.targets.add(o)
                self.target_of.setdefault(os.path.realpath(self._abs(o)), o)
                if o in seen:
                    self.default.add(o)


def ctest_doc(build):
    sys.path.insert(0, HERE)
    import gate_graph
    raw, rc = gate_graph.ctest_inventory(build)
    if rc != 0:
        return None
    return json.loads(raw or "{}")


def pulls(doc):
    """{row: {the rows ctest runs WITH it}}: the setup and cleanup rows of every fixture it requires, transitively."""
    props = {t["name"]: {p["name"]: p["value"] for p in t.get("properties", [])} for t in doc.get("tests", [])}
    by_fixture = {}
    for n, p in props.items():
        for f in _names(p.get("FIXTURES_SETUP")) + _names(p.get("FIXTURES_CLEANUP")):
            by_fixture.setdefault(f, set()).add(n)
    out = {}
    for n in props:
        seen, todo = set(), [n]
        while todo:
            for f in _names(props.get(todo.pop(), {}).get("FIXTURES_REQUIRED")):
                for m in by_fixture.get(f, ()):
                    if m != n and m not in seen:
                        seen.add(m)
                        todo.append(m)
        out[n] = seen
    return out


def own_targets(t, nj):
    """(targets, undeclared, unknown names) of ONE row's listing entry `t` - its command's artefacts and its declaration."""
    cmd = [str(c) for c in (t.get("command") or [])]
    props = {p["name"]: p["value"] for p in t.get("properties", [])}
    out = set()
    for w in cmd:
        w = w.split("=", 1)[1] if w.startswith("-") and "=" in w else w
        if os.path.isabs(w):
            tg = nj.target_of.get(os.path.realpath(w))
            if tg:
                out.add(tg)
    declared = "JAH_BUILD_TARGETS" in props
    unknown = []
    for v in _names(props.get("JAH_BUILD_TARGETS")):
        if v == "none":
            continue
        if v != "all" and v not in nj.targets:
            unknown.append(v)
        out.add(v)
    undeclared = bool(cmd) and nj.target_of.get(os.path.realpath(cmd[0])) is None and not declared
    if undeclared:
        out.add("all")
    return out, undeclared, unknown


def row_targets(build, rows, doc=None, nj=None):
    """{"targets": sorted union, "rows": {row: sorted}, "undeclared": [rows - their own or a pulled row's], "unknown":
    ["row: name" - a declared name that is no target of the build]} for `rows` (each with the rows ctest runs with it)."""
    nj = nj or Ninja.load(build)
    doc = doc if doc is not None else ctest_doc(build)
    if nj is None or doc is None:
        raise OSError(f"the build {build} cannot be read (build.ninja or ctest's listing)")
    by = {t["name"]: t for t in doc.get("tests", [])}
    pl = pulls(doc)
    res, und, unk, every = {}, set(), [], set()
    for r in rows:
        if r not in by:
            continue
        need = set()
        for m in [r, *sorted(pl.get(r, ()))]:
            tg, u, k = own_targets(by[m], nj)
            need |= tg
            if u:
                und.add(m)
            unk += [f"{m}: {x}" for x in k]
        res[r] = sorted(need)
        every |= need
    return {"targets": sorted(every), "rows": res, "undeclared": sorted(und), "unknown": sorted(set(unk))}


def uncovered(stamp_targets, needs, nj):
    """The targets of `needs` the stamp does not claim: a target is covered when the stamp names it, or names `all` and the
    target is in the default build's closure."""
    have = set(stamp_targets or ())
    return sorted(t for t in needs if t not in have and not ("all" in have and (t == "all" or t in nj.default)))


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    rows = [l.strip() for l in sys.stdin if l.strip()]
    try:
        print(json.dumps(row_targets(sys.argv[1], rows), sort_keys=True))
    except OSError as e:
        print(f"row_targets: {e}", file=sys.stderr)
        sys.exit(2)
