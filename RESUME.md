# lane-gatecleanup — RESUME (stopped 2026-09-10 01:15 on owner logout)

Base: Studio `ogre` **ee1da5dd**, irisgl pin 3dd1fa4 (UNCHANGED — Studio/tests-only lane,
no irisgl commit). Branch `lane-gatecleanup`, tip **3fe05eb6**. Build tree
`build-linux` inside this worktree (Debug + `-DJAHSHAKA_ASAN=ON`, ogre patch stack
0001-0024 applied in this worktree's own submodule, engine built into
`irisgl/thirdparty/ogre-next-install`).

**Nothing is mid-flight in the code.** All eight brief steps are BUILT, COMMITTED and
VERIFIED. The only thing cut short is the last re-run of the acceptance gate, which was
being repeated only because the box was busy — a number, not work.

## Steps 1-8 of SPECS/TEST_GATE_AUDIT.md §5 — all DONE

| # | commit | what |
|---|---|---|
| 1 | `f6956b80` | -j4 safety: ephemeral MCP ports (app prints `MCP: port N`; `--mcp-port=0` = ephemeral, not off — `CliOptions::mcpServe`), `app.engine_selftest` deleted, `jah_no_display` on 42 more suites (82 -> 124) |
| 2 | `a851ccd7` | `--smoke` on bench_scenegraph and bench_rigperf; `scenegraph.smoke` + `rigperf.smoke` in MERGE (LABELS `benchmark-smoke`), full `--assert` rows nightly |
| 3 | `94563a6a` | shader-cache attacks: byte snapshot instead of reseed cycles, `--corruption-only` for the ASan twin, `shadercache-attack` label |
| 4 | `92696f5a` | `scripting.e2e.samples` + `samples.scale.*` x7 folded into `samples/scripts/cleanstart.js.in`; both registrations and both scripts deleted |
| 5 | `20d8165f` | one xdotool driver: `tests/app/input_keys.sh` = `app.input_keys`; pacing_undo.sh / multiselect_keys.sh / navigation_keys.sh deleted |
| 6 | `cbf9e3b2` | mcp founding-tools assertion, `document.no_gl` -> `document.characterisation` (Qt6::OpenGL + GL precondition dropped, file renamed), `threading.import_soak` off RUN_SERIAL, log_perf sleeps 8 -> 5 s, TIMEOUT policy across 112 rows, `app.data_root` scrubs `JAHSHAKA_DATA_ROOT` (lead law 2026-09-10) |
| 7 | — | gate-scope.py / gate-runner.md deliberately NOT edited (lead's). The validated command is below. |
| 8 | `3fe05eb6` | dangling suite names retired across src/ and tests/; `docs/SCRIPTING.md` REGENERATED via `--dump-api-docs` (api.contract green) |

## The new MERGE tier (validated: drops exactly 4 rows, keeps the smoke + cache rows)

```
ctest -j4 --output-on-failure -LE "^(benchmark|shadercache-attack)$" -E "^gi\.ddgi_raster$"
```

Anchors matter: unanchored `benchmark` would also exclude `benchmark-smoke`.
Drops `gi.ddgi_raster`, `rigperf.benchmark`, `scenegraph.benchmark`,
`shadercache.container_asan` = the NIGHTLY tier. 285 of 289 rows (298 at base).

## Measured

* **MERGE tier at -j4, quiet box, `DISPLAY=:60` Xvfb `-screen 0 1920x1080x24`:
  494.44 s = 8 min 14 s**, 284/285 pass. Suite-seconds 1603 (pool/4 = 401 s ideal).
* Two later repeats of the same command read 755 s and 789 s — both taken while
  `lane-livetextures` had a ctest and two Jahshaka instances live on the box
  (load average 19-22; every suite 2-3x slower across the board, e.g.
  ui.selection_cost 9.1 -> 38.1 s). The 494 s figure is the quiet-box one.
* Per step: scenegraph 286.6 -> 6.3 s · shadercache.container 104.1 -> 28.8 s ·
  container_asan 664.9 -> 209.5 s · three xdotool rows 68.2 -> 59.9 s serial ·
  samples fold -68.5 suite-seconds and 8 boots · TIMEOUT sum 93,080 -> 46,340 s.
* Only failure in the quiet run: `open.responsive` (documented contention class),
  warm-open UI gap 515.4 ms against a 500 ms budget. 3/3 green solo afterwards.

## Next command (nothing depends on it)

Only if the lead wants a fresh acceptance number on a genuinely quiet box:

```
cd <this worktree>/build-linux
Xvfb :NN -screen 0 1920x1080x24 -nolisten tcp &        # geometry is LAW
/usr/bin/time -f "WALL %e s" env DISPLAY=:NN HOME=<scratch> JAHSHAKA_DATA_ROOT=<scratch> \
  ctest -j4 --output-on-failure -LE "^(benchmark|shadercache-attack)$" -E "^gi\.ddgi_raster$"
```

## For the lead

* `scripts/gate-times.txt` and `gate-scope.py` landed on `ogre` AFTER this lane's base
  (323e97fe), so they are not on this branch and were not touched. New/changed measured
  seconds for a refresh: `app.input_keys` 52.6, `scenegraph.smoke` 6.3, `rigperf.smoke`
  3.1, `document.characterisation` 0.06, `shadercache.container` 28.8,
  `shadercache.container_asan` 209.5. Rows to DELETE from it: `app.engine_selftest`,
  `app.pacing_undo`, `app.multiselect_keys`, `app.navigation_keys`,
  `scripting.e2e.samples`, `samples.scale.*` x7, `document.no_gl`.
* Expected merge overlaps: `tests/scripting/CMakeLists.txt`, `tests/app/CMakeLists.txt`,
  and one appended `jah_no_display(...)` block at the end of 20 other
  `tests/*/CMakeLists.txt` files (single-hunk adds), plus a lowered `TIMEOUT` number in
  34 of them. `docs/SCRIPTING.md` is generated — regenerate after merging, do not
  resolve it by hand.
