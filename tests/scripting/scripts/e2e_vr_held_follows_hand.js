// vr.held_follows_hand — WHAT THE HAND HOLDS IS DRAWN WITH THE HAND THAT HOLDS
// IT (lane VR-REORDER-1; SPECS/ONE_PICTURE_SPEC.md A8: "a held object's
// frame-N position equals the wand's").
//
// A NEAR GRAB IS A RIGID WELD (vr.grab's own contract): the held cube's offset
// in the hand's frame is the same on every frame for as long as it is held. So
// after each driven frame the cube's position, expressed in the frame of the
// hand that frame LOCATED and DREW (`vr.state().hands.right`, the pose the
// proxy wand was placed from), must stay where it was at the grab. When the
// interaction step reads the PREVIOUS frame's hand — the order before the lane
// — the cube is drawn one hand-move behind the wand, and the offset swims by
// exactly that move.
//
// THE HAND MOVES BETWEEN FRAMES because the wearer is walked: `vr.move` steps
// the rig 1 cm between two driver ticks, which carries every
// located pose with it at the next locate (Monado's simulated controllers may
// move on their own as well; either motion is a hand motion). The render loop
// is LIVE (`--script-live`), so the interaction is stepped by the driver's tick
// exactly as in a headset — `vr.step()` is never called. No injection: an
// injected sample would stand the driver's step down.
//
// FRAMES, NEVER WALL TIME: bounded by counts of reads and of located frames.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function sub(a, b) { return { x: a.x - b.x, y: a.y - b.y, z: a.z - b.z }; }
function len(v) { return Math.sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
/// v rotated by the CONJUGATE of q ({x,y,z,w}): world offset -> hand frame.
function intoFrame(q, v) {
    var x = -q.x, y = -q.y, z = -q.z, w = q.w;
    var tx = 2 * (y * v.z - z * v.y), ty = 2 * (z * v.x - x * v.z), tz = 2 * (x * v.y - y * v.x);
    return { x: v.x + w * tx + (y * tz - z * ty),
             y: v.y + w * ty + (z * tx - x * tz),
             z: v.z + w * tz + (x * ty - y * tx) };
}
/// Wait for the driver to draw a NEW located frame; returns that frame's state.
function nextFrame(prev) {
    for (var reads = 0; reads < 2000000; ++reads) {
        var s = vr.state();
        if (s.renderedPoseSerial !== prev) return s;
    }
    throw new Error("no located frame was drawn after " + prev);
}

project.create("vr held follows hand " + Date.now());
var cube = scene.addPrimitive("Cube");
node.transform(cube, { scale: { x: 0.05, y: 0.05, z: 0.05 } });

var a = vr.available();
assert(a.available === true, "the runtime answered: " + a.runtime);
assert(vr.begin({ mirror: "none" }) === true, "vr.begin() starts a session on the editor's scene");

// THE SESSION IS LOCATING, THE PREVIEW HAS PLACED THE WEARER, AND THE DRIVER IS
// STEPPING THE INTERACTION ON THE RIGHT HAND.
var s = vr.state();
for (var reads = 0; reads < 4000000; ++reads) {
    s = vr.state();
    if (s.head.valid && s.hands.right.valid && s.stepPoseSerial > 0 && !s.preview.placing) break;
}
assert(s.head.valid && s.hands.right.valid, "the head and the right hand are located");
assert(s.stepPoseSerial > 0, "the driver steps the interaction");

// THE CUBE IN THE HAND, 3 cm off the grip — well inside arm's reach, so the
// grab is NEAR (a rigid weld; a far grab is filtered and is not this row).
var hand = s.hands.right;
node.transform(cube, { position: { x: hand.x, y: hand.y + 0.03, z: hand.z } });
editor.select(cube);
s = nextFrame(s.renderedPoseSerial);
assert(vr.grab({ hand: "right" }) === true, "the right hand takes hold of the cube");
var im = vr.interactionMode();
assert(im.grabbing === true && im.far === false && im.nodes === 1,
       "a NEAR grab of one node (" + JSON.stringify({ grabbing: im.grabbing, far: im.far,
                                                      nodes: im.nodes }) + ")");

// THE OFFSET AT THE GRAB, measured on the first frame the step has followed.
s = nextFrame(s.renderedPoseSerial);
s = nextFrame(s.renderedPoseSerial);
function offsetNow(st) {
    var p = node.info(cube).position;
    return intoFrame(st.hands.right.rotation, sub(p, st.hands.right));
}
var start = offsetNow(s);
console.log("      the cube in the hand's frame at the grab: " + JSON.stringify(start));

var speed = vr.interactionMode().flySpeed;
assert(speed > 0, "the wearer's fly speed is " + speed + " m/s");
var stepSeconds = 0.01 / speed;          // 1 cm per frame
var worst = 0, moved = 0, maxMove = 0, frames = 0;
var lastHand = s.hands.right;
for (var i = 0; i < 60; ++i) {
    // ONE CENTIMETRE OF THE WEARER, between two ticks, alternating sides.
    vr.move({ right: (i % 2) === 0, left: (i % 2) === 1, seconds: stepSeconds });
    s = nextFrame(s.renderedPoseSerial);
    // THE SAME TICK'S CUBE AND HAND: `vr.state()` and `node.info` are two
    // verbs, and a tick may run between them — a pair whose serial moved under
    // it is read again (bounded) rather than compared across two frames.
    var off = offsetNow(s), again = vr.state(), tries = 0;
    while (again.renderedPoseSerial !== s.renderedPoseSerial && tries < 1000) {
        s = again;
        off = offsetNow(s);
        again = vr.state();
        ++tries;
    }
    if (again.renderedPoseSerial !== s.renderedPoseSerial)
        throw new Error("no consistent (state, cube) pair for frame " + (frames + 1));
    ++frames;
    var d = len(sub(off, start));
    var step = len(sub(s.hands.right, lastHand));
    lastHand = s.hands.right;
    if (step > 1e-4) ++moved;
    if (step > maxMove) maxMove = step;
    if (d > worst) worst = d;
    if (i < 4 || d > 1e-3)
        console.log("      frame " + frames + ": hand moved " + step.toFixed(4) + " m, the cube "
                    + "is " + d.toFixed(4) + " m off its weld");
}
console.log("      over " + frames + " frames: the hand moved on " + moved + " (at most "
            + maxMove.toFixed(4) + " m), the cube left its weld by at most " + worst.toFixed(5) + " m");
assert(moved >= 30, "the hand really moved on most frames (" + moved + " of " + frames + ")");
assert(worst < 1e-3,
       "THE HELD CUBE IS DRAWN WITH THE HAND OF ITS OWN FRAME: never more than 1 mm off its "
       + "weld (worst " + worst.toFixed(5) + " m)");

assert(vr.release({ hand: "right" }) === true, "the cube is put down");
vr.end();
console.log("vr.held_follows_hand: PASS");
