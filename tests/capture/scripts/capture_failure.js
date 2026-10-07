// capture.failure — EVERY FAILURE ENDS CLEANLY WITH ONE MESSAGE (VIDEO-REC-1; the VR rule).
//
// No H.264 encoder (the test instrument `fault: "noEncoder"` answers the encoder check with
// none — the path a machine without one takes), a folder that cannot be created, and an
// encoder error in the MIDDLE of a recording (`fault: "encoderError"`, the errorOccurred
// path): each refuses or stops with a message in words, publishes no file, leaves no
// recording view behind, and the next recording works.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

assert(project.create("capture failure " + Date.now()) !== false, "project.create");
scene.addPrimitive("cube", { position: [0, 0.5, 0] });
editor.frame(2, 1 / 60);
var dir = app.dataRoot().root + "/capture-failure";

// ---- no encoder ----
var r = capture.start({ path: dir + "/none.mp4", fault: "noEncoder" });
assert(r === null, "no encoder: refused, not thrown");
var why = String(app.lastError());
assert(why.indexOf("no H.264 video encoder") >= 0, "and said in words: " + why);
assert(capture.status().recording === false, "nothing records");

// ---- a folder that cannot be created ----
r = capture.start({ path: "/proc/jahshaka-capture-test/x.mp4" });
assert(r === null, "an unwritable folder: refused");
why = String(app.lastError());
assert(why.indexOf("cannot be") >= 0, "and said in words: " + why);

// ---- an encoder error mid-recording ----
r = capture.start({ path: dir + "/broken.mp4", fault: "encoderError" });
assert(r !== null, "the faulted recording starts: " + app.lastError());
var guard = 0;
while (capture.status().state === "recording" && guard++ < 300) editor.frame(1, 1 / 60);
var st = capture.status();
console.log("after the fault: " + JSON.stringify(st));
assert(st.state === "failed", "the recording failed cleanly: " + st.state);
assert(st.error.indexOf("the encoder failed") === 0, "with one message: " + st.error);
assert(capture.inspect(dir + "/broken.mp4").ok === false, "no file was published");
assert(capture.inspect(dir + "/.broken.recording.mp4").ok === false, "the raw file is gone");
var b = capture.button();
assert(b.dialogOpen === true && b.failure === st.error, "the user is told once, in the dialog");
assert(capture.dismiss() === true, "the dialog closes");
editor.frame(3, 1 / 60);   // the editor renders on

// ---- and the next recording works ----
r = capture.start({ path: dir + "/after.mp4" });
assert(r !== null, "a recording after a failure starts: " + app.lastError());
guard = 0;
while (capture.status().armed < 20 && guard++ < 200) editor.frame(1, 1 / 60);
capture.stop({ wait: true, timeoutMs: 30000 });
var ok = capture.status();
assert(ok.state === "done" && ok.frames === 20, "and finishes: " + ok.state + " " + ok.frames);
assert(capture.inspect(ok.path).ok === true, "its file is whole");
console.log("capture.failure: PASS");
