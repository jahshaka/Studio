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

// ---- THE INPUT HOOK, WITH NO RUNTIME AT ALL (phase 4b stage 1) -----------
//
// This is the backbone (SPECS/VR_INPUT_SPEC.md §2.4 I1): the whole interaction
// layer is arithmetic over two poses and four booleans, so on a box with no
// headset, no controller and no runtime a script writes a hand's sample and
// reads it back through exactly the fields the runtime fills. Every gesture
// test in the tree stands on this.

assert(st.input.left.valid === false && st.input.right.valid === false,
       "vr.state().input is a pair of hands, always, valid or not");
assert(st.input.left.select === 0 && st.input.left.selectPressed === false &&
       st.input.left.stick.x === 0 && st.input.left.stickPressed === false,
       "...every control at its zero with nothing reporting");
assert(st.profile === "", "no interaction profile is bound (there is no runtime)");
assert(st.bindings.offered === 0 && st.bindings.accepted === 0,
       "and no suggested-binding block was offered");

assert(vr.inject("right", {
    grip: { x: 1, y: 1.4, z: -2, rotation: { x: 0, y: 0, z: 0, w: 1 } },
    aim:  { x: 1, y: 1.45, z: -2.05, rotation: { x: 0, y: 0, z: 0, w: 1 } },
    select: 1, grab: 0.25, menuPressed: true, stick: { x: -0.5, y: 0.75 },
    stickPressed: true
}) === true, "vr.inject writes a hand with no runtime running");

var inj = vr.state().input.right;
console.log("injected: " + JSON.stringify(inj));
assert(inj.valid === true && inj.fromInjection === true,
       "...and vr.state().input reports it as an INJECTED sample, never as a wearer's");
assert(inj.grip.valid === true && Math.abs(inj.grip.x - 1) < 1e-5 &&
       Math.abs(inj.grip.z + 2) < 1e-5,
       "the grip pose comes back exactly as written, in world space");
assert(inj.aim.valid === true && Math.abs(inj.aim.z + 2.05) < 1e-5,
       "...and the aim pose beside it — two different answers, not one derived from the other");
assert(inj.select === 1 && inj.selectPressed === true,
       "a select of 1 is a press (the verb derives it at 0.5 when it is not said)");
assert(Math.abs(inj.grab - 0.25) < 1e-6 && inj.grabPressed === false,
       "...and a quarter-squeeze is not a press");
assert(inj.menuPressed === true && inj.stickPressed === true &&
       Math.abs(inj.stick.x + 0.5) < 1e-5 && Math.abs(inj.stick.y - 0.75) < 1e-5,
       "menu, stick and its press as written");
assert(vr.state().hands.right.valid === true &&
       Math.abs(vr.state().hands.right.x - 1) < 1e-5,
       "`hands` IS `input.grip`, injection included — one pose, reported twice");
assert(vr.state().hands.left.valid === false, "and the other hand is untouched");

// THE PRESS CAN BE SAID OUTRIGHT, which is what a gesture test that wants a
// half-pulled trigger held down needs.
assert(vr.inject("right", { grip: { x: 0, y: 1, z: -1 }, select: 0.2,
                            selectPressed: true }) === true,
       "an explicit press overrides the derivation");
assert(vr.state().input.right.selectPressed === true &&
       Math.abs(vr.state().input.right.select - 0.2) < 1e-6, "...and both are reported as written");

// A DEFAULT STATE STOPS INJECTING.
assert(vr.inject("right") === true, "vr.inject(hand) with no state withdraws the injection");
assert(vr.state().input.right.valid === false &&
       vr.state().input.right.fromInjection === false,
       "...and the hand is nobody's again");
assert(vr.state().hands.right.valid === false, "...`hands` with it");

// THE REFUSALS ARE ERRORS, NOT SILENT FALSES.
var badHand = false;
try { vr.inject("middle", {}); } catch (e) { badHand = String(e).indexOf("left") >= 0; }
assert(badHand, "vr.inject THROWS on a hand that is not a hand");
var badKey = false;
try { vr.inject("left", { trigger: 1 }); } catch (e) { badKey = String(e).indexOf("trigger") >= 0; }
assert(badKey, "...and on an unknown key, naming it");
var badRot = false;
try { vr.inject("left", { grip: { x: 0, y: 0, z: 0, rotation: { x: 0, y: 1, z: 0, s: 0 } } }); }
catch (e) { badRot = String(e).indexOf("rotation") >= 0; }
assert(badRot, "...and on a rotation it cannot parse (the unnamed-fourth-number rule)");

// THE HAPTIC refuses with no session, because there is nothing to buzz.
assert(vr.haptic("left") === false, "vr.haptic refuses with no session running");
assert(app.lastError().indexOf("vr.haptic") >= 0,
       "...and says which verb refused: " + app.lastError());
var badHaptic = false;
try { vr.haptic("both"); } catch (e) { badHaptic = String(e).indexOf("left") >= 0; }
assert(badHaptic, "...and throws on a hand that is not a hand");

// ---- and the editor is untouched -----------------------------------------

var id = scene.addPrimitive("Cube");
assert(!!id, "the editor still works after all that");
editor.frame(2);
assert(vr.state().active === false, "and still no session");

console.log("scripting.e2e.vr_verbs: PASS");
