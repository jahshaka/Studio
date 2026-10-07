// shader.live_compiles (SHADER-WARM-2): after the startup splash, NOTHING compiles on
// the UI thread outside a compile window — the owner's shape, "the Unreal way". The
// startup gate builds the global set (the engine's passes, the editor's, the default
// world's materials); a create or an open builds its project's set inside its own
// window; the frames after it compile nothing. app.shaderCache().liveCompiles counts
// every compile that broke that, and the driver (test_live_compiles.cpp) asserts 0.
function live() { return app.shaderCache().liveCompiles; }
function compiled() { return app.shaderCache().compiledThisRun; }
var r = { atStart: live(), compiledAtStart: compiled(), phases: [] };
function phase(label, fn) {
    var a = live(), c = compiled();
    fn();
    r.phases.push({ phase: label, live: live() - a, compiled: compiled() - c });
}
phase("desktop 60 frames", function () { editor.frame(60); });
var basic = "";
phase("create basic", function () { basic = project.create(project.nextFreeName("live basic"), { template: "basic" }); });
phase("basic 240 frames", function () { editor.frame(240); });
phase("create world", function () { project.create(project.nextFreeName("live world"), { template: "world" }); });
phase("world 240 frames", function () { editor.frame(240); });
phase("reopen basic", function () { project.open(basic); });
phase("reopen 120 frames", function () { editor.frame(120); });
r.live = live();
r.compiled = compiled();
console.log("LIVECOMPILES " + JSON.stringify(r));
app.quit();
