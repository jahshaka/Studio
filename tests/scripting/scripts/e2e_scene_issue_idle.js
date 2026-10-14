// scripting.e2e.scene_issue_idle — AN IDLE EDITOR CHECKS NOTHING; A WRITE CHECKS
// WHAT WAS WRITTEN (BISTRO-ENGINE-1 stage 5, finding 14).
//
// The editor runs the scene checker once a second (SceneIssueWatch). Its two
// node conditions — `shadow.leak` (a box per solid node) and `texture.missing`
// (every mesh's material) — used to walk the whole scene on every pass,
// changed or not: ~0.2 s of UI thread per second at 6,000 meshes in a Debug
// build, which was the owner's Bistro session (218 heartbeat lines of ~430 ms,
// once a second, with nothing moving). Each node's answers are now cached and a
// pass re-checks only the nodes the document wrote — the node graph's write
// journal, through a tap (graph::openJournalTap).
//
// COUNTS, NEVER TIME. `editor.checkScene()` is the timer's pass (one call, one
// pass); its `walked.nodes` is what THAT pass re-checked, `walked.visited` the
// running total over every pass (the editor's own 1 Hz pass can run between two
// verbs, so "a write re-checks N nodes" is read as a difference of the total).
// The law guarded:
//   * an idle pass visits 0 nodes, however big the scene;
//   * a move re-checks the moved node (and its subtree) and nothing else;
//   * a material edit re-checks the nodes wearing it; an add, the added node;
//   * an unshadowed lamp finds the leak a move makes, and the line stays live;
//   * a line cleared from outside is re-raised by the next pass;
//   * a deleted lamp's line goes on the next pass, another lamp or not.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function leaksOf(r) { return r.list.filter(function (i) { return i.kind === "shadow.leak"; }); }
function visited() { return editor.checkScene().walked.visited; }

project.create("Idle issues " + Date.now());
var cubes = [];
for (var b = 0; b < 4; ++b)
    cubes = cubes.concat(scene.addPrimitive("cube", { count: 250, position: { x: 30 + b * 4, y: 0, z: 0 } }));
assert(cubes.length === 1000, "a thousand cubes in the scene");

var v0 = visited();
assert(v0 >= 1000, "the cubes' arrival was checked (" + v0 + " node checks so far)");
for (var i = 0; i < 5; ++i) {
    var idle = editor.checkScene();
    assert(idle.walked.nodes === 0 && !idle.walked.full && !idle.walked.leak,
           "idle pass " + (i + 1) + " over 1,000 cubes visits 0 nodes");
}
assert(visited() === v0, "five idle passes checked nothing");

// A MOVE re-checks the moved cube, alone.
node.transform(cubes[3], { position: { x: 2, y: 0, z: 2 } });
var v1 = visited();
assert(v1 - v0 === 1, "a move re-checks the moved node and nothing else (" + (v1 - v0) + ")");
assert(editor.checkScene().walked.nodes === 0, "...and the pass after it visits 0 nodes");

// AN UNSHADOWED LAMP: it is checked, finds nothing far away, then finds the
// leak a move makes.
var lamp = scene.addLight("point", { position: { x: 100, y: 5, z: 100 }, name: "Fill" });
node.setProperty(lamp, "distance", 3);
node.setProperty(lamp, "shadowMapType", 0);   // 0 = off
assert(leaksOf(editor.checkScene()).length === 0,
       "an unshadowed lamp far from every cube leaks through nothing");
var rest = editor.checkScene();
assert(!rest.walked.leak && rest.walked.nodes === 0, "the next idle pass visits nothing");
var v2 = visited();
node.transform(cubes[0], { position: { x: 100, y: 3, z: 100.5 } });
var leaks = leaksOf(editor.checkScene());
assert(visited() - v2 === 1, "the move under the lamp re-checks one node");
assert(leaks.length === 1 && leaks[0].node === lamp, "...and finds the leak it made");
var still = editor.checkScene();
assert(still.walked.nodes === 0 && leaksOf(still).length === 1,
       "an idle pass visits nothing and keeps the line live");
assert(editor.clearIssue(leaks[0].id), "the line cleared from outside");
assert(leaksOf(editor.checkScene()).length === 1,
       "...is re-raised by the next pass while the condition holds");

// A DELETED LAMP TAKES ITS LINE WITH IT, even while another unshadowed lamp
// keeps the leak check alive (the read's case: a lamp is no check's node).
var other = scene.addLight("point", { position: { x: -100, y: 5, z: -100 }, name: "Other fill" });
node.setProperty(other, "distance", 3);
node.setProperty(other, "shadowMapType", 0);
assert(leaksOf(editor.checkScene()).length === 1, "a second unshadowed lamp far from everything adds no line");
assert(node.remove(lamp), "the leaking lamp deleted");
assert(leaksOf(editor.checkScene()).length === 0, "...its line is gone on the next pass");

// A MATERIAL EDIT re-checks the node wearing it.
var v3 = visited();
material.set(cubes[7], { roughness: 0.25 });
var v4 = visited();
assert(v4 - v3 >= 1 && v4 - v3 <= 2, "a material edit re-checks its wearer (" + (v4 - v3) + ")");
assert(editor.checkScene().walked.nodes === 0, "...and the pass after it visits nothing");

// A NODE ADDED is checked, not the scene.
scene.addPrimitive("sphere", { position: { x: -3, y: 0, z: 0 } });
var v5 = visited();
assert(v5 - v4 >= 1 && v5 - v4 <= 3, "a node added is checked, not the scene (" + (v5 - v4) + ")");
assert(editor.checkScene().walked.nodes === 0, "...and the pass after it visits nothing");
console.log("SCENE_ISSUE_IDLE PASS");
