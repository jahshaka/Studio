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
    assert(project.save() === true, "the save writes the row");
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
    console.log("save: ALL OK");
}
