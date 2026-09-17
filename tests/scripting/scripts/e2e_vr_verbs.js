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
assert(st.head.valid === false, "the head has not been located");
assert(st.hands.left.valid === false && st.hands.right.valid === false,
       "and neither hand has: `hands` is a pair of poses, always, valid or not");
assert(st.handActions === false, "no action set was attached (there is no session)");
assert(st.preview.active === false, "the editor's VR preview is not running");
assert(st.proxies === true, "the wearer's controller proxies are on by default (nothing to draw)");

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

// ---- the proxies answer with no session, and change nothing ---------------

assert(typeof vr.proxies() === "boolean", "vr.proxies() answers a boolean with no session");
assert(vr.proxies(false) === false, "...and can be turned off");
assert(vr.proxies(true) === true, "...and on again, without a session to draw them in");

// ---- and the two measuring verbs answer honestly with no session ----------
assert(vr.move({ forward: true }) === false,
       "vr.move refuses when the editor's VR preview is not running");
var moveErr = app.lastError();
assert(typeof moveErr === "string" && moveErr.indexOf("vr.move") >= 0,
       "...and says which verb refused: " + moveErr);
// AN UNKNOWN KEY IS AN ERROR, NOT A REFUSAL (player.vrMove's rule, shared): a
// misspelled intent that answered `false` would be indistinguishable from "no
// session", which is exactly the confusion the refuse/fail split exists for.
var threw = false;
try { vr.move({ sideways: true }); } catch (e) { threw = String(e).indexOf("sideways") >= 0; }
assert(threw, "vr.move THROWS on an unknown key, naming it");
var marker = vr.proxyPose("left");
assert(marker.drawn === false && marker.x === 0 && marker.y === 0 && marker.z === 0,
       "vr.proxyPose answers with no session at all: nothing is drawn, and the pose is zero");

// ---- and the editor is untouched -----------------------------------------

var id = scene.addPrimitive("Cube");
assert(!!id, "the editor still works after all that");
editor.frame(2);
assert(vr.state().active === false, "and still no session");

console.log("scripting.e2e.vr_verbs: PASS");
