#!/usr/bin/env python3
# source.shaders_spirv_valid (URGENT #89, PHOTON-7 10-4) - EVERY GENERATED SHADER IS VALID SPIR-V, NOT ONLY WHAT THE
# DRIVER ACCEPTS. An invalid module the NVIDIA driver swallows reaches a merge unseen: the fog march's compare tap
# (irisgl d7582e77) gave the global shadow texture a depth image type that getShadow's parameter did not have, five of
# the self-test's 100 pixel shaders passed a mismatched OpFunctionCall argument, and only the validation layer's row
# (app.engine_selftest_validation, VUID-VkShaderModuleCreateInfo-pCode-08737) saw it, a day later.
#
# WHAT IT READS: the HLMS PERMUTATIONS THE TREE'S APP WARM-UP GENERATED - scripts/warm-app-cache.sh boots
# `Jahshaka --engine-selftest` (the app's startup warm-up gate, the born World's set at Epic, plus the self-test's six
# poses) with JAHSHAKA_HLMS_DEBUG_DIR=<build>/tests/.hlms-dump/ and stamps the dump with the cache key it was made at.
# The row refuses a dump that is not the current key's (a cold cache: run scripts/warm-app-cache.sh <build>; every
# spec.sh call warms it first).
# WHAT IT DOES: each dumped vertex and pixel shader through glslang (the build's) with Ogre's Vulkan preamble emulated
# (OgreVulkanProgram.cpp getPreamble / addVertexSemanticsToPreamble; every root-layout slot macro given its own binding),
# then spirv-val --relax-block-layout --target-env vulkan1.2 - the validation layer's own check - in parallel; any
# compile failure or invalid module is red, named. FIRST, THE NEGATIVE CONTROL: the fault's own pattern, which must be
# found invalid (a lint that cannot fail guards nothing).
import concurrent.futures
import glob
import os
import re
import shutil
import subprocess
import sys
import tempfile

src, build = sys.argv[1], sys.argv[2]
glslang = sys.argv[3] if len(sys.argv) > 3 else (shutil.which("glslangValidator") or "/usr/bin/glslangValidator")
spirv_val = shutil.which("spirv-val")
tests = os.path.join(build, "tests")
dump = os.path.join(tests, ".hlms-dump")


def fail(msg):
    print("FAIL: " + msg)
    sys.exit(1)


if not spirv_val or not os.path.isfile(glslang):
    fail("needs glslangValidator (%s) and spirv-val (the Vulkan SDK's / spirv-tools)" % glslang)

PRE = ["#extension GL_EXT_samplerless_texture_functions : require", "#define vulkan_layout layout",
       "#define vulkan( x ) x", "#define vk_comma ,", "#define vkSampler1D sampler1D", "#define vkSampler2D sampler2D",
       "#define vkSampler2DArray sampler2DArray", "#define vkSampler3D sampler3D", "#define vkSamplerCube samplerCube"]
_k = 0
for _x in "BRtsTuUPC":
    for _i in range(64):
        PRE.append("#define ogre_%s%d set = 0, binding = %d" % (_x, _i, _k))
        _k += 1
VS_PRE = ["#define OGRE_POSITION location = 0", "#define OGRE_BLENDWEIGHT location = 1",
          "#define OGRE_BLENDINDICES location = 2", "#define OGRE_BLENDWEIGHT2 location = 3",
          "#define OGRE_BLENDINDICES2 location = 4", "#define OGRE_NORMAL location = 5", "#define OGRE_DIFFUSE location = 6",
          "#define OGRE_SPECULAR location = 7", "#define OGRE_BINORMAL location = 8", "#define OGRE_TANGENT location = 9",
          "#define OGRE_DRAWID location = 15u"] + ["#define OGRE_TEXCOORD%d location = %d" % (j, 16 + j) for j in range(8)]


def check(path, stage, text=None):
    """(name, None) valid; (name, why) not."""
    name = os.path.basename(path)
    lines = (text if text is not None else open(path, errors="replace").read()).split("\n")
    vi = [i for i, l in enumerate(lines) if l.strip().startswith("#version")]
    if not vi:
        return name, "no #version line"
    vi = vi[0]
    pre = PRE + (VS_PRE if stage == "vert" else [])
    with tempfile.TemporaryDirectory(prefix="spv-") as d:
        g, o = os.path.join(d, "s." + stage), os.path.join(d, "s.spv")
        open(g, "w").write("\n".join(lines[vi:vi + 1] + pre + lines[vi + 1:]))
        r = subprocess.run([glslang, "-V", "--target-env", "vulkan1.2", "-S", stage, "-o", o, g], capture_output=True,
                           text=True)
        if r.returncode != 0:
            errs = [l for l in r.stdout.split("\n") if "ERROR" in l][:2]
            return name, "glslang: " + " | ".join(errs)
        v = subprocess.run([spirv_val, "--relax-block-layout", "--target-env", "vulkan1.2", o], capture_output=True,
                           text=True)
        if v.returncode != 0:
            return name, "spirv-val: " + " | ".join((v.stderr or v.stdout).strip().split("\n")[:2])
    return name, None


# ---- THE NEGATIVE CONTROL: the fault's own pattern (a global texture used directly in a comparison sampler's constructor
# and passed to a function's texture2D parameter) must be found invalid
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
_, why = check("negative-control.frag", "frag", CONTROL)
if why is None:
    fail("the negative control (the fog march's fault pattern) was found VALID: the check cannot see the class it guards")
print("  ok   the negative control (the fault's pattern) is found invalid: %s" % why[:160])

# ---- THE DUMP, at the current key
key = None
r = subprocess.run(["bash", os.path.join(src, "scripts", "warm-app-cache.sh"), build, "--check"], capture_output=True,
                   text=True)
m = re.search(r"(?:warm|cold) at ([0-9a-f]{16})", r.stdout)
key = m.group(1) if m else None
if r.returncode != 0 or not key:
    fail("the shared app cache is not warm at this build's key (%s): run scripts/warm-app-cache.sh %s - its boot dumps "
         "the permutations this row validates" % ((r.stdout.strip() or r.stderr.strip())[:200], build))
stamp = os.path.join(dump, ".key")
if not os.path.isfile(stamp) or open(stamp).read().strip() != key:
    fail("no permutation dump at the current key %s (%s): run scripts/warm-app-cache.sh %s after removing its stamp "
         "%s/.shadercache.warm-key, or let the next spec.sh call warm" % (key, stamp, build, tests))
files = [(f, "vert") for f in sorted(glob.glob(os.path.join(dump, "*VertexShader_vs.glsl")))] + \
        [(f, "frag") for f in sorted(glob.glob(os.path.join(dump, "*PixelShader_ps.glsl")))]
if len([f for f, s in files if s == "frag"]) < 10:
    fail("the dump at %s holds %d pixel shaders - the warm-up's boot generates ~100" % (dump, len(files)))
bad = []
with concurrent.futures.ThreadPoolExecutor(max_workers=min(8, os.cpu_count() or 4)) as ex:
    for name, why in ex.map(lambda fs: check(*fs), files):
        if why:
            bad.append((name, why))
for name, why in bad:
    print("  FAIL %s: %s" % (name, why[:300]))
print("%s: %d shaders (%d vertex, %d pixel) of the warm-up's dump at key %s, %d invalid" %
      ("FAIL" if bad else "PASS", len(files), len([1 for f, s in files if s == "vert"]),
       len([1 for f, s in files if s == "frag"]), key, len(bad)))
sys.exit(1 if bad else 0)
