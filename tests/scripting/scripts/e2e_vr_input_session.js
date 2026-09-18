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
// `vr.inject` — the ENGINE's own store, the one the runtime's action system
// fills, with `JAHSHAKA_VR_TEST_INJECT=1` in this suite's environment because
// the engine otherwise refuses to fake a hand a real profile is reporting. This
// suite is the one that proves that route composes with a live session:
// `vr.step()` is the clock, and the render loop STANDS DOWN from stepping the
// interaction while any sample carries `fromInjection`, so the walk below is
// exact rather than a race with the runtime's frame rate.
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
/// ONE FRAME WITH THE LEFT STICK somewhere — the non-dominant hand by default,
/// so this is the locomotion hand.
///
/// WHY THERE IS A RENDERED FRAME IN THE MIDDLE, and it is a MEASURED property
/// of a live session rather than a convenience: with a session running, the
/// engine's `vrStatus().input[]` is the SESSION's own per-frame copy, filled by
/// `readInput` inside the frame (the injected sample replaces the runtime's
/// answer there — OgreVrSession.cpp). So an injection written between frames is
/// not visible to anybody until the next frame carries it in: inject, render,
/// then step. With NO session there is no copy and no frame — the engine
/// reports the injected store directly, which is why the headless suite needs
/// neither (`scripting.e2e.vr_input_headless`).
///
/// The driver's own step stands down from the second injected frame onward (any
/// sample carrying `fromInjection`), and the frame before that carries the
/// PREVIOUS sample, so the stick below is integrated exactly once per call.
function leftStick(x, y) {
    var wrote = vr.inject("left", { valid: true,
                                    aim: { x: 0, y: 1.4, z: 0 }, grip: { x: 0, y: 1.4, z: 0 },
                                    select: 0, grab: 0, menuPressed: false,
                                    stick: { x: x, y: y } });
    editor.frame(1);
    assert(wrote === true && vr.step() === true,
           "one frame with the left stick at (" + x + ", " + y + ")");
}
/// A SIGNED YAW DIFFERENCE, WRAPPED (the Fable read of stage 1, finding 4). The
/// RIG's yaw is stored unnormalised and may be compared directly, but the HEAD's
/// comes out of a quaternion in (-180, 180]: a wearer facing 170 degrees who
/// turns 30 more reads -160, and a plain subtraction calls that a 330-degree
/// turn. Every head-yaw check below goes through this.
function yawDelta(a, b) {
    var d = a - b;
    while (d > 180.0) d -= 360.0;
    while (d <= -180.0) d += 360.0;
    return d;
}

project.create("vr input session " + Date.now());
scene.addPrimitive("Cube");

var a = vr.available();
console.log("vr.available: " + JSON.stringify(a));
assert(a.available === true, "the runtime answered: " + a.runtime);

assert(vr.begin({ mirror: "none" }) === true, "vr.begin() starts a session on the editor's scene");

// ---- 0. NOBODY IS WALKED WHILE THEY ARE STILL BEING PLACED --------------
//
// THE FIX FOR THE FABLE READ'S (a). A session begins with the host owing the
// wearer a PLACEMENT: it waits for a located frame it can PAIR with the rig the
// engine holds and then writes the rig that puts them where the editor camera
// stands, because a correction computed from a mismatched pair is a teleport.
// Both hosts refuse their own fly over those frames — and the thumbstick, which
// writes `setVrOrigin` from the interaction rather than from the host, used to
// walk and turn straight past that refusal: an origin the host then overwrote
// (the walk lost) composed with a head it was never paired with (the frames
// between wrong).
//
// Asserted before anything else in this file, because it is only true at the
// very beginning of a session: a full stick over the placing frames moves
// nobody, and the same stick moves them the moment the placement lands.
var placingSeen = false, movedWhilePlacing = false, turnedWhilePlacing = false;
for (var pf = 0; pf < 200; ++pf) {
    if (vr.state().preview.placing !== true) break;
    placingSeen = true;
    // The sample, then the frame that carries it into the session (see
    // leftStick) — and then the placement is asked AGAIN: if it landed inside
    // that frame, the step below would legitimately be allowed to walk, so
    // there is nothing left to assert and the loop is done.
    assert(vr.inject("left", { valid: true,
                               aim: { x: 0, y: 1.4, z: 0 }, grip: { x: 0, y: 1.4, z: 0 },
                               stick: { x: 1, y: 1 } }) === true,
           "a FULL stick, forward and right, while the wearer is being placed");
    editor.frame(1);
    if (vr.state().preview.placing !== true) break;
    var r0 = vr.interactionMode().rig;
    var t0 = vr.interactionMode().turns;
    vr.step();
    var r1 = vr.interactionMode().rig;
    if (r1.live && (Math.abs(r1.x - r0.x) > 1e-6 || Math.abs(r1.z - r0.z) > 1e-6))
        movedWhilePlacing = true;
    if (vr.interactionMode().turns !== t0) turnedWhilePlacing = true;
}
assert(placingSeen === true,
       "the session began owing the wearer a placement (preview.placing was true)");
assert(movedWhilePlacing === false,
       "a FULL stick over every one of those frames walked the wearer nowhere");
assert(turnedWhilePlacing === false, "...and turned them not at all");
leftStick(0, 0);                            // centre the stick and re-arm the snap turn

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
// THE DISTANCE IS THE EDITOR'S OWN FLY SPEED × THE FRAMES, and it is asserted
// as that product rather than as "more than a metre": 45 frames at the nominal
// 1/90 s is half a second of travel, and the level heading is a unit vector, so
// the path length is exactly speed/2. (Read from the preview's own report so
// this does not hard-code a preference's default.)
var flySpeed = vr.state().preview.flySpeed;
var headYaw0 = vr.state().head.yaw;
var before = showRig("before the walk");
for (var f = 0; f < 45; ++f) leftStick(0, 1);
var after = showRig("after 45 frames of full forward stick");
var walked = Math.sqrt((after.x - before.x) * (after.x - before.x)
                       + (after.z - before.z) * (after.z - before.z));
console.log("      walked " + walked.toFixed(3) + " m at a fly speed of " + flySpeed.toFixed(2)
            + " u/s (45 frames at 1/90 s = " + (flySpeed * 0.5).toFixed(3) + " m)");
assert(near(walked, flySpeed * 0.5, 0.05),
       "the wearer's room MOVED by exactly the fly speed's half second: " + walked.toFixed(3)
       + " m against " + (flySpeed * 0.5).toFixed(3));
assert(near(after.y, before.y, 1e-4),
       "and it stayed LEVEL — a stick never flies a wearer up or down");
assert(near(after.yaw, before.yaw, 1e-4), "nor turns them");

// BACK THE OTHER WAY, AGAINST A MEASURED BUDGET rather than a fixed one — and
// the budget is GEOMETRY, not slack. The stick walks along the HEAD's level
// heading, and this runtime's simulated head drifts with wall time (the suite
// measures that drift for the turn case below); the two legs are therefore not
// quite anti-parallel, and two legs of length L whose headings differ by Δ
// leave the wearer up to 2·L·sin(Δ/2) from where they started. Measured here
// over exactly the window that matters, because a fixed tolerance would be an
// assertion about how loaded the box is.
for (var g = 0; g < 45; ++g) leftStick(0, -1);
var back = showRig("after 45 frames of full back stick");
var headDrift = Math.abs(yawDelta(vr.state().head.yaw, headYaw0));
assert(headDrift < 30.0, "the runtime's head drifted less than 30 degrees over the walk (" + headDrift.toFixed(3)
       + ") - a fixture ceiling, so the geometric budget below cannot go vacuous");
var returnBudget = Math.max(0.05, 2.0 * walked * Math.abs(Math.sin(headDrift * Math.PI / 360.0)));
console.log("      the head's heading drifted " + headDrift.toFixed(3)
            + " degrees over the two legs: the return budget is "
            + returnBudget.toFixed(4) + " m");
assert(dist2(back, before) < returnBudget,
       "and the same gesture reversed brings them back to where they started ("
       + dist2(back, before).toFixed(4) + " m off, against the drift's own "
       + returnBudget.toFixed(4) + " m)");

// A stick inside the DEAD ZONE walks nobody.
var quiet = showRig("before a dead-zone nudge");
for (var q = 0; q < 10; ++q) leftStick(0, 0.2);
assert(dist2(showRig("after ten dead-zone nudges"), quiet) < 1e-5,
       "a stick inside the dead zone moves the rig not at all");

// ---- 1b. THE PROJECT'S FLY SPEED IS THE WEARER'S SPEED ------------------
//         (lane VR-WORLD-1, the owner's request 2026-09-18)
//
// How a wearer moves is a property of the PROJECT (`world.vr`), and a session
// ADOPTS it when it begins. Three claims, and the rig is what measures them:
//
//   * the running session flies at the project's number;
//   * a document edit MID-SESSION does not move the wearer — the session runs
//     on what it latched, because nothing may change speed under somebody
//     wearing a headset;
//   * `vr.locomotion` overrides it for this session, the rig moves at the
//     overridden speed, and the DOCUMENT is not touched;
//   * and the next session adopts the project again, override gone.
var docSpeed = world.vr().flySpeed;
console.log("world.vr().flySpeed = " + docSpeed);
assert(near(vr.locomotion().flySpeed, docSpeed),
       "the session runs at the project's fly speed (" + docSpeed + " m/s)");
assert(vr.locomotion().session === true, "...and says a session has latched a project");
assert(near(vr.state().preview.flySpeed, docSpeed),
       "the editor preview's own fly (the arrow keys) is the same number");

world.vr({ flySpeed: 9 });
assert(near(vr.locomotion().flySpeed, docSpeed),
       "a PROJECT edit mid-session leaves the running session alone — nothing changes "
       + "speed under a wearer");

var over = vr.locomotion({ flySpeed: 3 });
assert(near(over.flySpeed, 3), "the session can be overridden to 3 m/s");
assert(near(world.vr().flySpeed, 9), "...and the override wrote NOTHING to the document");
leftStick(0, 0);
var slowFrom = showRig("before 45 frames at the overridden 3 m/s");
for (var sf = 0; sf < 45; ++sf) leftStick(0, 1);
var slowTo = showRig("after them");
var slowWalk = Math.sqrt((slowTo.x - slowFrom.x) * (slowTo.x - slowFrom.x)
                         + (slowTo.z - slowFrom.z) * (slowTo.z - slowFrom.z));
console.log("      walked " + slowWalk.toFixed(3) + " m — 45 frames at 1/90 s of 3 m/s is 1.5");
assert(near(slowWalk, 1.5, 0.05),
       "THE RIG MOVED AT THE SETTING'S OWN SPEED: " + slowWalk.toFixed(3) + " m against 1.5");
leftStick(0, 0);

// A NEW SESSION ADOPTS THE PROJECT AGAIN, and the override is gone with the
// session that made it.
assert(vr.end() === true, "the session ends");
assert(vr.begin({ mirror: "none" }) === true, "...and a second one begins on the same scene");
var replaced = false;
for (var rf = 0; rf < 200 && !replaced; ++rf) {
    editor.frame(1);
    var rs = vr.state();
    replaced = rs.head.valid && rs.preview.placing === false;
}
assert(replaced === true, "the wearer was placed again");
var adopted = vr.locomotion();
assert(near(adopted.flySpeed, 9),
       "THE NEW SESSION ADOPTED THE PROJECT'S 9 m/s: " + adopted.flySpeed);
assert(adopted.overridden.length === 0, "...and the previous session's override is gone");
leftStick(0, 0);
var fastFrom = showRig("before 45 frames at the project's 9 m/s");
for (var ff = 0; ff < 45; ++ff) leftStick(0, 1);
var fastTo = showRig("after them");
var fastWalk = Math.sqrt((fastTo.x - fastFrom.x) * (fastTo.x - fastFrom.x)
                         + (fastTo.z - fastFrom.z) * (fastTo.z - fastFrom.z));
console.log("      walked " + fastWalk.toFixed(3) + " m — 45 frames at 1/90 s of 9 m/s is 4.5");
assert(near(fastWalk, 4.5, 0.08),
       "THE RIG MOVED AT THE PROJECT'S SPEED: " + fastWalk.toFixed(3) + " m against 4.5");
// AN OVERRIDE ASKED FOR BEFORE A SESSION SURVIVES THE BEGIN, and dies with the
// session that used it (the Fable read of VR-WORLD-1, item 3): adopting a
// project used to clear every override, so a `vr.locomotion` issued in the
// breath before `vr.begin` was refused silently.
assert(vr.end() === true, "the session ends again");
assert(vr.locomotion().session === false, "...and the latch goes with it");
vr.locomotion({ flySpeed: 6 });
assert(near(vr.locomotion().flySpeed, 6), "an override set with NO session stands");
assert(vr.begin({ mirror: "none" }) === true, "a third session begins");
assert(near(vr.locomotion().flySpeed, 6),
       "THE OVERRIDE SURVIVED THE ADOPTION — the caller's ask was not thrown away");
assert(vr.locomotion().overridden.indexOf("flySpeed") >= 0, "...and is still listed as one");
assert(vr.end() === true, "that session ends");
assert(near(vr.locomotion().flySpeed, 9),
       "...and ITS overrides died with it: back to the project's 9 m/s");
assert(vr.begin({ mirror: "none" }) === true, "the session the rest of this file needs");
var settled = false;
for (var af = 0; af < 200 && !settled; ++af) {
    editor.frame(1);
    var as = vr.state();
    settled = as.head.valid && as.preview.placing === false;
}
assert(settled === true, "the wearer is placed once more");

// Back to the shipped default for everything below (the snap turn's cases read
// the rig, not the speed, but a suite leaves its fixture as it found it).
world.vr({ flySpeed: 15 });
leftStick(0, 0);

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

// FIRST, THE CONTROL: HOW MUCH DOES THIS RUNTIME MOVE THE HEAD BY ITSELF?
//
// Monado's simulated HMD is not still — its pose drifts between located frames,
// and the drift grows with the WALL TIME between two reads, so on a loaded box
// it is larger than on a quiet one (measured: 1.1 cm solo, 3.8 cm and 1.4
// degrees of heading under a -j4 gate). An assertion with a fixed tolerance is
// therefore an assertion about the load, which is the flake class this house
// has a rule about. So the control is MEASURED, over the same number of frames
// the subject gets, and the subject is compared against it.
var ctrlBefore = vr.state().head;
editor.frame(2);
var ctrlAfter = vr.state().head;
var driftM = dist2(ctrlAfter, ctrlBefore);
var driftDeg = Math.abs(yawDelta(ctrlAfter.yaw, ctrlBefore.yaw));
console.log("      the runtime's OWN drift over two frames: " + driftM.toFixed(5) + " m, "
            + driftDeg.toFixed(3) + " degrees");
var posBudget = Math.max(0.25, driftM * 4.0);
var yawBudget = Math.max(2.0, driftDeg * 4.0);

var headBefore = vr.state().head;
var rigBefore = showRig("before the snap turn");
var turnsBefore = vr.interactionMode().turns;
leftStick(1, 0);                              // one flick right
var afterTurn = vr.interactionMode();
var rigAfter = showRig("after one flick right");
assert(afterTurn.turns === turnsBefore + 1, "the flick was answered: one turn");
// A flick RIGHT turns the wearer RIGHT = a NEGATIVE step in the tree's
// right-handed yaw about +Y (yaw 0 looks down -Z; +yaw carries -Z toward -X,
// the wearer's left) — the lead's fix at merge; the suite asserted the number
// before and could not see the direction.
assert(near(rigAfter.yaw, rigBefore.yaw - 30.0, 1e-3),
       "the room turned by exactly 30 degrees, clockwise from above (" + rigBefore.yaw.toFixed(2) + " -> "
       + rigAfter.yaw.toFixed(2) + ")");
// The head is re-composed by the engine INSIDE the next frame, so ask for one.
editor.frame(2);
var headAfter = vr.state().head;
console.log("      head before (" + headBefore.x.toFixed(4) + ", " + headBefore.y.toFixed(4)
            + ", " + headBefore.z.toFixed(4) + ") after (" + headAfter.x.toFixed(4) + ", "
            + headAfter.y.toFixed(4) + ", " + headAfter.z.toFixed(4) + ")");
assert(dist2(headAfter, headBefore) < posBudget,
       "AND THE WEARER STAYED WHERE THEY WERE STANDING (" + dist2(headAfter, headBefore).toFixed(5)
       + " m, against a budget of " + posBudget.toFixed(5) + " m measured off this runtime's "
       + "own drift) — the turn moved the room around them, it did not carry them");
assert(near(yawDelta(headAfter.yaw, headBefore.yaw), -30.0, yawBudget),
       "...while what they are facing turned by the same 30 degrees ("
       + headBefore.yaw.toFixed(2) + " -> " + headAfter.yaw.toFixed(2) + ", delta "
       + yawDelta(headAfter.yaw, headBefore.yaw).toFixed(2) + ", budget "
       + yawBudget.toFixed(2) + ") — through the ±180 WRAP, because a head yaw comes out of "
       + "a quaternion and a plain subtraction across the seam reads 330 for a 30-degree turn");

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
assert(near(leftTurn.rig.yaw, rigAfter.yaw + 30.0, 1e-3),
       "...counter-clockwise, +30 in the tree's yaw (" + leftTurn.rig.yaw.toFixed(2) + ")");

// ---- 3. SMOOTH TURN IS A SESSION OPTION ---------------------------------
assert(vr.locomotion({ turn: "smooth" }).turn === "smooth", "smooth turn can be chosen");
var smoothBefore = vr.interactionMode().rig.yaw;
for (var sm = 0; sm < 10; ++sm) leftStick(1, 0);
var smoothAfter = vr.interactionMode().rig.yaw;
console.log("      ten smooth frames: yaw " + smoothBefore.toFixed(3) + " -> "
            + smoothAfter.toFixed(3));
assert(smoothAfter < smoothBefore - 5.0 && smoothAfter > smoothBefore - 15.0,
       "ten frames of smooth turn RIGHT is about 10 degrees clockwise (90 deg/s at 1/90 s a frame), and it "
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
var wroteRight = vr.inject("right", { valid: true,
                                      aim: { x: 0, y: 1, z: 3 }, grip: { x: 0, y: 1, z: 3 },
                                      select: 0, grab: 0, menuPressed: false,
                                      stick: { x: 0, y: 0 } });
editor.frame(1);                        // the session reads it inside a frame (see leftStick)
assert(wroteRight === true && vr.step() === true,
       "inject the right hand in front of the cube, carry it in on a frame, and step");
var h = vr.hover();
console.log("vr.hover: " + JSON.stringify(h));
assert(h !== null && h.id === cubes[0].id, "the ray finds the cube inside a live session");

// ---- 4b. THE GIZMO, WITH A REAL SESSION UNDER IT (phase 4b stage 2) -----
//
// THE TWO THINGS ONLY A SESSION CAN SHOW, and neither is in the headless
// suite (scripting.e2e.vr_gizmo) because neither exists there:
//
//   * THE EYE IS THE HEAD. With no session the wearer's eye is the aiming hand
//     itself; with one it is the located head, which is what the gizmo's size
//     rule measures from. Asserted against `vr.state().head` — a pose this
//     script never wrote.
//   * THE SIZE IS FROZEN FOR THE LENGTH OF A DRAG. This runtime's simulated
//     head DRIFTS between frames (the control measured in section 2), and the
//     rotation gizmo's drag angle is resolved against a sphere whose radius is
//     that size: a gizmo that re-sized while the head wandered would turn the
//     object as the wearer breathed.
editor.select(cubes[0].id);
editor.setGizmoMode("translate");
// The hand in front of the cube, pointing at it: one frame to carry the sample
// into the session, then one interaction frame.
// THE HAND IS AT THE WEARER'S SIDE, not at some fixed spot in the world: this
// script has walked and turned the wearer by now, and the gizmo is sized from
// the HEAD (a constant angular size) — so a hand left standing next to the cube
// while the head is fifteen metres away would be INSIDE a five-metre arrow, and
// the first thing its ray touched would be whichever handle it was standing in.
// A real hand is always about an arm's length from the eye, which is the case
// the sizing rule is written for.
function handNow() {
    var head = vr.state().head;
    return { x: head.x, y: head.y - 0.3, z: head.z };
}
function aimHandAt(to, buttons) {
    var gHand = handNow();
    var dx = to.x - gHand.x, dy = to.y - gHand.y, dz = to.z - gHand.z;
    var len = Math.sqrt(dx * dx + dy * dy + dz * dz);
    dx /= len; dy /= len; dz /= len;
    var pose = { x: gHand.x, y: gHand.y, z: gHand.z,
                 yaw: Math.atan2(-dx, -dz) * 180 / Math.PI,
                 pitch: Math.asin(Math.max(-1, Math.min(1, dy))) * 180 / Math.PI };
    var m = { valid: true, aim: pose, grip: pose,
              select: (buttons && buttons.select) || 0, grab: 0,
              menuPressed: !!(buttons && buttons.menu), stick: { x: 0, y: 0 } };
    assert(vr.inject("right", m) === true, "aim the hand at ("
           + to.x.toFixed(3) + ", " + to.y.toFixed(3) + ", " + to.z.toFixed(3) + ")"
           + ((buttons && buttons.select) ? " SELECT" : ""));
    editor.frame(1);
    assert(vr.step() === true, "...and step the interaction");
}
aimHandAt({ x: 0, y: 1, z: 0 });
var gz = vr.gizmo();
console.log("vr.gizmo (live session): " + JSON.stringify(gz));
var headNow = vr.state().head;
assert(gz.armed === true, "the gizmo is armed by the wearer's controller");
assert(Math.abs(gz.eye.x - headNow.x) < 0.2 && Math.abs(gz.eye.y - headNow.y) < 0.2
       && Math.abs(gz.eye.z - headNow.z) < 0.2,
       "and its EYE is the located HEAD, not the hand (gizmo eye " + gz.eye.x.toFixed(3) + ", "
       + gz.eye.y.toFixed(3) + ", " + gz.eye.z.toFixed(3) + " vs head " + headNow.x.toFixed(3)
       + ", " + headNow.y.toFixed(3) + ", " + headNow.z.toFixed(3) + ")");

// The X arrow, at 0.06 of the gizmo scale from the pivot (past the plane
// squares' 0.025, short of the arrow's 0.085 tip).
var gScale = vr.gizmo().scale;
var arrow = { x: 0.06 * gScale, y: 1, z: 0 };
aimHandAt(arrow);
assert(vr.gizmo().handle === "x", "the aim ray names the X arrow (it names '"
       + vr.gizmo().handle + "')");
var gPushes = editor.undoState().pushes;
aimHandAt(arrow, { select: 1 });
assert(vr.gizmo().dragging === true, "the trigger press starts a handle drag in a live session");
var frozen = vr.gizmo().scale;
var headAtPress = vr.state().head;
// ...AND THE NEXT STEPPED FRAME IS WHERE THE SIZE WOULD MOVE (the lead's fix
// round, item 5). `editor.frame()` alone proves nothing here: with an injection
// armed the render driver's own tick returns before it steps the interaction,
// so nothing between two reads would have run and the assertion would pass on
// an object nobody touched. The read has to bracket a real interaction frame —
// which is what aimHandAt does — and the head has to have MOVED across it,
// which this runtime's drifting simulated HMD does by itself (the control in
// section 2 measured it).
aimHandAt({ x: arrow.x + 1.0, y: 1, z: 0 }, { select: 1 });
var headAfter = vr.state().head;
var headMoved = dist2(headAfter, headAtPress);
console.log("      the head moved " + headMoved.toFixed(5) + " m across the drag's frames; "
            + "the gizmo scale is " + vr.gizmo().scale.toFixed(4) + " (was " + frozen.toFixed(4)
            + ")");
assert(headMoved > 0.0,
       "the runtime's head really did move while the drag ran (otherwise the next assertion is "
       + "about nothing)");
assert(vr.gizmo().scale === frozen,
       "THE GIZMO'S SIZE IS FROZEN FOR THE LENGTH OF THE DRAG, through stepped frames in which "
       + "the head moved (" + frozen.toFixed(4) + ") — the size rule measures from the eye, so "
       + "an unfrozen one would re-size the handles the drag is measuring against");
aimHandAt({ x: arrow.x + 1.0, y: 1, z: 0 });
var moved = node.transform(cubes[0].id).position;
console.log("      the cube landed at (" + moved.x.toFixed(3) + ", " + moved.y.toFixed(3) + ", "
            + moved.z.toFixed(3) + ")");
assert(moved.x > 0.5 && Math.abs(moved.y - 1) < 1e-3 && Math.abs(moved.z) < 1e-3,
       "the drag moved the cube along X and on no other axis");
assert(editor.undoState().pushes === gPushes + 1, "...in exactly one undo entry");
assert(vr.gizmo().dragging === false, "and the release ended it");
node.transform(cubes[0].id, { position: { x: 0, y: 1, z: 0 } });
editor.frame(1);

// ---- 5. vr.move IS THE ONE LOCOMOTION VERB, AND IT WORKS HERE -----------
var vBefore = vr.interactionMode().rig;
assert(vr.move({ forward: true, seconds: 0.5 }) === true,
       "vr.move() moves the wearer of whatever session is running");
var vAfter = vr.interactionMode().rig;
assert(dist2(vAfter, vBefore) > 0.5, "and the rig went with it ("
       + dist2(vAfter, vBefore).toFixed(3) + " m)");

// ---- 5b. THE TELEPORT MOVES THE WEARER (VR_INPUT_SPEC §6 row L4) --------
//
// THE HALF `scripting.e2e.vr_teleport` CANNOT ASSERT, for its own stated
// reason: with no session there is no rig, so that suite proves the arc, the
// landing, the refusals and the DRAWING and then asserts the refusal. Here
// there is a rig, so the wearer actually arrives — and the two claims that
// matter are the ones only a rig can show: they arrive OVER the landing point,
// and they arrive FACING THE WAY THEY ALREADY FACED (a teleport that also spun
// the room is the fastest way to make somebody sick).
//
// The arithmetic is `vr.grab_maths`'s teleportedTo; the binding is the DOMINANT
// stick pushed forward, which with the injected route is one sample.
/// ONE FRAME WITH THE DOMINANT (right) HAND aiming a throw. Three metres out in
// x so the arc is nowhere near the cube this file added at the origin.
function rightThrow(stickY) {
    var pose = { x: 3, y: 1.4, z: 0 };
    var wrote = vr.inject("right", { valid: true, aim: pose, grip: pose,
                                     select: 0, grab: 0, menuPressed: false,
                                     stick: { x: 0, y: stickY } });
    editor.frame(1);
    assert(wrote === true && vr.step() === true,
           "one frame with the dominant stick at y = " + stickY);
}

rightThrow(1);
var arc = vr.inputState().teleport;
console.log("the armed arc, in a live session: " + JSON.stringify(arc));
assert(arc.armed === true, "the dominant stick forward arms the arc in a live session");
assert(arc.landed === true && arc.valid === true,
       "it lands on the project's LOCKED ground, standably — a lock stops a click from "
       + "selecting a floor, not a wearer from standing on it");
assert(near(arc.landing.x, 3, 1e-3) && near(arc.landing.y, 0.0, 5e-3),
       "on the ground (which sits a hair above y = 0)");
// THE PICKED POINT IS WHERE THE STRAIGHT PIECE MET THE SURFACE — up to a couple
// of centimetres short of where the CURVE crosses it (5.3425 m), and right:
// the hit is ON the floor, which is what a landing is.
assert(near(arc.landing.z, -5.32, 0.03),
       "5.32 m along the aim: the chord of the piece that crosses the ground");
// THE DRAWING, WHICH ONLY A LIVE SCENE CAN SHOW: every piece of the curve is a
// line node in the world, on both helper channels (the eyes and the desktop
// editor's picture, and no capture), with the landing ring at the end.
assert(arc.drawn === arc.points - 1 && arc.drawn >= 2,
       "and every piece of the curve is drawn in the world (" + arc.drawn + " line nodes)");
assert(arc.marker === true, "with the landing ring standing on the landing");

var rigBefore = vr.interactionMode().rig;
var headBefore = vr.state().head;
var eyeHeight = headBefore.y - rigBefore.y;
console.log("      before the throw: rig (" + rigBefore.x.toFixed(2) + ", "
            + rigBefore.y.toFixed(2) + ", " + rigBefore.z.toFixed(2) + ") yaw "
            + rigBefore.yaw.toFixed(2) + ", the head " + eyeHeight.toFixed(3)
            + " m above its floor");
rightThrow(0);
var arcAfter = vr.inputState().teleport;
var rigAfter = vr.interactionMode().rig;
// ONE RENDERED FRAME BEFORE THE HEAD IS BELIEVED, and it is the session's own
// rule rather than a settle: `vrStatus().headPosition` is filled by
// `xrLocateViews` INSIDE the frame, so the head a host reads between frames is
// the one the last frame located — the rig has already moved, the head has not
// yet been re-composed through it. (Measured here: the rig read (3.15, 0.00,
// -5.31) immediately and the head still read its pre-teleport (1.08, 5.03,
// 10.15).) Counting a frame, never waiting a clock.
editor.frame(1);
var headAfter = vr.state().head;
console.log("      after it: rig (" + rigAfter.x.toFixed(2) + ", " + rigAfter.y.toFixed(2)
            + ", " + rigAfter.z.toFixed(2) + ") yaw " + rigAfter.yaw.toFixed(2)
            + ", the head at (" + headAfter.x.toFixed(2) + ", " + headAfter.y.toFixed(2)
            + ", " + headAfter.z.toFixed(2) + ")");
assert(arcAfter.armed === false, "letting the stick go took the landing");
assert(arcAfter.drawn === 0 && arcAfter.marker === false,
       "...and took the curve out of the world");
assert(arcAfter.teleports === 1, "one teleport happened");
assert(near(headAfter.x, arc.landing.x, 0.02) && near(headAfter.z, arc.landing.z, 0.02),
       "THE WEARER IS STANDING ON THE LANDING POINT: the head is over (3, -5.343)");
assert(near(headAfter.y, arc.landing.y + eyeHeight, 0.02),
       "at the same height above their floor as before (" + eyeHeight.toFixed(3) + " m)");
assert(near(rigAfter.yaw, rigBefore.yaw, 1e-3),
       "and facing exactly the way they already faced — a teleport does not turn anybody");

// ---- THE FALLBACK PLANE IS THE WEARER'S OWN FLOOR (the lead's read, item 4)
//
// A throw that meets no geometry falls back to a PLANE, and the first round
// used the world's y = 0 — a guess about the content: on a scene built on a
// raised floor, or on terrain, it landed the wearer in mid-air and reported it
// GREEN. The plane is the floor the WEARER IS STANDING ON, which is the rig's
// own y, and this is the case that can tell the two apart: stand the wearer
// five metres up (their floor is now y = 5), aim a throw from a hand above
// that, and the landing must be on THEIR floor — not on the document's ground
// five metres below, which the arc would reach a good deal later.
assert(vr.teleport({ to: { x: 0, y: 5, z: 0 } }) === true,
       "the wearer is stood on a floor five metres up");
editor.frame(1);
var raisedRig = vr.interactionMode().rig;
console.log("      the wearer's floor is now y = " + raisedRig.y.toFixed(3));
assert(near(raisedRig.y, 5.0, 0.01), "...and the rig says so");
var highPose = { x: 3, y: 6.4, z: 0 };
assert(vr.inject("right", { valid: true, aim: highPose, grip: highPose,
                            stick: { x: 0, y: 1 } }) === true, "a hand 1.4 m above that floor");
editor.frame(1);
assert(vr.step() === true, "one frame");
var high = vr.inputState().teleport;
console.log("the throw from the raised floor: " + JSON.stringify(high));
assert(high.landed === true && high.valid === true, "the throw lands");
assert(near(high.landing.y, 5.0, 1e-3),
       "ON THE WEARER'S OWN FLOOR (y = 5), not on the document's ground far below");
assert(near(high.landing.z, -5.3425, 5e-3),
       "...at the level throw's own 5.343 m from the hand");
assert(near(high.normal.y, 1.0, 1e-6), "with an upward normal");
assert(vr.inject("right", { valid: true, aim: highPose, grip: highPose,
                            stick: { x: 0, y: 0 } }) === true && (editor.frame(1) || true)
       && vr.step() === true, "the stick comes back");
editor.frame(1);
var arrived = vr.state().head;
assert(near(arrived.y, 5.0 + eyeHeight, 0.05),
       "and the wearer is standing on it, at their own eye height ("
       + arrived.y.toFixed(2) + ")");

// AND THE VERB DOES THE SAME THING, to a named place (no arc, no slope test —
// a script that says where means it).
var teleportsBeforeNamed = vr.inputState().teleport.teleports;
assert(vr.teleport({ to: { x: -4, y: 0, z: 7 } }) === true,
       "vr.teleport({to}) stands the wearer at a named point");
editor.frame(1);                        // ...and the frame that re-locates the head
var headNamed = vr.state().head;
assert(near(headNamed.x, -4, 0.02) && near(headNamed.z, 7, 0.02),
       "...and the head is over it: (" + headNamed.x.toFixed(2) + ", "
       + headNamed.z.toFixed(2) + ")");
assert(near(vr.interactionMode().rig.yaw, rigBefore.yaw, 1e-3), "still facing the same way");
assert(vr.inputState().teleport.teleports === teleportsBeforeNamed + 1,
       "every teleport counted, the named one included (" 
       + vr.inputState().teleport.teleports + " so far this session)");
assert(vr.inject("right") === true, "the aiming hand is withdrawn");
editor.frame(1);

// ---- 6. ENDING THE SESSION REMOVES THE INTERACTION ----------------------
assert(vr.end() === true, "vr.end() ends the session");
editor.frame(2);
var ended = vr.interactionMode();
console.log("vr.interactionMode after end: " + JSON.stringify(ended));
assert(ended.rig.live === false, "there is no rig any more");
assert(vr.move({ forward: true }) === false, "and vr.move() refuses again");

// ---- 7. IN THE PLAYER, ONLY LOCOMOTION RUNS — FOR THE VERBS TOO ---------
//
// THE FIX FOR THE FABLE READ'S (c). "The Player edits nothing" was enforced on
// the button EDGES only (the interaction's step() skips the whole editing half
// when the Player hosts the session), so `vr.select` / `vr.grab` / `vr.release`
// from a console, a script or an MCP session edited the document of a run the
// Player was showing — the docstring promised otherwise. Now the refusal lives
// in the service, where a verb and a button cannot disagree about it, and it
// SAYS which reason it refused for.
//
// A SECOND SESSION IN THIS PROCESS, deliberately: the rule is about WHO hosts
// the session, so the only way to assert it is to let the other host have one.
// (This is the same sequence `vr.player_session` uses; the page has to have
// been shown, because the mirror is the Player's own on-screen View.)
app.space("player");
assert(app.columns().space === "player", "the Player page is up");
assert(player.play({ vr: true }) === true, "player.play({vr:true}) starts the run in VR");
assert(player.state().vr.active === true, "the Player is hosting a session now");
player.frame(2);

var pmode = vr.interactionMode();
console.log("vr.interactionMode under the Player: " + JSON.stringify(pmode));
assert(pmode.installed === true, "the interaction is installed (a session is running)");
// WHATEVER THE SELECTION IS, it must come out of this unchanged — the set is
// not empty here (a new primitive selects itself) and "nothing was edited" is
// about what these three verbs did, not about the state they found.
var selBefore = JSON.stringify(editor.selectionSet());

// A hand IS reporting — the refusal below is about the HOST, not about an empty
// ray, which is exactly what the old boolean could not tell apart.
var wrotePlayerHand = vr.inject("right", { valid: true, aim: { x: 0, y: 1, z: 3 },
                                           grip: { x: 0, y: 1, z: 3 } });
player.frame(1);                        // ...carried in by the Player's own frame
assert(wrotePlayerHand === true && vr.step() === true,
       "a controller reports, pointing where the cube is");
assert(vr.select() === false, "vr.select() REFUSES in the Player");
console.log("app.lastError: " + app.lastError());
assert(app.lastError().indexOf("PLAYER") >= 0,
       "...naming the reason: " + app.lastError());
assert(vr.grab() === false, "vr.grab() refuses too");
assert(app.lastError().indexOf("PLAYER") >= 0, "...and says so: " + app.lastError());
assert(vr.release() === false, "and so does vr.release()");
assert(app.lastError().indexOf("PLAYER") >= 0, "...with the same reason: " + app.lastError());
assert(JSON.stringify(editor.selectionSet()) === selBefore,
       "AND THE SELECTION IS EXACTLY WHAT IT WAS — nothing was selected, toggled or "
       + "deselected by any of that: " + selBefore);

// ...AND THE STICK STILL WALKS THE WEARER, because locomotion is the half the
// Player DOES run.
var pRig = vr.interactionMode().rig;
assert(pRig.live === true, "there is a rig again (the Player's)");
assert(vr.move({ forward: true, seconds: 0.5 }) === true,
       "vr.move() moves the wearer of the Player's session");
var pRigAfter = vr.interactionMode().rig;
assert(dist2(pRigAfter, pRig) > 0.5, "and the rig went with it ("
       + dist2(pRigAfter, pRig).toFixed(3) + " m)");

// ---- 7b. AND THE PLAYER'S THROW WORKS (the lead's read, item 1) --------
//
// THE DEFECT THIS CASE EXISTS FOR. A teleport is LOCOMOTION, which the Player
// runs — but the Player's step() also cancels "whatever the editor was doing"
// EVERY frame, and that cancel took the armed arc with it. So in the Player the
// arc was cancelled and re-armed on every single frame (the counters spun) and
// the release could never find one armed: the wearer aimed, saw a curve, let go
// and stayed exactly where they were. Split now — the Player cancels the
// EDITING half only — and this is the assertion that says so.
/// One frame of the Player's own loop with the dominant stick somewhere.
function playerThrow(stickY) {
    // SIX METRES OUT IN X, clear of the cube this file added at the origin —
    // aimed at it the throw hits a vertical face and is refused, which is a
    // different case (and the teleport suite's).
    var pose = { x: 6, y: 1.4, z: 0 };
    var wrote = vr.inject("right", { valid: true, aim: pose, grip: pose,
                                     select: 0, grab: 0, menuPressed: false,
                                     stick: { x: 0, y: stickY } });
    player.frame(1);
    assert(wrote === true && vr.step() === true,
           "one PLAYER frame with the dominant stick at y = " + stickY);
}

playerThrow(1);
var pArc = vr.inputState().teleport;
console.log("the Player's armed arc: " + JSON.stringify(pArc));
assert(pArc.armed === true, "the arc arms in the Player and STAYS armed frame after frame");
assert(pArc.landed === true && pArc.valid === true, "with a standable landing");
assert(pArc.drawn >= 2, "drawn in the wearer's eyes (the Player draws no other helper)");
// TWO MORE FRAMES: this is the heart of it — the first round's arc did not
// survive a single one of them.
playerThrow(1);
playerThrow(1);
var pArc2 = vr.inputState().teleport;
assert(pArc2.armed === true, "still armed three frames later");
assert(pArc2.arms === pArc.arms,
       "...and it was never re-armed in between (the counter stood still: "
       + pArc2.arms + ")");
var pRigBefore = vr.interactionMode().rig;
var pTeleports = pArc2.teleports;
playerThrow(0);
player.frame(1);
var pRigAfter = vr.interactionMode().rig;
console.log("      the Player's rig: (" + pRigBefore.x.toFixed(2) + ", "
            + pRigBefore.z.toFixed(2) + ") -> (" + pRigAfter.x.toFixed(2) + ", "
            + pRigAfter.z.toFixed(2) + ")");
assert(vr.inputState().teleport.teleports === pTeleports + 1,
       "letting the stick go in the PLAYER takes the landing");
assert(dist2(pRigAfter, pRigBefore) > 1.0,
       "and the Player's OWN rig moved with it (" + dist2(pRigAfter, pRigBefore).toFixed(2)
       + " m) — the wearer of a run can teleport");
assert(near(pRigAfter.yaw, pRigBefore.yaw, 1e-3), "facing the way they already faced");

assert(player.endVr() === true, "player.endVr() ends the Player's session");
player.stop();
assert(vr.inject("right") === true, "the injection is withdrawn");

console.log("vr.input_session: PASS");
