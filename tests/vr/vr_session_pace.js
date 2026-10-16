// vr.session_pace — THE SESSION'S PERIOD PACES THE RENDER LOOP, NEVER THE PANEL'S (ENGINE-UPGRADE-U3 decision 53, the
// driver's half). FREE-RUNNING (JAH_FREE_RUNNING=1) under Monado's null compositor: while the session pumps, the
// period the driver sends the render loop (app.frameStats().displayPeriodMs) is the RUNTIME's predicted one
// (VrStatus::refreshHz), and no display period is missed over 300 headset frames. The engine's half — no pad after a
// frame xrWaitFrame paced, a headset faster than the panel — is frame.runtime_pace's (the null compositor runs at
// 20 Hz, slower than the rig's panel, so the defect's case cannot be built on this runtime).

var failures = 0;
function ok(cond, msg) {
    if (cond) { console.log("ok:   " + msg); }
    else { console.log("FAIL: " + msg); ++failures; }
}

if (project.create("vr session pace") === false) throw new Error("project.create failed: " + app.lastError());
if (app.columns().space !== "editor") app.space("editor");
editor.frame(5);
var panelPeriod = app.frameStats().displayPeriodMs;

vr.press();
for (var i = 0; i < 120 && !vr.state().active; ++i) editor.frame(5);
ok(vr.state().active === true, "the editor's VR session is up (" + app.lastError() + ")");
// THE WARM-UP (a suite warms up before it measures): the session's first frames build what the eyes need.
for (var w = 0; w < 120 && vr.state().rendered < 30; ++w) editor.frame(5);
var warm = vr.state();
while (vr.state().frames < warm.frames + 90) {}
var warmed = vr.state();
console.log("warm-up: 90 frames, " + (warmed.missedDisplayPeriods - warm.missedDisplayPeriods) + " missed");

var s0 = vr.state(), polls = 0, sent = {}, reads = 0;
while (vr.state().frames < s0.frames + 300 && ++polls < 8000000) {
    sent[app.frameStats().displayPeriodMs.toFixed(2)] = true; ++reads;
}
var s1 = vr.state();
var periodMs = s1.refreshHz > 0 ? 1000.0 / s1.refreshHz : 0;
var sentList = Object.keys(sent);
console.log("VR_SESSION_PACE refresh_hz=" + s1.refreshHz + " runtime_period_ms=" + periodMs.toFixed(2) +
            " panel_period_ms=" + panelPeriod.toFixed(2) + " sent_during_session=" + sentList.join(",") + " frames=" +
            (s1.frames - s0.frames) + " missed=" + (s1.missedDisplayPeriods - s0.missedDisplayPeriods));
ok(periodMs > 0, "the runtime reports its period (" + periodMs.toFixed(2) + " ms)");
ok(reads > 0 && sentList.length === 1 && Math.abs(parseFloat(sentList[0]) - periodMs) < 0.05,
   "the render loop was sent the SESSION's period throughout (" + sentList.join(",") + " ms), never the panel's (" +
   panelPeriod.toFixed(2) + " ms)");
ok(s1.frames - s0.frames >= 300, "300 accepted headset frames (" + (s1.frames - s0.frames) + ")");
ok(s1.missedDisplayPeriods - s0.missedDisplayPeriods === 0,
   "no display period missed (" + (s1.missedDisplayPeriods - s0.missedDisplayPeriods) + ")");
vr.press();
editor.frame(5);
var after = app.frameStats().displayPeriodMs;
ok(Math.abs(after - panelPeriod) < 0.05, "after the session the panel's period again (" + after.toFixed(2) + " ms)");
console.log(failures ? "vr_session_pace: FAILED" : "vr_session_pace: PASS");
if (failures) throw new Error(failures + " check(s) failed");
