// vr.start_press (VR-START-2) — A START BY ANY PATH ANSWERS THE LAST FAILURE.
// The phase-3 push tier's red, made deterministic: the app boots with `--vr` and
// NO runtime (run_vr_app.sh, JAH_VR_RUNNER_DEFER=1), so the startup check opens
// the "VR did not start" dialog; then the VR BUTTON — never Try again — is
// pressed until the runner's private runtime answers. Every failed press reuses
// the open dialog; the press that succeeds must close it and clear the failure.
// At the base (d99c41cf5) the session came up under the open dialog.
function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function report() { var r = vr.startReport(); console.log("report: " + JSON.stringify(r)); return r; }

if (project.create("vr start press") === false) throw new Error("project.create failed");
if (app.columns().space !== "editor") app.space("editor");
editor.frame(5);
var r = report();
assert(!vr.state().active && r.dialogOpen === true && r.dialogs === 1,
       "no runtime at launch: the startup check's dialog is open");

console.log("VRRUNNER: start-runtime");
// Bounded in FRAMES: each pass is one press, then 10 frames.
var presses = 0;
for (var i = 0; i < 120 && !vr.state().active; ++i) {
    vr.press();
    ++presses;
    if (!vr.state().active) {
        r = report();
        assert(r.dialogOpen === true && r.failure.length > 0,
               "press " + presses + " failed: the dialog is open and says why");
        editor.frame(10);
    }
}
assert(vr.state().active, "a press brought the session up (" + presses + " presses)");
r = report();
assert(r.dialogOpen === false, "the session is up: the start dialog closed");
assert(r.failure === "" && r.reason === "" && r.title === "",
       "the session is up: the failure record cleared");
assert(r.dialogs === presses, "one dialog per failed attempt (the boot check + " + (presses - 1) + ")");
assert(vr.tryAgain() === false, "with VR up there is no dialog to try again from");
assert(vr.state().active, "...and the session is still running");
editor.frame(10);
vr.press();   // leave VR
editor.frame(5);
assert(!vr.state().active, "the button left VR");
console.log("vr_start_press: PASS");
