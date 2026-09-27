// scripting.e2e.vr_input_headless — EVERY VR GESTURE, WITH NO HEADSET
// (SPECS/VR_INPUT_SPEC.md stage 1 §10).
//
// THE POINT OF THE INJECTION ROUTE, demonstrated: this process was launched
// WITHOUT --vr, so no OpenXR loader was ever opened and no session exists — and
// every controller gesture still runs, end to end, through the same code a
// wearer drives. `vr.inject` writes the per-hand state the runtime's action
// system would have written — into THE ENGINE's own store, the one a session
// fills, which is why there is no second store to disagree with it — and
// `vr.step()` runs one interaction frame on it. The interaction cannot tell the
// difference: the aim ray, the document pick, the selection, the rigid grab,
// the snap and the undo macro are all the real ones.
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

/// ONE FRAME OF CONTROLLER INPUT: `vr.inject` says what the hand is doing and
/// `vr.step()` does it. Every field is passed EVERY time — `vr.inject` REPLACES
/// a hand's whole sample (the engine's contract: poses included, from the call
/// until the next one), so there is nothing to inherit and nothing to forget.
function sendHand(o) {
    var pose = { x: o.x, y: o.y, z: o.z, yaw: o.yaw || 0, pitch: o.pitch || 0 };
    var m = { valid: true, aim: pose, grip: pose,
              select: o.select || 0, grab: o.grab || 0, menuPressed: !!o.menu,
              stick: { x: o.stickX || 0, y: o.stickY || 0 },
              // FOCUS IS A PROPERTY OF THE SESSION, carried on every sample: a
              // runtime that loses it reports `focused:false` on BOTH hands, so
              // a test that means "the dashboard came up" says it here.
              focused: o.focused === undefined ? true : !!o.focused };
    var hand = o.hand || "right";
    assert(vr.inject(hand, m) === true && vr.step() === true,
           "one frame: " + hand + " at (" + o.x + ", " + o.y + ", " + o.z + ") yaw "
           + (o.yaw || 0) + " pitch " + (o.pitch || 0)
           + (o.select ? " select" : "") + (o.grab ? " grab" : "") + (o.menu ? " menu" : "")
           + (o.focused === false ? " UNFOCUSED" : ""));
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

/// BOTH HANDS IN ONE FRAME. `vr.inject` REPLACES a hand's whole sample and
/// leaves it standing until the next call, so a two-hand frame is two
/// injections and then ONE step — the step last, or the interaction would run a
/// frame on half a pair (which is exactly what it is written to survive: a hand
/// that did not report holds the object still).
function sendPair(l, r) {
    var mk = function (o) {
        var pose = { x: o.x, y: o.y, z: o.z, yaw: o.yaw || 0, pitch: o.pitch || 0 };
        return { valid: true, aim: pose, grip: pose, select: o.select || 0,
                 grab: o.grab || 0, menuPressed: !!o.menu,
                 stick: { x: o.stickX || 0, y: o.stickY || 0 }, focused: true };
    };
    assert(vr.inject("left", mk(l)) === true && vr.inject("right", mk(r)) === true
           && vr.step() === true,
           "one two-hand frame: left (" + l.x + ", " + l.y + ", " + l.z + ")"
           + (l.grab ? " grab" : "") + ", right (" + r.x + ", " + r.y + ", " + r.z + ")"
           + (r.grab ? " grab" : "") + (r.menu || l.menu ? " menu" : ""));
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
// ASSERTED, NOT SKIPPED (the Fable read of 1S, finding 5): this used to be an
// `if` with a `note:` in its else, so a project template that stopped shipping
// a locked floor would have retired the whole case in silence. The floor IS the
// fixture — every new project has one and it ships locked — so a scene without
// it is a defect in the template and this line is where it surfaces.
assert(floorAny.length > 0,
       "a new project's scene has a floor under (8, 8) (" + floorAny.length + " node(s))");
assert(floorPickable.length === 0,
       "...and it ships LOCKED, so the document's own raycast refuses it");
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

// TWENTY FRAMES OF FULL STICK FORWARD, and the whole gesture computed here
// rather than bounded (the Fable read of 1S, finding 5: this used to assert
// "more than 3.4 m" for a number the arithmetic pins to four figures).
//
// THE DISTANCE is exact: the stick MULTIPLIES it (d *= e^(k·y·dt), the same
// gesture at every scale), so twenty frames of full forward at 1/90 s a frame
// and k = 1.5 is d0 · e^(1.5·20/90).
//
// THE OBJECT'S POSITION IS THE SAME ARITHMETIC THROUGH ONE MORE STEP: the
// virtual far hand is LOW-PASSED, deliberately and by an amount that grows with
// the distance (kLeverTauPerMetre = 0.02 s per metre beyond arm's reach —
// at range the lever multiplies the wrist's own tremor), so the object LAGS the
// distance the gesture is holding. The lag is not a tolerance to hide in: the
// one-pole recursion is replayed below, frame by frame, and the object is
// asserted against it to the millimetre.
var dt = 1 / 90;
var expect = hFar.distance;                     // the distance the gesture holds
var handNow = 4 - hFar.distance;                // z of the virtual hand at the grab
var handStart = handNow;
for (var i = 0; i < 20; ++i) {
    sendHand({ x: 0, y: 1, z: 4, grab: 1, stickY: 1 });
    expect *= Math.exp(1.5 * dt);
    var tau = Math.max(0, expect - 0.9) * 0.02;     // vrgrab::leverTau
    var alpha = tau <= 0 ? 1 : 1 - Math.exp(-dt / tau);
    handNow += ((4 - expect) - handNow) * alpha;    // smoothedTowards, one axis
}
var gPush = vr.interactionMode();
console.log("after 20 frames of push: distance " + gPush.distance.toFixed(4)
            + " (arithmetic: " + expect.toFixed(4) + "), the filtered hand at z "
            + handNow.toFixed(4));
assert(near(gPush.distance, expect, 0.01),
       "the stick PUSHED it away along the ray, by exactly e^(1.5·20/90): "
       + hFar.distance.toFixed(3) + " -> " + gPush.distance.toFixed(3) + " m");
assert(near(gPush.distance, 4.187, 0.01),
       "...which is 4.187 m for this 3.000 m grab, to the centimetre");
showPos("cube A pushed away", cubeA);
var pushed = posOf(cubeA);
assert(near(pushed.z, handNow - handStart, 2e-3),
       "and the object went with the FILTERED hand, to the millimetre: z "
       + pushed.z.toFixed(4) + " against the replayed one-pole " + (handNow - handStart).toFixed(4)
       + " (it lags the 4.187 m the gesture holds, which is what the lever filter is for)");
assert(near(pushed.x, 0, 1e-4) && near(pushed.y, 1, 1e-4),
       "...staying ON the ray to a tenth of a millimetre (x and y unmoved)");

// Pull it back in.
for (var j = 0; j < 20; ++j) sendHand({ x: 0, y: 1, z: 4, grab: 1, stickY: -1 });
assert(vr.interactionMode().distance < gPush.distance,
       "and the stick the other way PULLS it back in");

// ---- 5b. THE TURNTABLE TURNS THE HELD OBJECT THE WAY THE STICK POINTS ---
//
// The dominant stick's X spins a far-held object about the world's up — the one
// rotation a far grab cannot do with the wrist. THE SIGN IS THE SUBJECT (the
// Fable read of stage 1, finding 6): the tree's yaw is the right-handed
// rotation about +Y, under which a positive angle is counter-clockwise seen
// from above, so a flick RIGHT has to arrive at the maths negated — otherwise
// the one stick turns the wearer clockwise and the thing in their hand
// anti-clockwise. Ten frames of full right at 90 deg/s is -10 degrees of yaw.
var spinBefore = node.transform(cubeA).rotation.y;
for (var t = 0; t < 10; ++t) sendHand({ x: 0, y: 1, z: 4, grab: 1, stickX: 1 });
var spinAfter = node.transform(cubeA).rotation.y;
console.log("      ten frames of full stick right: yaw " + spinBefore.toFixed(3) + " -> "
            + spinAfter.toFixed(3));
assert(near(spinAfter, spinBefore - 10.0, 0.2),
       "stick RIGHT spins the held object CLOCKWISE from above, by 90 deg/s: "
       + spinBefore.toFixed(2) + " -> " + spinAfter.toFixed(2)
       + " (the same direction the same stick turns the wearer)");
for (var t2 = 0; t2 < 10; ++t2) sendHand({ x: 0, y: 1, z: 4, grab: 1, stickX: -1 });
assert(near(node.transform(cubeA).rotation.y, spinBefore, 0.2),
       "...and the other way brings it back");

// Release.
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
// THE RUNTIME TAKES THE INPUT AWAY (a dashboard over the session): the sample
// says `focused:false`, which is exactly what a runtime outside its Focused
// state reports. The squeeze is still "held" in the sample and it makes no
// difference — the focus is read BEFORE any press edge, so the gesture is
// cancelled rather than released.
sendHand({ x: 1.5, y: 1, z: 1.7, grab: 1, focused: false });
var gCancel = vr.interactionMode();
assert(gCancel.grabbing === false, "the gesture is over");
assert(gCancel.cancels === cancels + 1, "...and it was CANCELLED, not released");
showPos("cube A after the cancel", cubeA);
assert(near(posOf(cubeA).x, 0, 1e-3),
       "the object went back exactly where it was found");
assert(editor.undoState().pushes === pushes,
       "and NOTHING was pushed — a cancelled gesture is not an undo step");
sendHand({ x: 1.5, y: 1, z: 1.7 });     // the input comes back, nothing held

// ---- 8. A TWO-OBJECT GESTURE IS ONE MACRO OF TWO COMMANDS --------------

node.transform(cubeA, { position: { x: 0, y: 1, z: 0 } });
node.transform(cubeB, { position: { x: 3, y: 1, z: 0 } });
editor.frame(1);
// NOTHING SELECTED FIRST, since the VR GIZMO (phase 4b stage 2): a trigger
// press with the ray on a gizmo HANDLE drags that handle instead of selecting,
// which is the desktop's own precedence (the gizmo's hit test runs before the
// pick, enginesceneviewport.cpp). Cube A is still selected here from the
// section above, so its gizmo's centre ball sits exactly where this press aims
// — and the press would grab the ball, the release would record its own undo
// entry, and this case, which is about the GRAB's undo shape, would be
// counting the gizmo's. The gesture the gizmo answers is `scripting.e2e.vr_gizmo`.
editor.selectNone();
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


// ---- 9b. TWO HANDS ON ONE OBJECT (owner answer 7, with roll) -----------
//
// THE UPGRADE, NOT A SECOND GESTURE. A squeeze with the other hand while
// something is held is read as "put my other hand on it": the SAME gesture
// continues, the pair is captured, and the object does not move by so much as a
// millimetre on the frame the count changes — which is the whole of the "no
// jump" rule and the reason both transitions re-capture instead of computing a
// correction. The arithmetic under all of it is `vr.grab_maths` (the span as
// the scale, the axis' minimal rotation, the wrists' average roll, the
// hand-off's bit-for-bit hold); what this asserts is that the gesture, the
// document and the undo stack agree with it.

node.transform(cubeA, { position: { x: 0, y: 1, z: 0 }, scale: { x: 1, y: 1, z: 1 } });
editor.frame(1);
editor.selectNone();
pushes = editor.undoState().pushes;

// The right hand takes hold at 0.7 m — inside arm's reach, so a NEAR grab and
// the object rides the grips, which is what a two-hand gesture is made of.
sendHand({ x: 0.25, y: 1, z: 1.7 });
sendHand({ x: 0.25, y: 1, z: 1.7, grab: 1 });
assert(vr.interactionMode().grabbing === true, "the right hand has it");
assert(vr.interactionMode().far === false, "...as a near grab");

// THE LEFT HAND JOINS. Hands at x = -0.25 and +0.25: a 0.5 m span about
// (0, 1, 1.7).
sendPair({ x: -0.25, y: 1, z: 1.7, grab: 1 }, { x: 0.25, y: 1, z: 1.7, grab: 1 });
var two = vr.interactionMode();
console.log("two-handed: " + JSON.stringify({ two: two.twoHanded, scale: two.scale,
                                              roll: two.rollDegrees, nodes: two.nodes,
                                              count: two.twoHands }));
assert(two.twoHanded === true, "the off hand's squeeze UPGRADED the gesture");
assert(two.grabbing === true, "...it is still ONE gesture");
assert(two.twoHands === 1, "and the upgrade was counted once");
assert(near(two.scale, 1.0, 1e-5), "the pair starts at scale 1");
showPos("cube A on the frame the second hand arrived", cubeA);
var atUpgrade = posOf(cubeA);
assert(near(atUpgrade.x, 0, 1e-4) && near(atUpgrade.y, 1, 1e-4) && near(atUpgrade.z, 0, 1e-4),
       "and the object did not move at all when the hand count changed");

// SPREAD THE PALMS TO A METRE: the span doubles, so the object doubles in size
// and its offset from the midpoint AT THE CAPTURE doubles with it — it was
// 1.7 m in front of the hands, so it goes to 3.4 m in front of them.
sendPair({ x: -0.5, y: 1, z: 1.7, grab: 1 }, { x: 0.5, y: 1, z: 1.7, grab: 1 });
var spread = vr.interactionMode();
var sized = node.transform(cubeA);
console.log("      after the spread: scale " + spread.scale.toFixed(4) + ", the node's own "
            + JSON.stringify(sized.scale) + " at " + JSON.stringify(sized.position));
assert(near(spread.scale, 2.0, 1e-3), "0.5 m of span became 1.0 m: the factor is 2");
assert(near(sized.scale.x, 2, 1e-3) && near(sized.scale.y, 2, 1e-3)
       && near(sized.scale.z, 2, 1e-3),
       "the object is twice the size, UNIFORMLY (a pair of hands cannot say 'wider only')");
assert(near(sized.position.z, -1.7, 2e-3),
       "...and it scaled about the midpoint at the capture: 1.7 m in front became 3.4 m "
       + "(z = -1.7)");
assert(near(sized.position.x, 0, 1e-3) && near(sized.position.y, 1, 1e-3),
       "on the axis of the spread it stayed put (the midpoint did not move)");

// BACK TO ONE: the same span again is the same size again — the factor is
// measured against the CAPTURE, never accumulated, so a gesture that returns
// returns exactly.
sendPair({ x: -0.25, y: 1, z: 1.7, grab: 1 }, { x: 0.25, y: 1, z: 1.7, grab: 1 });
assert(near(node.transform(cubeA).scale.x, 1, 1e-3),
       "bringing the palms back to 0.5 m brings the size back to 1 exactly");
assert(near(posOf(cubeA).z, 0, 2e-3), "...and the object back to where it was");

// THE ROLL — the half a pair of POINTS cannot see (owner answer 7's "full
// version"). Neither palm moves, so the span, the midpoint and the axis are
// all unchanged and the minimal rotation is the identity; both wrists roll 30
// degrees about the line between them (+X, i.e. a pitch of the grips), and the
// object must roll 30 degrees about that line THROUGH the midpoint.
sendPair({ x: -0.25, y: 1, z: 1.7, grab: 1, pitch: 30 },
         { x: 0.25, y: 1, z: 1.7, grab: 1, pitch: 30 });
var rolled = vr.interactionMode();
var rt = node.transform(cubeA);
console.log("      after the 30 degree roll: roll " + rolled.rollDegrees.toFixed(3)
            + " degrees, the node at " + JSON.stringify(rt.position) + " rotation "
            + JSON.stringify(rt.rotation));
assert(near(rolled.rollDegrees, 30.0, 0.05),
       "both wrists rolling 30 degrees about the axis IS a 30 degree roll of the pair");
assert(near(rt.rotation.x, 30.0, 0.1), "the object rolled 30 degrees about that axis");
// The offset (0,0,-1.7) rolled about +X by 30 degrees: y' = -z sin30 = 0.85,
// z' = z cos30 = -1.4722, off the midpoint (0, 1, 1.7).
assert(near(rt.position.y, 1.85, 5e-3) && near(rt.position.z, 0.2278, 5e-3),
       "...about the MIDPOINT, not about itself: (0,1,0) -> (0,1.85,0.228)");
assert(near(rolled.scale, 1.0, 1e-3), "a roll is not a scale");

// ONE WRIST IS HALF A ROLL: the gesture belongs to the pair.
sendPair({ x: -0.25, y: 1, z: 1.7, grab: 1 },
         { x: 0.25, y: 1, z: 1.7, grab: 1, pitch: 30 });
assert(near(vr.interactionMode().rollDegrees, 15.0, 0.05),
       "one wrist rolled 30 with the other still is a 15 degree roll (the average)");

// MENU HELD SNAPS THE FACTOR by the editor's SCALE step — the factor, never
// the resulting size.
assert(near(editor.setSnapSize({ scale: 0.5 }).scale, 0.5),
       "the editor's scale snap is 0.5 for this case");
// A 20 % spread (0.5 -> 0.6 m) is a factor of 1.2, which on a half step is 1.
sendPair({ x: -0.3, y: 1, z: 1.7, grab: 1, menu: true },
         { x: 0.3, y: 1, z: 1.7, grab: 1, menu: true });
var snapped = vr.interactionMode();
console.log("      with menu held, a 1.2 factor reads " + snapped.scale.toFixed(4));
assert(snapped.snapping === true, "menu held snaps a two-hand gesture too");
assert(near(snapped.scale, 1.0, 1e-3), "a factor of 1.2 on a 0.5 step snaps to 1.0");
assert(near(node.transform(cubeA).scale.x, 1, 1e-3), "so the object's size does not budge");
// ...and a 60 % spread is 1.6, which snaps to 1.5.
sendPair({ x: -0.4, y: 1, z: 1.7, grab: 1, menu: true },
         { x: 0.4, y: 1, z: 1.7, grab: 1, menu: true });
assert(near(vr.interactionMode().scale, 1.5, 1e-3),
       "a factor of 1.6 snaps to 1.5 — one whole step, in the factor");
editor.setSnapSize({ scale: 0.25 });
sendPair({ x: -0.25, y: 1, z: 1.7, grab: 1 }, { x: 0.25, y: 1, z: 1.7, grab: 1 });

// ONE HAND LETS GO: the gesture CONTINUES on the other, from exactly where the
// pair left the object, and NOTHING is committed.
var before = posOf(cubeA);
var commits = vr.interactionMode().commits;
sendPair({ x: -0.25, y: 1, z: 1.7, grab: 0 }, { x: 0.25, y: 1, z: 1.7, grab: 1 });
var one = vr.interactionMode();
assert(one.twoHanded === false, "the left hand let go: back to one hand");
assert(one.grabbing === true, "...and the gesture is still live");
assert(one.commits === commits, "nothing was committed by a hand-off");
var afterHandoff = posOf(cubeA);
assert(near(afterHandoff.x, before.x, 1e-4) && near(afterHandoff.y, before.y, 1e-4)
       && near(afterHandoff.z, before.z, 1e-4),
       "and the object did not move on the hand-off either (a FRESH capture, no correction)");
assert(editor.undoState().pushes === pushes,
       "no undo step yet — the gesture has not ended");

// THE REMAINING HAND CARRIES ON: 20 cm up is 20 cm up, from where the pair
// left it.
sendPair({ x: -0.25, y: 1, z: 1.7, grab: 0 }, { x: 0.25, y: 1.2, z: 1.7, grab: 1 });
assert(near(posOf(cubeA).y, afterHandoff.y + 0.2, 2e-3),
       "the one-hand follow carries on from there: a 20 cm lift is a 20 cm lift");

// AND THE WHOLE THING IS ONE UNDO STEP — captured at the FIRST squeeze,
// whatever happened to the hand count in between.
sendPair({ x: -0.25, y: 1, z: 1.7, grab: 0 }, { x: 0.25, y: 1.2, z: 1.7, grab: 0 });
assert(vr.interactionMode().grabbing === false, "the last hand let go: the gesture is over");
assert(editor.undoState().pushes === pushes + 1,
       "ONE undo step for a gesture that changed hands twice and scaled and rolled");

// THE VERB DOES THE SAME THING (API-first): `vr.grab({hand})` on a live
// gesture upgrades it, and `vr.release({hand})` hands it back.
node.transform(cubeA, { position: { x: 0, y: 1, z: 0 }, scale: { x: 1, y: 1, z: 1 } });
editor.frame(1);
sendPair({ x: -0.25, y: 1, z: 1.7 }, { x: 0.25, y: 1, z: 1.7 });
assert(vr.grab({ hand: "right" }) === true, "vr.grab takes hold with the right hand");
assert(vr.grab({ hand: "left" }) === true, "...and vr.grab with the LEFT hand upgrades it");
assert(vr.interactionMode().twoHanded === true, "the verb's gesture is two-handed");
assert(vr.release({ hand: "left" }) === true, "vr.release with the left hand hands it back");
assert(vr.interactionMode().twoHanded === false
       && vr.interactionMode().grabbing === true, "...and the right hand still has it");
assert(vr.release({ hand: "right" }) === true, "the right hand's release commits");
assert(vr.interactionMode().grabbing === false, "and the gesture is over");

// A LIGHT HAS NO SIZE (§5.1): the pair turns and carries it, and the factor is
// 1 for it — even in a mixed selection.
var lamp = scene.addLight("Point");
node.transform(lamp, { position: { x: 0, y: 1, z: 0 } });
editor.frame(1);
editor.select(lamp);
sendPair({ x: -0.25, y: 1, z: 1.7 }, { x: 0.25, y: 1, z: 1.7 });
assert(vr.grab({ hand: "right" }) === true, "a squeeze takes hold of the lamp");
assert(vr.grab({ hand: "left" }) === true, "...with both hands");
sendPair({ x: -0.5, y: 1, z: 1.7, grab: 1 }, { x: 0.5, y: 1, z: 1.7, grab: 1 });
var lampT = node.transform(lamp);
console.log("      the lamp after a doubling spread: scale " + JSON.stringify(lampT.scale)
            + " at " + JSON.stringify(lampT.position));
assert(near(lampT.scale.x, 1, 1e-3), "the lamp is NOT resized — a light has no size");
assert(near(lampT.position.z, 0, 2e-3),
       "...and it is not thrown away from the midpoint either (the factor is 1 for it)");
sendPair({ x: -0.5, y: 1, z: 1.7, grab: 0 }, { x: 0.5, y: 1, z: 1.7, grab: 0 });
node.remove(lamp);
editor.selectNone();


// ---- 9c. EITHER HAND'S RELEASE ENDS THE GESTURE IT OWNS ----------------
//         (the lead's read of the first round, item 2)
//
// THE ORDER THE FIRST ROUND COULD NOT SURVIVE: right presses, left joins,
// RIGHT lets go first. The object is handed to the left hand — and until this
// round nothing could put it down, because the dominant hand's edge needs the
// dominant hand's own transition and the off hand's edge asked for a PAIR. The
// object stayed welded to the left grip with no button pressed anywhere, and
// the stick, the gizmo and the gaze fly were all refused meanwhile because a
// gesture was live. Driven here with the BUTTONS, not the verbs, because the
// defect was in the edges.

node.transform(cubeA, { position: { x: 0, y: 1, z: 0 }, scale: { x: 1, y: 1, z: 1 } });
editor.frame(1);
editor.selectNone();
pushes = editor.undoState().pushes;

sendPair({ x: -0.25, y: 1, z: 1.7 }, { x: 0.25, y: 1, z: 1.7 });
sendPair({ x: -0.25, y: 1, z: 1.7 }, { x: 0.25, y: 1, z: 1.7, grab: 1 });   // R presses
var owned = vr.interactionMode();
assert(owned.grabbing === true && owned.hand === "right", "the RIGHT hand takes hold");
sendPair({ x: -0.25, y: 1, z: 1.7, grab: 1 }, { x: 0.25, y: 1, z: 1.7, grab: 1 });  // L joins
assert(vr.interactionMode().twoHanded === true, "...and the left hand joins it");
// THE DOMINANT HAND LETS GO FIRST.
sendPair({ x: -0.25, y: 1, z: 1.7, grab: 1 }, { x: 0.25, y: 1, z: 1.7, grab: 0 });
var handed = vr.interactionMode();
console.log("after the RIGHT hand let go first: " + JSON.stringify({
    grabbing: handed.grabbing, hand: handed.hand, two: handed.twoHanded }));
assert(handed.grabbing === true, "the gesture continues");
assert(handed.twoHanded === false, "...on one hand");
assert(handed.hand === "left", "...and that hand is the LEFT one, which is still squeezing");
assert(editor.undoState().pushes === pushes, "nothing committed by the hand-off");
// The left hand carries it 30 cm up, to prove it really is the one holding.
sendPair({ x: -0.25, y: 1.3, z: 1.7, grab: 1 }, { x: 0.25, y: 1, z: 1.7, grab: 0 });
assert(near(posOf(cubeA).y, 1.3, 2e-3), "the left hand carries it (a 30 cm lift is 30 cm)");
// AND THE LEFT HAND'S OWN RELEASE ENDS IT — the edge this round added.
sendPair({ x: -0.25, y: 1.3, z: 1.7, grab: 0 }, { x: 0.25, y: 1, z: 1.7, grab: 0 });
var done = vr.interactionMode();
assert(done.grabbing === false,
       "the OFF hand's release ends a gesture it owns (it could not, before this round)");
assert(done.hand === "", "nothing is held");
assert(editor.undoState().pushes === pushes + 1,
       "...and the whole thing is ONE undo step, committed by that release");
assert(near(posOf(cubeA).y, 1.3, 2e-3), "the object stayed where the left hand left it");

// AND THE REFUSALS TELL THE TRUTH IN THE TWO-HAND STATES (item 9): a third
// message each, because the old two both lied — "nothing to grab" when that
// hand was already holding it, and "no grab is running" while one plainly was.
sendPair({ x: -0.25, y: 1, z: 1.7 }, { x: 0.25, y: 1, z: 1.7 });
assert(vr.grab({ hand: "right" }) === true, "the right hand takes hold (by verb)");
assert(vr.grab({ hand: "right" }) === false, "a second grab with the SAME hand refuses...");
assert(app.lastError().indexOf("already holding") >= 0,
       "...saying that hand is already holding it: " + app.lastError());
assert(vr.grab({ hand: "left" }) === true, "the left hand joins");
assert(vr.grab({ hand: "left" }) === false, "a third grab refuses...");
assert(app.lastError().indexOf("no third hand") >= 0,
       "...saying both hands are already on it: " + app.lastError());
assert(vr.release({ hand: "left" }) === true, "the left hand hands it back");
assert(vr.release({ hand: "left" }) === false, "a second release with that hand refuses...");
assert(app.lastError().indexOf("not holding anything") >= 0
       && app.lastError().indexOf("right hand is") >= 0,
       "...naming the hand that IS holding it: " + app.lastError());
assert(vr.release({ hand: "right" }) === true, "and the right hand's release commits");

// ---- 9d. A HAND-OFF ASKS FOR THE POSE ITS ARRANGEMENT USES -------------
//         (the lead's read, item 6)
//
// A NEAR hold rides the GRIP and a FAR hold rides the AIM, and the first
// round's guard accepted either — so a near hand-off to a hand whose grip was
// not located captured the follow from a pose nobody located (the jump the
// guard exists to prevent) and a far one whose aim was not located welded a
// ten-metre object to the wrist. Injected here, because a controller that
// reports a pose for one and not the other is a runtime's business.

/// A hand with one of its two poses UNLOCATED. `vr.inject`'s pose reader takes
/// a `valid` key, which is exactly what a runtime says when it has a grip and
/// no aim (or the other way round).
function sendPairPartial(l, r) {
    var mk = function (o) {
        var pose = { x: o.x, y: o.y, z: o.z, yaw: o.yaw || 0, pitch: o.pitch || 0 };
        var m = { valid: true, select: 0, grab: o.grab || 0, menuPressed: !!o.menu,
                  stick: { x: 0, y: 0 }, focused: true };
        m.aim = o.noAim ? { valid: false } : pose;
        m.grip = o.noGrip ? { valid: false } : pose;
        return m;
    };
    assert(vr.inject("left", mk(l)) === true && vr.inject("right", mk(r)) === true
           && vr.step() === true,
           "one frame: left" + (l.noGrip ? " NO GRIP" : "") + (l.noAim ? " NO AIM" : "")
           + (l.grab ? " grab" : "") + ", right" + (r.noGrip ? " NO GRIP" : "")
           + (r.noAim ? " NO AIM" : "") + (r.grab ? " grab" : ""));
}

node.transform(cubeA, { position: { x: 0, y: 1, z: 0 }, scale: { x: 1, y: 1, z: 1 } });
editor.frame(1);
editor.selectNone();
// A NEAR two-hand hold, then the right hand lets go on a frame where the left
// hand has an AIM but no GRIP. The near follow needs the grip.
sendPair({ x: -0.25, y: 1, z: 1.7 }, { x: 0.25, y: 1, z: 1.7 });
assert(vr.grab({ hand: "right" }) === true && vr.grab({ hand: "left" }) === true,
       "both hands on the cube, near");
var atHandoff = posOf(cubeA);
sendPairPartial({ x: -0.25, y: 1, z: 1.7, grab: 1, noGrip: true },
                { x: 0.25, y: 1, z: 1.7, grab: 0 });
// THE RELEASE IS THE VERB HERE because the SETUP was the verb: the squeeze
// edges are transitions of the injected buttons, and these two hands never
// pressed anything (§9c drives the same hand-off through the buttons).
assert(vr.release({ hand: "right" }) === true, "the right hand lets go on that very frame");
var wait = vr.interactionMode();
assert(wait.grabbing === true && wait.hand === "left",
       "the gesture is handed to the left hand even though its GRIP is not located");
showPos("the cube while the hand-off waits", cubeA);
assert(near(posOf(cubeA).x, atHandoff.x, 1e-4) && near(posOf(cubeA).y, atHandoff.y, 1e-4)
       && near(posOf(cubeA).z, atHandoff.z, 1e-4),
       "...and the object does not move a micron while the capture waits for a pose");
// The grip comes back, half a metre away from where it last was: the capture is
// taken THERE, so this frame is still a hold.
sendPair({ x: -0.75, y: 1, z: 1.7, grab: 1 }, { x: 0.25, y: 1, z: 1.7, grab: 0 });
assert(near(posOf(cubeA).x, atHandoff.x, 2e-3),
       "the frame the grip returns on is a HOLD, not a jump of the 0.5 m it moved while away");
sendPair({ x: -0.55, y: 1, z: 1.7, grab: 1 }, { x: 0.25, y: 1, z: 1.7, grab: 0 });
assert(near(posOf(cubeA).x, atHandoff.x + 0.2, 3e-3),
       "...and the follow carries on from there (20 cm right is 20 cm)");
sendPair({ x: -0.55, y: 1, z: 1.7, grab: 0 }, { x: 0.25, y: 1, z: 1.7, grab: 0 });
assert(vr.interactionMode().grabbing === false, "put down");

// A FAR two-hand hold, then the same thing with the AIM missing: the far follow
// needs the aim, and a located GRIP is not a substitute (it would weld a
// four-metre object to the wrist).
node.transform(cubeA, { position: { x: 0, y: 1, z: 0 }, scale: { x: 1, y: 1, z: 1 } });
editor.frame(1);
sendPair({ x: -0.25, y: 1, z: 4 }, { x: 0.25, y: 1, z: 4 });
assert(vr.grab({ hand: "right" }) === true, "a FAR grab at 4 m");
assert(vr.interactionMode().far === true, "...and it is far");
assert(vr.grab({ hand: "left" }) === true, "both hands on it");
var atFarHandoff = posOf(cubeA);
sendPairPartial({ x: -0.25, y: 1, z: 4, grab: 1, noAim: true },
                { x: 0.25, y: 1, z: 4, grab: 0 });
assert(vr.release({ hand: "right" }) === true, "the right hand lets go on that very frame");
var farWait = vr.interactionMode();
assert(farWait.grabbing === true && farWait.hand === "left",
       "the far gesture is handed over with the left AIM unlocated");
showPos("the cube while the far hand-off waits", cubeA);
assert(near(posOf(cubeA).x, atFarHandoff.x, 1e-4)
       && near(posOf(cubeA).z, atFarHandoff.z, 1e-4),
       "...and it does not snap to the wrist (the grip is located, and is NOT the pose a far "
       + "hold rides)");
sendPair({ x: -0.25, y: 1, z: 4, grab: 1 }, { x: 0.25, y: 1, z: 4, grab: 0 });
assert(near(posOf(cubeA).z, atFarHandoff.z, 0.02),
       "the frame the aim returns on is a hold");
sendPair({ x: -0.25, y: 1, z: 4, grab: 0 }, { x: 0.25, y: 1, z: 4, grab: 0 });
assert(vr.interactionMode().grabbing === false, "put down");

// AND THE OFF HAND IS WITHDRAWN. An empty state is the "stop injecting"
// spelling, and it matters to the cases below: with the LEFT hand still
// reporting, swapping the dominant hand would find it aimed at a cube and the
// hover would answer for a hand this section put there.
assert(vr.inject("left") === true && vr.step() === true, "the left hand is withdrawn");
assert(vr.inputState().hands[0].valid === false, "...and reports nothing again");

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

throws(function () { vr.inject("middle", {}); }, "middle",
       "an unknown hand throws, naming it");
throws(function () { vr.inject("right", { nosuch: 1 }); }, "nosuch",
       "an unknown inject key throws, naming it");
throws(function () { vr.inject("right", { aim: { nosuchpose: 1 } }); }, "nosuchpose",
       "an unknown POSE key throws too — ONE pose reader for every vr verb, and it "
       + "validates (a pose silently read at the origin is the collapsed-camera defect)");
throws(function () { vr.step({ nosuch: 1 }); }, "nosuch", "an unknown step key throws");
throws(function () { vr.step({ frames: 0 }); }, "frames",
       "a step of no frames is refused, naming the key");
throws(function () { vr.select({ mode: "sideways" }); }, "sideways",
       "an unknown select mode throws");
throws(function () { vr.grab({ nosuch: 1 }); }, "nosuch", "an unknown grab key throws");
throws(function () { vr.teleport({ nosuch: 1 }); }, "nosuch",
       "an unknown teleport key throws, naming it");
throws(function () { vr.teleport({ to: { x: 0, q: 1 } }); }, "q",
       "...and an unknown key inside `to` throws too");
throws(function () { vr.teleport({ to: { x: 0 }, arm: true }); }, "not two",
       "and saying two of the forms at once is refused rather than guessed");
throws(function () { vr.locomotion({ turn: "spinny" }); }, "spinny",
       "an unknown turn mode throws");
throws(function () { vr.locomotion({ snapTurnDegrees: 0 }); }, "snapTurnDegrees",
       "a snap turn of zero degrees is refused");

// ---- 12. AND THE EDITOR IS EXACTLY AN EDITOR AFTERWARDS ---------------

editor.frame(2);
assert(vr.state().active === false, "no session was ever started by any of this");
assert(vr.interactionMode().installed === true,
       "the interaction is installed (the first vr.step() installed it, there being no session "
       + "to do it) and stepping");
assert(scene.nodes().length >= 2, "the scene still has its cubes");

console.log("scripting.e2e.vr_input_headless: PASS");
