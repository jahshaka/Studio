// vr.ui_stall — A UI-THREAD STALL SHORTER THAN THE DISPLAY PERIOD NEVER COSTS A DISPLAY PERIOD (ENGINE-UPGRADE-U3;
// the lead's ruling after stage 1's measurement).
//
// TODAY the editor's render driver ticks on Qt's main thread and the VR frame's `xrWaitFrame` runs INSIDE that tick,
// so any event-loop work between ticks delays the runtime's wait. THE STIMULUS IS A SHARE OF THE PERIOD, not a number
// of milliseconds: the 2026-10-03 perf audit's 7 ms at a 90 Hz headset is 7/11.1 = 0.63 of the period, so the stall is
// 0.63 x the RUNTIME's own predicted display period (vr.state().refreshHz) — 32 ms under Monado's 20 Hz null
// compositor, 7 ms at 90 Hz — slept in a beforeFrame slot (app.tickStall) at most once every 350 ms while the app
// free-runs (`--script-live`: the driver ticks between this script's verbs — the owner's mode, never a lockstep
// editor.frame).
//
// THE COUNT IS THE RUNTIME'S: VrStatus::missedDisplayPeriods — its predicted display times stepped by more than one of
// its display periods (a refresh no frame of ours reached). Over 600 accepted frames with no stall (the control) and
// over 600 with the stall armed.
//
// THE BAR (a law of the design, stage 5): 0 periods missed per stall. RED ON MAIN — the base numbers are in
// tests/vr/CMakeLists.txt's vr.ui_stall block — so the row is `measure` until stage 5 flips it to a guard.
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
ok(control.refreshHz > 0, "the runtime reports its display period (" + control.refreshHz + " Hz)");
var periodMs = 1000.0 / control.refreshHz;
var stallMs = Math.round(periodMs * 7.0 / (1000.0 / 90.0));   // 0.63 of the period: 7 ms at 90 Hz
var armed = app.tickStall(stallMs, 350);
console.log("VR_UI_STALL period_ms=" + periodMs.toFixed(2) + " stall_ms=" + stallMs + " (0.63 of the period)");
ok(armed.armed === true, "the " + stallMs + " ms stall is armed every 350 ms (" + JSON.stringify(armed) + ")");
var stalled = window(600);
var stalls = app.tickStall(0).stalls;   // disarm, read what was slept
console.log("stalled: " + JSON.stringify(stalled) + " stalls=" + stalls);
ok(control.frames >= 600 && stalled.frames >= 600, "both windows drew 600 accepted headset frames");
ok(stalls > 0, "the stall fired (" + stalls + " times)");
console.log("VR_UI_STALL control_missed=" + control.missed + " stalled_missed=" + stalled.missed + " stalls=" + stalls
            + " missed_per_stall=" + (stalls ? (stalled.missed / stalls).toFixed(2) : "n/a"));
ok(stalled.missed === 0,
   "A UI STALL OF 0.63 OF THE PERIOD (" + stallMs + " ms of " + periodMs.toFixed(1) + ") COSTS NO DISPLAY PERIOD: "
   + stalled.missed + " missed over " + stalls + " stalls (" + (stalls ? (stalled.missed / stalls).toFixed(2) : "n/a")
   + " per stall; the control missed " + control.missed + ")");
vr.press();   // leave VR
editor.frame(5);
console.log(failures ? "vr_ui_stall: FAILED" : "vr_ui_stall: PASS");
if (failures) throw new Error(failures + " check(s) failed");
