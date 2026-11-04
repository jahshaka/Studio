// pool.host_epic::frame_step - A SCRIPTED FRAME IS ONE GRID STEP, AT THE DOOR (TEST-STREAMLINE-1 red (b); the landing
// audit's F5, 2026-11-04). The engine has no wall clock: editor.frame(n) hands the document's SimulationClock exactly
// n x dt seconds (dt = 1/60 unless stated), whatever the frames' wall time, so a played document reads the same time
// after the same frames on any box (src/viewport/framestep.h; EditorApi::frame -> EngineSceneViewport::stepFrames ->
// renderFrames(n, dt) -> syncFrame(dt)).
// THE LAWS (exact - integer steps, no tolerance but the float the animation clock is kept in):
//   1. during play, editor.frame(37) advances the clock by exactly 37 steps and a played lamp's animation by 37/60 s;
//   2. editor.frame(10, 1/30) - a stated dt - advances it by exactly 20 steps;
//   3. a negative dt (the deleted "use the wall" value) and a dt past scene.clock().maxAdvance are REFUSED, and a refused
//      call moves the clock by nothing.
// RED-CAPABLE: with the wall rule (the base before ba38b6234) the same 37 frames bought 21-22 steps on this headless host
// and 57-66 as an app row (block B: paused_shot's lamp read t 0.350 / 0.367 / 1.100). It replaces app.frame_step's CPU
// loop, which never reached a door (its "every wall pace" loop discarded the wall).
var g = project.create(project.nextFreeName("frame-step"), { template: "empty" });
if (!g || g.length < 10) throw new Error("project.create refused");
var lamp = scene.addLight("point", { position: { x: 0, y: 2, z: 0 }, intensity: 1.0 });
if (!lamp) throw new Error("scene.addLight(point) refused");
anim.create(lamp, "ramp");
if (!anim.keyframe(lamp, "intensity", 0, 1.0) || !anim.keyframe(lamp, "intensity", 10, 11.0))
    throw new Error("anim.keyframe(intensity) refused");
if (!editor.play()) throw new Error("editor.play refused");

var c0 = scene.clock(), t0 = anim.seek();
editor.frame(37);
var c1 = scene.clock(), t1 = anim.seek();
var d1 = c1.steps - c0.steps;
if (d1 !== 37 || Math.abs((t1 - t0) - 37 / 60) > 1e-6)
    throw new Error("editor.frame(37) during play: " + d1 + " steps, the lamp's t " + t0 + " -> " + t1 +
                    " (the law: exactly 37 steps, t + 37/60 = " + (37 / 60) + ")");

editor.frame(10, 1 / 30);
var c2 = scene.clock();
var d2 = c2.steps - c1.steps;
if (d2 !== 20) throw new Error("editor.frame(10, 1/30): " + d2 + " steps (the law: exactly 20)");

function refused(dt) {
    try { editor.frame(1, dt); } catch (e) { return String(e); }
    return "";
}
var neg = refused(-1), past = refused(c2.maxAdvance + 0.01), c3 = scene.clock();
if (neg.indexOf("negative") < 0) throw new Error("editor.frame(1, -1) was not refused as negative: '" + neg + "'");
if (past.indexOf("bound") < 0) throw new Error("editor.frame(1, maxAdvance + 0.01) was not refused: '" + past + "'");
if (c3.steps !== c2.steps) throw new Error("a refused editor.frame moved the clock " + (c3.steps - c2.steps) + " steps");
if (!editor.stop()) throw new Error("editor.stop refused");
// THE LAW's NUMBERS ON THE ok: CHANNEL (the clean-state law compares an arm's ok:/FAIL:/LAW lines)
console.log("ok: frame_step: editor.frame(37) during play = " + d1 + " steps (t + " + (t1 - t0).toFixed(6) +
            " s); editor.frame(10, 1/30) = " + d2 + " steps; dt -1 and dt " + (c2.maxAdvance + 0.01).toFixed(4) +
            " refused, the clock unmoved");
"ok";
