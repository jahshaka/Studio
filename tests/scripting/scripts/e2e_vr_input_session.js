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

assert(player.endVr() === true, "player.endVr() ends the Player's session");
player.stop();
assert(vr.inject("right") === true, "the injection is withdrawn");

console.log("vr.input_session: PASS");
