#!/usr/bin/env python3
"""gate-scope — derive the SCOPED ctest tier from the paths a change touched.

    scripts/gate-scope.sh <base>..<tip> [--build build-linux] [--run] [--json]
    scripts/gate-scope.sh --files path [path ...]

The owner's rule (2026-09-09 night): the full gate (298 suites, ~25 min wall) runs once
per BATCH before a push; a lane gates on what its work can break. This tool turns a git
range (or a file list) into an exact `ctest -R '^(a|b|c)$'` selection, with one rationale
line per touched path, an estimated wall time from scripts/gate-times.txt (a full-gate snapshot) and the build dir's last run, and a
loud FALLBACK to the MERGE tier whenever a path matches nothing precise (a rule that
guesses is worse than the merge tier). Tiers are contracts: the selection printed here is
what the gate-runner runs, nothing hand-picked out of it.

How suites are found (all read from the tree and the build dir, nothing hard-wired to a
suite NAME so new suites are covered the day they are registered):
  * every suite's home is its tests/<dir>/CMakeLists.txt (ctest's backtrace) — a touched
    file under tests/<dir>/ selects that dir's suites; a touched script selects the suites
    that run it;
  * a compiled test target lists the src/ files it builds — a touched source selects every
    dir whose CMakeLists names it;
  * an app-spawning suite (--script / bash) is selected by the API MODULES its script calls
    (`assets.`, `node.`, ...) when a module's api file changed;
  * everything else goes through the AREA RULES below (a src/ or irisgl/ directory → test
    dirs + module prefixes), which are the part a human maintains.
"""
import argparse, json, os, re, subprocess, sys, collections

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# ---- AREA RULES: path prefix (or regex) -> (test dirs, script modules) --------------
# A touched path picks the FIRST rule whose prefix matches (most specific first). Test
# dirs are tests/<dir>; modules are the `<module>.` prefixes an e2e script calls. The
# special dir "*vulkan-scripts" means every app-spawning suite that is NOT --headless
# (anything that renders), "*headless-scripts" the --headless ones, "*all-scripts" both.
AREA_RULES = [
    # --- engine: anything that changes pixels or the boundary ---------------------------
    (r"^irisgl/(engine/|thirdparty/ogre-patches/|scripts/build-ogre)",
     ["engine", "gi", "lights", "looks", "distortion", "planar", "ssr", "shadow", "shadercache",
      "mirror", "cameras", "samples", "picking", "skeletal", "particles", "thumbnails",
      "materialpreview", "player", "sockets", "threading", "perf", "gizmo", "assets", "log",
      "shutdown", "openasync", "*vulkan-scripts"], []),
    (r"^irisgl/mirror/",
     ["mirror", "skeletal", "sockets", "cameras", "gizmo", "player", "thumbnails",
      "materialpreview", "samples", "picking", "particles", "*vulkan-scripts"], []),
    (r"^irisgl/import/",
     ["importer", "importasync", "meshbake", "avatar", "skeletal", "assetdelete", "assetgc",
      "assetmeta", "assetmigrate", "assetpaths", "assets", "samples", "thumbnails"],
     ["assets", "avatar", "anim"]),
    (r"^irisgl/document/(physics|animation)/",
     ["document", "skeletal", "avatar", "particles", "player", "cameras", "samples",
      "*headless-scripts"], ["node", "anim", "avatar", "player", "scene"]),
    (r"^irisgl/document/",
     ["document", "math", "input", "gizmo", "picking", "commands", "skeletal", "sockets",
      "cameras", "mirror", "samples", "reopen", "export", "meshbake", "*headless-scripts"],
     []),
    (r"^irisgl/core/", ["document", "math", "input", "gizmo", "cameras", "*headless-scripts"], []),
    (r"^irisgl/CMakeLists|^irisgl/cmake/|^irisgl/irisglfwd", ["*merge-tier"], []),
    # --- Studio ---------------------------------------------------------------------------
    (r"^src/scripting/modules/([a-z]+)api\.(cpp|h)$", ["api"], ["$1"]),   # $1 = module name
    (r"^src/(modules/[a-z]+/(api/)?[a-z]+api|player/api/playerapi)\.(cpp|h)$", ["api"], ["$module"]),
    (r"^src/scripting/(mcp/|claude/)", ["mcp", "claudechat", "api"], ["app"]),
    (r"^src/scripting/", ["api", "*all-scripts", "mcp"], []),
    (r"^src/services/import/", ["importer", "importasync", "assetdelete", "assetgc", "assetmeta",
                                "assetmigrate", "assetpaths", "assets", "drawers", "thumbnails",
                                "meshbake", "samples"], ["assets", "project"]),
    (r"^src/services/(asset|projectassets|thumbnail|meshbake|audiopeaks|videoutils|rigsignature|animationfile|avatarassets|fitsize)",
     ["assetdelete", "assetgc", "assetmeta", "assetmigrate", "assetpaths", "assets", "drawers",
      "thumbnails", "importer", "importasync", "export", "avatar", "reopen", "samples"],
     ["assets", "project", "avatar"]),
    (r"^src/services/(player|playback)", ["player"], ["player", "scene"]),
    (r"^src/services/(shortcut|input)", ["shortcuts", "input", "app"], ["input", "editor", "app"]),
    # sceneedit/selection/undo sit under EVERY scene.add*/node.* verb, so the headless
    # scripts (~1 min at -j4) ride along as insurance the ubiquitous-module filter removes.
    (r"^src/services/(selection|sceneedit|undo|nodenaming|outline|scenefolders|scenenodehelper)",
     ["services", "commands", "app", "input", "*headless-scripts"], ["editor", "scene", "node"]),
    (r"^src/services/(looks|worldmodes|sunlink|planarreflectors|gibounds|lightbindings|iesprofile|sceneextents)",
     ["services", "looks", "planar", "lights", "gi"], ["world", "scene", "node"]),
    (r"^src/services/(project|sceneopen|apppaths|sessionheader|sessionmarkers|jahlog|loadtimeline|perfsampler|framepacing|mainthread|engineerror|ogresamples|shutdown)",
     ["services", "app", "apppaths", "log", "perf", "shutdown", "openasync", "hygiene", "threading"],
     ["app", "project"]),
    (r"^src/services/", ["services", "*headless-scripts"], []),
    (r"^src/(data|io|commands)/", ["document", "commands", "reopen", "export", "samples", "assetpaths",
                                   "assetmigrate", "services", "*headless-scripts"], ["project", "scene", "node"]),
    (r"^src/viewport/", ["app", "input", "gizmo", "picking", "cameras", "sockets", "ui"],
     ["editor", "camera", "input"]),
    (r"^src/(bridge|player)/", ["player", "thumbnails", "materialpreview", "assets", "avatar", "app"],
     ["player", "avatar", "materials", "assets"]),
    (r"^src/modules/materials/", ["shadergraph", "pieces", "materialpreview", "ui"],
     ["materials", "material", "graph"]),
    (r"^src/modules/avatar/", ["avatar", "skeletal", "ui"], ["avatar", "anim"]),
    (r"^src/(modules/publish|export)/", ["export", "ui"], ["project", "publish"]),
    (r"^src/(ui|shell)/", ["ui", "app", "theme", "shortcuts", "desktops", "drawers"], ["editor", "app", "desktop"]),
    (r"^src/app/", ["app", "apppaths", "log", "shutdown", "hygiene", "threading", "api"], ["app"]),
    (r"^src/", ["*merge-tier"], []),
    # --- data, docs, build ---------------------------------------------------------------
    (r"^docs/SCRIPTING\.md$", ["api"], []),
    (r"^(docs/|README|LICENSE|\.claude/|\.github/|[A-Za-z_\-]+\.md$|\.gitignore$|\.gitmodules$)", [], []),   # no suite at all
    (r"^(scenes/|app/content/|app/samples/)", ["samples", "reopen", "assets"], ["project"]),
    (r"^app/", ["ui", "theme", "app"], []),
    (r"^(CMakeLists\.txt|cmake/|tests/CMakeLists\.txt)", ["*merge-tier"], []),
    (r"^scripts/", [], []),
]

# Cheap smoke suites always added when src/ or irisgl/ moved (a boot that renders + the
# contract of the scripting surface), ~15 s together.
ALWAYS_ON_CODE = ["app.startup_quiet", "api.contract"]
# Anchored: `benchmark` alone would also drop the `benchmark-smoke` rows and `shadercache`
# would drop the product-contract cache suites (code review 2026-09-10). `--timeout 120` is
# ctest's DEFAULT for the rows that set no TIMEOUT (46 of them) — a hang costs 2 min, not 25.
NIGHTLY_LABELS = {"benchmark", "shadercache-attack"}
MERGE_TIER = ('ctest -j4 --timeout 120 --output-on-failure '
              '-LE "^(benchmark|shadercache-attack)$" -E "^gi\\.ddgi_raster$"')


def sh(cmd, cwd=ROOT):
    # A failing git/ctest call must not read as "nothing touched" / "no suites": that was
    # a silently green empty gate (platform audit H4.2, 2026-09-10).
    r = subprocess.run(cmd, cwd=cwd, shell=True, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(f"gate-scope: command failed ({r.returncode}): {cmd}\n{r.stderr}")
        sys.exit(2)
    return r.stdout


def touched_paths(rng):
    """Files changed in the Studio range, plus the irisgl submodule's own diff (prefixed)."""
    files = [l for l in sh(f"git diff --name-only {rng}").splitlines() if l]
    if not files:
        sys.stderr.write(f"gate-scope: range {rng} touches no file — refusing to scope an empty change\n")
        sys.exit(2)
    out = [f for f in files if f != "irisgl"]
    if "irisgl" in files:
        base, tip = rng.split("..", 1)
        old = sh(f"git rev-parse {base}:irisgl").strip()
        new = sh(f"git rev-parse {tip}:irisgl").strip()
        sub = sh(f"git diff --name-only {old} {new}", cwd=os.path.join(ROOT, "irisgl")).splitlines()
        out += ["irisgl/" + s for s in sub if s]
    return sorted(set(out))


def load_inventory(build):
    raw = sh(f"ctest --show-only=json-v1", cwd=build)
    j = json.loads(raw)
    bt = j["backtraceGraph"]; files = bt["files"]; nodes = bt["nodes"]
    inv = {}
    for t in j["tests"]:
        n = nodes[t["backtrace"]]
        while n.get("file") is None and "parent" in n:
            n = nodes[n["parent"]]
        cm = files[n["file"]]
        d = os.path.relpath(os.path.dirname(cm), ROOT)          # tests/<dir>
        cmd = t.get("command", [])   # absent for a not-yet-built executable (partial build dir)
        props = {p["name"]: p["value"] for p in t.get("properties", [])}
        script = None
        m = re.search(r"--script\s+(\S+)", " ".join(cmd))
        if m: script = m.group(1)
        if cmd and cmd[0].endswith("bash"):
            script = cmd[1]
        inv[t["name"]] = {
            "dir": d.split("/")[-1] if d.startswith("tests") else d,
            "app": any("bin/Jahshaka" in c for c in cmd) or (cmd and cmd[0].endswith("bash")),
            "headless": "--headless" in cmd,
            "script": script,
            "serial": bool(props.get("RUN_SERIAL")),
            "labels": set(props.get("LABELS", []) or []),
        }
    return inv


TIMES_FILE = os.path.join(ROOT, "scripts", "gate-times.txt")


def record_times(log):
    """Snapshot per-suite seconds from a ctest OUTPUT log into scripts/gate-times.txt."""
    times = {}
    for line in open(log, errors="replace"):
        m = re.search(r"Test +#\d+: (\S+) .*?(?:Passed|Failed|\*\*\*\w+) +([\d.]+) sec", line)
        if m: times[m.group(1)] = float(m.group(2))
    with open(TIMES_FILE, "w") as f:
        f.write("# suite seconds — snapshot of a full gate; refresh with gate-scope.sh --record-times <ctest log>\n")
        for n in sorted(times): f.write(f"{n} {times[n]}\n")
    print(f"recorded {len(times)} suite times into {os.path.relpath(TIMES_FILE, ROOT)}")


def load_costs(build):
    """Per-suite seconds from the build dir's last ctest run (LastTest.log) — CTestCostData's
    'cost' column is ctest's scheduling weight, not seconds."""
    costs = {}
    if os.path.exists(TIMES_FILE):
        for line in open(TIMES_FILE):
            if line.startswith("#"): continue
            parts = line.split()
            if len(parts) == 2: costs[parts[0]] = float(parts[1])
    p = os.path.join(build, "Testing/Temporary/LastTest.log")
    if os.path.exists(p):
        cur = None
        for line in open(p, errors="replace"):
            m = re.match(r"\d+/\d+ Testing: (\S+)", line)
            if m: cur = m.group(1); continue
            m = re.match(r"Test time =\s+([\d.]+) sec", line)
            if m and cur: costs[cur] = float(m.group(1)); cur = None
    return costs


def script_modules(path):
    """The `<module>.` prefixes a script (js, js.in, sh) calls."""
    try: txt = open(path).read()
    except OSError: return set()
    return set(re.findall(r"\b([a-z]+)\.[a-zA-Z_]+\(", txt))


def cmake_src_refs():
    """tests/<dir> -> src/irisgl files its CMakeLists compiles."""
    refs = collections.defaultdict(set)
    tests = os.path.join(ROOT, "tests")
    for d in os.listdir(tests):
        cm = os.path.join(tests, d, "CMakeLists.txt")
        if not os.path.exists(cm): continue
        for m in re.finditer(r"\$\{CMAKE_SOURCE_DIR\}/((?:src|irisgl)/[A-Za-z0-9_/.\-]+)", open(cm).read()):
            refs[d].add(m.group(1))
    return refs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("range", nargs="?", help="git range base..tip (Studio repo)")
    ap.add_argument("--files", nargs="*", help="explicit touched paths instead of a range")
    ap.add_argument("--build", default="build-linux")
    ap.add_argument("--run", action="store_true", help="run the selection now (DISPLAY must be set)")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--record-times", metavar="CTEST_LOG", help="snapshot suite seconds from a ctest output log")
    a = ap.parse_args()
    if a.record_times:
        record_times(a.record_times); return
    build = a.build if os.path.isabs(a.build) else os.path.join(ROOT, a.build)
    if not (a.range or a.files):
        ap.error("give a range (base..tip) or --files")
    paths = a.files if a.files else touched_paths(a.range)

    inv = load_inventory(build)
    costs = load_costs(build)
    refs = cmake_src_refs()
    by_dir = collections.defaultdict(list)
    for n, t in inv.items(): by_dir[t["dir"]].append(n)
    mods = {n: script_modules(t["script"]) for n, t in inv.items() if t["script"]}
    # A module every script calls (project.create, console.log, app.*) selects nothing by
    # itself — it would turn any rule into "all scripts". Such a module still selects
    # everything when ITS api file is the touched one (rule "$1"/"$module").
    freq = collections.Counter(m for ms in mods.values() for m in ms)
    ubiquitous = {m for m, c in freq.items() if c > 0.4 * max(1, len(mods))}
    script_suites = collections.defaultdict(list)
    for n, t in inv.items():
        if t["script"]: script_suites[os.path.relpath(t["script"], ROOT)].append(n)
    # a .js.in script is configured into the build dir; map by basename too
    script_base = collections.defaultdict(list)
    for s, ns in script_suites.items(): script_base[os.path.basename(s).replace(".js", "")] += ns

    selected = {}            # suite -> reason
    skipped_ubiquitous = set()
    fallback = []            # paths that force the merge tier
    rationale = []
    code_moved = False

    def add(suites, why):
        for s in suites:
            if s in inv: selected.setdefault(s, why)

    def expand(dirs, modules, why, own_api=False):
        for d in dirs:
            if d == "*merge-tier": fallback.append(why); continue
            if d in ("*vulkan-scripts", "*headless-scripts", "*all-scripts"):
                for n, t in inv.items():
                    if not t["app"]: continue
                    if d == "*vulkan-scripts" and t["headless"]: continue
                    if d == "*headless-scripts" and not t["headless"]: continue
                    add([n], why)
                continue
            add(by_dir.get(d, []), why)
        for m in modules:
            if m in ubiquitous and not own_api:
                skipped_ubiquitous.add(m); continue
            add([n for n, ms in mods.items() if m in ms], f"{why} [module {m}]")

    for p in paths:
        hit = []
        # 1. a test file → its dir's suites / the suites running that script
        if p.startswith("tests/"):
            parts = p.split("/")
            d = parts[1] if len(parts) > 1 else ""
            base = os.path.basename(p).replace(".js.in", "").replace(".js", "").replace(".sh", "")
            if p in script_suites: add(script_suites[p], f"{p}: script"); hit.append("script")
            elif base in script_base: add(script_base[base], f"{p}: script"); hit.append("script")
            elif d in by_dir: add(by_dir[d], f"{p}: tests/{d}"); hit.append(f"tests/{d}")
            else:
                # tests/CMakeLists.txt, tests/support/*.h (included by ~40 test sources), a
                # dir with no registered suite: nothing precise owns it → merge tier, loudly
                # (platform audit H4.1: this used to select ZERO suites and continue).
                fallback.append(f"{p}: no suite owns this tests/ path")
            rationale.append((p, ", ".join(hit) or "no suite in that dir → fallback"))
            continue
        if p.startswith(("src/", "irisgl/")) and not p.startswith("irisgl/thirdparty/ogre-next"):
            code_moved = True
        # 2. a source a compiled test target names
        for d, files in refs.items():
            if p in files: add(by_dir.get(d, []), f"{p}: compiled into tests/{d}"); hit.append(f"tests/{d}")
        # 3. the area rules
        for pat, dirs, modules in AREA_RULES:
            m = re.match(pat, p)
            if not m: continue
            mm = []
            for x in modules:
                if x == "$1": mm.append(m.group(1))
                elif x == "$module": mm.append(re.sub(r"api\.(cpp|h)$", "", os.path.basename(p)))
                else: mm.append(x)
            expand(dirs, mm, f"{p}: rule {pat}", own_api=any(x in ("$1", "$module") for x in modules))
            hit.append(f"rule {pat}" + (f" modules={mm}" if mm else ""))
            break
        else:
            fallback.append(f"{p}: no rule")
        rationale.append((p, "; ".join(hit) or "NO RULE → merge tier"))
    if code_moved: add(ALWAYS_ON_CODE, "code moved: smoke + contract")

    # The nightly guards never ride a scoped gate (they are the PUSH/NIGHTLY tier's);
    # gi.ddgi_raster likewise.
    nightly = [n for n in selected if (inv[n]["labels"] & NIGHTLY_LABELS) or n == "gi.ddgi_raster"]
    for n in nightly: selected.pop(n)
    names = sorted(selected)
    est = sum(costs.get(n, 10.0) for n in names)
    serial = sum(costs.get(n, 10.0) for n in names if inv[n]["serial"])
    wall = max(est / 4.0, serial) + 5
    regex = "^(" + "|".join(re.escape(n) for n in names) + ")$"
    cmd = f"ctest -j4 --timeout 120 --output-on-failure --no-tests=error -R '{regex}'"

    if a.json:
        print(json.dumps({"paths": paths, "suites": names, "fallback": fallback, "estimated_seconds": est,
                          "estimated_wall": wall, "command": MERGE_TIER if fallback else cmd}, indent=1))
        return
    print(f"gate-scope: {len(paths)} touched path(s)")
    for p, why in rationale: print(f"  {p}\n      -> {why}")
    if fallback:
        print("\nFALLBACK → MERGE TIER (a touched path has no precise rule):")
        for f in fallback: print(f"  {f}")
        print(f"\n{MERGE_TIER}")
        if a.run: sys.exit(subprocess.call(MERGE_TIER, cwd=build, shell=True))
        return
    if skipped_ubiquitous:
        print(f"\n(modules called by >40% of scripts select nothing on their own: {sorted(skipped_ubiquitous)})")
    if nightly: print(f"\n(nightly-tier suites left out: {sorted(nightly)})")
    if not names:
        print("\nSCOPED tier: NOTHING to gate — every touched path is docs/scripts/data with no owning suite "
              "(a code path always adds app.startup_quiet + api.contract)")
        return
    print(f"\nSCOPED tier: {len(names)} suite(s), ~{est:.0f} suite-seconds, ~{wall/60:.1f} min wall at -j4 "
          f"(serial islands {serial:.0f} s); costs from scripts/gate-times.txt + the build dir's last run, 10 s assumed otherwise")
    for n in names: print(f"  {costs.get(n, 0):7.1f}  {n}   <- {selected[n]}")
    print(f"\n{cmd}")
    if a.run: sys.exit(subprocess.call(cmd, cwd=build, shell=True))


if __name__ == "__main__":
    main()
