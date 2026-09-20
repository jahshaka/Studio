// scripting.e2e.render_stats — the DATA half of the engine-drawn stats overlay
// (STATS_OVERLAY_SPEC.md phase 1). API-first: the verbs and this test exist
// before any pixel of the readout, because a stats row that lies is worse than
// no stats row.
//
// TWO VERBS, and the difference between them is the whole point:
//
//   app.renderStats()  what the RENDERER measured (Ogre's FrameStats + its
//                      per-frame geometry counters). Its `fps` is a measurement
//                      of our own 16 ms QTimer, not of the renderer's headroom.
//   app.frameStats()   what the ONE render loop DID — and, new here, how long
//                      its ticks actually took (`workMs`, `worstMs`,
//                      `slowFrames`). That is the number that diagnoses
//                      anything, and it used to be measured and thrown away
//                      (enginerenderdriver.cpp discarded it below 100 ms).
//
// WHAT IS ASSERTED, deliberately narrow: shape, laziness, monotonicity and the
// invariants that must hold between fields. NOT specific magnitudes — this runs
// on a real GPU here and under lavapipe elsewhere, and a suite that pins "62
// fps" is a suite that fails on the CI box for being right.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

var RENDER_KEYS = ["metricsRecording", "fps", "frameMs", "lastMs", "p95Ms", "p99Ms",
                   "bestMs", "worstMs", "draws", "batches", "sceneTriangles",
                   "submittedTriangles", "vertices", "instances"];
var FRAME_KEYS = ["running", "intervalMs", "ticks", "rendered", "drawing", "fpsDrawn",
                  "slowFramesLastMinute", "workMs", "worstMs", "slowFrames", "enabledViews"];

// ---------------------------------------------------------------------------
// Phase A — shape. Every advertised key is present and is a number/bool.
var rs = app.renderStats();
for (var i = 0; i < RENDER_KEYS.length; ++i) {
    var k = RENDER_KEYS[i];
    assert(rs[k] !== undefined, "app.renderStats has '" + k + "'");
}
assert(typeof rs.metricsRecording === "boolean", "metricsRecording is a bool");
for (var j = 1; j < RENDER_KEYS.length; ++j)
    assert(typeof rs[RENDER_KEYS[j]] === "number", RENDER_KEYS[j] + " is a number");
// perPass is a LIST, and it is empty unless the render monitor is capturing —
// the counters cost listeners on every workspace, so nothing pays for them
// while nobody is looking.
assert(rs.perPass !== undefined && rs.perPass.length !== undefined,
    "app.renderStats has a perPass list");
assert(rs.perPass.length === 0, "…empty with no capture running (" + rs.perPass.length + ")");
// THE DELETED COUNTER. `skipped` was a lifetime tick total shown on the F3
// readout as if it were dropped frames (owner review 2026-09-18) — it is gone,
// and `ticks - rendered` is the same number for anyone who wants it.
assert(app.frameStats().skipped === undefined,
    "the lifetime 'skipped' counter is gone from app.frameStats");
assert(rs.triangles === undefined,
    "…and the unqualified 'triangles' key is gone: it is submittedTriangles now");

var fs = app.frameStats();
for (var f = 0; f < FRAME_KEYS.length; ++f)
    assert(fs[FRAME_KEYS[f]] !== undefined, "app.frameStats has '" + FRAME_KEYS[f] + "'");
assert(typeof fs.drawing === "boolean", "frameStats.drawing is a bool (the idle/drawing state)");
assert(typeof fs.fpsDrawn === "number", "frameStats.fpsDrawn is a number");
assert(typeof fs.slowFramesLastMinute === "number", "frameStats.slowFramesLastMinute is a number");
assert(typeof fs.workMs === "number", "frameStats.workMs is a number");
assert(typeof fs.worstMs === "number", "frameStats.worstMs is a number");
assert(typeof fs.slowFrames === "number", "frameStats.slowFrames is a number");

// ---------------------------------------------------------------------------
// Phase B — the geometry counters are LAZY. Reading the verb is what switches
// recording on (it costs integer adds per draw call and is off by default in
// Ogre), so the honest contract is: the first read may report
// metricsRecording=false with zeros, and after that it is on and stays on.
var project_name = "Render Stats " + Date.now();
var guid = project.create(project_name);
assert(guid.length > 10, "project.create -> " + guid);
scene.addPrimitive("Cube");
editor.frame(4);

var live = app.renderStats();
assert(live.metricsRecording === true,
    "reading renderStats enables geometry recording (now " + live.metricsRecording + ")");
assert(live.draws > 0, "a frame with a cube in it draws something (" + live.draws + ")");
assert(live.submittedTriangles > 0, "…and it has triangles (" + live.submittedTriangles + ")");
assert(live.vertices >= live.submittedTriangles,
    "vertices >= triangles for indexed geometry (" + live.vertices + " / " +
    live.submittedTriangles + ")");

// ---------------------------------------------------------------------------
// Phase C — the timings are real and internally consistent.
assert(live.frameMs > 0, "rolling frame time is a real number (" + live.frameMs + " ms)");
assert(live.fps > 0, "…and so is the fps derived from it (" + live.fps + ")");
// fps and frameMs are two views of ONE number: 1000/frameMs must be fps.
assert(Math.abs(1000.0 / live.frameMs - live.fps) < 0.5,
    "fps and frameMs describe the same sample (" + live.fps + " vs " +
    (1000.0 / live.frameMs) + ")");
assert(live.worstMs >= live.bestMs,
    "worst is not better than best (" + live.worstMs + " >= " + live.bestMs + ")");
assert(live.p99Ms >= 0 && live.p95Ms >= 0, "percentiles are non-negative");

// ---------------------------------------------------------------------------
// Phase D — the loop counters MOVE, and monotonically. editor.frame() steps the
// engine directly and deliberately bypasses the driver, so `rendered` must NOT
// move here — that contract is already documented on app.frameStats and this is
// what keeps it true.
var before = app.frameStats();
editor.frame(10);
var after = app.frameStats();
assert(after.ticks >= before.ticks, "ticks never goes backwards");
assert(after.rendered >= before.rendered, "rendered never goes backwards");
assert(after.ticks - after.rendered >= before.ticks - before.rendered,
       "the idle-tick count (ticks - rendered, the old 'skipped') never goes backwards");
assert(after.slowFrames >= before.slowFrames, "slowFrames never goes backwards");
assert(after.worstMs >= before.worstMs, "worstMs never goes backwards");
assert(after.workMs >= 0, "workMs is non-negative (" + after.workMs + " ms)");
// The renderer's own counters DO move for stepped frames — that is the
// difference between "the loop" and "the renderer", and it is why both verbs
// exist rather than one.
var rs2 = app.renderStats();
assert(rs2.draws > 0, "stepped frames still reach the renderer (" + rs2.draws + " draws)");

// ---------------------------------------------------------------------------
// Phase F — THE SCENE'S OWN TRIANGLES (owner review 2026-09-18, answer Q3).
//
// The owner opened an empty new world, read "4,611 triangles" and asked what
// they were. This phase is the answer, asserted: `sceneTriangles` counts what
// is IN the scene — the authored faces of the visible mesh nodes, once each —
// while `submittedTriangles` counts what the renderer handed the GPU across
// every pass. The first must not move when a helper or a render pass does.
var freshName = "Scene Triangles " + Date.now();
assert(project.create(freshName).length > 10, "a fresh default world: " + freshName);
editor.frame(4);

// THE DEFAULT GROUND, EXACTLY. It is a 33x33 lattice (it exists so vertex-rate
// fog and lighting have somewhere to happen) and it is the only mesh in a new
// world, so the number is the ground's own count and nothing else.
var GROUND_TRIS = 2178;
var fresh = app.renderStats();
assert(fresh.sceneTriangles === GROUND_TRIS,
    "a new world reads the ground's own triangles exactly (" + fresh.sceneTriangles +
    " vs " + GROUND_TRIS + ")");
assert(fresh.submittedTriangles > fresh.sceneTriangles,
    "…while the renderer submits MORE than that — the SSR depth pre-pass draws the same " +
    "ground a second time, plus the horizon, the icons, the sky and the post quads (" +
    fresh.submittedTriangles + ")");

// A CUBE ADDS EXACTLY A CUBE.
var cubeId = scene.addPrimitive("Cube");
editor.frame(2);
var withCube = app.renderStats().sceneTriangles;
var CUBE_TRIS = 12;
assert(withCube === GROUND_TRIS + CUBE_TRIS,
    "adding a cube adds exactly its own triangles (" + withCube + ")");

// DELETING IT TAKES THEM BACK OUT.
node.remove(cubeId);
editor.frame(2);
assert(app.renderStats().sceneTriangles === GROUND_TRIS,
    "removing the cube takes its triangles back out of the scene count");
// …and put it back for the helper/SSR arms below, which want something in the
// world that is not the ground.
cubeId = scene.addPrimitive("Cube");
editor.frame(2);
// (HIDING one is the same rule and is asserted on real document nodes in
// services.scene_stats instead: the outliner's eye has no verb — isVisible is
// not serialized, so node.physics deliberately refuses a key for it
// (nodeapi.cpp) — and the hidden/visible split is a document contract, not a
// renderer one.)

// HELPERS DO NOT COUNT, AND TURNING THEM OFF DOES NOT MOVE THE SCENE. Game
// view is the master switch over the grid, the light wires and the selection
// highlight; the submitted figure is allowed to move (that is what it is for),
// the scene's own count is not.
var helpersOn = app.renderStats();
assert(helpersOn.sceneTriangles === withCube, "the cube is back (" + helpersOn.sceneTriangles + ")");
editor.setOverlays({ gameView: true });
editor.frame(3);
var helpersOff = app.renderStats();
assert(helpersOff.sceneTriangles === helpersOn.sceneTriangles,
    "turning the editor helpers off does not move sceneTriangles (" +
    helpersOff.sceneTriangles + ")");
editor.setOverlays({ gameView: false });
editor.frame(3);

// A RENDER PASS DOES NOT COUNT EITHER. Screen-space reflections add a depth
// pre-pass that draws the scene's geometry a SECOND time: the GPU figure falls
// by about a ground when they go off, the scene's own count does not move at
// all. (This is the measured explanation of the owner's 4,611: 2 x 2,178 plus
// nine small draws.)
var ssrOn = app.renderStats();
world.override({ id: "ssr", value: "off" });
editor.frame(4);
var ssrOff = app.renderStats();
assert(ssrOff.sceneTriangles === ssrOn.sceneTriangles,
    "switching SSR off does not move sceneTriangles (" + ssrOff.sceneTriangles + ")");
assert(ssrOff.submittedTriangles < ssrOn.submittedTriangles,
    "…while the SUBMITTED figure falls with the pre-pass (" + ssrOn.submittedTriangles +
    " -> " + ssrOff.submittedTriangles + ")");
world.clearOverride({ id: "ssr" });
editor.frame(2);

// AN EMPTY SCENE READS ZERO. Not "about zero", not the helpers' count: the
// world has no mesh in it and the readout says so.
assert(project.create("Empty Triangles " + Date.now(), { empty: true }).length > 10,
    "an Empty scene");
editor.frame(4);
var empty = app.renderStats();
assert(empty.sceneTriangles === 0,
    "an Empty scene reads 0 scene triangles (" + empty.sceneTriangles + ")");
assert(empty.submittedTriangles > 0,
    "…while the renderer still draws the sky and the post chain (" +
    empty.submittedTriangles + ")");

// ---------------------------------------------------------------------------
// Phase E — no world, no lie. Closing the project leaves the verbs answerable:
// the loop is still there and still reports itself.
project.close();
var idle = app.frameStats();
assert(typeof idle.ticks === "number", "frameStats still answers with no world open");
var idleRender = app.renderStats();
assert(idleRender.metricsRecording === true,
    "renderStats still answers with no world open");

console.log("PASS e2e_render_stats");
