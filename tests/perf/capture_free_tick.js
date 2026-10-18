// perf.capture_free_tick — A CAPTURE IN FREE-RUNNING COSTS THE UI TICK NOTHING IT CAN FEEL (FRAME-MONITOR-DOOR-1).
// THE OWNER'S PATH (JAH_FREE_RUNNING=1, --script-live: the render thread draws on its own, the driver's tick is posted
// by each frame's publish — Ctrl+F4's 60-frame capture, perf.capture({frames:60})), the method of the diagnosis
// (spikes/free-running-hang/scratch/frh.js.in): 200 cubes, the script polling app.frameStats() from its worker.
//
// THE DEFECT IT GUARDS (measured on the base 3962c2f0b, 2026-10-17): the monitor's drain asked the engine for its
// records by REQUEST on every tick — a request waits for the frame in flight, the next free frame pushes one more
// record, so the drain never found the ring empty and ran its 64 iterations x 2 requests = 128 frame-waits a call:
// the tick during a capture took 3.1-4.1 s, the render thread drew ~192 frames per UI tick, the 60-frame capture
// took 214 s. THIS ROW ON THE BASE'S BINARY (2026-10-17 20:37, Xvfb :81, Debug): RED — the worst UI tick 191.75
// render-thread frames (bar 2), a script poll answered after 7,436 ms, the observed tick 9,043 ms; the capture hit the
// bound at +9 ticks / +1,536 render-thread frames, 23.9 s in (spikes/frame-monitor-door/base-cft.log).
//
// THE BARS, IN FRAMES (the engine has no wall clock; the clock below is printed, never asserted):
//  * every UI tick during the capture lasts at most ONE FRAME BUDGET: the render thread drew at most 2 frames per tick
//    (the frame in flight + one) — a tick that blocks for k frames shows k frames drawn beside it;
//  * the capture's 60 frames end within 120 ticks and within 120 render-thread frames;
//  * the bundle holds EXACTLY the 60 records (the window counts the engine's frames, never the ticks), and the engine
//    dropped no record and no event;
//  * NO REQUEST ON THE TICK PATH: the host's engine requests (app.frameStats().submit.requests) do not move across the
//    capture's ticks — the capture's whole delta is its start's two (the start snapshot, the start applied) and its
//    close's two (the end snapshot, the stop applied), nothing else. A request back in the drain would add one a tick.
// The tick that ENDS the capture runs finish() (the end snapshot, the stop, the bundle written) — the capture's own
// writing, reported on its own line, outside "during".
function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function J(x) { return JSON.stringify(x); }
var DT = 1 / 60, N = 200, SIDE = 45, POLL_GUARD = 2000000;
if (project.create("capture free tick " + Date.now(), { template: "empty" }) === false)
    throw new Error("project.create failed: " + app.lastError());
if (app.columns().space !== "editor") app.space("editor");
scene.addPrimitive("plane", { scale: { x: 150, y: 1, z: 150 } });
scene.addLight("directional", { position: { x: 2, y: 8, z: 3 } });
var group = scene.addEmpty({ position: { x: 0, y: 0, z: 0 } });
var ids = [];
for (var b = 0; b < N / 100; ++b)
    ids = ids.concat(scene.addPrimitive("cube", { position: { x: 0, y: 0.5, z: 0 }, scale: { x: 0.5, y: 0.5, z: 0.5 },
                                                  parent: group, count: 100 }));
for (var i = 0; i < ids.length; ++i)
    node.transform(ids[i], { position: { x: (i % SIDE) * 1.2 - SIDE * 0.6, y: 0.5, z: Math.floor(i / SIDE) * 1.2 - SIDE * 0.6 } });
editor.setCamera({ position: { x: 0, y: 40, z: 90 }, lookAt: { x: 0, y: 0, z: 0 } });
editor.frame(60, DT);
assert(ids.length === N, N + " cubes");

// THE DRIVER TICKS FREE before the capture: 60 ticks, counted.
var w0 = app.frameStats(), polls = 0;
while (app.frameStats().ticks - w0.ticks < 60 && ++polls < POLL_GUARD) {}
var w1 = app.frameStats();
assert(w1.ticks - w0.ticks >= 60, "the driver ticks free-running before the capture (" + (w1.ticks - w0.ticks) + " ticks)");
console.log("before the capture: " + (w1.rendered - w0.rendered) + " render-thread frames over " + (w1.ticks - w0.ticks) +
            " ticks");

var q0 = app.frameStats().submit.requests;
var r = perf.capture({ frames: 60, label: "capture-free-tick" });
assert(r.started === true, "a 60-frame capture starts (" + J(r) + ")");
var s0 = app.frameStats(), t0 = Date.now();
var qStart = s0.submit.requests - q0, qDuring = 0, qLast = s0.submit.requests;
var last = s0, lastAt = t0, worstPerTick = 0, worstTickMs = 0, worstPollMs = 0, endLine = "", over = false;
var obs = 0;
polls = 0;
for (;;) {
    var p0 = Date.now();
    var s = app.frameStats();
    var pollMs = Date.now() - p0;
    var recording = perf.status().recording;
    if (s.ticks !== last.ticks) {
        var dTicks = s.ticks - last.ticks, dFrames = s.rendered - last.rendered, now = Date.now();
        var perTick = dFrames / dTicks, tickMs = (now - lastAt) / dTicks;
        if (recording) {
            ++obs;
            if (perTick > worstPerTick) worstPerTick = perTick;
            if (tickMs > worstTickMs) worstTickMs = tickMs;
            if (pollMs > worstPollMs) worstPollMs = pollMs;
        } else {
            endLine = "the capture's last tick (finish: the end snapshot, the stop, the bundle): +" + dTicks + " ticks, " +
                      dFrames + " frames, " + (now - lastAt) + " ms";
        }
        last = s; lastAt = now;
    }
    if (recording) { qDuring += s.submit.requests - qLast; qLast = s.submit.requests; }
    if (!recording) break;
    // THE BOUND IS THE BAR, IN FRAMES: past it the capture failed — stop it and say so.
    if (s.ticks - s0.ticks > 120 || s.rendered - s0.rendered > 120 || ++polls > POLL_GUARD) { over = true; break; }
}
var t1 = Date.now(), s1 = app.frameStats();
var qClose = s1.submit.requests - qLast;
if (perf.status().recording) perf.stop();
var st = perf.status();
console.log("during the capture: " + obs + " tick observations; worst " + worstPerTick.toFixed(2) +
            " render-thread frames per UI tick; worst tick " + worstTickMs.toFixed(1) + " ms; worst poll " +
            worstPollMs + " ms (printed, never asserted)");
if (endLine) console.log(endLine);
console.log("the host's engine requests: the start " + qStart + ", the capture's ticks " + qDuring + ", the close " + qClose);
console.log("the capture: +" + (s1.ticks - s0.ticks) + " ticks, +" + (s1.rendered - s0.rendered) + " render-thread frames, " +
            (t1 - t0) + " ms; status " + J({ frames: st.frames, engineFramesDropped: st.engineFramesDropped,
                                             engineEventsDropped: st.engineEventsDropped, bundle: st.lastBundle }));
assert(worstPerTick <= 2, "every UI tick during the capture lasts at most one frame budget (worst " +
       worstPerTick.toFixed(2) + " render-thread frames per tick, bar 2)");
assert(!over && s1.ticks - s0.ticks <= 120, "the capture's 60 frames end within 120 ticks (+" + (s1.ticks - s0.ticks) + ")");
assert(s1.rendered - s0.rendered <= 120,
       "...and within 120 render-thread frames (+" + (s1.rendered - s0.rendered) + ")");
assert(st.frames === 60, "the bundle holds EXACTLY the capture's 60 records (" + st.frames + ")");
assert(qDuring === 0, "no request on the tick path: the capture's ticks made " + qDuring + " engine request(s)");
assert(qStart === 2 && qClose === 2, "the capture's requests are its start's two and its close's two (" + qStart + ", " +
       qClose + ")");
assert(st.engineFramesDropped === 0 && st.engineEventsDropped === 0,
       "the engine dropped no record and no event (" + st.engineFramesDropped + ", " + st.engineEventsDropped + ")");
console.log("capture_free_tick: PASS");
