// meshbake.archive_bakes, run 2 (see archive_bakes.sh): the bakes are gone.
function assert(cond, msg) {
    if (!cond) { console.log("FAIL: " + msg); throw new Error("assert failed: " + msg); }
    console.log("ok: " + msg);
}
var list = project.list();
var matcaps = list.filter(function (p) { return p.name.indexOf("Matcaps") >= 0; })[0];
assert(matcaps && matcaps.guid, "the imported Matcaps project is in the library");
app.openStats({ reset: true });
assert(project.open(matcaps.guid) === true, "project.open(Matcaps) with its bakes gone");
var s = app.openStats();
assert(s.uiThreadParses === 0 && s.workerParses === 0,
       "a model with no current bake is NOT parsed (" + JSON.stringify(s) + ")");
var issues = editor.checkScene().list || [];
var missing = issues.filter(function (x) { return x.kind === "model.missing"; });
assert(missing.length > 0, "the missing model raises a scene issue (" + JSON.stringify(issues) + ")");
assert(/\.(obj|fbx|glb|gltf|dae)/i.test(missing[0].message), "the issue names the model file: " + missing[0].message);
