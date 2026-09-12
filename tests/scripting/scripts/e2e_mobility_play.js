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
// and world.giStatus() are measurements, not document echoes — the document's
// count is the mirror's, the renderer's is giStatus's, §3.3.4):
//   * a still scene reports only the object the author marked;
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
// The RENDERER's side of the same question lives on world.giStatus(), beside
// the probe and rebuild counters it decides (REALTIME_REFLECTIONS_SPEC §3.3.4).
var gi = world.giStatus();
assert(gi.live === true, "giStatus is live (the engine answered)");
assert(gi.movableItems === 1,
       "the ENGINE holds one movable object — the push landed [" + gi.movableItems + "]");
assert(gi.movableLights === 0, "...and no movable lights");
assert(gi.mobilityMisses === 0, "nothing has surprised us yet");
assert(gi.mobilityRebuilds === 0, "and no rebuild has been charged to mobility");

// ---- AN EDITOR MOVE IS AUTHORING, NOT A PROMOTION -----------------------
// The same write, outside play, must change nothing: promoting here would
// rebuild the room's lighting in the middle of somebody's drag.
node.transform(prop, { position: { x: 0, y: 1.5, z: 0 } });
editor.frame(2);
var dragged = editor.mirrorStats();
assert(dragged.movableNodes === 1,
       "MOVING IT IN THE EDITOR DOES NOT PROMOTE IT [" + dragged.movableNodes + "]");
assert(world.giStatus().mobilityMisses === 0,
       "...and is not a miss either (it is authoring)");
// ...and it still costs what moving a STILL object has always cost: exactly one
// re-solve when the gesture settles. That is the control for the promotion case
// below — the settle machinery is untouched, it is only mobility flips that no
// longer reach it — and letting it land here is what keeps that window clean.
var solvesAfterDrag = editor.mirrorStats().giRefreshes;
editor.frame(60);
assert(editor.mirrorStats().giRefreshes === solvesAfterDrag + 1,
       "an editor drag of a STILL object still settles into exactly one re-solve [" +
       solvesAfterDrag + " -> " + editor.mirrorStats().giRefreshes + "]");

// ---- play, and push the unmarked crate ----------------------------------
var rebuildsBefore = world.giStatus().rebuilds;
assert(editor.play() === true, "editor.play()");
editor.frame(2);                       // the frame that sees the crate standing still
assert(world.giStatus().mobilityMisses === 0,
       "standing still during play is not a miss");

node.transform(prop, { position: { x: 0, y: 2, z: 0 } });
editor.frame(2);
var moved = world.giStatus();
assert(moved.mobilityMisses === 1, "the crate that started moving counts ONE miss");
assert(moved.lastMobilityMiss === "Crate", "...and the message names it by name");
assert(editor.mirrorStats().movableNodes === 2,
       "...and it is treated as movable from that frame");
assert(moved.movableItems === 2, "...in the engine too");
assert(node.mobility(prop).resolved === "movable" && node.mobility(prop).reason === "play",
       "node.mobility explains it: movable, because it moved while playing");
assert(node.mobility(prop).setting === "auto",
       "...without rewriting the author's setting (still auto)");

// KEEP MOVING: the warning is once per object per play session, not per frame.
node.transform(prop, { position: { x: 0, y: 3, z: 0 } });
editor.frame(2);
node.transform(prop, { position: { x: 0, y: 4, z: 0 } });
editor.frame(2);
assert(world.giStatus().mobilityMisses === 1,
       "...and it is counted ONCE, however long it keeps moving");

// THE WHOLE POINT: no rebuild of the room's lighting happened for any of it.
assert(world.giStatus().rebuilds === rebuildsBefore,
       "NO GI REBUILD while it happened (option A: a ghost, not a freeze) [" +
       world.giStatus().rebuilds + " vs " + rebuildsBefore + "]");
assert(world.giStatus().mobilityRebuilds === 0,
       "...and none is charged to mobility either (recording a class is free)");

// ---- THE SETTLE MUST NOT HEAR ABOUT IT (code review 2026-09-12, item 1) --
// The renderer's GI geometry signature is the mirror's settle key, and an
// object entering or leaving the bounce CHANGES it — so without the mobility
// gate, the frame the crate promoted would arm a pending refresh and fire a
// full re-solve about a quarter of a second later: a hitch, at play, which is
// the exact thing this program exists to remove. Sixty-plus frames is well past
// both settle gates (15 frames / 250 ms).
var solvesBefore = editor.mirrorStats().giRefreshes;
var rebuildsNow = world.giStatus().rebuilds;
editor.frame(90);
assert(editor.mirrorStats().giRefreshes === solvesBefore,
       "NO GI re-solve settles out of the promotion (" + editor.mirrorStats().giRefreshes +
       " vs " + solvesBefore + ")");
assert(world.giStatus().rebuilds === rebuildsNow,
       "...and no from-scratch rebuild either, 90 frames past the settle window");
assert(world.giStatus().mobilityRebuilds === 0, "...and the mobility counter is still zero");

// ---- stop: the promotion goes with the play session ---------------------
assert(editor.stop() === true, "editor.stop()");
editor.frame(3);
assert(node.mobility(prop).resolved === "static",
       "stopping play puts the crate back (the ghost bounce goes with it)");
assert(editor.mirrorStats().movableNodes === 1,
       "...leaving only the object the author marked");
assert(world.giStatus().movableItems === 1, "...in the engine too");

// A marked object never surprises anyone: play it again and move the Lift.
// (The counter is a RUNNING TOTAL for the session — a monitor reads it — so
// the claim is that it does not move, not that it is zero.)
var missesBefore = world.giStatus().mobilityMisses;
assert(editor.play() === true, "play again");
editor.frame(2);
node.transform(marked, { position: { x: 3, y: 2, z: 0 } });
editor.frame(2);
assert(world.giStatus().mobilityMisses === missesBefore,
       "A MARKED OBJECT IS NEVER A MISS (marking it is exactly the fix the warning asks for)");
assert(editor.stop() === true, "stop");

// ---- AN AUTHORING FLIP COSTS ONE REBUILD, AND EXACTLY ONE ---------------
// The other half of item 1: marking a still object Movable takes it out of the
// bounce, which the renderer pays for with one from-scratch rebuild. The settle
// must not add a SECOND one on top of it, however long the scene then idles.
assert(editor.stop() === true, "stopped");
editor.frame(10);
var reb0 = world.giStatus().rebuilds;
var mob0 = world.giStatus().mobilityRebuilds;
var solves0 = editor.mirrorStats().giRefreshes;
assert(node.setProperty(prop, "mobility", "movable") === true, "mark the Crate Movable by hand");
editor.frame(90);                       // well past both settle gates
var after = world.giStatus();
assert(after.mobilityRebuilds === mob0 + 1,
       "the flip costs EXACTLY ONE mobility rebuild [" + mob0 + " -> " + after.mobilityRebuilds + "]");
assert(after.rebuilds === reb0 + 1,
       "...one from-scratch GI rebuild in total [" + reb0 + " -> " + after.rebuilds + "]");
assert(editor.mirrorStats().giRefreshes === solves0,
       "...and NO re-solve settled out of it on top [" + editor.mirrorStats().giRefreshes +
       " vs " + solves0 + "]");
editor.frame(60);
assert(world.giStatus().rebuilds === reb0 + 1 && world.giStatus().mobilityRebuilds === mob0 + 1,
       "...and 60 more idle frames add nothing");

console.log("mobility_play: all assertions passed");
