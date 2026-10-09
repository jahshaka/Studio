// vr.held_keys — A RIG MOVED BETWEEN THE WAIT AND THE RENDER IS THIS FRAME'S
// RIG (lane VR-REORDER-1's fix round; SPECS/ONE_PICTURE_SPEC.md A8).
//
// HELD fly keys move the wearer INSIDE the driver's tick: the host waits and
// locates (Engine::vrWaitFrame), then its fly step moves the rig, then the VR
// interaction step reads the hands, then the frame renders. The engine
// re-composes the located poses through a rig moved in that window (VrSession::
// recompose), so on every frame the head, the hands, the controller proxy, the
// held cube (editor) and the play camera (Player) all stand on the SAME rig.
// `vr.held_follows_hand` walks the rig BETWEEN ticks and never enters that
// path; this row holds the keys (`vr.move({hold:true})` — the keyboard's own
// per-frame step) so it does.
//
// What reds it: a re-compose with a wrong sign or order (the hand leaves its
// place in the room by the rig's step), no re-compose at all (the same), or the
// interaction's slot running BEFORE the host's fly (the held cube is placed
// from the pre-fly hand and is one rig step off its weld).
//
// Monado's simulated controller stands still in the room, so the hand's
// offset from the rig is a constant; the simulated head wanders slowly, so its
// offset from the rig moves by millimetres a frame while the rig moves by
// centimetres. The render loop is LIVE (`--script-live`). FRAMES, never wall
// time: every loop is bounded by a count.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function sub(a, b) { return { x: a.x - b.x, y: a.y - b.y, z: a.z - b.z }; }
function len(v) { return Math.sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
function intoFrame(q, v) {
    var x = -q.x, y = -q.y, z = -q.z, w = q.w;
    var tx = 2 * (y * v.z - z * v.y), ty = 2 * (z * v.x - x * v.z), tz = 2 * (x * v.y - y * v.x);
    return { x: v.x + w * tx + (y * tz - z * ty),
             y: v.y + w * ty + (z * tx - x * tz),
             z: v.z + w * tz + (x * ty - y * tx) };
}
function nextFrame(prev) {
    for (var reads = 0; reads < 2000000; ++reads) {
        var s = vr.state();
        if (s.renderedPoseSerial !== prev) return s;
    }
    throw new Error("no located frame was drawn after " + prev);
}
/// ONE TICK'S READINGS, consistent: every read between two state reads that
/// name the same rendered frame (a tick may run between two verbs).
function sample(prev, extra) {
    for (var tries = 0; tries < 1000; ++tries) {
        var s = nextFrame(prev);
        var r = { st: s, rig: vr.interactionMode().rig, proxy: vr.proxyPose("right"),
                  extra: extra ? extra() : null };
        if (vr.state().renderedPoseSerial === s.renderedPoseSerial) return r;
        prev = s.renderedPoseSerial;
    }
    throw new Error("no consistent sample");
}

/// A WORLD OFFSET IN THE RIG'S FRAME: rotated by -yaw about +Y (the rig's yaw
/// is the right-handed rotation about +Y, vrorigin.h).
function intoRig(v, yawDeg) {
    var t = -yawDeg * Math.PI / 180.0, c = Math.cos(t), sn = Math.sin(t);
    return { x: c * v.x + sn * v.z, y: v.y, z: -sn * v.x + c * v.z };
}
function wrap(d) { while (d > 180) d -= 360; while (d <= -180) d += 360; return d; }

/// THE ARM: hold `intent(i)` (the side keys swapped every frame, or a turn
/// swapped every frame) and check 40 frames. Swapping is what makes a
/// frame-late pose VISIBLE: a pose composed on the previous frame's rig is off
/// by that frame's step, and with the step changing sign every frame the error
/// jumps by twice the step, where the simulated head's own wander is a few
/// millimetres. The HAND has an absolute reference (it stands still in the
/// room, so its offset in the RIG's frame before the hold is its offset on
/// every held frame). `extra` reads the host's own thing (the DRAWN cube, the
/// camera) and `check` returns its error.
function holdArm(name, intent, extra, check) {
    var first = sample(vr.state().renderedPoseSerial, extra);
    var handRef = intoRig(sub(first.st.hands.right, first.rig), first.rig.yaw);
    var prev = null, frames = 0, stepped = 0;
    var worst = { hand: 0, proxy: 0, head: 0, own: 0 };
    var cur = first;
    for (var i = 0; i < 40; ++i) {
        if (vr.move(intent(i)) !== true)
            throw new Error(name + ": vr.move({hold:true}) refused: " + app.lastError());
        cur = sample(cur.st.renderedPoseSerial, extra);
        var hand = cur.st.hands.right, rig = cur.rig, head = cur.st.head;
        if (!hand.valid) throw new Error(name + ": the right hand is not located");
        var handOff = intoRig(sub(hand, rig), rig.yaw);
        var headOff = intoRig(sub(head, rig), rig.yaw);
        var headYaw = wrap(head.yaw - rig.yaw);
        worst.hand = Math.max(worst.hand, len(sub(handOff, handRef)));
        if (prev) {
            ++frames;
            var rigStep = len(sub(rig, prev.rig)), yawStep = Math.abs(wrap(rig.yaw - prev.rig.yaw));
            var headMove = len(sub(headOff, prev.headOff));
            var headTurn = Math.abs(wrap(headYaw - prev.headYaw));
            if (yawStep > 4.0) {
                ++stepped;
                worst.head = Math.max(worst.head, headTurn / yawStep);
            } else if (rigStep > 0.04) {
                ++stepped;
                worst.head = Math.max(worst.head, headMove / rigStep);
            }
            if (i < 4)
                console.log("      " + name + " frame " + frames + ": rig step " + rigStep.toFixed(4)
                            + " m / " + yawStep.toFixed(2) + " deg, hand off its place "
                            + len(sub(handOff, handRef)).toFixed(5) + " m, head-in-rig moved "
                            + headMove.toFixed(5) + " m / " + headTurn.toFixed(3) + " deg");
        }
        if (cur.proxy.drawn) worst.proxy = Math.max(worst.proxy, len(sub(cur.proxy, hand)));
        worst.own = Math.max(worst.own, check(cur));
        prev = { rig: rig, headOff: headOff, headYaw: headYaw };
    }
    assert(vr.move({ hold: true }) === true, name + ": and let go");
    console.log("      " + name + ": over " + frames + " frames the rig stepped on " + stepped
                + "; worst: hand off its place " + worst.hand.toFixed(5) + " m, proxy-to-hand "
                + worst.proxy.toFixed(5) + " m, head-in-rig move / rig step " + worst.head.toFixed(3)
                + ", " + name + " " + worst.own.toFixed(5) + " m");
    assert(stepped >= 20, name + ": the held intent moved the rig on most frames ("
                          + stepped + " of " + frames + ")");
    assert(worst.hand < 1e-3, name + ": the hand stays where it stands in the room, on the moved "
                              + "rig, every frame (" + worst.hand.toFixed(5) + " m)");
    assert(worst.proxy < 1e-3, name + ": the proxy is drawn on the hand (" + worst.proxy.toFixed(5) + " m)");
    assert(worst.head < 0.5, name + ": the head is on this frame's rig, not the last one's (its "
                             + "offset moved " + worst.head.toFixed(3) + " of a rig step; frame-late "
                             + "reads ~2)");
    assert(worst.own < 1e-3, name + ": " + (name === "player" ? "the play camera IS the head"
                                                              : "the DRAWN cube stays on the DRAWN wand")
                             + " (" + worst.own.toFixed(5) + " m)");
}
function sideKeys(i) { return { right: (i % 2) === 0, left: (i % 2) === 1, hold: true }; }
function snapTurns(i) { return { hold: true, turnDegrees: (i % 2) === 0 ? 15 : -15 }; }

project.create("vr held keys " + Date.now());
var cube = scene.addPrimitive("Cube");
node.transform(cube, { scale: { x: 0.05, y: 0.05, z: 0.05 } });
world.vr({ flySpeed: 2.0 });

var a = vr.available();
assert(a.available === true, "the runtime answered: " + a.runtime);

// ---- 1. THE EDITOR'S PREVIEW, holding a cube ------------------------------
assert(vr.begin({ mirror: "none" }) === true, "vr.begin() starts the editor's preview");
var s = vr.state();
for (var reads = 0; reads < 4000000; ++reads) {
    s = vr.state();
    if (s.head.valid && s.hands.right.valid && s.stepPoseSerial > 0 && !s.preview.placing) break;
}
assert(s.head.valid && s.hands.right.valid && s.stepPoseSerial > 0,
       "the head and the right hand are located and the driver steps the interaction");
var h = s.hands.right;
node.transform(cube, { position: { x: h.x, y: h.y + 0.03, z: h.z } });
editor.select(cube);
s = nextFrame(s.renderedPoseSerial);
assert(vr.grab({ hand: "right" }) === true, "the right hand takes hold of the cube");
var im = vr.interactionMode();
assert(im.grabbing === true && im.far === false, "a NEAR grab");
s = nextFrame(nextFrame(s.renderedPoseSerial).renderedPoseSerial);
// WHAT IS DRAWN, ON BOTH SIDES: the cube's ENGINE node (vr.nodePose) against
// the wand's (vr.proxyPose), read on the same rendered frame. The document's
// node (node.info) is blind to a frame of lag between the document and the
// picture, which is exactly the defect this row guards: before the interaction
// step ran inside the host's tick (VR-REORDER-1's fix round) the drawn cube was
// one rig step off the drawn wand — measured RED_ON_BASE m on the side-key arm.
function drawnWeld() {
    var weld = null;
    return function (c) {
        if (!c.proxy.drawn || !c.extra.drawn) throw new Error("the wand or the cube is not drawn");
        var off = intoFrame(c.proxy.rotation, sub(c.extra, c.proxy));
        if (!weld) { weld = off; return 0; }
        return len(sub(off, weld));
    };
}
holdArm("editor", sideKeys, function () { return vr.nodePose(cube); }, drawnWeld());
// ...AND A TURN (the recompose's ROTATION half and the held thing carried by
// the stick's own turn(), vrinteraction.cpp): snap turns of 15 degrees, swapped
// every frame, with the cube in the hand. Before the fix round the cube was a
// turn behind the wand — RED_ON_BASE_TURN m.
holdArm("editor turn", snapTurns, function () { return vr.nodePose(cube); }, drawnWeld());
assert(vr.release({ hand: "right" }) === true, "the cube is put down");
assert(vr.end() === true, "the preview ends");
editor.frame(2);

// ---- 2. THE PLAYER'S VR MODE ----------------------------------------------
app.space("player");
assert(app.columns().space === "player", "the Player page is up");
assert(player.play({ vr: true }) === true, "player.play({vr:true}) starts the run in VR");
s = vr.state();
for (var reads2 = 0; reads2 < 4000000; ++reads2) {
    s = vr.state();
    if (s.head.valid && s.hands.right.valid && player.state().vr.placing !== true) break;
}
assert(s.head.valid && s.hands.right.valid, "the Player's wearer is located");
holdArm("player", sideKeys, function () { return editor.camera().position; }, function (c) {
    return len(sub(c.extra, c.st.head));
});
player.stop();
console.log("vr.held_keys: PASS");
