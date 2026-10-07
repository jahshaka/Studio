// shader.async_soak (ASYNC-SHADERS-1): the background compiler under use, with the Vulkan
// validation layer on (the driver sets it): 50 material applies while the camera flies, a
// BLOCKING render meeting a pending permutation (an offscreen screenshot right after a new
// material: the in-frame queue's workers and the service's publish in one frame), a SCENE
// SWITCH while compiles are pending, and a quit with compiles pending. The driver asserts a
// clean exit, no validation message, no failed permutation and no UI-thread compile from the
// asynchronous view.
app.setAsyncShaders(true);
// The world the scene switch goes to, made first (a second CREATE after a first project draws
// validation errors on the base build too — VUID-vkCmdDraw-None-09600 / oldLayout-01197, a
// pre-existing finding — so the switch is an OPEN, which is clean on the base).
var otherWorld = project.create("as1 soak other", { template: "basic" });
editor.frame(30);
project.create("as1 soak", { template: "basic" });
editor.frame(60);
var objs = [scene.find("Floor"),
            scene.addPrimitive("Sphere", { position: [0, 1, 0] }),
            scene.addPrimitive("Cube", { position: [3, 0.5, 0] }),
            scene.addPrimitive("Torus", { position: [-3, 1, 0] })];
var presets = materials.presets();
editor.frame(60);
var t = 0;
function fly(n) {
    for (var i = 0; i < n; ++i) {
        t += 0.02;
        editor.setCamera({ position: [8 * Math.cos(t), 3 + Math.sin(t * 0.7), 8 * Math.sin(t)],
                           lookAt: [0, 0.5, 0] });
        editor.frame(1);
    }
}
var applied = 0, maxPending = 0, maxFrame = 0;
for (var k = 0; k < 50; ++k) {
    var node = objs[k % objs.length];
    if (!node) continue;
    if (k % 3 === 0 && presets.length) {
        material.apply(node, presets[k % presets.length].name);
    } else {
        material.set(node, {
            baseColor: ["#c03020", "#20a040", "#2040c0", "#e0c040"][k % 4],
            metallic: (k & 1) ? 1.0 : 0.0,
            roughness: 0.2 + 0.015 * k,
            clearCoat: (k & 2) ? 0.6 : 0.0,
            anisotropy: (k & 4) ? 0.5 : 0.0,
            emissiveIntensity: (k & 8) ? 2.0 : 0.0,
            emissiveColor: "#ff8040",
            useFresnelColor: (k & 16) ? true : false
        });
    }
    ++applied;
    var t0 = Date.now();
    fly(12);
    var ms = (Date.now() - t0) / 12;
    if (ms > maxFrame) maxFrame = ms;
    var s = app.asyncShaders();
    if (s.pending > maxPending) maxPending = s.pending;
}
var s1 = app.asyncShaders();
var liveBeforeBlocking = app.shaderCache().liveCompiles;
// A BLOCKING RENDER MEETS A PENDING PERMUTATION: a never-seen material, then at once an
// offscreen screenshot (a one-shot view: blocking, it waits for exactly that request).
material.set(objs[1], { clearCoat: 0.9, anisotropy: 0.9, metallic: 0.25, roughness: 0.77 });
editor.frame(1);
var pendingAtShot = app.asyncShaders().pending;
var shot = editor.screenshot(OUTDIR + "/blocking_meets_pending.png", 256, 256);
fly(30);
// A SCENE SWITCH WHILE COMPILES ARE PENDING: new permutations requested, then another world.
material.set(objs[2], { clearCoat: 0.45, anisotropy: 0.2, useFresnelColor: true, metallic: 0.8 });
material.set(objs[3], { clearCoat: 0.15, anisotropy: 0.6, emissiveIntensity: 1.0, metallic: 0.1 });
editor.frame(1);
var pendingAtSwitch = app.asyncShaders().pending;
project.open(otherWorld);
editor.frame(120);
app.waitForAsyncShaders();
editor.frame(10);
console.log("AS1SOAK " + JSON.stringify({ applied: applied, maxPending: maxPending, stats: s1,
                                          live: liveBeforeBlocking, pendingAtShot: pendingAtShot,
                                          shot: shot, pendingAtSwitch: pendingAtSwitch,
                                          after: app.asyncShaders() }));
objs = [scene.addPrimitive("Sphere", { position: [0, 1, 0] }),
        scene.addPrimitive("Cube", { position: [2, 0.5, 0] }), null, null];
editor.frame(2);
// THE PENDING SHUTDOWN: new permutations requested, then quit before they land.
for (var j = 0; j < objs.length; ++j)
    if (objs[j]) material.set(objs[j], { clearCoat: 0.3, anisotropy: 0.7, useFresnelColor: true,
                                         emissiveIntensity: 3.0, metallic: 0.5 });
editor.frame(1);
console.log("AS1SOAK_QUIT pending " + app.asyncShaders().pending);
app.quit();
