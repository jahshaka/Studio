#!/usr/bin/env python3
# source.view_ray_origin — EVERY RAY THAT STARTS ON A POINT THE VIEW DREW TAKES THE ONE VIEW-RAY ORIGIN (GATHER-HANG-1
# stage 3; spikes/black-specks/DIAGNOSIS.md).
#
# WHAT WAS WRONG. The Forward+ lamp's visibility ray (jah_lamp_ray.glsl) and the glass escape's record
# (PhotonVct_piece_ps.any) started on the drawn surface lifted by the float bound alone - `jahRayOrigin( p, n, carried,
# 0.0 )` - while the sun contact, the reflection and the gather's probes added the drawn surface's bound
# (jahViewDrawnBound). A large triangle's own ray-triangle test rounds at the triangle's scale: the lamp ray hit the
# plane it started on at t ~ 1e-6 - 24 pure-black pixels in a lamp's highlight on an 80 m plane (lights.lamp_ray_origin).
#
# THE LAW, over every shader text under irisgl/engine (src/: .comp .glsl; media/: .glsl .any), comments stripped by
# webgpu_census.strip (the one stripper):
#   (1) jah_ray_origin.glsl defines `jahViewRayOrigin(` once, and its body calls jahRayOrigin( with jahViewDrawnBound(;
#   (2) every VIEW-RAY file (VIEW below) calls `jahViewRayOrigin(` and never `jahRayOrigin(` directly;
#   (3) the area visibility (POINT_CASE: its rays aimed at a light's strata through jah_area_strata.glsl's
#       jahAreaRayOrigin, an Hlms piece that cannot include jah_ray_origin.glsl) builds its lift with jahViewDrawnBound(
#       - its own sum, kept bit-equal (the lead's condition: a caller whose present offset one function cannot reproduce
#       exactly keeps its expression and calls the shared term);
#   (4) every other `jahRayOrigin(` call is in a file of CACHE (a ray that starts on a HIT or a stored CACHE point, on
#       level 0 or at its stored bound - jah_ray_origin.glsl's own cases): a new file calling it is RED until it is put
#       in VIEW or CACHE with its reason - nobody builds a ray origin without saying which kind of point it starts on.
# A NEGATIVE CONTROL runs first: the lamp ray with the bare form, an unlisted caller, a view file that lost the call and
# a shared function without the drawn term (each red), and the lawful shape (green).
#
# usage: view_ray_origin.py <source root>
import os
import re
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from webgpu_census import strip  # noqa: E402  (the one comment stripper)

ENGINE = os.path.join("irisgl", "engine")
ORIGIN = "src/rayquery/include/jah_ray_origin.glsl"
VIEW = {   # a ray from a point the view drew
    "src/rayquery/rq_sun_contact.comp": "the sun contact (a depth-rebuilt point)",
    "src/rayquery/rq_reflect.comp": "the reflection (a depth-rebuilt point)",
    "src/rayquery/rq_probe_gather.comp": "the gather's probes (a placed point)",
    "src/rayquery/rq_probe_integrate.comp": "the integrate's pixel rays (a depth-rebuilt point)",
    "src/rayquery/include/jah_lamp_ray.glsl": "the Forward+ lamp's ray (a rasterised point)",
    "media/Photon/Hlms/PhotonVct_piece_ps.any": "the glass escape's record (a rasterised point)",
}
POINT_CASE = {"src/rayquery/rq_area_vis.comp": "the area visibility's lift (its own sum: bit-equal)"}
CACHE = {   # a ray from a hit (on level 0) or a stored cache point (its stored bound)
    "src/rayquery/include/jah_rq_hit_record.glsl": "a hit's own sun leg",
    "src/rayquery/include/jah_rq_hit_radiance.glsl": "a hit's onward rays",
    "src/rayquery/include/jah_rq_hit_direct.glsl": "a hit's light rays",
    "src/rayquery/rq_voxel_sides.comp": "a voxel side's stored point",
    "src/rayquery/rq_card_movers.comp": "a card texel's captured point",
    "src/rayquery/rq_card_gather.comp": "a card texel's captured point",
}
SRC_EXTS = (".comp", ".glsl")
MEDIA_EXTS = (".glsl", ".any")
RAY_ORIGIN = re.compile(r"\bjahRayOrigin\s*\(")
VIEW_ORIGIN = re.compile(r"\bjahViewRayOrigin\s*\(")
DRAWN = re.compile(r"\bjahViewDrawnBound\s*\(")


def texts(root):
    eng = os.path.join(root, ENGINE)
    out = {}
    for sub, exts in (("src", SRC_EXTS), ("media", MEDIA_EXTS)):
        for d, _, files in os.walk(os.path.join(eng, sub)):
            for f in files:
                if f.endswith(exts):
                    p = os.path.join(d, f)
                    with open(p, encoding="utf-8", errors="replace") as fh:
                        out[os.path.relpath(p, eng).replace(os.sep, "/")] = strip(fh.read())
    return out


def judge(root):
    """The faults (empty = the law holds) and the view rays found."""
    t = texts(root)
    faults = []
    o = t.get(ORIGIN, "")
    heads = [m.start() for m in re.finditer(r"\bvec3\s+jahViewRayOrigin\s*\(", o)]
    if len(heads) != 1:
        faults.append(f"(1) {ORIGIN} defines jahViewRayOrigin {len(heads)} times (1 wanted)")
    else:
        body = o[heads[0]:o.find("}", heads[0])]
        if not (RAY_ORIGIN.search(body) and DRAWN.search(body)):
            faults.append("(1) jahViewRayOrigin's body does not call jahRayOrigin( with jahViewDrawnBound(")
    found = []
    for f, why in VIEW.items():
        s = t.get(f)
        if s is None:
            faults.append(f"(2) {f} ({why}) is gone: the VIEW table is stale")
            continue
        n_view, n_bare = len(VIEW_ORIGIN.findall(s)), len(RAY_ORIGIN.findall(s))
        if n_view == 0:
            faults.append(f"(2) {f} ({why}) calls no jahViewRayOrigin(")
        if n_bare:
            faults.append(f"(2) {f} ({why}) builds {n_bare} origin(s) by the bare jahRayOrigin( - not the one view-ray form")
        found.append((f, n_view))
    for f, why in POINT_CASE.items():
        s = t.get(f)
        if s is None or not DRAWN.search(s):
            faults.append(f"(3) {f} ({why}) builds its lift without jahViewDrawnBound(")
    for f, s in sorted(t.items()):
        if f == ORIGIN or f in VIEW:
            continue
        if RAY_ORIGIN.search(s) and f not in CACHE:
            faults.append(f"(4) {f} calls jahRayOrigin( and is in neither VIEW nor CACHE: say which kind of point it starts on")
    return faults, found


def control():
    """The negative control: synthetic trees, each red or green as named."""
    base = {
        ORIGIN: "vec3 jahRayOrigin( vec3 s, vec3 n, float c, float b ) { return s; }\n"
                "vec3 jahViewRayOrigin( vec3 w, vec3 n, float c, float e, JahViewDrawn d )\n"
                "{ return jahRayOrigin( w, n, c, e + jahViewDrawnBound( d.footprints, w, d.eyePos, d.rayDown, d.height, d.perspective ) ); }\n",
        "src/rayquery/rq_area_vis.comp": "float l = jahViewDrawnBound( a, b, c, d, e, f );\n",
    }
    for f in VIEW:
        base[f] = "vec3 o = jahViewRayOrigin( p, n, d, 0.0, v );\n"
    for f in CACHE:
        base[f] = "vec3 o = jahRayOrigin( p, n, d, 0.0 );\n"
    cases = [
        ("the lawful shape", {}, True),
        ("the lamp ray with the bare form (the defect)",
         {"src/rayquery/include/jah_lamp_ray.glsl": "vec3 o = jahRayOrigin( p, n, d, 0.0 );\n"}, False),
        ("an unlisted caller", {"src/rayquery/rq_new.comp": "vec3 o = jahRayOrigin( p, n, d, 0.0 );\n"}, False),
        ("a view file that lost its call", {"src/rayquery/rq_reflect.comp": "vec3 o = p;\n"}, False),
        ("the shared function without the drawn term",
         {ORIGIN: "vec3 jahRayOrigin( vec3 s, vec3 n, float c, float b ) { return s; }\n"
                  "vec3 jahViewRayOrigin( vec3 w, vec3 n, float c, float e, JahViewDrawn d ) { return jahRayOrigin( w, n, c, e ); }\n"},
         False),
        ("a bare call inside a comment only (green)",
         {"src/rayquery/rq_reflect.comp": "// jahRayOrigin( p, n, d, 0.0 )\nvec3 o = jahViewRayOrigin( p, n, d, 0.0, v );\n"},
         True),
    ]
    ok = True
    for name, change, lawful in cases:
        tmp = tempfile.mkdtemp(prefix="view_ray_origin_")
        try:
            for f, s in {**base, **change}.items():
                p = os.path.join(tmp, ENGINE, f)
                os.makedirs(os.path.dirname(p), exist_ok=True)
                with open(p, "w", encoding="utf-8") as fh:
                    fh.write(s)
            faults, _ = judge(tmp)
        finally:
            shutil.rmtree(tmp)
        good = (not faults) == lawful
        ok = ok and good
        print(f"  {'ok  ' if good else 'FAIL'} control: {name} -> {'green' if not faults else 'red'}", flush=True)
    return ok


def main():
    if len(sys.argv) != 2:
        print("usage: view_ray_origin.py <source root>")
        print("source.view_ray_origin: FAILED (1)")
        return 2
    fails = 0
    if not control():
        fails += 1
    faults, found = judge(sys.argv[1])
    for f, n in found:
        print(f"  view ray: {f} - {n} jahViewRayOrigin( call(s)", flush=True)
    for x in faults:
        print(f"  FAIL {x}", flush=True)
    fails += len(faults)
    print("source.view_ray_origin: PASSED" if not fails else f"source.view_ray_origin: FAILED ({fails})", flush=True)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
