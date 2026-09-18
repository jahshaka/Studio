// vr.warmup — THE STEREO WARM-UP: NO FRAME THE RUNTIME IS SHOWN COMPILES A
// SHADER (lane VR-WARMUP-1; SPECS/VR_SPEC.md §6, VrConfig::warmUpFrames).
//
// THE DEFECT THIS PINS. Before the warm-up, the FIRST frame a VR session was
// asked for a picture of built every Hlms permutation and every pipeline the
// two eyes needed, on the frame thread: measured on this rig at 1,179 ms cold
// and 89 ms warm on the Grand Showroom, 920 ms cold on the default scene, with
// `engine.record` holding all of it and the GPU at 0.1 ms. At the Quest Pro's
// 62.5 Hz that is 73 repeated headset frames — the jerk the owner reported at
// every session start.
//
// THE RULE IT ASSERTS is the product one, and it is a COUNT, not a clock: a
// frame the wearer is shown compiles NOTHING. The session spends its first two
// frames on a warm-up instead — its own chain, in stereo, at its own eye size,
// from the rig's origin through a wide frustum — with no XR frame open, nothing
// submitted and nothing mirrored.
//
// ITS CALIBRATION IS ITS OWN SUITE: `vr.warmup_control` runs the same session
// with `warmUp: 0` and asserts that the compiles DO land on the shown frames.
// Two processes, because a second session in one process finds everything
// already built — which is itself asserted here, at the end.
//
// Monado's simulated HMD, like every runtime, decides for itself when it wants
// a picture; nothing here waits on a clock for it — the loops below count
// `vr.state().rendered`, which is the frames the runtime ASKED FOR and got.
function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

project.create("vr warmup " + Date.now());
// SOMETHING WITH ITS OWN MATERIAL BESIDES THE FLOOR: the permutation set is
// per datablock, and a scene with one surface has almost nothing to warm.
scene.addPrimitive("Cube");
scene.addPrimitive("Sphere");

var av = vr.available();
console.log("runtime: " + av.runtime + " " + av.version);
assert(av.available === true, "the runtime is there");

var before = app.shaderCache();
console.log("shaders before the session: compiled=" + before.compiledThisRun +
            " loaded=" + before.loadedThisRun +
            " microcodeLoaded=" + before.microcodeLoaded);

assert(vr.begin({ mirror: "left" }) === true, "the session began: " + app.lastError());
var st = vr.state();
assert(st.active === true, "the session is active");
assert(st.warmUp.frames === 0,
       "nothing is warmed at begin — the warm-up rides the session's first frames, " +
       "when the rig has been placed and the runtime is running");

// ---- 1. THE WARM-UP FRAMES, AND NOTHING SUBMITTED WHILE THEY RUN --------
var pumped = 0;
while (pumped < 12 && vr.state().warmUp.frames < 2) { editor.frame(1); ++pumped; }
st = vr.state();
assert(st.warmUp.frames === 2,
       "both warm-up frames ran in the session's first " + pumped + " frames: " +
       JSON.stringify(st.warmUp));
assert(st.warmUp.ms > 0, "and they cost real time: " + st.warmUp.ms.toFixed(1) + " ms");
assert(st.rendered === 0,
       "the runtime was shown NOTHING while the session warmed (rendered " + st.rendered + ")");
var warmed = app.shaderCache();
console.log("shaders after the warm-up: compiled=" + warmed.compiledThisRun +
            " (+" + (warmed.compiledThisRun - before.compiledThisRun) + " in the warm-up)");
// THIS ARM IS SELF-CALIBRATING TOO, and it is why the suite insists on a wiped
// home (the fixture in tests/vr/CMakeLists.txt): on a COLD shader cache the
// eyes' permutations do not exist anywhere yet, so the warm-up MUST have built
// some. Warm, this reads +0 and "zero compiles on shown frames" would be true
// for the wrong reason.
assert(warmed.compiledThisRun > before.compiledThisRun,
       "the warm-up is where the eyes' shaders were built: +" +
       (warmed.compiledThisRun - before.compiledThisRun) + " compiled in " +
       st.warmUp.ms.toFixed(1) + " ms, on frames the runtime was not shown");

// ---- 2. THE RULE: NO SHOWN FRAME COMPILES ------------------------------
var worstMs = 0, committed = 0, guard = 0;
var perFrame = [];
while (committed < 8 && guard < 90) {
    var r0 = vr.state().rendered;
    var t0 = Date.now();
    editor.frame(1);
    var dt = Date.now() - t0;
    ++guard;
    if (vr.state().rendered > r0) {
        ++committed;
        perFrame.push(dt + "/" + app.renderStats().lastMs.toFixed(1));
        if (dt > worstMs) worstMs = dt;
    }
}
assert(committed >= 8, "the runtime asked for at least 8 pictures (" + committed +
                       " in " + guard + " frames)");
console.log("shown frames (wall ms / engine lastMs): " + perFrame.join("  "));
var after = app.shaderCache();
assert(after.compiledThisRun === warmed.compiledThisRun,
       "NOT ONE SHADER WAS COMPILED ON A FRAME THE RUNTIME WAS SHOWN (" +
       warmed.compiledThisRun + " before those " + committed + " frames, " +
       after.compiledThisRun + " after)");
// A CLOCK, DELIBERATELY WIDE. The count above is the real assertion; this one
// only says the storm is not somewhere else. The bound is 250 ms against a
// measured 13-19 ms of work per shown frame (plus this rig's 10-18 ms present
// copy) and a pre-warm-up first frame of 890-1,180 ms: four times clear of
// both, so a loaded box cannot red it and the defect cannot pass it.
assert(worstMs < 250,
       "and none of them hitched: worst " + worstMs + " ms (the same frame cost " +
       "890-1,180 ms before the warm-up existed)");
assert(!app.lastError(), "nothing failed: " + JSON.stringify(app.lastError()));

// ---- 3. A SECOND SESSION FINDS EVERYTHING BUILT -------------------------
// The eye size has not changed and the process has already warmed, so this
// session's warm-up compiles nothing at all — the "warm second session" half
// of the lane's question, asserted rather than measured by hand.
assert(vr.end() === true, "the session ended");
var mid = app.shaderCache();
assert(vr.begin({ mirror: "left" }) === true, "a second session began: " + app.lastError());
pumped = 0;
while (pumped < 12 && vr.state().warmUp.frames < 2) { editor.frame(1); ++pumped; }
st = vr.state();
assert(st.warmUp.frames === 2, "its warm-up ran too: " + JSON.stringify(st.warmUp));
for (var i = 0; i < 8; ++i) editor.frame(1);
var end = app.shaderCache();
assert(end.compiledThisRun === mid.compiledThisRun,
       "AND A WARM SECOND SESSION AT THE SAME EYE SIZE COMPILED NOTHING, warm-up " +
       "included (" + mid.compiledThisRun + " -> " + end.compiledThisRun + ")");
console.log("second session's warm-up: " + JSON.stringify(vr.state().warmUp));
assert(vr.end() === true, "the second session ended");
console.log("DONE");
