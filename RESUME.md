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
7 the six TIMEOUT re-measures and 12 the layered pools + gpu.cutout_soak: queued in the gate slot by
/tmp/claude-1000/.../scratchpad/tc2b_gpu.sh (detached; progress in spikes/testing-cleanup-2b/gpu/progress); when done,
the TIMEOUT comments get the 6x quiet worst, one commit each item. open.responsive (the push row, TIMEOUT 1740 from the
same contended 281 s) is to be measured alone too.

## Rig
Xvfb :78 (pid in build-linux/.xvfb78.pid); the GPU runner's pid in build-linux/.tc2b_gpu_runner.pid. Kill only those.
