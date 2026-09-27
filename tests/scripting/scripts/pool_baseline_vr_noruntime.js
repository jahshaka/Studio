// THE vr_noruntime POOL'S BASELINE (lane SUITE-POOL-1): run by the pool runner after EVERY arm,
// green or red (`jah_add_pool(vr_noruntime ... BASELINE ...)`, `--pool-baseline`). The arms
// drive the VR interaction with injected hands (`vr.inject`), which REPLACE a hand until they
// are cleared, and their gestures move the editor's gizmo mode — both process-level, both what
// a fresh process does not have. A throw in an arm skips its own tail, so the pool puts them
// back here: the next arm starts from what a fresh process has.
vr.inject("left");
vr.inject("right");
editor.setGizmoMode("translate");
true
