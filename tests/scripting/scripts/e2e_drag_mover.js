// scripting.e2e.drag_mover — A DRAGGED STILL ON THE MOVER CHANNEL, AS A VERB
// (MOVER-1; the render audit's PHOTON F3 second half).
//
// A Still object being dragged stales the cascade grid on every frame it moves
// and the scheduler answers with one re-voxelisation per frame — measured on
// the Mirror Room, 32 over a 60-frame drag, 1.0-7.6 ms of CPU submission each.
// `world.gi({dragOnMoverChannel:true})` promotes it onto the mover channel for
// the length of the gesture instead: the cascades pay one re-voxelisation at
// each end of the drag and nothing in between. It is ON by default since
// 2026-09-20 (the owner's call on PICTURES-1's sheet): the picture AT REST is
// identical either way, so what the default buys is a gesture that does not
// re-solve the room under the hand; off stays available per project.
//
// This is the DOCUMENT half: the key, its refusals, its undo step, its survival
// of save/close/open, the two readings that say what it is doing, and the
// property that keeps it a project's own choice (no World Mode switch touches
// it). The PIXELS and the counters are gi.drag_mover
// (the engine suite) and the lane's evidence directory (the picture pairs).

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function throws(fn, msg) {
    var threw = false;
    try { fn(); } catch (e) { threw = true; console.log("ok: " + msg + " (" + e.message + ")"); }
    if (!threw) throw new Error("assert failed (no error): " + msg);
}

var guid = project.create("Drag mover " + Date.now());
assert(guid.length > 10, "project.create -> " + guid);

// ---- 1. the default is ON ---------------------------------------------------
assert(world.get().gi.dragOnMoverChannel === true,
       "a new project DOES put a dragged object on the mover channel — the owner's default");

// ---- 2. it is a bool, and nothing else -------------------------------------
assert(world.gi({ dragOnMoverChannel: false }) === true, "set false — the older rule is still reachable");
assert(world.get().gi.dragOnMoverChannel === false, "...and it reads back");
assert(world.gi({ dragOnMoverChannel: true }) === true, "set true");
assert(world.get().gi.dragOnMoverChannel === true, "...and back again");
throws(function () { world.gi({ dragOnMoverChannel: "yes" }); },
       "a STRING is refused with a sentence, not coerced");
throws(function () { world.gi({ dragOnMoverChannel: 1 }); },
       "...and so is a number");
assert(world.get().gi.dragOnMoverChannel === true, "a refused call changed nothing");
throws(function () { world.gi({ dragMoverChannel: true }); },
       "and a near-miss key is refused with the list of the ones that exist");

// ---- 3. one call, one undo step --------------------------------------------
function pushes() { return editor.undoState().pushes; }
var before = pushes();
world.gi({ dragOnMoverChannel: false });
assert(pushes() === before + 1, "a change records exactly ONE undo step");
before = pushes();
try { world.gi({ dragOnMoverChannel: "no" }); } catch (e) {}
assert(pushes() === before, "a refused call records nothing");

// ---- 4. saved with the scene -----------------------------------------------
project.save();
project.close();
project.open(guid);
assert(world.get().gi.dragOnMoverChannel === false,
       "the non-default setting survived save / close / open");
world.gi({ dragOnMoverChannel: true });
project.save();
project.close();
project.open(guid);
assert(world.get().gi.dragOnMoverChannel === true,
       "...and so does the default, written explicitly");

// ---- 5. the two readings ---------------------------------------------------
// They exist so that "is anything riding the channel right now" is answerable
// from outside, which is the only way the promotion can be seen at all: a
// promoted object looks exactly like itself.
var st = world.giStatus();
assert(typeof st.dragMovers === "number" && st.dragMovers === 0,
       "world.giStatus().dragMovers is 0 in a scene nobody is dragging");
assert(typeof st.dragMoverGestures === "number" && st.dragMoverGestures === 0,
       "...and dragMoverGestures counts the gestures that have ENDED (none yet)");

// ---- 6. it is NOT a tier row -----------------------------------------------
// A tier says how much a solve may COST; this says what a gesture does to the
// room's stored lighting. A World Mode switch must leave it exactly where the
// project put it, in both directions.
world.gi({ dragOnMoverChannel: true });
world.mode({ mode: "low" });
assert(world.get().gi.dragOnMoverChannel === true, "a World Mode switch does not touch it");
world.mode({ mode: "epic" });
assert(world.get().gi.dragOnMoverChannel === true, "...in either direction");
var rowIds = world.modeTable().rows.map(function (r) { return r.id; });
assert(rowIds.indexOf("giDragMoverChannel") < 0 && rowIds.indexOf("dragOnMoverChannel") < 0,
       "and it is not a World Mode row (no tier column)");

// ---- 7. the document's own mobility is a different question ----------------
// The promotion is the RENDERER's transient state: nothing in the document says
// the object moves, and nothing here may start saying so.
var box = scene.addPrimitive ? scene.addPrimitive("cube") : null;
if (box) {
    var m = node.mobility(box);
    assert(m.setting === "auto" && m.resolved === "static",
           "a new cube is Auto and resolves STATIC, whatever the rule says");
    node.transform(box, { position: { x: 1, y: 0.5, z: 0 } });
    node.transform(box, { position: { x: 1.2, y: 0.5, z: 0 } });
    editor.frame(3);
    assert(node.mobility(box).resolved === "static",
           "...and two moves do not rewrite the DOCUMENT's answer either");
}

console.log("PASS drag_mover");
