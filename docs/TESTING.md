# Testing Jahshaka — the developer's guide

How to run the tests on your change, how a test is shaped, how to add one, how to read a red, and how the rig works.
WHEN tests run and who runs them is the process (`DOCS/PROCESS.md` in the lead repo, its tools `spec.sh` and
`sprint.sh`); this page is about the tests themselves.

## 1. Running the tests

Everything runs from a configured build dir (`build-linux` in your tree) on YOUR OWN X
display — never the desktop's `:0` (§4):

```bash
# what your change reaches, by subsystem, and the ctest command of each (spec.sh touched prints the same)
scripts/gate-scope.sh <base>..HEAD
# one ctest row
DISPLAY=:NN ctest -R '^scripting\.e2e\.mobility$' --output-on-failure
# one POOL row (every arm), and ONE ARM of it
DISPLAY=:NN ctest -R '^pool\.gi_verbs$' --output-on-failure
JAH_POOL_ARMS=gi_verbs.gi_status DISPLAY=:NN ctest -R '^pool\.gi_verbs$' --output-on-failure
```

A row is a seconds-long GUARD of a named behaviour under its budget (a fixture or a CPU row 10 s, an app row 60 s;
`source.row_budget` holds every row at its price in `scripts/gate-times.txt`), or it is labelled `measure` (a timing,
soak, churn or scale row, or a guard over its budget — `JAH_MEASURE_ROWS` in `tests/CMakeLists.txt`) and runs only at
the sprint's ship, alone on the card. A guard is never deleted for its price: a pool over 60 s is split by its arms, a
row over its budget is rewritten to it or becomes a measure.

## 2. How a test is shaped

There are three shapes, and a new test takes the cheapest one that can see its claim.

**A unit test** — a small C++ binary (`tests/<family>/test_*.cpp`) linking only what it tests,
`QT_QPA_PLATFORM=offscreen`, no display, no engine. Milliseconds. One ctest row per binary.

**A headless engine test** — a C++ binary that boots the Ogre engine with no window
(`tests/support/enginetesthelpers.h`) and reads real pixels back. About 2 s of boot. One row
per claim, often one binary run in several modes (`--bound-bar`, `_norays`): its boot is cheap
and a mode is often a device latch that needs a fresh process.

**An app test = an ARM of a POOL.** The editor's verbs are tested through the real app running
a JavaScript file (`tests/scripting/scripts/e2e_*.js`), the same verbs the console, MCP and the
UI call (the API-first rule). The app runs one script per `--script` process, and a boot costs
~14 s engine-up with a cold shader cache (every gate after a rebuild), ~5.5 s warm, ~3.2 s
headless (measured 2026-09-27) — so app tests are grouped into POOLS: one ctest row,
`pool.<pool>`, one process, N scripts run in turn as named ARMS
(`Jahshaka --scripts <list> --pool <pool>`, driven by `tests/support/run_pool.py`).

For every arm the pool:

1. starts it in a fresh JavaScript realm (no global of an earlier arm survives). The first arm
   starts on the boot; every later one starts where the previous arm's baseline left the app —
   NO PROJECT OPEN, the desktop page up, the boot's window size — so an arm begins by creating
   or opening what it needs (`project.create(...)`), as every script here already does;
2. runs the script: it throws on a failed assertion, or ends with a non-zero number;
3. restores the baseline after it, green or red: the pool's own baseline script if it has one
   (`jah_add_pool(... BASELINE <script>)` — `vr_noruntime`'s withdraws injected hands), then
   the window back to the boot's size and out of full screen, the open project closed through
   `project.close()`, the deferred deletes delivered. A baseline that cannot be restored ends
   the process (`POOL-BASELINE-LOST`).

The driver prints each arm's ONE verdict line, `ARM <pool>.<arm> PASS|FAIL|CRASH|TIMEOUT <ms>
[why]`, once the baseline behind it held — that line is what the run log reads. It owns what a
dying process cannot say: an arm that began and never finished is `CRASH` (the process died)
or `TIMEOUT` (it ran past 6x its measured time and was killed), and a process that dies,
hangs or loses its baseline AFTER an arm finished makes THAT arm `CRASH` — never a silent
restart. Either way the driver starts a new process (`POOL <pool> RESTART <n>`) and continues
from the NEXT arm, so a crash costs one arm, never the pool. The row fails iff an arm is not
PASS, and a closing summary repeats every arm's verdict:

```
POOL gi_verbs VERDICTS (2 process(es), 1 restart(s))
  gi_verbs.gi_status: PASS 9120
  gi_verbs.gi_bounds: CRASH 4410 the process died in the arm (signal 11)
  gi_verbs.gi_ddgi: PASS 6230
POOL gi_verbs: 3 arm(s) — 2 PASS, 1 not PASS, 1 restart(s)
solo retry: JAH_POOL_ARMS=gi_verbs.gi_bounds ctest -R '^pool\.gi_verbs$'
```

**The arm-name convention** (the contract the gate's selector reads):

- an arm's full name is `<pool>.<arm>`; `<arm>` is its old suite's short name
  (`scripting.e2e.gi_status` became `gi_verbs.gi_status`);
- a pool row's COMMAND names every arm as a triple `--arm <arm> <script path> <budget s>`, in
  order, so a tool maps a script to its (pool, arm) from `ctest --show-only=json-v1` alone;
- `JAH_POOL_ARMS=<pool>.<arm>[,<pool>.<arm>…]` in the environment runs only those arms of
  the pools it names (`<pool>` alone = the whole pool); a pool it does not name runs every
  arm. A name the pool does not have is a `FAIL` of its own ("no such arm"), never a silent
  pass. So a selector runs "arms a and b of pool p, all of pool q" as ONE ctest invocation:
  `JAH_POOL_ARMS=p.a,p.b ctest -R '^pool\.(p|q)$'`;
- the run log (`scripts/gate_runlog.py`) records one line per arm from the driver's `ARM` lines — the one channel.

**THE POOL'S DECLARATION** (TEST-TIER-1, TEST-NEEDS-1; `tests/support/test_boot.cmake`). Every
engine-up pool — like every row that starts the app — declares what its process boots,
`jah_add_pool(<pool> TIER <low|medium|high|epic> NEEDS <photon bloom ssao smaa planar…>|NONE …)` or
`TIER document`, and it is the PROCESS's, never the document's:

- `TIER low NEEDS NONE` — JAHSHAKA_TEST_TIER=low / JAHSHAKA_TEST_NEEDS=none (`services/testtier.h`):
  every scene it binds, a new one and an opened one (after its reader), is put on the Low World
  Mode through the call `world.mode` makes, every switchable feature off, and (`TEST_WINDOW`,
  its own declaration) the window boots 1280x720. For arms whose claims are verbs, UI state, counts — nothing that reads the picture.
  `app.testTier()` reads it (`{tier: "low", needs: []}`).
- `TIER document` — no test tier: a new scene is the product's Epic, an opened scene keeps the
  tier it saved (`app.testTier().tier` is `""`). For an arm that asserts a picture, GI, Atom, a
  number measured at the shipped tier, or a World row the document saved.

An arm that needs more than its pool keeps MOVES to a pool that keeps it — never mix (a Low pool must never host a
GI assertion: the irradiance field and the chain differ by tier). At every boot the pool prints
`MEM <pool> gpuPoolUsed=<MB> textures=<MB> processMiB=<MiB> tier=<t>` — the boot footprint
through `app.memoryStats`/`app.textureMemory` and the process's own `nvidia-smi` line. THE LEAK PROBE: after every arm's baseline (the
project closed, the deferred deletes delivered) the pool prints `MEM <pool>.<arm> gpuPoolUsed=<MB>
textures=<MB>`, so a process's VRAM across its arms is a curve in its output; when the last arm's figure exceeds the first's by more than the largest single step, the
driver prints the FINDING line `LEAK <pool> +<MB> over <n> arms` — not a red. A `--script` row takes the same switch as
`JAHSHAKA_TEST_TIER=low` in its ENVIRONMENT (the flag wins where both are given).

A pool's arms SHARE its home (fresh every run, the shader cache kept warm), so an arm never
asserts on what another arm left in the library, and an arm that changes an app-wide setting
puts it back. Each pool was proved arm by arm against the solo suites it replaced before they
were deleted (the same verdicts, the same count of `ok` lines).

## 3. How to add a test

**Add an ARM to your family's pool.** Write the script (`tests/scripting/scripts/e2e_<x>.js`:
`project.create(...)` first — an arm starts from the boot, never from another arm's state —
then assertions that throw), and add ONE line where the family's arms are declared:

```cmake
jah_pool_arm(editor my_claim ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_my_claim.js 12)
```

The number is the arm's measured seconds inside its pool (round up; the pool prints
`ARM editor.my_claim PASS <ms>`). Its kill budget (6x, never under 30 s) and the row's TIMEOUT
are derived from it — never typed, never widened by hand. A slower arm is re-measured.

**A ROW of its own is justified only for a new fixture or a process switch** — and the brief
names which:

- a FRESH HOME is the claim (a first launch, a virgin library, a cold cache);
- something the process LATCHES AT BOOT: an environment variable (`JAHSHAKA_NO_RAY_QUERY`,
  `JAHSHAKA_TEST_NO_EDITOR_BOOT`, `JAHSHAKA_PLAYER_TEST_NO_ATTACH`…), a flag (`--vr`,
  `--script-live`), a Monado session;
- a TIMING measurement: a `measure` row under the card's lock (`jah_gpu_exclusive_test`, §4);
- a PRIVATE DISPLAY (an xinput/xdotool driver) or an MCP driver that restarts the app;
- a CRASH-CLASS CHURN (a soak whose subject is the process dying).

Engine and unit binaries keep one row per claim — their boot is cheap. A spec's acceptance tests are arm names
(`gi_verbs.my_claim`) as often as row names.

**A row that boots Vulkan declares its class** (the card's slots, `tests/cmake/vram_rows.cmake`): register it with
`jah_gpu_row(<row> CLASS app|selftest|engine|vr COMMAND …)` instead of `add_test(NAME <row>
COMMAND …)` — `app` for a `Jahshaka` process, `engine` for an engine/Qt suite binary, `vr` for a
Monado row. A row that boots no Vulkan is a plain `add_test` with `jah_no_display()`. A pool
declares nothing (its class is `app`, `CLASS vr` for a VR pool). `source.gpu_rows_closure` fails
the gate on a Vulkan row registered any other way, naming its CMakeLists line.

## 4. The rig

- **Your own X display**, `Xvfb :NN -screen 0 1920x1080x24` (NN in 60-99, `/tmp/.X<NN>-lock`
  absent first), and an explicit `DISPLAY=:NN` on every ctest or app line. Never `:0` — that
  is the person at the desk. 1920x1080 exactly: framing-dependent pixel suites move with the
  window's aspect. Start it as `Xvfb :NN … & echo $! > <your tree>/xvfb.pid`, stop exactly
  that pid — never by name or number.
- **`--data-root <dir>` / `JAHSHAKA_DATA_ROOT`** moves the settings file, the library, the
  asset store and the shader cache together. Every suite sets its own; set it for any app you
  start by hand, or you write the build tree's shared `jahsettings.ini`.
- **The card's lock.** `scripts/gpu-exclusive.sh <command>` runs a command holding the ONE lock on the GPU (a flock;
  the verbs hold it around their ctest runs, a measuring row registered with `jah_gpu_exclusive_test()` takes it
  itself, an app or measurement you run by hand wraps itself in it). Inside one ctest, every GPU row takes its
  class's slots of the card (8: a fixture 1, an app row 2, a measuring row all 8), so `ctest -j8` keeps each class
  to its width.
- **Priority.** Builds run under `nice -n 19 ionice -c 3`; ctest and hand-started app instances
  under `nice -n 19` ONLY (IONICE-1: the root disk's mq-deadline ages an idle-class read 10 s, and
  a 0.1 s script under `ionice -c 3` took 60 s). A person's live app always wins the CPU.

## 5. Reading a red

1. **Find the arm.** A pool's last lines name every arm's verdict. `FAIL` carries the first
   failing assertion; a red arm's own output is quoted (`| …`) just above its `ARM` line. A
   green arm's output stays out of the row's output (ctest truncates a passing row's), in
   `pool-logs/<pool>-process<N>.log` in the row's working directory — its path is printed.
   `CRASH`/`TIMEOUT` mean the process died or was killed IN that arm or in the baseline
   right after it; the next arms ran in a new process and have their own verdicts.
   `CRASH <ms> xid <n>` is the kernel's word: an NVIDIA Xid from the pool's pid, logged while
   that arm ran — a GPU fault, never environmental, even when the arm's own assertions held.
2. **Retry the arm alone**, through the real row:
   `JAH_POOL_ARMS=<pool>.<arm> DISPLAY=:NN ctest -R '^pool\.<pool>$' --output-on-failure`.
   Green alone and red in its pool, three times, is a STATE LEAK between arms: fix the arm's
   baseline (what it assumes about the library or a setting), never the budget.
3. **No red is explained away.** A red is fixed where it is found — never a retry that hides it, a widened TIMEOUT
   or a loosened bar. A red with an Xid from its pid in the kernel log is never environmental.

## 6. Tests count frames, never wall-clock

The engine has no wall clock: it steps a fixed 1/60 s per frame. A test waits for a state by
stepping frames (`editor.frame(n)`) and reading until the value stops moving — never by
sleeping a number of milliseconds and hoping. A test that measures time warms up first (the
first seconds of a process are the shader-compile storm), runs under the GPU lock, and its
millisecond bars are `measure` rows (the sprint's ship); the guards assert counts. The `<ms>` on an ARM line is
a report for the scheduler, never an assertion.

## 7. What your change reaches, and why

`scripts/gate-scope.sh <base>..HEAD` prints what your change reaches and why, by subsystem, and the ctest command of
each; it runs nothing. You never pick rows by hand and never widen the selection "to be safe" — if it looks wrong, the
selector is corrected (`scripts/gate-scope.py`) and the diff becomes a case in `tests/hygiene/gate_selection_cases.json`.

### How the selection is made

Your diff is read three ways. None of them is a list someone keeps by hand; all three are read
fresh from your build and your diff every time.

1. **What the build rebuilt.** For C++ (and anything else the build compiles), the tool asks
   the build graph which test executables contain your changed code — directly, through a
   header they include, or through a library whose code they actually call. A compiled test
   runs if and only if its executable is one of them. If your change is in the engine, that is
   every test that uses the engine: the engine is one library that every such test pulls in
   whole. That is correct, not a gap.
2. **What your header change names.** When you change a header, the tool reads which
   declarations you changed — a function, a member, a type — and selects the files that use
   those names. Change a comment and nothing is selected but the source lints. Add a member to
   a struct and only the code that names the new member is affected (it is in your diff anyway).
   Change a member's default and every user of the struct is.
3. **What the app can reach.** Almost every change rebuilds the app, so the app suites (the
   `--script` tests and the harnesses that start the app) are chosen by area: the directory you
   changed maps to test families and to the scripting modules (`world.`, `node.`, …) whose
   scripts exercise it. A change to a scripting module's API file selects every script that
   calls that module.

CMake changes are read the same way: a changed `add_test` selects its test, a changed
`target_compile_options(x …)` selects what target `x` ends up in, a new test directory selects
its tests. A setting at the top of the build (a global flag) reaches every row, and says so.

### Reading the print

Every touched file gets a line saying what it reached and through what:

```
  irisgl/import/meshbake.cpp
      -> graph: 24 executable(s) incl. the app; rule ^irisgl/import/ modules=['assets', 'avatar', 'anim'] (app/other rows)
  irisgl/import/meshbake.h
      -> symbols ['bakeThreads', 'setBakeThreads', 'stageSummary'] named by 5 file(s)
```

and every selected test says why it is there (`[relinks test_mesh_bake]`, `symbol StageMs`,
`rule ^irisgl/import/ [module assets]`), and each subsystem ends with its exact `ctest` line.

### When a test you expected is missing, or one you did not expect is there

- **Unexpected test selected:** read its reason. `[relinks …]` means your code really is in that
  executable; `symbol X` means that file names something you changed; `rule …` means your
  directory's area includes that family. It runs. If the reason is wrong, that is a defect in
  the selector — report it, do not skip the test.
- **Expected test missing:** run it yourself as well (`DISPLAY=:NN ctest -R '^name$'`) and
  report the missing selection. If a later run (the merge, the ship) goes red on a test your
  selection did not include, that is a **selector defect**: it is fixed in
  `scripts/gate-scope.py` and your spec's diff becomes a new case in
  `tests/hygiene/gate_selection_cases.json`, which the `gate.selection` test replays on every
  change to the tool. The selector learns from facts, not from a wider rule.

## 8. The engine's environment switches (ENV-SWEEP-2)

An environment variable read inside `irisgl/engine/src` is a MEASUREMENT switch or a
test door, never a product mode: every one below has a reader in the tree (the suite or
script that sets it), and a switch that loses its reader is deleted, not kept "in case".
A product setting is a verb or an `EngineConfig` field. The switches the Photon files
read (`photon/`, `OgreScreenProbeGather*`, `OgreSurfaceCache*`) are listed by their own
lanes. Deleted by the sweep (no reader): `JAHSHAKA_GI_DEBUG` (17 diagnostic log sites),
`JAHSHAKA_HIT_LIST_OFF`, `JAHSHAKA_GI_NO_RECENTRE`.

THE RAY TIER'S MEASURING DOORS ARE ARMS (lane TEST-1, the perf audit's A2): `JAH_RQ_REFIT`,
`JAH_R5_NO_MOTION`, `JAH_R5_MONO_EYES`, `JAH_R6_NO_ALPHA`, `JAH_R7_NO_POSED` and
`JAH_R7_EDGE_CLASSES` — five of them read per frame inside shipped code — are deleted, and so
are the per-frame `JAHSHAKA_GATHER_NO_TEMPORAL`, `JAHSHAKA_ATOM_DECODE_OFF`, `JAHSHAKA_CARD_FOOTPRINT_K`
and `JAHSHAKA_GI_FIELD_NO_SCROLL`. Each is a registered arm now (`Engine::setArm` / the verb `engine.arm(name, value)`, latched at the top
of the next frame; `Engine::arms()` / `engine.arms()` list them with their defaults):
`rayquery.tlasRefit`, `reflect.motion`, `reflect.monoEyes`, `reflect.alphaTested`,
`reflect.posed`, `gather.temporal`, `atom.decode`,
`gi.fieldScroll`. TESTING-CLEANUP-2 (H8f) converted the last ones read on a per-frame, per-pass or
per-build path, and deleted them: `JAH_GI_CASCADE_FAULT` / `_POST` -> `gi.cascadeFault` /
`gi.cascadeFaultPost` (-1 = none, else the cascade that throws), `JAHSHAKA_GI_FIELD_RAYS` / `_SAMPLES` / `_STATIC` -> `gi.fieldRays` /
`gi.fieldSamples` / `gi.fieldStatic` (read at the field's build: set the arm, step a frame, then
build), `JAH_VCT_REFUSE_GEOMETRY` -> `gi.refuseGeometry`, `JAHSHAKA_ATOM_DISCRIMINATE` ->
`atom.discriminate` (1 codes, 2 the chart), `JAHSHAKA_HIT_WORLD_LIGHTS` -> `atom.hitWorldLights`
(1 off, 2 all), `JAHSHAKA_HIT_VCT_SPECULAR` -> `atom.hitVctSpecular` (0 = compiled out). What
remains below is read ONCE (a boot or session latch). A new measuring switch is a row in that table
(OgreFrameMonitor.cpp `kArms`), never a `getenv`. `scripts/perf-ab.py` drives the arms.

| Switch | Read in | Reader | What it does |
|---|---|---|---|
| `JAHSHAKA_NO_RAY_QUERY` | OgreEngine.cpp | tests/CMakeLists.txt, ssr, app rows | boots without the ray-query tier (the no-rays rows) |
| `JAHSHAKA_NO_DITHER` | OgreChain.cpp | vr, samples, scale | the chain without its dither (byte-comparable pictures) |
| `JAHSHAKA_WARMUP_PASS` | OgreChain.cpp | shadercache.warm_up, projectrunner | the shader warm-up pass (an engine route with a known crash, off unless asked) |
| `JAHSHAKA_HLMS_DEBUG_DIR` | OgreEngine.cpp | docs/SCRIPTING.md | dumps the generated Hlms shaders |
| `JAHSHAKA_ATOM_DRAW_OFF` | OgreScene.cpp | the `--engine-selftest` hash A/B (no script can reach the selftest) | the Atom split off: every item through PBS |
| `JAHSHAKA_ATOM_OCCLUSION_OFF` | OgreScene.cpp | the `--engine-selftest` hash A/B | the id pass frustum-only |
| `JAHSHAKA_ATOM_TRACE` | OgreGpuScene.cpp | scale | the GPU scene's per-frame trace lines |
| `JAHSHAKA_RAY_DENY_STORAGE_FORMAT` | OgreRayQuery.cpp | gi.rt_reflect | refuses a storage format (the fallback path) |
| `JAH_ORTHO_POSTFX` | OgreView.cpp | ssr.e2e | post effects on an orthographic view |
| `JAH_TEXTURE_CACHE`, `JAH_TEXTURE_MULTILOAD`, `JAH_TEXTURE_SYNC_LOAD` | OgreEngine.cpp, OgreMaterials.cpp | scripts/threading/texture-ab.sh | the texture-load A/B arms |
| `JAH_TEXTURE_WAIT_MS`, `JAH_TEXTURE_WAIT_FAULT`, `JAH_TEXTURE_DRAIN_ADVANCE_MS` | OgreEngine.cpp | threading.texture_wait_watchdog, app.* verbs (docs/SCRIPTING.md) | the texture wait budget and its fault hook |
| `JAHSHAKA_VR_TEST_INJECT`, `JAHSHAKA_VR_TEST_NO_ACTIONS` | EnginePrivate.h, OgreVrSession.cpp | vr.session, the VR session scripts | injected controllers / an action-set refusal |
| `JAH_ASYNC_SHADER_THREADS` | OgreEngine.cpp | **NONE in the tree** (TESTING-CLEANUP-2B's sweep, 2026-10-10: ASYNC-SHADERS-1's override of `EngineConfig::asyncShaderThreads`) | the background shader compiler's thread count — by this section's rule a switch with no reader is deleted: the engine lane that owns it does (a product file, not a tooling lane's) |
