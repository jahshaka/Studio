// scripting.e2e.vr_gizmo — THE EDITOR'S GIZMO IN THE WEARER'S HANDS, WITH NO
// HEADSET (SPECS/VR_INPUT_SPEC.md §5.2, phase 4b stage 2).
//
// THE SAME ROUTE stage 1's gesture suite takes, for the same reason: this
// process was launched WITHOUT --vr, so no OpenXR loader was ever opened and no
// session exists — and the whole gesture runs anyway. `vr.inject` writes the
// per-hand state the runtime's action system would have written, into THE
// ENGINE's own store, and `vr.step()` runs one interaction frame on it. What is
// exercised is the EDITOR's gizmo: the viewport's own TranslationGizmo /
// RotationGizmo / ScaleGizmo objects, their frozen drag frame, their snap and
// their undo shape. There is no VR copy of any of it, which is the point.
//
// WHAT IS *NOT* HERE, and where it is instead:
//   * THE PICK GEOMETRY — the angular ring distance, the spherical quad of a
//     plane handle, the edge-on refusal and the tolerance arithmetic: the
//     document-only suites `gizmo.ring_pick` and `gizmo.plane_handles`, which
//     need no engine, no display and no app.
//   * THE SIZE RULE — `gizmo.screen_size`'s VR arm: a constant ANGULAR size
//     over distance, and the desktop's own per-frame sizing standing down
//     while a wearer is driving the gizmo.
//   * UNDO REACHING BACK INTO THE GESTURE — a script run is ONE open undo
//     macro, so `editor.undo()` cannot reach the step a release just recorded
//     (the note six other e2e scripts carry). The honest counter is
//     `editor.undoState().pushes`, which moves by exactly ONE per handle drag.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function near(a, b, eps) { return Math.abs(a - b) <= (eps === undefined ? 1e-3 : eps); }
function posOf(id) { return node.transform(id).position; }

/// THE YAW AND PITCH THAT POINT A HAND AT A WORLD POINT.
///
/// `vr.inject`'s pose is Ry(yaw) * Rx(pitch) * Rz(roll) (vrapi.cpp's one pose
/// reader), and a pose's forward is that rotation applied to -Z:
///
///     forward = (-sin(yaw) cos(pitch), sin(pitch), -cos(yaw) cos(pitch))
///
/// so pitch = asin(fy) and yaw = atan2(-fx, -fz). Inverted here rather than
/// guessed at, because every aim in this file is a point on a handle a few
/// centimetres wide.
function aimAt(from, to) {
    var dx = to.x - from.x, dy = to.y - from.y, dz = to.z - from.z;
    var len = Math.sqrt(dx * dx + dy * dy + dz * dz);
    dx /= len; dy /= len; dz /= len;
    var pitch = Math.asin(Math.max(-1, Math.min(1, dy))) * 180 / Math.PI;
    var yaw = Math.atan2(-dx, -dz) * 180 / Math.PI;
    return { yaw: yaw, pitch: pitch };
}

/// ONE FRAME OF CONTROLLER INPUT, aimed at a world point. Every field is passed
/// every time — `vr.inject` REPLACES a hand's whole sample.
function sendAim(o) {
    var a = aimAt(o.from, o.at);
    var pose = { x: o.from.x, y: o.from.y, z: o.from.z, yaw: a.yaw, pitch: a.pitch };
    var m = { valid: true, aim: pose, grip: pose,
              select: o.select || 0, grab: o.grab || 0, menuPressed: !!o.menu,
              stick: { x: 0, y: 0 },
              focused: o.focused === undefined ? true : !!o.focused };
    assert(vr.inject("right", m) === true && vr.step() === true,
           "one frame: aim from (" + o.from.x.toFixed(2) + ", " + o.from.y.toFixed(2) + ", "
           + o.from.z.toFixed(2) + ") at (" + o.at.x.toFixed(3) + ", " + o.at.y.toFixed(3) + ", "
           + o.at.z.toFixed(3) + ")" + (o.select ? " SELECT" : "") + (o.menu ? " MENU" : "")
           + (o.focused === false ? " UNFOCUSED" : ""));
}

/// A point on the gizmo's own X arrow, in world space: `t` of the gizmo scale
/// along +X from the pivot. The arrow's pick segment runs to handleLength *
/// handleScale = 1.7 * 0.05 = 0.085 of the scale, and the plane squares cover
/// the inner kPlaneHandleSpan * handleScale = 0.025 of it — so 0.06 is on the
/// shaft, past the squares and short of the tip.
function onXArrow(pivot, scale, t) {
    return { x: pivot.x + t * scale, y: pivot.y, z: pivot.z };
}

project.create("vr gizmo " + Date.now());

// ONE CUBE, at a known place, and the hand stands 3 m in front of it. A
// primitive Cube is two units across (half-extent 1).
var cube = scene.addPrimitive("Cube");
node.transform(cube, { position: { x: 0, y: 1, z: 0 } });
editor.frame(2);
var pivot = { x: 0, y: 1, z: 0 };
var hand = { x: 0, y: 1, z: 3 };

// ---- 0. NO SELECTION, NO GIZMO -----------------------------------------

editor.selectNone();
sendAim({ from: hand, at: pivot });
var g0 = vr.gizmo();
console.log("vr.gizmo (nothing selected): " + JSON.stringify(g0));
assert(g0.present === true, "the editor viewport has a gizmo object");
assert(g0.mode === "translate", "and the editor is in translate mode to begin with");
assert(g0.dragging === false && g0.handle === "", "with nothing selected no handle is under the ray");

// ---- 1. THE SIZE RULE, AS THE WEARER SEES IT ---------------------------

editor.select(cube);
assert(editor.selection() === cube, "the cube is selected");
sendAim({ from: hand, at: pivot });
var g1 = vr.gizmo();
console.log("vr.gizmo (armed): " + JSON.stringify(g1));
assert(g1.armed === true, "a located hand arms the gizmo's VR pointer");
assert(near(g1.eye.x, hand.x) && near(g1.eye.z, hand.z),
       "with no session the wearer's EYE is the aiming hand itself (the honest answer on a box "
       + "with no runtime)");
// kGizmoScreenFraction * distance * tan(fov/2) = 4.33 * 3 * 1 at the nominal
// 90-degree eye.
assert(near(g1.scale, 4.33 * 3.0, 0.01),
       "the gizmo is sized by the VR rule: 4.33 * 3 m = " + g1.scale.toFixed(3));
assert(near(g1.toleranceDegrees, 0.7427, 0.001),
       "...and the pick tolerance is the desktop's 7 px of a 1080-tall frame as an angle at that "
       + "eye: " + g1.toleranceDegrees.toFixed(4) + " degrees");

// THE SAME ANGLE AT EVERY DISTANCE (the owner's decision 5). Stand twice as far
// away: the scale doubles, so what the wearer sees does not change.
var far = { x: 0, y: 1, z: 6 };
sendAim({ from: far, at: pivot });
var g2 = vr.gizmo();
assert(near(g2.scale, 2 * g1.scale, 0.02),
       "twice as far away, the gizmo is twice as big in world units — i.e. the SAME SIZE to the "
       + "wearer (" + g2.scale.toFixed(3) + " vs " + g1.scale.toFixed(3) + ")");

// ---- 2. THE RAY IS ON A HANDLE, AND A PRESS DRAGS IT --------------------

sendAim({ from: hand, at: pivot });
var scale = vr.gizmo().scale;
var grip = onXArrow(pivot, scale, 0.06);
sendAim({ from: hand, at: grip });
assert(vr.gizmo().handle === "x",
       "the aim ray on the X arrow names it — a press's answer, without pressing");

var pushes0 = editor.undoState().pushes;
var start = posOf(cube);
sendAim({ from: hand, at: grip, select: 1 });          // the press EDGE
assert(vr.gizmo().dragging === true, "the trigger press starts a HANDLE drag, not a selection");
assert(editor.selection() === cube, "...and leaves the selection alone");

// Move the aim a metre further along the axis and release.
var target = onXArrow(pivot, scale, 0.06);
target.x += 1.0;
sendAim({ from: hand, at: target, select: 1 });
var mid = posOf(cube);
console.log("   dragged to (" + mid.x.toFixed(3) + ", " + mid.y.toFixed(3) + ", "
            + mid.z.toFixed(3) + ")");
assert(mid.x > 0.5 && mid.x < 1.5, "the node follows the aim ALONG THE AXIS (+" + mid.x.toFixed(3) + ")");
assert(near(mid.y, start.y, 1e-3) && near(mid.z, start.z, 1e-3),
       "...and moves on no other axis at all");

sendAim({ from: hand, at: target });                    // the release EDGE
assert(vr.gizmo().dragging === false, "the release ends the drag");
var pushes1 = editor.undoState().pushes;
console.log("   undo pushes: " + pushes0 + " -> " + pushes1);
assert(pushes1 === pushes0 + 1,
       "ONE undo entry for the whole drag — the mouse drag's own shape (Gizmo::createUndoAction), "
       + "not a second one for VR");
assert(vr.gizmo().commits === 1, "and the interaction counted one committed handle drag");

// ---- 3. A PRESS THAT IS NOT ON A HANDLE IS STILL A SELECTION -----------

var second = scene.addPrimitive("Cube");
node.transform(second, { position: { x: 6, y: 1, z: 0 } });
editor.frame(2);
// ...and the gizmo put back on the FIRST cube: `scene.addPrimitive` selects
// what it adds, so without this the gizmo would be standing on the very node
// this case aims at, and "the ray is on no handle" would be asserting the
// opposite of what it says.
editor.select(cube);
var far2 = { x: 6, y: 1, z: 0 };
sendAim({ from: hand, at: far2 });
assert(vr.gizmo().handle === "", "the ray on the far cube is on no handle (it names '"
       + vr.gizmo().handle + "')");
sendAim({ from: hand, at: far2, select: 1 });
sendAim({ from: hand, at: far2 });
assert(editor.selection() === second,
       "a press off the handles selects what it points at — the desktop's own precedence (the "
       + "gizmo's hit test first, then the pick)");

// ---- 4. THE MODE CYCLE: A SHORT PRESS OF `menu` ------------------------

editor.select(cube);
editor.setGizmoMode("translate");
sendAim({ from: hand, at: pivot });
assert(editor.gizmoMode() === "translate", "back in translate mode");
sendAim({ from: hand, at: pivot, menu: true });         // down for one frame
sendAim({ from: hand, at: pivot });                     // ...and up: a SHORT press
assert(editor.gizmoMode() === "rotate",
       "a short press of `menu` cycles translate -> rotate, through editor.setGizmoMode");
sendAim({ from: hand, at: pivot, menu: true });
sendAim({ from: hand, at: pivot });
assert(editor.gizmoMode() === "scale", "...rotate -> scale");
sendAim({ from: hand, at: pivot, menu: true });
sendAim({ from: hand, at: pivot });
assert(editor.gizmoMode() === "translate", "...and scale -> translate: the Space key's own cycle");
assert(vr.gizmo().modes === 3, "three cycles, counted");

// A LONG press does NOT cycle: 0.25 s at the nominal 1/90 s a scripted step
// charges is 23 frames, so 30 of them is a hold.
for (var i = 0; i < 30; ++i) sendAim({ from: hand, at: pivot, menu: true });
sendAim({ from: hand, at: pivot });
assert(editor.gizmoMode() === "translate",
       "a HELD `menu` is the modifier and does not cycle the mode (30 frames = 0.33 s, past the "
       + "0.25 s threshold)");

// ---- 5. `menu` HELD SNAPS THE DRAG -------------------------------------

node.transform(cube, { position: { x: 0, y: 1, z: 0 } });
editor.frame(1);
sendAim({ from: hand, at: pivot });
scale = vr.gizmo().scale;
grip = onXArrow(pivot, scale, 0.06);
var modeBefore = editor.gizmoMode();
var pushes2 = editor.undoState().pushes;
sendAim({ from: hand, at: grip, select: 1, menu: true });
assert(vr.gizmo().dragging === true, "a drag begun with `menu` held");
var snapTarget = onXArrow(pivot, scale, 0.06);
snapTarget.x += 1.37;                                   // deliberately off the grid
sendAim({ from: hand, at: snapTarget, select: 1, menu: true });
sendAim({ from: hand, at: snapTarget, menu: true });    // release, still holding menu
sendAim({ from: hand, at: snapTarget });                // and menu up
var snapped = posOf(cube);
console.log("   snapped drag landed at x = " + snapped.x.toFixed(4));
assert(near(snapped.x, Math.round(snapped.x), 1e-3) && snapped.x !== 0,
       "with `menu` held the drag lands on the snap grid (x = " + snapped.x.toFixed(3) + ")");
assert(editor.undoState().pushes === pushes2 + 1, "...and still costs exactly one undo entry");
assert(editor.gizmoMode() === modeBefore,
       "...and the `menu` that snapped it did NOT also cycle the mode when it came up: a press "
       + "used as the modifier cannot be a short press");

// ---- 6. A GESTURE THAT LOSES ITS INPUT IS CANCELLED --------------------

node.transform(cube, { position: { x: 0, y: 1, z: 0 } });
editor.frame(1);
sendAim({ from: hand, at: pivot });
scale = vr.gizmo().scale;
grip = onXArrow(pivot, scale, 0.06);
var before = posOf(cube);
var pushes3 = editor.undoState().pushes;
sendAim({ from: hand, at: grip, select: 1 });
var moveTo = onXArrow(pivot, scale, 0.06);
moveTo.x += 2.0;
sendAim({ from: hand, at: moveTo, select: 1 });
assert(posOf(cube).x > 1.0, "the drag has moved the cube...");
sendAim({ from: hand, at: moveTo, select: 1, focused: false });   // the dashboard came up
var after = posOf(cube);
console.log("   after losing focus mid-drag: x = " + after.x.toFixed(4));
assert(vr.gizmo().dragging === false, "losing focus ends the handle drag");
assert(near(after.x, before.x, 1e-3) && near(after.y, before.y, 1e-3)
       && near(after.z, before.z, 1e-3),
       "...by putting the node back where the drag found it (a cancel, not a commit)");
assert(editor.undoState().pushes === pushes3,
       "...and pushing NOTHING — an empty undo entry would eat the user's next Ctrl+Z");

// ---- 7. THE ROTATION RINGS AND THE SCALE HANDLES ANSWER TOO ------------

node.transform(cube, { position: { x: 0, y: 1, z: 0 }, rotation: { x: 0, y: 0, z: 0 } });
editor.frame(1);
editor.setGizmoMode("rotate");
sendAim({ from: hand, at: pivot });
scale = vr.gizmo().scale;
// A point on the Z ring, at its own radius: the axis rings are the unit circle
// of the rotation gizmo's handle scale, which the shipped constants make
// kScaleEnd * kHandleScale * kRotationExtentRatio / kScreenRingRadius =
// 1.46 * 0.05 * 1.00 / 1.18 = 0.06186 of the gizmo scale (gizmomeshes.h). The
// +X point of the Z ring is also ON the Y ring — the two circles cross there —
// so the answer is the pick's own tie rule: the ring more face-on to the
// pointer wins, which from in front is Z.
var ringRadius = 0.06186 * scale;
var onRing = { x: pivot.x + ringRadius, y: pivot.y, z: pivot.z };
sendAim({ from: hand, at: onRing });
var ringHandle = vr.gizmo().handle;
console.log("   the ray on the +X point of the Z ring names '" + ringHandle + "'");
assert(ringHandle === "z",
       "a rotation ring answers the aim ray (angular pick), and where two rings cross the one "
       + "facing the pointer wins");
var rot0 = node.transform(cube).rotation;
var pushes4 = editor.undoState().pushes;
sendAim({ from: hand, at: onRing, select: 1 });
assert(vr.gizmo().dragging === true, "a press on a ring starts a rotation drag");
// Swing the aim a quarter of the way round the ring.
var swung = { x: pivot.x, y: pivot.y + ringRadius, z: pivot.z };
sendAim({ from: hand, at: swung, select: 1 });
sendAim({ from: hand, at: swung });
var rot1 = node.transform(cube).rotation;
console.log("   rotation " + JSON.stringify(rot0) + " -> " + JSON.stringify(rot1));
assert(Math.abs(rot1.x - rot0.x) + Math.abs(rot1.y - rot0.y) + Math.abs(rot1.z - rot0.z) > 1.0,
       "the ring drag turned the node");
assert(editor.undoState().pushes === pushes4 + 1, "...in exactly one undo entry");

editor.setGizmoMode("scale");
node.transform(cube, { position: { x: 0, y: 1, z: 0 }, rotation: { x: 0, y: 0, z: 0 },
                       scale: { x: 1, y: 1, z: 1 } });
editor.frame(1);
sendAim({ from: hand, at: pivot });
scale = vr.gizmo().scale;
var onScaleX = { x: pivot.x + 0.06 * scale, y: pivot.y, z: pivot.z };
sendAim({ from: hand, at: onScaleX });
console.log("   the ray on the scale gizmo's X box names '" + vr.gizmo().handle + "'");
assert(vr.gizmo().handle === "x", "the scale gizmo's X handle answers the ray (3D geometry, no "
       + "second pick path needed)");
var scale0 = node.transform(cube).scale;
var pushes5 = editor.undoState().pushes;
sendAim({ from: hand, at: onScaleX, select: 1 });
var pulled = { x: pivot.x + 0.06 * scale + 1.5, y: pivot.y, z: pivot.z };
sendAim({ from: hand, at: pulled, select: 1 });
sendAim({ from: hand, at: pulled });
var scale1 = node.transform(cube).scale;
console.log("   scale " + JSON.stringify(scale0) + " -> " + JSON.stringify(scale1));
assert(scale1.x > scale0.x + 0.1, "the scale drag grew the node on X");
assert(near(scale1.y, scale0.y, 1e-3) && near(scale1.z, scale0.z, 1e-3),
       "...and on no other axis");
assert(editor.undoState().pushes === pushes5 + 1, "...in exactly one undo entry");

// ---- 8. THE POINTER GOES AWAY WITH THE HAND ----------------------------

assert(vr.inject("right", {}) === true, "the hand stops reporting");
assert(vr.step() === true, "one more frame");
var gEnd = vr.gizmo();
console.log("vr.gizmo (no hand): " + JSON.stringify(gEnd));
assert(gEnd.armed === false,
       "with no hand located the VR pointer is disarmed and the gizmo is the desk's again");
assert(gEnd.dragging === false, "and nothing is being dragged");

console.log("PASS: the editor's gizmo, driven by a controller ray, with no headset in the room");
