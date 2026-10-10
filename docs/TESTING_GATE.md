# The test gate — how it works (2026-09-09; reshaped by D6B-GATE-SHAPE 2026-09-27)

Jahshaka's ctest suite is ~700 registered rows; the MERGE/PUSH tier runs ~575 of them — 559 in
its parallel phase and 16 timing rows serially (rc-smoke15a, 2026-10-01: 31.4 + 8.3 min after a
13.6 min build; §5 has the history). Since 2026-09-09 the gate is TIERED: a change is gated on what it can
break, and the full run is a per-batch event. This file is the reference; `scripts/gate-scope.py`
carries the rules, and SPECS/audits/GATE_SUITES_AUDIT_2026-09-26.md is the audit the 2026-09-27
reshape followed.

## 1. The tiers

| Tier | What runs | When | Who runs it |
|---|---|---|---|
| **SCOPED** | the suites `scripts/gate-scope.sh <base>..<tip>` selects from the touched paths | ONE gate per BATCH of ready lanes, on `d-build..candidate` (§3c) — a builder runs its named acceptance tests + its subject suite, not a gate | the lead (`merge-dbuild-lane.sh batch` → `rc-gate.sh`), or gate-runner with the selection |
| **MERGE** | TWO PHASES (TEST-SELECTOR-1): the parallel phase `python3 scripts/gate-scope.py --merge-tier [-j N]` prints (every row but the `timing` ones), then the serial phase `--merge-tier-serial` prints (the `timing` rows at -j1, each taking the whole GPU — inside the parallel phase they waited holding the admission's turnstile and stalled every admission on the box). The parallel phase is `ctest -j4 --timeout 120 --output-on-failure -LE` over `gate-scope.py`'s `STAGE_CLOSE_LABELS` ∪ `TARGET_LABELS` ∪ {`timing`} (today: every `stage-close` row — §1d —, the ASan shader-cache attack `shadercache-attack`, and the target tests `photon-target` and `scale-target`, §1b/§1c; the benches' `--smoke` rows, label `benchmark-smoke`, DO run). The set lives in the script ONLY; this doc never copies it (`source.gate_scope_rules` case 9 fails on a literal `-LE` list here), and neither may a script — `scripts/lead/rc-gate.sh` must run what `--merge-tier` prints. `--timeout 120` is the default for rows that set none; the box runs ONE gate at a time (§4c, the gate slot) — there is no lower width "while another lane's gate is live". **Measured (D6B-GATE-SHAPE, 2026-09-27): 56.7 min at -j4 beside other lanes' gates, 36.6 min simulated on the audit's quieter durations — the reshape's ~30 min goal (and its lane's ≤ 35 min bar) was NOT reached; §5 has the before/after pair and why** | a lane whose scope falls back (see §3), any merge the lead wants covered wider, and — while the full gate is under moratorium — the gate before a push | gate-runner |
| **PUSH** | the MERGE tier + the `--engine-selftest` sha256 lines — **SIX of them since TESTING-CLEANUP-2 (H5)**: `pose 1`, `pose 2`, `pose motion`, `pose B1 (rays)`, `pose B2 (no rays)`, `pose mover` (§7). Poses 1-2 are the default scene at the PLAIN grade, `motion` the same scene mid-walk; pose pair B is a purpose-built fixture (glossy floor, cascade crossing, emissive 3.0, mirror pillar) at the VIEWPORT grade, which is the only grade that carries the SSR prepass and therefore the ray tier, `mover` the fixture with a sphere crossing it. Quote all six. (The moratorium of 2026-09-09 lifted 2026-09-10 with the cleanup. The `stage-close` rows are not push rows: the lead runs the STAGE-CLOSE batch before every push, below) | once per BATCH of merged lanes, before a push | gate-runner |
| **STAGE-CLOSE** | THE BATCH OF THE ROWS THE MERGE AND PUSH TIERS LEAVE OUT (§1d; lane STAGE-CLOSE-1): `python3 scripts/gate-scope.py --stage-close-tier --run` (or `JAH_GATE_TIER=stage-close scripts/lead/rc-gate.sh <tag>`) — ONE gate in the slot, two phases: every `stage-close` row without `quiet-box` (the minutes-of-one-process sweeps atom.cluster_cut, atom.cluster_crack, gi.chain_converge_scenes; samples.mirror_room_boots; gpu.cutout_soak and the shadow casters' `.churn` twins; shadercache.container_asan) at the gate's width, then every `quiet-box` / `timing` one — the benches' `--assert` rows, gi.gather_cost, vr.frame_budget, **open.crash_soak** (OPEN-FRAMES-1: the async-open teardown repro twelve times under glibc's malloc checks; read one failure as "run it again", three as a regression of the open's slice-boundary drive) and the `<suite>.timing` millisecond rows (§4) — at `-j1` on ONE hold of the whole card (`--stage-close-tier` / `--stage-close-tier-serial` print the two lines; the run-log tier is `stage-close`) | at every stage close and before every push — started by the lead, never by a timer | the lead |

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
  prints it in a `TARGET TESTS` section, and runs it as ITS OWN STEP after the gate's verdict:
  `--run` prints the gating phases' verdict line (every gating record already written), THEN runs
  the target rows INSIDE THE GATE — on the gate's own display, under its slot (§4c), holding the
  whole card once (GATE-COST-1 P9; TARGET-STEP-DISPLAY-1, ledger §1851: the DETACHED step the gate
  used to start outlived the lane's Xvfb and recorded 159 "Malformed resolution string" rows against
  a dead display) — and exits with the gating phases' code whatever the targets read. The lane reads
  its verdict from the `GATE VERDICT` line; the targets' records go under the run log's tier
  `target`. `--no-targets` skips the step; `--targets-only` runs it alone (a stage close). Still `-j1`
  (it is a measurement), and its exit code is reported and discarded. The MERGE and PUSH tiers drop it
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
today (2026-09-28) are five: `gi.chain_face_target`, `gi.field_follows_energy`,
`gi.cone_corner_target`, `gi.cone_integrator_parity_offaxis` and `gi.sealed_room_chain_target`
(SEALED-ROOM-LEAK-1: 12/255 against 2). DAG-LOCK-1 took the label off
the DAG's displacement-lock row (the stand-in 0 of 22 groups over, worst 1.39x) — it gates now, and
TESTING-CLEANUP-2B renamed it `atom.dag_bound` (no `_target` on a gating row).
D6B-GATE-SHAPE took the label off three that had gone green (3/3 on the rig):
`gi.volume_edge_spec_target` (0.0000 / 0.0011 against 0.05), `gi.gather_reference_target`
(worst 0.969 against 1.00 +- 0.05) and `gi.gather_plane_target` (1.000) — they gate now.
PHOTON-WRITER-1 retired three: `gi.chain_converge_target` (its claim - one at-rest sweep is the
fixed point - is the ordinary row's byte-equality assertion), `gi.field_follows_ground_target` and
`gi.chain_face_sky_target` (both gated in their ordinary rows since the field's probe rays cross
the voxels as rays and a probe behind the shaded point has no say).
`gi.rt_reflect_lamp_clip` was the fourth and **VOXEL-CLIP-1 took its label off** (2026-09-22,
fork ad452604a+155a56bf8 (was 0087)) — with a note worth keeping, because it is about the INSTRUMENT: a target test
has to be answerable through the thing that reads it. That row asked a PERFECT mirror to read
radiance 3.0 out of an offscreen view whose render target is `PFG_RGBA8_UNORM`
(`OgreView::createRtt`), so its pixel was pinned at exactly 1.0000 before the patch and after
it — the 0.333x it printed was the readback's ceiling, not the voxel store's. It now reads the
same pixel through a GREY mirror at L = 3.0 and at L = 0.8 and asserts the RATIO (3.75 correct,
1.25 under a clip): the mirror's reflectance and the whole grade cancel, and the claim is about
the store alone. Measured 1.255x unpatched / 3.745x patched.

THE WIDTH IS ONE CONSTANT: `GATE_JOBS` in `scripts/gate-scope.py` (today **4**;
`gate-scope.py --gate-jobs` prints it). The scoped gate (a batch candidate's), the MERGE tier, a
fallback, the merge refusal's re-selection and `rc-gate.sh`'s ctest all read it; changing the box's
width is that one line and an owner decision (§7b rule 3). The gate-speed audit's scheduler replay
(rc-smoke15a's durations, the registered locks and tokens) puts -j6 at 21.7 min against 31.0 at
-j4, CPU contention unmeasured (`~/Developer/spikes/gate-speed-1/`). Every gate runs at
`GATE_JOBS`, and only ONE gate runs on the box at a time (§4c, the gate slot; GATE-COST-1): the
same ~600-row tier took 0.5-0.7 h alone and 2.6-6.6 h beside 2.6-3.9 sibling gates, so the box
finished fewer tiers per hour the more ran at once (SPECS/audits/GATE_COST_2026-10-09.md §3e).
The box still admits Vulkan processes by VRAM itself (§4b, GATE-ADMIT-1). There is no "-j2 while
other gates are live" rule.


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
through the GPU-timing lock (§4). The 250 k / 1 M / 5 M / 10 M shells are baked by the fixture
row `scale.assets` (the tool `scale_assets_gen`; ~25 min for the four, a no-op that reads no blob once
cached; RUN_SERIAL) into THE SHARED SCALE CACHE, one per box and outside every tree:
`~/Developer/testing/scale-cache/v<bake format>/` (CMake `JAH_SCALE_ASSET_CACHE_ROOT`,
`$JAH_SCALE_ASSET_CACHE` moves it). Every reader checks a blob's fingerprint from its header; a
blob another bake producer made is STALE and the fixture row re-bakes it in place. ctest adds the
fixture row to any selection naming a row that reads a shell; a row whose shell is missing or stale
FAILS and says how to bake it — no row measures a smaller asset in its place.

### 1d. THE STAGE-CLOSE ROWS — `stage-close` and `quiet-box` (lanes D6B-GATE-SHAPE, STAGE-CLOSE-1)

THE STAGE-CLOSE ROWS — run by the lead as ONE batch, in the gate slot, at every stage close and
before every push; never by a timer (the owner, 2026-10-09: the daily run the label was first
named after never existed — no such tier ever ran and its timer was never installed — while its
rows rode every scoped gate a broad rule reached: 437 runs, 15.4 h in a week). Two labels, read
by `gate-scope.py` alone:

- **`stage-close`** — the row leaves the MERGE and PUSH tiers (`STAGE_CLOSE_LABELS`) and runs in
  the STAGE-CLOSE batch (§1, `--stage-close-tier`). It is MINUTES OF ONE PROCESS whose push-time
  guard lives elsewhere (atom.cluster_cut 141 s and atom.cluster_crack 136 s — the bake's sweeps;
  gi.chain_converge_scenes 80 s, whose claim gi.chain_converge holds at every push;
  samples.mirror_room_boots, ten boots; gpu.cutout_soak), a CHURN TWIN (shadow.cutout_caster.churn
  and shadow.two_sided_caster.churn: the gate rows' script in ten processes — the gate rows
  themselves run one), or a measurement.
  **A `stage-close` row rides a SCOPED gate ONLY through its OWN subject** (`SUBJECT_ONLY_LABELS`):
  a rule of `STAGE_CLOSE_SUBJECTS` names it, or the change is in its own test directory
  (`tests/<its dir>/`: its source, script or registration). A broad rule that reaches it — the
  engine family, every app row, a header's readers, a `tests/support` helper — leaves it for the
  batch, and the selection prints it by name ("left for the stage-close batch"). A catch such a
  row would have made on a lane waits for the stage close (accepted, 2026-10-09). The subject
  rules (`gate-scope.sh --stage-close-rules` prints them):

  | a change to | still selects |
  |---|---|
  | `irisgl/import/` (the cluster-DAG bake), `irisgl/thirdparty/meshoptimizer*` | atom.cluster_cut, atom.cluster_crack |
  | Photon: `irisgl/engine/src/photon/`, `OgreGi`, `OgrePhotonView`, the voxel gather, `OgreVoxelReaderParity`, the surface cache, `irisgl/engine/media/Photon/`; the fork's VCT / irradiance-field code and media | gi.chain_converge_scenes, samples.mirror_room_boots |
  | the Mirror Room sample, `OgrePlanar`, the screen-probe gather | samples.mirror_room_boots |
  | shadow / caster: `OgreShadow`, `OgreAtomCasterPass`, the Hlms shadow/caster media; the fork's `OgreHlmsPbs.cpp` and its shadow/caster pieces | gpu.cutout_soak, shadow.cutout_caster.churn, shadow.two_sided_caster.churn |

  Once selected, a stage-close row GATES like any row (the label is NOT in
  `SCOPE_EXCLUDED_LABELS`, which means "never gating").
  **A STAGE-CLOSE RED BLOCKS.** `scripts/ci_gate_check.py <base>..<tip> --stage-close [--mode push]
  --build <a build of the tip>` is the judge's SELECTION of every `stage-close` row (not the range's scoped
  rows), judged by the one judge in mode stage-close (or push): the door (a proved `real:<id>` clears through
  the Prover against the base), the aborts, a nondeterminism clearance re-asked 3/3 at the tip, the
  overrides — refused (exit 1) while one is red, a KNOWN RED or unrecorded; `--mode merge` beside it is
  refused, never ignored. rc-gate.sh runs it after the batch (base = the last pushed main) and `push.sh`
  refuses a push on it (base = the previous pushed sha).

- **`quiet-box`**, beside `stage-close` on every row that MEASURES — the wall-clock benchmarks,
  gi.gather_cost (the GPU clock), vr.frame_budget, and the `<suite>.timing` rows (§4) — and on
  open.crash_soak (twelve RUN_SERIAL processes read probabilistically). A scoped gate NEVER runs
  these (no subject rule names one); in the batch they run `-j1` on one hold of the whole card,
  after the other rows.

`source.gate_scope_rules` case 12 holds the split (a change to test_open_responsive.cpp scopes
open.responsive and not open.responsive.timing); `gate.stage_close_select` holds the subject-only
selection, the batch's two phases and the refusal of the retired tier flag;
`gate.stage_close_labels` holds the rename (no retired label or word in tests/, scripts/, docs/)
and the shadow casters' split. The run log's archive keeps the old tier name on its old records.

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
the full tiers stay where the process needs them (a stage close and its stage-close batch, a fork
pin bump, the phase's push). `-j N` / `--jobs N` sets the ctest parallelism (default `GATE_JOBS`); the printed command,
the wall estimate and a fallback's MERGE tier all follow it.

```
scripts/gate-scope.sh <base>..<tip>            # a lane: its base commit .. its tip — prints the selection and why
scripts/gate-scope.sh <range> --run            # run it (DISPLAY, data root set) under THE GATE SLOT (§4c); each row's
                                               #   record -> the run log as it ends; the verdict line, then the target
                                               #   step inside the gate; exits with the GATING phases' code
scripts/gate-scope.sh <range> --resume         # --run, only the rows with no record at the tip (a killed gate, a lost display)
scripts/gate-scope.sh <range> --run --no-targets    # ...without the target step
scripts/gate-scope.sh <range> --run --targets-only  # the target step alone (a stage close; reported, exit 0)
scripts/gate-scope.sh <range> --json           # machine-readable: suites, reasons, rationale, command
scripts/gate-scope.sh --files src/x.cpp ...    # a file list instead of a range (no diff: no symbols, no CMake reading)
scripts/gate-scope.sh --solo <suite> [--times 3]   # the flake protocol (§4), each run logged as a retry
scripts/gate-scope.sh --attribute <row>[,<row>...] --batch <tag> --lanes <lane>:<worktree>:<tip> [...]
                                               # a batch red: each row 3x solo on each lane's OWN tip (§3c)
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

**The lane's own range, the fork, and the fallback** (TEST-SELECTOR-1). The range is the LANE'S
OWN diff: across a forward merge (a merge whose second parent is on d-build's first-parent line) it
starts at that merge's d-build parent, and what came in is the batch gate's (§3c); an
internal branch merged into the lane is the lane's own change. A batch CANDIDATE (§3c) scopes as a
plain range: its merges' second parents are lane tips, never on d-build's first-parent line. A fork pin bump
(`irisgl/thirdparty/ogre-next`) selects by the FORK DIFF'S reach: every built or staged fork file
selects the engine's pixel family and every executable that extracts the engine; only what the
engine never stages (other render systems, unstaged samples media, docs, HLSL/Metal twins) selects
nothing; an unreadable fork diff is the tier. §7b rule 4's one full tier per bump runs at the merge:
`gate-scope.sh <range> --run --fork-tier` (ci_gate_check requires it). Build files resolve
through ctest's own backtraces (the rows registered from inside a changed command's span) when the
tip's file is the working tree's text. The FALLBACK to
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
`scripts/gate-times.txt` — a suite's QUIET median, of its PASS records whose `box.other_ctests == 0`,
when >= 3 such exist, else every record's (medians under sibling gates ran ~26 % high,
TESTING-DEBTS-1); the estimate line counts which source each cost came from).

**THE RUN LOG (TESTING_V2 T8).** `--run` (its tier `scoped`, or `scoped-fallback` / `scoped-tier` when a scoped gate ran the whole tier), `--solo` and the rc-gate tiers
(`scripts/gate_runlog.py run --tier <t> -- <ctest line>`) append one JSON record per row and per
pool arm to `<workspace>/testing/runs/<date>-<tier>-<tip>.jsonl` — the tier one of
`gate_runlog.TIERS` (scoped, scoped-fallback, scoped-tier, target, merge, stage, stage-close, push, smoke,
fork, `lane` — a lane tool's own runs under the lane's name, ignored by batch and push judgement — and
`solo` — a `--solo` batch, the default since GATE-COST-2; any other name is refused before the run).
Every record carries `overrides: [NAME=value …]`, the law switches a HUMAN set for its run
(`gate_runlog.LAW_SWITCHES`: JAH_GATE_SLOT=0, a token count other than 11, JAH_VRAM_ALL, JAH_JUDGE_READ,
the wait and re-queue knobs, a fake journal, a private JAH_VRAM_DIR, a JAH_GATE_SLOT_HELD naming no live
holder) — the push judge refuses a candidate whose records carry any; a `--verdict` is not one. What the
TOOLS set after a whole-card drain timed out is `fallback: drain-timeout` on every record of that run, and
a timing, quiet-box or perf row's record carrying it is not a measurement (the judge refuses it: re-run). A selftest hash that left its record and a trend
step are RECORDS too (`kind: hash-move {pose, old, new}` via `gate_runlog.py hash-move`, `kind: trend-step
{row, target, delta}` written by the gate's trend and `gate_runlog.py trend --record <tier> <lane>`), beside
`kind: abort` and `kind: drain-timeout` (§4c, §4); their suites start with `@`, which no selection names.
A row's record: verdict (PASS | FAIL | CRASH | TIMEOUT | NOTRUN |
NOADMIT | OOM | LOST, §4b), retries, wall seconds, a target's value, the selection reason, the
tree's three shas, `lanes` (the LIST of lanes the gated tree carries — a batch candidate's every
lane; one lane = a one-element list; `--lane a,b` or repeated), the box (load over the suite's own window; the GPU clock state and the sibling
gates sampled AT THE SUITE'S START — ctest's `Start N:` line, TEST-SELECTOR-1 L1; -j, the display).
`gpu_ms` is gone (TEST-SELECTOR-1 L4: no suite ever printed one — 0 of 38,269 records; a GPU time
reaches the log as a target line, `target: <value> (bar <bar>) <what>`). The fields are `testing/runs/README.md`; the two
standing queries are `scripts/gate_runlog.py longest`, `scripts/gate_runlog.py load-reds` and `scripts/gate_runlog.py trend` (TEST-1: every `target:` row as a series over tips; a STEP is a reading outside 3.5 x MAD of the trailing 10 tips it DESCENDS from, under one condition — clock locked or not, the GPU solo or shared — confirmed by the next two descendant readings all out of band on the same side; it names the first tip, its lane and the last in-band tip. Every gate prints it for its own tip after its verdict — `UNCONFIRMED` is a step's first day —, rc-gate.sh writes it to the state file and the d-build merge prints it in its summary. NON-GATING: a step gets the lead's verdict, as a red does).

**THE BUILT FORK MUST BE THE PIN (TESTING-DEBTS-1 T12).** `irisgl/scripts/build-ogre.sh` writes
`<install>/BUILT_FROM` (the ogre-next commit it built, `dirty` on a second line for a dirty
checkout). `gate-scope.sh --run` / `--solo` and `gate_runlog.py run` REFUSE (exit 4, before any
suite) when the ogre-next checkout or that record is not the commit irisgl pins — or the record is
missing — and print the fetch / `submodule update` / `build-ogre.sh` lines that fix it. (A gate on
a stale install tests media the pin no longer matches: REFLECT-MOVERS-1's void 124-minute run.)

**The guard: `gate.selection`** (`tests/hygiene/gate_selection.py`, label `hygiene`) replays
recorded lane diffs (`tests/hygiene/gate_selection_cases.json`: D's last ten lanes, their reds
with each verdict's evidence) against the current build's graph: every REAL red must be selected,
no lane may fall back, a fork bump must select the tier by rule, the audit's S1/S2 subjects select
their suites, and the graph's precision holds (a compiled row whose executable does not contain
the file is NOT selected). **A red that a stage, stage-close or push tier finds and that a lane's
scoped selection missed is a SELECTOR DEFECT: it is fixed in `gate-scope.py` and added to the
cases file** — never answered with a wider rule "to be safe". `source.gate_scope_rules` guards the
tool's own traps (the empty inventory, `--build .`, the targets' split, the fallback's -j).

**ENFORCEMENT (T6; §7b "the rules are tooling, not text").** (1) THE ONE GATE before a merge is
the BATCH's (§3c): `scripts/gate-scope.sh <d-build>..<candidate> --run`, run by `rc-gate.sh` on the
candidate's tree: it prints the selection and every reason, runs it, writes the run log, and exits
non-zero on any gating red (target tests report, never gate, and run after the verdict as their own
step). (2) `source.testing_rules` (label `hygiene`) lints the rules the tree can show: R1
every measuring row (`.timing`, `.benchmark`, `JAHSHAKA_TIMING_BARS=1`) inside the GPU lock; R2
every `stage-close` row priced in `scripts/gate-times.txt`; R3 no copied `ctest -LE` tier outside
`gate-scope.py`; R4 no `RUN_SERIAL` without a comment naming its reason (or the GPU lock); R5 every
app/lint row reachable from a subject (an API module its script or harness calls, a rule's
directory, a tree file it runs). (3) THE MERGE REFUSAL: `scripts/ci-gate-check.sh <range>` exits 1
with the reason unless the run log answers every row and arm the range selects, on a clean tree
BUILT FROM THAT COMMIT (GATE-COST-2: every build writes `<build>/BUILT_FROM` — the studio and irisgl
commits and whether either was dirty — as its last step, `cmake/BuiltFrom.cmake`; each record carries it
as `tip.built`, a gate on a stale build prints `STALE BUILD`, and a record built from another commit or a
dirty tree is never the tip's run — a no-op `cmake --build` refreshes the stamp),
under the flake law (§4) — at the range's tip, or, for a row the last fix round did not reach, at
an earlier commit of the lane (§3b). It prints, per row, the commit its record came from. A hook
or a CI job calls it; `gate.ci_check` and `gate.fix_round` prove it.

**THE JOINT SUITES ARE THE BATCH GATE (BATCH-GATE-1).** `--joint` is retired (it refuses and
names `merge-dbuild-lane.sh batch`): lanes that touched one file family are gated TOGETHER on one
candidate (§3c), whose scoped selection is the union of their reaches and whose tree is the
combination neither lane alone could show.

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

## 3b. Re-gating after a fix (2026-09-11, owner: "no double checking"; MECHANICAL since GATE-SPEED-1)

UNDER THE BATCH GATE (§3c) a lane attributed a red drops out of its batch, fixes, and rides the
next candidate; the reuse rule below is what the judge applies to every range, a candidate's
included. A lane that gets a red FIXES, then runs THE FIX ROUND: `scripts/gate-scope.sh <pre-fix
tip>..<post-fix tip> --run` — the scoped selection of the FIX's own diff, never the lane's whole
selection again. After a forward merge of d-build into the lane, the fix round is `<merge
commit>..<tip>` (the merge commit is the lane's own; a range across it scopes the whole lane again).
A red the fix does not reach is answered where it happened: a recorded verdict, or `--solo` 3/3 for
the contention class — a green re-run at a later commit does NOT answer it. The batch gate before the push is the full safety net. The lead's post-merge
targeted run stays (owner decision): it catches a merge interaction at merge time instead of at
the batch gate.

THE MERGE REFUSAL ACCEPTS IT (`scripts/ci_gate_check.py`; until GATE-SPEED-1 it keyed every record
on the EXACT tip, so every fix round re-ran the whole selection — 45.9 % of all suite-time
09-28..10-01, the gate-speed audit's R1). For a row with no record at the tip it takes the NEWEST
record of that row at an earlier commit A of the lane (its own commits in base..tip; a d-build
commit a forward merge carried in is not one), provided:
- the scoped selection of `A..tip` does NOT include the row (a pool arm: does not include that arm)
  — the same selector and the same reach, so a miss is a selector defect (§3), never a reason to
  re-run "to be safe";
- the fork pin is the same at A and at the tip, and `A..tip` neither falls back nor selects the
  tier by rule — A FORK PIN CHANGE INVALIDATES EVERY EARLIER RECORD (§7b rule 4 stays);
- that newest record decides: green is re-used; a red without its verdict (or a contention red
  without its 3/3) REFUSES — a later red blocks an older green. `--verdict "<row>=<text>"`
  records the answer at the commit where that red happened (repeatable: every `--verdict` counts).
THE FLAKE LAW ACROSS COMMITS: a row green at the tip is STILL refused while an earlier commit that
the fix does not reach holds an open red of it — a verdict-less red, or a contention red without its
3/3. A red at a commit the fix DOES reach is answered by the tip's run.
A row the fix reaches needs its record at the tip. Every row prints its source
(`row <name> <- <sha> (the tip)` or `... re-used from <sha>`); the summary counts both. Measured
on this lane's own two rounds: `spikes/gate-speed-1/`.

## 3c. ONE GATE PER BATCH (BATCH-GATE-1, 2026-10-09; the owner's question 3, spikes/preflight-testing-1)

MEASURED: in the week to 2026-10-09, 87 full-size gates = 26 first gates + 51 re-gates (36 after a fix
round, 11 after a rebase, 4 at the same tip) + 10 rc; a lone full gate 0.61 h, and a second gate on
the one GPU adds no capacity (§4c). The lever left is ONE gate per BATCH of lanes, not one per lane.

**Builders** run their NAMED ACCEPTANCE TESTS + their SUBJECT SUITE (the brief names both), by hand,
never a gate — no slot:
```
DISPLAY=:NN nice -n 19 ctest --test-dir build-linux -R '^(<the named tests>|<the subject suite>)$' --output-on-failure
```
(a tooling lane — a change to the gate's own scripts — still runs its own scoped gate, the lead's
exception.) The lead's per-lane merge (`merge-dbuild-lane.sh <lane> …`) still judges a single lane.

**The lead** stacks the ready lanes as ONE CANDIDATE and gates it once:
```
~/Developer/scripts/lead/merge-dbuild-lane.sh batch <tag> <lane>:<studio tip>:<irisgl tip> [...]   # in the lead's order
#   every lane, before any ref moves: the judge-diff refusal (JAH_JUDGE_READ="<tip> <tip>" for a read one), check-trailers,
#   THE PIN CHECK, the dry merges of both repos against the RUNNING candidate (lane 1 on d-build, lane 2 on d-build +
#   lane 1, …) — any conflict refuses the whole batch, naming the lane and the files, and nothing moves; then the
#   candidate on branches batch-<tag> ($D, $D/irisgl; never d-build), fork-pin-check.sh on its irisgl tip, and it
#   prints THE ONE gate command (a refusal at any point removes every ref the run made):
JAH_GATE_TIER=scoped JAH_GATE_RANGE=<d-build>..<candidate> JAH_GATE_LANES=<lane>,<lane> \
    setsid nohup ~/Developer/scripts/lead/rc-gate.sh batch-<tag> <candidate> > /tmp/jah-lead/rc-batch-<tag>.out 2>&1 < /dev/null & disown
#   (JAH_GATE_TIER=fork when the candidate moved the fork pin: gate-scope's --fork-tier, the whole tier) — a fresh
#   tree, the six hashes, the slot taken once, `gate-scope.py <range> --run` inside it (the card per phase)
~/Developer/scripts/lead/merge-dbuild-lane.sh batch-land <tag> [--build <rc build>] [--display :NN] [--control-tree <dir>] [--verdict "<row>=<text>" ...]
#   d-build's judge, UNCHANGED: ci_gate_check.py d-build..candidate --build <the candidate's build> — green:
#   d-build fast-forwarded to the candidate in both repos (irisgl first), the HASHES line; a stale candidate (d-build
#   moved since `batch`) is refused; red: nothing moves and the red rows are ATTRIBUTED on the rig display NAMED by
#   --display (:60-:99, its X lock present; the environment's DISPLAY is never read) — without it, the command:
scripts/gate-scope.sh --attribute <row>[,<row>...] --batch <tag> --candidate <rc tree>:<candidate> \
    --control <rc-base>:<d-build tip> --display :NN --lanes <lane>:<worktree>:<tip> [...]
```
**THE GENERATED FILE.** `docs/SCRIPTING.md` is `--dump-api-docs`'s output and `api.contract`
byte-compares it: two lanes' versions cannot merge as text. When more than one lane touched it (or it
conflicted), `batch` records `SCRIPTING_REGEN=1` in the batch's state; `rc-gate.sh`, after building
the candidate and BEFORE its gate, runs `merge-dbuild-lane.sh batch-scripting <tag> <rc tree>`: the
candidate's own binary regenerates the file, a difference is committed onto `batch-<tag>` ("SCRIPTING.md
regenerated at batch <tag>", author jahshaka), the tree is rebuilt and THAT sha is gated and landed.
**THE FORK FREEZE** (owner decision 4; TESTING_V3_SPEC §1.3.2). Every lane's fork pin (and d-build's)
is printed; the batch carries ONE pin P — d-build's, or a DESCENDANT of it on the fork's `jahshaka`
branch (then a fork-tier batch) — and EVERY lane must pin exactly P: a lane on any other pin, an ancestor included, is
REFUSED BY NAME ("re-pin <lane> to <P>") and the lead has it re-pinned before the cut; two pins that
are two fork lines refuse the batch (exit 5). P ≠ d-build's pin = the candidate moves the pin = the
full tier.

**THE ATTRIBUTION.** Each red row runs 3x `--solo`-style in each lane's OWN worktree at its exact
batch tip (the lanes' built trees; a worktree moved past its tip is refused), 3x at the CANDIDATE
(its rc tree) and 3x at every CONTROL — `--control` is REQUIRED (several allowed); batch-land's is the
BASE BUILD `rc-base` (`--control-tree` moves it), refused plainly when it is missing, its HEAD is not
d-build's tip, or it has no build-linux — never `$D`, the merge target, which has no binary
(TESTING_V3_SPEC §1.3.2). `--display :NN` is REQUIRED too (:60-:99 with its X lock; the environment's
DISPLAY is never read). The whole card is taken once through `hold_card()` — whatever the card's
admission demands, it never assumes the gate slot is free. One table `row | tree | n/3 red | the first
failing check`; the records carry `reason: attribute:<tag>`, retry, and in `lanes` that lane (the
candidate's: the batch's list; a control's: its tree's name). Only REAL verdicts count: a run that
never got its admission (NOADMIT) or never ran leaves its cell INCOMPLETE. A row a tree's build does
not register is ABSENT there and never blames it. Per row, IN THIS ORDER:
1. a CONTROL cell INCOMPLETE or aborted → the row is INCOMPLETE: nothing is named without its baseline;
2. red on a CONTROL (d-build's own tip) = a D-BUILD DEFECT: it names nobody (kind `defect`);
3. a lane is NAMED when ANY of its solos is red (the flake law: one red run is a red, never outvoted)
   — it DROPS OUT and the rest are RE-GATED as a new candidate (a new tag — the exact-tip rule stands:
   no prefix records);
4. any other cell INCOMPLETE or aborted → INCOMPLETE (re-run the attribution);
5. red at the candidate and green on every lane's own tip and on d-build's = a COMBINATION DEFECT
   (kind `combination`): the batch is REFUSED;
6. green at the candidate too = NOT REPRODUCED (kind `nondeterminism`): it passes the verdict door with
   its solos recorded — a contention-class row by its 3/3 at the candidate tip, any other by `--verdict`.
Findings 2, 5 and 6 are REGISTERED, never only printed: one `<workspace>/testing/defects.pending/<id>.json`
each in TESTING_V3_SPEC §1.5's FULL schema (`{id, rows, kind, cause, first_seen {tip, pin, run}, state: open,
found_by: gate, recheck, expires}` — recheck a DATE: the next day for NOT REPRODUCED, +7 days for a
defect; NOT REPRODUCED also `uses: 1, suspects: [the batch's lanes], census`), which VERDICT-1's registry
(`testing/defects.json`) ingests — its reader quarantines a malformed entry (it never reaches the door), and
gate.batch loads every pending file through it. **ONE ENTRY PER ROW AND KIND, AND A SIGHTING NEVER REWRITES IT**
(TESTING-CLEANUP-2B items 14-15): ids are `<BATCH>-<ROW-UPPER>-<n>` (n the next free number of the stem); a row that
already has an OPEN entry of the kind (registry or pending) gets no second entry — the new sighting is appended to
`testing/defects.pending/<id>.recheck.json` (`{id, sightings: [{tip, pin, run, batch, cause, date, recheck_proposed}]}`)
and the entry itself — `testing/defects.json` and the pending files are TRACKED — is never re-serialised: its `recheck`
stays the lead's, so a sighting never stands in for the re-verdict the door demands of an entry past its date. The reader
attaches the sightings to the entry (`sightings`), never reads the file as an entry. Never reused: a NOT REPRODUCED entry
(single-use at its batch) and a fixed or retired one (a new sighting is a new finding). Registrations hold the pending
directory's flock across the scan, the numbering and the write: two attributions at once never take one number. Exit codes: 3 a
combination defect, else 7 an INCOMPLETE or aborted attribution, else 5 a d-build defect, else 0; 4 an
unusable tree, 64 usage. Every commit the batch tooling writes (the candidate's merges, the regenerated
SCRIPTING.md) is authored jahshaka by the script itself, and batch-land runs check-trailers on the
regenerated commit.

The judge needs no change: `ci_gate_check.py` keys records on the exact Studio sha + its irisgl pin,
and d-build fast-forwards to the gated candidate in both repos. The queue rules (a pin-bumping
lane first, owed re-runs before a fresh tier) are the lead's, not tooling. Guard: `gate.batch`
(tests/hygiene/gate_batch_test.py, a toy repo pair).

## 4. Flake protocol (the law in the refusal since TEST-SELECTOR-1)

A red of a CONTENTION-CLASS suite is re-run SOLO 3× (`scripts/gate-scope.sh --solo <suite>`: each
admission of a solo run takes the whole card, §4b); 3/3 green = environmental, with the evidence
string in the report (host-load timing). ANY OTHER red needs a
recorded verdict that passes THE VERDICT DOOR (below; VERDICT-1):
`scripts/ci-gate-check.sh <range> --verdict "<row>=<text>" ...` writes it into the run log, per row and timestamped (it clears only the reds logged before it; a row that never ran — NOADMIT — is missing, which no verdict clears). THE MERGE REFUSAL
APPLIES THIS (TEST-SELECTOR-1 L2/L3): `ci-gate-check.sh` refuses a merge while a selected row was
never run at the tip, a contention-class red lacks 3/3 solo PASS after it (a solo red means it is not
contention: a verdict), or any other red lacks a verdict — one solo PASS erases nothing — and
`scripts/lead/merge-dbuild-lane.sh` calls it before it merges. A
`VK_ERROR_OUT_OF_DEVICE_MEMORY` red is NOT environmental since GATE-ADMIT-1 (§4b): the box admits
by VRAM, so an OOM means the budget is wrong (a class under-counted, a row outside it) or an
unadmitted process filled the card — the verdict names which (`scripts/gpu-admit.sh status` and
`nvidia-smi` beside the red). THE CONTENTION CLASS IS DATA: the defect registry's enrolled
`nondeterminism` entries (`<workspace>/testing/defects.json`, below), read by the refusal and by `--solo` — never a
prose list; a row joins it by the lead's `defect enrol` (a measured rate and the census). Every failure in a gate
report carries a verdict through the door; a report without verdicts is not a gate.

**THE VERDICT DOOR (lane VERDICT-1, 2026-10-09; TESTING_V3_SPEC §1.4-1.6 is the law text; the preflight measured 58
verdicts of a week, 6 clearing solos below 3/3 by prose, 2 LOSTs called environmental with no journal read, 13
never-ran pools recorded FAIL).** `ci_gate_check.py` accepts registered facts, never prose; `--verdict "<row>=<text>"`
keeps its syntax and the text is parsed:
- **`real:<ID>`** — `<ID>` is in THE DEFECT REGISTRY (`testing/defects.json`, below) for this row, not retired, not
  past its recheck date, and PROVED: a PASS record of the row at a later sha that REACHES the row (a descendant of the
  red's sha whose change the selector maps to the row — a PASS at the red's own sha, at an ancestor, or after a
  docs-only / empty commit is the same code passing again: nondeterminism, 3/3 solo), or the SAME red — the same status text too,
  the same verdict class and the same failLine with its numbers masked — reproduced by a recorded solo at the
  range's base (a d-build commit): the row is then **KNOWN RED** — `--mode merge` passes it, `--mode push` and
  `--mode stage-close` refuse it (never push on a red). A ticket named alone clears nothing; a CRASH never.
  A `nondeterminism` id clears a FAIL/TIMEOUT by 3/3 solo PASS (never a LOST/OOM/CRASH or an Xid red: those take a
  `kind: defect` entry) — a SINGLE-USE (NOT REPRODUCED) one only in the merge that registered it, never a push. A
  nondeterminism clearance passes a merge; a push or stage close re-asks 3/3 solo green AT THE CANDIDATE.
- **`contention:<evidence>`** (FAIL / TIMEOUT only) — the row has an ENROLLED, open `nondeterminism` entry within its
  recheck date (the contention class), the red's **competitor census** (`box.census`) shows real competition — a GPU
  process of ours outside the gate, one whose exe is not in the idle baseline `testing/box-baseline.json`, or any
  above its `vram_floor_mb`; a sibling ctest; a build outside the gate's tree; memory or IO pressure ≥ 10 % avg10;
  NEVER the gate's own queue or drain, never the idle desktop — AND 3/3 solo PASS after the red (the solos may run after the verdict). The class's own 3/3 clearance
  (no verdict) needs the same census.
- **LOST / OOM / CRASH** — `real:<ID>` only; `xid-read:<journal window>` ONLY when the record says
  `journal_unreadable` (else the Xid would be in the record), the window covering the red. A red whose record
  carries an `xid` (a pid of the row's own process tree) takes `real:` only — never environmental (CLAUDE.md).
- **solos below 3/3 are red** — never cleared by text, listed or not. NOADMIT / NOTRUN never ran: nothing clears them.
- **`environmental`, ENOSPC and a dead display are never verdicts** — the cause is a registered `box` defect or the
  run's abort record; a row an abort dropped RED (`droppedRed`) needs 3/3 solo PASS after it, whatever else answered it.
- A lane-tool record (tier `lane`) never answers a row for the judge; `overrides` in the candidate's records refuse a
  push or a stage close.
A refused verdict prints `VERDICT REFUSED <row>: <why>` (the rule named) and the row stays red.

**A REBASE CARRIES ITS OPEN REDS (VERDICT-1 U4).** The judge reads, for a lane's tip, the lane's OWN records
(`lanes == [<lane>]`, no `batch` tag; `--lane`, else the tip's records, else the branch) at any other tip, ancestor or
not, and refuses the tip while a red there has no later green record of the same row+arm at the tip and no verdict
through the door: `OPEN RED carried from <old tip>: <row>` (a red at a lane tip that DESCENDS from the checked tip
is never answered by the tip's PASS: that code predates it). Batch records never carry. `--verdict` records the answer
AT the old tip. Forward only: schema-2 records; the historic cases are `scripts/gate-report.py --carried`.

**THE DEFECT REGISTRY (TESTING_V3 §1.5) — `testing/defects.json`, owned by VERDICT-1's reader
(`gate_runlog.defects_load()`).** `{"defects": [{id, rows, kind: defect|nondeterminism|selector|box|combination,
cause, first_seen {tip, pin, run}, state: open | {fixed: {tip}} | retired, recheck: YYYY-MM-DD, found_by:
read|gate|owner|lane}]}`; a `nondeterminism` entry is single-use (`uses: 1, suspects, census`) or `enrolled {by, rate,
census, date}` by the lead. BATCH-GATE-1's pending entries (`testing/defects.pending/<id>.json`) are read with it. The
malformed entry or pending file is QUARANTINED (moved / copied to `testing/defects.quarantine/` with a `.why`, printed as
`REGISTRY: <file> quarantined: <why>`, a finding until fixed) and the rest loads; only an unreadable registry FILE
leaves the judge unusable (exit 2). The contention class is
the enrolled nondeterminism subset; `contention.json` is gone.

**THE Xid IN THE RECORD (VERDICT-1 U3).** `vram_tokens.py`'s supervise reads the kernel journal (through
`kernel_xid.py`) after every admitted row and prints `XID … from pid <p> of the row …` and `XID-WINDOW <start>..<end>`;
the run log records `xid: null | {pid, window, lines[]}` on the row (and an arm whose process faulted, from the pool
runner's `ARM … CRASH … xid <n>`), and `journal_unreadable` when the journal could not be read. The weekly read is
`scripts/gate-report.py` (testing/runs/README.md).

**THE GPU-TIMING ADMISSION (lane DEVPROCESS-1, 2026-09-23; THE TIMING LIST since D6B-GATE-SHAPE,
2026-09-27; ONE ADMISSION since TEST-SELECTOR-1, 2026-10-01).** `RUN_SERIAL` serialises only
inside one ctest process while every lane is its own. THE RULE: **a suite that measures time or GPU
budget takes ALL the VRAM tokens (§4b) — the card drains to it and nothing else, timed or untimed,
shares the GPU while it measures; everything else shares the GPU within the budget; a measurement
lane (debug-runner) wraps every `perf.capture` run the same way.** The wrapper is
`scripts/gpu-exclusive.sh [--run-timeout <s>] [--label <row:class>] <command…>` = `vram_tokens.py
admit all --timing`: the turnstile keeps the queue behind a waiting timing row (a stream of small
requests cannot starve it — OUTSIDE a gate: inside one the gate's rows pass the turnstile, see below), a bounded 900 s wait (`NOADMIT vram: …`, exit 75, the command never
runs), the command exec'd in place so a ctest timeout still kills the suite itself and the tokens
die with it. (Until TEST-SELECTOR-1 it was a separate `flock` that excluded only the OTHER timing
rows: gi.rt_reflect_cost and gi.field_scroll went red beside sibling GPU rows — plan 9ab.)
**THE WHOLE CARD ONCE PER PHASE (GATE-COST-1 P2):** the gate's serial timing phase, a
`gate-scope.sh --solo` batch and the target step take EVERY token ONCE (`vram_tokens.hold_card`;
the line `vram: the whole card (11 tokens) held for the phase, drained in <s> s`) and their rows'
own admissions run nested on it (`already admitted by the parent`; each timing row still prints
its `gpu-lock: waited` line, 0 s). One drain per phase instead of one per row — 278 per-row drains
in 38 h, the union of whole-card drain or hold 17.0 of ~27 active hours (the audit's §3f) — and
nothing shares the card while any row of the phase measures. A solo retry is therefore solo on the
GPU too. **A WHOLE-CARD HOLD ALWAYS TAKES THE GATE SLOT (GATE-COST-2):** outside a gate it queues for
the slot (§4c) FIFO with the gates before it drains — a drain out of the slot kept the admission's
turnstile up to an hour while the gate in the slot NOADMITted every row at 900 s (the audit's
62-of-70 turnstile-queued NOADMITs; the band-aid audit's #2). A drain past `JAH_VRAM_PHASE_WAIT`
(3600 s) prints the card's holders with their AGE, writes a `kind: drain-timeout` record into the
run log (suite `@drain-timeout`: the holders, the wait) and the phase runs with per-row admission
(a solo run: `JAH_VRAM_ALL=1`, every admission takes the whole card itself).

**THE LOCK LIST IS THE TIMING LIST** (`JAH_GPU_EXCLUSIVE_SUITES`, `tests/CMakeLists.txt`,
registered by `jah_gpu_exclusive_test()`, whose `RUN_TIMEOUT` is the suite's own budget and
whose TIMEOUT is that + 30 s — the admission's wait is outside it, below; configure fails if a listed suite is
registered any other way). A suite is on it because it asserts milliseconds, a ratio of milliseconds or a frame
budget — never for a flake history: the six members that measured no time LEFT it
(app.engine_selftest_validation, claude.chat, ui.media_lazy, scripting.e2e.space_switch /
sun_light / reflection_map; audit R4 — ui.media_lazy had waited 88 s for it), and the timing
ratios that sat under RUN_SERIAL only JOINED it (mirror.scale, perf.epic_steady_state,
perf.drag_mirror_room, perf.capture_off_is_free; R3), with app.play_select beside app.input_keys
(key/gesture arrival; R5). Every timing row also takes ctest's `RESOURCE_LOCK gpu_timing`, so two
of ONE gate never start together and wait in a slot on the admission. `ctest -N -V | grep -c 'Test
command: .*gpu-exclusive.sh'` = 37 (a bare `grep -c gpu-exclusive` also counts the guard's own
command line). Its guard is `devprocess.gpu_lock` (label `tooling`). A contention verdict on a
timing row needs a sibling that took no token (an app on `:0`, a measurement outside the wrapper,
or — the admission never excludes it — the CPU load of the gate's own other slots) — say which.

**THE ADMISSION'S WAIT IS NEVER THE ROW'S TIME** (LOCK-WAIT-1, stage close 1: perf.epic_steady_state
ran ~30 s solo and was killed at its TIMEOUT in the stage tier by queue time). The wrapper prints
`gpu-lock: waited <s> s` once it holds the card; the run log records it per row as `lockWaitS`
and subtracts it from the row's `seconds` (the raw ctest figure stays as `wallSeconds`). A timing
row's own budget (`RUN_TIMEOUT`) is enforced by the wrapper through timeout(1) FROM AFTER the
admission, so ctest's TIMEOUT (budget + 30 s + the wait's bound) is only the backstop and a queued
row never runs short; a wait past the bound is the run log's NOADMIT (never ran — the box's queue,
not the row's code), and a row stopped by its own budget is a TIMEOUT.

**THE MILLISECOND BARS ARE STAGE-CLOSE ROWS: counts at push, milliseconds in the batch's quiet-box phase** (D6B-GATE-SHAPE;
audit §5). A wall-clock bar reads the box as much as the code, and the GPU lock does not exclude
the CPU load of a -j4 gate (open.responsive's 300 ms frame bar read 605 ms cold on a UI thread
that was not blocked). So a suite with such a bar is TWO ROWS OVER ONE BINARY: the push row
asserts every count, structure and picture claim and PRINTS each millisecond reading
(`time: within|OVER: …`); `<suite>.timing` (labels `stage-close;quiet-box`, the lock) runs the same
binary with `JAHSHAKA_TIMING_BARS=1` and asserts them (`tests/support/timingbars.h`,
`JAH_TIMING_CHECK`; for a `--script` suite, `jah_timing_script()` generates the armed copy). Rows:
open.responsive, archive.responsive, avatar.responsive, chain.hzb, engine.gpu_cull,
gi.field_follows, ui.properties_filter, ui.components, meshbake.cards, app.update_check,
app.shutdown_order, vr.warmup, scripting.e2e.editor_controls. Nothing was deleted: a bar that
reds only on a loaded box is read in the stage-close batch, never widened.

**THE SERIAL FAMILIES** (D6B-GATE-SHAPE; audit R1/R2). RUN_SERIAL stops EVERY other suite of the
gate while one runs (54 rows, 1,487 s of the audit's 3,125 s gate, 48 % of its wall with one row
running). A group that must not overlap EACH OTHER takes a `RESOURCE_LOCK` instead, and a COST
above every measured row so its chain starts first and overlaps the gate:
`monado` — the sixteen rows that start a Monado runtime (the fourteen of the push tier plus
the stage-close vr.frame_budget and vr.warmup.timing) (each runner's IPC socket lives in its
own XDG_RUNTIME_DIR, so the "one socket" reason was void; vr.eye_grade captures the compositor
with `xwd -id` and therefore runs on ITS OWN Xvfb, displays 241-299); `gi_chain` — the
cascade-chain rows (their VRAM reason was measured false). `threading` — the four compile halves of tests/threading (mode / mode_serial / gi_resolve /
gi_resolve_serial; GATE-SPEED-1: they were RUN_SERIAL; their claim is a byte compare, the verdict
is in their CMake). RUN_SERIAL remains only where the audit kept it (app.input_keys,
app.watchdog_stall, gi.field_scroll, newproject_stall.timing, and the stage-close benches).

## 4b. THE VRAM BUDGET — box-wide tokens (lane GATE-ADMIT-1, 2026-09-27)

**Why.** Every lane's gate is its own ctest process, and ctest's own admission (RESOURCE_GROUPS,
RESOURCE_LOCK, RUN_SERIAL) stops at the edge of ONE ctest process. Four lanes at -j4 put ~16
Vulkan processes on one 16.4 GB card; on 2026-09-27 ~47 reds printed
`VK_ERROR_OUT_OF_DEVICE_MEMORY` (a 64 MB pool allocation, latched by Ogre as a device loss), all
where four gates overlapped (SPECS/audits/GPU_LOSS_AUDIT_2026-09-27.md A3: the model peaks 6.5 GB
for one lane, 19.5 GB for three, 26 GB for four). So the budget lives outside ctest, like the
GPU-timing lock.

**THE MECHANISM.** `scripts/gpu-admit.sh <k> [--label <row>] -- <command…>` (implementation
`scripts/vram_tokens.py`): N = 11 flock TOKENS in `/tmp/jah-vram/` (`JAH_VRAM_TOKENS` overrides;
0 = admission off). DEFAULT 11: 1 token measured 1,090 MiB at the peak (two MERGE tiers sharing
the budget: 13,333 MiB with 12 held over a 280 MiB desktop; ~820 MiB at p95), so 11 tokens peak at
~12.2 GB and leave ~1.3 GB beside the owner's 2.8 GB instance on the 16.4 GB card (12 left 243 MiB).
The owner's instance, the desktop and any app started by hand without the helper take no token
(start such an app as `scripts/gpu-admit.sh 2 -- ./Jahshaka …` — `nvidia-smi` is never consulted: racy,
slow, and blind to what a process allocates next; the token count is the contract, tuned by
measurement). An acquirer takes the TURNSTILE, reads the free tokens from `/proc/locks` and locks
the k LOWEST only when k are free — all or nothing, never hold-and-wait, and a 3-token row at the
head of the queue is not starved by 1-token rows behind it — EXCEPT BY A GATE (TESTING-CLEANUP-2B fix round 2/3): a
row of the gate holding the slot owns the card, so it does not wait behind a turnstile someone else holds; it scans the
tokens itself (re-scanning while enough are free — a sibling row of the same gate scanning at once is a race, not a
shortage; bounded by attempts) and is admitted, NOADMIT only when the free tokens are truly short, the line naming the
request ahead ("a row of this gate: …" or the foreign pid and label). A waiter at the turnstile DURING a gate can
therefore be starved until the gate ends, by construction: every gate and every class-2 measurement queues at the SLOT,
so such a waiter is unslotted (`JAH_GATE_SLOT=0`, a hand `gpu-admit.sh` run, an old tree's script) and its wait is
bounded by the gate (and by its own `JAH_VRAM_WAIT`). The command is EXEC'D IN PLACE with
the token fds inherited: the pid ctest started is the suite, and the tokens are freed when it and
everything it spawned exit, for any reason (a crash, a ctest timeout kill). A nested admission
(`JAH_VRAM_HELD` in the environment) runs on its parent's tokens. A wait prints ONE line —
`vram: waiting for <k> tokens, <n> free` — and `vram: admitted with <k> tokens <i,j> after <s> s`;
it is bounded at 900 s (`JAH_VRAM_WAIT`), after which the command never runs (exit 75) and prints
`NOADMIT vram: no admission for <k> tokens within 900 s (<n> of <N> free …) — <row>`: the run log
(`scripts/gate_runlog.py`) records that row — or a pool's never-started arms — as verdict
`NOADMIT` with the line as its status, never a generic FAIL (the box was over-subscribed; nothing
about the row's code). **A NOADMIT IS RE-QUEUED IN THE SAME RUN (GATE-COST-1 P5):** a row that got no
admission within its wait (or a pool whose every arm got none) is re-run at the end of the same
`run_ctest()` — normal admission, after the other rows — up to `JAH_GATE_REQUEUE` (2) more times
(`=== re-queued <n> row(s) that got no admission …`). EVERY TRY IS RECORDED (GATE-COST-2): a held try
as verdict `NOADMIT` with `requeued: <k>` (k = 0 for the first run), the last try as what it was (a
pass, a red, or NOADMIT), also carrying `requeued` — NOADMIT is "never ran" to the refusal, so no
verdict moves, and the run log still counts how often admission failed. The
hand `--solo` re-run of a NOADMIT, which asked for the whole card and queued behind the same
congestion (§1906: 15 rows, 3.4 h), is no longer the path. A burst of final NOADMITs means the box
asked for more than 900 s of queue three times over.
A red row (or pool arm) whose OWN output carries the engine's in-frame OOM line (`GPU out of memory
(VK_ERROR_OUT_OF_DEVICE_MEMORY; the device is NOT lost)`, exit 1) is recorded `OOM`, one carrying the
loss line (`THE GPU DEVICE WAS LOST` / `the graphics device was lost`, exit 3) `LOST` — the matched
line rides as `budget` (TESTING-DEBTS-1). `OOM` is the budget class: an unadmitted process or a row
over its class (read `scripts/gpu-admit.sh status` + nvidia-smi per pid); `LOST` is never
environmental (look for the Xid). A PASS that logged an OOM warning stays PASS.
`scripts/gpu-admit.sh status` lists the holders (the file of a held token names its pid and row).

**THE CLASSES** (the audit's A2 table, nvidia-smi per pid, 2026-09-26) — `jah_vram_tokens()` in
`tests/support/vram_tokens.cmake` is the one lookup (the toy row `gate.tokens_by_needs` proves it):

| class | tokens | what | measured |
|---|---|---|---|
| `app` | by the declaration (below) | an app process (`Jahshaka --script`, a pool process, a harness that spawns it) | the old flat 2 was per pid median 2,000 / p90 2,270 / max 2,600 MiB — a THIN margin against 2 × 1,090 = 2,180 |
| `selftest` | 2 | `--engine-selftest` (the app's own route; binds no editor scene, declares nothing) | = app at Epic |
| `engine` | 1 | a headless engine / Qt+engine suite at its own tier (mostly Medium) | ~0.6-1.2 GB |
| `vr` | 3 | a VR / Monado row (the app, stereo views, the runtime's compositor) | ~2 GB (est.) |
| `none` | 0 | RenderSystem_NULL, no display, lavapipe — never registered | 0 |

**THE BOOT DECLARATION (TEST-NEEDS-1; owner 2026-10-09: "a test suite passes variables for what it
needs; if you don't need Photon, run Low").** Every row that starts the app — `jah_gpu_row` /
`jah_gpu_exclusive_test` CLASS app (and CLASS vr when its command names the app), every engine-up
`jah_add_pool` — declares `TIER <low|medium|high|epic> NEEDS <photon bloom ssao smaa planar…>|NONE`,
or `TIER document`. The helpers put it on the row as `JAHSHAKA_TEST_TIER` / `JAHSHAKA_TEST_NEEDS`
(`set:`; `unset:` both for `document`); the process puts every scene it binds on that World Mode and
switches every feature the list does not name OFF (`worldmodes::applyTestTier`; a pinned row stays;
a named Photon runs at the mode's own Photon tier). `app.testTier()` reports `{tier, needs}`.
- `low NONE` — the row reads no picture and exercises no engine lifecycle: UI state, document,
  scripting verbs, counts.
- `epic NEEDS photon` — the engine lifecycle (startup, open/close, teardown, shutdown order, crash
  soaks): Photon stays on exactly where the teardown order matters (trap 1).
- a GI claim — the tier its bar names, `NEEDS photon`; a shipped-picture claim — `epic` with every
  feature it reads (all five for the shipped frame: perf.epic_steady_state, vr.frame_budget); a
  script that calls `world.mode(X)` itself declares X with all five (setMode puts every column back).
- `document` — no test tier: for a claim ABOUT the document's World state (a sample opens at its
  authored tier with no deviations, a saved World row survives a reopen, the product's own boot);
  a test tier would rewrite it before the claim reads it. Today's former `TIER epic` pools are
  `document` (their contract was "each scene keeps its own tier").
There is no default: an undeclared app row is a configure error, and `source.row_declares_needs`
refuses one on what ctest will run — and refuses `NONE` on a row whose scripts read the picture
(editor/player/camera.screenshot, vr.eyeScreenshot, editor.presentedFrame, capture.lastFrame;
xwd, readPixels, --engine-selftest in a harness) or drive Photon (world.photon/gi/setPhotonView/
giStatus/giVoxelStats) — unless its claim IS the picture with every feature off (a "GI off"
claim), which it states: `NEEDS NONE NEEDS_WHY "<the claim>"` (the lint prints every one).

**THE APP'S TOKENS ARE THE DECLARATION'S** (`JAH_VRAM_TOKENS_BY_NEEDS`): `low` 2; `medium` 2;
`high` 2; `epic` + photon 3, without 2; `document` 3 — measured per pid and additive on the card
(the numbers in vram_tokens.cmake).
**THE WINDOW IS ITS OWN DECLARATION**: `TEST_WINDOW` boots a windowed script run at 1280x720
(JAHSHAKA_TEST_WINDOW); a tier never implies it, so a row that gained a tier kept the screen-sized
window its bars were measured at. The rows TEST-TIER-1 measured at Low carry it.

A row MEASURED heavier than its class declares `TOKENS <k>` = round(its peak GB) with the number in
its comment (GATE-ADMIT-1's run, nvidia-smi per pid: test_gi_gather 3.6 GB → 4, voxel_coverage
2.8 → 3, gather_reference / hit_shade / chain_face / every test_scale row 1.5-2.0 → 2). A HARNESS
that spawns the app over MCP (compiled with JAHSHAKA_BINARY: open.responsive, ui.column_law,
mcp.e2e, …) is `CLASS app` — the app it starts is the process that holds the VRAM.

**HOW A ROW DECLARES ITS CLASS.** Every row that boots Vulkan is registered through ONE of:
`jah_gpu_row(<row> CLASS <class> COMMAND <exe-or-target> [args…] [WORKING_DIRECTORY <dir>])` (a
drop-in for `add_test(NAME … COMMAND …)`); `jah_gpu_exclusive_test(NAME … CLASS <class> …)` (a
timing row holds the GPU lock FIRST, then its tokens: the lock for exclusivity, the tokens for
memory); `jah_add_pool(<pool> [CLASS vr] [TIER …])` — the pool's tokens are taken by
`run_pool.py --vram-tokens <k>` ONCE for the pool's whole run (every app process, a restart included, inherits
them — TESTING-CLEANUP-2B F-C1: a token taken between two processes no longer turns the pool's remaining arms NOADMIT; a `HEADLESS` pool takes none). A row that boots no Vulkan is a plain `add_test` with
`jah_no_display()`. **The wait never eats a row's budget, and the TIMEOUT a site declares is the row's whole
bound** (TESTING-CLEANUP-2B; `tests/cmake/vram_rows.cmake`): every registered row carries `RESOURCE_GROUPS
vram:<k>` and the build names its resource spec (`CTEST_RESOURCE_SPEC_FILE`: one `vram` resource, the box's
token count), so ctest — a gate, a tier, a hand run — never STARTS a row whose tokens its own running rows hold,
and the clock starts with the row. Only a process outside the ctest (a hand-run app holding tokens) can still
take a row's tokens — and INSIDE A GATE the row's admission then does not wait at all: it exits 75 (NOADMIT,
never ran) and the gate re-queues it at its end (P5); a final try still short stays NOADMIT, which the judge
reads as MISSING (re-run it), never a skip (`vram_tokens.row_wait_bound`). Outside a gate a hand run waits as
before (its wait subtracted from its seconds, `tokenWaitS`; a TIMEOUT that ends the wait is recorded NOADMIT). (Until 2026-10-10 every GPU
row's TIMEOUT carried a hidden +900 s for the wait, and rows ran past their own budget unseen — the shadow churn
twins 1,008 / 1,159 s against 900.)

**THE CLOSURE.** `source.gpu_rows_closure` (hygiene) reads what ctest will run
(`ctest --show-only=json-v1`): every row whose command or environment names an Ogre-linked
binary (`ldd` → libOgreNextMain), runs a binary that carries the app's path as a literal (a
harness that spawns the app) or runs the pool driver, minus the `jah_no_display` rows, the
app `--headless` rows and the lavapipe rows (host memory), minus the registered rows, must be
EMPTY, and every row that runs an app process declared an app-sized class (app/selftest/vr, read
from its `--label <row>:<class>`) — each offender is named with its CMakeLists line. At
GATE-ADMIT-1: 322 Vulkan rows, all registered (192 at 1 token, 113 at 2, 14 at 3, 3 at 4 — the
suite prints the count).
`--self-test` proves the detector names a synthetic unregistered row. The helper's own guard is
`devprocess.vram_admit` (tooling): 14 fake rows of the three classes against 12 tokens on the
kernel's lock table — the bound, all or nothing, lowest first, a killed holder frees,
`JAH_VRAM_TOKENS=4`, the bounded wait, the row as the admission's child (exit codes and signals
through, a SIGTERM forwarded), the Xid read, nesting.

**THE KERNEL'S WORD ON EVERY GPU ROW** (TEST-SELECTOR-1 H4; ONE reader, `scripts/kernel_xid.py`).
The admission (`vram_tokens.py admit`, every `jah_gpu_row` and timing row) runs the row as its
child, tracks the row's process tree (the harness, the app it spawns), and after the row reads the
kernel journal since the launch (`journalctl -k`, never sudo): an `NVRM: Xid` line from any pid of
that tree prints `XID <n> from pid <p> of the row …` and turns the row red (never environmental).
A pool (`run_pool.py`) reads the same way per app process and turns the arm running at the fault's
second into `ARM <pool>.<arm> CRASH <ms> xid <n> …` (a process's verdicts are printed once, after
that read). An unreadable journal is a printed FINDING in every user, never a red of every row: the
ONE row that reds for it is `devprocess.kernel_journal` (tooling; it names the fix — the user joins
`adm`). After EVERY row — green too (GATE-COST-2; a fault on a passing row must not be read before
journald has it) — the read waits 1 s: the Xid is logged at the fault, seconds before the process
ends (the fence wait until DEVICE_LOST measured 10-11 s), so only journald's millisecond ingest is left. (`tests/support/no_xid_run.sh`, photon.view's own wrapper, is deleted: every row
has it now.)

**THE MEASUREMENT** (`~/Developer/spikes/gate-admit-1/`, 12 tokens then): two MERGE tiers at -j4
started together on :63/:64, the box otherwise quiet: 0 `OUT_OF_DEVICE_MEMORY`, 0 Xid, peak 13,333
MiB (p95 10.1 GB), walls 42.9 / 42.1 min, 56 admission waits (max 160 s). Both tiers ran from ONE
build dir (a driver slip: `gate_runlog.py run` defaults to `--build build-linux`), so their 42-45
reds each (`database is locked`, `could not save`, `No such file`) were the two tiers sharing every
`e2e-home-*`/`pool-home-*` under that dir — the run's own artefact, not a rig defect; the union of
those reds re-run alone was 100 of 101 green (the one red vr.eye_grade, a base red).

## 4c. ONE GATE AT A TIME — the gate slot (lane GATE-COST-1, 2026-10-09)

**Why** (`SPECS/audits/GATE_COST_2026-10-09.md`). The same ~600-row tier took **0.5-0.7 h on a box
with no sibling gate** and **2.6-6.6 h beside 2.6-3.9 of them**: every row ran 2-2.5x slower on CPU
and I/O, the VRAM queue added 12-20 row-hours per tier, and the box finished FEWER tiers per hour
the more ran at once (~1.6/h alone, ~0.75/h at four). Most NOADMITs, lint TIMEOUTs and load-bar
FAILs — and their solos and verdicts — came from gates beside gates.

**THE RULE.** A GATE RUN holds THE GATE SLOT for its whole run; the box runs one at a time.
A gate run is `gate-scope.sh --run` (scoped, a fallback, `--fork-tier`,
`--targets-only`, `--resume`) and `gate_runlog.py run` (the rc tiers; `scripts/gpu-admit.sh gate
-- <command>` holds the slot across a whole script that runs several). **A WHOLE-CARD HOLD ALWAYS TAKES
IT** (GATE-COST-2): a `--solo` batch, an `--attribute` run, a timing phase run by hand — anything that drains
the card through `hold_card()`, and `admit all` (`gpu-exclusive.sh`: a timing row run outside a gate, inside
the admission's own 900 s bound — NOADMIT past it) — queues for it at class 2, behind any waiting gate. Per-row admissions of
k tokens never take it (an `admit <k>`, a pool's app, a lane's own hand run), nor does a build. The waiting gate
prints `gate-slot: queued at position <p> (<p> ahead) behind <holder>, holding it for <age>` (again
whenever the position moves, and every 10 min) and `gate-slot: taken after <s> s in the queue`; the
queue has NO bound (a gate never gives up for the slot). `scripts/gpu-admit.sh status`
names the holder, the queue in serving order with each CLASS, and every token holder with its AGE — a hung holder
is visible.
**THE SLOT IS BY PRIORITY, NOT ARRIVAL** (TESTING-CLEANUP-2B item 13; the owner 2026-10-10: "the gate needs to run,
not wait for everything else"). Classes: **0** the owner's smoke rc, **1** a gate (rc-gate's tiers, `gate-scope.py
--run`, `gate_runlog.py run`), **2** a lane's measurement (`gpu-exclusive.sh`, `--solo`, `--attribute`, a whole-card hold
by hand, `gate_runlog.py run --tier lane|solo`). A waiter of a lower class is served before every waiting higher one
(by ticket inside a class) and never displaces the holder; a free slot is taken at once by whoever asks. A class-2
holder that holds across rows (`--solo`, `--attribute`) checks the queue between rows and YIELDS to a waiting class
0/1 (`yield_card`: the card and the slot given up, re-asked at its class, resumed at its next row — its recorded runs
stay). `gpu-admit.sh gate --class N --label …` names the class; without it the label decides (`rc-<tag> smoke` -> 0,
anything else -> 1). A measurement loop of `gpu-exclusive.sh` runs yields between its runs by construction (each run
asks again at class 2).
**WHAT THE CLASS RULE COSTS, ACCEPTED** (the owner's rule, TESTING-CLEANUP-2B item 15): (1) a class-2 waiter can
STARVE under a steady run of gates — every gate that asks while it waits is served first, with no bound on how many;
(2) `admit all` (`gpu-exclusive.sh`) is not starved forever: its wait is bounded by the admission's `wait_bound`
(`JAH_VRAM_WAIT`, 900 s by default) and past it the run is NOADMIT (exit 75, never ran) — a measurement loop that must
wait out a gate batch sets a longer `JAH_VRAM_WAIT`; (3) a lane's own tier run (`gate_runlog.py run --tier lane|solo`)
is ONE ctest process holding the slot for its whole run — one ctest cannot yield between its rows — so a gate that asks
behind it waits at most that one lane run, never more (keep such runs to one row, as item 7's re-measures do). **A
YIELD'S RE-HOLD IS A NEW HOLD**: after `yield_card` the re-drain's own outcome decides the runs after it — a re-drain
that times out writes its `drain-timeout` record and runs the rest with `JAH_VRAM_ALL` and the `fallback` stamp, a
clean one without (`gate-scope.py _hold_state`, both the `--solo` and `--attribute` paths; gate.cost case (a)/(b)).
**Mechanism** (`scripts/vram_tokens.py`): each waiter flocks its own ticket
`/tmp/jah-vram/gate-queue/<seq>.<pid>.c<class>` (made locked under a private name, then renamed in); the
holder's ticket is renamed `….held` when it takes the slot — under the counter's lock, and only when no ticket
is held and no waiter outranks it (a lower class, or the same class and an earlier ticket), so two askers never
both take it; a dead waiter's or holder's ticket is reaped by the next reader. The
slot and a phase's tokens belong to the GATE PROCESS, never to its ctest, and THE ctest TREE DIES WITH
THE GATE: ctest runs in its own process group with PR_SET_PDEATHSIG (SIGTERM), a detached reaper
kills that group the moment the gate's pid is gone (ctest's own SIGTERM orphans its rows — measured),
and a gate told SIGTERM/SIGINT/SIGHUP stops its tree, releases slot and card and prints `GATE ABORTED`.
A gate SIGKILLed by oomd therefore frees the slot at once and leaves no rows running past 15 s. A
process inside a gate (`JAH_GATE_SLOT_HELD`) never queues again. `JAH_GATE_SLOT=0` turns it off.

**INSIDE A GATE, IN ORDER:**
1. **The CPU phase (P8):** the `hygiene` rows (lints and the selector's own rows: no display, no GPU;
   a row in a FIXTURE stays with its partners in the GPU phase) run first, at the gate's width, before any GPU row starts (`=== the CPU phase: <n> lint/selector
   row(s) …`). Their walks stay on FIRST-PARTY paths: `source.one_fonticons` and
   `source.one_material_resolve` walked `src irisgl` — the vendored submodules, the fork's build tree
   and install, ~26k entries and 104 MB of C++ per run in a lane tree — on the USB-stick root under
   `ionice -c 3` (idle-class I/O, starved beside three gates): 0-1 s rows that TIMED OUT at 60 s
   (24 of the window's 34 TIMEOUTs were lints, §2 of the audit). They now skip irisgl's SUBMODULES —
   exactly the `path =` lines of `irisgl/.gitmodules` (`tests/hygiene/first_party.sh`) — and `.git`;
   the in-tree vendored directories (`meshoptimizer-clusterlod`, the `*-patches` stacks) and the build's
   install dir are files of this tree and are still walked.
2. **The GPU phase**, at the gate's width; a NOADMIT re-queued at its end (§4b, P5).
3. **The timing phase**, serial, on ONE whole-card hold (§4, P2).
4. **The verdict line** `=== GATE VERDICT: GREEN|RED (exit <n>) ===` — every gating record written.
5. **The target step** (§1b, P9), on the gate's display and the whole card, reported, never gating.

**EACH ROW IS RECORDED AS IT ENDS, AND A GATE NEVER RUNS ON A DEAD DISPLAY (P6).** ctest runs with
`-V`; the runner keeps each row's own lines and appends its records at its result line (the
records used to be written after ctest exited: the oomd kill of 2026-10-09 took 303 finished rows of
rc-smoke16a with it). The stream the caller sees is `--output-on-failure`'s. `gate-scope.sh <range>
--resume` runs only the rows with no record at the tip (NOADMIT/NOTRUN are no record) — the rest of a
killed gate. A gate on a local display `:N` reads its X server's pid from `/tmp/.X<N>-lock` at the
start and checks, at every row's end and every 2 s, that the same pid lives, still owns the lock (a
display NUMBER is reused) and accepts a connection; when it does not, the run's process tree is
stopped, no row that ended after the death is recorded, and the gate ends with
`=== GATE ABORTED: <why> …` and `=== GATE VERDICT: ABORTED …` (exit 6). THE ABORT IS A RECORD
(GATE-COST-2): one `kind: abort` record (suite `@abort`) names every row that ended after the death
with its status and FAIL line, and the rows still running at it (`inFlight`); the refusal reads nothing from it, and `--resume` re-runs a row the
abort dropped RED as a SOLO — 3x, tier `solo`, retry, on one whole-card hold before the verdict — never
in the ordinary pass (`gate_runlog.owed_solos`: until a solo answers it); the abort record keeps
`droppedRed` for the judge. A run on a display already
dead refuses to start. (634 garbage records in the audit's window came from gates that kept running
6-12 min after their Xvfb died.) A ctest killed by a signal ends the run the same way: what it
finished is recorded, nothing else starts — no timing phase and no target step: the gate ends with
`GATE VERDICT: ABORTED` (exit 128 + the signal).

Guard: `gate.cost` (tests/hygiene/gate_cost_test.py; the slot's FIFO, every whole-card hold in it, a
NOADMIT record per try, the drain-timeout record, the abort record and the resume of its reds, the
lints' prune list = .gitmodules, the Xid ingest wait on a green row, the reaped
dead waiter, eight racing gates, a SIGKILLed gate's tree and slot, the in-run re-queue, the killed-ctest
records and --resume, a killed ctest ending the gate, the dead display, the CPU phase, the whole-card
phases with the caller's environment, and the per-row records equal to the old junit path's).

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
constraint" was measured with three processes; with four gates it is. The box enforces that law
itself since GATE-ADMIT-1 (§4b: box-wide VRAM tokens), and since GATE-COST-1 runs one gate at a
time (§4c).

WHAT THE SHAPE CAN STILL WIN is the gap to busy/4 — about 8 % after D6B. The rest is
suite-seconds: the tier grew ~20 rows between the audit and D6B (atom.lod_switch alone is
250 s), and three other lanes' gates doubled every row's seconds on the day of the
measurement. The residual one-running tail is the RUN_SERIAL rows the audit KEPT (§4, the
serial families paragraph); the four threading compile halves left it for the `threading`
RESOURCE_LOCK (GATE-SPEED-1), which keeps their stated reason ("four Epic opens compiling at
once") without stopping the gate.

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
(SPECS/TEST_GATE_AUDIT.md, steps 1-8) moves the benchmark to a 15 s smoke + a stage-close row,
restructures and moves the cache attacks to the stage-close batch, folds 14 duplicate sample boots into
`samples.cleanstart`, merges the three xdotool drivers, fixes the port-8751 collision that
makes `-j4` unsafe, and trims timeouts to 6× measured. Projection: ~9 min for the full gate at
`-j4`, ~5 min for MERGE. The multi-script runner this section once
scheduled as "phase 2" is built: §8 (the pools).

## 6. Who does what

- **Lane** (feature-/engine-builder): runs its NAMED acceptance tests + its subject suite (§3c),
  never a gate (a tooling lane's own scoped gate is the lead's exception); reports what ran
  and every failure's verdict.
- **Lead**: audits the lane (diff, `~/Developer/scripts/platform-audit.sh`), stacks the ready lanes
  as ONE candidate (`merge-dbuild-lane.sh batch`), runs ONE gate on it (`rc-gate.sh`, §3c), lands
  it (`batch-land`) or attributes its reds, pushes at a sensible batch (irisgl first), and runs
  the STAGE-CLOSE batch at every stage close and before every push.
- **gate-runner**: runs the tier it is given, never picks, restores what it touched, kills
  only its own pids.

## 7. The six selftest hashes (lane FENCE-1, PHOTON phase A; MOTION and MOVER since TESTING-CLEANUP-2 H5)

`./build-linux/bin/Jahshaka --engine-selftest out.png` prints SIX `sha256` lines and writes
six files. They are the cheapest early warning in the tree — seconds, one process — and a
push quotes all six. Every frame before every shot is a COUNTED frame on the fixed 1/60 s clock
(`renderFrames(n, 1/60)`; H5 replaced the runner's two wall-clock pumps, the candidate cause of
B1/B2's old nondeterminism, §1771/§1830).

| line (in print order) | file | what it fences | grade |
|---|---|---|---|
| `pose 1` | `out.png` | the default scene from the camera that has never moved | Plain |
| `pose 2` | `out.pose2.png` | the same scene after the camera moves +5 m in x, turns, and settles 240 frames — the cascade scroll, the field's follow, the settle | Plain |
| `pose motion` | `out.motion.png` | a WALK from pose 2's place, 0.1 m per frame for 30 frames, the shot at the 30th with NO settle — the per-frame paths of a moving camera (the cascade scroll mid-step, an unconverged history) | Plain |
| `pose B1 (rays)` | `out.B1.png` | fixture B **with** the ray tier | Viewport |
| `pose B2 (no rays)` | `out.B2.png` | the same fixture after `world.rayTracing("off")` | Viewport |
| `pose mover` | `out.mover.png` | rays back on and settled, then a glossy sphere crossing the floor 0.1 m per frame for 30 frames, the shot mid-move — the moving-object paths (the march's object motion, the trace's mover branches, the reprojection velocity) | Viewport |

The record lives in `testing/HASHES_OF_RECORD` (the lead's; one line per landing that moved
one). TESTING-CLEANUP-2 measured the six 5/5 cold on the rig (Xvfb `:NN` `1920x1080x24`,
`--data-root`), and TESTING-CLEANUP-2B re-read them at its tip:

```
pose 1        87b9d5b37d91fbf7158cca7fe73127bc661c65296db703e4ac9c9f65990cb05c
pose 2        e10ed7139c0764da8ec5e9c4da525b14c348ed67829ff98ac5daa0f544b9a7de
pose motion   e0e1bb6a323990df76a77e8b1175638f868aa5da681a173384f9dd0c4ecc7476
pose B1 rays  8e425c516f9f11eca1f11c77669c148d647a07f976ab0892949f889ed24123bd
pose B2 none  244ff98ed7745b33546b3006c62252c08e04f941f4ac862094460afc2f364f23
pose mover    32734538ec9ed24ba5aefdc596a05e21a9ceb2c43d5194c4a28be2ccb34ad5b4
```

The runner refuses a degenerate fence: pose 2 equal to pose 1 (the camera move did not take), the
motion pose equal to pose 2 (the walk did not take), the mover equal to B1 (the sphere did not reach
the shot), any of them the clear colour — and B1 != B2 whenever the machine has ray queries.

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
(`mem`); after every arm's baseline it prints `MEM <pool>.<arm> gpuPoolUsed=<MB> textures=<MB>` (the
arm's `mem`), and a climb past the largest single arm's step is the FINDING `LEAK <pool> +<MB> over
<n> arms` (the row's `leak`; not a red until runs say it is real). Measured on this box (warm cache, one process, nvidia-smi per pid): Epic 1,532 MiB at
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

