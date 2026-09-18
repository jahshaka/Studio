// vr.no_picture_start — THE OWNER'S FAILED WiVRn SMOKE, AS A SUITE
// (lane VR-3b, 2026-09-17; SPECS/VR_SPEC.md §6).
//
// WHAT THIS IS. On a real headset the runtime answers "no picture"
// (`shouldRender = 0`) for the first frames of a session — WiVRn does it for as
// long as it takes to synchronise. The owner's smoke died there: with the
// session's own View off (VR_SPEC F4), the Player's View off and the editor
// hidden, the host skipped the frame ("nothing is showing anywhere"), so the
// pump never called xrWaitFrame again, the runtime never synchronised and kept
// answering "no picture": a black headset, a spinning desktop, for ever.
//
// WHAT THIS SUITE PROVES (the lead's second read, 2026-09-17): that the pump
// keeps accepting frames while the runtime asks for none, that the runtime then
// synchronises, that the desktop keeps ITS OWN picture until the first drawn eye
// (the Player's View stays ON through the no-picture stretch since VR-3b), and
// that a runtime-stopped session hands the Player back. It does NOT put the
// driver's "nothing enabled" skip on the line — with a session alive
// `hasEnabledViews()` is true by construction (OgreEngine.cpp), and no view is
// off here; a driver-level case for that heartbeat is a named follow-up.
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
// THE GI CHAIN BEFORE THE NO-PICTURE STRETCH (lane V1-RIG fix round item 1).
// The chain is the headset's from the frame the session's View declares itself
// the GI driver, and the wearer stands still here: a no-picture frame is not an
// event in the world and must cost the chain nothing.
function chainWork() {
    var g = world.giStatus();
    var total = 0, pending = 0;
    for (var c = 0; c < g.cascades.length; ++c) {
        total += g.cascades[c].rebuilds;
        pending += g.cascades[c].pending;
    }
    return { rebuilds: g.rebuilds, full: g.cascadeFullRebuilds, cascade: total,
             pending: pending, follows: g.ifdFollows, vr: g.cascadeProfileVr };
}
// A BASELINE IS ONLY HONEST ON A QUIET CHAIN. The scheduler spends at most one
// rebuild a frame, so a chain that owes several (the one profile rebuild a
// session start costs queues every cascade) is still working for a few frames
// after the event — and a window opened on top of that reads the queue draining
// as work the window caused. `pending` is the queue; this drains it, bounded.
function quietChain(label) {
    for (var q = 0; q < 120; ++q) {
        var w = chainWork();
        if (w.pending === 0) {
            // ...and two more frames, because the LAST rebuild of the queue owes
            // the irradiance field's follow on the frame after it.
            player.frame(2);
            console.log(label + ": the chain is quiet after " + q + " frames");
            return chainWork();
        }
        player.frame(1);
    }
    return chainWork();
}
// THE BASELINE IS TAKEN AFTER THE FIRST FEW FRAMES, not at `play`, and the
// measurement is why: at the moment a run enters VR there is no chain at all
// (`cascades` is empty — a camera-centred arm is not built before a camera has
// been tracked), so the first frames legitimately contain the chain's ONE
// from-scratch build. What must not happen is a build on the frames AFTER that,
// which is what the defect did — one per no-picture frame, for ever.
var giBefore = null;
var kBaselineFrame = 5;
for (var i = 0; i < 40; ++i) {
    player.frame(1);
    seen.push(player.state().vr.frames);
    if (i === kBaselineFrame) {
        giBefore = chainWork();
        console.log("the chain after " + (i + 1) + " frames (the baseline): " +
                    JSON.stringify(giBefore));
    }
}
st = player.state().vr;

// ---- 1b. A NO-PICTURE FRAME IS NOT A CHAIN REBUILD -----------------------
//
// THE DEFECT THIS CATCHES (V1-RIG fix round item 1, found by the Fable read).
// The session switches its own View OFF on every `shouldRender = 0` frame and on
// every frame with no valid pose — which is what keeps the desktop drawing
// through a doff, an open dashboard or WiVRn's first frames. The GI driver
// election used to test `isEnabled()` BEFORE the priority test, so on each of
// those frames the driver fell to the desktop view and came back on the next;
// `updateGiTracking` read the flip as a change of driver PROFILE (desktop
// against VR: different cascade COUNT and step), raised the chain-shape debt and
// the flush answered it with `rebuildVct()` — a teardown, every cascade and the
// whole irradiance field. TWICE per doff, with the chain's centre jumping to the
// editor's camera in between, and on EVERY WiVRn session start, because its
// first frames are no-picture ones.
//
// So: forty no-picture frames, a still wearer, and the chain must not have moved
// a single counter. It is not a timing assertion and not a budget: it is
// "nothing happened, so nothing was rebuilt".
var giAfter = chainWork();
console.log("the chain after 40 no-picture frames: " + JSON.stringify(giAfter));
assert(giBefore !== null, "the baseline was taken");
assert(giAfter.rebuilds === giBefore.rebuilds,
       "THIRTY-FOUR MORE NO-PICTURE FRAMES REBUILD THE GI ARM NOT ONCE (" +
       giBefore.rebuilds + " -> " + giAfter.rebuilds + ")");
assert(giAfter.full === giBefore.full,
       "...and trip the teleport guard not once (" + giBefore.full + " -> " + giAfter.full + ")");
assert(giAfter.cascade === giBefore.cascade,
       "...and re-voxelise no cascade (" + giBefore.cascade + " -> " + giAfter.cascade + ")");
assert(world.giStatus().cascades.length > 0,
       "...on a chain that EXISTS by now (the assertions are not vacuous)");
assert(giAfter.follows === giBefore.follows,
       "...and re-place the irradiance field not once (" + giBefore.follows +
       " -> " + giAfter.follows + ")");
// AND THE PROFILE FOLLOWED THE SESSION, NOT THE FRAME: whatever column the chain
// was built from before the stretch, it is still that column after it. (With a
// session alive and a chain built it is the VR one; the assertion is the
// EQUALITY, so it holds at a tier with no chain too.)
assert(giAfter.vr === giBefore.vr,
       "the cascade profile followed the SESSION and not the frame (" + giBefore.vr +
       " -> " + giAfter.vr + ")");
console.log("after 40 frames of 'no picture': " + JSON.stringify(st));
assert(st.rendered === 0, "the runtime has asked for NO picture so far (rendered " +
                          st.rendered + ")");
assert(st.frames >= 35, "AND THE PUMP KEPT ACCEPTING FRAMES WHILE THE RUNTIME ASKED FOR NO PICTURE: " + st.frames +
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

// ---- 2b. A BLINKING RUNTIME COSTS THE GI CHAIN NOTHING -------------------
//
// THE TOGGLE, which the stretch above cannot produce. `NO_RENDER_FRAMES` makes
// the session's own View go off ONCE and come back ONCE; a real runtime switches
// it off and on again all day — a doff, an open dashboard, a guardian breach, a
// moment of lost tracking. `JAHSHAKA_VR_TEST_BLINK_EVERY` (armed by the runner)
// answers every Nth frame "no picture" for as long as the session lives, which
// is that cycle.
//
// WHY IT IS WORTH ASSERTING. The GI driver election used to test `isEnabled()`
// BEFORE the priority test, so each of those off-frames could hand the chain to
// another view of the same scene and each hand-over was read as a change of
// driver PROFILE — a whole-chain rebuild, twice per cycle, with the chain's
// centre jumping to the other camera in between. The lane MEASURED that no such
// view exists today (in a live session `app.engineObjects().enabledViews` is 1,
// in both hosts, even with the selection inset open — the Player's View is
// switched off by design and the editor's desktop view is taken over by the
// mirror), so the hand-over is LATENT rather than live. The election now reads
// only `giPriority()`, which makes "the profile follows the SESSION, not the
// frame" true by construction instead of true by accident, and this is the net
// that keeps it that way.
var giBlink = quietChain("before the blinking window");
console.log("the chain before the blinking window: " + JSON.stringify(giBlink));
for (var bf = 0; bf < 100; ++bf) player.frame(1);
var giBlinked = chainWork();
console.log("the chain after 100 blinking frames: " + JSON.stringify(giBlinked));
assert(player.state().vr.active === true, "the session survived the blinking");
assert(giBlinked.rebuilds === giBlink.rebuilds,
       "A BLINKING RUNTIME REBUILDS THE GI ARM NOT ONCE (" + giBlink.rebuilds + " -> " +
       giBlinked.rebuilds + ")");
assert(giBlinked.cascade === giBlink.cascade,
       "...and re-voxelises no cascade (" + giBlink.cascade + " -> " + giBlinked.cascade +
       "; the wearer's own sway is inside the re-centre's band)");
assert(giBlinked.vr === giBlink.vr,
       "...and the cascade profile never flips (" + giBlink.vr + " -> " + giBlinked.vr + ")");

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
