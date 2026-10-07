// capture.ui — THE RECORD BUTTON (VIDEO-REC-1; owner §10.6).
//
// Press records; while recording the button is the RED dot with "m:ss" (the video's
// own elapsed time); Esc in the viewport stops it ONLY while recording; press again stops;
// once a recording is finished, the button's popup holds the file and "Open folder" and its
// tooltip names it. The button records to the default folder — ~/Videos/Jahshaka under this run's
// own fresh HOME, never the owner's.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function steps(n) { for (var i = 0; i < n; ++i) editor.frame(1, 1 / 60); }

assert(project.create("capture ui " + Date.now()) !== false, "project.create");
scene.addPrimitive("cube", { position: [0, 0.5, 0] });
editor.setCamera({ position: { x: 3, y: 2, z: 4 }, lookAt: { x: 0, y: 0.5, z: 0 } });
editor.frame(2, 1 / 60);
capture.helpers(false);

var b0 = capture.button();
console.log("idle: " + JSON.stringify(b0));
assert(b0.placed === true && b0.visible === true, "the button is placed beside the photo button");
assert(b0.text === "Record" && b0.red === false && b0.recording === false, "idle: Record, not red");
assert(b0.width >= 28, "a clickable button: " + b0.width + " px");

// ---- Esc with nothing recording keeps its meaning (nothing to stop) ----
editor.key("Esc");
assert(capture.status().recording === false, "Esc with no recording starts nothing");

// ---- press, press inside the warm-up: a quiet cancel, never the failure dialog ----
assert(capture.press() === true, "press starts a recording");
editor.frame(2, 1 / 60);
assert(capture.status().warming === true, "still in the warm-up");
assert(capture.press() === true, "a second press during the warm-up stops it");
var bc = capture.button();
var sc = capture.status();
assert(sc.state === "idle" && sc.warning === "cancelled before the first frame", "a quiet cancel: " + sc.state);
assert(bc.dialogOpen === false && bc.failure === "" && bc.red === false, "no failure dialog, not red");

// ---- press -> recording, red, elapsed ----
assert(capture.press() === true, "press starts a recording");
steps(16 + 75);   // the warm-up, then 75 frames = 1.25 s of video
var b1 = capture.button();
console.log("recording: " + JSON.stringify(b1));
assert(b1.recording === true && b1.red === true, "recording: the red dot");
assert(/^\d+:\d\d$/.test(b1.text), "the elapsed time on the button: " + b1.text);
assert(b1.text === "0:01", "1.25 s of video reads 0:01");
var st1 = capture.status();
console.log("the click's own UI-thread time: " + st1.startMs + " ms");
assert(st1.path.indexOf("/Videos/Jahshaka/") >= 0, "the default folder: " + st1.path);

// ---- Esc stops it, only while recording ----
editor.key("Esc");
var st2 = capture.status();
assert(st2.recording === false && (st2.state === "finishing" || st2.state === "done"),
       "Esc in the viewport stopped the recording: " + st2.state);
capture.wait(30000);
var done1 = capture.status();
assert(done1.state === "done", "finished: " + (done1.error || ""));
var b2 = capture.button();
console.log("after Esc: " + JSON.stringify(b2));
assert(b2.red === false && b2.text === "Record", "the button is back to Record");
assert(b2.menu.join("|").indexOf("Open folder") >= 0, "the popup offers Open folder");
editor.key("Esc");
assert(capture.status().state === "done", "a second Esc does nothing to a finished recording");

// ---- press, press: start and stop from the button ----
assert(capture.press() === true, "press starts again");
steps(16 + 30);
assert(capture.button().red === true, "red again");
assert(capture.press() === true, "press again stops");
capture.wait(30000);
var done2 = capture.status();
assert(done2.state === "done" && done2.path !== done1.path, "a second file: " + done2.path);
var b3 = capture.button();
console.log("after press: " + JSON.stringify(b3));
assert(b3.menu.join("|").indexOf(done2.path.split("/").pop()) >= 0, "the popup names the new file");
assert(b3.toolTip.indexOf(done2.path.split("/").pop()) >= 0, "and the tooltip says where it went");
assert(b3.menuOpen === false, "nothing popped up by itself");
var f = capture.inspect(done2.path);
assert(f.ok && f.fastStart && f.width === 1920, "the button's file is a fast-start 1080p MP4");
console.log("capture.ui: PASS");
