// vr.mirror_live — THE DESKTOP FOLLOWS THE RUNTIME, THROUGH THE APP
// (lane MIRROR-LIVE-1; the owner's finding F2 of the push-#50 smoke).
//
// WHAT HE FOUND, wearing the headset: "in the editor the 3D view is not the
// same as the VR view — a static image, or its own camera. It is supposed to be
// a copy of VR so we do not have two render pipelines."
//
// BOTH HALVES OF THAT ARE THE RULE. While the runtime is drawing eyes the
// desktop IS the eye's copy and its own View does not draw at all (one render
// pipeline, which is what buys 90 Hz). The moment the runtime stops asking for
// pictures — which on a real headset is EXACTLY when the wearer lifts it to
// look at the desk — there is no live eye to copy, so the desktop takes its own
// camera back and draws again. The engine owns that decision
// (VrSession::setDesktopShowsEye) and reports it as `vr.state().mirror.showing`.
//
// WHAT THIS SUITE ADDS TO `vr.session`, which asserts the same rule on PIXELS
// (readPixels of the real desktop target, both arms): the APP. The editor's own
// viewport, the real render driver, the real hosts — and the page rule beside
// it, because both are about a session that must not outlive what hosts it.
//
// THE MEASUREMENT IS `editor.viewportState().framesPresented`, which counts
// only frames a view really drew (OgreView::notePresented tests the enabled
// flag): zero while the eye is copied, one per frame while the desktop is its
// own. A script cannot read the window's pixels — that is what the engine suite
// is for — but "the editor's own View drew this frame" is the fact the owner's
// complaint is about, and it is exact.
//
// Monado never stops rendering, so the runner arms the pump's blink hook
// (JAHSHAKA_VR_TEST_BLINK_EVERY / _BLINK_FRAMES): every Nth frame of every
// session in this process is answered "no picture" for a stretch of frames, the
// way a doff does.
function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

project.create("vr mirror live " + Date.now());
scene.addPrimitive("Cube");
assert(app.columns().space === "editor", "the editor page is up");

var av = vr.available();
console.log("runtime: " + av.runtime + " " + av.version);
assert(av.available === true, "the runtime is there");

// ---- 1. THE SESSION, AND THE WISH ---------------------------------------
assert(vr.begin({}) === true, "vr.begin() with no options: " + app.lastError());
var st = vr.state();
assert(st.active === true, "the session is running");
assert(st.mirror.mode === "left",
       "AND THE DESKTOP IS A COPY BY DEFAULT: mirror.mode is " + st.mirror.mode);
assert(st.mirror.showing === "own",
       "...but it shows its OWN camera until an eye has been drawn (showing " +
       st.mirror.showing + ") — never a window onto a target nobody has written");

var waited = 0;
while (waited < 400 && vr.state().rendered === 0) { editor.frame(1); ++waited; }
assert(vr.state().rendered > 0, "the runtime asked for a picture after " + waited + " frames");
editor.frame(2);
assert(vr.state().mirror.showing === "eye",
       "THE DESKTOP BECOMES THE EYE'S COPY on the first drawn frame");

// ---- 2. FRAME BY FRAME: WHO DRAWS ---------------------------------------
//
// One pass over a window long enough to hold several of the runner's blinks,
// recording for every single frame which picture the engine says is on the
// desktop and whether the desktop's own View drew it. The two must agree on
// every frame, which is the whole rule:
//
//   showing "eye" -> the editor's View drew NOTHING (one pipeline)
//   showing "own" -> the editor's View drew THIS frame (live, not a still)
var prev = editor.viewportState().framesPresented;
var eyeFrames = 0, ownFrames = 0, drewOnEye = 0, missedOnOwn = 0;
for (var i = 0; i < 200; ++i) {
    editor.frame(1);
    var showing = vr.state().mirror.showing;
    var fp = editor.viewportState().framesPresented;
    var drew = fp > prev;
    prev = fp;
    if (showing === "eye") { ++eyeFrames; if (drew) ++drewOnEye; }
    else { ++ownFrames; if (!drew) ++missedOnOwn; }
}
console.log("over 200 frames: " + eyeFrames + " showed the eye, " + ownFrames +
            " the desktop's own camera");
assert(eyeFrames > 50, "the eye is what the desktop shows most of the time (" + eyeFrames + ")");
assert(ownFrames > 5,
       "AND THE RUNTIME'S BLINKS REALLY HAND IT BACK (" + ownFrames + " frames of own camera; " +
       "a 0 here means the hook is not armed and the rest of this case is vacuous)");
assert(drewOnEye === 0,
       "ONE RENDER PIPELINE: the editor's own View drew on NONE of the " + eyeFrames +
       " frames the headset's eye was copied (" + drewOnEye + ")");
assert(missedOnOwn === 0,
       "AND THE DESKTOP IS LIVE WHEN IT IS ITS OWN: it drew on every one of the " +
       ownFrames + " frames the runtime asked for no picture (" + missedOnOwn + " missed) — " +
       "this is the owner's F2");

// ---- 3. A FRESH PICTURE, NOT A REPEAT -----------------------------------
//
// "It drew" and "it drew something new" are different claims. Park on a frame
// where the desktop is its own, move the editor's camera, and its picture must
// follow — a frozen eye painted over it could not. (The pixels themselves are
// `vr.session`'s; here the camera really moves and the view really draws.)
var parked = 0;
while (parked < 400 && vr.state().mirror.showing !== "own") { editor.frame(1); ++parked; }
assert(vr.state().mirror.showing === "own", "parked on the desktop's own camera");
var camBefore = editor.camera();
editor.setCamera({ position: { x: camBefore.position.x + 1.5, y: camBefore.position.y + 0.8,
                               z: camBefore.position.z + 1.5 } });
var drewBefore = editor.viewportState().framesPresented;
editor.frame(1);
assert(editor.viewportState().framesPresented > drewBefore,
       "the editor's own View drew the frame after its camera moved");
var camAfter = editor.camera();
assert(Math.abs(camAfter.position.x - camBefore.position.x) > 1.0,
       "...from somewhere else (" + camBefore.position.x.toFixed(2) + " -> " +
       camAfter.position.x.toFixed(2) + ")");

// ---- 4. LEAVING THE EDITOR PAGE ENDS THE PREVIEW ------------------------
//
// THE OWNER'S PAGE RULE (2026-09-18, joint): a VR session is hosted BY a page —
// its mirror paints that page's view, its fly keys are that viewport's, the
// wearer stands where that camera stood. The Player has always ended its
// session with its page; the editor's preview does now. It ends by exactly the
// path `vr.end()` takes, so everything a manual end restores is restored.
assert(vr.state().active === true, "the session is still running");
assert(vr.locomotion().session === true, "...and holding the project's VR settings");
app.space("assets");
assert(app.columns().space === "assets", "the Assets page is up");
assert(vr.state().active === false,
       "LEAVING THE EDITOR PAGE ENDED THE EDITOR'S VR PREVIEW");
assert(vr.locomotion().session === false,
       "...and dropped the session's locomotion latch, as a manual end does");
assert(vr.state().preview.flyRedirected === false,
       "...and gave the editor viewport its own fly keys back");

app.space("editor");
var back = editor.viewportState().framesPresented;
editor.frame(5);
assert(editor.viewportState().framesPresented > back,
       "THE EDITOR VIEWPORT DRAWS ITS OWN PICTURE AGAIN (" + back + " -> " +
       editor.viewportState().framesPresented + ")");
assert(vr.state().active === false, "and the session did not come back with the page");

// ...and nothing was left behind: a second session begins on the same viewport.
assert(vr.begin({}) === true, "a SECOND session begins after the page round trip: " +
                              app.lastError());
waited = 0;
while (waited < 400 && vr.state().rendered === 0) { editor.frame(1); ++waited; }
assert(vr.state().rendered > 0, "and the runtime draws for it too");
editor.frame(2);
assert(vr.state().mirror.showing === "eye", "and the desktop is its copy again");
assert(vr.end() === true, "the second session ends by the verb");
editor.frame(3);
assert(vr.state().active === false, "it is over");
var ended = editor.viewportState().framesPresented;
editor.frame(5);
assert(editor.viewportState().framesPresented > ended,
       "A SESSION THAT ENDS LEAVES THE DESKTOP DRAWING (" + ended + " -> " +
       editor.viewportState().framesPresented + ")");

// ---- 5. THE SAME TWO RULES IN THE PLAYER --------------------------------
//
// The Player's host is the other half of "both hosts": the same engine rule
// paints its window, and its page rule is the one the owner confirmed as
// already correct (`vr.player_session` case 6b asserts the run stops with it;
// this asserts the MIRROR does what the editor's does).
app.space("player");
assert(app.columns().space === "player", "the Player page is up");
assert(player.play({ vr: true }) === true, "play in VR: " + app.lastError());
waited = 0;
while (waited < 400 && player.state().vr.rendered === 0) { player.frame(1); ++waited; }
assert(player.state().vr.rendered > 0, "the runtime draws for the Player too");
player.frame(2);
var pst = player.state().vr;
assert(pst.mirror.mode === "left" && pst.mirror.showing === "eye",
       "THE PLAYER'S WINDOW IS THE EYE'S COPY (" + JSON.stringify(pst.mirror) + ")");
var sawOwn = 0;
for (var p = 0; p < 120; ++p) {
    player.frame(1);
    if (player.state().vr.mirror.showing === "own") ++sawOwn;
}
assert(sawOwn > 0,
       "AND IT TAKES ITS OWN PICTURE BACK WHEN THE RUNTIME STOPS DRAWING (" + sawOwn +
       " of 120 frames)");
app.space("editor");
assert(player.state().vr.active === false,
       "LEAVING THE PLAYER PAGE ENDED THE PLAYER'S SESSION (the rule the owner confirmed)");
assert(player.playing() === false, "...and the run with it");

console.log("vr.mirror_live: PASS");
