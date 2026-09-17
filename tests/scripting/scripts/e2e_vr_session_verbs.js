// vr.verbs_session — the `vr.*` verbs against a real OpenXR runtime
// (SPECS/VR_SPEC.md §6; the runner starts Monado's SIMULATED headset).
//
// The other half of `scripting.e2e.vr_verbs`: that one proves the verbs refuse
// cleanly with no runtime, this one proves they WORK with one — from a script,
// which is the whole of phase 2's editor surface (there is no button yet, and
// phase 3's will call these same verbs).
//
// FRAMES, NEVER WALL TIME. `editor.frame(n)` renders n frames, and inside a
// session each of those blocks in xrWaitFrame until the runtime wants the next
// picture — so this script is paced by the runtime, and what it asserts is a
// COUNT the runtime accepted (VR_SPEC §6 flake class (b)).

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

project.create("vr session verbs " + Date.now());
scene.addPrimitive("Cube");

// ---- available ------------------------------------------------------------

var a = vr.available();
console.log("vr.available: " + JSON.stringify(a));
assert(a.available === true, "the runtime answered: " + a.runtime);
assert(a.runtime.indexOf("Monado") >= 0, "and it is the runtime the runner named");
assert(a.openxr.length > 0, "the OpenXR version it negotiated is reported: " + a.openxr);
assert(a.eyeSize[0] > 0 && a.eyeSize[1] > 0,
       "with a recommended eye size of " + a.eyeSize[0] + "x" + a.eyeSize[1]);

// ---- begin ----------------------------------------------------------------

assert(vr.begin({ mirror: "left" }) === true, "vr.begin() starts a session");
assert(vr.begin() === false, "a second vr.begin() refuses rather than starting two");

var st = vr.state();
console.log("vr.state after begin: " + JSON.stringify(st));
assert(st.active === true, "the session is active");
assert(st.mirror === "left", "the mirror is the left eye");
assert(st.eyeSize[0] > 0, "the session renders " + st.eyeSize[0] + "x" + st.eyeSize[1] + " per eye");

// ---- the runtime paces the frames ----------------------------------------

editor.frame(90);
st = vr.state();
console.log("vr.state after 90 frames: " + JSON.stringify(st));
assert(st.frames >= 60, "the runtime ACCEPTED " + st.frames + " frames");
assert(st.rendered >= 50, "and asked us to draw " + st.rendered + " of them");
assert(st.state === "focused", "the session reached `focused` (it is " + st.state + ")");
assert(st.ipd > 0.03 && st.ipd < 0.10, "the eyes are " + st.ipd.toFixed(4) + " m apart");
assert(st.space === "stage" || st.space === "local", "the reference space is " + st.space);
assert(st.refreshHz > 0, "the runtime's cadence is " + st.refreshHz.toFixed(1) + " Hz");

// ---- end ------------------------------------------------------------------

assert(vr.end() === true, "vr.end() ends it");
assert(vr.end() === false, "and a second vr.end() refuses");
st = vr.state();
assert(st.active === false, "no session is running");
assert(st.frames === 0, "the counters belong to the session, not to the process");

// ---- and the editor still renders -----------------------------------------

editor.frame(5);
assert(vr.begin() === true, "a session can be started AGAIN in the same process");
editor.frame(20);
assert(vr.state().frames >= 5, "the second session submits frames too");
assert(vr.end() === true, "and ends");
editor.frame(5);

console.log("vr.verbs_session: PASS");
