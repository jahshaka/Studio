// capture.record_basic — THE VERB RECORDS N FRAMES (VIDEO-REC-1; VIDEO_CAPTURE_SPEC §10).
//
// The clip is stepped on the scene's own clock, one 1/60 s grid step per editor.frame, so
// the recording's time is the scene's and nothing here reads a wall clock. The claims are
// about the FILE: N frames, 1920x1080, avc1, a constant 60/1 sample table, the index BEFORE
// the media (the fast-start step), decodable by Qt; and about the status: every step was a
// frame (no hold, no drop), and the encoder Qt opened is named — a hardware H.264 one on
// this box (NVENC; the probe's finding, spikes/venc-probe/).

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

assert(project.create("capture basic " + Date.now()) !== false, "project.create");
var cube = scene.addPrimitive("cube", { position: [0, 0.5, 0] });
assert(cube !== null && cube !== false, "a cube to look at");
editor.setCamera({ position: { x: 3, y: 2, z: 4 }, lookAt: { x: 0, y: 0.5, z: 0 } });
editor.frame(4, 1 / 60);

var dir = app.dataRoot().root + "/capture-basic";
var path = dir + "/basic.mp4";
var idle = capture.status();
assert(idle.recording === false, "nothing records before start: " + idle.state);

var st = capture.start({ path: path });
assert(st !== null, "capture.start answers a status");
assert(st.state === "recording" && st.recording === true, "recording");
assert(st.width === 1920 && st.height === 1080 && st.fps === 60, "1920x1080 at 60");
assert(st.helpers === false, "scene only by default");
assert(capture.start({ path: dir + "/second.mp4" }) === null, "a second recording is refused");
console.log("second start refused: " + app.lastError());

var N = 90;
var guard = 0;
while (capture.status().armed < N && guard++ < 400) editor.frame(1, 1 / 60);
var mid = capture.status();
console.log("before stop: " + JSON.stringify(mid));
assert(mid.armed === N, "N frames armed on N grid steps after the warm-up");
assert(mid.encoder.length > 0, "the encoder is named while recording: " + mid.encoder);

var stopped = capture.stop({ wait: true, timeoutMs: 30000 });
assert(stopped !== null, "capture.stop answers a status");
var done = capture.status();
console.log("done: " + JSON.stringify(done));
assert(done.state === "done", "the file is finished: " + done.state + " " + (done.error || ""));
assert(done.path === path, "at the asked path");
assert(done.frames === N, "N video frames: " + done.frames);
assert(done.held === 0, "no held frame on a one-step-a-frame clock");
assert(done.dropped === 0 && done.encoderDropped === 0, "nothing dropped");
assert(done.encoder.indexOf("h264") === 0, "an H.264 encoder: " + done.encoder);
assert(done.hardwareEncoder === true, "Qt opened a HARDWARE encoder: " + done.encoder);
assert(capture.stop() === null, "stop with nothing recording refuses");

var f = capture.inspect(path, { decode: true, timeoutMs: 20000 });
console.log("inspect: " + JSON.stringify(f));
assert(f.ok === true, "the MP4 parses: " + (f.error || ""));
assert(f.fastStart === true && f.moovOffset < f.mdatOffset, "fast-start: moov at " + f.moovOffset +
       " before mdat at " + f.mdatOffset);
assert(f.codec === "avc1", "H.264 (avc1)");
assert(f.width === 1920 && f.height === 1080, "1920x1080 in the sample entry");
assert(f.frames === N, "N samples in the file: " + f.frames);
assert(f.constantRate === true && Math.abs(f.fps - 60) < 1e-9,
       "a constant 60/1: " + f.timescale + "/" + f.sampleDelta + " (the muxer's last sample: " + f.lastDelta + ")");
assert(f.lastDelta === f.sampleDelta, "the last frame lasts one step too (the timing is closed)");
assert(Math.abs(f.presentationMs - N * 1000 / 60) < 1, "the movie plays N/60 s: " + f.presentationMs + " ms");
assert(f.duration === N * f.sampleDelta, "the media is N steps long: " + f.duration);
assert(f.keyframes >= 1, "at least one keyframe: " + f.keyframes);
assert(f.decoded.width === 1920 && f.decoded.height === 1080, "Qt decodes 1920x1080 frames");
assert(f.decoded.frames === N, "Qt's decoder delivers all N frames: " + f.decoded.frames);
console.log("decoded " + f.decoded.frames + " of " + N + " frames, first frame mean " +
            JSON.stringify(f.decoded.mean));
assert(capture.inspect(path + ".partial").ok === false, "no fast-start partial is left behind");
var stray = capture.inspect(dir + "/.basic.recording.mp4");
assert(stray.ok === false, "the encoder's raw file is gone");

// The last frame is the picture: not empty, not the clear colour alone.
assert(capture.lastFrame(dir + "/last.png") === true, "the last frame is readable");
console.log("capture.record_basic: PASS");
