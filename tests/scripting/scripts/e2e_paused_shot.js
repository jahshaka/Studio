// scripting.e2e.paused_shot (PHOTON-7 stage 10-3, the lead's ruling) - A SHOT OF A PAUSED ANIMATED SCENE IS AT REST.
// editor.screenshot freezes the document clock while it settles, so an animated scene is still for its shot and owes
// the rest like any other: giAtRest's readers' terms (the reflection's 2N, the gather's N) apply to it, and its picture
// is a function of the pose and the rest index alone. THE LAWS: a shot taken while play runs (a lamp's intensity keyed
// 1 -> 20 -> 1, looping) ends AT REST (not at the settle's cap), and it equals - to the code - a shot of the same scene
// stopped and posed at the same document time.
var g = project.create(project.nextFreeName("paused-shot"), { template: "empty" });
if (!g || g.length < 10) throw new Error("project.create refused");
var sun = scene.addLight("directional", { position: { x: 0, y: 12, z: 0 }, rotation: { x: -52, y: 28, z: 0 },
                                          intensity: 2.0 });
if (!sun) throw new Error("scene.addLight(directional) refused");
var floor = scene.addPrimitive("plane", { position: { x: 0, y: 0, z: 0 }, scale: { x: 20, y: 1, z: 20 } });
if (!material.set(floor, { baseColor: "#c8c8c8", roughness: 0.6 })) throw new Error("material.set(floor) refused");
var box = scene.addPrimitive("cube", { position: { x: 0, y: 1, z: 0 }, scale: { x: 1, y: 2, z: 1 } });
if (!box) throw new Error("scene.addPrimitive(cube) refused");
var lamp = scene.addLight("point", { position: { x: 2, y: 2, z: 2 }, intensity: 1.0 });
if (!lamp) throw new Error("scene.addLight(point) refused");
// THE ANIMATION: the lamp's intensity, 1 -> 20 -> 1 over two seconds, looping
anim.create(lamp, "pulse");
if (!anim.keyframe(lamp, "intensity", 0, 1.0) || !anim.keyframe(lamp, "intensity", 1, 20.0) ||
    !anim.keyframe(lamp, "intensity", 2, 1.0))
    throw new Error("anim.keyframe(intensity) refused");
anim.setLooping(lamp, true);
if (world.rayTracing("auto") !== "auto") throw new Error("world.rayTracing(auto) refused");
editor.setCamera({ position: { x: 0, y: 4, z: 9 }, lookAt: { x: 0, y: 1, z: 0 } });
editor.frame(60);

if (!editor.play()) throw new Error("editor.play refused");
editor.frame(37);
var t = anim.seek();
var playing = editor.screenshot("paused_shot_playing.png", 128, 128, [], "viewport");
if (!playing || !playing.width) throw new Error("the shot during play failed: " + app.lastError());
console.log("paused_shot: during play at t " + t + ": settled " + playing.settleFrames + " frames, atRest " +
            playing.atRest + " (" + playing.atRestReason + ")");
if (playing.atRest !== true) throw new Error("a shot during play ended at '" + playing.atRestReason + "', not at rest");
if (!editor.stop()) throw new Error("editor.stop refused");
anim.seek(t);
editor.frame(10);
var stopped = editor.screenshot("paused_shot_stopped.png", 128, 128, [], "viewport");
if (!stopped || !stopped.width) throw new Error("the stopped shot failed: " + app.lastError());
console.log("paused_shot: stopped, posed at t " + anim.seek() + ": settled " + stopped.settleFrames + " frames, atRest " +
            stopped.atRest + " (" + stopped.atRestReason + ")");
if (stopped.atRest !== true) throw new Error("the stopped shot ended at '" + stopped.atRestReason + "', not at rest");
var c = app.compareImages("paused_shot_playing.png", "paused_shot_stopped.png", 1);
console.log("paused_shot: the two shots: " + c.over + " pixels over one code of " + (c.width * c.height) + ", max " +
            c.maxDelta);
if (c.over !== 0) throw new Error("a shot during play differs from the stopped shot at the same time: " + c.over + " px");
"ok";
