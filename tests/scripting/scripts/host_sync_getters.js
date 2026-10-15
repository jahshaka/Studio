// host.sync_getters — THE DOOR, COUNTED (ENGINE-UPGRADE-U3 stage 2). Every synchronous getter of the engine that is
// still a read of its state on the caller's thread counts the calls the HOST makes of it (engine.syncGetters();
// a call inside one of the render loop's frames is the engine's own). U3 closes that door before the render loop
// gets its own thread (stage 4): what the editor reads per frame comes from the published snapshot, what it set it
// keeps itself.
//
// THE LAW (a count, in frames): over 600 frames of the EDITOR — 300 of the driver's own ticks (`--script-live`, the
// owner's mode: one after each script frame) and the script's 300 lockstep frames, an object moving and selected —
// the host makes ZERO synchronous reads of the engine. Stage 4 admits exactly the lockstep verb's wait and the two
// joins (the view's create and destroy).
function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
var OTHER = "sync getters other " + Date.now();   // the world the cover's load in place opens, inside the window
if (project.create(OTHER) === false) throw new Error("project.create failed: " + app.lastError());
var NAME = "sync getters " + Date.now();
if (project.create(NAME) === false) throw new Error("project.create failed: " + app.lastError());
if (app.columns().space !== "editor") app.space("editor");
var cube = scene.addPrimitive("cube", { position: [0, 0.5, 0] });
editor.select(cube);
editor.frame(30);   // the open and the first frames: their reads are not the steady editor's

var before = engine.syncGetters();
var driverFrames = 0;
var covered = false, panelShown = false, overlayShown = false;
var viewRecreated = false, vrProbed = false, cacheSaved = false, captured = false;
var CAPTURE = "host_sync_getters_capture.png";   // the row's own working directory (a scratch home)
for (var i = 0; i < 300; ++i) {
    // THE ROW SEES ITS SUBJECTS (ENGINE-UPGRADE-U3 stage 3, decision 18): inside the counted window, the World panel
    // shown and edited (its shadow and GI rows refresh off the snapshot), the stats overlay on (its figures read
    // off the snapshot), and one cover (a load in place under the loading cover: presentCovered's size check).
    if (i === 60) { editor.propertiesTab({ tab: "world" }); panelShown = true; }
    if (i === 80) world.setShadowResolution(1024);
    if (i === 120) { editor.setOverlays({ stats: true }); overlayShown = true; }
    if (i === 200) {
        editor.loadingCover(true);
        var covers0 = editor.viewportState().coversPresented;
        if (!project.openAsync(OTHER)) throw new Error("project.openAsync failed: " + app.lastError());
        for (var g = 0; g < 4000 && project.openState() !== "idle"; ++g) {
            if (editor.viewportState().cover !== "none") covered = true;
            editor.frame(1);
        }
        covered = covered || editor.viewportState().coversPresented > covers0;
        console.log("the load in place: covers presented " + covers0 + " -> " + editor.viewportState().coversPresented);
        editor.loadingCover(false);
        cube = scene.addPrimitive("cube", { position: [0, 0.5, 0] });
        editor.select(cube);
    }
    // ...AND THE STAGE-4 SUBJECTS (U3 stage 4a, decision 39): views made and destroyed (the Materials page's
    // preview views, then the editor's again), a VR probe, a shader-cache save and a capture of the presented frame.
    if (i === 230) { app.space("materials"); }
    if (i === 240) { app.space("editor"); viewRecreated = true; }
    if (i === 250) { var va = vr.available(); vrProbed = va !== undefined && va !== null; }
    if (i === 260) { cacheSaved = app.saveShaderCache() !== undefined; }
    if (i === 270) { var pf = editor.presentedFrame(CAPTURE); captured = !!pf && pf.width > 0; }
    node.transform(cube, { position: [Math.sin(i * 0.05), 0.5, Math.cos(i * 0.05)] });
    editor.frame(1);
    // ...AND ONE OF THE DRIVER'S OWN TICKS after it (the owner's loop: its beforeFrame subscribers, its status reads):
    // the verbs below return to the event loop until the driver has rendered once more. Bounded in polls.
    var r0 = app.frameStats().rendered, polls = 0;
    while (app.frameStats().rendered === r0 && ++polls < 200000) {}
    if (app.frameStats().rendered !== r0) ++driverFrames;
}
console.log("driver frames in the window: " + driverFrames);
assert(panelShown && overlayShown, "the World panel and the stats overlay were up inside the window");
assert(covered, "a cover was on screen inside the window (the load in place under the loading cover)");
assert(viewRecreated && vrProbed && cacheSaved && captured,
       "views were made and destroyed, a VR probe, a shader-cache save and a capture ran inside the window");
assert(driverFrames >= 290, "the driver ticked between the script's frames (" + driverFrames + " of 300)");
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
