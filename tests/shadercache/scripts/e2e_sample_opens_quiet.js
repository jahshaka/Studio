// shader.sample_opens_quiet (ASYNC-SHADERS-1): every shipped sample, opened through the sample
// browser's own route on a cold shader cache, the way the owner opens it (the background
// compiler on): after each open's window, 240 frames compile NOTHING on the UI thread.
// app.shaderCache().liveCompiles counts every compile a frame waited for (SHADER-WARM-2's
// sentry); the driver (test_sample_opens_quiet.cpp) asserts 0 per sample.
function live() { return app.shaderCache().liveCompiles; }
app.setAsyncShaders(true);
var out = { samples: [] };
var names = project.samples();
for (var i = 0; i < names.length; ++i) {
    var a = live();
    var ok = project.openSample(names[i]);
    var guard = 0;
    while (project.archiveState() !== "idle" && guard++ < 20000) editor.frame(1);
    while (project.openState() !== "idle" && guard++ < 40000) editor.frame(1);
    var b = live();
    // THE WATCH WINDOW'S MARKERS: the driver names whatever the engine says it compiled on the
    // main thread between them (an in-frame Hlms or compute build, a queued job a blocking
    // pass took over) — a failure names its offender.
    console.log("SAMPLEOPENS_WATCH " + names[i]);
    editor.frame(240);
    console.log("SAMPLEOPENS_WATCHEND " + names[i]);
    out.samples.push({ name: names[i], ok: ok, beforeOpen: b - a, after: live() - b });
}
out.live = live();
out.async = app.asyncShaders();
console.log("SAMPLEOPENS " + JSON.stringify(out));
app.quit();
