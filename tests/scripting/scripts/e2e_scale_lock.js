// scripting.e2e.scale_lock — THE SCALE-RATIO LOCK (SCALE-LOCK-1; the owner's
// "a lock for the transformations", 2026-09-17).
//
// The feature is a per-NODE flag that changes what an edit to ONE scale channel
// means: with it on, the other two are multiplied by the same ratio (Unreal's
// per-actor preserve-ratio). This suite is the API-first half — the verb, the
// arithmetic, the three degenerate rules, the reflected row, the undo step, and
// the save/reopen round trip through the real writer and reader. The panel's
// lock icon and the gizmo's Shift-drag call exactly the same document path
// (iris::scalelock::apply, over the scale the gesture started at), and are
// gated by ui.panel_undo and gizmo.scale_lock.
//
// WHAT IS ASSERTED, in order:
//   A. the flag: default off, set/read through the verb AND through the
//      reflected `scaleLock` row, and idempotent (setting what it already is
//      records nothing).
//   B. the arithmetic: a locked one-channel write scales the other two by the
//      same ratio, in both directions, on a non-uniform starting scale.
//   C. the degenerate rules, each stated in the verb's own documentation:
//      a zero channel has no ratio (the other two keep their values), a
//      negative ratio mirrors all three, and writing the value a channel
//      already has changes nothing.
//   D. naming TWO or three channels is taken literally even when locked (the
//      caller has already said what every channel should be), and
//      `scaleUniform` overrides the flag in both directions — the verb's
//      spelling of Shift-dragging a field.
//   E. it travels: a duplicate of a locked node is locked, and the flag
//      survives save -> close -> open through the scene file.
//
// Document verbs only -> --headless: no engine, no display.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

function near(a, b) { return Math.abs(a - b) < 1e-4; }

function scaleOf(id) { return node.transform(id).scale; }

function assertScale(id, x, y, z, msg) {
    var s = scaleOf(id);
    assert(near(s.x, x) && near(s.y, y) && near(s.z, z),
           msg + " (" + s.x + ", " + s.y + ", " + s.z + " — expected " + x + ", " + y + ", " + z + ")");
}

var guid = project.create("Scale Lock " + Date.now());
assert(guid.length > 10, "project.create");

var cube = scene.addPrimitive("cube", { position: { x: 0, y: 0, z: 0 } });
assert(cube.length > 10, "scene.addPrimitive(cube)");

// ---- A. the flag ---------------------------------------------------------
assert(node.scaleLock(cube) === false, "A: a fresh node does NOT preserve its ratio");
assert(node.property(cube, "scaleLock") === false,
       "A: …and the reflected row says the same thing");

var rows = node.properties(cube).filter(function (r) { return r.name === "scaleLock"; });
assert(rows.length === 1, "A: node.properties lists the row exactly once");
assert(rows[0].type === "bool" && rows[0].writable === true &&
       rows[0].displayName === "Lock Scale Ratio",
       "A: …as a writable bool named 'Lock Scale Ratio' (" + JSON.stringify(rows[0]) + ")");

assert(node.setScaleLock(cube, true) === true, "A: node.setScaleLock(id, true)");
assert(node.scaleLock(cube) === true, "A: …and it reads back locked");
assert(node.property(cube, "scaleLock") === true, "A: …through the row too");
// Idempotent: the same value again is accepted and records nothing (the undo
// half of that claim is ui.panel_undo's; here the point is it does not refuse).
assert(node.setScaleLock(cube, true) === true, "A: setting the lock it already has is fine");

assert(node.setProperty(cube, "scaleLock", false) === true,
       "A: the row writes it too (node.setProperty)");
assert(node.scaleLock(cube) === false, "A: …and the verb agrees");
node.setScaleLock(cube, true);

// ---- B. the arithmetic ---------------------------------------------------
// A deliberately NON-UNIFORM start: "preserve the ratio" is about the ratio the
// object has, not about making it a cube.
node.transform(cube, { scale: { x: 1, y: 2, z: 0.5 } });
assertScale(cube, 1, 2, 0.5, "B: the authored non-uniform scale");

node.transform(cube, { scale: { x: 2 } });
assertScale(cube, 2, 4, 1, "B: locked, x 1 -> 2 doubles all three");

node.transform(cube, { scale: { y: 1 } });
assertScale(cube, 0.5, 1, 0.25, "B: locked, y 4 -> 1 quarters all three");

node.transform(cube, { scale: { z: 0.5 } });
assertScale(cube, 1, 2, 0.5, "B: locked, z 0.25 -> 0.5 doubles all three back");

// The one-channel write is the same verb whichever way it is spelled, and the
// RESULT is what the verb returns — not a stale read.
var back = node.transform(cube, { scale: { x: 3 } });
assert(near(back.scale.x, 3) && near(back.scale.y, 6) && near(back.scale.z, 1.5),
       "B: the verb RETURNS the scaled vector, not the one it was handed");
node.transform(cube, { scale: { x: 1, y: 2, z: 0.5 } });

// ---- C. the three degenerate rules --------------------------------------
// ZERO HAS NO RATIO, ON EITHER SIDE. Every multiple of 0 is 0, so nothing can be
// recovered from "it was 0 and now it is 3" — and nothing can REACH 0 by a ratio
// without taking the other two with it, for good. Both halves move the edited
// channel alone. (A flattened axis is a thing people have on purpose; the
// surprise would be losing the two numbers they never touched — and with a
// typed value in the panel, losing them in one keystroke.)
node.transform(cube, { scale: { x: 0, y: 2, z: 0.5 } });
assertScale(cube, 0, 2, 0.5, "C: a channel set to zero (all three named — literal)");
node.transform(cube, { scale: { x: 3 } });
assertScale(cube, 3, 2, 0.5,
            "C: 0 -> 3 has NO ratio: x takes the value, y and z keep theirs");

// THE ROUND TRIP THROUGH ZERO, which is the one that used to be irrecoverable:
// {x: 0} then {x: 1} on a locked (1, 2, 0.5) must come back to (1, 2, 0.5).
node.transform(cube, { scale: { x: 1, y: 2, z: 0.5 } });
node.transform(cube, { scale: { x: 0 } });
assertScale(cube, 0, 2, 0.5,
            "C: a locked write of ZERO flattens that channel and only that channel");
node.transform(cube, { scale: { x: 1 } });
assertScale(cube, 1, 2, 0.5,
            "C: …so un-flattening it brings the object back exactly (it used to collapse to " +
            "(1, 0, 0) and stay there)");

// A NEGATIVE RATIO MIRRORS ALL THREE, sign included.
node.transform(cube, { scale: { x: 1, y: 2, z: 0.5 } });
node.transform(cube, { scale: { x: -1 } });
assertScale(cube, -1, -2, -0.5, "C: x 1 -> -1 mirrors every channel (the ratio keeps its sign)");
node.transform(cube, { scale: { x: 1 } });
assertScale(cube, 1, 2, 0.5, "C: …and mirroring back restores it exactly");

// THE SAME VALUE IS A NO-OP. The ratio is exactly 1.
node.transform(cube, { scale: { y: 2 } });
assertScale(cube, 1, 2, 0.5, "C: writing a channel the value it already has changes nothing");

// ---- D. two or three channels, and the override --------------------------
node.transform(cube, { scale: { x: 2, y: 2 } });
assertScale(cube, 2, 2, 0.5,
            "D: naming TWO channels is taken literally even when locked (z is left alone)");

node.transform(cube, { scale: [1, 2, 0.5] });
assertScale(cube, 1, 2, 0.5, "D: the array spelling is the whole vector, taken literally");

node.transform(cube, { scale: { x: 2 }, scaleUniform: false });
assertScale(cube, 2, 2, 0.5, "D: scaleUniform:false is per-channel on a LOCKED node");

// …and where it could only be a misunderstanding, it is REFUSED rather than
// silently ignored (a malformed call is loud — the vr.begin whitelist rule).
node.transform(cube, { scale: { x: 1, y: 2, z: 0.5 } });
// (A refused verb THROWS in this host and carries the reason; app.lastError()
// holds the same text.)
function refusal(fn) {
    try { fn(); } catch (e) { return String(e.message || e); }
    return "";
}
var why = refusal(function () {
    node.transform(cube, { scale: { x: 4, y: 4 }, scaleUniform: true });
});
assert(why.indexOf("scaleUniform") >= 0,
       "D: scaleUniform with TWO channels named is refused, and says why: " + why);
assertScale(cube, 1, 2, 0.5, "D: …and the refused call wrote nothing");
why = refusal(function () {
    node.transform(cube, { position: { x: 1 }, scaleUniform: true });
});
assert(why.indexOf("no scale at all") >= 0,
       "D: scaleUniform with NO scale is refused too: " + why);
assert(node.transform(cube).position.x === 0,
       "D: …and that refusal wrote no position either");

node.setScaleLock(cube, false);
node.transform(cube, { scale: { x: 1, y: 2, z: 0.5 } });
node.transform(cube, { scale: { x: 2 } });
assertScale(cube, 2, 2, 0.5, "D: unlocked, a one-channel write is that one channel");
node.transform(cube, { scale: { x: 1, y: 2, z: 0.5 } });
node.transform(cube, { scale: { x: 2 }, scaleUniform: true });
assertScale(cube, 2, 4, 1,
            "D: scaleUniform:true preserves the ratio on an UNLOCKED node (the verb's Shift-drag)");

// ---- E. it travels -------------------------------------------------------
node.setScaleLock(cube, true);
node.transform(cube, { scale: { x: 1, y: 2, z: 0.5 } });
var copy = node.duplicate(cube);
assert(copy.length > 10, "E: node.duplicate");
assert(node.scaleLock(copy) === true, "E: a duplicate of a locked node is locked");
node.transform(copy, { scale: { z: 1 } });
assertScale(copy, 2, 4, 1, "E: …and the copy's own locked edit preserves its ratio");

// The plain node beside it is untouched: the flag is per node, not per scene.
var plain = scene.addPrimitive("sphere", { position: { x: 3, y: 0, z: 0 } });
assert(node.scaleLock(plain) === false, "E: the flag is PER NODE (the sphere is unlocked)");

// SAVE -> CLOSE -> OPEN, through the real writer and reader.
var lockedName = node.info(cube).name;
var plainName = node.info(plain).name;
assert(project.save() === true, "E: project.save");
assert(project.close() === true, "E: project.close");
assert(project.open(guid) === true, "E: project.open (REOPEN)");

var reopened = scene.find(lockedName);
assert(reopened.length > 10, "E: the locked node is back after the reopen (" + lockedName + ")");
assert(node.scaleLock(reopened) === true, "E: …and it is STILL locked (writer + reader)");
assert(node.scaleLock(scene.find(plainName)) === false,
       "E: …while the unlocked one is still unlocked (false round-trips as false)");
// The flag is live after the reopen, not just stored.
node.transform(reopened, { scale: { x: 2 } });
assertScale(reopened, 2, 4, 1, "E: the reopened node's lock still does the arithmetic");

console.log("e2e_scale_lock: ALL OK");
