#!/usr/bin/env python3
# source.ray_loops_bounded — EVERY RAY QUERY LOOP IS BOUNDED IN THE SHADER (GATHER-HANG-1 stage 2; CODING_RULES section 2:
# every data-driven GPU loop is bounded in the shader, its count clamped at its law).
#
# WHAT WAS WRONG. `while( jahRqProceed( q ) )` runs one trip per candidate the traversal hands back, and nothing bounded
# it: the three loops of irisgl/engine/src/rayquery/include/jah_rq_alpha.glsl (JAH_RQ_PROCEED, _TRANSMIT, _PANES: 27 call
# sites) and two bare opaque loops (rq_reflect.comp's id query, rq_motion_skin.comp) - 29 unbounded ray query loops.
#
# THE LAW, over every shader text under irisgl/engine (src/: .comp .glsl .vert .frag; media/: .glsl .any - the Forward+
# lamps take jah_rq_alpha.glsl as an Hlms piece), comments stripped by webgpu_census.strip (the one stripper):
#   (1) `jahRqProceed(` appears EXACTLY ONCE outside the seam's own #define (jah_rq.glsl): in jah_rq_alpha.glsl's
#       #define JAH_RQ_LOOP_BEGIN_ - so every loop is that counted form (the API token itself lives only in the seam:
#       source.ray_seam);
#   (2) JAH_RQ_LOOP_BEGIN_ counts a trip a candidate and, at the bound it reads from jahRqStepBoundNow(), calls
#       jahRqTerminate( q ) and breaks; every definition of jahRqStepBoundNow answers kJahRqStepBound or a min() with it
#       (a test door may LOWER the bound, never raise it);
#   (3) the law is kJahRqStepLaw = 1024u and the bound kJahRqStepBound = 4u * kJahRqStepLaw, and the host's mirror
#       (ScreenProbeGather.h kRayQueryStepLaw / kRayQueryStepBound) holds the same numbers.
# The row prints the loops it found through the counted family (the four JAH_RQ_PROCEED forms' call sites).
# A NEGATIVE CONTROL runs first: synthetic trees with a raw loop, a family head with no cut, a bound a door can raise
# and a mirror that differs (each red), a raw loop inside a comment and the lawful shape (green).
#
# usage: ray_loops_bounded.py <source root>
import os
import re
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from webgpu_census import strip  # noqa: E402  (the one comment stripper; its cases are the census's self-test)

ENGINE = os.path.join("irisgl", "engine")
SEAM = "src/rayquery/include/jah_rq.glsl"
ALPHA = "src/rayquery/include/jah_rq_alpha.glsl"
MIRROR = "src/ScreenProbeGather.h"
SRC_EXTS = (".comp", ".glsl", ".vert", ".frag")
MEDIA_EXTS = (".glsl", ".any")
PROCEED = re.compile(r"\bjahRqProceed\s*\(")
FAMILY = re.compile(r"\b(JAH_RQ_PROCEED(?:_TRANSMIT|_PANES|_OPAQUE)?)\s*\(")
LAW = 1024


def shader_files(root):
    for top in ("src", "media"):
        exts = SRC_EXTS if top == "src" else MEDIA_EXTS
        for d, dirs, names in os.walk(os.path.join(root, ENGINE, top)):
            dirs.sort()
            for n in sorted(names):
                if n.endswith(exts):
                    yield os.path.relpath(os.path.join(d, n), os.path.join(root, ENGINE)).replace(os.sep, "/")


def macro(text, name):
    """the whole text of `#define name(...)` with its continued lines, or None"""
    m = re.search(r"#\s*define\s+%s\b.*?(?:\\\n.*?)*(?:\n|$)" % re.escape(name), text)
    return m.group(0) if m else None


def check(root):
    """(faults, loops through the family)"""
    bad, loops, read = [], [], 0
    texts = {}
    for rel in shader_files(root):
        read += 1
        with open(os.path.join(root, ENGINE, rel), encoding="utf-8", errors="replace") as fh:
            texts[rel] = strip(fh.read())
    if read == 0:
        return ["no shader file under %s" % os.path.join(root, ENGINE)], []
    good = 0
    for rel, text in texts.items():
        define = None   # the #define a continued line belongs to
        for n, line in enumerate(text.split("\n"), 1):
            m = re.match(r"\s*#\s*define\s+(\w+)", line)
            if m:
                define = m.group(1)
            if PROCEED.search(line) and not (rel == SEAM and define == "jahRqProceed"):
                if rel == ALPHA and define == "JAH_RQ_LOOP_BEGIN_":
                    good += 1
                else:
                    bad.append("(1) %s:%d%s: a ray query loop outside the counted form: %s"
                               % (rel, n, " (in #define %s)" % define if define else "", line.strip().rstrip("\\").strip()))
            fm = FAMILY.search(line)
            if fm and not re.match(r"\s*#\s*define\b", line) and not (rel == ALPHA and define):
                loops.append("%s:%d %s" % (rel, n, fm.group(1)))
            if not line.rstrip().endswith("\\"):
                define = None
    if good != 1:
        bad.append("(1) %s: want exactly ONE jahRqProceed(, in #define JAH_RQ_LOOP_BEGIN_ (found %d)" % (ALPHA, good))
    alpha = texts.get(ALPHA, "")
    head = macro(alpha, "JAH_RQ_LOOP_BEGIN_") or ""
    if not (re.search(r"jahRqBound_\s*=\s*jahRqStepBoundNow\s*\(\s*\)", head) and
            re.search(r">=\s*jahRqBound_", head) and re.search(r"\bjahRqTerminate\s*\(", head) and "break" in head):
        bad.append("(2) %s: JAH_RQ_LOOP_BEGIN_ does not terminate its query at jahRqStepBoundNow()'s bound" % ALPHA)
    bodies = re.findall(r"uint\s+jahRqStepBoundNow\s*\(\s*\)\s*\{(.*?)\n?\}", alpha, flags=re.S)
    if not bodies:
        bad.append("(2) %s: no jahRqStepBoundNow()" % ALPHA)
    for b in bodies:
        rets = re.findall(r"return\s+([^;]*);", b)
        if not rets or not all(re.fullmatch(r"\s*kJahRqStepBound\s*", r) or re.search(r"\bmin\s*\([^;]*\bkJahRqStepBound\b", r)
                               or re.fullmatch(r".*\?\s*kJahRqStepBound\s*:\s*min\s*\(.*\bkJahRqStepBound\b.*\)\s*", r)
                               for r in rets):
            bad.append("(2) %s: a jahRqStepBoundNow() may answer above kJahRqStepBound: return %s" % (ALPHA, "; ".join(rets)))
    law = re.search(r"const\s+uint\s+kJahRqStepLaw\s*=\s*(\d+)u\s*;", alpha)
    if not law or int(law.group(1)) != LAW or not re.search(
            r"const\s+uint\s+kJahRqStepBound\s*=\s*4u\s*\*\s*kJahRqStepLaw\s*;", alpha):
        bad.append("(3) %s: the law is kJahRqStepLaw = %du and the bound kJahRqStepBound = 4u * kJahRqStepLaw" % (ALPHA, LAW))
    try:
        with open(os.path.join(root, ENGINE, MIRROR), encoding="utf-8") as fh:
            mirror = strip(fh.read())
    except OSError as e:
        mirror = ""
        bad.append("(3) cannot read %s (%s)" % (MIRROR, e))
    hl = re.search(r"kRayQueryStepLaw\s*=\s*(\d+)u\s*;", mirror)
    if not hl or int(hl.group(1)) != LAW or not re.search(r"kRayQueryStepBound\s*=\s*4u\s*\*\s*kRayQueryStepLaw\s*;", mirror):
        bad.append("(3) %s: the host's mirror is kRayQueryStepLaw = %du, kRayQueryStepBound = 4u * kRayQueryStepLaw"
                   % (MIRROR, LAW))
    return bad, loops


LAWFUL_SEAM = "#define jahRqProceed( q ) rayQueryProceedEXT( q )\n#define jahRqTerminate( q ) rayQueryTerminateEXT( q )\n"
LAWFUL_ALPHA = '''const uint kJahRqStepLaw = 1024u;
const uint kJahRqStepBound = 4u * kJahRqStepLaw;
uint jahRqStepBoundNow()
{
	const uint b = 0u;
	return b == 0u ? kJahRqStepBound : min( b, kJahRqStepBound );
}
#define JAH_RQ_LOOP_BEGIN_( q ) \\
	{ \\
		const uint jahRqBound_ = jahRqStepBoundNow(); \\
		uint jahRqTrips_ = 0u; \\
		while( jahRqProceed( q ) ) \\
		{ \\
			if( ++jahRqTrips_ >= jahRqBound_ ) \\
			{ \\
				jahRqTerminate( q ); \\
				break; \\
			}
#define JAH_RQ_LOOP_END_ \\
		} \\
	}
#define JAH_RQ_PROCEED( q ) \\
	JAH_RQ_LOOP_BEGIN_( q ) \\
	JAH_RQ_LOOP_END_
'''
LAWFUL_MIRROR = "constexpr uint32_t kRayQueryStepLaw = 1024u;\nconstexpr uint32_t kRayQueryStepBound = 4u * kRayQueryStepLaw;\n"
LAWFUL_JOB = "void main()\n{\n\tJahRq q;\n\tJAH_RQ_PROCEED( q )\n}\n"


def synthetic(**over):
    root = tempfile.mkdtemp(prefix="ray-loops-")
    files = {SEAM: LAWFUL_SEAM, ALPHA: LAWFUL_ALPHA, MIRROR: LAWFUL_MIRROR, "src/rayquery/rq_job.comp": LAWFUL_JOB}
    for k, v in over.items():
        files[k.replace("__", "/")] = v
    for rel, text in files.items():
        path = os.path.join(root, ENGINE, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w") as fh:
            fh.write(text)
    return root


def self_test():
    raw = "void main()\n{\n\tJahRq q;\n\twhile( jahRqProceed( q ) )\n\t{\n\t}\n}\n"
    cases = [
        ("a raw loop in a job", {"src__rayquery__rq_raw.comp": raw}, True),
        ("a raw loop in an Hlms piece", {"media__Hlms__Jahshaka__Raw_piece_ps.any": raw}, True),
        ("the family head with no cut", {ALPHA.replace("/", "__"): LAWFUL_ALPHA.replace("jahRqTerminate( q ); \\\n", "")},
         True),
        ("a bound a door can raise", {ALPHA.replace("/", "__"): LAWFUL_ALPHA.replace(
            "return b == 0u ? kJahRqStepBound : min( b, kJahRqStepBound );", "return b == 0u ? kJahRqStepBound : b;")}, True),
        ("a mirror that differs", {MIRROR.replace("/", "__"): LAWFUL_MIRROR.replace("1024u", "2048u")}, True),
        ("a raw loop inside a comment", {"src__rayquery__rq_quiet.comp": "/* " + raw + " */\n// while( jahRqProceed( q ) )\n"},
         False),
        ("the lawful shape", {}, False),
    ]
    ok = True
    for name, over, want_red in cases:
        root = synthetic(**over)
        try:
            red = bool(check(root)[0])
        finally:
            shutil.rmtree(root, ignore_errors=True)
        good = red == want_red
        ok = ok and good
        print("  %s: the negative control - %s is %s" % ("ok" if good else "FAIL", name, "red" if red else "green"))
    return ok


def main():
    if len(sys.argv) != 2:
        print("usage: ray_loops_bounded.py <source root>")
        print("source.ray_loops_bounded: FAILED (1)")
        return 2
    fails = 0 if self_test() else 1
    bad, loops = check(sys.argv[1])
    for b in bad:
        print("  FAIL: " + b)
    if not bad:
        print("  ok: one jahRqProceed(, in JAH_RQ_LOOP_BEGIN_, cut at kJahRqStepBound = 4096 (the law 1024); the host's "
              "mirror equal; %d ray query loops through the counted family:" % len(loops))
        for l in loops:
            print("    " + l)
    fails += len(bad)
    sys.stdout.flush()
    print("source.ray_loops_bounded: " + ("PASSED" if not fails else "FAILED (%d)" % fails))
    return 0 if not fails else 1


if __name__ == "__main__":
    sys.exit(main())
