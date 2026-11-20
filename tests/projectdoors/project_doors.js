// project.refuses_unreadable's JS half (STUDIO-HYGIENE-1 stage 6 and its audit). Each process runs this file
// plus ONE call: make(dir, rig) in the first, refuse(dir) in the second.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
// A verb that fails throws; its message is what the user is told.
function refusal(fn) {
    try { fn(); } catch (e) { return String(e && e.message ? e.message : e); }
    return null;
}
function byName(name) {
    var rows = project.list().filter(function (p) { return p.name === name; });
    assert(rows.length === 1, "'" + name + "' is listed");
    return rows[0].guid;
}
// A world with a ROTATED node (a v1 scene's euler rotation is not read: opened, it comes back unrotated).
function rotatedWorld(name) {
    project.create(name, { template: "empty" });
    var id = scene.addPrimitive("cube");
    node.transform(id, { rotation: { x: 0, y: 45, z: 0 } });
    assert(project.save() === true, "'" + name + "' saves");
}

// Process 1: the four projects whose rows the python step rewrites (the corrupt one PINS a model, so a
// refused threaded open that still planned it would read its bake), and the source project, exported.
function make(dir, rig) {
    rotatedWorld("Doors Corrupt Row");
    var placed = assets.importAndPlace(rig, { position: { x: 0, y: 0, z: 0 } });
    assert(placed.nodeId, "...and pins a model");
    assert(project.save() === true, "...saved with it");
    rotatedWorld("Doors Newer Row");
    rotatedWorld("Doors Older Row");
    rotatedWorld("Doors Unversioned Row");
    project.create("Doors Source", { template: "empty" });
    scene.addPrimitive("cube");
    scene.addPrimitive("sphere");
    assert(project.save() === true, "the source project saves");
    var a = project.exportArchive(dir + "/source.zip");
    assert(a.path.length > 0, "the source project exports: " + a.path);
    console.log("make: ALL OK");
}

// One refused open, both routes' law: the sentence, nothing open, a save refused, and a second open refused
// again (the row is never changed: checked by the python step after this process).
function refusedOpen(name, words) {
    var guid = byName(name);
    var why = refusal(function () { project.open(guid); });
    console.log(name + ": " + why);
    assert(why !== null && why.indexOf("'" + name + "' could not be opened") >= 0, "opening '" + name + "' is REFUSED");
    for (var i = 0; i < words.length; ++i)
        assert(why.indexOf(words[i]) >= 0, "...the reason names '" + words[i] + "'");
    assert(project.current() == null, "...no scene is open");   // undefined: no open project
    assert(project.openRefusal() === why.replace(/^project\.open: /, ""), "...project.openRefusal() carries the same sentence");
    var saved = refusal(function () { if (project.save() !== true) throw new Error("project.save returned false"); });
    assert(saved !== null, "...a save then is refused (" + saved + ")");
    assert(refusal(function () { project.open(guid); }) !== null, "...and a second open is refused again");
}

// Process 2.
function refuse(dir) {
    var before = project.list().length;

    // THE IMPORT REFUSES WHAT THE OPEN WOULD: a newer manifest, a newer scene, a scene that does not parse.
    var why = refusal(function () { project.importArchive(dir + "/manifest_v3.zip"); });
    console.log("manifest v3: " + why);
    assert(why !== null && why.indexOf("manifest is version 3") >= 0 && why.indexOf("newer") >= 0
           && why.indexOf("reads version 2") >= 0, "an archive whose manifest is version 3 is REFUSED, naming both versions");
    why = refusal(function () { project.importArchive(dir + "/scene_v3.zip"); });
    console.log("archive scene v3: " + why);
    assert(why !== null && why.indexOf("cannot be read") >= 0 && why.indexOf("format version 3") >= 0
           && why.indexOf("newer") >= 0, "an archive whose scene is format version 3 is REFUSED (a tile that could never open)");
    why = refusal(function () { project.importArchive(dir + "/scene_garbage.zip"); });
    console.log("archive scene garbage: " + why);
    assert(why !== null && why.indexOf("cannot be read") >= 0 && why.indexOf("does not parse") >= 0,
           "an archive whose scene does not parse is REFUSED with the parse error");
    assert(project.list().length === before, "...and no project is made by any of the three");

    // THE OPEN REFUSES ANY SCENE BUT ITS OWN FORMAT — newer, older, unversioned — and one that does not parse.
    refusedOpen("Doors Newer Row", ["format version 3", "newer"]);
    refusedOpen("Doors Older Row", ["format version 1", "older"]);
    refusedOpen("Doors Unversioned Row", ["format version 1", "older"]);
    refusedOpen("Doors Corrupt Row", ["does not parse"]);

    // THE THREADED ROUTE (the tile's, the import-open's): the same refusal at its first slice, read by
    // project.openRefusal — and NOTHING planned for it: the pinned model's bake is never read.
    var corrupt = byName("Doors Corrupt Row");
    app.openStats({ reset: true });
    assert(project.openAsync(corrupt) === true, "the threaded open of the corrupt row starts");
    var turns = 0;
    while (project.openState() === "opening" && turns < 100000) ++turns;   // each call turns the loop
    assert(project.openState() === "idle", "...and ends (" + turns + " polls)");
    why = project.openRefusal();
    console.log("threaded: " + why);
    assert(why.indexOf("'Doors Corrupt Row' could not be opened") >= 0 && why.indexOf("does not parse") >= 0,
           "...refused, project.openRefusal() carrying the sentence");
    assert(project.current() == null, "...no scene is open");
    var stats = app.openStats();
    console.log("threaded open stats: " + JSON.stringify({ bakeHits: stats.bakeHits, bakeMisses: stats.bakeMisses,
                                                            workerParses: stats.workerParses, bakeBuilds: stats.bakeBuilds }));
    assert(stats.bakeHits === 0 && stats.bakeMisses === 0 && stats.workerParses === 0 && stats.bakeBuilds === 0,
           "...and nothing was planned for it: no bake read, looked up or built");

    // THE DOOR STILL OPENS: the source itself (v2 everything) opens with its two nodes, and clears the refusal.
    assert(project.open(byName("Doors Source")) === true, "a scene this build wrote opens");
    var meshes = scene.nodes().filter(function (n) { return n.type === "mesh"; });
    assert(meshes.length === 2, "...with its two nodes (" + meshes.length + ")");
    assert(project.openRefusal() === "", "...and project.openRefusal() reads empty again");
    console.log("refuse: ALL OK");
}
