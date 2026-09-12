// scripting.e2e.mobility_play — THE SURPRISE MOVER
// (SPECS/REALTIME_REFLECTIONS_SPEC.md §3.3.3, owner decision O3).
//
// Somebody's script pushes a prop nobody marked as moving, during play. The
// decision was: it moves SMOOTHLY. It is treated as movable from that frame,
// with NO rebuild of the room's lighting — so it leaves a "ghost" of its old
// bounce light where it started until play stops — and the author gets ONE
// warning naming it. The alternative (option B) was a half-second freeze the
// moment it started moving, which is exactly what this whole program removes.
//
// Asserted here, through the real app with the engine up (editor.mirrorStats()
// and world.giStatus() are measurements, not document echoes):
//   * a still scene reports ZERO movable objects;
//   * the document's resolution and the ENGINE's record agree (the push landed);
//   * moving an unmarked prop during play counts exactly ONE miss, names it,
//     and moving it again does not count a second;
//   * world.giStatus().rebuilds DOES NOT MOVE while it happens;
//   * stopping play clears the promotion;
//   * a prop the author marked Movable never counts as a miss at all.
//
// Engine UP, not --headless: the counters are the point.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

project.create("Mobility Play " + Date.now());

// Something for the renderer to actually light.
var ground = scene.addPrimitive("ground");
var prop = scene.addPrimitive("cube", { position: { x: 0, y: 1, z: 0 } });
node.setProperty(prop, "name", "Crate");
var marked = scene.addPrimitive("cube", { position: { x: 3, y: 1, z: 0 } });
node.setProperty(marked, "name", "Lift");
assert(node.setProperty(marked, "mobility", "movable") === true, "the Lift is marked Movable");
editor.frame(3);

var stats = editor.mirrorStats();
assert(stats.available === true, "mirrorStats is live (the editor viewport has a mirror)");
assert(stats.movableNodes === 1,
       "exactly ONE object in a still scene is movable: the one the author marked " +
       "[" + stats.movableNodes + "]");
assert(stats.engineMovableNodes === stats.movableNodes,
       "the ENGINE holds the same count — the push landed");
assert(stats.engineMovableItems === 1, "...and it is the one carrying geometry");
assert(stats.mobilityMisses === 0, "nothing has surprised us yet");

// ---- AN EDITOR MOVE IS AUTHORING, NOT A PROMOTION -----------------------
// The same write, outside play, must change nothing: promoting here would
// rebuild the room's lighting in the middle of somebody's drag.
node.transform(prop, { position: { x: 0, y: 1.5, z: 0 } });
editor.frame(2);
var dragged = editor.mirrorStats();
assert(dragged.movableNodes === 1,
       "MOVING IT IN THE EDITOR DOES NOT PROMOTE IT [" + dragged.movableNodes + "]");
assert(dragged.mobilityMisses === 0, "...and is not a miss either (it is authoring)");

// ---- play, and push the unmarked crate ----------------------------------
var rebuildsBefore = world.giStatus().rebuilds;
assert(editor.play() === true, "editor.play()");
editor.frame(2);                       // the frame that sees the crate standing still
assert(editor.mirrorStats().mobilityMisses === 0,
       "standing still during play is not a miss");

node.transform(prop, { position: { x: 0, y: 2, z: 0 } });
editor.frame(2);
var moved = editor.mirrorStats();
assert(moved.mobilityMisses === 1, "the crate that started moving counts ONE miss");
assert(moved.lastMobilityMiss === "Crate", "...and the message names it by name");
assert(moved.movableNodes === 2, "...and it is treated as movable from that frame");
assert(moved.engineMovableNodes === 2, "...in the engine too");
assert(node.mobility(prop).resolved === "movable" && node.mobility(prop).reason === "play",
       "node.mobility explains it: movable, because it moved while playing");
assert(node.mobility(prop).setting === "auto",
       "...without rewriting the author's setting (still auto)");

// KEEP MOVING: the warning is once per object per play session, not per frame.
node.transform(prop, { position: { x: 0, y: 3, z: 0 } });
editor.frame(2);
node.transform(prop, { position: { x: 0, y: 4, z: 0 } });
editor.frame(2);
assert(editor.mirrorStats().mobilityMisses === 1,
       "...and it is counted ONCE, however long it keeps moving");

// THE WHOLE POINT: no rebuild of the room's lighting happened for any of it.
assert(world.giStatus().rebuilds === rebuildsBefore,
       "NO GI REBUILD while it happened (option A: a ghost, not a freeze) [" +
       world.giStatus().rebuilds + " vs " + rebuildsBefore + "]");

// ---- stop: the promotion goes with the play session ---------------------
assert(editor.stop() === true, "editor.stop()");
editor.frame(3);
var stopped = editor.mirrorStats();
assert(node.mobility(prop).resolved === "static",
       "stopping play puts the crate back (the ghost bounce goes with it)");
assert(stopped.movableNodes === 1, "...leaving only the object the author marked");
assert(stopped.engineMovableNodes === 1, "...in the engine too");

// A marked object never surprises anyone: play it again and move the Lift.
// (The counter is a RUNNING TOTAL for the session — a monitor reads it — so
// the claim is that it does not move, not that it is zero.)
var missesBefore = editor.mirrorStats().mobilityMisses;
assert(editor.play() === true, "play again");
editor.frame(2);
node.transform(marked, { position: { x: 3, y: 2, z: 0 } });
editor.frame(2);
assert(editor.mirrorStats().mobilityMisses === missesBefore,
       "A MARKED OBJECT IS NEVER A MISS (marking it is exactly the fix the warning asks for)");
assert(editor.stop() === true, "stop");

console.log("mobility_play: all assertions passed");
