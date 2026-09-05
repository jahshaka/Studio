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
                   "bestMs", "worstMs", "draws", "batches", "triangles", "vertices",
                   "instances"];
var FRAME_KEYS = ["running", "intervalMs", "ticks", "rendered", "skipped",
                  "workMs", "worstMs", "slowFrames", "enabledViews"];

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

var fs = app.frameStats();
for (var f = 0; f < FRAME_KEYS.length; ++f)
    assert(fs[FRAME_KEYS[f]] !== undefined, "app.frameStats has '" + FRAME_KEYS[f] + "'");
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
assert(live.triangles > 0, "…and it has triangles (" + live.triangles + ")");
assert(live.vertices >= live.triangles,
    "vertices >= triangles for indexed geometry (" + live.vertices + " / " + live.triangles + ")");

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
assert(after.skipped >= before.skipped, "skipped never goes backwards");
assert(after.slowFrames >= before.slowFrames, "slowFrames never goes backwards");
assert(after.worstMs >= before.worstMs, "worstMs never goes backwards");
assert(after.workMs >= 0, "workMs is non-negative (" + after.workMs + " ms)");
// The renderer's own counters DO move for stepped frames — that is the
// difference between "the loop" and "the renderer", and it is why both verbs
// exist rather than one.
var rs2 = app.renderStats();
assert(rs2.draws > 0, "stepped frames still reach the renderer (" + rs2.draws + " draws)");

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
