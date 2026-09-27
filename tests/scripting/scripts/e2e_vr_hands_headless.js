// scripting.e2e.vr_hands — BARE HANDS, WITH NO HEADSET AND NO HANDS
// (SPECS/VR_INPUT_SPEC.md §7, phase 4b stage 3).
//
// WHY THIS FILE EXISTS AT ALL. Stage 3 puts the wearer's own fingers on the
// same actions their controllers press, and NOTHING on this box can report a
// finger: Monado's simulated rig has no hands (`hand_tracking_supported =
// false`, verified in the source of the installed build) and no buttons, and
// the owner's Quest Pro is the only hardware in the project that tracks one. So
// the injection route is not a convenience here, it is the whole gate: this
// process was launched WITHOUT --vr, no OpenXR loader was ever opened, and
// every rule stage 3 adds is driven through `vr.inject` — which writes THE
// ENGINE's own per-hand store, the one a session fills, so the interaction
// cannot tell a script's hand from a wearer's.
//
// WHAT IS ASSERTED HERE:
//   * a hand is a HAND because its PROFILE says so, and the profile is per hand;
//   * the MANIPULATION FRAME: a grab follows the pinch point, not the palm —
//     asserted by moving the two independently, which no controller can do;
//   * ARM'S REACH is measured from the hand, not along the aim ray;
//   * the SKELETON round-trips (`joints` in, `vr.handJoints` out);
//   * a hand RE-BOUND mid-gesture cancels that gesture and records nothing.
//
// WHAT IS NOT HERE, and where it is instead:
//   * THE PINCH HYSTERESIS (0.7 to press, 0.3 to release, against a trigger's
//     0.5/0.4) — `vr.grab_maths`, pure. It cannot be driven from here on
//     purpose: an INJECTED sample carries its own press (the struct is the whole
//     truth), and the hysteresis is what the engine applies to a RUNTIME's
//     analogue value. Asserting it here would be asserting the injection's own
//     0.5 default.
//   * THE DRAWING — `mirror.vr_proxies`, which hand-builds a live status and
//     asks the drawer. It cannot happen here either, and that is a fact rather
//     than a gap: the mirror builds NO VR furniture at all without an active
//     session (`setVrProxies(showProxies && st.active, …)`), so with no session
//     there is nothing in the scene to measure. `drawn` is asserted to be 0
//     below, in words, so that the two halves cannot both think the other has
//     it.
//   * THE BINDINGS PARSING against a real runtime — `vr.input_session` (Monado).
//   * THE PINCH THRESHOLDS' FEEL, and whether a skeleton drawn as segments
//     reads as a hand — only the owner's headset can judge those.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function near(a, b, eps) { return Math.abs(a - b) <= (eps === undefined ? 1e-3 : eps); }
function posOf(id) { return node.transform(id).position; }
/// A VERB THAT REFUSES A MALFORMED CALL THROWS (ApiModule::fail — a script
/// error with a line number), while one that refuses a legal call the engine
/// would not take answers false (ApiModule::refuse + app.lastError). Both
/// spellings appear below, each where it belongs.
function throws(fn, needle, msg) {
    try { fn(); } catch (e) {
        assert(String(e).indexOf(needle) >= 0, msg + " (" + e + ")");
        return;
    }
    throw new Error("assert failed: " + msg + " — it did not throw");
}

var kHandProfile = "/interaction_profiles/ext/hand_interaction_ext";
var kTouchProfile = "/interaction_profiles/oculus/touch_controller";

/// ONE FRAME OF A BARE HAND. Every field every time: `vr.inject` REPLACES the
/// hand's whole sample, so there is nothing to inherit and nothing to forget.
/// `aim` is where the hand POINTS (the runtime's own ray), `grip` is the PALM,
/// and `manip` is the PINCH POINT — three different places on a real hand, which
/// is the whole reason stage 3 exists.
function sendHand(o) {
    var m = { valid: true,
              aim: o.aim,
              grip: o.grip,
              profile: o.profile === undefined ? "hand_interaction" : o.profile,
              select: o.select || 0, grab: o.grab || 0, menuPressed: !!o.menu,
              focused: true };
    if (o.manip !== undefined) m.manip = o.manip;
    if (o.joints !== undefined) m.joints = o.joints;
    var hand = o.hand || "right";
    assert(vr.inject(hand, m) === true && vr.step() === true,
           "one frame: " + hand + " " + (m.profile || "(nothing bound)")
           + (o.manip ? " pinch at (" + o.manip.x + ", " + o.manip.y + ", " + o.manip.z + ")" : "")
           + (o.grab ? " grasp" : "") + (o.select ? " pinch-press" : ""));
}

/// A PLAUSIBLE TRACKED HAND: 26 poses in the extension's own order (palm, wrist,
/// then thumb, index, middle, ring and little from the knuckle out). The shape
/// does not have to be anatomical — what is under test is that 26 poses go in
/// and the same 26 come back out in the same order — but it is built as a real
/// hand would be so that a failure prints something a person can read.
function skeleton(origin) {
    var j = [];
    j.push({ x: origin.x, y: origin.y, z: origin.z });                    // 0 palm
    j.push({ x: origin.x, y: origin.y - 0.04, z: origin.z + 0.04 });      // 1 wrist
    var fingers = [
        { dx: -0.03, n: 4 },   // thumb: metacarpal, proximal, distal, tip
        { dx: -0.01, n: 5 },   // index
        { dx:  0.01, n: 5 },   // middle
        { dx:  0.03, n: 5 },   // ring
        { dx:  0.05, n: 5 }    // little
    ];
    for (var f = 0; f < fingers.length; ++f) {
        for (var k = 0; k < fingers[f].n; ++k) {
            j.push({ x: origin.x + fingers[f].dx,
                     y: origin.y + 0.02 * k,
                     z: origin.z - 0.02 * k });
        }
    }
    return j;
}

project.create("vr hands " + Date.now());

// THIS PROJECT ASKS FOR BARE HANDS (lane HANDS-SWITCH-1; the owner, 2026-09-18,
// joint: bare hands are OFF unless the author says otherwise, per project).
//
// AND IT IS A DECLARATION OF INTENT RATHER THAN A SWITCH THIS FILE NEEDS, which
// is worth saying plainly: the row is read by a SESSION when it begins
// (`VrConfig::hands`) and this process was launched without --vr, so there is no
// session here for it to gate. What it does assert is the row itself —
// `world.vr` takes a boolean, writes the document and reads it back — and it
// keeps this file honest about which mode the rules below belong to.
assert(world.vr({ hands: true }).hands === true,
       "the project asks for bare hands (world.vr({hands:true}))");
assert(world.vr().hands === true, "...and reads back as one");

// ONE CUBE at a known place, and the hand comes at it down -Z from in front. A
// primitive Cube is two units across (half-extent 1, measured), so its +Z face
// is at z = 1.
var cube = scene.addPrimitive("Cube");
node.transform(cube, { position: { x: 0, y: 1, z: 0 } });
editor.frame(2);

// ---- 1. A HAND IS A HAND BECAUSE ITS PROFILE SAYS SO --------------------
//
// PER HAND, which is the part that is new: WiVRn binds `hand_interaction` for a
// hand with nothing in it and `touch_controller` for one holding a controller,
// each on its own, so a wearer really can have one of each.

sendHand({ aim: { x: 0, y: 1, z: 3 }, grip: { x: 0, y: 1, z: 3 },
           manip: { x: 0, y: 1, z: 2.8 } });
var st = vr.state();
console.log("right hand: " + JSON.stringify(st.input.right));
assert(st.input.right.profile === kHandProfile,
       "the short name 'hand_interaction' injected the full profile path");
assert(st.input.left.profile === "", "...and the left hand has nothing bound");
assert(st.profile === kHandProfile,
       "the session's summary profile is the right hand's (the manipulating hand)");
assert(st.input.right.manip.valid === true, "the hand reports a manipulation frame");
assert(near(st.input.right.manip.z, 2.8), "...at the pinch point it was given");
assert(near(st.input.right.grip.z, 3.0), "...which is NOT the palm's own pose");
assert(st.input.right.jointsTracked === false,
       "no skeleton has been injected yet, so nothing is tracked");

// A SAMPLE THAT SAYS NOTHING ABOUT ITS MANIPULATION FRAME HOLDS BY ITS GRIP —
// the rule lives in the engine, once, so every injector gets it.
sendHand({ aim: { x: 0, y: 1, z: 3 }, grip: { x: 0, y: 1, z: 3 } });
var stG = vr.state();
assert(stG.input.right.manip.valid === true && near(stG.input.right.manip.z, 3.0),
       "with no `manip` given, the manipulation frame IS the grip");

// An unknown profile name is refused rather than read as "nothing bound" — a
// hands test that silently asserted the controller behaviour would be worse
// than a red one.
throws(function () {
    vr.inject("right", { valid: true, aim: { x: 0, y: 1, z: 3 }, profile: "gloves" });
}, "unknown profile 'gloves'",
   "an unknown profile name is REFUSED, naming what was passed");

// ---- 1b. A BARE HAND HAS NO MODIFIER ------------------------------------
//
// `aim_activate_ext` IS THE PINCH (the extension defines it as "the wearer
// pinched at the thing they are pointing at"), so binding it as `menu` — which
// the first cut did, because §7's table said to — made one pinch raise BOTH
// select and the editor's modifier: every hand select would have been a TOGGLE,
// every hand grab a snapped one, and a light pinch that crossed the runtime's
// own bool threshold but not our 0.7 would have been a short unconsumed menu
// tap, i.e. a gizmo-mode cycle the wearer never asked for. `menu` is therefore
// UNBOUND on the hand profile and a bare hand has no modifier at all (a
// modifier for fingers is a different gesture — the off hand's grasp, a dwell —
// and a joint decision that has not been made).
//
// AN INJECTION MAY NOT INVENT ONE EITHER: a sample that says a bare hand
// pressed `menu` describes something no runtime in this build can report, so it
// is REFUSED rather than silently dropped.
throws(function () {
    vr.inject("right", { valid: true, aim: { x: 0, y: 1, z: 3 },
                         profile: "hand_interaction", menuPressed: true });
}, "menuPressed is not a thing a bare hand can do",
   "an injected bare hand may NOT press `menu` — it is unbound on the profile");
// ...and the same press on a CONTROLLER is ordinary.
assert(vr.inject("right", { valid: true, aim: { x: 0, y: 1, z: 3 },
                            profile: "touch", menuPressed: true }) === true,
       "...while a controller's menu press is ordinary");
assert(vr.step() === true && vr.state().input.right.menuPressed === true,
       "...and reads back as pressed");
assert(vr.inject("right") === true, "the hand is withdrawn again");

// ---- 2. THE GRAB FOLLOWS THE PINCH POINT, NOT THE PALM ------------------
//
// THE CASE NO CONTROLLER CAN MAKE. On a controller the grip and the
// manipulation frame are the same pose, so nothing could tell which one a grab
// was measured from. A bare hand has them in two places — the palm and the
// point where the fingers meet — so the two are moved INDEPENDENTLY here: the
// palm stays exactly where it is and only the pinch moves, and the held object
// must move by what the pinch moved.

editor.select(cube);
assert(editor.selection() === cube, "the cube is selected");
var start = posOf(cube);
// The pinch point is at the cube's near face; the palm is a metre off to the
// side, where a real palm is when the fingers are on something.
sendHand({ aim: { x: 0, y: 1, z: 3 }, grip: { x: 1, y: 1, z: 3 },
           manip: { x: 0, y: 1, z: 1.2 }, grab: 0 });
sendHand({ aim: { x: 0, y: 1, z: 3 }, grip: { x: 1, y: 1, z: 3 },
           manip: { x: 0, y: 1, z: 1.2 }, grab: 1 });
var mode = vr.interactionMode();
assert(mode.grabbing === true, "a grasp with the pinch point on the cube holds it");
assert(mode.far === false,
       "...as a NEAR grab: the pinch is 0.2 m from the hit, whatever the aim ray's length");
// ONLY THE PINCH MOVES: the palm and the aim stay put.
sendHand({ aim: { x: 0, y: 1, z: 3 }, grip: { x: 1, y: 1, z: 3 },
           manip: { x: 0.5, y: 1, z: 1.2 }, grab: 1 });
var moved = posOf(cube);
console.log("      the cube moved to (" + moved.x.toFixed(3) + ", " + moved.y.toFixed(3)
            + ", " + moved.z.toFixed(3) + ")");
assert(near(moved.x, start.x + 0.5, 1e-3),
       "the cube followed the PINCH POINT's half metre, with the palm never moving");
assert(vr.release() === true, "the grasp releases");

// ...AND THE CONVERSE, which is what keeps every controller honest: with no
// pinch point, the same gesture follows the GRIP.
var start2 = posOf(cube);
sendHand({ aim: { x: 0, y: 1, z: 3 }, grip: { x: 0, y: 1, z: 1.2 }, grab: 0 });
sendHand({ aim: { x: 0, y: 1, z: 3 }, grip: { x: 0, y: 1, z: 1.2 }, grab: 1 });
assert(vr.interactionMode().grabbing === true, "a grasp with the PALM on the cube holds it");
sendHand({ aim: { x: 0, y: 1, z: 3 }, grip: { x: -0.25, y: 1, z: 1.2 }, grab: 1 });
var moved2 = posOf(cube);
assert(near(moved2.x, start2.x - 0.25, 1e-3),
       "...and it follows the grip when that is the only frame the hand has");
assert(vr.release() === true, "released");

// ---- 3. ARM'S REACH IS MEASURED FROM THE HAND ---------------------------
//
// "Can I reach that?" is a question about two POINTS — where the hand is and
// where the thing is. The hit's distance ALONG THE AIM RAY answered it while
// only controllers existed, because a controller's aim origin sits a few
// centimetres from its grip; on a hand the aim ray starts at the runtime's own
// pointing origin and the hand holds things at the pinch point, which can be
// most of an arm away from it.

node.transform(cube, { position: { x: 0, y: 1, z: 0 } });
editor.select(cube);
// The ray travels 2 m to the cube's face, and the PINCH is 0.3 m from that
// face: within reach, so a NEAR grab.
sendHand({ aim: { x: 0, y: 1, z: 3 }, grip: { x: 0, y: 1, z: 3 },
           manip: { x: 0, y: 1, z: 1.3 }, grab: 0 });
sendHand({ aim: { x: 0, y: 1, z: 3 }, grip: { x: 0, y: 1, z: 3 },
           manip: { x: 0, y: 1, z: 1.3 }, grab: 1 });
assert(vr.interactionMode().far === false,
       "a hit 2 m along the ray but 0.3 m from the PINCH is a near grab");
assert(vr.release() === true, "released");
// The same ray with the pinch back AT the hand is out of reach: a far grab.
sendHand({ aim: { x: 0, y: 1, z: 3 }, grip: { x: 0, y: 1, z: 3 },
           manip: { x: 0, y: 1, z: 3 }, grab: 0 });
sendHand({ aim: { x: 0, y: 1, z: 3 }, grip: { x: 0, y: 1, z: 3 },
           manip: { x: 0, y: 1, z: 3 }, grab: 1 });
var far = vr.interactionMode();
assert(far.far === true, "...and with the pinch back at the hand, 2 m away, it is a FAR grab");
assert(near(far.distance, 2.0, 0.05), "at the ray's own distance: " + far.distance.toFixed(3));
assert(vr.release() === true, "released");

// ---- 4. THE SKELETON ROUND-TRIPS ---------------------------------------

var joints = skeleton({ x: 0.2, y: 1.2, z: 0.5 });
assert(joints.length === 26, "a hand is 26 joints (the XR_EXT_hand_tracking set)");
sendHand({ aim: { x: 0, y: 1, z: 3 }, grip: { x: 0.2, y: 1.2, z: 0.5 }, joints: joints });
var hj = vr.handJoints("right");
console.log("vr.handJoints(right): tracked=" + hj.tracked + " count=" + hj.count
            + " drawn=" + hj.drawn + " bones=" + hj.bones);
assert(hj.tracked === true, "the injected skeleton is reported as tracked");
assert(hj.count === 26, "...all 26 joints of it");
assert(near(hj.joints[0].x, 0.2) && near(hj.joints[0].y, 1.2) && near(hj.joints[0].z, 0.5),
       "joint 0 is the PALM, at the place it was given");
assert(near(hj.joints[1].y, 1.16), "joint 1 is the wrist, below it");
assert(hj.profile === kHandProfile, "and the hand it belongs to is a hand");
assert(vr.state().input.right.jointsTracked === true,
       "the per-hand bit on the status says so too (the cheap question, asked per frame)");
// NOTHING IS DRAWN, and that is the mirror's rule rather than a gap: it builds
// no VR furniture at all without an active session, so with no session there
// are no bone nodes to show. `mirror.vr_proxies` is where the drawing is
// asserted, against a hand-built live status.
assert(hj.drawn === 0 && hj.bones === 0,
       "with NO SESSION nothing is drawn for it — the drawer needs a live session "
       + "(mirror.vr_proxies asserts the segments)");
// A hand with no skeleton answers cleanly rather than at all.
var hjl = vr.handJoints("left");
assert(hjl.tracked === false && hjl.count === 0 && hjl.joints.length === 0,
       "the left hand has no skeleton and says so");
// THE WITHDRAWAL: an empty list stops injecting.
sendHand({ aim: { x: 0, y: 1, z: 3 }, grip: { x: 0.2, y: 1.2, z: 0.5 }, joints: [] });
assert(vr.handJoints("right").tracked === false, "an empty `joints` list stops injecting one");
// ...AND A HAND WITHDRAWN TAKES ITS SKELETON WITH IT.
sendHand({ aim: { x: 0, y: 1, z: 3 }, grip: { x: 0.2, y: 1.2, z: 0.5 }, joints: joints });
assert(vr.handJoints("right").tracked === true, "with the skeleton back");
assert(vr.inject("right") === true, "the hand is withdrawn (an empty state)");
assert(vr.handJoints("right").tracked === false,
       "...and its skeleton went with it — a hand a script took away leaves no fingers behind");
// TOO MANY JOINTS IS AN ERROR, not a silent truncation.
var tooMany = skeleton({ x: 0, y: 1, z: 0 });
tooMany.push({ x: 0, y: 0, z: 0 });
throws(function () {
    vr.inject("right", { valid: true, aim: { x: 0, y: 1, z: 3 }, joints: tooMany });
}, "at most 26", "27 joints is REFUSED, naming the limit");

// ---- 5. A HAND THAT CHANGES SHAPE MID-GESTURE ---------------------------
//
// THE WEARER PUT A CONTROLLER DOWN, OR PICKED ONE UP. The runtime re-binds that
// hand (its own XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED, arriving here
// as a changed `profile`), and the manipulation frame moves from a pinch point
// to a fist in ONE frame. Following the gesture through that change would carry
// the held object by the difference; the rule is the focus-loss rule, applied to
// one hand: cancel, put it back, record nothing.

node.transform(cube, { position: { x: 0, y: 1, z: 0 } });
editor.select(cube);
var before = posOf(cube);
var pushes0 = editor.undoState().pushes;
var changes0 = vr.interactionMode().profileChanges;
// A SESSION'S FIRST BIND IS NOT A CHANGE (the fix round's item 5): the runtime
// naming what the wearer is holding is not a re-bind of anything, and the
// previous frame's sample starts empty — so the very first profile seen is
// SEEDED rather than counted. Every hand above this line was bound
// `hand_interaction` or `touch` for the first time in this process, and the
// count so far is what that seeding leaves: the REAL changes below move it.
console.log("      profileChanges before the re-bind case: " + changes0);
sendHand({ aim: { x: 0, y: 1, z: 3 }, grip: { x: 1, y: 1, z: 3 },
           manip: { x: 0, y: 1, z: 1.2 }, grab: 0 });
sendHand({ aim: { x: 0, y: 1, z: 3 }, grip: { x: 1, y: 1, z: 3 },
           manip: { x: 0, y: 1, z: 1.2 }, grab: 1 });
assert(vr.interactionMode().grabbing === true, "the bare hand is holding the cube");
sendHand({ aim: { x: 0, y: 1, z: 3 }, grip: { x: 1, y: 1, z: 3 },
           manip: { x: 0, y: 1, z: 2.0 }, grab: 1 });
assert(!near(posOf(cube).z, before.z, 1e-3), "...and has carried it somewhere");
// THE HAND PICKS A CONTROLLER UP. Same pose, same held button, new profile.
sendHand({ aim: { x: 0, y: 1, z: 3 }, grip: { x: 1, y: 1, z: 3 },
           manip: { x: 0, y: 1, z: 2.0 }, grab: 1, profile: "touch" });
var afterChange = vr.interactionMode();
console.log("      after the re-bind: grabbing=" + afterChange.grabbing
            + " profileChanges=" + afterChange.profileChanges
            + " profileCancels=" + afterChange.profileCancels);
assert(vr.state().input.right.profile === kTouchProfile, "the hand is a controller now");
assert(afterChange.grabbing === false, "the gesture was CANCELLED by the re-bind");
assert(afterChange.profileChanges === changes0 + 1,
       "the change was counted EXACTLY once (" + changes0 + " -> "
       + afterChange.profileChanges + ")");
assert(afterChange.profileCancels >= 1, "...and so was what it cost");
var back = posOf(cube);
assert(near(back.x, before.x, 1e-3) && near(back.y, before.y, 1e-3)
           && near(back.z, before.z, 1e-3),
       "the cube went back exactly where the gesture found it: (" + back.x.toFixed(3) + ", "
       + back.y.toFixed(3) + ", " + back.z.toFixed(3) + ")");
assert(editor.undoState().pushes === pushes0,
       "and NOTHING was recorded — a cancelled gesture is not an edit");

// THE OTHER HAND IS UNTOUCHED BY A RE-BIND OF THIS ONE: the wearer still has
// it, and a gesture it owns is still theirs.
node.transform(cube, { position: { x: 0, y: 1, z: 0 } });
editor.select(cube);
sendHand({ hand: "right", aim: { x: 0, y: 1, z: 3 }, grip: { x: 0, y: 1, z: 1.2 },
           manip: { x: 0, y: 1, z: 1.2 }, grab: 0, profile: "touch" });
sendHand({ hand: "right", aim: { x: 0, y: 1, z: 3 }, grip: { x: 0, y: 1, z: 1.2 },
           manip: { x: 0, y: 1, z: 1.2 }, grab: 1, profile: "touch" });
assert(vr.interactionMode().grabbing === true, "the right hand (a controller) is holding it");
// The LEFT hand goes from nothing bound to bare fingers — a real event in a
// mixed session, and none of the right hand's business.
sendHand({ hand: "left", aim: { x: -1, y: 1, z: 3 }, grip: { x: -1, y: 1, z: 3 },
           profile: "hand_interaction" });
assert(vr.interactionMode().grabbing === true,
       "a re-bind of the OTHER hand leaves the gesture alone");
assert(vr.release() === true, "and it can still be put down");

console.log("scripting.e2e.vr_hands: PASS");
