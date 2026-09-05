# Shader-cache measurement harness

`shadercache-bench.sh` is the only honest way to quote a warm-vs-cold number for this
app. Two rules it exists to enforce, both learned the hard way
(SPECS/SHADER_CACHE_SPEC.md §2.6, §11):

1. **The GPU driver keeps its own shader cache and it is worth ~0.65 s (~9%) per
   launch.** A benchmark that does not point `XDG_CACHE_HOME` at a scratch directory
   is measuring the NVIDIA driver, not us. This is Epic's `-clearPSODriverCache` rule
   restated for Linux.
2. **Our own cache lives under `QStandardPaths::AppDataLocation`**, which on Linux is
   `$XDG_DATA_HOME/Jahshaka` (default `$HOME/.local/share/Jahshaka`). The harness pins
   `HOME` as well, so "warm our cache / cold the driver's" is expressible — and it is
   the only combination that isolates what we built.

```
scripts/shadercache/shadercache-bench.sh --runs 3                # cold, every layer
scripts/shadercache/shadercache-bench.sh --mode warm --runs 3    # our cache warm, driver cold
scripts/shadercache/shadercache-bench.sh --mode warm-driver      # driver warm, our cache cold
scripts/shadercache/shadercache-bench.sh --probe                 # + the attribution probes
```

`--probe` builds and `LD_PRELOAD`s the two interposers next to this file and reports
where the time actually goes:

| Probe | What it times |
|---|---|
| `vkCreateShaderModule` | SPIR-V → VkShaderModule (measured: ~0 — it is never the cost) |
| `vkCreateGraphicsPipelines` / `vkCreateComputePipelines` | the driver's PSO/ISA compile — what layers 3+4 remove |
| `Ogre::VulkanProgram::compile` | GLSL parse + glslang + SPIR-V reflect — what layer 2 removes |
| `glslang::GlslangToSpv` | the GLSL→SPIR-V step alone, inside the above |
| `Ogre::Hlms::createShaderCacheEntry` | one Hlms shader end to end (template + compile + PSO) |
| `Ogre::Hlms::compileShaderCode` | Hlms template preprocessing + compile — what layer 1 removes |
| `initialiseAllResourceGroups` | script parsing (`.material`, `.program`, `.json`) |
| `probe-load -> exit` | the wall the probe itself sees, plus when the first/last compile happened |

**The interposition trap that cost the spec author their measurement** (§11: "0 calls
recorded and the process exited 139"): `RenderSystem_Vulkan.so` is `dlopen`'d
`RTLD_LOCAL`. Interposing a symbol it defines *works* (the global scope wins during
its relocation), but `dlsym(RTLD_NEXT, ...)` cannot see the real implementation to
chain to — it returns null and the wrapper jumps to 0. `sc-glslprobe.cpp` resolves the
real symbol out of the object itself with `dlopen(..., RTLD_NOLOAD)` instead. Symbols
in `libOgreNextMain.so` need no such workaround: the app links it directly, so it is
in the global scope and `RTLD_NEXT` is enough.

## Recorded baseline — Linux, RTX 4080 SUPER / driver 595.84, Debug build

`--engine-selftest`, cold driver cache, cold app cache, own Xvfb, scratch `HOME`:

| | value |
|---|---|
| Wall | 7.12 – 7.20 s |
| Shaders compiled (glslang) | 68 |
| PSOs created | 42 (28 graphics + 14 compute) |
| Time before the FIRST shader compile | 3.87 s |
| `VulkanProgram::compile` (all 68) | 0.53 s |
| ├ `glslang::GlslangToSpv` | 0.32 s |
| `vkCreate*Pipelines` (all 42) | 0.64 s |
| `vkCreateShaderModule` (all 68) | 0.0004 s |
| `Hlms::createShaderCacheEntry` (11) | 0.54 s |
| `initialiseAllResourceGroups` | 0.19 s |

**The attribution §11 could not make: shader + PSO work is ~1.2 s of a 7.1 s cold
selftest (≈17%), and 3.9 s of the run elapses before the first shader is touched.**
Of that 1.2 s, glslang is 0.32 s and driver PSO creation is 0.64 s — so the *driver*
half is the bigger one, and it is layer 3 (`VkPipelineCache`) that recovers it. No
combination of caches can make a cold selftest faster than ~5.9 s.

## Recorded results — the same box, after the program landed

`--engine-selftest`, three runs per mode, both caches pinned to scratch:

| mode | wall | shaders compiled | microcode hits |
|---|---|---|---|
| baseline (no cache at all, before this lane) | 7.17 s | 68 | 0 |
| cold (first ever launch) | 7.16 s | 65 | 3 |
| warm — our cache warm, DRIVER cache cold | 6.06 s | 0.7 | 71 |
| warm-all — both warm | 6.05 s | 0.7 | 71 |

Three things worth reading off that table:

1. **The cold path costs nothing.** 7.16 s against a 7.17 s pre-cache baseline —
   the fingerprinting, the checksums, the atomic writes and the startup gate all
   fit inside the noise.
2. **A warm launch is 1.10 s (15%) faster**, and that is the whole ceiling: the
   probes in the section above measured shader + PSO work at ~1.2 s of a 7.1 s
   cold start, and this recovers essentially all of it. `SHADER_CACHE_SPEC` §8's
   estimate of 7.2 s → 4.5–5.5 s was never reachable; the work simply is not
   there.
3. **`warm` and `warm-all` are the same number.** Our own pipeline cache fully
   substitutes for the NVIDIA driver's — the 0.67 s that used to arrive by
   accident now arrives under our control, and a driver update no longer looks
   like a performance regression.

The three microcode hits on a cold run are not a cache read: two shaders
generated from byte-identical source share one microcode entry in memory, and
the SMAA materials do it every launch. "Did the disk cache work" is answered by
`app.shaderCache().microcodeLoaded`, never by that number.

### And the number nobody was measuring: cold scene-open responsiveness

`open.responsive` reports the worst UI-thread gap during a threaded open of the
Showroom. Same build, same box, cache on versus off:

| | worst gap, cold open | worst gap, second open |
|---|---|---|
| shader cache ON | 477 – 479 ms | 510 ms |
| shader cache OFF | 2264 ms | 513 ms |

A cold open without the cache spends 2.3 seconds unable to answer the window,
because that is where the world's shaders get compiled. With the cache it is
under half a second. That is a bigger user-visible win than the 1.1 s of startup,
and it was not what the program set out to buy.

---

## The v2 fix wave (2026-09-06) — re-measured, because the old numbers changed meaning

`SHADER_CACHE_AUDIT.md` F1 found two independent defects in the warm-up: the
recorded permutation set was written only at shutdown, and it was replayed into
an empty, lightless, shadowless, 1x offscreen scene. F1(c) asked for the stale
"empty scene is better" measurement to be re-taken, because it was taken against
the unrecorded set and the wrong-pass-shape argument changes what it means.

### The tool

`scripts/shadercache/warmup-efficacy.js`, run through this directory's bench
harness (or by hand with `HOME` and `XDG_CACHE_HOME` pinned into a scratch
tree). It prints `EFFICACY-GATE` at the start of the script — i.e. **what had
been built by the time the window existed** — and `EFFICACY-TOTAL` at the end,
so `gate / total` is the fraction of a session's shader work that happened
behind the splash. Everything outside it is a compile the user could have seen.

### F1(c), re-taken: old warm-up versus new

This box, RTX 4080 SUPER, Debug+ASan build, scratch `HOME`, `--script`, a
library holding **two** worlds (Showroom + Matcaps) opened and CLOSED in turn.
Two worlds is what discriminates: with only one, the shutdown-only recording
happens to capture it and the two builds are within noise (98% either way).
Both columns are the SHIPPED per-scene warm-up route (re-taken after the
`CompositorPassWarmUp` route was switched to opt-in; the numbers did not move).

| launch | build | wall | gate built / session total | compiled AFTER the gate | `warmup.set` |
|---|---|---|---|---|---|
| 1 cold | old | 11.6 s | 70 / 103 | 22 | 0 B during the run |
| 1 cold | **new** | 10.4 s | 70 / 105 | 24 | **517 B, written during the OPEN** |
| 2 warm | old | 7.98 s | 105 / 117 | **6 compiled** | 427 B |
| 2 warm | **new** | 7.79 s | 109 / 119 | **4 compiled** | 701 B |
| 3 warm | old | 6.80 s | 113 / 117 (96.6%) | 0 compiled | **251 B — it SHRANK** |
| 3 warm | **new** | 6.80 s | 115 / 119 (96.6%) | 0 compiled | 812 B |

What to read off it:

1. **The empty-scene argument is dead, and it was never the big term.** Giving
   the warm view the editor's pass shape (shadows, a shadow-casting directional,
   the achieved sample count) costs ~130 ms of extra gate time on the first warm
   launch and two extra permutations, and it stops the replay building
   zero-light/no-shadow variants nothing draws. The old comment's "the quad
   added permutations the app never uses" was measured against a replay that had
   nothing to replay.
2. **The set stops forgetting.** The old recording SHRANK across sessions
   (427 → 251 bytes) because only worlds still open at quit were in it. The new
   one is written on every open and every close and grows with the library.
3. **Wall time is unchanged.** This is not a speed fix; it is a *when* fix. The
   number that moved is "shaders compiled after the window appeared" on the
   first warm launch: 6 → 4.
4. **The first-ever launch pays two more permutations** (105 vs 103 session
   total, 24 vs 22 compiled after the gate). Those are the lit and shadow-caster
   variants of the warm view itself, and they are variants the editor's own pass
   uses — which is the whole point of matching the pass shape. Stated rather
   than buried: on a machine that never opens a world twice, the fix costs two
   shaders and buys nothing.

### F3, re-measured: the per-scene precache defaults ON now

`open.responsive`, Showroom, worst UI-thread gap:

| | cold open | warm open |
|---|---|---|
| the figures that kept it OFF | 1723 – 1761 ms | 646 – 736 ms |
| **this build, precache ON** | **425.7 ms** | **381.8 ms** |

The warm open is now *below* the number recorded with the precache off
(439 – 476 ms), and the reason is the self-disarming idle check that was
already written: the one no-op warm-up is paid on the COLD open — which is
budgeted at 4000 ms and pays those compiles either way — and every open after
it is skipped. The 500 ms razor is not touched.

### F2, measured — and why the route it unblocks still ships OFF

Case 6 of the warm-up suite, run both ways (`shadercache.warmup` = the shipped
default, `shadercache.warmup_pass` = `JAHSHAKA_WARMUP_PASS=1`). A cutout cube
parked 40 units behind the camera, a material family nothing else in the
process has built:

| route | shaders compiled by `View::warmUpShaders()` |
|---|---|
| `CompositorPassWarmUp` (opt-in) | **2** |
| full-frame (shipped default) | **0** |

`SceneManager::warmUpShaders` runs no frustum test, so the pass sweeps every
render queue and every object regardless of where the camera is looking. That
is the whole prize, and ogre-patch 0016 is what makes the route run at all:
with the patch reversed and the engine rebuilt, the suite **SEGVs** on its very
first `warmUpShaders()` in `ForwardPlusBase::getGridBuffer` — the crash the
audit predicted, reproduced and removed.

**But there is a second one behind it, and it is a read-after-destroy.** Driven over
MCP on an Xvfb display, under gdb: the cold open is fine, and the SECOND world
opened in the session dies in `ParallelHlmsCompileQueue::warmUpSerial`
(`OgreRenderQueue.cpp:1333`) with `renderable->getDatablock()` null and the
request's `movableObject` unreadable — collected requests naming objects from a
world that has been destroyed. Nothing clears the pending request list when the
scene that filled it goes away, and neither `RenderQueue::warmUpShaders`
(`:1169`) nor `warmUpSerial` (`:1332`) null-checks. It does **not** reproduce
offscreen (the suite's case 7 rebinds three worlds on one view and survives),
which is why the route is default-off rather than test-gated: a suite would not
have caught it. Fixing it is a second patch and a second lane.
