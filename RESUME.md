# TESTING-CLEANUP-2 part A — RESUME (2026-10-10, before the disk move)

Tips: Studio `testing-cleanup-2` (this file's commit), irisgl `testing-cleanup-2-irisgl` 3035c07.
Evidence: ~/Developer/spikes/testing-cleanup-2/ (r2_*.log, h5_run*.log, owed/).

## Round 2 (the lead's A-E): ALL DONE and committed
- A 4b6971ea3: databaseReads is an app-only TU, links system SQLite directly; available only
  when qsqlite NEEDS that libsqlite3, else scale.library SKIPs 77. database.cpp/.h = base.
- B 7cbea294f: JAHSHAKA_NO_DEVICE_LOSS_DIALOG=1 in spawn(), run_pool.py, mcp.e2e; app.device_loss_end
  asserts RIG (exit 3 at once) + USER (86 in 60 s); priced 60.04.
- C irisgl 3035c07: the Mac walks _dyld images for the layer list.
- D/E b77326e4d: scriptrunner forced exit -> forcedexit::now (86); the watchdog line goes through
  JahLog first; stale _Exit(0) comments fixed; testing_rules R2 keys on stage-close;
  settleToSize compares in pixels (viewportState.devicePixelRatio).

## Verified at the round-2 build (Xvfb :71)
hygiene 23/23; app.device_loss_end, mcp.e2e, ui.shot_aspect, import.shutdown,
app.engine_selftest_validation, pool.editor_view (engine_arms) green; scale.library RED BY DESIGN
(boot +45.92 MB, create +11.85 MB: TC2-LIBRARY-BOOT-READ / -CREATE-READ). Six hashes 5/5 cold:
1=87b9d5b3… 2=e10ed713… B1=8e425c51… B2=244ff98e… motion=e0e1bb6a… mover=32734538…

## Not done / owed
- The TIMEOUT re-measure (needs the lead's slot).
- Owed CMake (exclusions not lifted): meshbake + theme homes (H8e), atom.dag_bound rename (H6d),
  the test_shutdown_order.cpp one-liner (P10g) — patches in spikes/testing-cleanup-2/owed/.
- Not run: gpu.cutout_soak (stage-close, 1800 s); pool.vr_validation / capture_validation were
  green at H4 and were not re-run after round 2.

## Next step
Report round 2 to the lead (the tips above). Nothing of mine is running; no Xvfb (killed).
