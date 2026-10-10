// meshbake.quit_during_bake's script (bake_quit.sh): a dense model is placed (its import bakes it),
// exported as a share file, taken out of the library, and imported back — the import lands the rows
// and returns with the model's bake on a worker (a node paste carries the same closure, but the
// clipboard's 4 MB inline budget references a model this size instead of carrying it) — and the run
// ENDS at once. The exit must cancel that bake inside its job (MeshBake's cancel points) and end 0,
// not wait the job out or reach the forced exit 86.
var dir = app.dataRoot().root + "/..";
project.create("quit during bake " + Date.now());
var guid = assets.import(dir + "/dense.obj");
var id = assets.addToScene(guid, { position: { x: 0, y: 0, z: 0 } });
var oid = assets.importSettings(guid).sourceOid;
var file = dir + "/dense.jbundle";
var r = node.exportArchive(id, file);
console.log("QUIT_EXPORTED " + JSON.stringify(r));
node.remove(id);
assets.list({ scope: "store" }).forEach(function (row) {
    var o = null;
    try { o = assets.importSettings(row.guid).sourceOid; } catch (e) { }
    if (o === oid) assets.remove(row.guid, { keepShared: false, force: true });
});
assets.gc({ dryRun: false });
var back = assets.import(file);
console.log("QUIT_IMPORTED " + (back ? 1 : 0) + " pending " + assets.pendingBakes());
console.log("QUIT_END");
