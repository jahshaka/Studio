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
    (r"^irisgl/(engine/|thirdparty/ogre-next|scripts/build-ogre)",
     # `rtreflect` is tests/rtreflect (the gi.rt_reflect family): it was MISSING
     # from this list, so no engine change ever selected the ray-traced
     # reflection suites — DRAG-1 changed what the ray arm reads from the voxels
     # and two scoped gates came back green while gi.rt_reflect was red. The
     # entries here are test DIRECTORY names, not ctest labels, so a suite whose
     # dir is not named is invisible however it is labelled.
     ["engine", "gi", "rtreflect", "lights", "looks", "distortion", "planar", "ssr", "shadow", "shadercache",
      "compute", "hdr", "sky", "pieces", "vr",
      "mirror", "cameras", "samples", "picking", "skeletal", "particles", "thumbnails",
      "materialpreview", "player", "sockets", "threading", "perf", "gizmo", "assets", "log",
      "shutdown", "openasync", "*vulkan-scripts"], []),
    (r"^irisgl/mirror/",
     ["mirror", "skeletal", "sockets", "cameras", "gizmo", "player", "thumbnails",
      "materialpreview", "samples", "picking", "particles",
      # THE MIRROR PUSHES EVERY ENVIRONMENT DESC EACH FRAME (GATE-SCOPE-2, 2026-09-17): the
      # GI/planar/shadow/post/sky/fog/ray settings and their staleness machine live in
      # scenemirror.cpp, so a mirror edit can move any of these pictures.
      "gi", "rtreflect", "lights", "looks", "planar", "ssr", "shadow", "hdr", "sky", "distortion", "vr",
      "*vulkan-scripts"], []),
    # `hygiene` rides irisgl/import, irisgl/document and irisgl/core because
    # source.bake_key_guard (BAKEKEY-1) watches files in all three: the mesh
    # bake's format version is HAND-bumped, and the lane that has to bump it is
    # exactly a lane whose scoped selection comes from these rules. Five
    # display-free shell scripts, well under a second.
    (r"^irisgl/import/",
     ["importer", "importasync", "meshbake", "avatar", "skeletal", "assetdelete", "assetgc",
      "assetmeta", "assetmigrate", "assetpaths", "assets", "samples", "thumbnails", "hygiene"],
     ["assets", "avatar", "anim"]),
    (r"^irisgl/document/(physics|animation)/",
     ["document", "skeletal", "avatar", "particles", "player", "cameras", "samples",
      "*headless-scripts"], ["node", "anim", "avatar", "player", "scene"]),
    (r"^irisgl/document/",
     # `ui` and `theme` are here because THE PANELS DISPLAY THE DOCUMENT: the
     # properties column reads a document object's values and its suites pin
     # them, so a document change can red a ui suite and this rule selected
     # none. DRAG-1 changed the unauthored material's values and
     # ui.material_panel went red at a gate that had never run it.
     ["document", "math", "input", "gizmo", "picking", "commands", "skeletal", "sockets",
      "cameras", "mirror", "samples", "reopen", "export", "meshbake", "hygiene",
      "ui", "theme", "*headless-scripts"],
     []),
    (r"^irisgl/core/",
     ["document", "math", "input", "gizmo", "cameras", "hygiene", "*headless-scripts"], []),
    (r"^irisgl/docs/", [], []),                                   # documentation: no suite at all
    (r"^irisgl/CMakeLists|^irisgl/cmake/|^irisgl/irisglfwd", ["*merge-tier"], []),
    # --- Studio ---------------------------------------------------------------------------
    (r"^src/scripting/modules/([a-z]+)api\.(cpp|h)$", ["api"], ["$1"]),   # $1 = module name
    (r"^src/(modules/[a-z]+/(api/)?[a-z]+api|player/api/playerapi)\.(cpp|h)$", ["api"], ["$module"]),
    (r"^src/scripting/(mcp/|claude/)", ["mcp", "claudechat", "api"], ["app"]),
    (r"^src/scripting/", ["api", "*all-scripts", "mcp"], []),
    (r"^src/services/import/", ["importer", "importasync", "assetdelete", "assetgc", "assetmeta",
                                "assetmigrate", "assetpaths", "assets", "drawers", "thumbnails",
                                "meshbake", "samples"], ["assets", "project"]),
    (r"^src/services/(asset|projectassets|thumbnail|meshbake|audiopeaks|videoutils|rigsignature|animationfile|avatarassets|extentmeasure)",
     ["assetdelete", "assetgc", "assetmeta", "assetmigrate", "assetpaths", "assets", "drawers",
      "thumbnails", "importer", "importasync", "export", "avatar", "reopen", "samples"],
     ["assets", "project", "avatar"]),
    (r"^src/services/(player|playback)", ["player"], ["player", "scene"]),
    (r"^src/services/(shortcut|input)", ["shortcuts", "input", "app"], ["input", "editor", "app"]),
    # sceneedit/selection/undo sit under EVERY scene.add*/node.* verb, so the headless
    # scripts (~1 min at -j4) ride along as insurance the ubiquitous-module filter removes.
    # `hygiene` rides this rule because source.one_material_resolve watches
    # exactly these files — "there is ONE resolveMaterial and ONE apply" is a
    # claim about sceneeditservice.cpp — and a lane that rewrote the apply
    # path gated without it (BUNDLE-P3, the GATE-SCOPE-2 lesson again). The
    # nine hygiene suites are display-free lints costing under a second.
    (r"^src/services/(selection|sceneedit|undo|nodenaming|outline|scenefolders|scenenodehelper)",
     ["services", "commands", "app", "input", "hygiene", "*headless-scripts"],
     ["editor", "scene", "node"]),
    (r"^src/services/(looks|worldmodes|sunlink|planarreflectors|gibounds|lightbindings|iesprofile|sceneextents)",
     ["services", "looks", "planar", "lights", "gi"], ["world", "scene", "node"]),
    (r"^src/services/(project|sceneopen|apppaths|sessionheader|sessionmarkers|jahlog|loadtimeline|perfsampler|framepacing|mainthread|engineerror|ogresamples|shutdown)",
     ["services", "app", "apppaths", "log", "perf", "shutdown", "openasync", "hygiene", "threading"],
     ["app", "project"]),
    # THE PROJECT'S VR SETTINGS TABLE (VR-WORLD-1): the panel section, both VR
    # verbs, the undo rows and the file's own keys are generated from it, so a
    # vrworld-only edit has to gate the VR suites and the panel's — the generic
    # services rule below reaches neither (the Fable read, item 7).
    (r"^src/services/vrworld", ["vr", "player", "ui", "document", "services", "reopen"],
     ["vr", "world", "player"]),
    # THE LIBRARY RESET (RESET-LIBRARY-1): it deletes the catalog, the store's
    # contents and the project folders and then re-runs the fresh-install
    # bootstrap, so its own suite, the data-root suite and the asset suites are
    # what tell you it still leaves a first launch behind — the generic
    # services rule below reaches none of them.
    (r"^src/services/libraryreset", ["libraryreset", "apppaths", "assets", "services"],
     ["app", "assets", "project"]),
    (r"^src/services/", ["services", "*headless-scripts"], []),
    (r"^src/(data|io|commands)/", ["document", "commands", "reopen", "export", "samples", "assetpaths",
                                   "assetmigrate", "services", "*headless-scripts"], ["project", "scene", "node"]),
    (r"^src/viewport/", ["app", "input", "gizmo", "picking", "cameras", "sockets", "ui",
                         # the render driver, the frame monitor host, the screenshot grades and the
                         # VR pacing live here (GATE-SCOPE-2, 2026-09-17)
                         "perf", "hdr", "vr", "player", "thumbnails"],
     ["editor", "camera", "input", "perf", "vr"]),
    (r"^src/(bridge|player)/", ["player", "thumbnails", "materialpreview", "assets", "avatar", "app"],
     ["player", "avatar", "materials", "assets"]),
    # A MATERIAL IS A BUNDLE (MATERIAL_BUNDLE_SPEC phase 1), so the module's
    # code owns the bundle MODEL's suite, the asset suites its definition is
    # stored through, and the thumbnails rendered from it — an edit to
    # core/graphdefinition.cpp used to gate without the model suite at all
    # (the GATE-SCOPE-2 lesson again).
    (r"^src/modules/materials/", ["shadergraph", "pieces", "materialpreview", "ui",
                                  "materialbundle", "assets", "assetdelete", "assetgc",
                                  "assettray", "thumbnails"],
     ["materials", "material", "graph"]),
    (r"^src/modules/avatar/", ["avatar", "skeletal", "ui"], ["avatar", "anim"]),
    (r"^src/modules/vr/", ["vr", "player", "app"], ["vr", "player"]),
    (r"^src/(modules/publish|export)/", ["export", "ui"], ["project", "publish"]),
    # …and here because source.panel_rows_guarded and source.db_pointers_initialised
    # read src/ui and src/shell (the panels' rows and their database pointers).
    (r"^src/(ui|shell)/",
     ["ui", "app", "theme", "shortcuts", "desktops", "drawers", "hygiene"],
     ["editor", "app", "desktop"]),
    (r"^src/app/", ["app", "apppaths", "log", "shutdown", "hygiene", "threading", "api"], ["app"]),
    (r"^src/", ["*merge-tier"], []),
    # --- data, docs, build ---------------------------------------------------------------
    (r"^docs/SCRIPTING\.md$", ["api"], []),
    (r"^(docs/|README|LICENSE|\.claude/|\.github/|[A-Za-z_\-]+\.md$|\.gitignore$|\.gitmodules$)", [], []),   # no suite at all
    (r"^(scenes/|app/content/|app/samples/)", ["samples", "reopen", "assets"], ["project"]),
    (r"^app/", ["ui", "theme", "app"], []),
    (r"^(CMakeLists\.txt|cmake/|tests/CMakeLists\.txt)", ["*merge-tier"], []),
    # THE TOOL'S OWN GUARD (source.gate_scope_rules): scripts/gate-scope.* is the
    # one path under scripts/ that a suite reads, so an edit to it selects the
    # hygiene rows (seven display-free shell/python lints, well under a second
    # together). Without this line an edit to the scoping tool selected NOTHING
    # and its own guard never ran — the rule class GATE-SCOPE-2 audited.
    (r"^scripts/gate-scope", ["hygiene"], []),
    (r"^scripts/", [], []),
]

# Cheap smoke suites always added when src/ or irisgl/ moved (a boot that renders + the
# contract of the scripting surface), ~15 s together.
ALWAYS_ON_CODE = ["app.startup_quiet", "api.contract"]
# Anchored: `benchmark` alone would also drop the `benchmark-smoke` rows and `shadercache`
# would drop the product-contract cache suites (code review 2026-09-10). `--timeout 120` is
# ctest's DEFAULT for the rows that set no TIMEOUT (46 of them) — a hang costs 2 min, not 25.
NIGHTLY_LABELS = {"benchmark", "shadercache-attack"}

# TARGET TESTS (PHOTON phase A, A1 §0; the label's ONE definition lives here).
#
# A target test states the CORRECT number for a term the renderer gets wrong
# today, and it cannot pass until the PART named in its header lands. It is not
# a disabled test and it is not a bracket around an artefact: it RUNS in every
# scoped selection and PRINTS its value ("target: <value> (bar <bar>)"), so the
# distance to the bar is visible on every lane and a target that goes green
# early is noticed instead of sitting red for a month. What it does not do is
# decide a gate: the lane that fixes the term DELETES the label from the
# suite's CMake, and that deletion IS the part's acceptance.
#
# So the exclusion is from PASS/FAIL, never from the run. gate-scope runs the
# gating suites first (their exit code is the gate's), then the target suites in
# a second ctest invocation whose result is reported and discarded. The MERGE
# and PUSH tiers drop them with -LE, which is the only shape ctest offers for
# "do not let these decide the tier"; their values are read from a lane's scoped
# run, where they are printed.
TARGET_LABELS = {"photon-target"}

NIGHTLY_LABEL_RE = "|".join(sorted(re.escape(l) for l in NIGHTLY_LABELS | TARGET_LABELS))
MERGE_TIER = ('ctest -j4 --timeout 120 --output-on-failure '
              f'-LE "^({NIGHTLY_LABEL_RE})$" -E "^gi\\.ddgi_raster$"')


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


def resolve_build(arg):
    """WHERE THE BUILD DIR IS, and the trap this function exists to close.

    `ctest --show-only=json-v1` in a directory that is NOT a configured build
    dir prints an EMPTY inventory and exits 0 (measured: the Studio repo root
    does exactly this). Every suite name a rule produces is then filtered out
    by `add()` — it only keeps names the inventory knows — so the selection came
    out empty and the tool said "NOTHING to gate ... docs/scripts/data" and
    exited 0. A SILENT EMPTY GATE, with the rationale lines above it listing
    sixteen test dirs it had just decided to skip (ledger §620 item 5, found by
    SQUARE-1 the hard way: `gate-scope.py <range> --build . --run` run from
    INSIDE the build dir, where `--build .` was resolved against the repo root).

    Two halves, and both are needed. This one makes `--build .` from inside the
    build dir MEAN the build dir: the argument is tried against the CWD first
    and then against the repo root (the documented form, `--build build-linux`
    from the tree top), and the first candidate that actually carries a
    CTestTestfile.cmake wins. `load_inventory` below is the other half: a build
    dir that registers no suite is now an error instead of an empty answer.
    """
    cands = [arg] if os.path.isabs(arg) else [os.path.abspath(arg), os.path.join(ROOT, arg)]
    for c in cands:
        if os.path.isfile(os.path.join(c, "CTestTestfile.cmake")): return c
    return cands[-1]          # nothing configured: keep the documented resolution for the error


def load_inventory(build):
    raw = sh(f"ctest --show-only=json-v1", cwd=build)
    j = json.loads(raw)
    if not j.get("tests"):
        # See resolve_build: an unconfigured directory answers with an empty
        # inventory and a zero exit status, and an empty inventory selects
        # nothing however many rules fired.
        sys.stderr.write(
            f"gate-scope: {build} registers no ctest suite — that is not a configured build "
            f"dir.\n            Point --build at one (an absolute path, a path relative to "
            f"the repo root, or\n            '.' from inside the build dir itself). Refusing "
            f"to scope against an empty inventory.\n")
        sys.exit(2)
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


# A build-registration edit that only adds or removes SOURCE FILES from a list (a new
# .cpp beside its header, a deleted dead file) says nothing on its own: the files it
# names are in the same diff and scope precisely. Such an edit no longer forces the
# merge tier (2026-09-11, owner: "don't run the full gate twice" — seven lanes fell back
# to MERGE in one day for exactly this). Anything else in the file (a flag, a target,
# a find_package, a condition) still falls back, loudly.
_LIST_ENTRY = re.compile(r'^"?[\w${}./+\-]+\.(?:cpp|cc|cxx|c|h|hh|hpp|ui|qrc|mm|js|js\.in|sh)"?\)?$')
# A one-line `add_subdirectory(<dir>)` in tests/CMakeLists.txt REGISTERS a suite whose own
# files are in the same diff and scope it precisely (2026-09-13 audit: three of the last
# five lanes ran the full MERGE tier for exactly this one line).
_SUBDIR_ENTRY = re.compile(r'^add_subdirectory\(\s*[\w\-]+\s*\)$')


def _changed_lines(diff_text):
    for line in diff_text.splitlines():
        if line.startswith(("+++", "---")): continue
        if line.startswith(("+", "-")): yield line[1:].strip()


def cmake_list_only(rng, relpath):
    """True when every changed line of `relpath` in `rng` is a source-file list entry,
    a comment or blank (and at least one line changed)."""
    if not rng or ".." not in rng: return False
    base, tip = rng.split("..", 1)
    if relpath.startswith("irisgl/"):
        old = sh(f"git rev-parse {base}:irisgl").strip()
        new = sh(f"git rev-parse {tip}:irisgl").strip()
        text = sh(f"git diff -U0 {old} {new} -- {relpath[len('irisgl/'):]}",
                  cwd=os.path.join(ROOT, "irisgl"))
    else:
        text = sh(f"git diff -U0 {rng} -- {relpath}")
    lines = [l for l in _changed_lines(text)]
    if not lines: return False
    subdir_ok = relpath == "tests/CMakeLists.txt"
    return all(l == "" or l.startswith("#") or _LIST_ENTRY.match(l)
               or (subdir_ok and _SUBDIR_ENTRY.match(l)) for l in lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("range", nargs="?", help="git range base..tip (Studio repo)")
    ap.add_argument("--files", nargs="*", help="explicit touched paths instead of a range")
    ap.add_argument("--build", default="build-linux")
    ap.add_argument("--run", action="store_true", help="run the selection now (DISPLAY must be set)")
    ap.add_argument("-j", "--jobs", type=int, default=4,
                    help="ctest parallelism (default 4 — the tier's contract; a lane beside other live lanes runs 2)")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--record-times", metavar="CTEST_LOG", help="snapshot suite seconds from a ctest output log")
    a = ap.parse_args()
    if a.record_times:
        record_times(a.record_times); return
    build = resolve_build(a.build)
    if not (a.range or a.files):
        ap.error("give a range (base..tip) or --files")
    paths = a.files if a.files else touched_paths(a.range)
    list_only = {p for p in paths
                 if p in ("CMakeLists.txt", "irisgl/CMakeLists.txt", "tests/CMakeLists.txt")
                 and cmake_list_only(a.range, p)}

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
        if p in list_only:
            rationale.append((p, "source-list edit only → scoped by the files it names"))
            code_moved = True
            continue
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
        # A pin bump of irisgl/thirdparty/ogre-next COUNTS as code moving (2026-09-22): since
        # the fork, that gitlink is how every engine change — C++ and media alike — arrives.
        if p.startswith(("src/", "irisgl/")):
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

    # The nightly guards never ride a scoped gate (they are the PUSH/NIGHTLY tier's).
    # (gi.ddgi_raster used to be named here beside them; the suite was deleted with the
    # irradiance field's rasterised probe source on 2026-09-17, lane FIELD-RASTER-CRUD.)
    nightly = [n for n in selected if inv[n]["labels"] & NIGHTLY_LABELS]
    for n in nightly: selected.pop(n)
    # TARGET TESTS ARE SPLIT OUT, NOT DROPPED (see TARGET_LABELS): they run, they
    # print their value, and their exit code is not the gate's.
    targets = sorted(n for n in selected if inv[n]["labels"] & TARGET_LABELS)
    selected_targets = {n: selected.pop(n) for n in targets}
    names = sorted(selected)
    est = sum(costs.get(n, 10.0) for n in names)
    serial = sum(costs.get(n, 10.0) for n in names if inv[n]["serial"])
    wall = max(est / float(a.jobs), serial) + 5

    def ctest_for(suites, jobs):
        rx = "^(" + "|".join(re.escape(n) for n in suites) + ")$"
        return f"ctest -j{jobs} --timeout 120 --output-on-failure --no-tests=error -R '{rx}'"

    cmd = ctest_for(names, a.jobs) if names else ""
    # The targets run SERIALLY (-j1): they are measurements, and a measurement
    # sharing the GPU with three siblings prints a number nobody can use.
    target_cmd = ctest_for(targets, 1) if targets else ""

    if a.json:
        print(json.dumps({"paths": paths, "suites": names, "targets": targets, "fallback": fallback,
                          "estimated_seconds": est, "estimated_wall": wall,
                          "command": MERGE_TIER if fallback else cmd,
                          "target_command": target_cmd}, indent=1))
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
    if targets:
        print(f"\nTARGET TESTS (label {'/'.join(sorted(TARGET_LABELS))}) — they RUN and PRINT their "
              f"value, and they do NOT decide this gate:")
        for n in targets: print(f"  {costs.get(n, 0):7.1f}  {n}   <- {selected_targets[n]}")
    if not names and not targets:
        # THE ONLY HONEST EMPTY SELECTION is a change that moved no code: docs,
        # a spec, a data file, a scratch script. A change that DID move code
        # cannot select nothing, because ALWAYS_ON_CODE adds the smoke pair to
        # every code path — so an empty selection there is a defect in this
        # tool (or an inventory that does not know the suites the rules name),
        # and it exits non-zero rather than reading as a green gate. That is
        # the second half of the trap resolve_build documents: the first half
        # made `--build .` work, this one makes a wrong answer loud.
        if code_moved:
            sys.stderr.write(
                "gate-scope: a code path selected NO suite — the rules fired (see the rationale "
                "above) but\n            named nothing this build dir registers. That is a rule "
                "or inventory defect, not\n            an empty change; refusing to report a "
                "green empty gate.\n")
            sys.exit(3)
        print("\nSCOPED tier: NOTHING to gate — every touched path is docs/scripts/data with no owning suite "
              "(a code path always adds app.startup_quiet + api.contract)")
        return
    if names:
        print(f"\nSCOPED tier: {len(names)} suite(s), ~{est:.0f} suite-seconds, ~{wall/60:.1f} min wall at -j{a.jobs} "
              f"(serial islands {serial:.0f} s); costs from scripts/gate-times.txt + the build dir's last run, 10 s assumed otherwise")
        for n in names: print(f"  {costs.get(n, 0):7.1f}  {n}   <- {selected[n]}")
        print(f"\n{cmd}")
    else:
        print("\nSCOPED tier: every selected suite is a TARGET test — this change gates on nothing "
              "of its own, and the targets below still run and report.")
    if target_cmd: print(f"\n{target_cmd}    # target tests: reported, NOT gating")
    if a.run:
        rc = subprocess.call(cmd, cwd=build, shell=True) if cmd else 0
        if target_cmd:
            # THE TARGETS' RUN IS A REPORT. Its exit code is printed and thrown
            # away: a target is red until its part lands, and a lane that had to
            # go green on one could not merge anything.
            print("\n=== target tests (label %s): reported, not gating ==="
                  % "/".join(sorted(TARGET_LABELS)))
            trc = subprocess.call(target_cmd, cwd=build, shell=True)
            print("=== target tests exited %d — NOT part of this gate's verdict ===" % trc)
        sys.exit(rc)


if __name__ == "__main__":
    main()
