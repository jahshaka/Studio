# The test gate — how it works (2026-09-09; reshaped by D6B-GATE-SHAPE 2026-09-27)

Jahshaka's ctest suite is ~700 registered rows; the MERGE/PUSH tier runs 653 of them (§5 has
the measured walls). Since 2026-09-09 the gate is TIERED: a change is gated on what it can
break, and the full run is a per-batch event. This file is the reference; `scripts/gate-scope.py`
carries the rules, and SPECS/audits/GATE_SUITES_AUDIT_2026-09-26.md is the audit the 2026-09-27
reshape followed.

## 1. The tiers

| Tier | What runs | When | Who runs it |
|---|---|---|---|
| **SCOPED** | the suites `scripts/gate-scope.sh <base>..<tip>` selects from the touched paths | a lane's own gate; a merge of that lane | the lane (feature-/engine-builder), or gate-runner with the selection |
| **MERGE** | the command `python3 scripts/gate-scope.py --merge-tier [-j N]` prints — `ctest -j4 --timeout 120 --output-on-failure -LE` over `gate-scope.py`'s `NIGHTLY_LABELS` ∪ `TARGET_LABELS` (today: every `nightly` row — §1d —, the ASan shader-cache attack `shadercache-attack`, and the target tests `photon-target` and `scale-target`, §1b/§1c; the benches' `--smoke` rows, label `benchmark-smoke`, DO run). The set lives in the script ONLY; this doc never copies it (`source.gate_scope_rules` case 9 fails on a literal `-LE` list here), and neither may a script — `scripts/lead/rc-gate.sh` must run what `--merge-tier` prints. `--timeout 120` is the default for rows that set none; `-j 2` while another lane's gate is live. **Measured (D6B-GATE-SHAPE, 2026-09-27): 56.7 min at -j4 beside other lanes' gates, 36.6 min simulated on the audit's quieter durations — the reshape's ~30 min goal (and its lane's ≤ 35 min bar) was NOT reached; §5 has the before/after pair and why** | a lane whose scope falls back (see §3), any merge the lead wants covered wider, and — while the full gate is under moratorium — the gate before a push | gate-runner |
| **PUSH** | the MERGE tier + the `--engine-selftest` sha256 lines — **FOUR of them since lane FENCE-1**: `pose 1`, `pose 2`, `pose B1 (rays)`, `pose B2 (no rays)`. Poses 1-2 are the default scene at the PLAIN grade; pose pair B is a purpose-built fixture (glossy floor, cascade crossing, emissive 3.0, mirror pillar) at the VIEWPORT grade, which is the only grade that carries the SSR prepass and therefore the ray tier. Quote all four. (The moratorium of 2026-09-09 lifted 2026-09-10 with the cleanup: the four nightly suites are NIGHTLY, not push, unless the batch touched their subject) | once per BATCH of merged lanes, before a push | gate-runner |
| **NIGHTLY** | `python3 scripts/gate-scope.py --nightly-tier` — every `nightly` row, `-j1` (§1d): the benches' `--assert` rows, shadercache.container_asan, **open.crash_soak** (OPEN-FRAMES-1: the async-open teardown repro twelve times under glibc's malloc checks; read one failure as "run it again", three as a regression of the open's slice-boundary drive), the minutes-of-one-process sweeps (atom.cluster_cut, atom.cluster_crack, gi.chain_converge_scenes), vr.frame_budget, and the `<suite>.timing` millisecond rows (§4) | once a day / before a tag, on a quiet box, and whenever a batch touched a nightly row's subject | the lead |

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
today (2026-09-27) are five: `gi.chain_face_target`, `gi.field_follows_energy`,
`gi.cone_corner_target`, `gi.cone_integrator_parity_offaxis` and `atom.dag_bound_target`.
D6B-GATE-SHAPE took the label off three that had gone green (3/3 on the rig):
`gi.volume_edge_spec_target` (0.0000 / 0.0011 against 0.05), `gi.gather_reference_target`
(worst 0.969 against 1.00 +- 0.05) and `gi.gather_plane_target` (1.000) — they gate now.
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

### 1d. THE NIGHTLY LABELS — `nightly` and `quiet-box` (lane D6B-GATE-SHAPE)

`benchmark` used to double as the nightly marker, so rows that are not benchmarks at all
(open.crash_soak, gi.gather_cost) carried it. Two labels say it now, both read by
`gate-scope.py` alone:

- **`nightly`** — the row leaves the MERGE and PUSH tiers (`NIGHTLY_LABELS`) and runs in the
  nightly tier. It is either MINUTES OF ONE PROCESS whose push-time guard lives elsewhere
  (atom.cluster_cut 141 s and atom.cluster_crack 136 s — the bake's sweeps; gi.chain_converge_scenes
  80 s, whose claim gi.chain_converge holds at every push) or a measurement.
  **A `nightly` row still rides the SCOPED gate of its own subject**: a change to the bake
  (irisgl/import) selects the cluster suites, a GI change the converge sweep — the move was
  made only after the scope rules could see them (the audit's condition, S2).
- **`quiet-box`**, beside `nightly` on every row that MEASURES — the wall-clock benchmarks,
  gi.gather_cost (the GPU clock), vr.frame_budget, and the `<suite>.timing` rows (§4) — and on
  open.crash_soak (twelve RUN_SERIAL processes read probabilistically: a scoped gate that ran
  it would stop for minutes). A scoped
  gate NEVER runs these (it shares the box with other lanes by construction); they are also
  the rows the GPU-timing lock is for.

`source.gate_scope_rules` case 12 holds the split (a change to test_open_responsive.cpp scopes
open.responsive and not open.responsive.timing; `--nightly-tier` names `nightly`).

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

Thirteen `source.*` rows read the SOURCE TREE, not a running binary: they need no display and
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

**THE RUN LOG (TESTING_V2 T8).** `--run` (its tier `scoped`, or `scoped-fallback` / `scoped-tier` when a scoped gate ran the whole tier), `--solo` and the rc-gate tiers
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

**ENFORCEMENT (T6; §7b "the rules are tooling, not text").** (1) THE ONE COMMAND a developer runs
before a merge is `scripts/gate-scope.sh <base>..HEAD --run`: it prints the selection and every
reason, runs it, writes the run log, and exits non-zero on any gating red (target tests report,
never gate). (2) `source.testing_rules` (label `hygiene`) lints the rules the tree can show: R1
every measuring row (`.timing`, `.benchmark`, `JAHSHAKA_TIMING_BARS=1`) inside the GPU lock; R2
every `nightly` row priced in `scripts/gate-times.txt`; R3 no copied `ctest -LE` tier outside
`gate-scope.py`; R4 no `RUN_SERIAL` without a comment naming its reason (or the GPU lock); R5 every
app/lint row reachable from a subject (an API module its script or harness calls, a rule's
directory, a tree file it runs). (3) THE MERGE REFUSAL: `scripts/ci-gate-check.sh <range>` exits 1
with the reason unless the run log holds, at the range's tip on a clean tree, a latest-PASS record
for every row and arm the range selects (a solo retry's green after a red counts; both stay in the
log). A hook or a CI job calls it; `gate.ci_check` proves it.

**THE JOINT SUITES (T4).** At a merge where two lanes touched one file family, the lead runs
`scripts/gate-scope.sh --joint <rangeA> <rangeB> [--run]`: the gate is the UNION of both
selections, printed with the paths both touched and the JOINT ROWS — each lane's own guards
(selected by its test-side changes) that the other lane's change also reaches, the combination
neither lane's gate saw. No automation triggers it.

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

**THE GPU-TIMING LOCK (lane DEVPROCESS-1, 2026-09-23; THE TIMING LIST since D6B-GATE-SHAPE,
2026-09-27).** Measured: VRAM is not the constraint (2.3 GB of 16 GB with three GPU processes
live) — GPU TIME is, and `RUN_SERIAL` serialises only inside one ctest process while every lane
is its own. THE RULE: **a suite that measures time or GPU budget runs under the GPU lock;
everything else shares the GPU; at most three app-spawning lanes; a measurement lane
(debug-runner) takes the lock around every `perf.capture` run via the wrapper.** The lock is
`scripts/gpu-exclusive.sh <command…>` — one box-wide `flock` on `/tmp/jah-gpu-timing.lock` (the
lock lives in the kernel and dies with its holder), a bounded 900 s wait (exit 75, the command
never runs), the command exec'd in place so a ctest timeout still kills the suite itself.

**THE LOCK LIST IS THE TIMING LIST** (`JAH_GPU_EXCLUSIVE_SUITES`, `tests/CMakeLists.txt`,
registered by `jah_gpu_exclusive_test()`, whose `RUN_TIMEOUT` is the suite's own budget and
whose TIMEOUT is that plus the 900 s wait; configure fails if a listed suite is registered any
other way). A suite is on it because it asserts milliseconds, a ratio of milliseconds or a frame
budget — never for a flake history: the six members that measured no time LEFT it
(app.engine_selftest_validation, claude.chat, ui.media_lazy, scripting.e2e.space_switch /
sun_light / reflection_map; audit R4 — ui.media_lazy had waited 88 s for it), and the timing
ratios that sat under RUN_SERIAL only JOINED it (mirror.scale, perf.epic_steady_state,
perf.drag_mirror_room, perf.capture_off_is_free; R3), with app.play_select beside app.input_keys
(key/gesture arrival; R5). Every lock row also takes ctest's `RESOURCE_LOCK gpu_timing`, so two
of ONE gate never start together and wait in a slot on the flock. `ctest -N -V | grep -c 'Test
command: .*gpu-exclusive.sh'` = 38 (a bare `grep -c gpu-exclusive` also counts the guard's own
command line). Its guard is `devprocess.gpu_lock` (label `tooling`). A contention verdict on a
lock row needs a sibling that was NOT under the lock (an app on `:0`, a measurement outside the
wrapper, or — the lock never excludes it — the CPU load of the gate's own other slots) — say which.

**THE MILLISECOND BARS ARE NIGHTLY: counts at push, milliseconds on a quiet box** (D6B-GATE-SHAPE;
audit §5). A wall-clock bar reads the box as much as the code, and the GPU lock does not exclude
the CPU load of a -j4 gate (open.responsive's 300 ms frame bar read 605 ms cold on a UI thread
that was not blocked). So a suite with such a bar is TWO ROWS OVER ONE BINARY: the push row
asserts every count, structure and picture claim and PRINTS each millisecond reading
(`time: within|OVER: …`); `<suite>.timing` (labels `nightly;quiet-box`, the lock) runs the same
binary with `JAHSHAKA_TIMING_BARS=1` and asserts them (`tests/support/timingbars.h`,
`JAH_TIMING_CHECK`; for a `--script` suite, `jah_timing_script()` generates the armed copy). Rows:
open.responsive, archive.responsive, avatar.responsive, chain.hzb, engine.gpu_cull,
gi.field_follows, ui.properties_filter, ui.components, meshbake.cards, app.update_check,
app.shutdown_order, vr.warmup, scripting.e2e.editor_controls. Nothing was deleted: a bar that
reds only on a loaded box is read nightly, never widened.

**THE SERIAL FAMILIES** (D6B-GATE-SHAPE; audit R1/R2). RUN_SERIAL stops EVERY other suite of the
gate while one runs (54 rows, 1,487 s of the audit's 3,125 s gate, 48 % of its wall with one row
running). A group that must not overlap EACH OTHER takes a `RESOURCE_LOCK` instead, and a COST
above every measured row so its chain starts first and overlaps the gate:
`monado` — the sixteen rows that start a Monado runtime (the fourteen of the push tier plus
the nightly vr.frame_budget and vr.warmup.timing) (each runner's IPC socket lives in its
own XDG_RUNTIME_DIR, so the "one socket" reason was void; vr.eye_grade captures the compositor
with `xwd -id` and therefore runs on ITS OWN Xvfb, displays 241-299); `gi_chain` — the
cascade-chain rows (their VRAM reason was measured false). RUN_SERIAL remains only where the
audit kept it (app.input_keys, app.watchdog_stall, gi.field_scroll, threading.mode /
mode_serial / gi_resolve / gi_resolve_serial / newproject_stall, and the nightly benches).

## 5. What the full gate costs, and where its wall goes

**CURRENT COUNTS (2026-09-27, lane D6B-GATE-SHAPE, Studio d-build): 697 rows registered,
653 in the MERGE/PUSH tier** (the audit of 2026-09-26 counted 650/638; `ctest -N` with the
`-LE` that `gate-scope.py --merge-tier` prints is the count — never this line). The wall is
LOAD times SHAPE, and D6B measured both, one tier run before and one after, at -j4 on the rig
beside two other lanes' gates (load 8-13), plus a scheduler simulation (spikes/d6b-gate-shape/
simulate.py: ctest's own ordering — dependency levels, COST, RESOURCE_LOCK, RUN_SERIAL — over a
finished gate's durations; it reproduces the before-run's 5,310 s within 1.2 %) that separates
the two:

| | rows | wall at -j4 | one row running | suite-seconds (busy/4 = the floor) |
|---|---|---|---|---|
| before, measured (51e9f2c49) | 659 | 5,311 s (88.5 min) | 2,029 s (38 %) | 15,140 (3,785 s) |
| after, measured (58b216b46) | 653 | 3,400 s (56.7 min) | 420 s (12 %) | 11,989 (2,997 s) |
| before's durations, after's SHAPE (simulated) | 653 | 3,975 s (66.3 min) | 354 s (9 %) | 14,738 (3,684 s) |
| the audit's quieter durations: before shape / after shape (simulated) | 659 / 653 | 3,333 s / 2,195 s (55.5 / 36.6 min) | 48 % / 11 % | 8,531 / 8,010 (2,003 s) |

THE AFTER-RUN'S REDS WERE MOSTLY THE BOX'S VRAM, and that is a law to keep: 16 of its 25 reds
printed `VK_ERROR_OUT_OF_DEVICE_MEMORY` and three more a lost device, while four lanes' gates
and apps held 15.0 of 16.4 GB (nvidia-smi: ~1.5-2.8 GB per Jahshaka process, not the ~1.6 GB of
2026-09-10); those 19 and a segfault of the same minutes were all green solo, three timing rows
of the lock were 3/3 green solo, and the last two (vr.eye_grade, threading.newproject_stall)
were red on the base tree's before-run with the same assertion. DEVPROCESS-1's "VRAM is not the
constraint" was measured with three processes; with four gates it is. Keep the `-j2 while two or
more other Vulkan gates are live` rule, and read a device-lost red beside nvidia-smi.

WHAT THE SHAPE CAN STILL WIN is the gap to busy/4 — about 8 % after D6B. The rest is
suite-seconds: the tier grew ~20 rows between the audit and D6B (atom.lod_switch alone is
250 s), and three other lanes' gates doubled every row's seconds on the day of the
measurement. The residual one-running tail is the RUN_SERIAL rows the audit KEPT (§4, the
serial families paragraph); a `threading` RESOURCE_LOCK for the four compile halves would
keep their stated reason ("four Epic opens compiling at once") without stopping the gate.

HISTORY, kept because the counts move most weeks: 455 registered / 451 run in 930-1,028 s at
-j4 on pushes #44-#46 (2026-09-18, ledger §680/§689/§707; the spread was load: one build, two
lanes, a sibling gate + two app instances), 368 registered / 364 run in 488-680 s at
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
`-j4`, ~5 min for MERGE. The multi-script runner this section once
scheduled as "phase 2" is built: §8 (the pools).

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

## 8. The pools — app tests as named ARMS (lane SUITE-POOL-1, 2026-09-27)

The app runs one script per `--script` process, and its boot is the expensive part: measured
on this box 2026-09-27, ~13.7 s engine-up with a cold shader cache (every gate after a
rebuild), ~5.5 s warm, ~3.2 s `--headless`. With one process per verb suite, app suites were
77 % of the gate's suite-seconds (SPECS/audits/SUITE_REDUNDANCY_AUDIT_2026-09-27.md). A POOL is
one ctest row, `pool.<pool>`, whose one process (`Jahshaka --scripts … --pool <pool>`,
`tests/support/run_pool.py`) runs a family's scripts as named ARMS. The developer's
description — the arm protocol, the verdict lines, the solo retry — is `docs/TESTING.md` §2;
this section is the contract.

**THE CONTRACT.**
- Every arm is `<pool>.<arm>` and prints exactly one verdict: `PASS`, `FAIL` (its first
  failing assertion), `CRASH` (the process died in it) or `TIMEOUT` (killed past 6x its
  measured seconds). A crash or a timeout costs its own arm: the driver restarts the process
  and continues from the next arm. The row is red iff an arm is not `PASS`, and every red in
  a gate report is named BY ARM, never "the pool".
- Every arm starts in a fresh JavaScript realm; after every arm, green or red, the pool's
  baseline is restored (its own `BASELINE` script if it has one, then the window's size, the
  open project closed, the deferred deletes delivered), so a later arm starts with NO project
  open and creates or opens what it needs. A death, hang or lost baseline after an arm is
  that arm's `CRASH`. The arms share the pool's home
  (fresh every run), so an arm never asserts on what another left in the library.
- The solo retry of an arm is the flake protocol's unit (§4):
  `JAH_POOL_ARMS=<pool>.<arm> DISPLAY=:NN ctest -R '^pool\.<pool>$'`, three times. Red in its
  pool and green alone three times is a STATE LEAK between arms — fixed in the arm's baseline,
  never with a budget.
- A row's TIMEOUT is derived (`jah_add_pool`: the sum of 6x each arm's measured seconds, floor
  30 s an arm, plus one 300 s boot budget) — never typed, never widened. An arm that got
  slower is re-measured.
- Selection (§3) names ARMS: a changed script or API module selects the arms that run it, and
  the scoped command runs them through their pools' rows with `JAH_POOL_ARMS`; a pool whose
  every arm is selected runs whole. The MERGE/PUSH tiers list pools as rows like any other.

**THE RULE FOR A NEW TEST.** A lane adds an ARM to its family's pool — a script and one
`jah_pool_arm(<pool> <arm> <script> <seconds>)` line beside the comment that says what it
claims. It adds a ROW of its own only for a new FIXTURE or a PROCESS SWITCH, and its brief
names which: a fresh home as the claim (a first launch, a cold cache); a boot latch (an
environment variable such as `JAHSHAKA_NO_RAY_QUERY` or `JAHSHAKA_TEST_NO_EDITOR_BOOT`, a flag
such as `--vr` or `--script-live`, a Monado session of its own); a GPU-lock timing measurement;
a private display (an xdotool/xinput driver); an MCP driver that restarts the app; a
crash-class churn. Engine and unit binaries keep one row per claim — their boot is ~2 s. A
brief's named acceptance tests are arm names as often as row names.

**THE MEASUREMENT** (the MERGE tier at -j4 on one rig display, one run each, one other lane
building beside them): at `b3039193a` (D6B's shape, before the pools) 656 rows in 2,841 s
(47.4 min; 2 reds: vr.eye_grade, mirror.scale timeout); at `95b7de06b` (the pools) 529 rows in
1,807 s (30.1 min; 2 reds: vr.eye_grade — the same red as before —, vr.session_undithered —
an engine binary this lane does not build differently, 1 of 3 solo retries red). −1,034 s of
wall, −127 rows, no arm and no assertion lost. The per-row seconds are
`scripts/gate-times.txt` (re-recorded from the after run).

**THE POOLS** (lane SUITE-POOL-1, 2026-09-27; the proof and the seconds are
`spikes/suite-pool-1/`): 142 former rows, 17 pool rows plus `pool.runner` (the runner's own
test). "solo" is the sum of the former rows in the MERGE tier at `b3039193a` (-j4, one sibling
gate live); "pooled" is the pool row in one run of every pool at -j4 on a quiet box.

| pool | arms | solo s | pooled s | the family |
|---|---|---|---|---|
| doc / doc_assets | 29 / 23 | 110 / 90 | 13 / 12 | the `--headless` document verbs / the asset, import, export and clipboard verbs |
| gi_verbs / gi_movers | 8 / 7 | 213 / 168 | 92 / 78 | world.gi rows and status / movers, mirror, rays, cards |
| editor / editor_view | 10 / 7 | 248 / 194 | 89 / 73 | scene building / the viewport, gizmo, outline, stats (TEST-TIER-1 since: editor 9 arms Low incl. `tier`, editor_view 9 Epic with full_surface, play_select; shading_live 6 with particles; player 6) |
| shading / shading_live | 9 / 5 | 226 / 155 | 90 / 91 | materials and the PBS knobs / live textures, thumbnails, reflection maps |
| world_sky / world_light | 7 / 4 | 310 / 218 | 108 / 157 | sky, clouds, ground, planar, world.vr / sun, lights, grades |
| player | 7 | 208 | 86 | play, possession, space switches |
| assets_import | 5 | 144 | 96 | the Assets page, the import dialog, samples, desktops |
| vr_noruntime | 6 | 120 | 24 | every `vr.*` verb with no runtime |
| vr_session | 4 | 153 | 75 | ONE Monado runtime, one `--vr` process (tests/vr/run_vr_app.sh --launch) |
| avatar_anim / atom / cameras | 3 / 3 / 5 | 64 / 98 / 201 | 17 / 55 / 32 | the avatar page and skinning / the Atom view, draw and LODs / every camera verb |
| **total** | **142** | **2,918** | **1,186** | **1,732 suite-seconds** |

**THE POOL'S TIER** (lane TEST-TIER-1, 2026-09-27; `SPECS/audits/GPU_LOSS_AUDIT_2026-09-27.md`
§A2; the proof is `spikes/test-tier-1/`). Every engine-up pool declares `TIER low|epic` on its
`jah_add_pool` line. A Low pool's process runs `--test-tier low`: every scene it binds is put on
the Low World Mode (after the reader, the `world.mode` path) and the window boots 1280x720. An
Epic pool has no test tier (the product's own tiers). At every boot the row prints
`MEM <pool> gpuPoolUsed=<MB> textures=<MB> processMiB=<MiB> tier=<t>` and the run log records it
(`mem`). Measured on this box (warm cache, one process, nvidia-smi per pid): Epic 1,532 MiB at
boot (peak 1,672), gpuPoolUsed 1,445 MB, textures 1,093 MB; Low 280 MiB at boot (peak 468),
gpuPoolUsed 323-327 MB, textures 40 MB. The split: LOW = editor, player, assets_import,
avatar_anim, vr_noruntime, cameras (every arm the same verdict and `ok` count as at Epic); EPIC =
gi_verbs, gi_movers, shading, shading_live, world_sky, world_light, atom, vr_session and
editor_view (gizmo_rings, render_stats, hierarchy_visibility, and full_surface / play_select
moved into it, are red at Low: picture, framing or Epic-row claims); particles (red at Low, and
short of its line after editor_view's window-changing arms) moved to shading_live. A Low pool never
hosts a GI, pixel or shipped-tier assertion — such an arm MOVES to an Epic pool. The same arm
`<pool>.tier` runs in one pool of each tier and asserts which it is in.

**ORDER IS DECLARED, AND RARELY MATTERS.** Arms run in declaration order. Three arms carry a
process-level claim and are pinned: `FIRST` for `player.player_verbs` and
`vr_session.player_session` (the Player page was never shown in the process) and `doc.memory`
(its burst reads the engine node pools' slots, which carry the process's history).
`vr_noruntime` has a BASELINE script (injected hands withdrawn, the gizmo mode back to
translate) run after every arm, green or red. `world_vr` lives in `world_sky`, not
`vr_noruntime`: a session override set with no session has no release, and the VR arms read
the defaults. A new arm with a claim like these says so on its `jah_pool_arm` line.

