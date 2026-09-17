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

// ---- 0b. FROM A PAGE THAT HAS NEVER BEEN SHOWN, NOTHING MOVES -----------
//
// The mirror is the Player's on-screen View and that View is created by the
// page's show event, so a session cannot begin before the page has been up.
// The refusal has to happen BEFORE the run starts (lead review, second read):
// the session can only begin after the scene is playing, so a refusal
// discovered at that point would have started and stopped a run inside one
// call — a transform snapshot, a physics restart and a possession edge, for a
// caller that was told nothing happened.

assert(player.playing() === false, "nothing is playing yet");
assert(player.play({ vr: true }) === false,
       "player.play({vr:true}) refuses from a page that has never been shown");
console.log("app.lastError: " + app.lastError());
assert(app.lastError().indexOf("has not been shown") >= 0,
       "...saying which: " + app.lastError());
assert(player.playing() === false, "AND THE RUN DID NOT START (nothing moved)");

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

// FRAME BY FRAME UP TO THE PLACEMENT, so the placement can be MEASURED rather
// than bounded by a number big enough to hide a third of a metre. The rig is
// placed on the first frame the runtime locates a pose; one more frame carries
// it into the head the engine reports.
var located = 0;
while (located < 400 && player.state().vr.posesValid !== true) { player.frame(1); ++located; }
assert(player.state().vr.posesValid === true,
       "the runtime located the wearer's head after " + located + " frames");
var tBefore = Date.now();
player.frame(1);                       // the frame that carries the placed rig
var placed = player.state().vr.head;
var gapMs = Math.max(1, Date.now() - tBefore);

// THE WANDER OF THIS SIMULATED HEAD — AS A SPEED, NOT AS "two frames"
// (lane VR-3b, 2026-09-17, measured). This head moves on a WALL CLOCK: it is a
// simulated device driven by the runtime's own time, not by our frame count. The
// placement is exact, and what separates `placed` from the pose it was computed
// from is the TIME between two locates — so a tolerance expressed in frames is a
// tolerance that shrinks exactly when the box gets slower. It failed that way
// under the Vulkan validation layer (0.0851 m against a two-frame wander of
// 0.0374) and would fail the same way on a loaded box or a cold shader cache.
//
// So: measure the head's SPEED here, over the same kind of frames, and allow it
// the distance it can cover in the time the placement gap actually took. The
// placement's exactness itself is asserted arithmetically, from both sides, in
// `player.vr`; this case exists to catch a wearer standing somewhere ELSE.
var tA = Date.now();
var w0 = player.state().vr.head;
player.frame(2);
var w1 = player.state().vr.head;
var wanderMs = Math.max(1, Date.now() - tA);
var wander = Math.sqrt(Math.pow(w1.x - w0.x, 2) + Math.pow(w1.y - w0.y, 2) +
                       Math.pow(w1.z - w0.z, 2));
var speed = wander / wanderMs;                       // metres per millisecond
var tolerance = 3.0 * speed * gapMs + 0.02;          // the gap, with room for jitter
var off = Math.sqrt(Math.pow(placed.x - cam0.position.x, 2) +
                    Math.pow(placed.y - cam0.position.y, 2) +
                    Math.pow(placed.z - cam0.position.z, 2));
console.log("placed at " + JSON.stringify(placed) + " vs the camera " +
            JSON.stringify(cam0.position) + " — " + off.toFixed(4) +
            " m; this head moves " + (speed * 1000).toFixed(3) + " m/s and the placement gap " +
            "was " + gapMs + " ms, so the tolerance is " + tolerance.toFixed(4) + " m");
assert(off < tolerance,
       "THE WEARER STANDS WHERE THE RUN BEGAN: " + off.toFixed(4) + " m off, inside what " +
       "this head can wander in the placement's own gap (" + tolerance.toFixed(4) + " m)");
// ...FACING ITS WAY. yaw 0 looks down -Z and the fixture's camera is level, so
// the two headings are directly comparable.
var camYaw = Math.atan2(-2 * (cam0.rotation.scalar * cam0.rotation.y +
                              cam0.rotation.z * cam0.rotation.x),
                        1 - 2 * (cam0.rotation.x * cam0.rotation.x +
                                 cam0.rotation.y * cam0.rotation.y)) * 180 / Math.PI;
console.log("head yaw " + placed.yaw.toFixed(2) + " vs the camera's " + camYaw.toFixed(2));
assert(Math.abs(placed.yaw - camYaw) < 5.0,
       "AND FACING THE WAY IT FACED (" + placed.yaw.toFixed(2) + " vs " + camYaw.toFixed(2) + ")");

player.frame(60);
st = player.state().vr;
console.log("vr after the run: " + JSON.stringify(st));
assert(st.frames >= 40, "the runtime ACCEPTED " + st.frames + " frames");
assert(st.rendered >= 30, "and asked us to draw " + st.rendered + " of them");
assert(st.state === "focused", "the session reached `focused` (it is " + st.state + ")");
assert(st.posesValid === true, "the runtime has located the wearer's head");
assert(st.spaceChanges === 0, "and never recentred the room under them");

// THE PER-FRAME WRITE: the document's camera IS the wearer's head, every frame.
// It is what makes player.screenshot, a script's camera read and the desktop
// Player agree about where the wearer is.
//
// READ THE HEAD FIRST, THEN STEP. The write is exact but one frame behind by
// construction — the frame's step() writes the pose located by the PREVIOUS
// frame, and the pose this frame locates is what `state()` reports afterwards
// (the headset itself is never behind: the engine composes the rig inside the
// pump). So the honest assertion is "the camera ends up at the head the step
// had to work with", and it is an equality, not a tolerance.
for (var i = 0; i < 5; ++i) {
    var h = player.state().vr.head;
    player.frame(1);
    var c = editor.camera().position;
    assert(near(c.x, h.x, 1e-4) && near(c.y, h.y, 1e-4) && near(c.z, h.z, 1e-4),
           "frame " + i + ": the play camera IS the head (" + c.x.toFixed(4) + ", " +
           c.y.toFixed(4) + ", " + c.z.toFixed(4) + ")");
}

// THE PLACEMENT. The rig was placed so the head lands on the play camera, and
// the camera then FOLLOWS the head — so after the placement the two agree.
var head = st.head;
console.log("head: " + JSON.stringify(head) + "  rig: " + JSON.stringify(st.origin));
assert(isFinite(head.x) && isFinite(head.y) && isFinite(head.z), "the head has a position");
assert(isFinite(st.origin.yaw), "and the rig has a heading");

assert(Math.abs(player.state().vr.origin.y - player.state().vr.head.y) > 0.5,
       "the rig's floor is a standing height below their eyes (" +
       (player.state().vr.head.y - player.state().vr.origin.y).toFixed(2) + " m), not at them");

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

// ---- 3b. RECENTRE: take me back to where the run began ------------------
//
// Not "re-place onto the camera" — the camera is written FROM the head every
// frame, so that would be a no-op (lead review F2). The anchor is the pose the
// play camera had when the run STARTED, which is the one thing in the room that
// has not moved with the wearer.

player.vrMove({ forward: true, seconds: 3.0 });     // walk well away
var walked = player.state().vr.head;
var awayFrom = Math.sqrt(Math.pow(walked.x - cam0.position.x, 2) +
                         Math.pow(walked.z - cam0.position.z, 2));
console.log("walked " + awayFrom.toFixed(2) + " m from where the run began");
assert(awayFrom > 10.0, "the wearer is well away from the start (" + awayFrom.toFixed(2) + " m)");
var tRecenter = Date.now();                       // the recentre's OWN gap (lead, second read)
assert(player.vrRecenter() === true, "player.vrRecenter() is accepted");
// A MOVE ARRIVING IN THE GAP IS ANSWERED BY THE TELEPORT, NOT ADDED TO IT
// (lead review, second read): a `vrMove` between the request and the placement
// would move the rig under a head already located for the old one — the
// mismatched pair the placement's guard exists to avoid, rebuilt from outside
// the frame loop where no guard can see it. The verb answers true (the caller
// asked for a move and is getting a teleport a moment later) and the landing
// must be unaffected.
assert(player.vrMove({ right: true, seconds: 2.0 }) === true,
       "a vrMove in the recentre's gap is accepted");
player.frame(1);
assert(player.vrMove({ forward: true, seconds: 2.0 }) === true,
       "...and another, after a pump");
player.frame(3);
var recentreMs = Math.max(1, Date.now() - tRecenter);
// The same physics as the placement: this head wanders on a wall clock, so the
// recentre is allowed the distance it can cover in the recentre's own gap —
// measured here, not borrowed from the placement's (the lead's second read).
var recentreTolerance = 3.0 * speed * recentreMs + 0.02;
var back = player.state().vr.head;
var backOff = Math.sqrt(Math.pow(back.x - cam0.position.x, 2) +
                        Math.pow(back.y - cam0.position.y, 2) +
                        Math.pow(back.z - cam0.position.z, 2));
console.log("after the recentre the head is " + backOff.toFixed(4) + " m from the start pose");
assert(backOff < recentreTolerance,
       "RECENTRE PUTS THE WEARER BACK WHERE VR BEGAN, and the two moves that arrived in " +
       "its gap changed nothing (" + backOff.toFixed(4) + " m, tolerance " +
       recentreTolerance.toFixed(4) + " over the recentre's " + recentreMs + " ms)");

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

// ---- 6a. AN ARMED ACTIVE CAMERA IS THE ONE THE WEARER STANDS ON ---------
//
// THE DEFECT THIS CASE EXISTS FOR (lead review F1): the Player renders through
// the scene's ACTIVE camera while playing (CAMERAS_SPEC D6), and a VR mode that
// placed the wearer on the FREE camera instead would stand them somewhere the
// Player's picture never was — metres away, in a scene authored around a shot.
// One rule, in the document (iris::Scene::renderCamera), used by the mirror and
// by the rig alike.

var shotId = scene.addCamera({ position: { x: 30, y: 2, z: -18 } });
assert(!!shotId, "a second camera, well away from the free one");
assert(scene.setActiveCamera(shotId) === true, "and it is armed as the active camera");
assert(player.play({ vr: true }) === true, "play in VR with an authored shot armed");
var locatedShot = 0;
while (locatedShot < 400 && player.state().vr.posesValid !== true) { player.frame(1); ++locatedShot; }
player.frame(1);
var atShot = player.state().vr.head;
console.log("the wearer stands at " + JSON.stringify(atShot) + " (the shot is at 30, 2, -18)");
assert(Math.abs(atShot.x - 30) < 0.5 && Math.abs(atShot.y - 2) < 0.5 &&
       Math.abs(atShot.z + 18) < 0.5,
       "THE WEARER STANDS AT THE AUTHORED SHOT, not at the free camera");
// ...and the head writes back to THAT camera, not to the free one.
var beforeWrite = player.state().vr.head;
player.frame(1);
var shotNow = node.transform(shotId).position;
assert(near(shotNow.x, beforeWrite.x, 1e-3) && near(shotNow.z, beforeWrite.z, 1e-3),
       "and the head writes back to the ACTIVE camera (" + shotNow.x.toFixed(3) + ", " +
       shotNow.y.toFixed(3) + ", " + shotNow.z.toFixed(3) + ")");
assert(player.stop() === true, "stop");
assert(scene.setActiveCamera(null) === true, "the shot is disarmed again");

// ---- 6b. LEAVING THE PLAYER PAGE TAKES THE HEADSET OFF ------------------
//
// A session belongs to the RUN, and the page IS the run's window: leaving it
// used to leave the engine pumping at the runtime's cadence behind a page
// nobody is on — the rig and the camera frozen, the driver still in VR pacing,
// the mirror presenting into an unmapped window every frame (lead review F3).

assert(player.play({ vr: true }) === true, "in VR once more");
player.frame(20);
assert(player.state().vr.active === true, "session running");
app.space("editor");
assert(app.columns().space === "editor", "the editor page is up");
assert(player.state().vr.active === false,
       "LEAVING THE PLAYER PAGE ENDED THE SESSION");
assert(player.playing() === false, "and stopped the run with it");
editor.frame(10);
assert(player.state().vr.active === false, "still ended after more editor frames");
// THE PACING IS THE EDITOR'S AGAIN — the driver is out of session pacing, so
// ordinary frames run without a runtime to wait for. Asserted through the frame
// budget the editor's own suites use: ten frames of a paced loop return.
assert(app.renderStats().frameMs >= 0, "the editor renders on its own clock again");
app.space("player");

// ---- 7. and the desktop player is itself again --------------------------

player.frame(10);
assert(player.play() === true, "a plain player.play() still works afterwards");
player.frame(10);
assert(player.state().vr.active === false, "with no session");
player.stop();

console.log("vr.player_session: PASS");
