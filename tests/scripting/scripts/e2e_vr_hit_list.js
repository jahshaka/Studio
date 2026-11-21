// vr.hit_list_stereo (PHOTON-2 stage 2's tail, 1.6 A2) — THE STEREO VIEW'S HIT LIST HOLDS BOTH EYES' DEMAND.
//
// One stereo view's two eyes append to ONE hit list (OgreChain.cpp's applyStereo leaves it single-layer), so the old
// list - a fixed factor of ONE eye's target, 0.5625 W x H - held each eye half of what a mono view's held. The list is
// now created at 0.5625 x W x H x the eyes and doubled on a frame that overflowed it, never shrunk (kHitListWidth's law,
// OgreView::sizeHitList). THE BARS, ON THE STEREO VIEW'S OWN LIST (PHOTON-7 stage 0's read R4: the status's sums and
// its hitCapacity - the LARGEST live view's, the desktop's beside a headset - cannot see it; giStatus().rayQuery.hitViews
// reads each live view's list), in a live session against Monado's simulated headset with the gather's every ray a
// record (the arm gather.decodeHits), read from the session's first eye frame:
//   (1) the stereo view's list at its FIRST read-back frame (firstCapacity: the creation size, never rewritten) holds
//       0.5625 x the eye's W x H x 2 eyes - a list created at one eye, or sized without the eye factor, is red at once;
//   (2) every reading of a frame drawn with a GROWN list (its capacity above firstCapacity) drops 0 records - the first
//       frames may overflow the creation size by design (under gather.decodeHits every ray of the first frames is a
//       record), the list doubles to hold that demand, and after that first growth it holds it; a session that never
//       grows drops nothing at all.
// A view's drops are its OWN list's overflow (the counter counts past its capacity); a dropped record is no sample
// (rq_probe_gather.comp kAtlasNoSample -> the filter's irradiance.w -1 -> the integrate keeps the pixel's history),
// never black.
function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

project.create("vr hit list " + Date.now());
world.mode({ mode: "epic" });
// a closed set of surfaces around the head and a mover in front of it (a hit on a mover is always a record)
scene.addPrimitive("Cube", { position: [0, 1.5, -3], scale: [6, 3, 0.2] });
scene.addPrimitive("Cube", { position: [-3, 1.5, 0], scale: [0.2, 3, 6] });
scene.addPrimitive("Cube", { position: [3, 1.5, 0], scale: [0.2, 3, 6] });
var mover = scene.addPrimitive("Sphere", { position: [0.4, 1.4, -1.5], scale: [0.5, 0.5, 0.5] });
assert(node.setProperty(mover, "mobility", "movable") === true, "the sphere is a mover");
editor.frame(10);

assert(vr.available().available === true, "the runtime is there");
assert(engine.arm("gather.decodeHits", 1) === 1, "every gather ray is a hit record");
assert(vr.begin({ mirror: "left" }) === true, "the session began: " + app.lastError());

var eye = vr.state().eyeSize;
var need = Math.ceil(0.5625 * eye[0] * eye[1] * 2);   // the creation size of the stereo target's list (both eyes)
var frames = 0, pumped = 0, readings = 0, grownReadings = 0, maxStereoViews = 0;
var first = 0, capacity = 0, records = 0, dropped = 0, droppedGrown = 0, firstDrops = 0, eyeW = 0, eyeH = 0;
while (frames < 60 && pumped < 400) {
    var r0 = vr.state().rendered;
    editor.frame(1);
    ++pumped;
    if (vr.state().rendered === r0) continue;   // the runtime asked for no picture this frame
    ++frames;
    var views = world.giStatus().rayQuery.hitViews || [];
    var st = null, stereoViews = 0;
    for (var i = 0; i < views.length; ++i)
        if (views[i].eyes === 2) { st = views[i]; ++stereoViews; }
    maxStereoViews = Math.max(maxStereoViews, stereoViews);
    if (!st) continue;                           // its list not read back yet
    ++readings;
    first = st.firstCapacity;
    eyeW = st.eyeWidth; eyeH = st.eyeHeight;
    capacity = st.capacity;
    records = Math.max(records, st.records);
    dropped = Math.max(dropped, st.dropped);
    if (st.capacity > st.firstCapacity) { ++grownReadings; droppedGrown = Math.max(droppedGrown, st.dropped); }
    else firstDrops = Math.max(firstDrops, st.dropped);
}
console.log("stereo hit list: " + frames + " eye frames, " + readings + " readings of the stereo view's list (" +
            grownReadings + " after its growth); eye " + eye[0] + " x " + eye[1] + " (the view's own " + eyeW + " x " +
            eyeH + "); the list at its first reading " + first + " (need " + need + "), now " + capacity +
            "; records max " + records + "; dropped max " + dropped + " (at the creation size " + firstDrops +
            ", after the growth " + droppedGrown + "); the editor frames pumped " + (10 + pumped) + " (the row's FRAMES)");
assert(frames >= 30, "the session drew eyes: " + frames + " frames");
assert(maxStereoViews === 1, "one stereo view reads its own list: " + maxStereoViews);
assert(readings >= 20, "the stereo view's list was read back: " + readings + " readings");
assert(eyeW === eye[0] && eyeH === eye[1], "the view's eye is the session's: " + eyeW + " x " + eyeH);
assert(first >= need, "the stereo list's first reading holds 0.5625 x W x H x 2 eyes: " + first + " >= " + need);
assert(records > 0, "the eyes' traces appended records: " + records);
if (grownReadings > 0)
    assert(droppedGrown === 0, "after its first growth the stereo list drops no record: " + droppedGrown);
else
    assert(dropped === 0, "a list that never grew dropped no record: " + dropped);
// THE STEREO SHAPE (PHOTON-7-9A, the lead's ruling (A) on cube site (6b)): View::setStereo rebuilds the chain at the
// view's shape inside the call, so no recording of the session's frames met a chain at another shape - the tier's defect
// counter reads 0 (each refusal would also be its frame's error, naming the view)
var shapeRefusals = world.giStatus().rayQuery.reflectShapeRefusals;
console.log("the reflection's shape refusals over the session: " + shapeRefusals);
assert(shapeRefusals === 0, "no reflection recording met a chain at another stereo shape: " + shapeRefusals);
// ...AND THE COUNTER COUNTS (its NEGATIVE CONTROL, the lead's ruling; DOCS/traps/ENGINE.md: a counter's row reads it
// non-zero once): the arm reflect.shapeMismatch (a test arm, latched at the next frame's top) makes the stereo view's
// recording read the other shape for exactly ONE eye frame - the count is read once, AFTER the arm-off frames (so the law
// is exact whatever the status's lag or the pumps an eye frame takes, the reader's audit): exactly 1, then still exactly
// 1 at the session's end
assert(engine.arm("reflect.shapeMismatch", 1) === 1, "the arm reflect.shapeMismatch is registered");
var rArm = vr.state().rendered, armPumps = 0;
while (vr.state().rendered === rArm && armPumps < 50) { editor.frame(1); ++armPumps; }
engine.arm("reflect.shapeMismatch", 0);
var armEyeFrames = vr.state().rendered - rArm;
for (var k = 0; k < 10; ++k) editor.frame(1);   // the arm-off frames
var refusedOnce = world.giStatus().rayQuery.reflectShapeRefusals;
console.log("the negative control: " + armEyeFrames + " eye frame(s) under the arm (" + armPumps + " pumped); the count after "
            + "the arm-off frames " + refusedOnce);
assert(armEyeFrames === 1, "the arm covered exactly one eye frame: " + armEyeFrames);
assert(refusedOnce === 1, "the counter counts: one forced eye frame reads exactly 1 refusal: " + refusedOnce);
for (var k2 = 0; k2 < 10; ++k2) editor.frame(1);
var refusedEnd = world.giStatus().rayQuery.reflectShapeRefusals;
assert(refusedEnd === 1, "the session adds none after the arm: exactly 1 at its end: " + refusedEnd);
assert(vr.end() === true, "the session ended");
engine.arm("gather.decodeHits", 0);
