// log.perf — the periodic performance sampler (SESSION_LOG_SPEC §10 phase 4
// gate), against the real binary with the render loop running.
//
// THE POINT OF THE WHOLE PROGRAM. Every number in a perf line is already a
// registry verb; what did not exist was anything that WROTE THEM DOWN over
// time. So the assertions here are about the series, not about magnitudes:
//
//   * with the sampler at 1 s, N seconds of rendering produce >= N lines;
//   * the counters in them are monotonically NON-DECREASING (they are
//     cumulative — a line where `rendered` went backwards means we are reading
//     a different loop, or resetting something we should not);
//   * with the sampler at 0, none;
//   * the sampler adds no measurable frame cost.
//
// Magnitudes are deliberately NOT pinned: this runs on a real GPU here and on
// lavapipe elsewhere, and a suite that asserts "58 fps" fails on the CI box for
// being right.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

// ---------------------------------------------------------------------------
// Phase A — shape and defaults.
var p = log.perf();
assert(typeof p.seconds === "number", "log.perf() reports its interval (" + p.seconds + " s)");
assert(typeof p.running === "boolean", "log.perf() reports whether it is running");
assert(p.defaultSeconds === 60 || p.defaultSeconds === 300,
       "the build's default interval is 60 (dev) or 300 (release): " + p.defaultSeconds);
// The sampler is ON BY DEFAULT — a sampler a user has to turn on has already
// missed the session that needed it. Gated as the shipped default rather than
// as the live state, because a settings file may legitimately say otherwise.
assert(p.defaultSeconds > 0, "the shipped default is a positive interval (the sampler is on)");
assert(p.running === (p.seconds > 0), "running iff the interval is positive");

// ---------------------------------------------------------------------------
// Phase B — a scene to actually render.
var guid = project.create("Log Perf " + Date.now());
assert(guid.length > 10, "project.create -> " + guid);
scene.addPrimitive("Cube");
scene.addPrimitive("Sphere");
editor.frame(10);

// ---------------------------------------------------------------------------
// Phase C — the line itself, on demand.
var marker = log.mark("log.perf: on-demand sample");
var line = log.sample();
assert(typeof line === "string" && line.length > 20, "log.sample() returned a line: " + line);
assert(/fps [0-9.]+/.test(line), "the line carries fps");
assert(/work [0-9.]+ms/.test(line), "the line carries the honest work-per-tick figure");
assert(/worst [0-9.]+ms/.test(line), "the line carries the worst tick");
assert(/slow \d+/.test(line), "the line carries the slow-frame count");
assert(/rendered \d+ skipped \d+/.test(line), "the line carries the loop's own counters");
assert(/engineErrors \d+/.test(line), "the line carries the engine-error count");
assert(/rss (\d+MB|n\/a)/.test(line), "the line carries resident memory, or says n/a honestly");
assert(/since-start \d+m/.test(line), "the line says how far into the session it is");

var recorded = log.since(marker, {});
var perfLines = recorded.filter(function (r) { return /\]perf: /.test(r); });
assert(perfLines.length >= 1, "the sample reached the log under the `perf` category");

// ---------------------------------------------------------------------------
// Phase D — THE SERIES. At 1 s, rendering for a few seconds must produce
// several lines whose cumulative counters never go backwards.
log.perf(1);
assert(log.perf().seconds === 1, "log.perf(1) re-times the sampler");
assert(log.perf().running === true, "and it is running");

// A QTimer only fires while an EVENT LOOP is turning, and a --script run does
// not turn one (editor.frame steps frames synchronously and never calls
// processEvents). So the TIMER half of the gate belongs to the MCP-driven part
// of log_perf.sh, which leaves the app running and waits in real time. What is
// asserted here is the SERIES SHAPE, over samples taken on demand — the same
// records the timer would produce, in the same order.
var seriesMark = log.mark("log.perf: the series");
var series = [];
for (var i = 0; i < 4; ++i) {
    editor.frame(20);
    series.push(log.sample());
}
log.flush();

var logged = log.since(seriesMark, {}).filter(function (r) { return /\]perf: /.test(r); });
assert(logged.length >= series.length,
       "every sample reached the log (" + logged.length + " >= " + series.length + ")");

function num(re, s) { var m = re.exec(s); return m ? parseFloat(m[1]) : NaN; }
var prevRendered = -1, prevSlow = -1, monotonic = true;
for (var s2 = 0; s2 < series.length; ++s2) {
    var rendered = num(/rendered (\d+)/, series[s2]);
    var slow = num(/slow (\d+)/, series[s2]);
    if (rendered < prevRendered || slow < prevSlow) monotonic = false;
    prevRendered = rendered;
    prevSlow = slow;
}
assert(monotonic, "the cumulative counters never go backwards across the series");

// PLAUSIBILITY, not magnitude.
for (var v = 0; v < series.length; ++v) {
    var work = num(/work ([0-9.]+)ms/, series[v]);
    assert(!isNaN(work) && work >= 0 && work < 60000,
           "work is a plausible millisecond figure (" + work + ")");
}

// ---------------------------------------------------------------------------
// Phase E — 0 means OFF. (That it is also SILENT with a running event loop is
// the shell half's assertion; here it is the state that is gated.)
log.perf(0);
assert(log.perf().running === false, "log.perf(0) stops the sampler");
assert(log.perf().seconds === 0, "and reports the interval as 0");
log.perf(1);
assert(log.perf().running === true, "a positive interval starts it again");

// ---------------------------------------------------------------------------
// Phase F — the sampler costs nothing measurable on the frame path.
//
// It is a TIMER reading counters, so this is close to a tautology by
// construction — which is exactly why it is worth asserting: the day somebody
// reimplements it as a frame hook, this is what says so.
var before = app.frameStats().workMs;
log.perf(1);
editor.frame(60);
for (var w = 0; w < 3; ++w) { log.sample(); editor.frame(20); }
var after = app.frameStats().workMs;
console.log("workMs before " + before.toFixed(2) + " after " + after.toFixed(2));
assert(after < before + 20.0 || before === 0,
       "the sampler did not move the frame-work average outside noise (" +
       before.toFixed(2) + " -> " + after.toFixed(2) + " ms)");

log.perf(0);
log.write("app", "display", "e2e: LOG PERF COMPLETE");
log.flush();
console.log("log.perf: all checks passed");
