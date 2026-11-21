// picture.advances_nothing (ONE-FRAME-1 stage 2, V2 plan 2.1) - A PICTURE ADVANCES NOTHING.
// THE LAW: taking a picture moves no clock. A scene's time is its document's steps (ONE-FRAME-1 stage 1: its mirror stamps
// the document's lifetime serial, the engine advances the scene by the steps it moved, once), and nothing in a picture
// steps a document. So across editor.screenshot the scene's time (what its particles, exposure meter, looks and clouds
// have lived), its grid state, the shader clock (scrolling maps, graph pieces) and the document's own clock are all
// EXACTLY unchanged, however many settle and stable frames the shot drew; the next scripted frame moves them one step.
// RED ON THE BASE 273b8e638 / irisgl 4576b132 (the ground read's 2.1, [F]; derived from its code, this row's verbs being
// the stage's): the shot synced the mirror twice, and each sync re-added the last step to the shader clock (+1/60 s,
// measured by mirror.shader_clock_steps on the base); takePicture restored the frame delay and THEN drew its >= 2 stable
// frames at the live delta, so particles, the meter and the clouds advanced >= 2 steps per shot.
// NOT RUN ON THE BASE and not measurable there: the base's mechanism was deleted across stages 0-2 (the mirror's per-sync
// shader clock in 0, the host-pushed delta in 1, the picture's live-delta stable frames in 2) and this row's verbs are the
// lane's; no single revert arm restores it (the lead's ruling 2026-11-21).
var g = project.create(project.nextFreeName("picture-still"), { template: "empty" });
if (!g || g.length < 10) throw new Error("project.create refused");
var sun = scene.addLight("directional", { position: { x: 0, y: 12, z: 0 }, rotation: { x: -52, y: 28, z: 0 },
                                          intensity: 2.0 });
if (!sun) throw new Error("scene.addLight(directional) refused");
var floor = scene.addPrimitive("plane", { position: { x: 0, y: 0, z: 0 }, scale: { x: 20, y: 1, z: 20 } });
if (!material.set(floor, { baseColor: "#c8c8c8", roughness: 0.6 })) throw new Error("material.set(floor) refused");
var fire = scene.addParticles("fire", { position: { x: 0, y: 0.2, z: 0 } });
if (!fire) throw new Error("scene.addParticles(fire) refused");
editor.setCamera({ position: { x: 0, y: 3, z: 7 }, lookAt: { x: 0, y: 1, z: 0 } });
editor.frame(60);

function clocks() {
    var v = editor.viewportState();
    return { sceneSeconds: v.sceneSeconds, stepSerial: v.stepSerial, shader: world.shaderTime().t,
             docSteps: scene.clock().steps, drawn: app.frameStats().submit.stepSerialDrawn };
}
var before = clocks();
if (!(before.sceneSeconds > 0)) throw new Error("the scene's clock never ran before the shot: " + JSON.stringify(before));
var shot = editor.screenshot("picture_still.png", 128, 128, [], "viewport");
if (!shot || !shot.width) throw new Error("the shot failed: " + app.lastError());
var after = clocks();
console.log("picture_still: the shot drew " + shot.settleFrames + " settle frames; before " + JSON.stringify(before) +
            ", after " + JSON.stringify(after));
["sceneSeconds", "stepSerial", "shader", "docSteps"].forEach(function (k) {
    if (after[k] !== before[k]) throw new Error("the picture advanced " + k + ": " + before[k] + " -> " + after[k]);
});
editor.frame(1);
var next = clocks();
var step = next.sceneSeconds - after.sceneSeconds;
console.log("picture_still: one scripted frame after it: scene +" + step + " s, serial " + after.stepSerial + " -> " +
            next.stepSerial + ", drawn " + next.drawn);
if (Math.abs(step - 1 / 60) > 1e-6) throw new Error("the next frame did not advance the scene one step: " + step);
if (next.stepSerial !== after.stepSerial + 1) throw new Error("the next frame's serial is not one step on");
if (next.drawn < after.drawn) throw new Error("the drawn grid state went backwards: " + after.drawn + " -> " + next.drawn);
console.log("ok: picture_still: a shot of " + shot.settleFrames + " settle frames moved no clock (scene " +
            before.sceneSeconds + " s, serial " + before.stepSerial + ", shader " + before.shader + " s); the next frame +1 step");
"ok";
