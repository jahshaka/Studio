// scripting.e2e.vr_verbs — the `vr.*` verbs WITH NO RUNTIME (SPECS/VR_SPEC.md §6).
//
// THE CASE THAT RUNS EVERYWHERE, and the one that matters most often: this
// process was launched WITHOUT `--vr`, so no OpenXR loader was ever opened and
// the engine booted exactly as it has always booted. Every verb must still
// answer — with a refusal, in words, immediately.
//
// It is the executable form of VR_SPEC §0's constraint ("without a headset the
// tool is today's tool, unchanged"): an editor that throws, hangs or crashes
// when a script asks about VR on a box with no headset is not that tool. The
// companion suite `vr.verbs_session` runs the same verbs against Monado's
// simulated headset; this one needs nothing at all.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

project.create("vr verbs " + Date.now());

// ---- available ------------------------------------------------------------

var a = vr.available();
console.log("vr.available: " + JSON.stringify(a));
assert(typeof a === "object" && a !== null, "vr.available() answers a map");
assert(a.available === false, "no runtime was asked for, so VR is not available");
assert(typeof a.reason === "string" && a.reason.length > 0,
       "and it says WHY, in words: " + a.reason);
assert(a.runtime === "", "no runtime is named");
assert(a.eyeSize.length === 2, "eyeSize is a pair even when there is no runtime");
assert(vr.info().available === a.available, "vr.info() is the same answer");

// ---- state ----------------------------------------------------------------

var st = vr.state();
console.log("vr.state: " + JSON.stringify(st));
assert(st.active === false, "no session is running");
assert(st.state === "unavailable", "the state is `unavailable`, not a lie about idling");
assert(st.frames === 0, "no frames have been submitted");
assert(st.ipd === 0, "no eyes have been located");

// ---- begin / end refuse, they do not throw and they do not hang -----------

var t0 = Date.now();
assert(vr.begin() === false, "vr.begin() refuses when there is no runtime");
assert(Date.now() - t0 < 5000, "and it refuses IMMEDIATELY (" + (Date.now() - t0) + " ms)");
var err = app.lastError();
console.log("app.lastError: " + err);
assert(typeof err === "string" && err.indexOf("vr.begin") >= 0,
       "the refusal is recorded as the session's last error");

assert(vr.begin({ mirror: "right", worldScale: 2 }) === false,
       "...with options too, and the options are not what it refuses over");
assert(vr.end() === false, "vr.end() refuses when no session is running");

// ---- and the editor is untouched -----------------------------------------

var id = scene.addPrimitive("Cube");
assert(!!id, "the editor still works after all that");
editor.frame(2);
assert(vr.state().active === false, "and still no session");

console.log("scripting.e2e.vr_verbs: PASS");
