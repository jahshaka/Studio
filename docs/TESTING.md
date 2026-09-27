# Testing Jahshaka — the developer's guide

Read this first. It says how to run the tests on your change, how a test is shaped, how to
add one, how to read a red, and how the rig works. `docs/TESTING_GATE.md` is the gate's
CONTRACT (the tiers, the flake protocol, the numbers); this file is how to live with it.

Part one (this lane, SUITE-POOL-1): running, shapes, adding, the rig, reading a red.
Part two (MODULAR-GATE-1): how a change selects its tests.

## 1. Running the tests

Everything runs from a configured build dir (`build-linux` in your tree) on YOUR OWN X
display — never the desktop's `:0` (§4). The four commands you need:

```bash
# the scoped gate on your change: what it selects, why, and then it runs it
scripts/gate-scope.sh <base>..HEAD --run

# one ctest row
DISPLAY=:NN ctest -R '^scripting\.e2e\.mobility$' --output-on-failure

# one POOL row (every arm), and ONE ARM of it (the solo retry)
DISPLAY=:NN ctest -R '^pool\.gi_verbs$' --output-on-failure
JAH_POOL_ARMS=gi_verbs.gi_status DISPLAY=:NN ctest -R '^pool\.gi_verbs$' --output-on-failure

# the MERGE tier (the full run) — the command is printed by the script, never typed by hand
python3 scripts/gate-scope.py --merge-tier
```

**The tiers and when each runs** (the contract is `TESTING_GATE.md` §1):

| tier | what | when |
|---|---|---|
| SCOPED | what `gate-scope.sh` selects for your diff, as ctest rows and pool ARMS | every lane, on its own change |
| MERGE | every row except the nightly/target labels | a stage close, a fork pin bump, a fallback |
| PUSH | MERGE + the four `--engine-selftest` hash lines | once per phase, before the push |
| NIGHTLY | the wall-clock/GPU-budget bars and soaks (label `nightly`) | once a day on a quiet box |

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

1. begins the arm on the boot's baseline — a fresh JavaScript realm (no global of an earlier
   arm survives), and in an engine-up pool the editor page re-begun on a new default scene;
2. runs the script: it throws on a failed assertion, or ends with a non-zero number;
3. prints ONE verdict line, `ARM <pool>.<arm> PASS <ms>` or `ARM <pool>.<arm> FAIL <ms> <the
   first failure>`;
4. closes whatever project the arm left open through `project.close()` and checks none is
   open. If one still is, the process ends (`POOL-BASELINE-LOST`) and the driver restarts it
   for the arms that remain.

The driver owns what a dying process cannot say: an arm that began and never printed its
verdict is `CRASH` (the process died) or `TIMEOUT` (it ran past 6x its measured time and was
killed); either way the driver starts a new process and continues from the NEXT arm, so a
crash costs one arm, never the pool. The row fails iff an arm is not PASS, and the last lines
of its output name every arm's verdict:

```
POOL gi_verbs VERDICTS (2 process(es))
ARM gi_verbs.gi_status PASS 9120
ARM gi_verbs.gi_bounds CRASH 4410 the process died (signal 11)
ARM gi_verbs.gi_ddgi PASS 6230
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
- `JAH_POOL_RECORD=<file>` makes the driver APPEND one JSON line per arm
  (`{"pool","arm","verdict","ms","process","reason"}`) — the run log's per-arm source.

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
- a TIMING measurement: it runs under the GPU lock (`jah_gpu_exclusive_test`, §4);
- a PRIVATE DISPLAY (an xinput/xdotool driver) or an MCP driver that restarts the app;
- a CRASH-CLASS CHURN (a soak whose subject is the process dying).

Engine and unit binaries keep one row per claim — their boot is cheap. A lane's "named
acceptance tests" are arm names (`gi_verbs.my_claim`) as often as row names.

## 4. The rig

- **Your own X display**, `Xvfb :NN -screen 0 1920x1080x24` (NN in 60-99, `/tmp/.X<NN>-lock`
  absent first), and an explicit `DISPLAY=:NN` on every ctest or app line. Never `:0` — that
  is the person at the desk. 1920x1080 exactly: framing-dependent pixel suites move with the
  window's aspect. Start it as `Xvfb :NN … & echo $! > <your tree>/xvfb.pid`, stop exactly
  that pid — never by name or number.
- **`--data-root <dir>` / `JAHSHAKA_DATA_ROOT`** moves the settings file, the library, the
  asset store and the shader cache together. Every suite sets its own; set it for any app you
  start by hand, or you write the build tree's shared `jahsettings.ini`.
- **The GPU lock.** A suite that measures TIME or a GPU BUDGET holds
  `scripts/gpu-exclusive.sh` (a box-wide `flock`); it is registered with
  `jah_gpu_exclusive_test()` and nothing else takes it. A measurement you run by hand wraps its
  app in the same script.
- **Priority.** Builds, ctest and hand-started app instances run under
  `nice -n 19 ionice -c 3`; a person's live app always wins the CPU.

## 5. Reading a red

1. **Find the arm.** A pool's last lines name every arm's verdict. `FAIL` carries the first
   failing assertion; the arm's own output is above it, between its `ARM-BEGIN` and its `ARM`
   line. `CRASH`/`TIMEOUT` mean the process died or was killed IN that arm; the next arms ran
   in a new process and have their own verdicts.
2. **Retry the arm alone**, through the real row:
   `JAH_POOL_ARMS=<pool>.<arm> DISPLAY=:NN ctest -R '^pool\.<pool>$' --output-on-failure`.
   Green alone and red in its pool, three times, is a STATE LEAK between arms: fix the arm's
   baseline (what it assumes about the library or a setting), never the budget.
3. **The contention protocol** (`TESTING_GATE.md` §4): a red on the documented contention list
   is retried SOLO up to three times; 3/3 green is environmental WITH the evidence string in the
   report. A red with an Xid from its pid in the kernel log is never environmental. A report
   without a verdict per red is not a gate.

## 6. Tests count frames, never wall-clock

The engine has no wall clock: it steps a fixed 1/60 s per frame. A test waits for a state by
stepping frames (`editor.frame(n)`) and reading until the value stops moving — never by
sleeping a number of milliseconds and hoping. A test that measures time warms up first (the
first seconds of a process are the shader-compile storm), runs under the GPU lock, and its
millisecond bars run nightly; the per-merge tiers assert counts. The `<ms>` on an ARM line is
a report for the scheduler, never an assertion.
