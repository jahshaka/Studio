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
| **MERGE** | `ctest -j4 --output-on-failure -LE "shadercache\|benchmark" -E "^gi\.ddgi_raster$"` — everything except the shader-cache attacks, the wall-clock benchmark and the raster probe build; ~7-8 min | a lane whose scope falls back (see §3), any merge the lead wants covered wider, and — while the full gate is under moratorium — the gate before a push | gate-runner |
| **PUSH** (full) | `ctest -j4 --output-on-failure` (all suites) + the `--engine-selftest` sha256 | once per BATCH of merged lanes, before a push — **SUSPENDED (owner, 2026-09-09) until the suite cleanup lands**; pushes gate on MERGE meanwhile | gate-runner |
| **NIGHTLY** (after the cleanup) | scenegraph.benchmark `--assert`, shadercache.container_asan, gi.ddgi_raster, the rigperf bench — the guards that need a quiet box or minutes of one process | once a day / before a tag, on a quiet box | the lead |

Tiers are contracts: nobody hand-picks suites out of one. A lane says which tier it ran and
its selection; the lead's merge audit reads that line.

`-j4` is the ceiling on this box (RTX 4080 16 GB: ~3 GB of VRAM per Vulkan boot today, ~1.6
after the probe-shadow fix; boots are CPU-bound too — expect ~1.6× over -j2, not 2×). Drop
to `-j2` while another Vulkan instance is live (a sibling gate, the owner's app).

## 2. Every gate, regardless of tier

- Its OWN Xvfb display AT 1920x1080 (`Xvfb :NN -screen 0 1920x1080x24`; framing-dependent pixel suites move with the window aspect — a 1600x1000 display reds scripting.e2e.particles), `DISPLAY=:NN` explicit on every ctest/app command line (never the
  inherited environment, never `:0`).
- `JAHSHAKA_DATA_ROOT=<scratch>` + a scratch `HOME` on every invocation, EXCEPT
  `app.data_root`, whose child is deliberately un-overridden: run it with
  `env -u JAHSHAKA_DATA_ROOT` or it reds on two assertions.
- A BYTE COPY of `build-linux/bin/jahsettings.ini` before, restored after if the hash moved.
- Logs named by commit (`gate-<sha>-pass1.log`, `-solo-<suite>-N.log`), scratch ≤ a few
  hundred MB, deleted at the end. Kill only pids you spawned.
- The main-tree GATE FREEZE: while a gate runs on `build-linux`, nobody advances `ogre` or
  rebuilds `build-linux` (api.contract reads the source tree; a relinked binary mid-gate
  makes a mixed verdict).

## 3. The SCOPED tier — `scripts/gate-scope.sh`

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
   rule is worse than the merge tier.
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

## 4. Flake protocol (unchanged)

A failure is re-run SOLO on a quiet box up to 3×; 3/3 green = environmental, with the
evidence string in the report (host-load timing, `VK_ERROR_OUT_OF_DEVICE_MEMORY`, the
texture-worker SEGV class). Known contention-sensitive suites: open.responsive,
app.engine_selftest_validation, app.pacing_undo, threading.newproject_stall,
scenegraph.benchmark, shadergraph.bake_output, claude.chat, scripting.e2e.space_switch /
sun_light, ui.media_lazy, gi.budget. Every failure in a gate report carries a verdict
(environmental + evidence, or real + the failing assertion); a report without verdicts is
not a gate.

## 5. Why the full gate costs 25 minutes, and what the cleanup changes

| Contributor | Seconds | Why |
|---|---|---|
| shadercache.container_asan | 572 | 56 engine cycles under ASan, 18 of them re-seeds |
| scenegraph.benchmark (RUN_SERIAL) | 287 (518 under load) | wall-clock regression guard; ctest runs it alone |
| gi.ddgi_raster | 155 | probe total fixed at 8192, converges 10× |
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
