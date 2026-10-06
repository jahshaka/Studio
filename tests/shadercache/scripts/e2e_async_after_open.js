// shader.async_after_open (ASYNC-SHADERS-1): after a project is open, a new material and a
// tier change compile in the BACKGROUND — no shader compiles on the UI thread, the objects
// waiting for theirs draw the grey placeholder, the "Compiling shaders (N)" count rises and
// falls back to 0, and the settled picture is the warm run's. Driven twice by
// test_async_after_open.cpp: a cold launch, then a warm one (the reference picture).
// OUTDIR is prepended by the driver.
function st() { return app.asyncShaders(); }
function live() { return app.shaderCache().liveCompiles; }
var r = { phases: [], frames: [] };
app.setAsyncShaders(true);
project.create("async after open", { template: "basic" });
editor.frame(120);
var sphere = scene.addPrimitive("Sphere", { position: [0, 1, 0] });
editor.frame(120);
app.waitForAsyncShaders();
editor.frame(60);
r.ready = st();
r.liveAtStart = live();

function phase(label, change) {
    var before = st();
    var liveBefore = live();
    change();
    var p = { phase: label, pendingPeak: 0, placeholderDraws: 0, pendingSkips: 0, frames: 0,
              worstFrameMs: 0, pictureDuring: "" };
    // FRAMES, never time: step until nothing is pending (bounded), then settle.
    for (var i = 0; i < 600; ++i) {
        var t0 = Date.now();
        editor.frame(1);
        var ms = Date.now() - t0;
        var s = st();
        if (ms > p.worstFrameMs) p.worstFrameMs = ms;
        if (s.pending > p.pendingPeak) p.pendingPeak = s.pending;
        // The first frame that drew a placeholder is photographed as presented.
        if (!p.pictureDuring && s.placeholderDraws > before.placeholderDraws) {
            p.pictureDuring = OUTDIR + "/" + label + "_during.png";
            editor.presentedFrame(p.pictureDuring, { helpers: true });
        }
        p.frames = i + 1;
        if (s.pending === 0 && i >= 2) break;
    }
    editor.frame(120);
    var after = st();
    p.pendingEnd = after.pending;
    p.placeholderDraws = after.placeholderDraws - before.placeholderDraws;
    p.pendingSkips = after.pendingSkips - before.pendingSkips;
    p.failed = after.failed - before.failed;
    p.live = live() - liveBefore;
    p.pictureAfter = OUTDIR + "/" + label + "_after.png";
    editor.presentedFrame(p.pictureAfter, { helpers: true });
    r.phases.push(p);
}
// A material no world has used yet: a clear coat is a permutation of its own (the presets
// and the default worlds are all in the startup set, so applying one compiles nothing).
phase("material", function () {
    material.set(sphere, { baseColor: "#d03020", metallic: 0.0, roughness: 0.4,
                           clearCoat: 0.6, clearCoatRoughness: 0.1 });
});
phase("tier", function () { world.gi({ tier: "low" }); });
r.live = live();
r.final = st();
console.log("ASYNCAFTEROPEN " + JSON.stringify(r));
app.quit();
