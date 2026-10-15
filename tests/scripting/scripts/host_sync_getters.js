// host.sync_getters — THE DOOR, COUNTED (ENGINE-UPGRADE-U3 stage 2). Every synchronous getter of the engine that is
// still a read of its state on the caller's thread counts the calls the HOST makes of it (engine.syncGetters();
// a call inside one of the render loop's frames is the engine's own). U3 closes that door before the render loop
// gets its own thread (stage 4): what the editor reads per frame comes from the published snapshot, what it set it
// keeps itself.
//
// THE LAW (a count, in frames): over 300 frames of the EDITOR — the driver ticking between this script's verbs
// (`--script-live`, the owner's mode) and the script's own lockstep frames, with an object moving and selected —
// the host makes ZERO synchronous reads of the engine. Stage 4 admits exactly the lockstep verb's wait and the two
// joins (the view's create and destroy).
function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
if (project.create("sync getters") === false) throw new Error("project.create failed: " + app.lastError());
if (app.columns().space !== "editor") app.space("editor");
var cube = scene.addPrimitive("cube", { position: [0, 0.5, 0] });
editor.select(cube);
editor.frame(30);   // the open and the first frames: their reads are not the steady editor's

var before = engine.syncGetters();
for (var i = 0; i < 300; ++i) {
    node.transform(cube, { position: [Math.sin(i * 0.05), 0.5, Math.cos(i * 0.05)] });
    editor.frame(1);
}
var after = engine.syncGetters();
var moved = [];
for (var k in after.getters) {
    var d = after.getters[k] - (before.getters[k] || 0);
    if (d > 0) moved.push(k + " x" + d);
}
console.log("HOST_SYNC_GETTERS calls=" + (after.total - before.total) + " " + moved.join(", "));
assert(after.total - before.total === 0,
       "the editor read the engine synchronously " + (after.total - before.total) + " time(s) over 300 frames ("
       + moved.join(", ") + ")");
console.log("host_sync_getters: PASS");
