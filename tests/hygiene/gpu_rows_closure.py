#!/usr/bin/env python3
"""source.gpu_rows_closure — EVERY VULKAN ROW TAKES THE VRAM BUDGET (lane GATE-ADMIT-1;
docs/TESTING_GATE.md §4b).

The budget (scripts/gpu-admit.sh, 11 box-wide tokens) holds only if EVERY process that boots
Vulkan in a test takes its tokens; one unregistered row is an allocation nobody counted, and
the card over-fills exactly as it did on 2026-09-27 (47 VK_ERROR_OUT_OF_DEVICE_MEMORY reds). So
the list is closed here, on what ctest will actually RUN (`ctest --show-only=json-v1`: every
row's resolved command, environment and properties), not on what a CMakeLists says:

  A row is a VULKAN ROW when its command (any word, or a path inside a word) or its ENVIRONMENT
  names a binary that links Ogre-Next (its DT_NEEDED closure holds libOgreNextMain — the app,
  every engine suite), or when it runs the pool driver (tests/support/run_pool.py) —
  UNLESS it carries jah_no_display() (DISPLAY is unset for it: Ogre's Vulkan plugin cannot even
  load without an X server, so a row that passes that way boots the NULL render system or no
  engine at all — the declaration is enforced by physics), runs the app `--headless` (the
  NULL render system), or pins lavapipe (VK_ICD_FILENAMES/VK_DRIVER_FILES = lvp_icd: a CPU
  Vulkan whose "VRAM" is host memory).
  ...or when a binary it runs is an APP-SPAWNING HARNESS: a target compiled with
  JAHSHAKA_BINARY naming the app (it spawns the app over MCP — an app process no command line
  shows). Those targets come FROM THE BUILD'S OWN RECORD of how it compiles them
  (`<build>/compile_commands.json`, which every tree's configure exports: each compile line that
  defines JAHSHAKA_BINARY as the app names its target's object directory,
  `CMakeFiles/<target>.dir/`), never from the binaries' bytes.

  THE COST IS THE HEADERS (STALL-ROW-1, TESTING-CLEANUP-2): "links Ogre" is the DT_NEEDED closure
  read from each ELF's DYNAMIC SECTION (the ELF header, the program headers and the dynamic
  segment — a few KB), resolved through the binary's own RUNPATH; no `ldd` (which maps every
  library of the closure) and no whole-file read. The row used to read all ~370 ELFs a listing
  names whole and ldd each — 19.7 GB a run: 10 s on a warm page cache, 120 s (its TIMEOUT) when
  lane builds had evicted it, with nothing printed (22 TIMEOUT + 5 FAIL in 252 runs). Output is
  line-buffered now, so a killed row still shows what it printed.
  A row is REGISTERED when its command runs scripts/gpu-admit.sh (jah_gpu_row,
  jah_gpu_exclusive_test) or the pool driver with `--vram-tokens` (jah_add_pool).
  THE CHECK: Vulkan rows minus registered rows = EMPTY, and every row that runs an APP process
  (the app, a harness that spawns it) declared an app-sized class (app/selftest/vr, read from
  its `--label <row>:<class>`). Each offender is named with the CMakeLists line that registered it.

--self-test runs the detector on a SYNTHETIC ctest listing (an unregistered app row, a
registered one, a no-display one) and fails unless exactly the unregistered row is named — so a
detector that silently finds nothing cannot pass as a closure.

Usage: gpu_rows_closure.py --build <dir> --ctest <ctest> --app <Jahshaka> [--self-test]
"""
import json
import os
import re
import struct
import subprocess
import sys

PATH_IN_WORD = re.compile(r"/[^\s\"';:=]+")

# ---- the ELF dynamic section (the headers only) ------------------------------------------------
DT_NULL, DT_NEEDED, DT_STRTAB, DT_STRSZ, DT_RPATH, DT_RUNPATH = 0, 1, 5, 10, 15, 29
PT_LOAD, PT_DYNAMIC = 1, 2


def elf_dynamic(path):
    """-> (needed names, runpath dirs with $ORIGIN expanded) of the ELF at `path`, or None when it
    is not an ELF. Reads the ELF header, the program headers, the dynamic segment and the string
    table it names — never the whole file."""
    try:
        with open(path, "rb") as f:
            ident = f.read(64)
            if len(ident) < 52 or ident[:4] != b"\x7fELF":
                return None
            is64, e = ident[4] == 2, ("<" if ident[5] == 1 else ">")
            if is64:
                phoff, = struct.unpack_from(e + "Q", ident, 32)
                phentsize, phnum = struct.unpack_from(e + "HH", ident, 54)
            else:
                phoff, = struct.unpack_from(e + "I", ident, 28)
                phentsize, phnum = struct.unpack_from(e + "HH", ident, 42)
            f.seek(phoff)
            ph = f.read(phentsize * phnum)
            loads, dyn = [], None
            for i in range(phnum):
                if is64:
                    p_type, _fl, p_off, p_vaddr, _pa, p_filesz = struct.unpack_from(e + "IIQQQQ", ph, i * phentsize)
                else:
                    p_type, p_off, p_vaddr, _pa, p_filesz = struct.unpack_from(e + "IIIII", ph, i * phentsize)
                if p_type == PT_LOAD:
                    loads.append((p_vaddr, p_off, p_filesz))
                elif p_type == PT_DYNAMIC:
                    dyn = (p_off, p_filesz)
            if dyn is None:
                return [], []                      # static: needs nothing
            f.seek(dyn[0])
            d = f.read(dyn[1])
            size, fmt = (16, e + "qQ") if is64 else (8, e + "iI")
            needed, paths, strtab, strsz = [], [], None, 0
            for off in range(0, len(d) - size + 1, size):
                tag, val = struct.unpack_from(fmt, d, off)
                if tag == DT_NULL:
                    break
                if tag == DT_NEEDED:
                    needed.append(val)
                elif tag in (DT_RPATH, DT_RUNPATH):
                    paths.append(val)
                elif tag == DT_STRTAB:
                    strtab = val
                elif tag == DT_STRSZ:
                    strsz = val
            if strtab is None:
                return [], []
            stroff = next((strtab - va + off for va, off, sz in loads if va <= strtab < va + sz), None)
            if stroff is None:
                return [], []
            f.seek(stroff)
            strings = f.read(strsz)
    except (OSError, struct.error):
        return None

    def at(o):
        z = strings.find(b"\0", o)
        return strings[o:z if z >= 0 else len(strings)].decode("utf-8", "replace")
    origin = os.path.dirname(os.path.realpath(path))
    dirs = []
    for o in paths:
        for dd in at(o).split(":"):
            if dd:
                dirs.append(dd.replace("$ORIGIN", origin).replace("${ORIGIN}", origin))
    return [at(o) for o in needed], dirs


_ogre_cache = {}


def links_ogre(path, _depth=0):
    """True when `path` is an ELF whose DT_NEEDED closure holds libOgreNextMain (directly or through
    libIrisGL / the engine library). Each library is resolved through the RUNPATH of the object that
    needs it; a name found in none of them is a system library — none of those leads to Ogre."""
    try:
        key = os.path.realpath(path)
    except OSError:
        return False
    if key in _ogre_cache:
        return _ogre_cache[key]
    _ogre_cache[key] = False                       # a cycle reads as "not through here"
    res = False
    if _depth < 16 and os.path.isfile(key) and (_depth > 0 or os.access(key, os.X_OK)):
        dyn = elf_dynamic(key)
        if dyn is not None:
            needed, dirs = dyn
            for name in needed:
                if name.startswith("libOgreNextMain"):
                    res = True
                    break
                for dd in dirs:
                    cand = os.path.join(dd, name)
                    if os.path.isfile(cand):
                        if links_ogre(cand, _depth + 1):
                            res = True
                        break
                if res:
                    break
    _ogre_cache[key] = res
    return res


_HARNESS_OBJ = re.compile(r"CMakeFiles/([^/\s]+)\.dir/")


def read_harnesses(build, app):
    """The app-spawning harness TARGET NAMES: every target whose compile line in
    <build>/compile_commands.json defines JAHSHAKA_BINARY as the app's path. A target name is
    unique in a CMake project, so a row's binary is a harness when its file name is one of them."""
    with open(os.path.join(build, "compile_commands.json")) as f:
        entries = json.load(f)
    want = os.path.realpath(app)
    names = set()
    for e in entries:
        cmd = e.get("command") or " ".join(e.get("arguments") or [])
        at = cmd.find("-DJAHSHAKA_BINARY=")
        if at < 0:
            continue
        value = cmd[at + len("-DJAHSHAKA_BINARY="):].split(" ", 1)[0].strip("\\\"'")
        if os.path.realpath(value) != want:
            continue                        # e.g. tests/tooling's /bin/false harness
        m = _HARNESS_OBJ.search(e.get("output") or cmd)
        if m:
            names.add(m.group(1))
    return names


APP_CLASSES = ("app", "selftest", "vr")


def admitted_class(words):
    """The class a gpu-admit.sh row declared (its `--label <row>:<class>`), else None."""
    if "--label" in words:
        lab = words[words.index("--label") + 1]
        if ":" in lab:
            return lab.rsplit(":", 1)[1]
    return None


def props_of(test):
    return {p["name"]: p["value"] for p in test.get("properties", [])}


def as_list(v):
    if v is None:
        return []
    return v if isinstance(v, list) else [v]


def classify(test, app, harnesses):
    """-> (vulkan, registered, why) for one ctest row; `why` starts with "APP " when the row
    runs an app process (the app itself, a harness that spawns it, a pool)."""
    cmd = test.get("command") or []
    props = props_of(test)
    envmod = as_list(props.get("ENVIRONMENT_MODIFICATION"))
    env = as_list(props.get("ENVIRONMENT"))
    words = [str(w) for w in cmd]
    # gpu-exclusive.sh IS the admission too (TEST-SELECTOR-1 G1+G2): a timing row takes ALL tokens
    registered = any(os.path.basename(w) in ("gpu-admit.sh", "gpu-exclusive.sh") for w in words) or (
        any(os.path.basename(w) == "run_pool.py" for w in words) and "--vram-tokens" in words)
    nodisplay = any(re.match(r"^DISPLAY=unset:", e) for e in envmod)
    if nodisplay:
        return False, registered, "jah_no_display"
    if "--headless" in words and (app in words):
        return False, registered, "the app --headless"
    if any(re.match(r"^VK_(ICD_FILENAMES|DRIVER_FILES)=.*lvp_icd", str(e)) for e in env):
        return False, registered, "lavapipe (CPU Vulkan: host memory, no VRAM)"
    if any(os.path.basename(w) == "run_pool.py" for w in words):
        return True, registered, "APP the pool driver"
    cands = []
    for w in words + [str(e) for e in env]:
        cands += [w] if w.startswith("/") else []
        cands += PATH_IN_WORD.findall(w)
    for c in cands:
        if os.path.realpath(c) == os.path.realpath(app):
            return True, registered, "APP " + os.path.basename(c)
        if os.path.basename(c) in harnesses:
            return True, registered, "APP %s (spawns the app)" % os.path.basename(c)
    # A REGISTERED row is admitted whatever it boots, so its binaries are not read (STALL-ROW-1:
    # only an UNREGISTERED row's binaries are, the one question the closure asks of them).
    if registered:
        return True, registered, "registered"
    for c in cands:
        if links_ogre(c):
            return True, registered, os.path.basename(c)
    return False, registered, "no engine binary"


def site(test, listing):
    """The CMakeLists line that registered the row (the backtrace's innermost frame that is not
    a helper's add_test)."""
    bt = test.get("backtrace")
    graph = listing.get("backtraceGraph") or {}
    nodes = graph.get("nodes") or []
    files = graph.get("files") or []
    chain = []
    while isinstance(bt, int) and 0 <= bt < len(nodes):
        n = nodes[bt]
        if "file" in n and "line" in n:
            chain.append("%s:%d" % (os.path.relpath(files[n["file"]]) if n["file"] < len(files) else "?", n["line"]))
        bt = n.get("parent")
    return chain[-2] if len(chain) >= 2 else (chain[0] if chain else "?")


def check(listing, app, harnesses, out):
    rows = listing.get("tests") or []
    vulkan, missing, reg, exempt_ogre = 0, [], 0, 0
    undersized = []
    by_class = {}
    for t in rows:
        v, r, why = classify(t, app, harnesses)
        words = [str(w) for w in (t.get("command") or [])]
        if v:
            vulkan += 1
            if r:
                reg += 1
                cls = admitted_class(words)
                if why.startswith("APP ") and cls is not None and cls not in APP_CLASSES:
                    undersized.append((t["name"], why[4:], cls, site(t, listing)))
            else:
                missing.append((t["name"], why, site(t, listing)))
        if v and r:
            k = None
            if "--vram-tokens" in words:
                k = words[words.index("--vram-tokens") + 1]
            else:
                for i, w in enumerate(words):
                    if os.path.basename(w) == "gpu-exclusive.sh":
                        k = "all"; break
                    if os.path.basename(w) == "gpu-admit.sh" and i + 1 < len(words):
                        k = words[i + 1]; break
            by_class[k] = by_class.get(k, 0) + 1
    out.write("gpu_rows_closure: %d rows; %d registered through the VRAM budget; %d unregistered "
              "row(s) boot Vulkan = MISSING\n" % (len(rows), reg, len(missing)))
    out.write("gpu_rows_closure: registered rows by tokens: %s\n"
              % ", ".join("%s token(s): %d" % (k, n) for k, n in sorted(by_class.items(), key=lambda x: str(x[0]))))
    for name, why, where in missing:
        out.write("  MISSING %s (runs %s) — registered at %s: use jah_gpu_row(%s CLASS …) or, if it "
                  "boots no Vulkan, jah_no_display(%s)\n" % (name, why, where, name, name))
    out.write("gpu_rows_closure: %d app-spawning row(s) registered under a non-app class\n" % len(undersized))
    for name, why, cls, where in undersized:
        out.write("  UNDERSIZED %s runs %s but declared CLASS %s — registered at %s: an app process is "
                  "CLASS app (or selftest/vr)\n" % (name, why, cls, where))
    return missing + undersized


def self_test(app, out):
    synthetic = {"tests": [
        {"name": "synthetic.unregistered_app", "command": [app, "--script", "/nonexistent.js"]},
        {"name": "synthetic.registered_app", "command": ["/x/scripts/gpu-admit.sh", "2", "--label", "r", "--", app]},
        {"name": "synthetic.no_display", "command": [app, "--dump-api-docs"],
         "properties": [{"name": "ENVIRONMENT_MODIFICATION", "value": ["DISPLAY=unset:"]}]},
        {"name": "synthetic.pool_unregistered", "command": ["/usr/bin/sh", "/x/fresh_home.sh", "--", "/usr/bin/python3",
                                                            "/x/tests/support/run_pool.py", "--pool", "p", "--app", app]},
        {"name": "synthetic.pool_registered", "command": ["/usr/bin/python3", "/x/tests/support/run_pool.py",
                                                          "--vram-tokens", "2", "--app", app]},
        {"name": "synthetic.shell_only", "command": ["/usr/bin/sh", "-c", "true"]},
        {"name": "synthetic.app_as_engine", "command": ["/x/scripts/gpu-admit.sh", "1", "--label",
                                                        "synthetic.app_as_engine:engine", "--", app, "--script", "x.js"]},
        # a HARNESS (a CMake-listed JAHSHAKA_BINARY target) spawns the app: unregistered, and
        # registered under an engine-sized class — both named
        {"name": "synthetic.harness_unregistered", "command": ["/x/tests/ui/test_synthetic_harness"]},
        {"name": "synthetic.harness_as_engine", "command": ["/x/scripts/gpu-admit.sh", "1", "--label",
                                                            "synthetic.harness_as_engine:engine", "--",
                                                            "/x/tests/ui/test_synthetic_harness"]},
    ]}
    # an UNREGISTERED row whose binary links Ogre through its DT_NEEDED closure (not the app, not a
    # harness): the ELF-header path itself. The engine library the app loads stands in for it.
    engine_lib = None
    dyn = elf_dynamic(app) or ([], [])
    for name in dyn[0]:
        for dd in dyn[1]:
            cand = os.path.join(dd, name)
            if os.path.isfile(cand) and not name.startswith("libOgreNextMain") and links_ogre(cand, 1):
                engine_lib = cand
                break
        if engine_lib:
            break
    if engine_lib:
        synthetic["tests"].append({"name": "synthetic.unregistered_engine", "command": [engine_lib]})
    import io
    buf = io.StringIO()
    missing = [m[0] for m in check(synthetic, app, {"test_synthetic_harness"}, buf)]
    want = ["synthetic.unregistered_app", "synthetic.pool_unregistered", "synthetic.app_as_engine",
            "synthetic.harness_unregistered", "synthetic.harness_as_engine"] + (
            ["synthetic.unregistered_engine"] if engine_lib else [])
    if not engine_lib:
        out.write("gpu_rows_closure --self-test: FAIL: no engine library in the app's DT_NEEDED closure "
                  "links Ogre — the ELF reader is broken\n")
        return False
    ok = sorted(missing) == sorted(want)
    out.write("gpu_rows_closure --self-test: the detector named %s (want %s): %s\n"
              % (missing, want, "ok" if ok else "FAIL"))
    return ok


def main(argv):
    sys.stdout.reconfigure(line_buffering=True)   # a killed row still shows what it printed
    args = {"--build": None, "--ctest": "ctest", "--app": None}
    selftest = False
    i = 0
    while i < len(argv):
        if argv[i] == "--self-test":
            selftest = True; i += 1
        elif argv[i] in args and i + 1 < len(argv):
            args[argv[i]] = argv[i + 1]; i += 2
        else:
            sys.stderr.write(__doc__); return 64
    if not args["--build"] or not args["--app"]:
        sys.stderr.write(__doc__); return 64
    app = args["--app"]
    if not links_ogre(app):
        print("gpu_rows_closure: the app %s does not exist or links no Ogre — build it first" % app)
        return 1
    fails = 0
    # THE HARNESSES ARE THE BUILD'S RECORD: an empty set would let every harness row pass as
    # "no engine binary", so an unreadable or empty record is a failure, never a pass.
    try:
        harnesses = read_harnesses(args["--build"], app)
    except (OSError, ValueError) as e:
        print("gpu_rows_closure: FAIL: %s/compile_commands.json cannot be read (%s) — configure with "
              "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON" % (args["--build"], e))
        return 1
    print("gpu_rows_closure: %d app-spawning harness target(s) compiled with JAHSHAKA_BINARY = the app"
          % len(harnesses))
    if not harnesses:
        print("gpu_rows_closure: FAIL: no target is compiled with JAHSHAKA_BINARY naming the app — "
              "the record is broken, not empty")
        fails += 1
    if selftest and not self_test(app, sys.stdout):
        fails += 1
    r = subprocess.run([args["--ctest"], "--show-only=json-v1"], cwd=args["--build"],
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if r.returncode != 0:
        print("gpu_rows_closure: ctest --show-only failed: %s" % r.stderr.decode()[-400:])
        return 1
    listing = json.loads(r.stdout)
    if check(listing, app, harnesses, sys.stdout):
        fails += 1
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
