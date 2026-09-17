// scripting.e2e.vr_input_headless — EVERY VR GESTURE, WITH NO HEADSET
// (SPECS/VR_INPUT_SPEC.md stage 1 §10).
//
// THE POINT OF THE INJECTION ROUTE, demonstrated: this process was launched
// WITHOUT --vr, so no OpenXR loader was ever opened and no session exists — and
// every controller gesture still runs, end to end, through the same code a
// wearer drives. `vr.inputInject` writes the per-hand state the runtime's action
// system would have written and the interaction cannot tell the difference: the
// aim ray, the document pick, the selection, the rigid grab, the snap and the
// undo macro are all the real ones.
//
// WHAT IS *NOT* HERE, and where it is instead:
//   * THE ARITHMETIC (rigid attach, the far lever, the snap quantisation, the
//     turn about the head, the stick's flight) — `vr.grab_maths`, pure and
//     display-free, with hand-computed expectations for every case.
//   * LOCOMOTION ACTUALLY MOVING A WEARER — it cannot happen here, and that is
//     a measured engine fact rather than a gap: Engine::setVrOrigin is a NO-OP
//     without a session (OgreEngine.cpp:1296, "a host sets it right after
//     beginVrSession"), so there is no rig to move and the verbs REFUSE. What
//     this file asserts is the refusal, in words; the rig moving is
//     `vr.input_session`, on Monado's simulated runtime.
//   * UNDO REACHING BACK INTO THE GESTURE — a script run is ONE open undo
//     macro, so `editor.undo()` from inside this file cannot reach the step the
//     release just recorded (the same note six other e2e scripts carry). What
//     is asserted instead is the honest counter: `editor.undoState().pushes`
//     moves by exactly ONE for a one-object gesture and exactly TWO for a
//     two-object one, which is the macro's shape.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function near(a, b, eps) { return Math.abs(a - b) <= (eps === undefined ? 1e-3 : eps); }
function throws(fn, needle, msg) {
    try { fn(); } catch (e) {
        assert(String(e).indexOf(needle) >= 0, msg + " (" + e + ")");
        return;
    }
    throw new Error("assert failed: " + msg + " — it did not throw");
}
function posOf(id) { return node.transform(id).position; }
function showPos(tag, id) {
    var p = posOf(id);
    console.log("      " + tag + " = (" + p.x.toFixed(3) + ", " + p.y.toFixed(3) + ", "
                + p.z.toFixed(3) + ")");
}

/// ONE FRAME OF CONTROLLER INPUT. Every field is passed EVERY time on purpose:
/// an injected hand keeps what it was given until it is changed (that is the
/// verb's contract), so a test that omitted a button would be asserting against
/// whatever the previous case left held.
function sendHand(o) {
    var pose = { x: o.x, y: o.y, z: o.z, yaw: o.yaw || 0, pitch: o.pitch || 0 };
    var m = { hand: o.hand || "right", valid: true, aim: pose, grip: pose,
              select: o.select || 0, grab: o.grab || 0, menu: !!o.menu,
              stickX: o.stickX || 0, stickY: o.stickY || 0 };
    assert(vr.inputInject(m) === true, "inject " + (o.hand || "right") + " at ("
           + o.x + ", " + o.y + ", " + o.z + ") yaw " + (o.yaw || 0) + " pitch " + (o.pitch || 0)
           + (o.select ? " select" : "") + (o.grab ? " grab" : "") + (o.menu ? " menu" : ""));
}

/// A TRIGGER CLICK — the pose with the trigger UP, then the same pose with it
/// DOWN. Both halves matter: the interaction fires on the press EDGE (one
/// gesture per press, whatever a script does with the values), so a second
/// press with the trigger never released is correctly no press at all. A
/// controller does the same thing; a test that forgot it would be asserting
/// against a held button.
function clickSelect(o) {
    var up = {}, down = {};
    for (var k in o) { up[k] = o[k]; down[k] = o[k]; }
    up.select = 0;
    down.select = 1;
    sendHand(up);
    sendHand(down);
}

project.create("vr input " + Date.now());

// TWO CUBES, at known places, and the ray comes at them down -Z from in front
// — which is the unrotated aim pose's own direction. A primitive Cube is TWO
// units across (half-extent 1, measured), so its +Z face is at z = centre + 1
// and every distance below is derived from that.
var cubeA = scene.addPrimitive("Cube");
var cubeB = scene.addPrimitive("Cube");
node.transform(cubeA, { position: { x: 0, y: 1, z: 0 } });
node.transform(cubeB, { position: { x: 3, y: 1, z: 0 } });
// A frame so the engine has the two items and their bounds: the document's
// picker uses Ogre's ray query as its broad phase.
editor.frame(2);

// ---- 0. NOTHING IS REPORTING YET ----------------------------------------

var st0 = vr.inputState();
console.log("vr.inputState (nothing injected): " + JSON.stringify(st0));
assert(st0.hands.length === 2, "vr.inputState() always answers two hands");
assert(st0.hands[0].hand === "left" && st0.hands[1].hand === "right",
       "...index 0 is the left hand and index 1 the right");
assert(st0.hands[1].valid === false, "with nothing injected no hand is located");
assert(st0.session === false, "and there is no VR session in this process");
assert(vr.hover() === null, "vr.hover() is null when no controller reports");
assert(vr.select() === false, "vr.select() refuses when no controller reports");
assert(vr.grab() === false, "vr.grab() refuses too");
assert(vr.release() === false, "...and so does vr.release() with no gesture running");

var mode0 = vr.interactionMode();
console.log("vr.interactionMode: " + JSON.stringify(mode0));
assert(mode0.dominant === "right", "the right hand is dominant by default (owner answer 3)");
assert(mode0.turn === "snap", "snap turn is the default (owner answer 2)");
assert(near(mode0.snapTurnDegrees, 30.0), "...at 30 degrees a flick");
assert(mode0.grabbing === false && mode0.hovering === false, "nothing is held and nothing hovered");

// ---- 1. THE RAY AND THE HOVER -------------------------------------------

sendHand({ x: 0, y: 1, z: 3 });     // 3 m in front of cube A, looking down -Z
var h = vr.hover();
console.log("vr.hover: " + JSON.stringify(h));
assert(h !== null, "a ray down -Z from 3 m away finds cube A");
assert(h.id === cubeA, "...and names it");
assert(h.hand === "right", "the DOMINANT hand is the one that points");
assert(near(h.distance, 2.0, 0.05), "the hit is 2 m away (3 m minus the cube's 1 m half-extent): "
       + h.distance.toFixed(3));
assert(near(h.x, 0, 0.01) && near(h.y, 1, 0.01) && near(h.z, 1.0, 0.02),
       "the hit point is on the cube's +Z face at z = 1: (" + h.x.toFixed(2) + ", " + h.y.toFixed(2)
       + ", " + h.z.toFixed(2) + ")");
assert(h.triangleIndex >= 0, "and it carries the TRIANGLE it struck (" + h.triangleIndex + ")");

var stInj = vr.inputState();
assert(stInj.source === "injection", "the interaction says where its input came from");
assert(stInj.hands[1].fromInjection === true,
       "...and so does the hand itself — a real smoke can never read a stale injection");
assert(stInj.hands[1].valid === true, "the injected hand is located");
assert(near(stInj.hands[1].aim.z, 3.0), "with the pose it was given");

// A ray into the sky hits nothing.
sendHand({ x: 0, y: 1, z: 3, pitch: 80 });
assert(vr.hover() === null, "a ray into the sky hovers nothing");

// ---- 2. THE LOCK IS `pickable`, AND THE VR RAY OBEYS IT -----------------
//
// The default scene's Ground ships LOCKED (services/defaultfloor.cpp:
// setPickable(false)) — the owner's own model, "the floor is just locked by
// default". Proven here with the document's own raycast so the case cannot
// silently pass on a scene that has no floor at all.
var floorAny = scene.raycast({ x: 8, y: 3, z: 8 }, { x: 0, y: -1, z: 0 },
                             { includeUnpickable: true });
var floorPickable = scene.raycast({ x: 8, y: 3, z: 8 }, { x: 0, y: -1, z: 0 });
console.log("straight down at (8, 8): " + floorAny.length + " node(s), "
            + floorPickable.length + " of them pickable");
if (floorAny.length > 0 && floorPickable.length === 0) {
    sendHand({ x: 8, y: 3, z: 8, pitch: -90 });
    assert(vr.hover() === null,
           "a ray on a LOCKED node (" + floorAny[0].name + ") hovers NOTHING — the lock IS "
           + "pickable, exactly as a desktop click reads it");
    // ...AND A PRESS ON IT IS A PRESS ON NOTHING, which DESELECTS — the desktop
    // rule, not a special case for locked things (VR_INPUT_SPEC §4).
    editor.select(cubeB);
    assert(editor.selection() === cubeB, "with cube B selected");
    assert(vr.select() === true, "a press on the locked floor is a press on NOTHING...");
    assert(editor.selectionSet().length === 0,
           "...so it DESELECTS, exactly as a desktop click on the floor does");
} else {
    console.log("note: this scene has no locked floor under (8, 8) — lock case not exercised");
}

// ---- 3. SELECT, TOGGLE, DESELECT ----------------------------------------

sendHand({ x: 0, y: 1, z: 3 });
assert(vr.select() === true, "a press on cube A selects it");
assert(editor.selection() === cubeA, "editor.selection() is cube A — the editor's own service");
assert(editor.selectionSet().length === 1, "and the set holds one node");

// The MENU BUTTON IS THE CTRL OF VR (owner answer 10).
clickSelect({ x: 3, y: 1, z: 3, menu: true });
var set = editor.selectionSet();
console.log("after menu+press on cube B: " + JSON.stringify(set));
assert(set.length === 2, "menu held turns the press into a TOGGLE: two nodes selected");
assert(set.indexOf(cubeA) >= 0 && set.indexOf(cubeB) >= 0, "...both cubes");
assert(vr.interactionMode().selects >= 2, "the interaction counted both selections");

// A MODIFIED press on empty space KEEPS the set (a slightly missed toggle in a
// headset must not throw a whole selection away).
clickSelect({ x: 0, y: 1, z: 3, pitch: 80, menu: true });
assert(editor.selectionSet().length === 2, "a menu-press on empty space keeps the set");

// A PLAIN press on empty space deselects.
clickSelect({ x: 0, y: 1, z: 3, pitch: 80 });
assert(editor.selectionSet().length === 0, "a plain press on empty space deselects everything");

// AND A PRESS IS AN EDGE, not a held value: pressing again without letting go
// does nothing at all (which is also why every case above releases first).
editor.select(cubeA);
sendHand({ x: 3, y: 1, z: 3, select: 1 });
assert(editor.selection() === cubeA,
       "a trigger that was already down is not a new press — the selection is untouched");

// ---- 4. THE NEAR GRAB ---------------------------------------------------
//
// 1.7 m in front of the cube's centre is 0.7 m from its face — inside arm's
// reach (0.9 m), so this is a NEAR grab and the object rides the GRIP pose.
var pushes = editor.undoState().pushes;
sendHand({ x: 0, y: 1, z: 1.7 });
var hNear = vr.hover();
assert(hNear !== null && hNear.distance < 0.9, "the ray is inside arm's reach ("
       + hNear.distance.toFixed(3) + " m)");
sendHand({ x: 0, y: 1, z: 1.7, grab: 1 });
var g = vr.interactionMode();
console.log("grabbing: " + JSON.stringify(g));
assert(g.grabbing === true, "a squeeze takes hold");
assert(g.far === false, "...as a NEAR grab");
assert(g.nodes === 1, "carrying one object");
assert(editor.selection() === cubeA, "a grab on something unselected SELECTS it first");

// The hand moves half a metre right and a quarter up.
sendHand({ x: 0.5, y: 1.25, z: 1.7, grab: 1 });
showPos("cube A while held", cubeA);
var held = posOf(cubeA);
assert(near(held.x, 0.5, 1e-3) && near(held.y, 1.25, 1e-3) && near(held.z, 0, 1e-3),
       "the object rode the hand exactly: a (0.5, 0.25, 0) hand move moved it the same");

// Release: ONE undo step.
sendHand({ x: 0.5, y: 1.25, z: 1.7, grab: 0 });
assert(vr.interactionMode().grabbing === false, "the release ended the gesture");
assert(editor.undoState().pushes === pushes + 1,
       "...and recorded EXACTLY ONE undo step for the whole gesture");
assert(editor.undoState().macroOpen === true,
       "(inside a script run the stack's macro is open, so undo cannot reach it from here)");
var after = posOf(cubeA);
assert(near(after.x, 0.5, 1e-3) && near(after.y, 1.25, 1e-3),
       "and the object STAYED where the hand left it after the commit");

// ---- 5. THE FAR GRAB, AND THE STICK ALONG THE RAY -----------------------

node.transform(cubeA, { position: { x: 0, y: 1, z: 0 } });     // back to a round number
editor.frame(1);
// THE COUNTER IS READ AFTER THE SETUP: node.transform is itself an undoable
// write, so a `pushes` taken before it would be counting the fixture.
pushes = editor.undoState().pushes;
sendHand({ x: 0, y: 1, z: 4 });
var hFar = vr.hover();
assert(hFar !== null && hFar.distance > 0.9, "4 m away the same ray is a FAR hit ("
       + hFar.distance.toFixed(3) + " m)");
sendHand({ x: 0, y: 1, z: 4, grab: 1 });
var gFar = vr.interactionMode();
assert(gFar.grabbing === true && gFar.far === true, "a squeeze at range is a FAR grab");
assert(near(gFar.distance, hFar.distance, 0.05), "held at the distance it was grabbed at ("
       + gFar.distance.toFixed(3) + " m)");

// Twenty frames of full stick forward: the distance is MULTIPLIED, so the
// object pushes away. 3.0 * e^(1.5 * 20/90) = 4.19 m.
for (var i = 0; i < 20; ++i) sendHand({ x: 0, y: 1, z: 4, grab: 1, stickY: 1 });
var gPush = vr.interactionMode();
console.log("after 20 frames of push: distance " + gPush.distance.toFixed(3));
assert(gPush.distance > hFar.distance + 0.4,
       "the stick PUSHED it away along the ray (" + hFar.distance.toFixed(3) + " -> "
       + gPush.distance.toFixed(3) + " m)");
showPos("cube A pushed away", cubeA);
var pushed = posOf(cubeA);
assert(pushed.z < -0.2, "and the object went with it, down the ray (z = " + pushed.z.toFixed(3)
       + ")");
assert(near(pushed.x, 0, 0.05) && near(pushed.y, 1, 0.05),
       "...staying ON the ray (x and y unmoved)");

// Pull it back in, then release.
for (var j = 0; j < 20; ++j) sendHand({ x: 0, y: 1, z: 4, grab: 1, stickY: -1 });
assert(vr.interactionMode().distance < gPush.distance,
       "and the stick the other way PULLS it back in");
sendHand({ x: 0, y: 1, z: 4, grab: 0 });
assert(editor.undoState().pushes === pushes + 1, "the far gesture is also ONE undo step");

// ---- 6. SNAPPING UNDER THE MENU BUTTON ----------------------------------
//
// The DELTA is quantised, never the absolute — so an object authored off-grid
// stays off-grid and moves in whole steps.
assert(near(editor.setSnapSize({ translate: 2 }).translate, 2.0),
       "the editor's translate snap is 2 units for this case");
node.transform(cubeA, { position: { x: 0.37, y: 1, z: 0 } });
editor.frame(1);
sendHand({ x: 0, y: 1, z: 1.7, menu: true });
sendHand({ x: 0, y: 1, z: 1.7, menu: true, grab: 1 });
assert(vr.interactionMode().snapping === true, "menu held during a grab SNAPS it");
sendHand({ x: 0.7, y: 1, z: 1.7, menu: true, grab: 1 });
showPos("cube A after a 0.7 move on a 2-unit grid", cubeA);
assert(near(posOf(cubeA).x, 0.37, 1e-3),
       "a 0.70 move on a 2-unit grid snaps to zero: the object does not budge, and it KEEPS "
       + "its off-grid 0.37");
sendHand({ x: 1.3, y: 1, z: 1.7, menu: true, grab: 1 });
showPos("cube A after a 1.3 move", cubeA);
assert(near(posOf(cubeA).x, 2.37, 1e-3),
       "a 1.30 move snaps to one whole step: 0.37 -> 2.37, never to 2.00");
sendHand({ x: 1.3, y: 1, z: 1.7, menu: false, grab: 0 });
editor.setSnapSize({ translate: 1 });

// ---- 7. A GESTURE THAT LOSES ITS INPUT IS CANCELLED, NOT COMMITTED ------

node.transform(cubeA, { position: { x: 0, y: 1, z: 0 } });
editor.frame(1);
pushes = editor.undoState().pushes;
sendHand({ x: 0, y: 1, z: 1.7 });
sendHand({ x: 0, y: 1, z: 1.7, grab: 1 });
sendHand({ x: 1.5, y: 1, z: 1.7, grab: 1 });
assert(near(posOf(cubeA).x, 1.5, 1e-3), "the object is 1.5 m out, mid-gesture");
var cancels = vr.interactionMode().cancels;
assert(vr.inputInject({ hand: "right", focused: false }) === true,
       "the runtime takes the input away (a dashboard over the session)");
var gCancel = vr.interactionMode();
assert(gCancel.grabbing === false, "the gesture is over");
assert(gCancel.cancels === cancels + 1, "...and it was CANCELLED, not released");
showPos("cube A after the cancel", cubeA);
assert(near(posOf(cubeA).x, 0, 1e-3),
       "the object went back exactly where it was found");
assert(editor.undoState().pushes === pushes,
       "and NOTHING was pushed — a cancelled gesture is not an undo step");
assert(vr.inputInject({ hand: "right", focused: true }) === true, "the input comes back");

// ---- 8. A TWO-OBJECT GESTURE IS ONE MACRO OF TWO COMMANDS --------------

node.transform(cubeA, { position: { x: 0, y: 1, z: 0 } });
node.transform(cubeB, { position: { x: 3, y: 1, z: 0 } });
editor.frame(1);
pushes = editor.undoState().pushes;
clickSelect({ x: 0, y: 1, z: 3 });
clickSelect({ x: 3, y: 1, z: 3, menu: true });
assert(editor.selectionSet().length === 2, "both cubes selected again");
sendHand({ x: 3, y: 1, z: 1.7 });
sendHand({ x: 3, y: 1, z: 1.7, grab: 1 });
assert(vr.interactionMode().nodes === 2, "a grab carries the WHOLE selection");
sendHand({ x: 3, y: 2, z: 1.7, grab: 1 });
assert(near(posOf(cubeA).y, 2, 1e-3) && near(posOf(cubeB).y, 2, 1e-3),
       "both objects rode the hand a metre up");
sendHand({ x: 3, y: 2, z: 1.7, grab: 0 });
assert(editor.undoState().pushes === pushes + 2,
       "two objects is TWO commands — inside ONE macro (the gizmo's own shape)");

// ---- 9. THE VERBS DRIVE THE SAME CODE THE BUTTONS DO -------------------
//
// The API-first rule, asserted: these are not a second implementation, they are
// the entry points the trigger and the squeeze call.
node.transform(cubeA, { position: { x: 0, y: 1, z: 0 } });
editor.frame(1);
editor.selectNone();
pushes = editor.undoState().pushes;
sendHand({ x: 0, y: 1, z: 1.7 });                       // no buttons at all
assert(vr.select({ mode: "replace" }) === true, "vr.select() selects what the ray is on");
assert(editor.selection() === cubeA, "...cube A");
assert(vr.grab() === true, "vr.grab() takes hold");
assert(vr.interactionMode().grabbing === true, "and the gesture is live");
sendHand({ x: 0, y: 1.4, z: 1.7 });                     // move the hand, no buttons
assert(near(posOf(cubeA).y, 1.4, 1e-3), "the object follows the hand with no button held");
assert(vr.release() === true, "vr.release() commits");
assert(editor.undoState().pushes === pushes + 1, "...one undo step, like a squeeze");
assert(vr.release() === false, "a second release refuses");

// ---- 10. LOCOMOTION: THE OPTIONS, AND THE REFUSAL WITH NO RIG ----------

var loco = vr.locomotion();
console.log("vr.locomotion: " + JSON.stringify(loco));
assert(loco.turn === "snap" && loco.dominant === "right", "the defaults, read back");
var set2 = vr.locomotion({ turn: "smooth", snapTurnDegrees: 45 });
assert(set2.turn === "smooth" && near(set2.snapTurnDegrees, 45), "and they can be set");
assert(vr.locomotion().turn === "smooth", "the setting stuck");
vr.locomotion({ turn: "snap", snapTurnDegrees: 30 });

// THE SWAP SWAPS BOTH ROLES (owner answer 3): one flag, never two.
assert(vr.locomotion({ dominant: "left" }).dominant === "left", "the dominant hand can swap");
sendHand({ hand: "right", x: 0, y: 1, z: 1.7 });
assert(vr.hover() === null,
       "with the LEFT hand dominant, the right hand's ray points at nothing the editor reads");
sendHand({ hand: "left", x: 0, y: 1, z: 1.7 });
var hLeft = vr.hover();
assert(hLeft !== null && hLeft.hand === "left", "...and the left hand is the one that points");
assert(hLeft.id === cubeA, "at cube A");
vr.locomotion({ dominant: "right" });

// WITH NO SESSION THERE IS NO RIG (the engine's origin is a no-op without one),
// so the stick walks nobody and the verb refuses in words.
var turnsBefore = vr.interactionMode().turns;
sendHand({ hand: "left", x: 0, y: 1, z: 1.7, stickY: 1, stickX: 1 });
assert(vr.interactionMode().turns === turnsBefore,
       "a full stick with no session turns nobody (there is no rig to turn)");
assert(vr.move({ forward: true }) === false, "vr.move() refuses with nobody in VR");
assert(app.lastError().indexOf("nobody is in VR") >= 0,
       "...and says so in words: " + app.lastError());

// ---- 11. MALFORMED CALLS THROW; ANSWERS DO NOT -------------------------

throws(function () { vr.inputInject({ hand: "middle" }); }, "middle",
       "an unknown hand throws, naming it");
throws(function () { vr.inputInject({ hand: "right", nosuch: 1 }); }, "nosuch",
       "an unknown inject key throws, naming it");
throws(function () { vr.inputInject({ hand: "right", aim: { nosuchpose: 1 } }); }, "nosuchpose",
       "an unknown POSE key throws too");
throws(function () { vr.select({ mode: "sideways" }); }, "sideways",
       "an unknown select mode throws");
throws(function () { vr.grab({ nosuch: 1 }); }, "nosuch", "an unknown grab key throws");
throws(function () { vr.locomotion({ turn: "spinny" }); }, "spinny",
       "an unknown turn mode throws");
throws(function () { vr.locomotion({ snapTurnDegrees: 0 }); }, "snapTurnDegrees",
       "a snap turn of zero degrees is refused");

// ---- 12. AND THE EDITOR IS EXACTLY AN EDITOR AFTERWARDS ---------------

editor.frame(2);
assert(vr.state().active === false, "no session was ever started by any of this");
assert(vr.interactionMode().installed === true,
       "the interaction is installed (the injection armed it) and stepping");
assert(scene.nodes().length >= 2, "the scene still has its cubes");

console.log("scripting.e2e.vr_input_headless: PASS");
