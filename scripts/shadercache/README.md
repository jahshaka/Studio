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
