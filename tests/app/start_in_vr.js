// app.start_in_vr's payload (VR-START-1): one script, three boots, and NOT ONE
// of them lets the OpenXR loader open a runtime (`vr.available().openxrCalls`
// stays 0): the policy refuses before the loader — no active manifest, or an
// active runtime that is not the headset runtime — and JAHSHAKA_VR=0 never asks.
// Each refusal ends in the editor plus ONE message: the startup check's notice,
// the button's dialog. start_in_vr.sh checks the order of the arms.
function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
var s = vr.startInVr();
var a0 = vr.available();
console.log("start_in_vr: source=" + s.source + " startCheck=" + s.startCheck +
            " enabled=" + s.enabled + " runtime=" + s.headsetRuntime);
var threw = false;
try { vr.startInVr("yes"); } catch (e) { threw = String(e).indexOf("true or false") >= 0; }
assert(threw, "vr.startInVr refuses a value that is not true or false");
threw = false;
try { vr.headsetRuntime("Oculus"); } catch (e) { threw = String(e).indexOf("one of") >= 0; }
assert(threw, "vr.headsetRuntime refuses a runtime it does not know");
// A script run starts on the Desktop page; the VR button means the editor
// preview only on the EDITOR page, so open a world there first.
if (project.create("vr start") === false) throw new Error("project.create failed");
if (app.columns().space !== "editor") app.space("editor");
assert(app.columns().space === "editor", "the editor is up and answers");

if (s.source === "setting" && s.startCheck) {
    // ARM 1: the defaults (Start in VR ON, WiVRn) with NO active manifest.
    assert(s.on === true && s.headsetRuntime === "WiVRn", "the defaults: Start in VR on, WiVRn");
    editor.frame(3);
    var r = vr.startReport();
    assert(r.notices === 1 && r.dialogs === 0 && r.failure === "noRuntime",
           "the startup check said so once, in a notice (" + JSON.stringify(r) + ")");
    vr.press();
    r = vr.startReport();
    assert(!vr.state().active && r.dialogs === 1 && r.dialogOpen === true && r.failure === "noRuntime",
           "the VR button: the editor stays, one dialog says no headset");
    assert(r.title === "No headset found", "the dialog's title");
    assert(vr.tryAgain() === false && vr.startReport().dialogs === 2, "Try again asked again, still no headset");
    assert(vr.available().openxrCalls === 0, "the loader was never opened");
    assert(vr.startInVr(false).on === false, "writing false (the next launch)");
} else if (s.source === "setting") {
    // ARM 2: Start in VR OFF; the active manifest names Monado, not WiVRn.
    editor.frame(3);
    assert(vr.startReport().notices === 0, "no startup check, no notice");
    vr.press();
    var r2 = vr.startReport();
    assert(r2.failure === "wrongRuntime" && r2.dialogs === 1 && r2.text.indexOf("Monado") >= 0,
           "the button: the active runtime is not the headset runtime (" + r2.text + ")");
    assert(vr.available().openxrCalls === 0, "the Monado manifest was never opened");
    assert(vr.startInVr(true).on === true, "writing true back");
} else if (s.source === "JAHSHAKA_VR=0") {
    // ARM 3: the runners' form.
    assert(s.enabled === false && a0.available === false, "JAHSHAKA_VR=0: no VR in this run");
    editor.frame(3);
    vr.press();
    var r3 = vr.startReport();
    assert(r3.failure === "disabled" && r3.notices === 0, "a forced-off run refuses in words");
    assert(vr.available().openxrCalls === 0, "and never touches OpenXR");
} else {
    throw new Error("start_in_vr: unexpected source " + s.source);
}
