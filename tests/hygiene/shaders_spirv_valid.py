#!/usr/bin/env python3
# source.shaders_spirv_valid (URGENT #89, PHOTON-7 10-4; REBUILT on the engine's own bytes, NONDET-1's F3, 2026-11-21) - EVERY
# SHADER MODULE THE ENGINE RAN IS VALID SPIR-V, NOT ONLY WHAT THE DRIVER ACCEPTS. An invalid module the NVIDIA driver swallows
# reaches a merge unseen: the fog march's compare tap (irisgl d7582e77) gave the global shadow texture a depth image type that
# getShadow's parameter did not have, five of the self-test's 100 pixel shaders passed a mismatched OpFunctionCall argument,
# and only the validation layer's row (app.engine_selftest_validation, VUID-VkShaderModuleCreateInfo-pCode-08737) saw it.
#
# WHAT IT READS - THE BYTES THE ENGINE CREATED ITS MODULES FROM, at the build's cache key:
#   (1) every entry of the tree's two warm MICROCODE CACHES - <build>/tests/.shadercache/microcode.cache (the app's warm-up)
#       and <build>/tests/.shadercache-engine/microcode.cache (the engine rows'): Ogre's file (uint32 count; per entry a
#       16-byte hash, a uint32 length, the blob), each blob the fork's JRL0 frame (OgreVulkanProgram.cpp:317-430: the magic
#       'JRL0', the per-binding-type array-range keys, the SPIR-V) - vertex, pixel AND COMPUTE (Hlms compute, Photon, the
#       gather's Hlms side): the old row re-compiled the app warm-up's GLSL DUMP, which holds no compute module at all
#       (JAHSHAKA_HLMS_DEBUG_DIR reaches the PBS and Atom Hlms only, OgreEngine.cpp:4296-4298; NONDET-1 F3);
#   (2) THE RAY TIER's BUILT-IN SPIR-V, compiled into the engine at build time (<build>/engine/generated/rayquery/*_spv.h,
#       glslangValidator --target-env spirv1.4): no disk cache holds it.
# A cache whose warm stamp is not this build's key is REFUSED (run scripts/warm-app-cache.sh <build>; spec.sh warms first).
# WHAT IT DOES: each module (deduplicated by its bytes) through spirv-val --relax-block-layout --target-env vulkan1.2 - the
# validation layer's own check - in parallel over our CPUs; its stage read from its OpEntryPoint. Every invalid module is
# RED, named (its source, its cache hash, its stage, spirv-val's line); a frame the parser cannot read is RED (a format the
# row no longer understands validates nothing). FIRST, THE NEGATIVE CONTROLS: the fault's own pattern, compiled by the
# build's glslang and wrapped in a synthetic JRL0 frame of a synthetic cache, must be found INVALID through the same path; a
# truncated cache must be refused.
# WHAT THE DELETED GLSL DUMP PROVED THAT THE BLOBS DO NOT: ONE thing - that each dumped vertex/pixel permutation COMPILES under
# our emulated preamble (a permutation the engine failed to compile leaves no module in any cache; the engine counts its compile
# failures itself: the self-test's line, quoted with the six). Nothing else: its coverage was a subset (the app warm-up's vertex
# and pixel shaders only), and a module no warm-up and no row created is in neither the dump nor a cache.
import concurrent.futures, glob, hashlib, os, re, shutil, struct, subprocess, sys, tempfile

src, build = sys.argv[1], sys.argv[2]
glslang = sys.argv[3] if len(sys.argv) > 3 else (shutil.which("glslangValidator") or "/usr/bin/glslangValidator")
spirv_val = shutil.which("spirv-val")
tests = os.path.join(build, "tests")
CACHES = (("app", os.path.join(tests, ".shadercache", "microcode.cache"), "app-off"),
          ("engine", os.path.join(tests, ".shadercache-engine", "microcode.cache"), "engine-off"))
JRL0 = 0x304C524A
MODELS = {0: "vert", 1: "tesc", 2: "tese", 3: "geom", 4: "frag", 5: "comp", 5267: "task", 5268: "mesh"}


def fail(msg):
    print("FAIL: " + msg)
    sys.exit(1)


if not spirv_val or not os.path.isfile(glslang):
    fail("needs glslangValidator (%s) and spirv-val (spirv-tools)" % glslang)


def read_cache(path):
    """[(hash hex, spirv bytes)] of an Ogre microcode.cache of JRL0 frames; raises ValueError naming the first entry the
    parser cannot read (a short file, a frame with no JRL0 magic, a SPIR-V size past its blob, trailing bytes)."""
    data = open(path, "rb").read()
    if len(data) < 4:
        raise ValueError("%s: %d bytes, no entry count" % (path, len(data)))
    n = struct.unpack_from("<I", data, 0)[0]
    off, out = 4, []
    for i in range(n):
        if off + 20 > len(data):
            raise ValueError("%s: entry %d of %d starts past the end (%d of %d bytes)" % (path, i, n, off, len(data)))
        h = data[off:off + 16].hex()
        ln = struct.unpack_from("<I", data, off + 16)[0]
        off += 20
        blob = data[off:off + ln]
        if len(blob) != ln:
            raise ValueError("%s: entry %d (%s) declares %d bytes, %d left" % (path, i, h[:16], ln, len(blob)))
        off += ln
        if len(blob) < 8 or struct.unpack_from("<I", blob, 0)[0] != JRL0:
            raise ValueError("%s: entry %d (%s) is not a JRL0 frame" % (path, i, h[:16]))
        o, nt = 8, struct.unpack_from("<I", blob, 4)[0]
        for _ in range(nt):
            c = struct.unpack_from("<I", blob, o)[0]
            o += 4 + 4 * c
        sz = struct.unpack_from("<I", blob, o)[0]
        o += 4
        if o + sz != len(blob):
            raise ValueError("%s: entry %d (%s): its SPIR-V (%d bytes at %d) does not end its frame (%d)" % (
                path, i, h[:16], sz, o, len(blob)))
        out.append((h, blob[o:o + sz]))
    if off != len(data):
        raise ValueError("%s: %d trailing bytes after %d entries" % (path, len(data) - off, n))
    return out


def stage_of(spv):
    """The first OpEntryPoint's execution model, else '?'."""
    if len(spv) < 20 or struct.unpack_from("<I", spv, 0)[0] != 0x07230203:
        return "?"
    o = 20
    while o + 4 <= len(spv):
        w = struct.unpack_from("<I", spv, o)[0]
        wc, op = w >> 16, w & 0xFFFF
        if wc == 0:
            return "?"
        if op == 15:
            return MODELS.get(struct.unpack_from("<I", spv, o + 4)[0], "?")
        o += 4 * wc
    return "?"


def validate(spv):
    """None valid; else spirv-val's first two lines."""
    with tempfile.NamedTemporaryFile(suffix=".spv") as f:
        f.write(spv)
        f.flush()
        v = subprocess.run([spirv_val, "--relax-block-layout", "--target-env", "vulkan1.2", f.name], capture_output=True,
                           text=True)
    return None if v.returncode == 0 else " | ".join((v.stderr or v.stdout).strip().split("\n")[:2])


def jrl0_cache(path, spvs):
    """A synthetic microcode.cache of JRL0 frames (no binding-type ranges) - the negative controls' input."""
    with open(path, "wb") as f:
        f.write(struct.pack("<I", len(spvs)))
        for i, spv in enumerate(spvs):
            blob = struct.pack("<II", JRL0, 0) + struct.pack("<I", len(spv)) + spv
            f.write(hashlib.md5(b"%d" % i).digest() + struct.pack("<I", len(blob)) + blob)


# ---- THE NEGATIVE CONTROLS: the fault's own pattern (a global texture used directly in a comparison sampler's constructor
# and passed to a function's texture2D parameter), compiled by the build's glslang, wrapped in a JRL0 frame, read back by
# read_cache and found INVALID; and a truncated cache refused
CONTROL = """#version 460 core
layout( set = 0, binding = 0 ) uniform texture2D texShadowMap0;
layout( set = 0, binding = 1 ) uniform samplerShadow shadowSampler;
layout( location = 0 ) out vec4 outColour;
float tap( texture2D t, samplerShadow s, vec2 uv, float d ) { return texture( sampler2DShadow( t, s ), vec3( uv, d ) ); }
void main()
{
    float direct = texture( sampler2DShadow( texShadowMap0, shadowSampler ), vec3( 0.5, 0.5, 0.5 ) );
    outColour = vec4( direct + tap( texShadowMap0, shadowSampler, vec2( 0.5 ), 0.5 ) );
}
"""
with tempfile.TemporaryDirectory(prefix="spv-control-") as d:
    g, o, c = os.path.join(d, "c.frag"), os.path.join(d, "c.spv"), os.path.join(d, "microcode.cache")
    open(g, "w").write(CONTROL)
    r = subprocess.run([glslang, "-V", "-S", "frag", "-o", o, g], capture_output=True, text=True)
    if r.returncode != 0 or not os.path.isfile(o):
        fail("the negative control does not compile with %s: %s" % (glslang, r.stdout.strip()[-200:]))
    jrl0_cache(c, [open(o, "rb").read()])
    mods = read_cache(c)
    why = validate(mods[0][1]) if len(mods) == 1 else "read %d modules" % len(mods)
    if why is None or stage_of(mods[0][1]) != "frag":
        fail("the negative control (the fog march's fault pattern, in a JRL0 frame) was found VALID or not a pixel shader "
             "(%s): the check cannot see the class it guards" % stage_of(mods[0][1]))
    print("  ok   the negative control (the fault's pattern, framed as the engine frames it) is found invalid: %s" % why[:140])
    data = open(c, "rb").read()
    open(c, "wb").write(data[:len(data) - 7])
    try:
        read_cache(c)
        fail("a truncated cache was READ: the parser would validate a part of a module and call it whole")
    except ValueError as e:
        print("  ok   a truncated cache is refused: %s" % str(e).split(": ", 1)[-1][:120])

# ---- THE CACHES, each at the current key
modules = {}   # spirv sha -> [stage, [(source, hash)], spv]
for name, path, boot in CACHES:
    r = subprocess.run(["bash", os.path.join(src, "scripts", "warm-app-cache.sh"), build, "--check", "--only", boot],
                       capture_output=True, text=True)
    if r.returncode != 0:
        fail("the %s cache is not warm at this build's key (%s): run scripts/warm-app-cache.sh %s --only %s - its boot fills "
             "the cache this row validates" % (name, (r.stdout.strip() or r.stderr.strip())[:200], build, boot))
    if not os.path.isfile(path):
        fail("no %s microcode cache at %s although its warm-up says warm" % (name, path))
    try:
        entries = read_cache(path)
    except ValueError as e:
        fail("the %s cache cannot be read: %s" % (name, e))
    for h, spv in entries:
        k = hashlib.sha256(spv).hexdigest()
        modules.setdefault(k, [stage_of(spv), [], spv])[1].append((name, h[:16]))
# ---- THE RAY TIER's BUILT-INS
rq = sorted(glob.glob(os.path.join(build, "engine", "generated", "rayquery", "*_spv.h")))
if not rq:
    fail("no ray-tier SPIR-V under %s/engine/generated/rayquery (build the tree)" % build)
for f in rq:
    words = [int(w, 16) for w in re.findall(r"0x[0-9a-fA-F]{8}", open(f).read())]
    spv = struct.pack("<%dI" % len(words), *words)
    k = hashlib.sha256(spv).hexdigest()
    modules.setdefault(k, [stage_of(spv), [], spv])[1].append(("rayquery", os.path.basename(f)[:-6]))
counts = {}
for st, srcs, _ in modules.values():
    for s in {s for s, _ in srcs}:
        counts.setdefault(s, {}).setdefault(st, 0)
        counts[s][st] += 1
if not counts.get("app", {}).get("frag") or not counts.get("engine", {}).get("comp"):
    fail("the caches hold no app pixel shader or no engine compute module (%s): not a warm-up's cache" % counts)
workers = len(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else (os.cpu_count() or 4)
bad = []
with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as ex:
    for k, why in zip(list(modules), ex.map(lambda k: validate(modules[k][2]), list(modules))):
        if why:
            bad.append((k, why))
for k, why in bad:
    st, srcs, _ = modules[k]
    print("  FAIL %s module %s (%s): %s" % (st, k[:16], ", ".join("%s %s" % s for s in srcs[:3]), why[:300]))
for s in sorted(counts):
    print("  %-8s %s" % (s, ", ".join("%s %d" % (st, n) for st, n in sorted(counts[s].items()))))
print("%s: %d distinct modules (the app and engine caches at their keys, the ray tier's %d built-ins), %d invalid" %
      ("FAIL" if bad else "PASS", len(modules), len(rq), len(bad)))
sys.exit(1 if bad else 0)
