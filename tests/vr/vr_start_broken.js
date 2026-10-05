// vr.start_broken (VR-START-1 case d, as far as a rig can make it): `--vr` with
// an active manifest whose runtime library cannot be loaded. The startup check,
// then three presses of the VR button: every one ends in the editor plus the
// dialog, never a crash or a hang; the editor keeps answering.
function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
// A script run starts on the Desktop page; the VR button means the editor
// preview only on the EDITOR page, so open a world there first.
if (project.create("vr start") === false) throw new Error("project.create failed");
if (app.columns().space !== "editor") app.space("editor");
editor.frame(5);
var r = vr.startReport();
assert(r.dialogs === 1 && r.dialogOpen, "the startup check answered with the dialog (" + r.failure + ")");
for (var i = 0; i < 3; ++i) {
    vr.press();
    editor.frame(5);
    assert(!vr.state().active, "press " + (i + 1) + ": no session, no crash");
}
r = vr.startReport();
assert(r.dialogs === 4 && r.dialogOpen && r.reason.length > 0,
       "one dialog per attempt, reused, with the loader's reason (" + r.reason + ")");
assert(app.columns().space === "editor", "the editor still answers");
console.log("vr_start_broken: PASS");
