# The test gate — how it works (2026-09-09)

Jahshaka's ctest suite is ~300 rows (~280 real suites) and costs ~2560 suite-seconds; run whole
at `-j2` it takes 25 minutes of wall time, and most of that is three suites plus eleven
serial islands. Since 2026-09-09 the gate is TIERED: a change is gated on what it can break,
and the full run is a per-batch event. This file is the reference; `scripts/gate-scope.py`
carries the rules.

## 1. The tiers

| Tier | What runs | When | Who runs it |
|---|---|---|---|
| **SCOPED** | the suites `scripts/gate-scope.sh <base>..<tip>` selects from the touched paths | a lane's own gate; a merge of that lane | the lane (feature-/engine-builder), or gate-runner with the selection |
| **MERGE** | the command `python3 scripts/gate-scope.py --merge-tier [-j N]` prints — `ctest -j4 --timeout 120 --output-on-failure -LE` over `gate-scope.py`'s `NIGHTLY_LABELS` ∪ `TARGET_LABELS` (today: the wall-clock benches, label `benchmark` — their `--smoke` rows, label `benchmark-smoke`, DO run —, the ASan shader-cache attack `shadercache-attack`, and the target tests `photon-target` and `scale-target`, §1b/§1c). The set lives in the script ONLY; this doc never copies it (`source.gate_scope_rules` case 9 fails on a literal `-LE` list here). `--timeout 120` is the default for rows that set none; `-j 2` while another lane's gate is live. **Measured 488-680 s at -j4 on the last three push gates (§5 has the three runs and what the spread is; 475 s at push #19, 572 s on the main tree 2026-09-10 — the older figures are history and the suite count moves most weeks)** | a lane whose scope falls back (see §3), any merge the lead wants covered wider, and — while the full gate is under moratorium — the gate before a push | gate-runner |
| **PUSH** | the MERGE tier + the `--engine-selftest` sha256 lines — **FOUR of them since lane FENCE-1**: `pose 1`, `pose 2`, `pose B1 (rays)`, `pose B2 (no rays)`. Poses 1-2 are the default scene at the PLAIN grade; pose pair B is a purpose-built fixture (glossy floor, cascade crossing, emissive 3.0, mirror pillar) at the VIEWPORT grade, which is the only grade that carries the SSR prepass and therefore the ray tier. Quote all four. (The moratorium of 2026-09-09 lifted 2026-09-10 with the cleanup: the four nightly suites are NIGHTLY, not push, unless the batch touched their subject) | once per BATCH of merged lanes, before a push | gate-runner |
| **NIGHTLY** (after the cleanup) | scenegraph.benchmark `--assert`, shadercache.container_asan, the rigperf bench, **open.crash_soak** (OPEN-FRAMES-1: the async-open teardown repro twelve times under `glibc's built-in malloc checks + MALLOC_PERTURB_ (the real checker is an opt-in — it aborts under the NVIDIA GLX library)`; ~110 s, RUN_SERIAL, labelled `benchmark` because that label IS the nightly marker the MERGE/PUSH commands exclude — read one failure as "run it again", three as a regression of the open's slice-boundary drive) — the guards that need a quiet box or minutes of one process | once a day / before a tag, on a quiet box, and whenever a batch touched the open path, the scene teardown or the engine's resource handling | the lead |

Tiers are contracts: nobody hand-picks suites out of one. A lane says which tier it ran and
its selection; the lead's merge audit reads that line.

### 1b. TARGET TESTS — the `photon-target` label (PHOTON phase A, A1 §0; lane FENCE-1)

Today's picture is wrong in known, measured ways. A suite that FENCES today's picture has to
be re-anchored by every part that fixes one of them — which is how `gi.chain_face` came to
bracket a staircase and `gi.field_follows` came to assert an existence bar re-anchored
downwards. Under forward-building a suite states the CORRECT number, and a suite that cannot
pass until its part lands is a **target test**:

- it carries the ctest label **`photon-target`**, and its header states the PART that turns it
  green and today's measured value;
- it is EXCLUDED FROM PASS/FAIL, never from the run. `scripts/gate-scope.sh` selects it,
  prints it in a `TARGET TESTS` section, and runs it in a SECOND ctest invocation at `-j1`
  (it is a measurement; a measurement sharing the GPU with three siblings prints a number
  nobody can use) whose exit code is reported and discarded. The MERGE and PUSH tiers drop it
  with `-LE`, which is the only shape ctest offers for "these do not decide the tier";
- every run prints `target: <value> (bar <bar>) <what>`, so the distance to the bar is visible
  from any lane's scoped gate and a target that goes green EARLY is noticed rather than
  sitting red for a month;
- **removing the label — one line in the suite's CMake row — IS the part's acceptance.** The
  lane that fixes the term deletes the label and, where an ordinary row carries a "no
  regression while red" bracket beside it, deletes the bracket too.

The label's definition lives in ONE place, `TARGET_LABELS` in `scripts/gate-scope.py`, and the
MERGE tier's `-LE` alternation is built from it, so the tool and the tier command cannot
disagree. `source.gate_scope_rules` case 6 is the guard.

A suite that mixes a target claim with correct claims becomes **two ctest rows over one
binary** (`--target`), never one labelled suite: a ctest label is per SUITE, so labelling the
whole thing would exclude the correct assertions from pass/fail as well. The rows registered
today are `gi.chain_face_target`, `gi.field_follows_energy` and `gi.volume_edge_spec_target`.
PHOTON-WRITER-1 retired three: `gi.chain_converge_target` (its claim - one at-rest sweep is the
fixed point - is the ordinary row's byte-equality assertion), `gi.field_follows_ground_target` and
`gi.chain_face_sky_target` (both gated in their ordinary rows since the field's probe rays cross
the voxels as rays and a probe behind the shaded point has no say).
`gi.rt_reflect_lamp_clip` was the fourth and **VOXEL-CLIP-1 took its label off** (2026-09-22,
ogre-patch 0087) — with a note worth keeping, because it is about the INSTRUMENT: a target test
has to be answerable through the thing that reads it. That row asked a PERFECT mirror to read
radiance 3.0 out of an offscreen view whose render target is `PFG_RGBA8_UNORM`
(`OgreView::createRtt`), so its pixel was pinned at exactly 1.0000 before the patch and after
it — the 0.333x it printed was the readback's ceiling, not the voxel store's. It now reads the
same pixel through a GREY mirror at L = 3.0 and at L = 0.8 and asserts the RATIO (3.75 correct,
1.25 under a clip): the mirror's reflectance and the whole grade cancel, and the claim is about
the store alone. Measured 1.255x unpatched / 3.745x patched.

`-j4` is the ceiling on this box (RTX 4080 16 GB: ~1.6 GB of VRAM per Vulkan boot since the
probe-shadow merge of 2026-09-10, ~3 GB before; boots are CPU-bound too — expect ~1.6× over
-j2, not 2×). One sibling gate at -j4 fits beside yours; drop to `-j2` only when two or more
other Vulkan gates (or the owner's app plus one) are live.


### 1c. SCALE TARGETS — the `scale-target` label (lane D1-SCALE-FIXTURES)

Phase E's measuring stick: `tests/scale/`, one `scale.*` row per wall of
`SPECS/audits/V2_ATOM_PHOTON_STRATEGY_2026-09-25.md` §3 (W1-W14), over three fixtures built at
test time (the 10k-instance WORLD through the product's mirror and tier tables, the 1 M / 5 M /
10 M ASSETS through the product's bake, the 10k-asset / 500-project LIBRARY through the import
door). Each row PRINTS today's number as a `target:` line with no bar (`bar none yet`) and
fails only when the measurement could not be taken. The label sits in `TARGET_LABELS` beside
`photon-target` and is handled the same way: selected and run by a scoped gate, reported and
discarded; dropped from the MERGE and PUSH tiers. The part that closes a wall writes the bar
into its row and removes the label. The rows that time the GPU or the frame are registered
through the GPU-timing lock (§4). The 1 M / 5 M / 10 M bakes are W11 itself (tens of minutes
in Debug), so they are made once by the tool `scale_assets_gen` (EXCLUDE_FROM_ALL) into the
build tree's bake cache (`$JAH_SCALE_ASSET_CACHE` moves it); a row whose asset is not cached
measures the largest one that is, or a 250 k shell it bakes, and says so.

## 2. Every gate, regardless of tier

- Its OWN Xvfb display AT 1920x1080 (`Xvfb :NN -screen 0 1920x1080x24`; framing-dependent pixel suites move with the window aspect — a 1600x1000 display reds scripting.e2e.particles), `DISPLAY=:NN` explicit on every ctest/app command line (never the
  inherited environment, never `:0`).
- `JAHSHAKA_DATA_ROOT=<scratch>` + a scratch `HOME` on every invocation (`app.data_root`
  scrubs the variable itself since the cleanup — no exception needed).
- A BYTE COPY of `build-linux/bin/jahsettings.ini` before, restored after if the hash moved.
- Logs named by commit (`gate-<sha>-pass1.log`, `-solo-<suite>-N.log`), scratch ≤ a few
  hundred MB, deleted at the end. Kill only pids you spawned.
- The main-tree GATE FREEZE: while a gate runs on `build-linux`, nobody advances `ogre` or
  rebuilds `build-linux` (api.contract reads the source tree; a relinked binary mid-gate
  makes a mixed verdict).

## 2b. The source-hygiene lints, and `source.member_init` in particular

Eleven `source.*` rows read the SOURCE TREE, not a running binary: they need no display and
no build artefact, they cost under ten seconds together, and every tier runs them (the
`hygiene` dir rides most gate-scope rules for exactly that reason). They exist because a
large deletion or a one-line law grows back one call site at a time.

**`source.member_init` (UNINIT-SWEEP-1, 2026-09-21) is the one a lane will meet by
accident**, because it fires on code nobody thought was about it: every scalar, pointer,
enum, atomic or array member you declare in `src/` or `irisgl/{core,document,engine,import,
mirror}` must carry a DEFAULT MEMBER INITIALISER, or be written by the mem-initialiser list
or body of EVERY constructor of its class. Not by a helper the constructor calls, not by an
out-parameter, not by "every caller sets it" — a helper can grow an early return, and the
initialiser is the one form a gate can see. **The answer to a red is the one-line
initialiser, never a row in `member_init_allow.txt`**: the allow file holds nine members
whose whole point is to stay unwritten (the maths types' `Qt::Uninitialized` overloads) and
nothing else belongs there. The staged `member_init_baseline.txt` is GONE — its 506 rows
were closed by 473 default member initialisers and 33 deletions — so the gate now reads
zero and stays there.

## 3. The SCOPED tier — `scripts/gate-scope.sh` (selection by reach, MODULAR-GATE-1)

THE PRINCIPLE (TESTING_V2 T3; PHOTON_ATOM_CONTRACT §7b rule 1): a lane runs everything its
change CAN REACH and nothing it cannot — read from the build and the diff, never guessed — and
the full tiers stay where the process needs them (a stage close, a fork pin bump, nightly, the
phase's push). `-j N` / `--jobs N` sets the ctest parallelism (default 4); the printed command,
the wall estimate and a fallback's MERGE tier all follow it.

```
scripts/gate-scope.sh <base>..<tip>            # a lane: its base commit .. its tip — prints the selection and why
scripts/gate-scope.sh <range> --run            # run it (DISPLAY, data root set); every row's verdict -> the run log
scripts/gate-scope.sh <range> --json           # machine-readable: suites, reasons, rationale, command
scripts/gate-scope.sh --files src/x.cpp ...    # a file list instead of a range (no diff: no symbols, no CMake reading)
scripts/gate-scope.sh --solo <suite> [--times 3]   # the flake protocol (§4), each run logged as a retry
scripts/gate-scope.sh --record-times           # scripts/gate-times.txt from the run log (median PASS s, 14 days)
```

**The three selectors** (the code: `Selection` in `scripts/gate-scope.py`, the graph and the
symbol reader in `scripts/gate_graph.py`):

1. **Rebuilt artefacts — the compiled rows.** A C/C++ file (or any other input the build
   compiles: a `.ui`, a `.qrc`, a shader the build turns into SPIR-V) reaches the executables
   ninja would relink: `build.ninja`'s edges plus ninja's deps log (which object read which
   header), refined by the libraries' own link references (`nm`) — an archive member counts
   only where the linker extracts it, a `libIrisGL` object only where a reference chain from
   the executable (or the library's static initialisers) reaches it. A compiled row runs iff
   one of its executables is reached. Measured at the landing: the mesh bake's object is in 24
   of the 154 executables that load libIrisGL; the engine's facade (`OgreEngine.cpp`) pulls all
   38 engine objects into every one of the 210 executables that name the engine — so an engine
   change reaches every engine test by construction, and that is the answer, not a gap.
2. **Symbols — a header hunk.** A changed header selects through what its changed lines
   DECLARE or modify: a function by its name; a member's modified type or default by the member
   AND its struct (every user constructing it); an added or removed member or enumerator by its
   own name (code that does not name it compiles to the same meaning); a line inside an inline
   body by its function. The files of `src/`, `irisgl/`, `tests/` that NAME one of those
   identifiers are the reached paths, each through its own graph reach and rules. A NEW header
   reaches its includers (all in the same diff). An identifier named by more than 60 files is
   too common to narrow by: the header then reaches every includer (the graph). A comment- or
   whitespace-only change to any C/C++ file reaches nothing (the source lints ride).
3. **Reach — the app rows.** Almost every change relinks `bin/Jahshaka`, so the rows that run
   the app — the `--script` suites, the wrapper-run ones, and the harnesses compiled with
   `JAHSHAKA_BINARY` that spawn it (`open.responsive`, `avatar.responsive`, `mcp.e2e`, …) — are
   chosen by the AREA RULES' families (a `src/`/`irisgl/` directory → test dirs + the API
   modules its scripts call; `*vulkan-scripts` / `*headless-scripts` / `*all-scripts` /
   `*app-rows` are the app groups) and by the verb → module map (a changed
   `src/scripting/modules/<m>api.cpp` selects every script calling `<m>.`; a module more than 40 %
   of scripts call selects nothing on its own). A path whose app relinked and that no rule
   narrows selects EVERY app row — never the whole tier. For runtime data the graph cannot see
   (engine media, scenes, sample content) a rule's dirs pick every row.

**Build files** (`CMakeLists.txt`, `*.cmake`) are read by what their CHANGED commands name: the
rows a command names (an `add_test`, a suite list such as `JAH_GPU_EXCLUSIVE_SUITES`), a helper
function's rows (ctest's backtrace) and the targets it is called on, a target the command
changes (`target_*()`, `set_target_properties()`), the sources it names
(`set_source_files_properties`), a sub-directory, a configured script (`configure_file`, and a
variable substituted into a `.in` as `@VAR@`), the commands that read a local variable it sets.
Source-list entries and comments select nothing (the files are in the diff). A NEW build file
selects its rows and the executables built under it. A directory-scope setting
(`add_compile_options`, `find_package`, a bare `set`, an `if()`) reaches every target under its
directory — at the root, in `tests/` or in `irisgl/` that is the MERGE tier BY RULE. A vendored
archive's flag change (`meshoptimizer`) enters through its first-party callers (the bake's rows).

**The full tier by rule, and the fallback.** A fork pin bump (`irisgl/thirdparty/ogre-next`)
selects the MERGE tier BY RULE (§7b rule 4: an Ogre change reaches everything). The FALLBACK to
the MERGE tier is left for a path with no rule, no symbol and no graph owner — printed loudly with
the path and the reason; a build dir with no ninja deps log (not built yet) turns the graph off
and says so (compiled rows then go by the rules' dirs, the old behaviour). `app.startup_quiet` +
`api.contract` ride every code change; the `quiet-box` rows never ride a scoped gate; TARGET
tests run in a second, non-gating ctest line (§1b).

**What it prints:** one rationale line per touched path (`graph: N executable(s) incl. the app;
rule …`, `symbols [FogDesc, density] named by 7 file(s)`, `names 2 row(s)`, `target meshoptimizer
(vendored) enters our code through [...]`), then every selected row with its reason
(`[relinks test_x]`, `symbol FogDesc`, `rule …`, `[module world]`) and the estimate (`N of M tier
rows, ~S of ~T suite-seconds`, costs from THE RUN LOG's 14-day medians over
`scripts/gate-times.txt`).

**THE RUN LOG (TESTING_V2 T8).** `--run`, `--solo` and the rc-gate tiers
(`scripts/gate_runlog.py run --tier <t> -- <ctest line>`) append one JSON record per row and per
pool arm to `<workspace>/testing/runs/<date>-<tier>-<tip>.jsonl`: verdict (PASS | FAIL | CRASH |
TIMEOUT | NOTRUN), retries, wall seconds, `gpu_ms` (a suite's `gpu_ms:` line), a target's value,
the selection reason, the tree's three shas, the box (load over the suite's own window, the GPU
clock state, -j, the display, sibling gates). The fields are `testing/runs/README.md`; the two
standing queries are `scripts/gate_runlog.py longest` and `scripts/gate_runlog.py load-reds`.

**The guard: `gate.selection`** (`tests/hygiene/gate_selection.py`, label `hygiene`) replays
recorded lane diffs (`tests/hygiene/gate_selection_cases.json`: D's last ten lanes, their reds
with each verdict's evidence) against the current build's graph: every REAL red must be selected,
no lane may fall back, a fork bump must select the tier by rule, the audit's S1/S2 subjects select
their suites, and the graph's precision holds (a compiled row whose executable does not contain
the file is NOT selected). **A red that a stage, nightly or push tier finds and that a lane's
scoped selection missed is a SELECTOR DEFECT: it is fixed in `gate-scope.py` and added to the
cases file** — never answered with a wider rule "to be safe". `source.gate_scope_rules` guards the
tool's own traps (the empty inventory, `--build .`, the targets' split, the fallback's -j).

**Measured at the landing** (thirteen recorded diffs of D's last eleven lanes, costed with the
per-row seconds of a -j4 MERGE-tier run; `spikes/modular-gate-1/table.md`): the five that FELL
BACK (tests/CMakeLists ×2, irisgl/CMakeLists, tests/support, irisgl/irisglfwd.h) now select 3,
143, 444, 492 and 584 rows (VIEWS-DEPTH-1 57 → 1 min, IMPORT-SPEED-1 57 → 13, D5-IRISGL-CRUD
57 → 45); an ENGINE lane selects 502-612 of the tier's 653 rows = 54-57 of its 57 minutes — the
engine's facade puts every engine object into every executable that uses the engine, and every
rendering app row runs the chain, so that IS its reach (the old path rules ran 436-620 rows and
missed the app-spawning harnesses); a fork pin bump is the tier by rule. Summed: 677 → 580 min.
The selector does not make an engine lane short; the pools (fewer app boots) are what an engine
lane's time rests on.

## 3b. Re-gating after a fix (2026-09-11, owner: "no double checking")

A lane that gets a red on its tier FIXES, then re-runs ONLY (a) the suites that failed and (b)
the SCOPED selection of the FIX's own diff (`scripts/gate-scope.sh <pre-fix tip>..<post-fix tip>
--run`) — never the whole tier again. The batch gate before the push is the full safety net.
The lead's post-merge targeted run stays (owner decision): it catches a merge interaction at
merge time instead of at the batch gate.

## 4. Flake protocol (unchanged)

A failure is re-run SOLO on a quiet box up to 3×; 3/3 green = environmental, with the
evidence string in the report (host-load timing, `VK_ERROR_OUT_OF_DEVICE_MEMORY`, the
texture-worker SEGV class). Known contention-sensitive suites: open.responsive,
app.engine_selftest_validation, app.input_keys, threading.newproject_stall,
scenegraph.benchmark, shadergraph.bake_output, claude.chat, scripting.e2e.space_switch /
sun_light, ui.media_lazy, gi.budget, scripting.e2e.reflection_map (the GI/VRAM contention
class; L8's gate, 2026-09-11). Every failure in a gate report carries a verdict
(environmental + evidence, or real + the failing assertion); a report without verdicts is
not a gate.

**THE GPU-TIMING LOCK (lane DEVPROCESS-1, 2026-09-23).** Measured: VRAM is not the constraint
(2.3 GB of 16 GB with three GPU processes live) — GPU TIME is, and `RUN_SERIAL` serialises
only inside one ctest process while every lane is its own. THE RULE: **a suite that measures
time or GPU budget runs under the GPU lock; everything else shares the GPU; at most three
app-spawning lanes; a measurement lane (debug-runner) takes the lock around every
`perf.capture` run via the wrapper.** The lock is `scripts/gpu-exclusive.sh <command…>` — one
box-wide `flock` on `/tmp/jah-gpu-timing.lock` (a lock file in RAM is fine: the lock lives in
the kernel and dies with its holder), a bounded 900 s wait (exit 75, the command never runs),
the command exec'd in place so a ctest timeout still kills the suite itself. The suites above
plus `app.watchdog_stall`, `gi.field_scroll` and `gi.rt_reflect_cost` (POST-C-FIXES-1) and the seven
timing rows of the scale suites (`scale.world`, `scale.voxel_scroll`, `scale.cluster_cut`,
`scale.decode`, `scale.tlas`, `scale.cpu_walks`, `scale.library`; D1-SCALE-FIXTURES, §1c) —
twenty-two, listed once in `JAH_GPU_EXCLUSIVE_SUITES`
(`tests/CMakeLists.txt`) — are REGISTERED through it by `jah_gpu_exclusive_test()`, whose
`RUN_TIMEOUT` is the suite's own budget and whose TIMEOUT is that plus the 900 s wait; configure
fails if a listed suite is registered any other way. Only those suites take it: a lane's
pixel/logic suites, gate-scope's `-j1` target run and the rc-gate's ctest line still overlap
freely. `ctest -N -V | grep -c 'Test command: .*gpu-exclusive.sh'` = 22 (a bare `grep -c
gpu-exclusive` also counts the guard's own command line, which names the script: 23). Its guard is `devprocess.gpu_lock` (label
`tooling`). A contention verdict on one of the twenty-two now needs a sibling that was NOT under
the lock (an app on `:0`, a measurement run outside the wrapper) — say which.

## 5. Why the full gate cost 25 minutes, and what the cleanup changed

**CURRENT COUNTS (2026-09-18, the last three PUSH-tier gates — ledger §680, §689, §707):
455 suites REGISTERED, 451 of them RUN in the MERGE/PUSH tier, in 930-1,028 s at -j4.**
The FOUR the tier excludes are exactly the two labels' members, all nightly —
`open.crash_soak` and `shadercache.container_asan` (`shadercache-attack`),
`scenegraph.benchmark` and `rigperf.benchmark` (`benchmark`). The `-E "^gi\.ddgi_raster$"` that used to ride
the tier's command line is GONE from it (this lane, 2026-09-18): that suite left with the
irradiance field's raster probe source (FIELD-RASTER-CRUD, 2026-09-17), so the regex
excluded nothing and the count is the labels' four alone. A tier command that filters a
suite nobody can name is how a stale exclusion survives a year. Measured in a lane worktree at
Studio `c8c3dfe8a` on 2026-09-18: 457 registered, 453 in the tier — two suites above push
#46's numbers, which is the normal weekly drift and the reason to re-count rather than
read. The three gates, with what was running beside each —
the spread is LOAD, not content:

| Push | Suites | Wall at -j4 | Retries | What else was on the box |
|---|---|---|---|---|
| #44 (2026-09-17 23:5x) | 443/443 | 930 s | zero | one lane's build |
| #45 (2026-09-18 01:3x) | 446/446 | 994 s | zero | two lanes (a scoped gate + a build; load 16 → 8) |
| #46 (2026-09-18 04:0x) | 451/451 (455 registered) | 1,028 s | zero | a sibling -j2 gate + two app instances |

So the band to plan with is **~1,000 s (16-17 min) for the PUSH tier at -j4 with lanes
live, and the MERGE tier is THE SAME SET** — the two tiers have had identical selections
since the 2026-09-10 cleanup (§4b), so "the MERGE tier" and "the PUSH tier" differ only in
WHEN they run and in the selftest-hash check the push one carries. A quiet box is faster
(the older 488-680 s readings below were measured on a tree with 90 fewer suites); nothing
in this range is a regression, and none of these three needed a single retry — which is
the number worth watching, because a tier whose reds are all flakes is not a gate.

HISTORY, kept because the counts move most weeks: 368 registered / 364 run in 488-680 s at
-j4 (2026-09-13, pushes §211/§214/§219), 353/353 at 488 s quiet on push #24, 354/354 at
672 s with a sibling lane compiling at -j10 (#26), 680 s on #25 (two reds, both the
documented UI-thread contention class, 3/3 solo green); zero retries on #24, #26, #27, #29
and #30 (508-579 s quiet, 674 s beside RR2 + two lanes); 362/358 since ENGINE-6 (#29),
363/359 since ENGINE-7 (#31, 497 s quiet), 364/360 since P0 (#32, 498 s), 366/362 since the
Ogre-samples ports (#33, 603 s, one solo retry), 368/364 since the smoke batch (#34, 521 s;
#35 529 s — the ASan cache attack 572 → 184 s after SHADERCACHE-2). Re-count with `ctest -N`
and `ctest -N` with the `-LE` that `gate-scope.py --merge-tier` prints rather than
trusting any number written here.

**The earlier measurement, kept for the shape of the cost (2026-09-10, main tree at
8cb9af75): 327 suites, 572 s at -j4.** The remaining floor is a ~112 s serial tail at the end of the run (the RUN_SERIAL
islands drain single-file; `app.input_keys` alone is 55 s and runs last with three cores
idle) — the next win is splitting that driver or scheduling the serial rows first
(ctest `COST` property). Slowest parallel rows: log.perf 40 s, possession 39, sockets.e2e 39,
samples.session 37, workflow_grid 36, reopen_fidelity 36, shadercache.app 36.

| Contributor | Seconds | Why |
|---|---|---|
| shadercache.container_asan | 572 | 56 engine cycles under ASan, 18 of them re-seeds |
| scenegraph.benchmark (RUN_SERIAL) | 287 (518 under load) | wall-clock regression guard; ctest runs it alone |
| 70 Vulkan app boots | 894 | ~7 s of engine boot before the first assertion |
| 11 RUN_SERIAL islands | 410 | pure wall time |

At `-j4` the floor is 572 + 410 ≈ 16 min by construction. The cleanup lane
(SPECS/TEST_GATE_AUDIT.md, steps 1-8) moves the benchmark to a 15 s smoke + nightly,
restructures and moves the cache attacks nightly, folds 14 duplicate sample boots into
`samples.cleanstart`, merges the three xdotool drivers, fixes the port-8751 collision that
makes `-j4` unsafe, and trims timeouts to 6× measured. Projection: ~9 min for the full gate at
`-j4`, ~5 min for MERGE. Phase 2 (own lane): a multi-script runner so ~65 document-only
suites share one live instance (−340 suite-seconds).

## 6. Who does what

- **Lane** (feature-/engine-builder): gates its worktree on SCOPED (or MERGE when scope falls
  back or the lane touches the engine), reports the tier + selection + wall time + every
  failure's verdict.
- **Lead**: audits the lane (diff, gate triage, `~/Developer/scripts/platform-audit.sh`),
  merges, runs the merge tier on the tip when several lanes have landed, pushes at a sensible
  batch (irisgl first), and runs NIGHTLY on a quiet box.
- **gate-runner**: runs the tier it is given, never picks, restores what it touched, kills
  only its own pids.

## 7. The four selftest hashes (lane FENCE-1, PHOTON phase A)

`./build-linux/bin/Jahshaka --engine-selftest out.png` prints FOUR `sha256` lines and writes
four files. They are the cheapest early warning in the tree — seconds, one process — and a
push quotes all four.

| line | file | what it fences | grade |
|---|---|---|---|
| `pose 1` | `out.png` | the default scene from the camera that has never moved | Plain |
| `pose 2` | `out.pose2.png` | the same scene after the camera moves +5 m in x, turns, and settles 240 frames — the cascade scroll, the field's follow, the settle | Plain |
| `pose B1 (rays)` | `out.B1.png` | fixture B **with** the ray tier | Viewport |
| `pose B2 (no rays)` | `out.B2.png` | the same fixture after `world.rayTracing("off")` | Viewport |

At Studio `fence-1` (2026-09-22), five consecutive runs on the rig (Xvfb `:NN`
`1920x1080x24`, `--data-root`; run 1 cache-cold, runs 2-5 warm) printed identical numbers:

```
pose 1        777eb2f12a15811a75a0fda1e7c746acbf07a030fca69e658c3132ebc3ffa7be
pose 2        c2c2b19f6c95351fbe6163b2036256fb240054e176dff87afe0e014fdf241d74
pose B1 rays  78349e56270c69fbb393c86507c38a209571b519f04229c035cd3e5b819be023
pose B2 none  541dce5a0f285cc84268011b837b3fcb28514dea138b282d142de797c70951a0
```

**WHY FIXTURE B EXISTS.** The default scene has no reflective pixel, no cascade crossing, no
emissive above 1.0 and nothing ray-traced, so no PHOTON defect and no PHOTON regression can
move poses 1 or 2. Fixture B is built in the same process THROUGH THE SCRIPTING VERBS (never a
shipped sample): an empty project, the realistic sky at a fixed sun, one directional light, a
60 x 60 m glossy floor, seven pillars of which one is emissive at radiance 3.0 and one is a
mirror, and a matte wall at z = -9 so the floor's bounce has a receiver beyond cascade 0's 5 m
face. `world.photon({tier:"high"})`, the `ssr` row forced to `hq`, camera (0,5,14) -> (0,1,0),
300 frames on the fixed clock per pose.

**THE GRADE IS LOAD-BEARING, and it had to be discovered.** `takeScreenshot(w, h)` is the
PLAIN grade — no post-processing at all — and the ray tier rides the SSR chain's prepass, so a
Plain-grade fixture B hashed IDENTICALLY with rays on and off (0 of 65,536 pixels different,
measured). B1/B2 are the VIEWPORT grade: the whole chain with the exposure RE-SEEDED from the
description rather than inherited from the on-screen view's temporal filter, so it is
deterministic by construction. At that grade the rays move 30,448 of 65,536 pixels, worst
78/255 — and the runner asserts B1 != B2 whenever the machine has ray queries (equal hashes on
a machine WITHOUT them is the correct answer and is said so, not asserted away).
