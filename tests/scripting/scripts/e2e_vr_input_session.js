// vr.input_session — LOCOMOTION AGAINST A REAL RUNTIME
// (SPECS/VR_INPUT_SPEC.md stage 1 §6, §10).
//
// THE HALF `scripting.e2e.vr_input_headless` CANNOT ASSERT, and the reason is a
// measured engine fact rather than a gap: Engine::setVrOrigin is a NO-OP
// without a session (OgreEngine.cpp:1296 — "a host sets it right after
// beginVrSession"), so with no runtime there is no rig, the stick walks nobody
// and the verbs refuse. Here there IS a session — Monado's simulated headset,
// started by the runner — so the thumbstick actually moves a wearer and the
// snap turn actually turns a room, and both can be MEASURED.
//
// The gestures themselves (ray, select, grab, snap, undo) are the headless
// suite's; the arithmetic is `vr.grab_maths`. What is new here is the RIG.
//
// THE CONTROLLERS ARE INJECTED, not real: Monado's simulated rig reports poses
// and NO BUTTONS and NO STICK (verified in the exact source of the installed
// build — VR_INPUT_SPEC §17), so a stick deflection can only arrive through
// `vr.inputInject`. That is what the injection route is for, and this suite is
// the one that proves it composes with a live session: an injected sample is
// one interaction frame, and the render loop stands down from stepping the
// interaction while anything is injected, so the walk below is exact rather
// than a race with the runtime's frame rate.
//
// FRAMES, NEVER WALL TIME (VR_SPEC §6 flake class (b)).

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function near(a, b, eps) { return Math.abs(a - b) <= (eps === undefined ? 1e-3 : eps); }
function dist2(a, b) {
    var dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return Math.sqrt(dx * dx + dy * dy + dz * dz);
}
function showRig(tag) {
    var r = vr.interactionMode().rig;
    console.log("      " + tag + ": rig (" + r.x.toFixed(3) + ", " + r.y.toFixed(3) + ", "
                + r.z.toFixed(3) + ") yaw " + r.yaw.toFixed(2));
    return r;
}
/// The LEFT hand (the non-dominant one by default) with a stick deflection and
/// nothing else — one interaction frame per call.
function leftStick(x, y) {
    assert(vr.inputInject({ hand: "left", valid: true,
                            aim: { x: 0, y: 1.4, z: 0 }, grip: { x: 0, y: 1.4, z: 0 },
                            select: 0, grab: 0, menu: false,
                            stickX: x, stickY: y }) === true,
           "inject the left stick (" + x + ", " + y + ")");
}

project.create("vr input session " + Date.now());
scene.addPrimitive("Cube");

var a = vr.available();
console.log("vr.available: " + JSON.stringify(a));
assert(a.available === true, "the runtime answered: " + a.runtime);

assert(vr.begin({ mirror: "none" }) === true, "vr.begin() starts a session on the editor's scene");

// THE POSES COME FROM INSIDE THE FRAME: nothing is located until the runtime
// has been asked, and the rig's first PLACEMENT waits for a located frame it
// can be paired with (EditorVrPreview::armPlacement). Frames, in a bounded
// loop — never a sleep.
var located = false;
for (var i = 0; i < 200 && !located; ++i) {
    editor.frame(1);
    var s = vr.state();
    located = s.head.valid && s.preview.placing === false;
}
var st = vr.state();
console.log("vr.state: " + JSON.stringify(st.head) + " placing=" + st.preview.placing
            + " frames=" + st.frames);
assert(st.active === true, "the session is running");
assert(st.head.valid === true, "the runtime located the head");
assert(st.preview.placing === false, "and the wearer has been placed (no placement pending)");

var mode = vr.interactionMode();
console.log("vr.interactionMode: " + JSON.stringify(mode));
assert(mode.rig.live === true, "the interaction can see a rig now");
assert(mode.installed === true, "and it was installed when the session began");

// ---- 1. THE STICK WALKS THE WEARER --------------------------------------
//
// Forty-five injected frames of full forward stick at the editor's own fly
// speed. The direction is the HEAD's level heading (vrorigin::levelForward),
// so the assertion is about DISTANCE and about the horizon: a wearer must never
// be flown up or down by a stick.
var before = showRig("before the walk");
for (var f = 0; f < 45; ++f) leftStick(0, 1);
var after = showRig("after 45 frames of full forward stick");
var walked = Math.sqrt((after.x - before.x) * (after.x - before.x)
                       + (after.z - before.z) * (after.z - before.z));
console.log("      walked " + walked.toFixed(3) + " m");
assert(walked > 1.0, "the wearer's room MOVED (" + walked.toFixed(3) + " m over 45 frames)");
assert(near(after.y, before.y, 1e-4),
       "and it stayed LEVEL — a stick never flies a wearer up or down");
assert(near(after.yaw, before.yaw, 1e-4), "nor turns them");

// Back the other way: the same gesture reversed returns them.
for (var g = 0; g < 45; ++g) leftStick(0, -1);
var back = showRig("after 45 frames of full back stick");
assert(dist2(back, before) < 0.05,
       "and the same gesture reversed brings them back to where they started ("
       + dist2(back, before).toFixed(4) + " m off)");

// A stick inside the DEAD ZONE walks nobody.
var quiet = showRig("before a dead-zone nudge");
for (var q = 0; q < 10; ++q) leftStick(0, 0.2);
assert(dist2(showRig("after ten dead-zone nudges"), quiet) < 1e-5,
       "a stick inside the dead zone moves the rig not at all");

// ---- 2. THE SNAP TURN, ABOUT THE WEARER'S HEAD --------------------------
//
// The room turns by exactly 30 degrees and the wearer is not teleported.
//
// WHAT THIS RUNTIME CAN AND CANNOT PROVE, stated rather than implied. The
// simulated HMD stands almost exactly OVER its own reference-space origin
// (measured: the rig and the head are ~3 cm apart in XZ) and its pose is not
// perfectly still (~1.4 cm of drift between two located frames), so the two
// candidate formulas — turning about the HEAD and turning about the RIG's
// origin — differ here by less than the noise. The discriminating assertion is
// therefore the PURE one, `vr.grab_maths` case 10, which puts the wearer a
// metre off their room's centre and shows the naive version swinging them half
// a metre sideways. What this case proves is that the real path applies that
// arithmetic to a real rig, turns by the right amount, and does not throw the
// wearer across the room.
leftStick(0, 0);                              // centred: the snap turn is armed
var headBefore = vr.state().head;
var rigBefore = showRig("before the snap turn");
var turnsBefore = vr.interactionMode().turns;
leftStick(1, 0);                              // one flick right
var afterTurn = vr.interactionMode();
var rigAfter = showRig("after one flick right");
assert(afterTurn.turns === turnsBefore + 1, "the flick was answered: one turn");
assert(near(rigAfter.yaw, rigBefore.yaw + 30.0, 1e-3),
       "the room turned by exactly 30 degrees (" + rigBefore.yaw.toFixed(2) + " -> "
       + rigAfter.yaw.toFixed(2) + ")");
// The head is re-composed by the engine INSIDE the next frame, so ask for one.
editor.frame(2);
var headAfter = vr.state().head;
console.log("      head before (" + headBefore.x.toFixed(4) + ", " + headBefore.y.toFixed(4)
            + ", " + headBefore.z.toFixed(4) + ") after (" + headAfter.x.toFixed(4) + ", "
            + headAfter.y.toFixed(4) + ", " + headAfter.z.toFixed(4) + ")");
assert(dist2(headAfter, headBefore) < 0.05,
       "AND THE WEARER STAYED WHERE THEY WERE STANDING (" + dist2(headAfter, headBefore).toFixed(5)
       + " m, against this runtime's own ~1.4 cm of pose drift) — the turn moved the room "
       + "around them, it did not carry them");
assert(near(headAfter.yaw, headBefore.yaw + 30.0, 1.0),
       "...while what they are facing turned by the same 30 degrees");

// ONE FLICK, ONE TURN: the stick held over does nothing until it comes back.
var heldTurns = vr.interactionMode().turns;
leftStick(1, 0);
leftStick(1, 0);
assert(vr.interactionMode().turns === heldTurns,
       "a stick HELD over does not turn again — one flick, one turn");
leftStick(0, 0);                              // re-arm
leftStick(-1, 0);
var leftTurn = vr.interactionMode();
assert(leftTurn.turns === heldTurns + 1, "released and flicked the other way, it turns again");
assert(near(leftTurn.rig.yaw, rigAfter.yaw - 30.0, 1e-3),
       "...by -30 degrees (" + leftTurn.rig.yaw.toFixed(2) + ")");

// ---- 3. SMOOTH TURN IS A SESSION OPTION ---------------------------------
assert(vr.locomotion({ turn: "smooth" }).turn === "smooth", "smooth turn can be chosen");
var smoothBefore = vr.interactionMode().rig.yaw;
for (var sm = 0; sm < 10; ++sm) leftStick(1, 0);
var smoothAfter = vr.interactionMode().rig.yaw;
console.log("      ten smooth frames: yaw " + smoothBefore.toFixed(3) + " -> "
            + smoothAfter.toFixed(3));
assert(smoothAfter > smoothBefore + 5.0 && smoothAfter < smoothBefore + 15.0,
       "ten frames of smooth turn is about 10 degrees (90 deg/s at 1/90 s a frame), and it "
       + "does NOT need to be released between frames");
vr.locomotion({ turn: "snap" });

// ---- 4. THE RAY WORKS IN A SESSION TOO ----------------------------------
//
// The same pick as the headless suite, through a live session: the editor's VR
// preview puts the editor's own scene in the headset, so the dominant hand's
// ray reads the same document.
var cubes = scene.nodes().filter(function (n) { return n.name.indexOf("Cube") === 0; });
assert(cubes.length >= 1, "the scene has a cube");
node.transform(cubes[0].id, { position: { x: 0, y: 1, z: 0 } });
editor.frame(1);
// The aim pose is WORLD space, so it does not matter where the rig walked to.
assert(vr.inputInject({ hand: "right", valid: true,
                        aim: { x: 0, y: 1, z: 3 }, grip: { x: 0, y: 1, z: 3 },
                        select: 0, grab: 0, menu: false, stickX: 0, stickY: 0 }) === true,
       "inject the right hand in front of the cube");
var h = vr.hover();
console.log("vr.hover: " + JSON.stringify(h));
assert(h !== null && h.id === cubes[0].id, "the ray finds the cube inside a live session");

// ---- 5. vr.move IS THE ONE LOCOMOTION VERB, AND IT WORKS HERE -----------
var vBefore = vr.interactionMode().rig;
assert(vr.move({ forward: true, seconds: 0.5 }) === true,
       "vr.move() moves the wearer of whatever session is running");
var vAfter = vr.interactionMode().rig;
assert(dist2(vAfter, vBefore) > 0.5, "and the rig went with it ("
       + dist2(vAfter, vBefore).toFixed(3) + " m)");

// ---- 6. ENDING THE SESSION REMOVES THE INTERACTION ----------------------
assert(vr.end() === true, "vr.end() ends the session");
editor.frame(2);
var ended = vr.interactionMode();
console.log("vr.interactionMode after end: " + JSON.stringify(ended));
assert(ended.rig.live === false, "there is no rig any more");
assert(vr.move({ forward: true }) === false, "and vr.move() refuses again");

console.log("vr.input_session: PASS");
