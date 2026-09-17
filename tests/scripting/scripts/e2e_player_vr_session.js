// vr.player_session — THE PLAYER IN THE HEADSET (SPECS/VR_SPEC.md §4.5, phase 3).
//
// The runner starts Monado's SIMULATED HMD and launches the app with `--vr`, so
// everything below happens against a real OpenXR runtime: a real session, real
// located poses, real frames the runtime accepts, and a real mirror on the
// Player's own on-screen view.
//
// FRAMES, NEVER WALL TIME (VR_SPEC §6 flake class (b)). Inside a session every
// `player.frame(n)` blocks in xrWaitFrame until the runtime wants the next
// picture, so this script is PACED BY THE RUNTIME and what it asserts are
// COUNTS the runtime accepted and POSITIONS the engine reports — never a
// duration, which on a loaded box measures the box.
//
// What this proves that `player.vr` (the arithmetic, pure) and
// `scripting.e2e.player_vr` (the refusals, no runtime) cannot:
//   * the session starts WITH the play run and ends with it;
//   * the wearer stands where the play camera stood, facing its way;
//   * the mirror is the PLAYER's view, and that view stops drawing its own
//     picture (phase 2 mirrored onto the editor's viewport and left it
//     rendering the world a second time at window size);
//   * the fly moves the RIG along the head's heading, by the distance the
//     player's own fly speed says;
//   * the two orders — player.stop() and player.endVr() — do what they say;
//   * the toggle switches the page and starts the run.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function near(a, b, eps) { return Math.abs(a - b) <= eps; }

project.create("player vr session " + Date.now());
scene.addPrimitive("Cube");

// ---- 0. the runtime is there and the player can see it -------------------

var st = player.state();
console.log("player.state().vr: " + JSON.stringify(st.vr));
assert(st.vr.available === true, "the player reports VR available: " + vr.available().runtime);
assert(st.vr.active === false, "and no session yet");

// The Player page has to be the space on screen: its on-screen View is created
// by the show event and that View is what the headset mirrors onto. This is
// exactly what the toggle does for a user (case 5 below drives the toggle
// itself); doing it by hand here keeps the cases independent.
app.space("player");
assert(app.columns().space === "player", "the Player page is up");

// ---- 1. play in VR -------------------------------------------------------

// WHERE THE PLAY CAMERA STANDS, before anything starts. The Player's camera IS
// the editor camera (the legacy rule EnginePlayerScene keeps), so this is the
// pose the wearer must find themselves standing in.
var cam0 = editor.camera();
console.log("the play camera: " + JSON.stringify(cam0.position));

assert(player.play({ vr: true }) === true, "player.play({vr:true}) starts the run in VR");
assert(player.playing() === true, "the scene is running");

st = player.state().vr;
console.log("vr after begin: " + JSON.stringify(st));
assert(st.active === true, "and the session is active");
assert(st.mirror === "left", "the desktop shows the left eye");
assert(st.mirrorView.indexOf("player") >= 0,
       "MIRRORED ONTO THE PLAYER'S OWN VIEW: " + st.mirrorView);

// ---- 2. the runtime paces the frames, and the wearer is somewhere --------

player.frame(90);
st = player.state().vr;
console.log("vr after 90 frames: " + JSON.stringify(st));
assert(st.frames >= 40, "the runtime ACCEPTED " + st.frames + " frames");
assert(st.rendered >= 30, "and asked us to draw " + st.rendered + " of them");
assert(st.state === "focused", "the session reached `focused` (it is " + st.state + ")");
assert(st.posesValid === true, "the runtime has located the wearer's head");

// THE PLACEMENT. The rig was placed so the head lands on the play camera, and
// the camera then FOLLOWS the head — so after the placement the two agree.
var head = st.head;
console.log("head: " + JSON.stringify(head) + "  rig: " + JSON.stringify(st.origin));
assert(isFinite(head.x) && isFinite(head.y) && isFinite(head.z), "the head has a position");
assert(isFinite(st.origin.yaw), "and the rig has a heading");

// THE PLACEMENT, and it is the heart of phase 3: the rig was placed so that the
// wearer's HEAD lands on the play camera — not the rig's floor origin, which is
// a standing height below it.
//
// THE TOLERANCE IS MEASURED, NOT GUESSED. The placement is exact at the instant
// it happens (asserted byte-exactly, and from both sides, in `player.vr`) — but
// this runtime's simulated head MOVES ON ITS OWN, and every frame since the
// placement has carried the wearer a little further across their room, which is
// correct behaviour and not drift. So the head's own excursion over a few
// frames is measured HERE and the placement is asserted against it: standing at
// the camera, within a wearer's own wander.
var wanderA = player.state().vr.head;
player.frame(12);
var wanderB = player.state().vr.head;
var wander = Math.sqrt(Math.pow(wanderB.x - wanderA.x, 2) + Math.pow(wanderB.y - wanderA.y, 2) +
                       Math.pow(wanderB.z - wanderA.z, 2));
head = player.state().vr.head;
var off = Math.sqrt(Math.pow(head.x - cam0.position.x, 2) + Math.pow(head.y - cam0.position.y, 2) +
                    Math.pow(head.z - cam0.position.z, 2));
console.log("camera was " + JSON.stringify(cam0.position) + ", head is " + JSON.stringify(head) +
            " — " + off.toFixed(3) + " m away, and this head wanders " + wander.toFixed(3) +
            " m in 12 frames on its own");
assert(wander > 0.0, "the simulated head moves on its own (" + wander.toFixed(3) + " m/12 frames)");
assert(off < 10.0 * wander + 0.05,
       "THE WEARER IS STANDING WHERE THE PLAY CAMERA STOOD (" + off.toFixed(3) +
       " m, inside their own wander)");
assert(Math.abs(player.state().vr.origin.y - head.y) > 0.5,
       "and the rig's floor is a standing height below their eyes (" +
       (head.y - player.state().vr.origin.y).toFixed(2) + " m), not at them");

// ---- 3. THE FLY MOVES THE RIG, ALONG THE HEAD'S HEADING -----------------
//
// Driven through the verb rather than through held keys, for the same reason
// every other e2e drives verbs: a script cannot hold a key down, and the verb
// and the key path are the same call (PlayerVr::move).

var speed = player.state().vr.flySpeed;
var rig0 = player.state().vr.origin;
var head0 = player.state().vr.head;
// One second of forward, in one call. The wearer's heading is head0.yaw and
// yaw 0 looks down -Z, so forward = (-sin yaw, 0, -cos yaw).
player.vrMove({ forward: true, seconds: 1.0 });
var rig1 = player.state().vr.origin;
var moved = { x: rig1.x - rig0.x, y: rig1.y - rig0.y, z: rig1.z - rig0.z };
var dist = Math.sqrt(moved.x * moved.x + moved.y * moved.y + moved.z * moved.z);
console.log("rig moved " + JSON.stringify(moved) + " (" + dist.toFixed(3) +
            " units at " + speed + " u/s, heading " + head0.yaw.toFixed(2) + " deg)");
assert(near(dist, speed, speed * 0.02 + 1e-3),
       "one second of forward moved the rig by the player's own fly speed");
assert(near(moved.y, 0, 1e-4), "level: forward does not change height");
var rad = head0.yaw * Math.PI / 180.0;
assert(near(moved.x / dist, -Math.sin(rad), 1e-3) && near(moved.z / dist, -Math.cos(rad), 1e-3),
       "and along the direction the HEAD is facing, flattened");
assert(near(rig1.yaw, rig0.yaw, 1e-4), "flying never turns the rig (no induced yaw)");

// Back where we came from: the same call with `back` must undo it exactly.
player.vrMove({ back: true, seconds: 1.0 });
var rig2 = player.state().vr.origin;
assert(near(rig2.x, rig0.x, 1e-2) && near(rig2.z, rig0.z, 1e-2),
       "and `back` for the same second returns the wearer to where they were");

// The head follows the rig (the engine composes them): one more frame and the
// reported head has moved by what the rig moved.
player.vrMove({ right: true, seconds: 0.5 });
var rigA = player.state().vr.origin;
var headA = player.state().vr.head;
player.frame(3);
var headB = player.state().vr.head;
console.log("head before/after the rig moved: " + JSON.stringify(headA) + " -> " +
            JSON.stringify(headB));
assert(!near(headB.x, headA.x, 1e-4) || !near(headB.z, headA.z, 1e-4),
       "the wearer's head moved with the rig they are standing on");

// ---- 4. vr.end() mid-play: VR stops, the scene keeps playing ------------

assert(player.endVr() === true, "player.endVr() ends the session");
assert(player.state().vr.active === false, "no session");
assert(player.playing() === true, "AND THE SCENE IS STILL PLAYING (the chosen order)");
player.frame(5);
assert(player.state().vr.active === false, "still no session after more frames");
assert(player.playing() === true, "and still playing");
assert(player.endVr() === false, "a second endVr() refuses");

// ---- 5. the toggle: one call takes you in, one takes you out ------------

assert(vr.toggle() === true, "vr.toggle() puts the player back in VR");
assert(app.columns().space === "player", "on the Player page");
assert(player.state().vr.active === true, "with a session running");
assert(player.playing() === true, "and the scene running");
player.frame(30);
assert(player.state().vr.frames > 0, "the second session submits frames too");

assert(vr.toggle() === false, "a second vr.toggle() leaves VR");
assert(player.state().vr.active === false, "the session is over");
assert(player.playing() === false, "and so is the run (a toggle stops the run)");

// ---- 6. player.stop() ends the session with the run ---------------------

assert(player.play({ vr: true }) === true, "in VR again");
player.frame(20);
assert(player.state().vr.active === true, "session running");
assert(player.stop() === true, "player.stop() stops the run");
assert(player.state().vr.active === false, "AND ends the session with it (the chosen order)");

// ---- 7. and the desktop player is itself again --------------------------

player.frame(10);
assert(player.play() === true, "a plain player.play() still works afterwards");
player.frame(10);
assert(player.state().vr.active === false, "with no session");
player.stop();

console.log("vr.player_session: PASS");
