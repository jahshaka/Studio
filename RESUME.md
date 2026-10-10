# MIRROR-SETTLE-1 — state at wind-down (2026-10-10)

Worktree: jahshaka/.claude/worktrees/mirror-settle-1 (branch mirror-settle-1, base b0a3b1f3c);
irisgl branch mirror-settle-1 (base 5d38b6f29). Both commits are WIP. Build dir build-linux is built at the tips.

## Design (built)
- SceneMirror::kStaticSettleNodesPerSync = 256 (scenemirror.h, derivation in the comment: 50k settles in 196 syncs = 3.3 s @60 Hz,
  10k in 40, 1k in 4). The settle walk = a pre-order stack of SettleEntry {weak node, parent, parentMovable, carry} (stepStaticSettle).
- SceneNode::_applyStaticDefaultsStep = applyStaticDefaultsFrom's body without recursion; promotion via graph::promoteNode
  (node + engine-owned children; `carry` reproduces setStatic's subtree reach). Demotion stays the subtree switch.
- A transform write mid-walk clears the walk, keeps pending; setSource clears the walk. Bake key: no bake file touched.
- tests: new mirror.settle_incremental (tests/mirror/test_mirror_settle.cpp, jah_gpu_row engine, priced 70.92 s in gate-times.txt);
  mirror.scale's settle lambda drains staticSettleInProgress().

## Green
- mirror.settle_incremental PASS (spikes/mirror-settle-1/ms1-settle.log): 50,521 nodes; one-shot 373 ms; incremental 198 slices,
  max 256 visits, slowest sync 6.26 ms; EQUALITY node-for-node (static+hint) incl. an interrupted settle. mirror.scale PASS.

## NOT green — scenegraph.benchmark (spikes/mirror-settle-1/ms1-bench{1,2}.log; before = spikes/stage-close-triage-1/bench1-3.log)
b.idle_sync max: 1k 2.4-3.2 -> 0.67-0.71 ms; 10k 31-37 -> 2.3-2.5 ms; 50k 392-1115 -> 9.7 ms (spike gone; idle_tick 50k now cv 0.24 green).
But the dispersion bars still red: b.idle_sync cv 0.67 @1k, 0.80-0.85 @10k, 0.69 @50k; rcv 0.28 / 0.48-0.57 / 0.64.
Cause: the settle now spans 4/40/196 measured syncs at +0.5..8 ms over a 0.09-0.4 ms idle base -> a bimodal series
(rcv was already 0.26-0.29 @1k on base too — check that one against base noise).

## Next step
Decide with the lead: the slice cost must sit near the idle base for the bars to hold (a bimodal series fails cv once p is small
and c >> base, and fails rcv once p ~ 0.3-0.7). Options: (a) much smaller budget (e.g. 16-32) and measure — but Ogre's node arrays
defragment every 100 freed slots (OgreNodeMemoryManager.cpp:119, cleanupThreshold 100; O(level) pass), which spikes periodically;
(b) a fork commit raising/deferring that cleanup threshold for migrations; (c) the lead re-judges the bar for the settle window.
Then: bench 3x via ctest (gpu-exclusive), the four selftest hashes (not yet run), check-trailers over both ranges, scoped gate when the lead says.
Xvfb :71 killed at wind-down.
