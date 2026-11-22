#!/usr/bin/env python3
"""shadercache.identity_print - THE ENGINE PRINTS THE STARTUP PASS's IDENTITY WITH NO DEVICE (SHADER-SYSTEM-1 stage 6a;
WARM-SHARE-1 call 4: the harness keys its warm state on this string and never re-derives it).

`Jahshaka --print-shader-identity` (src/app/main.cpp -> printShaderIdentity, src/app/shaderbuildgate.cpp) prints the
engine's identity lines (jahshaka::engine::shaderIdentityText) and the key the startup pass writes into warm-pass.key - startupPassKey(the media digest): the pass's code id, the staged media
by content, the shader cache's build terms (jahshaka::engine::shaderIdentity) - and its parts. No device term is in it:
a driver or a card is seen by the cache's load (ShaderCacheStats::layersNotLoaded), which runs the pass.

THE LAWS (CPU: no display, no card, a HOME of its own that stays empty):
  1  it answers with no display and no data root: exit 0, its HOME untouched, no Vulkan driver mapped (main.cpp's exit 3)
  2  the format: `shader-identity 1`, then layer.pipeline, layer.microcode, layer.hlms, layer.compute, cache, engine,
     ray-spirv, media, engine-identity (the engine's lines), pass-code, identity (the app's) - one `<name> <value>` line
     each, in that order
  3  engine / ray-spirv / pass-code are the BUILD's generated ids (jahshaka_engine_build_id.h, jah_startup_pass_id.h)
  4  JAHLAB prints the engine's lines with the same values (one engine, one media/), nothing of the app's; and
     engine-identity = "engine=<engine>|ray=<ray-spirv>|media=<media>|cache=<cache>" (the digest's own laws:
     engine.shader_cache_keys law 5)
  5  identity = "pass=<pass-code>|media=<media>|cache=<cache>" - startuppass::passKey's text, the record the gate writes
  6  the CONTROL: a second run prints the same bytes (no time, no path, no device in it)
usage: identity_print.py <Jahshaka binary> <jahlab binary> <build dir>"""
import os
import re
import subprocess
import sys
import tempfile

exe, lab, build = sys.argv[1], sys.argv[2], sys.argv[3]
fails = []
ENGINE = ["shader-identity", "layer.pipeline", "layer.microcode", "layer.hlms", "layer.compute", "cache", "engine",
          "ray-spirv", "media", "engine-identity"]
NAMES = ENGINE + ["pass-code", "identity"]


def law(ok, text):
    print(("  ok   " if ok else "  FAIL ") + text, flush=True)
    if not ok:
        fails.append(text)


def define(path, name):
    m = re.search(r'#define %s "([^"]*)"' % name, open(path).read())
    return m.group(1) if m else None


def run(home, binary=None):
    env = {k: v for k, v in os.environ.items() if k not in ("DISPLAY", "WAYLAND_DISPLAY", "QT_QPA_PLATFORM")}
    env["HOME"] = home
    env.pop("JAHSHAKA_DATA_ROOT", None)
    return subprocess.run([binary or exe, "--print-shader-identity"], env=env, capture_output=True, text=True, timeout=25)


home = tempfile.mkdtemp(prefix="identity_print.")
try:
    r = run(home)
    law(r.returncode == 0, "1  exit 0 with no display and no data root (exit %d%s)"
        % (r.returncode, (": " + r.stderr.strip()[-300:]) if r.returncode else ""))
    law(os.listdir(home) == [], "1  its HOME stays empty (%s)" % os.listdir(home))
    lines = r.stdout.splitlines()
    pairs = [l.split(" ", 1) for l in lines]
    law([p[0] for p in pairs] == NAMES and all(len(p) == 2 and p[1] and " " not in p[1] for p in pairs),
        "2  the format: %d lines, names %s" % (len(lines), [p[0] for p in pairs]))
    v = {p[0]: p[1] for p in pairs if len(p) == 2}
    law(v.get("shader-identity") == "1", "2  format 1 (%s)" % v.get("shader-identity"))
    idh = os.path.join(build, "engine", "generated", "jahshaka_engine_build_id.h")
    pch = os.path.join(build, "generated", "jah_startup_pass_id.h")
    law(v.get("engine") == define(idh, "JAHSHAKA_ENGINE_BUILD_ID") and
        v.get("ray-spirv") == define(idh, "JAHSHAKA_RAY_SPIRV_ID") and v.get("ray-spirv") not in (None, ""),
        "3  engine and ray-spirv are the build's (%s / %s)" % (v.get("engine"), v.get("ray-spirv")))
    law(v.get("pass-code") == define(pch, "JAH_STARTUP_PASS_CODE_ID"), "3  pass-code is the build's (%s)" % v.get("pass-code"))
    lr = run(home, lab) if lab else subprocess.CompletedProcess([], 0, "\n".join(l for l in r.stdout.splitlines()
                                                                                   if l.split(" ")[0] in ENGINE), "")
    if not lab:
        print("  (no jahlab on this platform: its half of law 4 reads the app's engine lines)")
    lpairs = [l.split(" ", 1) for l in lr.stdout.splitlines()]
    lv = {p[0]: p[1] for p in lpairs if len(p) == 2}
    law(lr.returncode == 0 and [p[0] for p in lpairs] == ENGINE and all(lv[n] == v.get(n) for n in ENGINE),
        "4  jahlab prints the engine's %d lines with the app's values, nothing of the app's (exit %d)"
        % (len(ENGINE), lr.returncode))
    law(v.get("engine-identity") == "engine=%s|ray=%s|media=%s|cache=%s"
        % (v.get("engine"), v.get("ray-spirv"), v.get("media"), v.get("cache")) and len(v.get("media", "")) == 32,
        "4  engine-identity = engine=<engine>|ray=<ray-spirv>|media=<media>|cache=<cache>, media 32 hex")
    law(v.get("identity") == "pass=%s|media=%s|cache=%s" % (v.get("pass-code"), v.get("media"), v.get("cache")),
        "5  identity = pass=<pass-code>|media=<media>|cache=<cache> (the record the gate writes)")
    again = run(home)
    law(again.returncode == 0 and again.stdout == r.stdout, "6  CONTROL: a second run prints the same bytes")
finally:
    for d, _, fs in os.walk(home, topdown=False):
        for f in fs:
            os.unlink(os.path.join(d, f))
        os.rmdir(d)

print("shadercache.identity_print: " + ("PASSED" if not fails else "FAILED (%d)" % len(fails)))
sys.exit(1 if fails else 0)
