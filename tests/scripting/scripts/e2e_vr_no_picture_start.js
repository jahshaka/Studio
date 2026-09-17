// vr.no_picture_start — THE OWNER'S FAILED WiVRn SMOKE, AS A SUITE
// (lane VR-3b, 2026-09-17; SPECS/VR_SPEC.md §6).
//
// WHAT THIS IS. On a real headset the runtime answers "no picture"
// (`shouldRender = 0`) for the first frames of a session — WiVRn does it for as
// long as it takes to synchronise. In that state the session's own View is
// switched off (VR_SPEC F4), the Player's View is off because the desktop shows
// the mirror, and the editor's viewport is hidden behind the Player page: the
// engine runs whole frames with NOTHING enabled. The owner's smoke died there —
// the host skipped the frame ("nothing is showing anywhere"), so the pump never
// called xrWaitFrame again, the runtime never synchronised and kept answering
// "no picture": a black headset, a spinning desktop, for ever.
//
// Monado's simulated HMD asks for a picture on its first or second frame and
// cannot be told otherwise, so the PUMP is told instead: the runner arms
// JAHSHAKA_VR_TEST_NO_RENDER_FRAMES (the frames are really waited for, begun and
// ended — only the runtime's answer to "do you want a picture" is replaced) and
// JAHSHAKA_VR_TEST_STOP_AFTER_FRAMES, which asks the runtime to take the session
// away so the real STOPPING event arrives — the owner's second run.
//
// The pixel half (the mirror must not paint an eye nobody has drawn) is in
// `vr.session`, where a readback can prove it. This is the APP: the Player page,
// the real driver, the real host.
function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

project.create("vr no picture start " + Date.now());
scene.addPrimitive("Cube");
app.space("player");
assert(app.columns().space === "player", "the Player page is up");

var av = vr.available();
console.log("runtime: " + av.runtime + " " + av.version);
assert(av.available === true, "the runtime is there");

var cam0 = editor.camera();
assert(player.play({ vr: true }) === true, "play in VR");
assert(player.state().vr.active === true, "the session is active");

// ---- 1. THE FRAME LOOP IS THE SESSION'S HEARTBEAT ------------------------
//
// Frames the runtime ACCEPTED must keep climbing while it asks for no picture.
// If the loop stops, `frames` stops: that IS the owner's bug, and it is a
// counter, not a clock.
var st = player.state().vr;
var seen = [];
for (var i = 0; i < 40; ++i) { player.frame(1); seen.push(player.state().vr.frames); }
st = player.state().vr;
console.log("after 40 frames of 'no picture': " + JSON.stringify(st));
assert(st.rendered === 0, "the runtime has asked for NO picture so far (rendered " +
                          st.rendered + ")");
assert(st.frames >= 35, "AND THE LOOP KEPT RUNNING WITH NOTHING ENABLED: " + st.frames +
                        " frames accepted");
assert(seen[39] > seen[0], "the count climbed every stretch (" + seen[0] + " -> " + seen[39] + ")");
assert(st.active === true, "the session is still alive");
assert(st.state === "synchronized" || st.state === "visible" || st.state === "focused",
       "and the runtime SYNCHRONISED while it was being pumped (" + st.state + ")");
assert(!app.lastError(), "and nothing has failed: " + JSON.stringify(app.lastError()));

// ---- 2. THE PICTURE ARRIVES ---------------------------------------------
var waited = 0;
while (waited < 400 && player.state().vr.rendered === 0) { player.frame(1); ++waited; }
st = player.state().vr;
console.log("first picture after " + waited + " more frames: " + JSON.stringify(st));
assert(st.rendered > 0, "THE PICTURE ARRIVES once the runtime wants one (" + st.rendered + " drawn)");

var located = 0;
while (located < 400 && player.state().vr.posesValid !== true) { player.frame(1); ++located; }
assert(player.state().vr.posesValid === true, "the wearer's head is located");
player.frame(2);
var head = player.state().vr.head;
console.log("the wearer stands at " + JSON.stringify(head) + ", the run began at " +
            JSON.stringify(cam0.position));
assert(isFinite(head.x) && isFinite(head.y) && isFinite(head.z),
       "and stands somewhere real (the placement's exactness is vr.player_session's case)");

// ---- 3. THE RUNTIME TAKES THE SESSION AWAY ------------------------------
//
// The owner's second run: READY -> SYNCHRONIZED -> stopped inside a second,
// with nothing downstream noticing. A stopped session is OVER: `active` says
// so, the engine ends it, and the Player is itself again.
var pumped = 0;
while (pumped < 900 && player.state().vr.active === true) { player.frame(1); ++pumped; }
st = player.state().vr;
console.log("after " + pumped + " frames the session reads " + JSON.stringify(st));
assert(st.active === false, "A SESSION THE RUNTIME STOPPED IS OVER (active is false)");
assert(st.frames === 0 && st.rendered === 0,
       "...and the engine ended it — the status is a no-session status again");
assert(player.playing() === true, "THE SCENE IS STILL PLAYING (only VR ended)");

// AND THE PLAYER IS ITSELF AGAIN: the desktop run goes on, frame after frame,
// on its own clock — the state the owner's run never reached (its driver spun
// at the session's zero interval against a pump that was gone).
player.frame(10);
assert(player.playing() === true, "and it keeps playing on the editor's own clock");
assert(player.state().vr.active === false, "still no session");
assert(player.stop() === true, "the run stops");

// ...and VR can be entered again in the same process (the hook is armed for
// every session in this run, so this one starts blind too and still works).
assert(player.play({ vr: true }) === true, "and a NEW session can begin afterwards");
player.frame(20);
assert(player.state().vr.frames > 0, "which the runtime accepts frames from (" +
                                     player.state().vr.frames + ")");
player.stop();
assert(player.state().vr.active === false, "player.stop() ends it with the run");

console.log("vr.no_picture_start: PASS");
