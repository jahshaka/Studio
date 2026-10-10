# TESTING-CLEANUP-2B — RESUME (2026-10-10)

Branch `testing-cleanup-2b` on b0a3b1f3c (batch B1 candidate; irisgl 5d38b6f unchanged, fork 27fe5ca57). Tooling only:
scripts/, tests/hygiene/, tests/cmake/, test CMake declarations, docs — no src/, irisgl/, media.
Evidence: ~/Developer/spikes/testing-cleanup-2b/ (logs, gpu/, item2_rebase_*.json, lead-docs.patch, lead-rig-up-ionice.patch).

## Done (one commit per item; see `git log b0a3b1f3c..`)
1 tooling-only rule (gate-scope PRODUCT_INPUT / tooling_path) + gate.ci_check fixture fallout · 2 the +900 s widening
deleted (tests/cmake/vram_rows.cmake: RESOURCE_GROUPS + the resource spec; NOADMIT on a timeout while waiting; 20 rows
re-based) · 3 H8b (nm cache of empty members, ctest-listing cache) · 4 H8a catch-all · 5 owed CMake · 6 every tier row
priced + --price-check · 8 forced_exit flag · 9 docs (+ the lead-repo patch) · 10 perf-ab nice only (+ rig-up patch)
· 11 container_asan stage-close.

## In flight / owed
LEAD PRIORITY CHANGE (2026-10-10 ~12:3x): the item-7 run held the slot ahead of batch B2's gate; released (runner and its
admission killed) BEFORE any row ran — it had the slot after 4,022 s in the queue but was still draining tokens.
Item 7, measured: NONE. Remaining: all six — open.responsive.timing, gi.field_alias, shadercache.container,
atom.coverage_trace, gi.reflect_lod_pop, player.editor_parity (+ open.responsive, the push row with the same 1740).
Item 12 (pool.vr_validation, pool.capture_validation, gpu.cutout_soak at the tip): not run.
DO NOT re-ask for the slot until the lead says batch B2 is DONE; then re-run scratchpad tc2b_gpu.sh's item-7/12 loops
(each row alone under gpu-exclusive.sh, 3 runs, 6x quiet worst into the TIMEOUT comment, one commit per item).
Done with no slot: the six selftest hashes = the record, 2/2 cold runs (spikes/testing-cleanup-2b/gpu/selftest*.log).

## Rig
Xvfb :78 (pid in build-linux/.xvfb78.pid); the GPU runner's pid in build-linux/.tc2b_gpu_runner.pid. Kill only those.
