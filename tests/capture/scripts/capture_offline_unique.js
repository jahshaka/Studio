// capture.offline_unique — THE OFFLINE MODE IS A PERFECT 60 (VIDEO-REC-2; VIDEO_CAPTURE_SPEC §2.3).
//
// A bar spinning at a keyed 90 degrees a second (linear tangents) beside a spark emitter, seen
// from straight above, recorded with the editor made SLOW on purpose: every frame first blocks
// the UI thread for 50 ms (app.blockUiThread) and then draws with NO dt, so the frame hands the
// clock its WALL time — three-plus grid steps a frame.
//
//   OFFLINE: N frames in, N frames out at a constant 60/1; the scene clock advanced exactly ONE
//            step per drawn frame however long the frame took; NO two consecutive decoded frames
//            the same picture; and the bar turns 1.5 degrees (= 90 deg/s x 1/60 s) per frame,
//            read off the DECODED pixels (the principal axis of the bright pixels).
//   REAL-TIME, the same load (the control): the clock takes the wall time, so the file HOLDS
//            pictures across the steps a slow frame spanned — repeated frames, and turns of whole
//            multiples of 1.5 degrees. It proves the instrument sees what offline removes.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

assert(project.create("capture offline " + Date.now(), { template: "empty" }) !== false, "project.create (empty)");
var bar = scene.addPrimitive("cube", { position: [0, 0, 0], scale: [2.4, 0.1, 0.3] });
assert(bar !== null && bar !== false, "the bar");
// EMISSIVE on the Empty template's black: a bright bar on black, no light needed.
assert(material.set(bar, { baseColor: "#000000", emissiveColor: "#ffffff", emissiveIntensity: 1 }) === true, "a glowing bar");
var sparks = scene.addParticles("sparks", { position: [8, 0, 0] });
assert(sparks !== null && sparks !== false, "a spark emitter beside it");

// 0 -> 180 degrees about Y over 2 s, LINEAR: 90 deg/s, 1.5 deg per 1/60 s step.
anim.create(bar, "spin");
assert(anim.keyframe(bar, "rotation", 0, { x: 0, y: 0, z: 0 }) === true, "key 0");
assert(anim.keyframe(bar, "rotation", 2, { x: 0, y: 180, z: 0 }) === true, "key 2 s");
anim.setKeyTangents(bar, "rotation", 0, { left: "linear", right: "linear" });
anim.setKeyTangents(bar, "rotation", 2, { left: "linear", right: "linear" });
var a1 = anim.sample(bar, "rotation", 1 / 60), a2 = anim.sample(bar, "rotation", 2 / 60);
console.log("the key samples at 1 and 2 steps: " + JSON.stringify(a1) + " " + JSON.stringify(a2));

editor.setCamera({ position: { x: 0, y: 12, z: 0.05 }, lookAt: { x: 0, y: 0, z: 0 } });
editor.frame(8, 1 / 60);

var dir = app.dataRoot().root + "/capture-offline";
var N = 60;
var DEG = 90 / 60;
var RECT = [560, 140, 800, 800];   // the bar's 1080p neighbourhood; the sparks are to the right of it

// One recording under the load: play from t = 0, step until N frames are armed, stop, inspect.
function record(name, mode) {
    assert(editor.play() === true, name + ": play (the clock at 0)");
    var st = capture.start({ path: dir + "/" + name + ".mp4", mode: mode });
    assert(st !== null && st.mode === mode, name + ": recording " + mode + " " + app.lastError());
    var c0 = scene.clock().steps, drawn = 0, guard = 0;
    while (capture.status().armed < N && guard++ < 1200) {
        app.blockUiThread(50);
        editor.frame(1);           // NO dt: the frame hands the clock its wall time
        ++drawn;
    }
    var steps = scene.clock().steps - c0;
    var mid = capture.status();
    console.log(name + " before stop: drawn " + drawn + " frames, the clock took " + steps + " steps; " + JSON.stringify(mid));
    var done = capture.stop({ wait: true, timeoutMs: 30000 });
    assert(done !== null && capture.status().state === "done", name + ": finished " + (capture.status().error || ""));
    done = capture.status();
    editor.stop();
    var f = capture.inspect(done.path, { perFrame: { rect: RECT, threshold: 128 }, timeoutMs: 60000 });
    var rows = f.decoded.perFrame;
    var diffs = [], turns = [];
    for (var i = 1; i < rows.length; ++i) {
        diffs.push(rows[i].diff);
        var d = rows[i].angle - rows[i - 1].angle;
        while (d > 90) d -= 180;
        while (d < -90) d += 180;
        turns.push(Math.abs(d));
    }
    console.log(name + " file: " + JSON.stringify({ frames: f.frames, fps: f.fps, constantRate: f.constantRate,
                decoded: f.decoded.frames, timedOut: f.decoded.timedOut }));
    console.log(name + " areas: " + rows.map(function (r) { return r.area; }).slice(0, 8).join(" ") + " ...");
    console.log(name + " diffs: " + diffs.map(function (x) { return x.toFixed(3); }).join(" "));
    console.log(name + " turns: " + turns.map(function (x) { return x.toFixed(3); }).join(" "));
    return { status: done, mid: mid, drawn: drawn, steps: steps, file: f, rows: rows, diffs: diffs, turns: turns };
}

// ---- OFFLINE ----
var off = record("offline", "offline");
assert(off.status.frames === N && off.status.held === 0, "N frames, none held: " + off.status.frames + "/" + off.status.held);
assert(off.status.dropped === 0 && off.status.encoderDropped === 0, "nothing dropped");
assert(off.steps === off.drawn, "ONE clock step per drawn frame under the load: " + off.steps + " steps / " + off.drawn + " frames");
assert(off.status.clipSeconds === N / 60, "the clip is N/60 s: " + off.status.clipSeconds);
assert(off.status.wallSeconds > off.status.clipSeconds, "slower than real time, and said so: clip " +
       off.status.clipSeconds + " s / wall " + off.status.wallSeconds + " s");
assert(off.status.worstRingWaitMs < 100, "no UI-thread wait over 100 ms on the ring: " + off.status.worstRingWaitMs + " ms (" +
       off.status.ringWaits + " waits)");
assert(off.file.frames === N && off.file.constantRate === true && Math.abs(off.file.fps - 60) < 1e-9,
       "the file: N samples at a constant 60/1");
assert(off.file.decoded.frames === N && off.rows.length === N, "N frames decoded and measured: " + off.rows.length);
var minDiff = Math.min.apply(null, off.diffs);
assert(minDiff > 0.3, "NO two consecutive frames the same picture: the smallest luma change " + minDiff.toFixed(3));
var minArea = Math.min.apply(null, off.rows.map(function (x) { return x.area; }));
assert(minArea > 2000, "every frame shows the bar: at least " + minArea + " bright px in the rect");
var worst = 0;
for (var t = 0; t < off.turns.length; ++t) worst = Math.max(worst, Math.abs(off.turns[t] - DEG));
assert(worst < 0.2, "every frame turns the bar 1/60 s of its 90 deg/s: |turn - 1.5 deg| <= " + worst.toFixed(3));

// ---- REAL-TIME under the same load (the control) ----
var rt = record("realtime", "realtime");
assert(rt.status.held > 0, "real time HOLDS frames under the load: " + rt.status.held + " of " + rt.status.frames);
var repeats = 0, multiples = 0;
for (var k = 0; k < rt.diffs.length; ++k) if (rt.diffs[k] < 0.3) ++repeats;
for (var m = 0; m < rt.turns.length; ++m) if (rt.turns[m] > 2.5 * DEG) ++multiples;
console.log("real time: " + repeats + " repeated pictures, " + multiples + " jumps of several steps");
assert(repeats > 0, "the instrument sees the held frames real time writes: " + repeats);
console.log("capture.offline_unique: PASS");
