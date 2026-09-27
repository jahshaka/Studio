# Testing Jahshaka — the developer's guide

<!-- MODULAR-GATE-1 delivers PART TWO of this file (§8 below). Part one (§1-§7: running,
     shapes, adding a test, the rig, reading a red) is SUITE-POOL-1's; the merge puts §8 after
     it. -->

## 8. What your change selects, and why

### The one command

Before you ask for a merge, from your tree's top, on your own display:

```bash
DISPLAY=:NN scripts/gate-scope.sh <your base>..HEAD --run
```

It prints what your change selects and why, runs exactly that, and writes every result into
the run log. That is your gate. You never pick suites by hand and you never widen the selection
"to be safe" — if it looks wrong, read on.

A merge is refused without it: `scripts/ci-gate-check.sh <base>..HEAD` checks the run log for a
green record of every test your change selects, at your exact commit, from a clean tree, and says
which are missing or red. The rules behind the gate are checked by the `source.testing_rules`
test (a timing test outside the GPU lock, a copied tier command, a `RUN_SERIAL` without its
reason, a test nothing can select): if it goes red on your change, its line names the rule.

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
its tests. A setting at the top of the build (a global flag) reaches everything, so it selects
the full tier — and says so.

### Reading the print

Every touched file gets a line saying what it reached and through what:

```
  irisgl/import/meshbake.cpp
      -> graph: 24 executable(s) incl. the app; rule ^irisgl/import/ modules=['assets', 'avatar', 'anim'] (app/other rows)
  irisgl/import/meshbake.h
      -> symbols ['bakeThreads', 'setBakeThreads', 'stageSummary'] named by 5 file(s)
```

and every selected test says why it is there (`[relinks test_mesh_bake]`, `symbol StageMs`,
`rule ^irisgl/import/ [module assets]`). The last lines are the size — `N of M tier rows, ~S of
~T suite-seconds` — and the exact `ctest` line that runs.

### When the full tier runs instead

You do not run the full tier for your lane. It runs:

| when | what | who |
|---|---|---|
| your change bumps the Ogre fork pin | the MERGE tier, selected by rule (an Ogre change reaches everything) | you, through the same command |
| a stage closes (a plan row and all its fix lanes merged) | the MERGE tier on the merged tip, in a fresh worktree | the lead |
| every night | the MERGE tier + the millisecond bars | the lead / CI |
| a phase ends | the PUSH tier (MERGE + the four selftest hashes), then the push | the lead |

If the tool prints **FALLBACK → MERGE TIER**, one of your paths has no rule, no symbol and no
build-graph owner (a new kind of file). The run is the full tier this time; tell the lead which
path it named — it gets a rule.

### When a test you expected is missing, or one you did not expect is there

- **Unexpected test selected:** read its reason. `[relinks …]` means your code really is in that
  executable; `symbol X` means that file names something you changed; `rule …` means your
  directory's area includes that family. It runs. If the reason is wrong, that is a defect in
  the selector — report it, do not skip the test.
- **Expected test missing:** run it yourself as well (`DISPLAY=:NN ctest -R '^name$'`) and
  report the missing selection. If a later tier (a stage close, nightly) goes red on a test your
  selection did not include, that is a **selector defect**: it is fixed in
  `scripts/gate-scope.py` and your lane's diff becomes a new case in
  `tests/hygiene/gate_selection_cases.json`, which the `gate.selection` test replays on every
  change to the tool. The selector learns from facts, not from a wider rule.

### Two lanes that touched the same files

When two lanes that changed the same file family merge, the lead runs
`scripts/gate-scope.sh --joint <rangeA> <rangeB> --run`: the union of both selections, with each
lane's own tests that the other lane's change also reaches named apart — the combination neither
gate saw. You do not run it for your own lane.

### A red in your gate

A red you caused is fixed; then re-run only the failures and the selection of the fix
(`scripts/gate-scope.sh <pre-fix>..<post-fix> --run`), never the whole gate again. A red that
looks like load (a timing bar, a known contention test) is retried alone, three times:

```bash
DISPLAY=:NN scripts/gate-scope.sh --solo <suite> --times 3
```

Three greens alone = environmental, and the run log now shows the red and the three greens side
by side with the load each ran under. Anything else is real.

### The run log

Every gate run leaves one line per test in `~/Developer/testing/runs/` (date, tier, tip): the
verdict, the time, the load, why it was selected. It is how the testing rules get re-read against
facts. `scripts/gate_runlog.py longest` lists this week's longest tests; `scripts/gate_runlog.py
load-reds` lists the tests that went red in a gate and green alone. The fields are in that
directory's `README.md`.
