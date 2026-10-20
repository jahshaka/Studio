// meshbake.quit_during_bake (archived: archive/tests/meshbake)'s script (bake_quit.sh): a dense model is placed (its import bakes it),
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
// THE QUIT LANDS INSIDE THE JOB (read 5): wait for the worker to have STARTED the bake — a quit
// before the pool picked the task up stops the worker between jobs, and there is no "cancelled" line
// to see. A POLL, not frames: this run is headless (no editor.frame), and the worker runs on its own
// thread whatever this one does; bounded by a count, never a clock.
// kMaxPolls (read 6 I4): measured, the bake is already running at the FIRST check (0 polls in 5 runs,
// 2026-10-10); a poll is two verb calls, 181 us in this Debug build (200,000 in 36.2 s), so 100,000
// polls end a never-started bake in a named FAIL after ~18 s, well inside the row's 80 s TIMEOUT
// (the import before it is ~13 s).
var kMaxPolls = 100000;
var polls = 0;
while (assets.bakesRunning() === 0 && assets.pendingBakes() > 0 && polls < kMaxPolls) ++polls;
console.log("QUIT_POLLS " + polls);
// A NAMED RED, never ctest's TIMEOUT (read 6 I4): the bound is a COUNT of polls.
if (assets.bakesRunning() === 0) {
    if (assets.pendingBakes() > 0)
        throw new Error("the bake never started (" + polls + " polls; the worker never picked the job up)");
    throw new Error("the bake finished before the quit could land inside it (" + polls + " polls)");
}
console.log("QUIT_RUNNING " + assets.bakesRunning());
console.log("QUIT_END");
