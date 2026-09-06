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
