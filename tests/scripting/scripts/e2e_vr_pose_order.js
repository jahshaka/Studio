// vr.pose_order — THE POSE BEFORE THE HOST'S VR STEP (lane VR-REORDER-1;
// SPECS/ONE_PICTURE_SPEC.md A8, the deep audit's integration V1).
//
// THE CONTRACT, AS A NUMBER: the pose serial the VR interaction step read
// (`vr.state().stepPoseSerial` — the grabs, the gizmo drag, the ray and
// teleport all act on that pose) equals the serial the frame that follows drew
// the eyes with (`renderedPoseSerial`). When the step reads the pose located
// for the PREVIOUS frame — the order before the lane, where xrWaitFrame and the
// locate ran inside renderOneFrame, after every host step — the two are off by
// one on every frame, and everything held, dragged or pointed at is drawn with
// the hand of one frame ago beside a wand placed with this one's.
//
// THE RENDER LOOP IS LIVE (`--script-live`, via the runner's events mode): the
// interaction is stepped by the DRIVER's tick, the path a headset runs, not by
// `vr.step()`. Each `vr.state()` is one verb on the UI thread, so it runs
// BETWEEN two ticks and reads both serials of the same tick. Monado's simulated
// rig locates real poses every frame (no injection: an injected sample would
// stand the driver's step down).
//
// FRAMES, NEVER WALL TIME: the loop is bounded by a count of reads, and the
// row asserts over the first 60 frames that locate a pose after the step runs.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

project.create("vr pose order " + Date.now());
scene.addPrimitive("Cube");

var a = vr.available();
assert(a.available === true, "the runtime answered: " + a.runtime);
assert(vr.begin({ mirror: "none" }) === true, "vr.begin() starts a session on the editor's scene");

// THE STEP IS RUNNING ON LOCATED POSES: the driver has ticked, the session has
// located the head, and the interaction has stepped at least once on it.
var s = vr.state();
var reads = 0;
while (!(s.head.valid && s.stepPoseSerial > 0 && s.renderedPoseSerial > 0) && reads < 2000000) {
    s = vr.state();
    ++reads;
}
assert(s.head.valid && s.stepPoseSerial > 0 && s.renderedPoseSerial > 0,
       "the driver steps the interaction on located poses (step " + s.stepPoseSerial
       + ", rendered " + s.renderedPoseSerial + ", after " + reads + " reads)");

var frames = 0, equal = 0, offs = {};
var last = s.renderedPoseSerial;
reads = 0;
while (frames < 60 && reads < 4000000) {
    s = vr.state();
    ++reads;
    if (s.renderedPoseSerial === last) continue;   // no new located frame yet
    last = s.renderedPoseSerial;
    ++frames;
    var off = s.renderedPoseSerial - s.stepPoseSerial;
    offs[off] = (offs[off] || 0) + 1;
    if (off === 0) ++equal;
    if (frames <= 5 || off !== 0)
        console.log("      frame " + frames + ": step read " + s.stepPoseSerial + ", rendered "
                    + s.renderedPoseSerial + " (located " + s.poseSerial + ")");
}
console.log("      rendered minus step-read, over " + frames + " frames: " + JSON.stringify(offs));
assert(frames === 60, "60 located frames were drawn while the step ran (" + frames + ")");
assert(equal === 60,
       "EVERY frame drew the pose the VR interaction step read (" + equal + " of 60; "
       + "the offsets were " + JSON.stringify(offs) + ")");

vr.end();
console.log("vr.pose_order: PASS");
