// vr.hit_list_stereo (PHOTON-2 stage 2's tail, 1.6 A2) — THE STEREO VIEW'S HIT LIST HOLDS BOTH EYES' DEMAND.
//
// One stereo view's two eyes append to ONE hit list (OgreChain.cpp's applyStereo leaves it single-layer), so the old
// list - a fixed factor of ONE eye's target, 0.5625 W x H - held each eye half of what a mono view's held. The list is
// now created at 0.5625 x W x H x the eyes and doubled on a frame that overflowed it, never shrunk (kHitListWidth's law,
// OgreView::sizeHitList). THE BAR, PER LIVE VIEW (PHOTON-7 stage 0; RD4 W1): in a live session against Monado's
// simulated headset, with the gather's every ray a record (the arm gather.decodeHits), hitDropped is 0 on every frame
// read after the growth (24 frames). A view's drops are its OWN list's overflow (the counter counts past its capacity),
// and the status sums the scene's LIVE views only (a view whose read-backs stopped is left out), so hitDropped 0 says
// every live view's list held its own demand - the old "hitCapacity >= hitRecords" compared the largest list with the
// records SUMMED over every view, stale ones included.
// MEASURED (2026-10-22): 25,667 records a frame at most in this scene (its probes on the few surfaces in view), 0
// dropped - a demand the old fixed list (0.5625 W x H of one layer) would also have held: the scene does not reach the
// old list's per-eye limit, so this row guards the stereo path's sizing and accounting, not a base red.
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

var rendered0 = vr.state().rendered;
var worst = 0, frames = 0, records = 0, capacity = 0, pumped = 0;
while (frames < 60 && pumped < 400) {
    var r0 = vr.state().rendered;
    editor.frame(1);
    ++pumped;
    if (vr.state().rendered === r0) continue;   // the runtime asked for no picture this frame
    var rq = world.giStatus().rayQuery;
    if (vr.state().rendered - rendered0 > 24) {
        ++frames;
        worst = Math.max(worst, rq.hitDropped);
        records = Math.max(records, rq.hitRecords);
        capacity = rq.hitCapacity;
    }
}
console.log("stereo hit list: " + frames + " eye frames read, records max " + records + ", capacity " + capacity +
            ", dropped max " + worst + "; the editor frames pumped " + (10 + pumped) + " (the row's FRAMES)");
assert(frames >= 30, "the session drew eyes: " + frames + " frames read after the growth");
assert(records > 0, "the eyes' traces appended records: " + records);
assert(worst === 0, "no live view's list dropped a record after the growth: " + worst);
assert(vr.end() === true, "the session ended");
engine.arm("gather.decodeHits", 0);
