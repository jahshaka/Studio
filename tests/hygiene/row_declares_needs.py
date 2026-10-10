#!/usr/bin/env python3
"""source.row_declares_needs — EVERY ROW THAT STARTS THE APP DECLARES WHAT IT BOOTS (lane
TEST-NEEDS-1; docs/TESTING_GATE.md §4b; owner 2026-10-09: "a test suite passes variables for what
it needs; if you don't need Photon, run Low").

A process with no test tier honours the DOCUMENT's tier — Epic, the whole chain, ~1.5-3 GB — so a
row that declares nothing boots a picture its claim never named. The declaration is two words on
the registration (jah_gpu_row / jah_gpu_exclusive_test CLASS app|vr, jah_add_pool; the lookup and
the vocabulary are tests/support/vram_tokens.cmake): `TIER <low|medium|high|epic>` and
`NEEDS <photon bloom ssao smaa planar…>|NONE`, which the helpers put on the row as
ENVIRONMENT_MODIFICATION `JAHSHAKA_TEST_TIER=set:<tier>` / `JAHSHAKA_TEST_NEEDS=set:<list|none>`
— or, for `TIER document` (no test tier: the process honours each scene's own tier, for a claim
about the document's World state), both `unset:`.

Read on what ctest will actually RUN (`ctest --show-only=json-v1`), like source.gpu_rows_closure,
whose detector this reuses: every VULKAN row that runs an APP process (the app, a harness that
spawns it, an engine-up pool) and is not CLASS selftest (`--engine-selftest` binds no editor
scene) must carry BOTH entries with a valid value; no row may type JAHSHAKA_TEST_TIER /
JAHSHAKA_TEST_NEEDS into its plain ENVIRONMENT or pass `--test-tier` on its command line (the
declaration is the one place). A headless or no-display row boots no chain and declares nothing.
And NONE MEANS NONE: a row declaring it whose scripts read the picture (PIXEL_VERBS / PIXEL_TOOLS)
or drive Photon (GI_VERBS) is refused — it must name what it needs, unless its claim IS the
picture with every feature off and it says so: `NEEDS_WHY "<the claim>"` (lead 2026-10-09), which
rides as JAHSHAKA_TEST_NEEDS_WHY and is printed in the lint's record. (A harness compiled from C++
is read through its source, tests/**/<binary>.cpp, found by the binary's name.)

--self-test runs the check on a SYNTHETIC listing (a declared app row, an undeclared one, a
hand-typed one, a bad word, a declared pool, an undeclared pool, a selftest row, a headless row)
and fails unless exactly the wrong rows are named — a toy row without the words is red.

RED ON BASE (a5ab3a057): 0 of the 122 app rows carried a declaration — every one is named.

Usage: row_declares_needs.py --build <dir> --ctest <ctest> --app <Jahshaka> [--self-test]
"""
import io
import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gpu_rows_closure as closure  # noqa: E402

import re

TIERS = ("low", "medium", "high", "epic")
# THE MEANING CHECK (lead's addendum): `NONE` says the row reads no switchable feature, so a row
# whose scripts READ THE PICTURE or DRIVE PHOTON cannot say it. The verbs grepped for, in every
# script the row runs (its command's .js/.js.in/.sh/.py words, the .js a harness script names,
# every pool arm's script):
PIXEL_VERBS = ("editor.screenshot(", "player.screenshot(", "camera.screenshot(", "vr.eyeScreenshot(",
               "editor.presentedFrame(", "capture.lastFrame(")
PIXEL_TOOLS = ("xwd", "--engine-selftest", "readPixels")     # a harness reading the screen / the hashes
GI_VERBS = ("world.photon(", "world.gi(", "world.setPhotonView(", "world.giStatus(", "world.giVoxelStats(",
            "world.mode(", "world.override(", "world.setPlanarReflections(", "world.postFx(",
            "world.refreshGi(", "world.photonView(")
MCP_PIXEL = '"screenshot"'   # the MCP server's screenshot TOOL, named by a C++ harness
SCRIPT_RX = re.compile(r"\.(js|js\.in|sh|py)$")
NAMED_JS = re.compile(r"[\w./${}@-]+\.js(?:\.in)?")
UNSET = "<document>"   # `TIER document`: both variables `unset:` — the process honours each scene's own tier
WORDS = ("photon", "bloom", "ssao", "smaa", "planar")


def declared(props):
    """-> {var: value} of the `set:` declaration entries in ENVIRONMENT_MODIFICATION."""
    out = {}
    for e in closure.as_list(props.get("ENVIRONMENT_MODIFICATION")):
        e = str(e)
        for var in ("JAHSHAKA_TEST_TIER", "JAHSHAKA_TEST_NEEDS"):
            if e.startswith(var + "=set:"):
                out[var] = e[len(var) + 5:]
            elif e == var + "=unset:":
                out[var] = UNSET
    return out


TESTS_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HARNESS_SOURCES = {}
for _d, _sub, _files in os.walk(TESTS_DIR):
    for _f in _files:
        if _f.startswith("test_") and _f.endswith(".cpp"):
            HARNESS_SOURCES.setdefault(_f[:-4], []).append(os.path.join(_d, _f))


def row_scripts(words, env):
    """Every script file a row runs that can be read: its command/environment words, the .js a
    harness .sh/.py names (beside it or in its scripts/), pool arms (`--arm <name> <script>`)."""
    out = []
    for w in list(words) + list(env):
        for part in re.split(r"[;,= ]", w):
            if SCRIPT_RX.search(part) and os.path.isfile(part):
                b = os.path.basename(part)
                if b in ("run_pool.py", "fresh_home.sh", "gpu-admit.sh", "gpu-exclusive.sh"):
                    continue
                out.append(part)
    # a HARNESS compiled from C++ (test_x spawning the app over MCP) carries its scripts in its
    # source: tests/**/<binary>.cpp, found by the binary's name
    for w in words:
        b = os.path.basename(w)
        if b.startswith("test_") and os.path.isfile(w):
            out += [c for c in HARNESS_SOURCES.get(b, [])]
    more = []
    for f in out:
        if f.endswith((".sh", ".py")):
            d = os.path.dirname(f)
            for m in NAMED_JS.findall(open(f, errors="ignore").read()):
                for c in (os.path.join(d, os.path.basename(m)), os.path.join(d, "scripts", os.path.basename(m))):
                    if os.path.isfile(c):
                        more.append(c)
    return list(dict.fromkeys(out + more))


def reads_picture(files):
    """-> the first pixel/GI verb found in the files, or None."""
    for f in files:
        try:
            text = open(f, errors="ignore").read()
        except OSError:
            continue
        for v in PIXEL_VERBS + GI_VERBS:
            if v in text:
                return "%s in %s" % (v.rstrip("("), os.path.basename(f))
        if f.endswith(".cpp") and MCP_PIXEL in text:
            return "the MCP screenshot tool in %s" % os.path.basename(f)
        if f.endswith((".sh", ".py", ".cpp")):
            for v in PIXEL_TOOLS:
                if re.search(r"(^|[^\w-])%s\b" % re.escape(v), text):
                    return "%s in %s" % (v, os.path.basename(f))
    return None


def site(test, listing):
    """The CMakeLists line that REGISTERED the row: the innermost backtrace frame outside the
    helpers' file (tests/CMakeLists.txt holds jah_gpu_row / jah_add_pool)."""
    bt = test.get("backtrace")
    graph = listing.get("backtraceGraph") or {}
    nodes, files = graph.get("nodes") or [], graph.get("files") or []
    chain = []
    while isinstance(bt, int) and 0 <= bt < len(nodes):
        n = nodes[bt]
        if "file" in n and "line" in n and n["file"] < len(files):
            chain.append((files[n["file"]], n["line"]))
        bt = n.get("parent")
    for f, line in chain:
        if not f.endswith("/tests/CMakeLists.txt"):
            return "%s:%d" % (os.path.relpath(f), line)
    return "%s:%d" % (os.path.relpath(chain[0][0]), chain[0][1]) if chain else "?"


def why_of(props):
    for e in closure.as_list(props.get("ENVIRONMENT_MODIFICATION")):
        e = str(e)
        if e.startswith("JAHSHAKA_TEST_NEEDS_WHY=set:") and e[28:].strip():
            return e[28:].strip()
    return None


def needs_error(value):
    words = value.split()
    if not words:
        return "an empty NEEDS (say NONE)"
    if "none" in words:
        return None if words == ["none"] else "NONE stands alone (got '%s')" % value
    bad = [w for w in words if w not in WORDS]
    if bad:
        return "'%s' is not a switchable feature (%s, or NONE)" % (" ".join(bad), " ".join(WORDS))
    if len(set(words)) != len(words):
        return "a feature named twice ('%s')" % value
    return None


def check(listing, app, out, harnesses=frozenset()):
    bad = []
    in_scope = 0
    by_decl = {}
    whys = []
    for t in listing.get("tests") or []:
        props = closure.props_of(t)
        words = [str(w) for w in (t.get("command") or [])]
        env = [str(e) for e in closure.as_list(props.get("ENVIRONMENT"))]
        where = site(t, listing)
        typed = [e.split("=", 1)[0] for e in env if e.startswith(("JAHSHAKA_TEST_TIER=", "JAHSHAKA_TEST_NEEDS="))]
        if typed:
            bad.append((t["name"], "types %s into its ENVIRONMENT — declare TIER/NEEDS on the registration" % "/".join(typed), where))
        if "--test-tier" in words:
            bad.append((t["name"], "passes --test-tier on its command — declare TIER/NEEDS on the registration", where))
        vulkan, _registered, why = closure.classify(t, app, harnesses)
        if not vulkan or not why.startswith("APP "):
            continue
        if closure.admitted_class(words) == "selftest":
            continue
        in_scope += 1
        d = declared(props)
        tier, needs = d.get("JAHSHAKA_TEST_TIER"), d.get("JAHSHAKA_TEST_NEEDS")
        if tier is None or needs is None:
            bad.append((t["name"], "starts the app (%s) and declares no %s" % (
                why[4:], " and no ".join(w for w, v in (("TIER", tier), ("NEEDS", needs)) if v is None)), where))
            continue
        if tier == UNSET or needs == UNSET:
            if tier != needs:
                bad.append((t["name"], "half a document declaration (TIER %s, NEEDS %s)" % (tier, needs), where))
                continue
            by_decl["document"] = by_decl.get("document", 0) + 1
            continue
        if tier not in TIERS:
            bad.append((t["name"], "TIER '%s' is not one of %s" % (tier, " ".join(TIERS)), where))
            continue
        err = needs_error(needs)
        if err:
            bad.append((t["name"], "NEEDS: " + err, where))
            continue
        if needs.split() == ["none"]:
            why_pic = reads_picture(row_scripts(words, env))
            because = why_of(props)
            if why_pic and because:
                whys.append((t["name"], why_pic, because))
            elif why_pic:
                bad.append((t["name"], "declares NONE but reads the picture or drives Photon (%s) — name "
                            "what it needs, or say NEEDS_WHY \"<the claim>\" when the claim IS every "
                            "feature off" % why_pic, where))
                continue
        key = "%s %s" % (tier, " ".join(sorted(needs.split())))
        by_decl[key] = by_decl.get(key, 0) + 1
    out.write("row_declares_needs: %d app rows; %d refused\n" % (in_scope, len(bad)))
    out.write("row_declares_needs: rows by declaration: %s\n"
              % ", ".join("%s: %d" % kv for kv in sorted(by_decl.items())))
    out.write("row_declares_needs: %d picture row(s) declare NONE with NEEDS_WHY\n" % len(whys))
    for name, what, because in whys:
        out.write("  NEEDS_WHY %s (%s): %s\n" % (name, what, because))
    for name, why, where in bad:
        out.write("  UNDECLARED %s — %s — registered at %s\n" % (name, why, where))
    return bad


PIXEL_TOY = None


def self_test(app, out):
    global PIXEL_TOY
    import tempfile
    fd, PIXEL_TOY = tempfile.mkstemp(suffix=".js")
    os.write(fd, b'var s = editor.screenshot("x.png", 64, 64, []);\n')
    os.close(fd)
    def mod(tier, needs):
        v = []
        if tier is not None:
            v.append("JAHSHAKA_TEST_TIER=set:" + tier)
        if needs is not None:
            v.append("JAHSHAKA_TEST_NEEDS=set:" + needs)
        return [{"name": "ENVIRONMENT_MODIFICATION", "value": v}]
    admit = ["/x/scripts/gpu-admit.sh", "1", "--label"]
    pool = ["/usr/bin/python3", "/x/tests/support/run_pool.py", "--vram-tokens", "1", "--app", app]
    synthetic = {"tests": [
        {"name": "toy.declared", "command": admit + ["toy.declared:app", "--", app, "--script", "x.js"],
         "properties": mod("low", "none")},
        {"name": "toy.declared_needs", "command": admit + ["toy.declared_needs:app", "--", app],
         "properties": mod("high", "photon bloom")},
        {"name": "toy.undeclared", "command": admit + ["toy.undeclared:app", "--", app, "--script", "x.js"]},
        {"name": "toy.tier_only", "command": admit + ["toy.tier_only:app", "--", app], "properties": mod("epic", None)},
        {"name": "toy.typed", "command": admit + ["toy.typed:app", "--", app],
         "properties": [{"name": "ENVIRONMENT", "value": ["JAHSHAKA_TEST_TIER=low"]}]},
        {"name": "toy.bad_word", "command": admit + ["toy.bad_word:app", "--", app], "properties": mod("low", "shadows")},
        {"name": "toy.none_plus", "command": admit + ["toy.none_plus:app", "--", app], "properties": mod("low", "none photon")},
        {"name": "toy.pool_declared", "command": pool, "properties": mod("epic", "photon")},
        {"name": "toy.document", "command": admit + ["toy.document:app", "--", app],
         "properties": [{"name": "ENVIRONMENT_MODIFICATION",
                         "value": ["JAHSHAKA_TEST_TIER=unset:", "JAHSHAKA_TEST_NEEDS=unset:"]}]},
        {"name": "toy.half_document", "command": admit + ["toy.half_document:app", "--", app],
         "properties": [{"name": "ENVIRONMENT_MODIFICATION",
                         "value": ["JAHSHAKA_TEST_TIER=unset:", "JAHSHAKA_TEST_NEEDS=set:photon"]}]},
        {"name": "toy.pool_undeclared", "command": pool},
        {"name": "toy.none_reads_pixels", "command": admit + ["toy.none_reads_pixels:app", "--", app, "--script", PIXEL_TOY],
         "properties": mod("low", "none")},
        {"name": "toy.none_why", "command": admit + ["toy.none_why:app", "--", app, "--script", PIXEL_TOY],
         "properties": [{"name": "ENVIRONMENT_MODIFICATION", "value": [
             "JAHSHAKA_TEST_TIER=set:low", "JAHSHAKA_TEST_NEEDS=set:none",
             "JAHSHAKA_TEST_NEEDS_WHY=set:the GI-off picture is the claim"]}]},
        {"name": "toy.photon_reads_pixels", "command": admit + ["toy.photon_reads_pixels:app", "--", app, "--script", PIXEL_TOY],
         "properties": mod("epic", "photon")},
        {"name": "toy.selftest", "command": admit + ["toy.selftest:selftest", "--", app, "--engine-selftest", "o.png"]},
        {"name": "toy.headless", "command": [app, "--headless", "--script", "x.js"]},
    ]}
    buf = io.StringIO()
    try:
        named = sorted(set(b[0] for b in check(synthetic, app, buf)))
    finally:
        os.unlink(PIXEL_TOY)
    want = sorted(["toy.undeclared", "toy.tier_only", "toy.typed", "toy.bad_word", "toy.none_plus",
                   "toy.pool_undeclared", "toy.half_document", "toy.none_reads_pixels"])
    ok = named == want
    out.write("row_declares_needs --self-test: named %s (want %s): %s\n" % (named, want, "ok" if ok else "FAIL"))
    if not ok:
        out.write(buf.getvalue())
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
    if not closure.links_ogre(app):
        print("row_declares_needs: the app %s does not exist or links no Ogre — build it first" % app)
        return 1
    fails = 0
    if selftest and not self_test(app, sys.stdout):
        fails += 1
    r = subprocess.run([args["--ctest"], "--show-only=json-v1"], cwd=args["--build"],
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if r.returncode != 0:
        print("row_declares_needs: ctest --show-only failed: %s" % r.stderr.decode()[-400:])
        return 1
    # the app-spawning harnesses, read once from the build's compile commands (the closure's reader)
    try:
        harnesses = closure.read_harnesses(args["--build"], app)
    except (OSError, ValueError) as e:
        print("row_declares_needs: cannot read %s/compile_commands.json: %s" % (args["--build"], e))
        return 1
    if not harnesses:
        print("row_declares_needs: no target in %s/compile_commands.json defines JAHSHAKA_BINARY as %s"
              " — no harness would be seen" % (args["--build"], app))
        return 1
    if check(json.loads(r.stdout), app, sys.stdout, harnesses):
        fails += 1
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
