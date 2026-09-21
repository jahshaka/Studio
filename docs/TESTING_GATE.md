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
| **MERGE** | `ctest -j4 --timeout 120 --output-on-failure -LE "^(benchmark\|shadercache-attack)$"` — everything except the two wall-clock benches (label `benchmark`; their `--smoke` rows, label `benchmark-smoke`, DO run), the ASan shader-cache attack (`shadercache-attack`); `--timeout 120` is the default for rows that set none. (The trailing `-E "^gi\.ddgi_raster$"` this command used to carry is GONE, 2026-09-18: that suite was deleted with the irradiance field's rasterised probe source on 2026-09-17, lane FIELD-RASTER-CRUD, so the regex excluded nothing. `CLAUDE.md`'s copy of the command still carries it and is the lead's to trim — the two differ by a filter that matches no suite, which changes no selection.) **Measured 488-680 s at -j4 on the last three push gates (§5 has the three runs and what the spread is; 475 s at push #19, 572 s on the main tree 2026-09-10 — the older figures are history and the suite count moves most weeks)** | a lane whose scope falls back (see §3), any merge the lead wants covered wider, and — while the full gate is under moratorium — the gate before a push | gate-runner |
| **PUSH** | the MERGE tier + the `--engine-selftest` sha256 (the moratorium of 2026-09-09 lifted 2026-09-10 with the cleanup: the four nightly suites are NIGHTLY, not push, unless the batch touched their subject) | once per BATCH of merged lanes, before a push | gate-runner |
| **NIGHTLY** (after the cleanup) | scenegraph.benchmark `--assert`, shadercache.container_asan, the rigperf bench, **open.crash_soak** (OPEN-FRAMES-1: the async-open teardown repro twelve times under `glibc's built-in malloc checks + MALLOC_PERTURB_ (the real checker is an opt-in — it aborts under the NVIDIA GLX library)`; ~110 s, RUN_SERIAL, labelled `benchmark` because that label IS the nightly marker the MERGE/PUSH commands exclude — read one failure as "run it again", three as a regression of the open's slice-boundary drive) — the guards that need a quiet box or minutes of one process | once a day / before a tag, on a quiet box, and whenever a batch touched the open path, the scene teardown or the engine's resource handling | the lead |

Tiers are contracts: nobody hand-picks suites out of one. A lane says which tier it ran and
its selection; the lead's merge audit reads that line.

`-j4` is the ceiling on this box (RTX 4080 16 GB: ~1.6 GB of VRAM per Vulkan boot since the
probe-shadow merge of 2026-09-10, ~3 GB before; boots are CPU-bound too — expect ~1.6× over
-j2, not 2×). One sibling gate at -j4 fits beside yours; drop to `-j2` only when two or more
other Vulkan gates (or the owner's app plus one) are live.

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
nothing else belongs there. The staged `member_init_baseline.txt` is GONE — the sweep
finished at 384 initialisers and 34 deletions — so the gate now reads zero and stays there.

## 3. The SCOPED tier — `scripts/gate-scope.sh`

`-j N` / `--jobs N` sets the ctest parallelism (default 4, the tier's contract). A lane gating beside other live lanes runs `-j 2`
(the concurrency law); the printed command and the wall estimate follow the value, so a lane never has to re-type the selection.

```
scripts/gate-scope.sh <base>..<tip>            # a lane: its base commit .. its tip
scripts/gate-scope.sh --files src/x.cpp ...    # a file list instead of a range
scripts/gate-scope.sh <range> --run            # run the selection (DISPLAY, data root set)
scripts/gate-scope.sh <range> --json           # machine-readable
scripts/gate-scope.sh --record-times <ctest output log>   # refresh scripts/gate-times.txt
```

How a touched path selects suites (all read from the tree and the build dir's
`ctest --show-only=json-v1`, so a new suite is covered the day it is registered):

1. a file under `tests/<dir>/` → every suite that `tests/<dir>/CMakeLists.txt` registers; a
   touched script (`.js`, `.js.in`, `.sh`) → exactly the suites that run it;
2. a source a compiled test target lists (`${CMAKE_SOURCE_DIR}/src/...` in a tests
   CMakeLists) → that dir's suites;
3. a changed API module (`src/scripting/modules/<m>api.cpp`, `src/modules/*/api/*api.cpp`,
   `src/player/api/playerapi.cpp`) → every app-spawning suite whose script calls `<m>.` —
   a module every script calls (`project.`, `app.`, `console.`) selects nothing on its own,
   only when its OWN api file changed;
4. everything else → the AREA_RULES table in `gate-scope.py` (a `src/` or `irisgl/`
   directory → test dirs + modules; `*headless-scripts` / `*vulkan-scripts` / `*all-scripts`
   are the app-spawning groups; `*merge-tier` = "cannot be scoped");
5. any path with NO rule, or a rule saying `*merge-tier` (root CMake, `irisgl/CMakeLists`,
   unknown `src/` files) → the whole run FALLS BACK to the MERGE tier, loudly. A guessing
   rule is worse than the merge tier. **Exception (2026-09-11):** an edit to `CMakeLists.txt`,
   `irisgl/CMakeLists.txt` or `tests/CMakeLists.txt` whose every changed line is a source-file
   list entry (a `.cpp`/`.h`/`.ui`/`.qrc`/script path), a comment or blank does NOT fall back —
   the files it names are in the same diff and scope precisely (seven lanes fell back on
   2026-09-11 for exactly this; replayed, 15c and the Assets lane scope to ~6 min instead of ~9).
   A flag, target, find_package or condition change still falls back.
6. Whenever `src/` or `irisgl/` moved, `app.startup_quiet` + `api.contract` ride along
   (~15 s: one rendering boot, the scripting contract).

The estimate line (`~N suite-seconds, ~M min wall at -j4`) comes from `scripts/gate-times.txt`
(a full-gate snapshot) overlaid with the build dir's last run; refresh the snapshot after a
full gate with `--record-times`.

Measured first cuts (2026-09-09): one API module file → 51 suites / 2.5 min; a viewport
controller → 47 / 1.1 min; asset service + DB + assets API + Assets page → 155 / 3.8 min; a
document scene-graph file → 109 / 2.5 min; an engine GI file → 176 / 8 min; docs only →
api.contract. An engine change scopes to near-full by design (pixels move everywhere).

**Maintaining the rules:** when a scoped gate MISSES something a later merge/push gate
catches, the rule for that path gains the suite — that is the feedback loop the owner asked
for ("test it out as new lanes land"). Rules live in one table; keep the most specific
prefix first.

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
and `ctest -N -LE "^(benchmark|shadercache-attack)$"` rather than
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
