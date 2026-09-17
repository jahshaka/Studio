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

// ---- THE EDITOR'S VR PREVIEW (phase 4) ------------------------------------
// The desktop viewport goes on being an editor while somebody wears the scene:
// its camera is NOT the wearer, the wearer's fly keys are, and the wearer shows
// up in it as proxies.
assert(st.preview.active === true, "the editor's VR preview owns this session");
assert(st.preview.flyRedirected === true,
       "the viewport's fly keys move the WEARER while it runs");
assert(st.proxies === true, "and the wearer's controller proxies are on by default");
assert(st.preview.helpers === true,
       "THE WEARER SEES THE EDITOR WORKING: an editor preview opens the desk's helper " +
       "channel in the headset (the Player's VR mode does not)");
var camBefore = editor.camera();

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

// ---- the poses: the head, and the two hands -------------------------------

console.log("vr.state head: " + JSON.stringify(st.head));
console.log("vr.state hands: " + JSON.stringify(st.hands));
assert(st.head.valid === true, "the head is located in world space");
assert(typeof st.head.yaw === "number", "with a heading of " + st.head.yaw.toFixed(1) + " degrees");
assert(st.handActions === true,
       "THE ACTION SET IS ATTACHED: two grip poses on the simple-controller profile");
assert(typeof st.hands.left.valid === "boolean" && typeof st.hands.right.valid === "boolean",
       "and both hands answer (left " + st.hands.left.valid + ", right " +
       st.hands.right.valid + ")");
if (st.hands.left.valid) {
    assert(isFinite(st.hands.left.x) && isFinite(st.hands.left.y) && isFinite(st.hands.left.z),
           "the left hand is at a finite world pose");
}

// ---- THE EDITOR'S CAMERA IS NOT THE WEARER --------------------------------
// Nothing writes the head pose back into the document: take the headset off and
// the viewport is exactly where you left it. (The Player's VR mode does the
// opposite, deliberately — there the wearer IS the play camera.)
var camNow = editor.camera();
assert(Math.abs(camNow.position.x - camBefore.position.x) < 1e-4 &&
       Math.abs(camNow.position.y - camBefore.position.y) < 1e-4 &&
       Math.abs(camNow.position.z - camBefore.position.z) < 1e-4,
       "the editor's camera has not moved: the wearer is not this camera");

// ---- the proxies are a verb, not a mode -----------------------------------

assert(vr.proxies(false) === false, "vr.proxies(false) turns the wearer's controllers off");
editor.frame(4);
assert(vr.state().proxies === false, "...and the state says so");
assert(vr.proxies(true) === true, "vr.proxies(true) turns them back on");
editor.frame(4);
assert(vr.state().proxies === true, "...and back on in the state");

// ---- end ------------------------------------------------------------------

assert(vr.end() === true, "vr.end() ends it");
assert(vr.end() === false, "and a second vr.end() refuses");
st = vr.state();
assert(st.active === false, "no session is running");
assert(st.frames === 0, "the counters belong to the session, not to the process");
assert(st.preview.active === false, "the preview is over");
assert(st.preview.flyRedirected === false,
       "AND THE VIEWPORT HAS ITS FLY KEYS BACK — an editor whose arrows stayed dead after " +
       "a session would be worse than one that never had VR");
assert(st.head.valid === false && st.hands.left.valid === false,
       "and no pose survives the session");

// ---- and the editor still renders -----------------------------------------

editor.frame(5);
assert(vr.begin() === true, "a session can be started AGAIN in the same process");
editor.frame(20);
var second = vr.state();
assert(second.frames >= 5, "the second session submits frames too");
// THE DEFAULT MIRROR IS `none` FOR THE EDITOR (vr.begin's contract): the
// desktop keeps drawing the EDITOR's picture, with the wearer's proxies in it,
// rather than paying for two renders and showing one. The first session above
// asked for "left" explicitly and got it.
assert(second.mirror === "none",
       "and it takes no mirror by default: the desktop stays an editor");
assert(vr.end() === true, "and ends");
editor.frame(5);

console.log("vr.verbs_session: PASS");
