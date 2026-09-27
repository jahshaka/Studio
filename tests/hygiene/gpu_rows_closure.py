#!/usr/bin/env python3
"""source.gpu_rows_closure — EVERY VULKAN ROW TAKES THE VRAM BUDGET (lane GATE-ADMIT-1;
docs/TESTING_GATE.md §4b).

The budget (scripts/gpu-admit.sh, 12 box-wide tokens) holds only if EVERY process that boots
Vulkan in a test takes its tokens; one unregistered row is an allocation nobody counted, and
the card over-fills exactly as it did on 2026-09-27 (47 VK_ERROR_OUT_OF_DEVICE_MEMORY reds). So
the list is closed here, on what ctest will actually RUN (`ctest --show-only=json-v1`: every
row's resolved command, environment and properties), not on what a CMakeLists says:

  A row is a VULKAN ROW when its command (any word, or a path inside a word) or its ENVIRONMENT
  names a binary that links Ogre-Next (`ldd` shows libOgreNextMain — the app, every engine
  suite), or when it runs the pool driver (tests/support/run_pool.py) —
  UNLESS it carries jah_no_display() (DISPLAY is unset for it: Ogre's Vulkan plugin cannot even
  load without an X server, so a row that passes that way boots the NULL render system or no
  engine at all — the declaration is enforced by physics), runs the app `--headless` (the
  NULL render system), or pins lavapipe (VK_ICD_FILENAMES/VK_DRIVER_FILES = lvp_icd: a CPU
  Vulkan whose "VRAM" is host memory).
  ...or when a binary it runs carries the app's path as a literal (a harness compiled with
  JAHSHAKA_BINARY that spawns the app over MCP: an app process no command line shows).
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
import subprocess
import sys

PATH_IN_WORD = re.compile(r"/[^\s\"';:=]+")
_ogre_cache = {}


def links_ogre(path):
    """True when `path` is an ELF that loads libOgreNextMain (directly or through libIrisGL)."""
    if path in _ogre_cache:
        return _ogre_cache[path]
    res = False
    try:
        if os.path.isfile(path) and os.access(path, os.X_OK):
            with open(path, "rb") as f:
                if f.read(4) == b"\x7fELF":
                    r = subprocess.run(["ldd", path], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                       timeout=30)
                    res = b"libOgreNextMain" in r.stdout
    except (OSError, subprocess.TimeoutExpired):
        res = False
    _ogre_cache[path] = res
    return res


_embeds_cache = {}


def embeds_app(path, app):
    """True when the ELF `path` carries the app's absolute path as a literal (utf-8 or a Qt
    utf-16 string): a HARNESS compiled with JAHSHAKA_BINARY that spawns the app (over MCP) —
    an app process no command line shows."""
    key = (path, app)
    if key not in _embeds_cache:
        res = False
        try:
            if os.path.isfile(path) and os.access(path, os.X_OK) and os.path.realpath(path) != os.path.realpath(app):
                with open(path, "rb") as f:
                    data = f.read()
                res = data[:4] == b"\x7fELF" and (app.encode() in data or app.encode("utf-16-le") in data)
        except OSError:
            res = False
        _embeds_cache[key] = res
    return _embeds_cache[key]


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


def classify(test, app):
    """-> (vulkan, registered, why) for one ctest row; `why` starts with "APP " when the row
    runs an app process (the app itself, a harness that spawns it, a pool)."""
    cmd = test.get("command") or []
    props = props_of(test)
    envmod = as_list(props.get("ENVIRONMENT_MODIFICATION"))
    env = as_list(props.get("ENVIRONMENT"))
    words = [str(w) for w in cmd]
    registered = any(os.path.basename(w) == "gpu-admit.sh" for w in words) or (
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
    found = None
    for w in words + [str(e) for e in env]:
        cands = [w] if w.startswith("/") else []
        cands += PATH_IN_WORD.findall(w)
        for c in cands:
            if os.path.realpath(c) == os.path.realpath(app):
                return True, registered, "APP " + os.path.basename(c)
            if embeds_app(c, app):
                return True, registered, "APP %s (spawns the app)" % os.path.basename(c)
            if found is None and links_ogre(c):
                found = os.path.basename(c)
    if found:
        return True, registered, found
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


def check(listing, app, out):
    rows = listing.get("tests") or []
    vulkan, missing, reg, exempt_ogre = 0, [], 0, 0
    undersized = []
    by_class = {}
    for t in rows:
        v, r, why = classify(t, app)
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
                    if os.path.basename(w) == "gpu-admit.sh" and i + 1 < len(words):
                        k = words[i + 1]; break
            by_class[k] = by_class.get(k, 0) + 1
    out.write("gpu_rows_closure: %d rows; %d Vulkan rows; %d registered through the VRAM budget; "
              "%d MISSING\n" % (len(rows), vulkan, reg, len(missing)))
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
    ]}
    import io
    buf = io.StringIO()
    missing = [m[0] for m in check(synthetic, app, buf)]
    want = ["synthetic.unregistered_app", "synthetic.pool_unregistered", "synthetic.app_as_engine"]
    ok = sorted(missing) == sorted(want)
    out.write("gpu_rows_closure --self-test: the detector named %s (want %s): %s\n"
              % (missing, want, "ok" if ok else "FAIL"))
    return ok


def main(argv):
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
    if selftest and not self_test(app, sys.stdout):
        fails += 1
    r = subprocess.run([args["--ctest"], "--show-only=json-v1"], cwd=args["--build"],
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if r.returncode != 0:
        print("gpu_rows_closure: ctest --show-only failed: %s" % r.stderr.decode()[-400:])
        return 1
    listing = json.loads(r.stdout)
    if check(listing, app, sys.stdout):
        fails += 1
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
