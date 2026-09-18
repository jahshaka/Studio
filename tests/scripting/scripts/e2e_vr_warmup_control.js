// vr.warmup_control — THE INSTRUMENT `vr.warmup` USES, CALIBRATED AGAINST THE
// DEFECT (lane VR-WARMUP-1).
//
// The same session as `vr.warmup`, begun with `warmUp: 0`, in its OWN process
// (a second session in one process finds everything already built, so the
// control cannot share one). It asserts the thing the warm-up exists to
// remove: with no warm-up, the frames the runtime is shown DO compile shaders,
// on the frame thread, while the wearer is looking at them.
//
// WHY A SUITE AND NOT A COMMENT. `vr.warmup`'s assertion is "zero compiles on
// shown frames", and zero is exactly what a broken measurement reports too —
// a counter read from the wrong place, a session that never showed a frame, a
// scene with nothing in it. This is the arm that fails if the instrument stops
// being able to see a compile at all.
//
// A LEGITIMATE FUTURE RED: if something else ever warms the instanced-stereo
// permutations before the session's first shown frame (a startup gate that
// knows about stereo, a shipped pipeline cache), this suite goes red and the
// right answer is to read it, not to widen it — the warm-up would have become
// unnecessary.
function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

project.create("vr warmup control " + Date.now());
scene.addPrimitive("Cube");
scene.addPrimitive("Sphere");

var av = vr.available();
assert(av.available === true, "the runtime is there (" + av.runtime + ")");

assert(vr.begin({ mirror: "left", warmUp: 0 }) === true,
       "the session began with no warm-up: " + app.lastError());
var st = vr.state();
assert(st.warmUp.frames === 0, "and it stays at zero warm-up frames: " +
                               JSON.stringify(st.warmUp));
var before = app.shaderCache();

var committed = 0, guard = 0, worstMs = 0;
while (committed < 4 && guard < 90) {
    var r0 = vr.state().rendered;
    var t0 = Date.now();
    editor.frame(1);
    var dt = Date.now() - t0;
    ++guard;
    if (vr.state().rendered > r0) { ++committed; if (dt > worstMs) worstMs = dt; }
}
assert(committed >= 4, "the runtime asked for at least 4 pictures (" + committed + ")");
var after = app.shaderCache();
console.log("compiled on the shown frames: " + before.compiledThisRun + " -> " +
            after.compiledThisRun + "; worst shown frame " + worstMs + " ms");
assert(vr.state().warmUp.frames === 0, "still no warm-up frame ran");
assert(after.compiledThisRun > before.compiledThisRun,
       "WITHOUT THE WARM-UP THE SHOWN FRAMES COMPILE (" +
       (after.compiledThisRun - before.compiledThisRun) +
       " shaders on the first " + committed + " frames the runtime was shown) — " +
       "which is what vr.warmup asserts is gone");
assert(vr.end() === true, "the session ended");
console.log("DONE");
