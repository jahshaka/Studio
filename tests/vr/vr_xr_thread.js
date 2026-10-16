// vr.xr_thread — THE XR FRAME LOOP IS THE RENDER THREAD'S (ENGINE-UPGRADE-U3 stage 6, the premise measured first).
// FREE-RUNNING (JAH_FREE_RUNNING=1, the owner's mode) under Monado's null compositor: the OpenXR session's
// xrWaitFrame / xrBeginFrame / xrEndFrame run inside the engine's frame on its render thread, the eyes located at that
// frame's head (the late pose), and the UI thread never waits for the runtime. What is asserted, in frames:
//   * 300 accepted headset frames, every one the runtime asked for drawn (frames = rendered over the window);
//   * none of them begun off the render thread (VrStatus::framesOffRenderThread stays 0);
//   * no runtime error the engine recorded inside the window (app.engineErrors);
//   * the UI thread's driver ticks never held a runtime wait: the worst tick stays under half the runtime's period
//     (xrWaitFrame paces the headset at 50 ms on the null compositor; a tick that carried it would cost >= 50 ms).

var failures = 0;
function ok(cond, msg) {
    if (cond) { console.log("ok:   " + msg); }
    else { console.log("FAIL: " + msg); ++failures; }
}

if (project.create("vr xr thread") === false) throw new Error("project.create failed: " + app.lastError());
if (app.columns().space !== "editor") app.space("editor");
scene.addPrimitive("cube", { position: [0, 1.2, -1.5] });
editor.frame(5);

vr.press();
for (var i = 0; i < 120 && !vr.state().active; ++i) editor.frame(5);
ok(vr.state().active === true, "the editor's VR session is up (" + app.lastError() + ")");
for (var w = 0; w < 120 && vr.state().rendered < 30; ++w) editor.frame(5);
var warm = vr.state();
while (vr.state().frames < warm.frames + 90) {}   // the warm-up: a suite warms up before it measures

var errors0 = app.engineErrors().recorded;
var sync0 = engine.syncGetters();
app.frameStats({ reset: true });
var s0 = vr.state();
while (vr.state().frames < s0.frames + 300) {}
var s1 = vr.state();
var fs = app.frameStats();
var sync1 = engine.syncGetters(), moved = [];
for (var k in sync1.getters) {
    var d = sync1.getters[k] - (sync0.getters[k] || 0);
    if (d > 0) moved.push(k + " x" + d);
}
console.log("VR_XR_THREAD_SYNC_READS " + (sync1.total - sync0.total) + ": " + moved.join(", "));
var errors = app.engineErrors().recorded - errors0;
var periodMs = s1.refreshHz > 0 ? 1000.0 / s1.refreshHz : 0;
console.log("VR_XR_THREAD frames=" + (s1.frames - s0.frames) + " rendered=" + (s1.rendered - s0.rendered) +
            " off_render_thread=" + s1.framesOffRenderThread + " runtime_errors=" + errors + " worst_tick_ms=" +
            fs.worstMs.toFixed(2) + " runtime_period_ms=" + periodMs.toFixed(2) + " missed=" +
            (s1.missedDisplayPeriods - s0.missedDisplayPeriods));
ok(s1.frames - s0.frames >= 300, "300 accepted headset frames (" + (s1.frames - s0.frames) + ")");
ok(s1.rendered - s0.rendered === s1.frames - s0.frames,
   "every frame the runtime asked for was drawn (" + (s1.rendered - s0.rendered) + " of " + (s1.frames - s0.frames) + ")");
ok(s1.framesOffRenderThread === 0, "no VR frame began off the render thread (" + s1.framesOffRenderThread + ")");
ok(errors === 0, "no runtime error recorded in the window (" + errors + ")");
ok(periodMs > 0 && fs.worstMs < periodMs / 2,
   "the UI thread's ticks never waited for the runtime: worst " + fs.worstMs.toFixed(2) + " ms against its " +
   periodMs.toFixed(2) + " ms period");
vr.press();
editor.frame(5);
console.log(failures ? "vr_xr_thread: FAILED" : "vr_xr_thread: PASS");
if (failures) throw new Error(failures + " check(s) failed");
