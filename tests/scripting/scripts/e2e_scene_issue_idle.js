// scripting.e2e.scene_issue_idle — AN IDLE EDITOR WALKS NOTHING (BISTRO-ENGINE-1
// stage 5, finding 14).
//
// The editor runs the scene checker once a second (SceneIssueWatch). Its two
// node-walking conditions — `shadow.leak` (a box per solid node) and
// `texture.missing` (every mesh's material) — used to walk the whole scene on
// every pass, changed or not: ~0.2 s of UI thread per second at 6,000 meshes in
// a Debug build, which was the owner's Bistro session (218 heartbeat lines of
// ~430 ms, once a second, with nothing moving). The walks now follow CHANGE:
// each runs only when what it reads can have moved.
//
// COUNTS, NEVER TIME: `editor.checkScene()` IS the timer's pass (one call, one
// pass) and reports how many nodes that pass visited. The law guarded:
//   * an idle pass visits 0 nodes, however big the scene;
//   * a transform write does not re-walk the textures (a move cannot unbind a
//     texture) and, with no unshadowed lamp, walks nothing at all;
//   * with an unshadowed lamp the leak walk follows transforms (it is about
//     where things are) — and finds the leak a move creates;
//   * a material edit re-walks the textures; a node added re-walks them;
//   * a line cleared from outside is re-raised by the next pass (the clock's
//     old behaviour, kept).

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function leaksOf(r) { return r.list.filter(function (i) { return i.kind === "shadow.leak"; }); }

project.create("Idle issues " + Date.now());
var cubes = [];
for (var b = 0; b < 4; ++b) {
    var ids = scene.addPrimitive("cube", { count: 250, position: { x: 30 + b * 4, y: 0, z: 0 } });
    cubes = cubes.concat(ids);
}
assert(cubes.length === 1000, "a thousand cubes in the scene");

// THE EDITOR'S OWN 1 Hz PASS RUNS TOO (a verb that pumps the event loop lets it
// in), so "a change was walked" is read from the running total `walks`, which
// counts every pass's walk whoever made it; "an idle pass walks nothing" is read
// from the pass this verb itself makes.
function walks() { return editor.checkScene().walked.walks; }
var settled = editor.checkScene();   // whatever the arrival owed is walked here at the latest
assert(settled.walked.walks >= 1, "the cubes' arrival was walked (" + settled.walked.walks + " walk(s))");

var walksBefore = walks();
for (var i = 0; i < 5; ++i) {
    var idle = editor.checkScene();
    assert(idle.walked.nodes === 0 && !idle.walked.textures && !idle.walked.leak,
           "idle pass " + (i + 1) + " over 1,000 cubes visits 0 nodes");
}
assert(walks() === walksBefore, "five idle passes added no walk");

// A MOVE: no texture walk, and with no lamp to leak, nothing at all.
node.transform(cubes[3], { position: { x: 2, y: 0, z: 2 } });
var moved = editor.checkScene();
assert(!moved.walked.textures, "a transform write does not re-walk the textures");
assert(moved.walked.nodes === 0, "...and with no unshadowed lamp the pass visits 0 nodes");
assert(walks() === walksBefore, "...so the move added no walk at all");

// AN UNSHADOWED LAMP: the leak walk runs, then rests, then follows a move.
var lamp = scene.addLight("point", { position: { x: 100, y: 5, z: 100 }, name: "Fill" });
node.setProperty(lamp, "distance", 3);
node.setProperty(lamp, "shadowMapType", 0);   // 0 = off
var w0 = walks();
assert(w0 > walksBefore, "an unshadowed lamp is walked");
assert(leaksOf(editor.checkScene()).length === 0,
       "...and the lamp, far from every cube, leaks through nothing");
var rest = editor.checkScene();
assert(!rest.walked.leak && rest.walked.nodes === 0, "the next idle pass walks nothing");
node.transform(cubes[0], { position: { x: 100, y: 3, z: 100.5 } });
var leakMove = editor.checkScene();
var leaks = leaksOf(leakMove);
assert(walks() > w0, "a move re-walks the leak (the boxes moved)");
assert(leaks.length === 1 && leaks[0].node === lamp, "...and finds the leak the move made");
var still = editor.checkScene();
assert(still.walked.nodes === 0 && leaksOf(still).length === 1,
       "an idle pass walks nothing and keeps the line live");
assert(editor.clearIssue(leaks[0].id), "the line cleared from outside");
var back = editor.checkScene();
assert(back.walked.leak && leaksOf(back).length === 1,
       "...is re-raised by the next pass while the condition holds");

// A MATERIAL EDIT re-walks the textures (and not the leak: nothing moved).
var w1 = walks();
material.set(cubes[7], { roughness: 0.25 });
var edited = editor.checkScene();
assert(walks() > w1, "a material edit re-walks the textures");
assert(editor.checkScene().walked.nodes === 0, "...and the pass after it walks nothing");

// A NODE ADDED re-walks them too.
var w2 = walks();
scene.addPrimitive("sphere", { position: { x: -3, y: 0, z: 0 } });
editor.checkScene();
assert(walks() > w2, "a node added re-walks the textures");
assert(editor.checkScene().walked.nodes === 0, "...and the pass after it walks nothing");
console.log("SCENE_ISSUE_IDLE PASS");
