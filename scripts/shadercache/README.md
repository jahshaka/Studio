# scripts/shadercache — the shader cache's tools

`make-seed.sh <cache-dir> <seed-dir>` makes THE SHIPPED MICROCODE SEED (COLD-BOOT-1 S1) from a warm shader cache; its
header says what a seed is and how the engine reads it (irisgl `engine/src/OgreShaderCache.cpp`, `readSeed`). A build
carries one at `<build>/bin/shadercache-seed`; `JAHSHAKA_SHADER_SEED=<dir>` names another for one process and
`JAHSHAKA_SHADER_SEED=none` turns it off. `app.shaderCache()` reports `seedDir`, `seedState` and `seedEntries`.

## Rules for any shader-cache measurement

1. **The GPU driver keeps its own pipeline cache, and it is most of a cold launch**: a warm driver cache took the
   startup gate from 26.5 s to 10.3 s (COLD-BOOT-1 S0, 2026-10-25; it was ~9 % of a launch on the 2026-09 set). A run
   that does not point `XDG_CACHE_HOME` at a scratch directory is measuring the NVIDIA driver, not us.
2. **Our own cache lives under the data root** (`<data-root>/shadercache`: `--data-root`, `JAHSHAKA_DATA_ROOT`). Pin
   `HOME` as well, so "our cache warm, the driver's cold" is expressible. A build that carries a seed serves a cold
   data root's SPIR-V from it: an unseeded cold run says `JAHSHAKA_SHADER_SEED=none`.
3. **An `LD_PRELOAD` interposer of a symbol in `RenderSystem_Vulkan.so` cannot chain through `RTLD_NEXT`**: the
   plugin is `dlopen`'d `RTLD_LOCAL`, so the interposition takes (the global scope wins during its relocation) but
   `dlsym(RTLD_NEXT, ...)` returns null and the wrapper jumps to 0 (exit 139, "0 calls recorded"). Resolve the real
   symbol out of the plugin itself (`dlopen(..., RTLD_NOLOAD)`, then `dlsym` on that handle). `libOgreNextMain.so`
   needs no workaround: the app links it, so it is in the global scope.

The harness that lived here — `shadercache-bench.sh`, its two `LD_PRELOAD` probes and `warmup-efficacy.js` — was deleted
in COLD-BOOT-1 S1: no row or tool ran it, its Ogre probe declared `VulkanProgram::compile` `void` where the fork returns
`bool` and summed into unguarded counters from ten compile threads, and the efficacy script called a deleted verb
(`app.warmUpSet`). Its recorded baselines are in git history and `SPECS/SHADER_CACHE_SPEC.md`.
