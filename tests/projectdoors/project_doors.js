// project.refuses_unreadable's JS half (STUDIO-HYGIENE-1 stage 6). Each process runs this file plus ONE
// call: make(dir) in the first, refuse(dir) in the second.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
// A verb that fails throws; its message is what the user is told.
function refusal(fn) {
    try { fn(); } catch (e) { return String(e && e.message ? e.message : e); }
    return null;
}

// Process 1: a real project (two nodes), saved and exported — the fixture's source; and a second
// project whose library row the python step corrupts.
function make(dir) {
    project.create("Doors Corrupt Row", { template: "empty" });
    scene.addPrimitive("cube");
    assert(project.save() === true, "the project to corrupt saves");
    var g = project.create("Doors Source", { template: "empty" });
    scene.addPrimitive("cube");
    scene.addPrimitive("sphere");
    assert(project.save() === true, "the source project saves");
    var a = project.exportArchive(dir + "/source.zip");
    assert(a.path.length > 0, "the source project exports: " + a.path);
    console.log("make: ALL OK");
}

// Process 2: the three fixtures the python made from it.
function refuse(dir) {
    var before = project.list().length;

    // 1. A NEWER BUILD'S MANIFEST: refused by name, no project made.
    var why = refusal(function () { project.importArchive(dir + "/manifest_v3.zip"); });
    console.log("manifest v3: " + why);
    assert(why !== null && why.indexOf("manifest is version 3") >= 0 && why.indexOf("newer") >= 0
           && why.indexOf("reads version 2") >= 0,
           "an archive whose manifest is version 3 is REFUSED, naming both versions");
    assert(project.list().length === before, "...and no project is made");

    // 2. A NEWER BUILD'S SCENE: the archive imports (its manifest is ours), the OPEN refuses it.
    var v3 = project.importArchive(dir + "/scene_v3.zip");
    assert(v3.guid && v3.guid.length > 0, "an archive with a v2 manifest and a v3 scene imports");
    why = refusal(function () { project.open(v3.guid); });
    console.log("scene v3: " + why);
    assert(why !== null && why.indexOf("could not be opened") >= 0
           && why.indexOf("format version 3") >= 0 && why.indexOf("newer") >= 0,
           "opening a scene of format version 3 is REFUSED, naming both versions");
    assert(project.current() == null, "...and no scene is open");   // undefined: no open project

    // 3. A SCENE THAT DOES NOT PARSE: refused, never opened (or imported) EMPTY — a save would write the
    // empty world over the row.
    var count = project.list().length;
    why = refusal(function () { project.importArchive(dir + "/scene_garbage.zip"); });
    console.log("archive scene garbage: " + why);
    assert(why !== null && why.indexOf("cannot be read") >= 0 && why.indexOf("does not parse") >= 0,
           "an archive whose scene does not parse is REFUSED with the parse error");
    assert(project.list().length === count, "...and no project is made");
    var corrupt = project.list().filter(function (p) { return p.name === "Doors Corrupt Row"; });
    assert(corrupt.length === 1, "the corrupted project is listed");
    why = refusal(function () { project.open(corrupt[0].guid); });
    console.log("row scene garbage: " + why);
    assert(why !== null && why.indexOf("could not be opened") >= 0 && why.indexOf("does not parse") >= 0,
           "opening a project whose scene does not parse is REFUSED with the parse error");
    assert(project.current() == null, "...and no scene is open");   // undefined: no open project

    // 4. THE DOOR STILL OPENS: the source itself (v2 everything) opens with its two nodes.
    var src = project.list().filter(function (p) { return p.name === "Doors Source"; });
    assert(src.length === 1, "the source project is listed");
    assert(project.open(src[0].guid) === true, "a scene this build wrote opens");
    var meshes = scene.nodes().filter(function (n) { return n.type === "mesh"; });
    assert(meshes.length === 2, "...with its two nodes (" + meshes.length + ")");
    console.log("refuse: ALL OK");
}
