// scripting.e2e.player_vr — THE PLAYER'S VR MODE WITH NO RUNTIME
// (SPECS/VR_SPEC.md §4.5, phase 3).
//
// THE CASE THAT RUNS EVERYWHERE, and the one the owner's rule is about: "it
// works without a headset exactly as today". This process was launched WITHOUT
// `--vr`, so no OpenXR loader was ever opened — and every VR-shaped call must
// refuse in words, immediately, WITHOUT starting anything: no session, no page
// switch, and above all no scene left running that nobody asked to run.
//
// The two halves that need a runtime are `vr.player_session` (the Player in
// Monado's simulated headset) and `player.vr` (the rig's arithmetic, which
// needs nothing at all and is pure C++).

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

project.create("player vr " + Date.now());
scene.addPrimitive("Cube");

// ---- the state verb answers, with no runtime and no session ---------------

var st = player.state();
console.log("player.state: " + JSON.stringify(st));
assert(st.available === true, "there is a player in this session");
assert(typeof st.vr === "object" && st.vr !== null, "player.state() carries a `vr` block");
assert(st.vr.available === false, "VR is not available (this process has no --vr)");
assert(typeof st.vr.reason === "string" && st.vr.reason.length > 0,
       "and it says why, in words: " + st.vr.reason);
assert(st.vr.active === false, "no session is running");
assert(st.vr.state === "unavailable", "the runtime lifecycle word is `unavailable`");
assert(st.vr.frames === 0 && st.vr.rendered === 0, "no frames have been submitted");

// ---- player.play({vr:true}) REFUSES, and does not enter play -------------

var t0 = Date.now();
assert(player.play({ vr: true }) === false, "player.play({vr:true}) refuses with no runtime");
assert(Date.now() - t0 < 5000, "and it refuses IMMEDIATELY (" + (Date.now() - t0) + " ms)");
var err = app.lastError();
console.log("app.lastError: " + err);
assert(err.indexOf("player.play") >= 0, "the refusal is recorded as the last error");
assert(err.indexOf("VR is not available") >= 0, "and it names the reason: " + err);
assert(player.playing() === false,
       "AND THE SCENE IS NOT RUNNING: a caller that asked for VR is not given a flat player");

// ---- the other VR verbs refuse the same way ------------------------------

assert(player.endVr() === false, "player.endVr() refuses when the player is not in VR");
// `player.vrMove` was DELETED in VR input stage 1: `vr.move` is the one
// locomotion verb and it moves whoever is in the headset (editor preview or
// Player). With no session at all it refuses, naming both hosts.
assert(typeof player.vrMove === "undefined", "player.vrMove is GONE (vr.move replaced it)");
assert(vr.move({ forward: true }) === false, "vr.move() refuses with nobody in VR");
assert(app.lastError().indexOf("nobody is in VR") >= 0,
       "...saying nobody is in VR: " + app.lastError());
assert(player.vrRecenter() === false, "player.vrRecenter() refuses");

// ---- the toggle refuses WITHOUT touching the page or the scene -----------

var spaceBefore = app.columns().space;
assert(vr.toggle() === false, "vr.toggle() refuses with no runtime");
console.log("app.lastError: " + app.lastError());
assert(app.lastError().indexOf("vr.toggle") >= 0, "and records why");
assert(app.columns().space === spaceBefore,
       "the space did not change (" + spaceBefore + "): a failed toggle moves nothing");
assert(player.playing() === false, "and nothing started playing");

// ---- the flat player is EXACTLY what it always was ------------------------

assert(player.play() === true, "player.play() — no argument — still starts the scene");
assert(player.playing() === true, "and it is playing");
assert(player.state().vr.active === false, "with no VR session, of course");
assert(player.stop() === true, "player.stop() stops it");
assert(player.playing() === false, "and it is stopped");

// ---- unknown options THROW, they are not ignored -------------------------
//
// The house split, and it is a real distinction: a REFUSAL is an answer to a
// well-formed question ("no headset") and must not abort the caller's script;
// a MALFORMED CALL is a bug in the script and throws, like every other verb's
// option check.

function throws(fn, needle, msg) {
    try { fn(); } catch (e) {
        assert(String(e).indexOf(needle) >= 0, msg + " (" + e + ")");
        return;
    }
    throw new Error("assert failed: " + msg + " — it did not throw");
}
throws(function () { player.play({ nosuchoption: 1 }); }, "nosuchoption",
       "an unknown play option throws, naming the option");
throws(function () { vr.move({ sideways: true }); }, "sideways",
       "an unknown vr.move key throws, naming the key");

// ---- and the editor is untouched -----------------------------------------

editor.frame(2);
assert(player.state().vr.active === false, "still no session after all that");

console.log("scripting.e2e.player_vr: PASS");
