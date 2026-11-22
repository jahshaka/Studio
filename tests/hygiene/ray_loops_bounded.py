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
#       (ScreenProbeGather.h kRayQueryStepLaw / kRayQueryStepBound) holds the same numbers;
#   (4) A CUT RAY ANSWERS AS OCCLUDED (the lead's ruling (B)): JAH_RQ_LOOP_BEGIN_ raises its `cut` flag at the bound,
#       JAH_RQ_PROCEED_TRANSMIT sets its T to 0 on it, and every call of JAH_RQ_PROCEED, _PANES and _OPAQUE names a
#       `cut` flag that its caller READS again within the next 12 code lines (the flag never left unread);
#   (5) EVERY RAY's INPUTS ARE DEFINED (the lead's calls 2-3): `jahRqInit(` appears only in the seam's own #define and
#       ONCE in jah_rq_alpha.glsl's #define JAH_RQ_INIT, which tests the ray by jahRqRayDefined (finite3 / finite1 - the
#       bits, never x == x - tMin >= 0, tMin <= tMax) and initialises an empty query (cull mask 0) otherwise, counted;
#       and there are as many JAH_RQ_INIT( calls as ray query loops;
#   (6) THE COUNTS AT THEIR LAWS (the lead's calls 1 and 7): the five count loops that trace a ray a trip and the three
#       marches a float steers carry their clamp (CLAMPED below: the hit table's lights to the section the host wrote,
#       and to an ABSOLUTE ceiling, the host's word of the light records its buffer holds - a wholly garbage header held),
#       the card movers' lamps to their buffer, the two gathers' rays to 4096 with their float bit-tested, the voxel
#       march's resolution and the young reach bit-tested and held), and the other count loops are on
#       ray_count_loops_pending.txt - every entry's header still in its file, at most kPendingAtMost entries: the list
#       ONLY SHRINKS (an entry clamped leaves it and the number falls in the same commit).
#   (7) EVERY EXIT OF AN UNBOUNDED LOOP FORM IS NAMED (the final read's F1: law (6) lists its clamps by hand, from the
#       preflight's census of loop HEADERS - a `for( ;; )` has no header bound, so the voxel march's SECOND exit, a finer
#       cascade's segment `itSub >= maxK` with maxK from the raw resolution, was never read and law (6) read green over
#       it): every `for( ;; )` / `while( true )` / `do {` in the shader texts is in UNBOUNDED (a new one is red until
#       named); inside its body every counter exit `it<name> >= <bound>` is one of its named exits (a new exit red);
#       each bound is built from resolutions that come ONLY from jahVoxResAtLaw( (the one clamped form: finite3, then
#       [1, kJahVoxResMax]), defined once.
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
INIT = re.compile(r"\bjahRqInit\s*\(")
INIT_CALL = re.compile(r"\bJAH_RQ_INIT\s*\(")
FLAGGED = re.compile(r"\bJAH_RQ_PROCEED(?:_PANES|_OPAQUE)?\s*\(([^()]*)\)")
LAW = 1024
PENDING = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ray_count_loops_pending.txt")
kPendingAtMost = 29   # (26 + the final re-read's three: jah_inject_visibility.glsl:311 maxSteps from p_chainRes (:286, float-steered: F1's kind), rq_probe_resume.comp:245 blocks = uint( r.blocks.w ), rq_hit_composite.comp:160 last <= capacity = uint( p.list.x ) - all three untested)
# (file under irisgl/engine, the clamp's pattern in its comment-stripped text, how many times, what it is)
CLAMPED = (
    ("src/rayquery/include/jah_rq_hit_direct.glsl", r"const\s+uint\s+count\s*=\s*jahHitLightCount\s*\(\s*T\s*\)", 2,
     "the hit table's lights to the section the host wrote (two loops)"),
    ("src/rayquery/include/jah_rq_hit_direct.glsl", r"min\s*\(\s*min\s*\(\s*T\.u\[0\]\s*,\s*e\s*>\s*b\s*\?\s*\(\s*e\s*-\s*b\s*\)\s*/\s*kJahHitLightWords\s*:\s*0u\s*\)\s*,\s*cap\s*\)", 1,
     "jahHitLightCount: u[0] held to (u[3] - u[2]) / kJahHitLightWords AND to the buffer's capacity (cap)"),
    ("src/rayquery/include/jah_rq_hit_direct.glsl", r"cap\s*=\s*finite1\s*\(\s*capF\s*\)\s*\?\s*uint\s*\(\s*clamp\s*\(\s*capF\s*,\s*0\.0\s*,", 1,
     "the table's ABSOLUTE ceiling: the host's word (JAH_HIT_PARAMS.worldMax.w), bit-tested - a wholly garbage header held"),
    ("src/rayquery/include/jah_rq_hit_direct.glsl", r"capF\s*=\s*JAH_HIT_PARAMS\.worldMax\.w\s*;", 1,
     "the ceiling read from the host's hit params, never from the table"),
    ("src/rayquery/rq_card_movers.comp", r"i\s*<\s*min\s*\(\s*lampCount\.x\s*,\s*uint\s*\(\s*lamps\.length\s*\(\s*\)\s*\)\s*\)", 1,
     "the card movers' lamps to their buffer's records"),
    ("src/rayquery/rq_card_gather.comp", r"const\s+int\s+n\s*=\s*finite1\s*\(\s*p\.knobs\.x\s*\)\s*\?\s*clamp\s*\(\s*int\s*\(\s*min\s*\(\s*p\.knobs\.x\s*,\s*4096\.0\s*\)\s*\)\s*,\s*1\s*,\s*4096\s*\)", 1,
     "the cards' gather's rays, bit-tested, to 4096"),
    ("src/rayquery/rq_voxel_sides.comp", r"const\s+int\s+rays\s*=\s*finite1\s*\(\s*p\.knobs\.x\s*\)\s*\?\s*clamp\s*\(\s*int\s*\(\s*min\s*\(\s*p\.knobs\.x\s*,\s*4096\.0\s*\)\s*\)\s*,\s*1\s*,\s*4096\s*\)", 1,
     "the voxel sides' rays, bit-tested, to 4096"),
    ("src/rayquery/include/jah_voxel_ray.glsl", r"return\s+finite3\s*\(\s*res\s*\)\s*\?\s*clamp\s*\(\s*ivec3\s*\(\s*min\s*\(\s*res\s*,\s*vec3\s*\(\s*kJahVoxResMax\s*\)", 1,
     "jahVoxResAtLaw: a cascade's resolution, bit-tested, to kJahVoxResMax (it steers maxSteps and maxK: law (7))"),
    ("src/rayquery/rq_probe_integrate.comp", r"youngReach\s*=\s*finite1\s*\(\s*p\.young\.y\s*\)\s*\?\s*clamp\s*\(\s*p\.young\.y\s*,\s*1\.0\s*,\s*64\.0\s*\)", 1,
     "the young reach, bit-tested, to 64 cells (it steers the tent's two loops)"),
    ("src/rayquery/rq_probe_integrate.comp", r"youngFrames\s*=\s*finite1\s*\(\s*p\.young\.x\s*\)", 1,
     "the young frames, bit-tested"),
)


def norm(t):
    return " ".join(t.split())


# law (7): each unbounded loop form's file, how many it holds, its counter exits (counter -> bound), and the
# resolutions its bounds are built from (each assigned ONLY from jahVoxResAtLaw)
UNBOUNDED = {
    "src/rayquery/include/jah_voxel_ray.glsl": (1, {"itMain": "maxSteps", "itSub": "maxK"},
                                                 {"maxSteps": "resI", "maxK": "resIK"}),
}
UNBOUNDED_FORM = re.compile(r"\bfor\s*\(\s*;\s*;\s*\)|\bwhile\s*\(\s*(?:true|1)\s*\)|\bdo\s*\{")
COUNTER_EXIT = re.compile(r"\b(it\w*)\s*>=\s*(\w+)")


def body_at(text, at):
    """the braced body of the loop whose header starts at `at` (from its first '{')"""
    i = text.find("{", at)
    depth = 0
    for j in range(i, len(text)):
        depth += 1 if text[j] == "{" else -1 if text[j] == "}" else 0
        if depth == 0:
            return text[i:j + 1]
    return text[i:]


def check_unbounded(texts):
    """law (7)'s faults"""
    bad = []
    helpers = sum(len(re.findall(r"\bivec3\s+jahVoxResAtLaw\s*\(", t)) for t in texts.values())
    for rel, t in sorted(texts.items()):
        forms = [m.start() for m in UNBOUNDED_FORM.finditer(t)]
        want, exits, built = UNBOUNDED.get(rel, (0, {}, {}))
        if len(forms) != want:
            bad.append("(7) %s: %d unbounded loop form(s), %d named - name its every exit in UNBOUNDED" % (rel, len(forms), want))
            continue
        for at in forms:
            body = body_at(t, at)
            found = {(c, b) for c, b in COUNTER_EXIT.findall(body)}
            for c, b in sorted(found - set(exits.items())):
                bad.append("(7) %s: an unnamed exit `%s >= %s` in an unbounded loop" % (rel, c, b))
            for c, b in sorted(set(exits.items()) - found):
                bad.append("(7) %s: the named exit `%s >= %s` is gone" % (rel, c, b))
        for bound, res in built.items():
            if not re.search(r"\b%s\s*=\s*%s\.x\s*\+\s*%s\.y\s*\+\s*%s\.z\b" % (bound, res, res, res), t):
                bad.append("(7) %s: %s is not built from %s's three sides" % (rel, bound, res))
            sets = re.findall(r"\b%s\s*=(?!=)\s*([^;]*);" % res, t)
            # (a literal zero is the declaration's "no segment yet", never a bound a float steers)
            sets = [v for v in sets if not re.fullmatch(r"ivec3\s*\(\s*0\s*\)", v.strip())]
            if not sets or any(not re.match(r"jahVoxResAtLaw\s*\(", v.strip()) for v in sets):
                bad.append("(7) %s: %s is assigned other than by jahVoxResAtLaw( (%s)" % (rel, res, "; ".join(v.strip() for v in sets) or "never"))
    if any(UNBOUNDED.values()) and helpers != 1:
        bad.append("(7) jahVoxResAtLaw is defined %d time(s) (1 wanted)" % helpers)
    return bad


def check_counts(root, texts, pending_path):
    """law (6)'s and law (7)'s faults"""
    bad = check_unbounded(texts)
    for rel, pat, want, what in CLAMPED:
        got = len(re.findall(pat, texts.get(rel, "")))
        if got != want:
            bad.append("(6) %s: %s - the clamp found %d time(s), want %d" % (rel, what, got, want))
    try:
        with open(pending_path) as fh:
            entries = [l.rstrip("\n").split("\t", 1) for l in fh if l.strip() and not l.startswith("#")]
    except OSError as e:
        return bad + ["(6) cannot read %s (%s)" % (pending_path, e)]
    if len(entries) > kPendingAtMost:
        bad.append("(6) %d pending count loops, at most %d: the list only shrinks" % (len(entries), kPendingAtMost))
    want = {}
    for e in entries:
        if len(e) != 2:
            bad.append("(6) a pending entry is not <file> TAB <header>: %r" % "\t".join(e))
            continue
        want[(e[0], norm(e[1]))] = want.get((e[0], norm(e[1])), 0) + 1
    for (rel, head), n in sorted(want.items()):
        lines = [norm(l) for l in texts.get(rel, "").split("\n")]
        if lines.count(head) < n:
            bad.append("(6) %s: the pending loop '%s' is gone (clamped or moved): take it off the list and lower "
                       "kPendingAtMost" % (rel, head))
    return bad


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


def check(root, pending=PENDING):
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
    inits_good, init_calls = 0, 0
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
            if INIT.search(line) and not (rel == SEAM and define == "jahRqInit"):
                if rel == ALPHA and define == "JAH_RQ_INIT":
                    inits_good += 1
                else:
                    bad.append("(5) %s:%d%s: a ray query initialised outside JAH_RQ_INIT (its inputs untested): %s"
                               % (rel, n, " (in #define %s)" % define if define else "", line.strip().rstrip("\\").strip()))
            if INIT_CALL.search(line) and not re.match(r"\s*#\s*define\b", line) and not (rel == ALPHA and define):
                init_calls += 1
            fm = FAMILY.search(line)
            if fm and not re.match(r"\s*#\s*define\b", line) and not (rel == ALPHA and define):
                loops.append("%s:%d %s" % (rel, n, fm.group(1)))
                gm = FLAGGED.search(line)
                if gm:
                    flag = gm.group(1).split(",")[-1].strip()
                    after = "\n".join(text.split("\n")[n:n + 12])
                    if not re.fullmatch(r"[A-Za-z_]\w*", flag) or not re.search(r"\b%s\b" % re.escape(flag), after):
                        bad.append("(4) %s:%d: %s's cut flag '%s' is not read within 12 lines (a cut ray must answer "
                                   "as occluded)" % (rel, n, fm.group(1), flag))
            if not line.rstrip().endswith("\\"):
                define = None
    if inits_good != 1:
        bad.append("(5) %s: want exactly ONE jahRqInit(, in #define JAH_RQ_INIT (found %d)" % (ALPHA, inits_good))
    if init_calls != len(loops):
        bad.append("(5) %d JAH_RQ_INIT( calls for %d ray query loops (every loop's ray through the guard)"
                   % (init_calls, len(loops)))
    if good != 1:
        bad.append("(1) %s: want exactly ONE jahRqProceed(, in #define JAH_RQ_LOOP_BEGIN_ (found %d)" % (ALPHA, good))
    alpha = texts.get(ALPHA, "")
    head = macro(alpha, "JAH_RQ_LOOP_BEGIN_") or ""
    if not (re.search(r"jahRqBound_\s*=\s*jahRqStepBoundNow\s*\(\s*\)", head) and
            re.search(r">=\s*jahRqBound_", head) and re.search(r"\bjahRqTerminate\s*\(", head) and "break" in head):
        bad.append("(2) %s: JAH_RQ_LOOP_BEGIN_ does not terminate its query at jahRqStepBoundNow()'s bound" % ALPHA)
    if not re.search(r"\(\s*cut\s*\)\s*=\s*true", head):
        bad.append("(4) %s: JAH_RQ_LOOP_BEGIN_ raises no cut flag at the bound" % ALPHA)
    transmit = macro(alpha, "JAH_RQ_PROCEED_TRANSMIT") or ""
    if not re.search(r"if\s*\(\s*jahRqCutT_\s*\)\s*\\?\s*\(\s*T\s*\)\s*=\s*0\.0", transmit):
        bad.append("(4) %s: JAH_RQ_PROCEED_TRANSMIT does not answer a cut ray's T as 0 (blocked)" % ALPHA)
    bodies = re.findall(r"uint\s+jahRqStepBoundNow\s*\(\s*\)\s*\{(.*?)\n?\}", alpha, flags=re.S)
    if not bodies:
        bad.append("(2) %s: no jahRqStepBoundNow()" % ALPHA)
    for b in bodies:
        rets = re.findall(r"return\s+([^;]*);", b)
        if not rets or not all(re.fullmatch(r"\s*kJahRqStepBound\s*", r) or re.search(r"\bmin\s*\([^;]*\bkJahRqStepBound\b", r)
                               or re.fullmatch(r".*\?\s*kJahRqStepBound\s*:\s*min\s*\(.*\bkJahRqStepBound\b.*\)\s*", r)
                               for r in rets):
            bad.append("(2) %s: a jahRqStepBoundNow() may answer above kJahRqStepBound: return %s" % (ALPHA, "; ".join(rets)))
    init = macro(alpha, "JAH_RQ_INIT") or ""
    defined = re.search(r"bool\s+jahRqRayDefined\s*\([^)]*\)\s*\{(.*?)\n\}", alpha, flags=re.S)
    body = defined.group(1) if defined else ""
    if not (re.search(r"jahRqOk_\s*=\s*jahRqRayDefined\s*\(", init) and re.search(r"jahRqOk_\s*\?\s*uint\s*\(\s*mask\s*\)\s*:\s*0u", init)
            and "jahRqRefusedCount" in init):
        bad.append("(5) %s: JAH_RQ_INIT does not test its ray by jahRqRayDefined, count a refusal and init an empty "
                   "query (cull mask 0)" % ALPHA)
    if not (re.search(r"finite3\s*\(\s*origin\s*\)", body) and re.search(r"finite3\s*\(\s*dir\s*\)", body)
            and re.search(r"finite1\s*\(\s*tMin\s*\)", body) and re.search(r"finite1\s*\(\s*tMax\s*\)", body)
            and re.search(r"tMin\s*>=\s*0\.0", body) and re.search(r"tMin\s*<=\s*tMax", body)):
        bad.append("(5) %s: jahRqRayDefined does not test origin, dir, tMin, tMax by their bits and tMin in [0, tMax]" % ALPHA)
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
    if pending:
        bad += check_counts(root, texts, pending)
    return bad, loops


LAWFUL_SEAM = "#define jahRqInit( q, a, f, m, o, t0, d, t1 ) rayQueryInitializeEXT( q, a, f, m, o, t0, d, t1 )\n" \
    "#define jahRqProceed( q ) rayQueryProceedEXT( q )\n#define jahRqTerminate( q ) rayQueryTerminateEXT( q )\n"
LAWFUL_ALPHA = '''bool jahRqRayDefined( vec3 origin, float tMin, vec3 dir, float tMax )
{
	return finite3( origin ) && finite3( dir ) && finite1( tMin ) && finite1( tMax ) && tMin >= 0.0 && tMin <= tMax;
}
#define JAH_RQ_INIT( q, tlas, flags, mask, origin, tMin, dir, tMax ) \\
	{ \\
		const bool jahRqOk_ = jahRqRayDefined( origin, tMin, dir, tMax ); \\
		if( !jahRqOk_ ) \\
			jahRqRefusedCount(); \\
		jahRqInit( q, tlas, flags, jahRqOk_ ? uint( mask ) : 0u, origin, tMin, dir, tMax ); \\
	}
const uint kJahRqStepLaw = 1024u;
const uint kJahRqStepBound = 4u * kJahRqStepLaw;
uint jahRqStepBoundNow()
{
	const uint b = 0u;
	return b == 0u ? kJahRqStepBound : min( b, kJahRqStepBound );
}
#define JAH_RQ_LOOP_BEGIN_( q, cut ) \\
	{ \\
		const uint jahRqBound_ = jahRqStepBoundNow(); \\
		uint jahRqTrips_ = 0u; \\
		( cut ) = false; \\
		while( jahRqProceed( q ) ) \\
		{ \\
			if( ++jahRqTrips_ >= jahRqBound_ ) \\
			{ \\
				jahRqTerminate( q ); \\
				( cut ) = true; \\
				break; \\
			}
#define JAH_RQ_LOOP_END_ \\
		} \\
	}
#define JAH_RQ_PROCEED( q, cut ) \\
	JAH_RQ_LOOP_BEGIN_( q, cut ) \\
	JAH_RQ_LOOP_END_
#define JAH_RQ_PROCEED_TRANSMIT( q, T ) \\
	{ \\
	bool jahRqCutT_; \\
	JAH_RQ_LOOP_BEGIN_( q, jahRqCutT_ ) \\
	JAH_RQ_LOOP_END_ \\
	if( jahRqCutT_ ) \\
		( T ) = 0.0; \\
	}
'''
LAWFUL_MIRROR = "constexpr uint32_t kRayQueryStepLaw = 1024u;\nconstexpr uint32_t kRayQueryStepBound = 4u * kRayQueryStepLaw;\n"
LAWFUL_JOB = "void main()\n{\n\tJahRq q;\n\tJAH_RQ_INIT( q, tlas, 0u, 0xFFu, o, 0.0, d, 1.0 )\n\tbool cut;\n\tJAH_RQ_PROCEED( q, cut )\n\tif( cut )\n\t\treturn;\n}\n"


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
        ("a cut flag never read", {"src__rayquery__rq_job.comp": LAWFUL_JOB.replace("\tif( cut )\n\t\treturn;\n", "")}, True),
        ("a transmit that keeps its T on a cut", {ALPHA.replace("/", "__"): LAWFUL_ALPHA.replace("( T ) = 0.0;", "( T ) = ( T );")},
         True),
        ("a ray initialised raw", {"src__rayquery__rq_job.comp": LAWFUL_JOB.replace("JAH_RQ_INIT(", "jahRqInit(")}, True),
        ("a guard that lets a negative tMin through", {ALPHA.replace("/", "__"): LAWFUL_ALPHA.replace(" && tMin >= 0.0", "")},
         True),
        ("a mirror that differs", {MIRROR.replace("/", "__"): LAWFUL_MIRROR.replace("1024u", "2048u")}, True),
        ("a raw loop inside a comment", {"src__rayquery__rq_quiet.comp": "/* " + raw + " */\n// while( jahRqProceed( q ) )\n"},
         False),
        ("the lawful shape", {}, False),
    ]
    ok = True
    for name, over, want_red in cases:
        root = synthetic(**over)
        try:
            red = bool(check(root, pending=None)[0])
        finally:
            shutil.rmtree(root, ignore_errors=True)
        good = red == want_red
        ok = ok and good
        print("  %s: the negative control - %s is %s" % ("ok" if good else "FAIL", name, "red" if red else "green"))
    return ok


LAWFUL_CLAMPED = {
    "src/rayquery/include/jah_rq_hit_direct.glsl": "const uint count = jahHitLightCount( T );\nconst uint count = "
    "jahHitLightCount( T );\nconst float capF = JAH_HIT_PARAMS.worldMax.w;\n"
    "const uint cap = finite1( capF ) ? uint( clamp( capF, 0.0, 16777216.0 ) ) : 0u;\n"
    "return min( min( T.u[0], e > b ? ( e - b ) / kJahHitLightWords : 0u ), cap );",
    "src/rayquery/rq_card_movers.comp": "for( uint i = 0u; i < min( lampCount.x, uint( lamps.length() ) ); ++i )",
    "src/rayquery/rq_card_gather.comp": "const int n = finite1( p.knobs.x ) ? clamp( int( min( p.knobs.x, 4096.0 ) ), 1, 4096 ) : 1;",
    "src/rayquery/rq_voxel_sides.comp": "const int rays = finite1( p.knobs.x ) ? clamp( int( min( p.knobs.x, 4096.0 ) ), 1, 4096 ) : 1;",
    "src/rayquery/include/jah_voxel_ray.glsl": "ivec3 jahVoxResAtLaw( vec3 res )\n{\n\treturn finite3( res ) ? clamp( ivec3( min( "
    "res, vec3( kJahVoxResMax ) ) + 0.5 ), ivec3( 1 ), ivec3( kJahVoxResMax ) ) : ivec3( 1 );\n}\n"
    "const ivec3 resI = jahVoxResAtLaw( res );\nconst int maxSteps = resI.x + resI.y + resI.z + 3;\n"
    "\tfor( int j = 0; j < JAH_VOX_COUNT; ++j )\nfor( ;; )\n{\n\tif( !sub && itMain >= maxSteps ) break;\n"
    "\tlost = itSub >= maxK;\n\tresIK = jahVoxResAtLaw( resK );\n\tmaxK = resIK.x + resIK.y + resIK.z + 3;\n}\n",
    "src/rayquery/rq_probe_integrate.comp": "const float youngFrames = finite1( p.young.x ) ? p.young.x : 0.0;\n"
    "const float youngReach = finite1( p.young.y ) ? clamp( p.young.y, 1.0, 64.0 ) : 1.0;",
}


def self_test_counts():
    """law (6)'s negative control: a clamp removed, a pending loop gone, the list grown (each red); the lawful shape green"""
    d = tempfile.mkdtemp(prefix="ray-counts-")
    try:
        one = os.path.join(d, "one.txt")
        with open(one, "w") as fh:
            fh.write("# a header line\nsrc/rayquery/include/jah_voxel_ray.glsl\tfor( int j = 0;   j < JAH_VOX_COUNT; ++j )\n")
        many = os.path.join(d, "many.txt")
        with open(many, "w") as fh:
            fh.write("src/rayquery/include/jah_voxel_ray.glsl\tfor( int j = 0; j < JAH_VOX_COUNT; ++j )\n" * (kPendingAtMost + 1))
        gone = os.path.join(d, "gone.txt")
        with open(gone, "w") as fh:
            fh.write("src/rayquery/rq_share_law.comp\tfor( int it = 0; it < n; ++it )\n")
        unclamped = dict(LAWFUL_CLAMPED)
        unclamped["src/rayquery/rq_card_movers.comp"] = "for( uint i = 0u; i < lampCount.x; ++i )"
        vox = "src/rayquery/include/jah_voxel_ray.glsl"
        rawsub = dict(LAWFUL_CLAMPED)   # F1: the segment's resolution raw
        rawsub[vox] = LAWFUL_CLAMPED[vox].replace("resIK = jahVoxResAtLaw( resK );", "resIK = ivec3( resK + 0.5 );")
        newexit = dict(LAWFUL_CLAMPED)   # a third exit nobody named
        newexit[vox] = LAWFUL_CLAMPED[vox].replace("\tlost = itSub >= maxK;", "\tlost = itSub >= maxK || itFar >= farK;")
        newloop = dict(LAWFUL_CLAMPED)   # a fifth march, an unbounded form nobody named
        newloop["src/rayquery/rq_card_gather.comp"] += "\nwhile( true ) { if( itX >= n ) break; }"
        cases = [("a clamp removed (the card movers' lamps)", unclamped, one, True),
                 ("law (7): a finer segment's resolution raw (F1)", rawsub, one, True),
                 ("law (7): an unnamed exit in the unbounded march", newexit, one, True),
                 ("law (7): a new unbounded loop form", newloop, one, True),
                 ("a pending loop gone from its file", LAWFUL_CLAMPED, gone, True),
                 ("the list grown past its bound", LAWFUL_CLAMPED, many, True),
                 ("the lawful counts", LAWFUL_CLAMPED, one, False)]
        ok = True
        for name, texts, pending, want_red in cases:
            red = bool(check_counts(d, texts, pending))
            good = red == want_red
            ok = ok and good
            print("  %s: the negative control - %s is %s" % ("ok" if good else "FAIL", name, "red" if red else "green"))
        return ok
    finally:
        shutil.rmtree(d, ignore_errors=True)


def main():
    if len(sys.argv) != 2:
        print("usage: ray_loops_bounded.py <source root>")
        print("source.ray_loops_bounded: FAILED (1)")
        return 2
    fails = 0 if self_test() else 1
    fails += 0 if self_test_counts() else 1
    bad, loops = check(sys.argv[1])
    for b in bad:
        print("  FAIL: " + b)
    if not bad:
        print("  ok: one jahRqProceed(, in JAH_RQ_LOOP_BEGIN_, cut at kJahRqStepBound = 4096 (the law 1024); the host's "
              "mirror equal; every cut answered as occluded; every ray through JAH_RQ_INIT; the counts at their laws (at "
              "most %d pending); %d ray query loops through the counted family:" % (kPendingAtMost, len(loops)))
        for l in loops:
            print("    " + l)
    fails += len(bad)
    sys.stdout.flush()
    print("source.ray_loops_bounded: " + ("PASSED" if not fails else "FAILED (%d)" % fails))
    return 0 if not fails else 1


if __name__ == "__main__":
    sys.exit(main())
