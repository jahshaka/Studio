// capture.first_recording — THE SESSION'S FIRST RECORDING COMPILES NOTHING (VIDEO-REC-2).
//
// VIDEO-REC-1 measured a 585 ms click on the first recording of a session. VIDEO-REC-2 found its
// cause: Qt's encoder probe (the first QMediaFormat::supportedVideoCodecs(Encode) of a process,
// ~0.7 s); the recorder's view and its NV12 job compiled (cold cache) or loaded (warm) on its first
// frames too. The startup shader gate now makes the probe and draws that very view
// (recordingview::create, armed every frame) behind the splash, so the first recording's start,
// warm-up and first recorded frames compile and load ZERO shaders and the click is a lookup.
// FIRST in the pool: the claim is about the process's first recording.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

assert(project.create("capture first " + Date.now()) !== false, "project.create");
scene.addPrimitive("cube", { position: [0, 0.5, 0] });
editor.setCamera({ position: { x: 3, y: 2, z: 4 }, lookAt: { x: 0, y: 0.5, z: 0 } });
// The world's own first frames compile what they compile (not this claim): settled first.
editor.frame(30, 1 / 60);

var sc0 = app.shaderCache();
var st = capture.start({ path: app.dataRoot().root + "/capture-first/first.mp4" });
assert(st !== null, "the first recording of the process starts " + app.lastError());
console.log("the click's own UI-thread time: " + st.startMs + " ms (VIDEO-REC-1's first click: 585 ms)");
var guard = 0;
while (capture.status().frames < 10 && guard++ < 200) editor.frame(1, 1 / 60);
var sc1 = app.shaderCache();
var first = capture.status();
console.log("the first click: " + first.startMs.toFixed(1) + " ms of UI time, Qt's encoder probe " + first.probeMs.toFixed(2) + " ms");
console.log("compiled " + sc0.compiledThisRun + " -> " + sc1.compiledThisRun + ", loaded " + sc0.loadedThisRun +
            " -> " + sc1.loadedThisRun + ", live compiles " + sc0.liveCompiles + " -> " + sc1.liveCompiles);
assert(capture.status().frames >= 10, "frames were recorded: " + capture.status().frames);
assert(sc1.compiledThisRun === sc0.compiledThisRun, "the first recording compiled NO shader: " +
       (sc1.compiledThisRun - sc0.compiledThisRun));
assert(sc1.liveCompiles === sc0.liveCompiles, "and no live compile was reported");
// ...NOR LOADED ONE: the pool keeps its shader cache (--warm), where a recording view the gate
// never drew would still have to LOAD its programs (the NV12 job) on its first frames.
assert(sc1.loadedThisRun === sc0.loadedThisRun, "nor loaded one from the cache: " +
       (sc1.loadedThisRun - sc0.loadedThisRun));
// THE ENCODER PROBE (the real 585 ms): made by the startup gate, so here it is a lookup.
assert(first.probeMs < 20, "Qt's encoder probe was already made at startup: " + first.probeMs.toFixed(2) + " ms");
assert(first.startMs < 50, "the first record click costs under 50 ms of UI time: " + first.startMs.toFixed(1) + " ms");
assert(capture.stop({ wait: true, timeoutMs: 30000 }) !== null && capture.status().state === "done", "finished");
console.log("capture.first_recording: PASS");
