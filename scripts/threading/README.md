# Threading measurement harnesses

Sibling of `scripts/shadercache/`, same rules and the same shape of output.
Everything here backs a gate in `SPECS/THREADING_ADOPTION_SPEC.md`.

## `texture-ab.sh` — batched texture loading (gate G2-c)

The A/B behind phase P2. **A** is the pre-P2 behaviour, reachable at run time
and nothing else; **B** is the new default. The binary is IDENTICAL between the
arms — everything that distinguishes them is an environment variable, so no
result can come from a stale build.

| | A (the old way) | B (the default since P2) |
|---|---|---|
| `JAH_TEXTURE_SYNC_LOAD` | `1` — `loadTexture` blocks per texture | unset — schedule; the frame edge waits once |
| `JAH_TEXTURE_MULTILOAD` | `0` — one decode thread | unset — `clamp(cores/2, 2, 6)` |
| `JAH_TEXTURE_CACHE` | `0` — no metadata cache, no channel sidecar | unset — both |

```
scripts/threading/texture-ab.sh --display :N --pairs 5
scripts/threading/texture-ab.sh --display :N --pairs 5 --keep --out /tmp/ab
```

**The scene is generated, not committed.** `gen-textures.py` writes 48 distinct
2048² RGBA PNGs (~570 MB) with the standard library only, and
`build-texture-scene.js.in` imports them through the asset store onto 12 cubes,
four map slots each. Committing half a gigabyte of art whose only property is
"it takes real work to decode" would be the wrong trade.

### The three rules this harness exists to enforce

1. **`HOME` is pinned and PRIMED, not fresh per run.** A fresh `HOME` means a
   cold shader cache, and a cold shader cache means every run also compiles a
   few dozen shaders — the number P1's A/B already measured. Interaction I-3 in
   the spec is explicit that P1 and P2 must be measured separately or neither
   number can be attributed. Two unmeasured priming runs (one per arm) come
   first; the scratch `HOME` is still thrown away at the end.
2. **The numbers come from the app's own instruments, never a stopwatch.**
   `app.openTimings()` (LoadTimeline), `app.frameStats()` and
   `app.waitForTextures()`. A shell's wall clock around a process launch
   measures Qt starting up and the DB opening as well.
3. **Median and spread, never a mean.** Five pairs on a box with other work on
   it is exactly the sample size where one outlier moves a mean and moves no
   median.

### Reading the output

`waitedMs` is the one that needs a word: it is what the frame edge blocked on
AFTER the first frame. In arm A it is ~0 *by construction* — all the waiting
already happened, one texture at a time, inside `loadTexture`. A low `waitedMs`
in arm A is not arm A winning; it is arm A having already paid.

`loadRequests` under 40 means the run did not open what it thought it did (the
fixture failed, or the project opened from a stale cache) and the numbers should
be thrown away.

### Recorded: 2026-09-07, Linux, Debug, RTX 4080 SUPER / 595.84, 5 pairs

`texture-ab.sh --pairs 5`, 48 textures at 2048², 12 materials, 49 load requests
per open in both arms.

| metric | A (pre-P2) | B (default) | B/A |
|---|---|---|---|
| openMs (LoadTimeline total) | **18674** (18629..18727) | **1838** (1828..1840) | 0.10x |
| firstFrameMs | 18706 (18662..18759) | 1873 (1862..1896) | 0.10x |
| slowFrames (>100 ms ticks) | 1 | 0 | — |
| worstMs | 104.7 | 94.8 | 0.91x |

**A ten-fold difference is not one change.** Three single-knob arms, 3 runs
each, against the same fixture and the same B baseline of 1838 ms:

| arm | what is switched off | median openMs | cost vs B |
|---|---|---|---|
| C | `JAH_TEXTURE_SYNC_LOAD=1` (per-texture wait) | 9520 | +7.7 s |
| D | `JAH_TEXTURE_MULTILOAD=0` (one decode thread) | 9693 | +7.9 s |
| E | `JAH_TEXTURE_CACHE=0` (metadata + channel sidecar) | 9813 | +8.0 s |

Each one alone costs about eight seconds, and all three together cost eighteen
rather than twenty-four, because **they are three different ways of
serialising the same work**: the synchronous wait lets only one texture be in
flight however large the pool is; an empty pool decodes serially however well
the requests are batched; and without the channel sidecar every image is fully
decoded a second time on the calling thread before it is even scheduled (48 ×
2048² ≈ 7.7 s, which is where E's number comes from almost exactly).

The spec's prediction for G2-c was "wall-clock open time falls roughly with the
multiload thread count, and the number of long UI stalls falls more than the
total". The first half understates it — the three mechanisms compound — and the
second half holds (1 slow frame to 0).

## `warmup-ab.sh` — parallel warm-up compile (gate P4)

### Recorded: 2026-09-07, same box, 5 runs per arm, both modes

**No difference, and the reason is the finding.** Cold: gate = 252 ms (A) vs
251 ms (B), **0 shaders built in either arm**. Warm: identical.

The startup shader-build gate has nothing to parallelise on this tree:

1. By the time `holdSplashForShaderBuild` runs, 41 shaders have already been
   compiled — everything process-wide (Hlms registration, the low-level material
   scripts, the compositor chain) happens inside `MainWindow`'s constructor,
   before the gate. The gate's own frames build zero, and it spends its 252 ms
   waiting out `kSettleMs`.
2. The one thing that *would* give it real work — replaying the recorded warm-up
   set — needs a set AND a cold shader cache, and that combination cannot occur:
   **`ShaderCache::wipe()` deletes `warmup.set` along with the cache files**,
   because the set lives in the same directory. Verified directly: a run staged
   with a rich `warmup.set` and a deleted manifest logs no
   `Loading VertexFormatWarmUpStorage` line at all. On the one launch where the
   set would earn its keep — the first after an app update, a driver update or a
   Clear Cache — it has just been deleted. That is a pre-existing defect in the
   shader-cache/warm-up design, not something this phase introduced, and it is
   why P4(a) shows no win.

P4(a) is still correct and free (the tier is read once, for a scene created and
destroyed at startup); it is simply worth nothing until (2) is fixed.

**What parallel compile IS worth, measured on the path that does exercise it**
— the per-scene warm-up on the editor scene, which has run at `Tier::Primary`
all along and became parallel the moment P1 landed. Cold cache, fresh HOME,
Matcaps at Epic, 3 runs per arm, 72 shaders compiled either way:

| | open to third frame |
|---|---|
| `JAHSHAKA_SCENE_THREADS=1` (serial compile) | **2145 ms** (2135..2150) |
| default, 8 threads | **1919 ms** (1918..1931) |

**226 ms, 10.5% of a cold project open.** That is the number P4's tier change
was reaching for; it already applies everywhere except the startup gate.
