// meshbake.archive_bakes, runs 2 and 3 (see archive_bakes.sh). With REBUILT
// set by the driver (run 2: the bakes were deleted, the sources are there) the
// open rebuilds them and the model is PRESENT; without it (run 3: the sources
// are gone too) the model is MISSING with a scene issue. No parse either way.
function assert(cond, msg) {
    if (!cond) { console.log("FAIL: " + msg); throw new Error("assert failed: " + msg); }
    console.log("ok: " + msg);
}
var rebuilt = (typeof REBUILT !== "undefined" && REBUILT === true);
var list = project.list();
var matcaps = list.filter(function (p) { return p.name.indexOf("Matcaps") >= 0; })[0];
assert(matcaps && matcaps.guid, "the imported Matcaps project is in the library");
// Read and zero: the library's background sweep may already have rebuilt some
// of the stale bakes before this line (it starts with the library).
var before = app.openStats({ reset: true });
assert(project.open(matcaps.guid) === true, "project.open(Matcaps)");
var s = app.openStats();
assert(s.uiThreadParses === 0 && s.workerParses === 0,
       "the open parsed NO model (" + JSON.stringify(s) + ")");
var issues = editor.checkScene().list || [];
var missing = issues.filter(function (x) { return x.kind === "model.missing"; });
if (rebuilt) {
    assert(s.bakeBuilds > 0,
           "the OPEN rebuilt its stale bakes from their sources on a worker (" + s.bakeBuilds +
           " bake build(s); the library sweep is off in this run)");
    assert(s.bakeMisses === 0, "the open read a bake for every model (misses " + s.bakeMisses + ")");
    assert(missing.length === 0, "and every model is present (" + JSON.stringify(missing) + ")");
} else {
    assert(missing.length > 0, "a model whose source is gone raises a scene issue (" + JSON.stringify(issues) + ")");
    assert(/\.(obj|fbx|glb|gltf|dae)/i.test(missing[0].message), "the issue names the model file: " + missing[0].message);
    assert(/re-import/i.test(missing[0].action || ""), "and its action says what fixes it: " + missing[0].action);
}
