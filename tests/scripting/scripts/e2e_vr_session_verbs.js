// vr.verbs_session — the `vr.*` verbs against a real OpenXR runtime
// (SPECS/VR_SPEC.md §6; the runner starts Monado's SIMULATED headset).
//
// The other half of `scripting.e2e.vr_verbs`: that one proves the verbs refuse
// cleanly with no runtime, this one proves they WORK with one — from a script,
// which is the whole of phase 2's editor surface (there is no button yet, and
// phase 3's will call these same verbs).
//
// FRAMES, NEVER WALL TIME. `editor.frame(n)` renders n frames, and inside a
// session each of those blocks in xrWaitFrame until the runtime wants the next
// picture — so this script is paced by the runtime, and what it asserts is a
// COUNT the runtime accepted (VR_SPEC §6 flake class (b)).

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

/// A vector turned by a quaternion ({x,y,z,w} or the document's {x,y,z,scalar}).
function rotQ(q, v) {
    var x = q.x, y = q.y, z = q.z;
    var w = (q.w === undefined) ? q.scalar : q.w;
    var ix = w * v.x + y * v.z - z * v.y;
    var iy = w * v.y + z * v.x - x * v.z;
    var iz = w * v.z + x * v.y - y * v.x;
    var iw = -x * v.x - y * v.y - z * v.z;
    return { x: ix * w + iw * -x + iy * -z - iz * -y,
             y: iy * w + iw * -y + iz * -x - ix * -z,
             z: iz * w + iw * -z + ix * -y - iy * -x };
}

project.create("vr session verbs " + Date.now());
scene.addPrimitive("Cube");

// ---- available ------------------------------------------------------------

var a = vr.available();
console.log("vr.available: " + JSON.stringify(a));
assert(a.available === true, "the runtime answered: " + a.runtime);
assert(a.runtime.indexOf("Monado") >= 0, "and it is the runtime the runner named");
assert(a.openxr.length > 0, "the OpenXR version it negotiated is reported: " + a.openxr);
assert(a.eyeSize[0] > 0 && a.eyeSize[1] > 0,
       "with a recommended eye size of " + a.eyeSize[0] + "x" + a.eyeSize[1]);

// ---- begin ----------------------------------------------------------------

assert(vr.begin({ mirror: "left" }) === true, "vr.begin() starts a session");
assert(vr.begin() === false, "a second vr.begin() refuses rather than starting two");

var st = vr.state();
console.log("vr.state after begin: " + JSON.stringify(st));
assert(st.active === true, "the session is active");
assert(st.mirror === "left", "the mirror is the left eye");
assert(st.eyeSize[0] > 0, "the session renders " + st.eyeSize[0] + "x" + st.eyeSize[1] + " per eye");

// ---- THE EDITOR'S VR PREVIEW (phase 4) ------------------------------------
// The desktop viewport goes on being an editor while somebody wears the scene:
// its camera is NOT the wearer, the wearer's fly keys are, and the wearer shows
// up in it as proxies.
assert(st.preview.active === true, "the editor's VR preview owns this session");
assert(st.preview.flyRedirected === true,
       "the viewport's fly keys move the WEARER while it runs");
assert(st.proxies === true, "and the wearer's controller proxies are on by default");
assert(st.preview.helpers === true,
       "THE WEARER SEES THE EDITOR WORKING: an editor preview opens the desk's helper " +
       "channel in the headset (the Player's VR mode does not)");
var camBefore = editor.camera();

// ---- the runtime paces the frames ----------------------------------------

editor.frame(90);
st = vr.state();
console.log("vr.state after 90 frames: " + JSON.stringify(st));
assert(st.frames >= 60, "the runtime ACCEPTED " + st.frames + " frames");
assert(st.rendered >= 50, "and asked us to draw " + st.rendered + " of them");
assert(st.state === "focused", "the session reached `focused` (it is " + st.state + ")");
assert(st.ipd > 0.03 && st.ipd < 0.10, "the eyes are " + st.ipd.toFixed(4) + " m apart");
assert(st.space === "stage" || st.space === "local", "the reference space is " + st.space);
assert(st.refreshHz > 0, "the runtime's cadence is " + st.refreshHz.toFixed(1) + " Hz");

// ---- the poses: the head, and the two hands -------------------------------

console.log("vr.state head: " + JSON.stringify(st.head));
console.log("vr.state hands: " + JSON.stringify(st.hands));
assert(st.head.valid === true, "the head is located in world space");
assert(typeof st.head.yaw === "number", "with a heading of " + st.head.yaw.toFixed(1) + " degrees");
assert(st.handActions === true,
       "THE ACTION SET IS ATTACHED: two grip poses on the simple-controller profile");
assert(typeof st.hands.left.valid === "boolean" && typeof st.hands.right.valid === "boolean",
       "and both hands answer (left " + st.hands.left.valid + ", right " +
       st.hands.right.valid + ")");
if (st.hands.left.valid) {
    assert(isFinite(st.hands.left.x) && isFinite(st.hands.left.y) && isFinite(st.hands.left.z),
           "the left hand is at a finite world pose");
}

// ---- THE CONTROLS, THROUGH THE APP (phase 4b stage 1) --------------------
// The engine suite `vr.session` asserts the action set, the four binding blocks
// and the injection rule at the boundary; this is the same thing through the
// VERBS, which is what a script, the MCP server and the Studio-side interaction
// service actually call.
assert(st.bindings.offered >= 3 && st.bindings.accepted === st.bindings.offered,
       "THE SUGGESTED BINDINGS PARSE: the runtime took " + st.bindings.accepted + " of the " +
       st.bindings.offered + " profiles offered");
assert(st.profile.indexOf("/interaction_profiles/") === 0,
       "...and it says which one it bound: " + st.profile);
assert(typeof st.input.left.valid === "boolean" && typeof st.input.right.valid === "boolean",
       "both hands report their controls (left " + st.input.left.valid + ", right " +
       st.input.right.valid + ")");
if (st.input.left.valid) {
    assert(st.input.left.grip.valid === st.hands.left.valid,
           "input.grip and hands are the same answer");
    assert(st.input.left.fromInjection === false,
           "...and nothing here is an injected sample: this is the runtime's own");
    console.log("left aim " + JSON.stringify(st.input.left.aim));
}
// THE REFUSAL RULE, in the app: a runtime with a bound profile wins.
assert(vr.inject("left", { grip: { x: 5, y: 5, z: 5 } }) === false,
       "an injection is REFUSED while the runtime reports a bound profile");
assert(app.lastError().indexOf("JAHSHAKA_VR_TEST_INJECT") >= 0,
       "...and names the one explicit override: " + app.lastError());
assert(vr.state().input.left.fromInjection === false, "...and nothing was written");
// THE ONE OUTPUT. A simulated controller has no haptic output and the call
// still succeeds — a controller that cannot buzz is a supported controller.
assert(vr.haptic("right", 1, 0.05) === true,
       "vr.haptic is accepted by the runtime (nothing buzzes on a simulated controller)");

// ---- THE EDITOR'S CAMERA IS NOT THE WEARER --------------------------------
// Nothing writes the head pose back into the document: take the headset off and
// the viewport is exactly where you left it. (The Player's VR mode does the
// opposite, deliberately — there the wearer IS the play camera.)
var camNow = editor.camera();
assert(Math.abs(camNow.position.x - camBefore.position.x) < 1e-4 &&
       Math.abs(camNow.position.y - camBefore.position.y) < 1e-4 &&
       Math.abs(camNow.position.z - camBefore.position.z) < 1e-4,
       "the editor's camera has not moved: the wearer is not this camera");

// ---- the proxies are a verb, not a mode -----------------------------------

assert(vr.proxies(false) === false, "vr.proxies(false) turns the wearer's controllers off");
editor.frame(4);
assert(vr.state().proxies === false, "...and the state says so");
assert(vr.proxies(true) === true, "vr.proxies(true) turns them back on");
editor.frame(4);
assert(vr.state().proxies === true, "...and back on in the state");

// ---- THE WEARER'S HANDS ARE DRAWN WHERE THEY ARE THIS FRAME ---------------
// (VR-4-FIX finding 4.) The runtime does not locate the hands until the frame
// is already being rendered — inside renderOneFrame, after every host tick has
// run — so a marker positioned from the host's own tick necessarily draws the
// frame before last's pose (measured at two frames, ~22 ms at 90 Hz). The
// session places the proxy nodes itself now, between the locate and the draw.
//
// `vr.proxyPose` is where the marker ACTUALLY IS (read back out of the scene
// graph); `vr.state().hands` is what the runtime reported. The measurement is
// the number of FRAMES between a rig move and the two agreeing.

function dist3(a, b) {
    var dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return Math.sqrt(dx * dx + dy * dy + dz * dz);
}

editor.frame(2);
var handsNow = vr.state().hands;
var markerL = vr.proxyPose("left");
console.log("left hand " + JSON.stringify(handsNow.left) + " marker " + JSON.stringify(markerL));
var handsLocated = handsNow.left.valid === true;
assert(markerL.drawn === handsLocated,
       "a located hand has a marker in the scene and an unlocated one has none (located=" +
       handsLocated + ")");

if (handsLocated) {
    assert(dist3(markerL, handsNow.left) < 1e-3,
           "the marker is drawn exactly where the runtime says the hand is");
    // MOVE THE RIG — the room the wearer stands in — and watch the marker
    // follow. The hands ride the rig (they are composed through it), so this
    // moves the world pose of both by a metre without a headset moving at all.
    var before = { x: markerL.x, y: markerL.y, z: markerL.z };
    assert(vr.move({ forward: true, seconds: 1.0 }) === true,
           "vr.move walks the wearer, the same step the held fly keys make");
    var lag = -1;
    for (var f = 1; f <= 8; ++f) {
        editor.frame(1);
        var hands = vr.state().hands;
        var marker = vr.proxyPose("left");
        if (!hands.left.valid || !marker.drawn) continue;
        if (dist3(marker, before) < 1e-4) continue;          // nothing has moved yet
        if (dist3(marker, hands.left) < 1e-3) { lag = f; break; }
    }
    console.log("LAG: the marker matched this frame's hand pose after " + lag + " frame(s)");
    assert(lag === 1,
           "THE MARKER IS DRAWN AT THIS FRAME'S POSE: one frame after the rig moved it is " +
           "already where the runtime put the hand (it was two frames behind when the host " +
           "pushed the pose from outside the frame loop)");
}

// ---- A USER'S SCREENSHOT HAS NO CONTROLLERS IN IT -------------------------
// (VR-4-FIX finding 2.) The proxies are helpers in BOTH channels, so a view
// whose ordinary helper channel is open draws them — and the offscreen view a
// user's screenshot renders through had it open. The Scene grade is the user's
// door and now opens neither channel; the script grades are measuring
// instruments and keep what they had (several suites photograph a gizmo).
if (handsLocated) {
    var m = vr.proxyPose("left");
    // END-ON, down the wand's own pointer: the marker's local -Z is where the
    // pointer runs, so standing behind it puts the whole wand in the middle of
    // the frame. The probe run is a horizontal line of single pixels across the
    // centre, which is what catches a wireframe marker at all.
    var fwd = rotQ(m.rotation, { x: 0, y: 0, z: -1 });
    editor.setCamera({ position: { x: m.x - fwd.x * 0.45,
                                   y: m.y - fwd.y * 0.45,
                                   z: m.z - fwd.z * 0.45 },
                       lookAt: { x: m.x, y: m.y, z: m.z } });
    var probes = [];
    for (var i = -20; i <= 20; ++i) probes.push({ x: 0.5 + i / 256.0, y: 0.5 });
    editor.frame(2);

    function probeRun(name, grade) {
        var shot = editor.screenshot(name + ".png", 256, 256, probes, grade);
        var out = [];
        for (var k = 0; k < shot.probes.length; ++k)
            out.push([shot.probes[k].r, shot.probes[k].g, shot.probes[k].b]);
        return out;
    }
    function differing(a, b) {
        var n = 0;
        for (var k = 0; k < a.length; ++k)
            if (a[k][0] !== b[k][0] || a[k][1] !== b[k][1] || a[k][2] !== b[k][2]) ++n;
        return n;
    }

    var plainOn  = probeRun("vr_shot_plain_on", "plain");
    var sceneOn  = probeRun("vr_shot_scene_on", "scene");
    assert(vr.proxies(false) === false, "the controllers off for the control shots");
    editor.frame(2);
    var plainOff = probeRun("vr_shot_plain_off", "plain");
    var sceneOff = probeRun("vr_shot_scene_off", "scene");
    assert(vr.proxies(true) === true, "...and back on");
    editor.frame(2);

    var plainMoved = differing(plainOn, plainOff);
    var sceneMoved = differing(sceneOn, sceneOff);
    console.log("probe run across the marker: plain " + plainMoved + " of " + probes.length +
                " pixels move with the controllers, scene " + sceneMoved);
    assert(plainMoved > 0,
           "THE CONTROL: the marker really is in this picture — a script-grade shot (a " +
           "measuring instrument, which keeps the editor's furniture) draws it");
    assert(sceneMoved === 0,
           "A USER'S SCREENSHOT HAS NO CONTROLLERS IN IT: the Scene grade is byte-identical " +
           "with the wearer's hands on and off");
}

// ---- THE DESKTOP IS STILL AN EDITOR: only the FLY is the wearer's ---------
// (VR-4-FIX finding 5.) The viewport used to skip the whole camera controller
// update while a session ran, which also skipped the arcball's axis-view lerp —
// so the Views dropdown froze for the length of a session. Only the fly belongs
// to the wearer.
assert(editor.setCameraMode("orbit") === true, "the arcball controller, whose snap is a lerp");
assert(editor.setView("top") === true, "the Views dropdown asks for the top view");
editor.frame(30);
var topCam = editor.camera();
var down = rotQ(topCam.rotation, { x: 0, y: 0, z: -1 });
console.log("after 30 frames in a session the camera looks " + JSON.stringify(down));
assert(down.y < -0.99,
       "THE AXIS-VIEW LERP STILL RUNS WITH A HEADSET ON: the camera arrived at the top view " +
       "(it never left its old pose while the whole controller update was skipped)");
assert(editor.setView("perspective") === true, "back to perspective");
assert(editor.setCameraMode("free") === true, "and to the free camera");
editor.frame(4);

// ---- end ------------------------------------------------------------------

assert(vr.end() === true, "vr.end() ends it");
assert(vr.end() === false, "and a second vr.end() refuses");
st = vr.state();
assert(st.active === false, "no session is running");
assert(st.frames === 0, "the counters belong to the session, not to the process");
assert(st.preview.active === false, "the preview is over");
assert(st.preview.flyRedirected === false,
       "AND THE VIEWPORT HAS ITS FLY KEYS BACK — an editor whose arrows stayed dead after " +
       "a session would be worse than one that never had VR");
assert(st.head.valid === false && st.hands.left.valid === false,
       "and no pose survives the session");

// ---- and the editor still renders -----------------------------------------

editor.frame(5);
assert(vr.begin() === true, "a session can be started AGAIN in the same process");
editor.frame(20);
var second = vr.state();
assert(second.frames >= 5, "the second session submits frames too");
// THE DEFAULT MIRROR IS `none` FOR THE EDITOR (vr.begin's contract): the
// desktop keeps drawing the EDITOR's picture, with the wearer's proxies in it,
// rather than paying for two renders and showing one. The first session above
// asked for "left" explicitly and got it.
assert(second.mirror === "none",
       "and it takes no mirror by default: the desktop stays an editor");
assert(vr.end() === true, "and ends");
editor.frame(5);

// ---- THE WORLD GOES WHILE SOMEBODY IS WEARING IT --------------------------
// (VR-4-FIX finding 1: a use-after-free, and the worst defect in the lane.)
// The session renders the viewport's ENGINE SCENE and holds a raw pointer to
// it — dereferenced every frame for its stereo quads and again in its own
// destructor — and NOTHING on the project-close path ended it. Opening another
// project (or closing this one) freed the world under a live session.
//
// Two answers, and this exercises both: the viewport tells the preview before
// it clears the scene (so the fly keys come back), and Engine::destroyScene
// ends a session bound to the scene it is about to destroy whoever asks.

assert(vr.begin() === true, "a session for the close test");
editor.frame(5);
assert(vr.state().active === true, "it is running");
project.create("vr session close " + Date.now());     // teardown, then a new world
editor.frame(5);
st = vr.state();
console.log("vr.state after the project changed under the session: " + JSON.stringify(st));
assert(st.active === false,
       "THE WORLD CLOSING ENDS THE SESSION — it is not left rendering a freed scene");
assert(st.preview.active === false, "the preview knows it is over");
assert(st.preview.flyRedirected === false,
       "...and the viewport has its fly keys back, on the new world");

// AND THE EDITOR IS WHOLE AFTERWARDS: the new world renders, and VR starts
// again on it. (A teardown that left the engine half-ended would show up here.)
scene.addPrimitive("Cube");
editor.frame(5);
assert(vr.begin() === true, "VR starts again on the world that replaced it");
editor.frame(10);
assert(vr.state().frames > 0, "and the runtime accepts its frames");
assert(vr.end() === true, "ended");
editor.frame(5);

console.log("vr.verbs_session: PASS");
