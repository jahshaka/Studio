// capture.abandon — QUIT WHILE SAVING LOSES NOTHING (VIDEO-REC-1 fix round, defect 1).
//
// The module's shutdown (and the recorder's destructor) stop a fast-start step that is still
// running; the encoder's own file — complete, playable, its index at the end — must then be at
// the FINAL path, never only as the hidden raw file. `fault: "slowSave"` holds the save until it
// is cancelled (a multi-GB file at quit), and capture.abandon() runs exactly the shutdown's call.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

assert(project.create("capture abandon " + Date.now()) !== false, "project.create");
scene.addPrimitive("cube", { position: [0, 0.5, 0] });
editor.setCamera({ position: { x: 3, y: 2, z: 4 }, lookAt: { x: 0, y: 0.5, z: 0 } });
editor.frame(2, 1 / 60);
var dir = app.dataRoot().root + "/capture-abandon";
var path = dir + "/quit.mp4";
var N = 30;
assert(capture.start({ path: path, fault: "slowSave" }) !== null, "recording");
var guard = 0;
while (capture.status().armed < N && guard++ < 200) editor.frame(1, 1 / 60);
capture.stop();
guard = 0;
while (capture.status().state === "finishing" && guard++ < 100) {
    capture.wait(100);            // the encoder's stop; the slow save then holds the state here
    if (capture.inspect(dir + "/.quit.recording.mp4").ok) break;
}
capture.wait(200);
var mid = capture.status();
assert(mid.state === "finishing", "the save is still running: " + mid.state);
assert(capture.inspect(path).ok === false, "nothing at the final path yet");

var st = capture.abandon();
console.log("abandoned: " + JSON.stringify(st));
assert(st.state === "done", "the recording is DONE, not lost: " + st.state);
assert(String(st.warning).indexOf("saved without fast-start") === 0, "and says how: " + st.warning);
var f = capture.inspect(path, { decode: true, timeoutMs: 20000 });
console.log("inspect: " + JSON.stringify(f));
assert(f.ok === true && f.frames === N, "the final path holds the whole clip: " + f.frames);
assert(f.fastStart === false, "(the encoder's own shape: index at the end)");
assert(f.decoded.frames === N, "and it plays: Qt decodes all " + f.decoded.frames);
editor.frame(2, 1 / 60);
assert(capture.inspect(dir + "/.quit.recording.mp4").ok === false, "no hidden raw file is left");
console.log("capture.abandon: PASS");
