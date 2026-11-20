// project.save_door's JS half (STUDIO-HYGIENE-1 stage 1). Each process runs this file plus ONE call.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function refusal(fn) {
    try { fn(); } catch (e) { return String(e && e.message ? e.message : e); }
    return null;
}
function meshes() { return scene.nodes().filter(function (n) { return n.type === "mesh"; }).length; }
function dirty() { return editor.undoState().dirty; }
function byName(name) {
    var rows = project.list().filter(function (p) { return p.name === name; });
    assert(rows.length === 1, "'" + name + "' is listed");
    return rows[0].guid;
}

// Process 1: the round trip and the dirty laws through the verbs; and the project whose row the python step
// makes refuse every write.
function make() {
    var g = project.create("Doors Round Trip", { template: "empty" });
    scene.addPrimitive("cube", { count: 3 });
    assert(dirty() === true, "a push makes the scene dirty");
    var commits = editor.undoState().dbCommits;
    assert(project.save() === true, "the save writes the row");
    assert(editor.undoState().dbCommits > commits,
           "...COMMITTED before it said saved (dbCommits " + commits + " -> " + editor.undoState().dbCommits +
           "): inside a script run's gesture batch the row was written but not committed");
    assert(dirty() === false, "...and the scene is clean");
    scene.addPrimitive("sphere");
    assert(dirty() === true, "a push AFTER a save is dirty (the coerced counter read it as saved: the close never asked)");
    assert(project.save() === true, "saved again");
    assert(project.close() === true, "closed");
    assert(project.open(g) === true, "reopened");
    assert(meshes() === 4, "the round trip keeps every node (4 meshes: " + meshes() + ")");
    assert(dirty() === false, "an opened scene is clean");

    project.create("Doors Save Trap", { template: "empty" });
    scene.addPrimitive("cube");
    assert(project.save() === true, "the trap project saves (before its row is made to refuse writes)");
    project.create("Doors Save Other", { template: "empty" });
    console.log("make: ALL OK");
}

// Process 2: a save whose UPDATE matches no row (the python step's trigger drops every write to that row) is
// a FAILED save — false, the reason given, the scene still dirty, so the close asks.
function save() {
    assert(project.open(byName("Doors Save Trap")) === true, "the trap project opens");
    assert(dirty() === false, "...clean");
    scene.addPrimitive("sphere");
    assert(dirty() === true, "an edit makes it dirty");
    var why = refusal(function () { if (project.save() !== true) throw new Error("project.save returned false"); });
    console.log("save: " + why);
    assert(why !== null && why.indexOf("the scene could not be saved") >= 0,
           "a save that wrote no row FAILS, with the sentence (it reported success)");
    assert(dirty() === true, "...and the scene is STILL dirty (markSaved ran on the failure)");

    // NO CLOSE DROPS THE EDITS IT COULD NOT SAVE (the audit's B1): auto-save is on, it fails, the close is
    // REFUSED and the world stays — project.open, project.close and project.create alike.
    var trap = byName("Doors Save Trap");
    var other = byName("Doors Save Other");
    var count = project.list().length;
    why = refusal(function () { project.open(other); });
    console.log("open over a failed auto-save: " + why);
    assert(why !== null && why.indexOf("could not be saved, so it stays open") >= 0,
           "an open over a world whose auto-save fails is REFUSED (it closed the world and dropped the edits)");
    assert(project.current() && project.current().guid === trap && dirty() === true, "...the world stays, still dirty");
    assert(refusal(function () { project.close(); }) !== null, "...a close is refused the same way");
    assert(refusal(function () { project.create("Doors Never Made", { template: "empty" }); }) !== null,
           "...and a create");
    assert(project.list().length === count, "...which made no project");
    assert(project.open(other, { discard: true }) === true, "{discard: true} drops them and opens");
    console.log("save: ALL OK");
}

// Process 3, auto-save OFF (the python step seeds the setting): a dirty world is never closed silently.
function autosaveOff() {
    assert(project.open(byName("Doors Round Trip")) === true, "the round-trip project opens");
    var before = meshes();
    scene.addPrimitive("cube");
    assert(dirty() === true, "an edit makes it dirty");
    var other = byName("Doors Save Other");
    var why = refusal(function () { project.open(other); });
    console.log("open with auto-save off: " + why);
    assert(why !== null && why.indexOf("unsaved changes and auto-save is off") >= 0 && why.indexOf("discard") >= 0,
           "an open over a dirty world with auto-save off is REFUSED, naming {discard: true} (it dropped the edits)");
    assert(refusal(function () { project.close(); }) !== null, "...a close too");
    var count = project.list().length;
    assert(refusal(function () { project.create("Doors Never Made", { template: "empty" }); }) !== null, "...a create too");
    assert(project.list().length === count, "...which made no project");
    assert(project.save() === true && dirty() === false, "saved, it is clean");
    assert(project.close() === true, "...and a clean world closes");
    assert(project.open(byName("Doors Round Trip")) === true, "reopened");
    assert(meshes() === before + 1, "...with the saved edit (" + meshes() + ")");
    scene.addPrimitive("sphere");
    assert(project.close({ discard: true }) === true, "{discard: true} closes a dirty world");
    assert(project.open(byName("Doors Round Trip")) === true && meshes() === before + 1,
           "...and the discarded edit is gone, the saved one kept (" + meshes() + ")");

    // A CREATE WHOSE FIRST SAVE FAILS (the trigger drops that row's writes) is UNSAVED, not clean: the create
    // cleared the stack, and the scene read clean though its row holds no scene - the close then dropped it.
    project.create("Doors Create Fails", { template: "empty" });
    assert(dirty() === true, "a created world whose first save failed is DIRTY (it read clean)");
    assert(refusal(function () { project.close(); }) !== null, "...so its close is refused, not silent");
    assert(project.close({ discard: true }) === true, "...and {discard: true} closes it");
    console.log("autosave off: ALL OK");
}
