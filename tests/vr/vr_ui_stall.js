// vr.ui_stall — A 7 ms UI-THREAD STALL COSTS NO HEADSET FRAME (ENGINE-UPGRADE-U3; the premise's measurement, stage 1).
//
// TODAY the editor's render driver ticks on Qt's main thread and the VR frame's `xrWaitFrame` runs INSIDE that tick
// (E/OgreVrSession.cpp, from the frame head), so any event-loop work between ticks delays the runtime's wait. The
// stimulus is the size the 2026-10-03 perf audit measured: a 7 ms sleep in a beforeFrame slot (app.tickStall), at most
// once every 350 ms, while the app free-runs under Monado's simulated headset (`--script-live`: the driver keeps
// ticking between this script's verbs — the owner's mode, never a lockstep editor.frame).
//
// THE COUNT IS THE RUNTIME'S: VrStatus::missedDisplayPeriods — its own predicted display times stepped by more than
// one of its display periods (a refresh no frame of ours reached). Measured over 600 accepted frames with no stall
// (the control) and over 600 with the stall armed.
//
// THE BAR (a law of the design, stage 5): 0 periods missed under the stall. The spec expected it RED ON MAIN — the row is
// `measure` until stage 5 flips it to a guard. THE BASE'S NUMBER is the BASE line in tests/vr/CMakeLists.txt's
// vr.ui_stall block (on this rig's 20 Hz null compositor a 7 ms stall misses nothing; 32 ms misses one per stall).
var failures = 0;
function ok(cond, msg) {
    if (cond) { console.log("ok:   " + msg); }
    else { console.log("FAIL: " + msg); ++failures; }
}

if (project.create("vr ui stall") === false) throw new Error("project.create failed: " + app.lastError());
if (app.columns().space !== "editor") app.space("editor");
editor.frame(5);

// The editor's VR preview session (the VR button), brought up and warmed in frames.
vr.press();
for (var i = 0; i < 120 && !vr.state().active; ++i) editor.frame(5);
ok(vr.state().active === true, "the editor's VR session is up (" + app.lastError() + ")");
for (var w = 0; w < 120 && vr.state().rendered < 30; ++w) editor.frame(5);

// Waits for `n` more accepted headset frames while the DRIVER ticks (every verb returns to the event loop, so the
// driver's timer fires between two of them). Bounded in polls, never in seconds.
function window(n) {
    var s0 = vr.state();
    var polls = 0;
    while (vr.state().frames < s0.frames + n && ++polls < 4000000) {}
    var s1 = vr.state();
    return { frames: s1.frames - s0.frames, missed: s1.missedDisplayPeriods - s0.missedDisplayPeriods,
             rendered: s1.rendered - s0.rendered, polls: polls, refreshHz: s1.refreshHz };
}

var control = window(600);
console.log("control: " + JSON.stringify(control));
var armed = app.tickStall(7, 350);
ok(armed.armed === true, "the 7 ms stall is armed every 350 ms (" + JSON.stringify(armed) + ")");
var stalled = window(600);
var stalls = app.tickStall(0).stalls;   // disarm, read what was slept
console.log("stalled: " + JSON.stringify(stalled) + " stalls=" + stalls);
ok(control.frames >= 600 && stalled.frames >= 600, "both windows drew 600 accepted headset frames");
ok(stalls > 0, "the stall fired (" + stalls + " times)");
console.log("VR_UI_STALL control_missed=" + control.missed + " stalled_missed=" + stalled.missed + " stalls=" + stalls
            + " missed_per_stall=" + (stalls ? (stalled.missed / stalls).toFixed(2) : "n/a"));
ok(stalled.missed === 0,
   "A 7 ms UI STALL COSTS NO HEADSET FRAME: " + stalled.missed + " display period(s) missed over " + stalls
   + " stalls (the control missed " + control.missed + ")");
vr.press();   // leave VR
editor.frame(5);
console.log(failures ? "vr_ui_stall: FAILED" : "vr_ui_stall: PASS");
if (failures) throw new Error(failures + " check(s) failed");
