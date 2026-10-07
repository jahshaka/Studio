// capture.first_recording — THE SESSION'S FIRST RECORDING COMPILES NOTHING (VIDEO-REC-2).
//
// VIDEO-REC-1 measured a 585 ms click on the first recording of a session: the recorder's view
// (its own chain at 1080p) and the NV12 compute job compiled on the UI thread. The startup shader
// gate now draws that very view (recordingview::create, armed every frame) behind the splash, so
// the first recording's start, its warm-up and its first recorded frames compile ZERO shaders.
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
console.log("compiled " + sc0.compiledThisRun + " -> " + sc1.compiledThisRun + ", loaded " + sc0.loadedThisRun +
            " -> " + sc1.loadedThisRun + ", live compiles " + sc0.liveCompiles + " -> " + sc1.liveCompiles);
assert(capture.status().frames >= 10, "frames were recorded: " + capture.status().frames);
assert(sc1.compiledThisRun === sc0.compiledThisRun, "the first recording compiled NO shader: " +
       (sc1.compiledThisRun - sc0.compiledThisRun));
assert(sc1.liveCompiles === sc0.liveCompiles, "and no live compile was reported");
assert(capture.stop({ wait: true, timeoutMs: 30000 }) !== null && capture.status().state === "done", "finished");
console.log("capture.first_recording: PASS");
