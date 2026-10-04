// vr.start_recovery (VR-START-1) — EVERY VR START FAILURE ENDS IN THE EDITOR
// PLUS ONE DIALOG, AND "TRY AGAIN" RECOVERS IN-PROCESS. Run by run_vr_app.sh
// with JAH_VR_RUNNER_EVENTS=1 and JAH_VR_RUNNER_DEFER=1: the app starts with
// `--vr` and NO runtime up (the runtime is the runner's PRIVATE Monado, so the
// user's system Monado can never be reached); this script asks the runner for
// it with `VRRUNNER:` lines.
//   1. (a) no runtime at launch: the startup check answers with the dialog;
//      the editor is up and answers.
//   2. §2 Try again: the runner starts the runtime; vr.tryAgain() brings the
//      session up in the SAME process (no restart).
//   3. (e) the runtime dies mid-session: the session ends, the editor carries
//      on, the dialog says the headset disconnected.
//   4. (f) the reconnect: the runner starts a NEW runtime process (WiVRn's
//      fresh streaming process, in kind); Try again brings a session up again.
function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function report() { var r = vr.startReport(); console.log("report: " + JSON.stringify(r)); return r; }
// Bounded waits in FRAMES (no wall clock): each pass renders 10 frames.
function untilActive(passes, retry) {
    for (var i = 0; i < passes; ++i) {
        if (vr.state().active) return true;
        if (retry) { if (!vr.tryAgain()) vr.press(); }
        editor.frame(10);
    }
    return vr.state().active;
}

editor.frame(5);
var r = report();
assert(vr.available().available === true, "--vr: this run may use VR");
assert(!vr.state().active, "no runtime at launch: no session");
assert(r.dialogs >= 1 && r.dialogOpen === true, "the startup check answered with the dialog");
assert(r.failure === "noRuntime" || r.failure === "runtimeBroken",
       "the failure is classified (" + r.failure + ")");
assert(app.columns().space !== undefined, "the editor is up and answers");

console.log("VRRUNNER: start-runtime");
assert(untilActive(120, true), "Try again brought the session up in the same process");
assert(report().dialogOpen === false, "the dialog closed on success");
editor.frame(30);

var before = report().dialogs;
console.log("VRRUNNER: stop-runtime");
for (var i = 0; i < 120 && vr.state().active; ++i) editor.frame(10);
assert(!vr.state().active, "the dropped session ended (the editor did not)");
editor.frame(10);
r = report();
assert(r.failure === "connectionLost" && r.dialogs === before + 1 && r.dialogOpen,
       "the user was told once that the headset disconnected");

console.log("VRRUNNER: start-runtime");
assert(untilActive(120, true), "Try again reconnected to the NEW runtime process, no restart");
vr.press();   // leave VR
editor.frame(5);
assert(!vr.state().active, "the button left VR");
console.log("vr_start: PASS");
