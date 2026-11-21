// project.remove_folder's JS half (STUDIO-HYGIENE-1 stage 2). Each process runs this file plus ONE call.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function refusal(fn) {
    try { fn(); } catch (e) { return String(e && e.message ? e.message : e); }
    return null;
}

// Process 1: a project with a real exports/ folder (a web export: viewer, glb, readme), then a second project
// left open.
function make() {
    project.create("Doors Delete", { template: "empty" });
    scene.addPrimitive("cube");
    var folder = project.current().folder;
    var web = project.exportWeb();
    assert(web.indexHtml && web.indexHtml.indexOf(folder) === 0, "the web export lands in the project's folder: " + web.dir);
    console.log("REMOVE FOLDER=" + folder);
    project.create("Doors Undeletable", { template: "empty" });
    console.log("UNDELETABLE FOLDER=" + project.current().folder);
    // ...with one asset filed in a project folder (the rows deleteFolderAndDependencies deletes, nested in the
    // project delete's transaction): the shell gives it a sidecar in the store, which a refused delete must keep.
    var things = assets.createFolder("Things");
    var sky = assets.createSky({ name: "Undeletable Sky", folder: things });
    assert(sky && assets.folders().filter(function (f) { return f.guid === things && f.count === 1; }).length === 1,
           "the undeletable project files one asset in a folder");
    console.log("UNDELETABLE SKY=" + sky);
    console.log("STORE ROOT=" + assets.storeRoot());
    project.create("Doors Keep", { template: "empty" });
    console.log("KEEP FOLDER=" + project.current().folder);
    console.log("make: ALL OK");
}

// Process 2a, the Projects directory made READ-ONLY by the shell: the folder cannot be moved aside, so the
// delete is REFUSED by its sentence and nothing goes — the row stays (the shell checks every file stayed).
function readOnly() {
    var del = project.list().filter(function (p) { return p.name === "Doors Delete"; });
    assert(del.length === 1, "the project to delete is listed");
    var why = refusal(function () { if (project.remove(del[0].guid) !== true) throw new Error("returned false"); });
    console.log("read-only: " + why);
    assert(why !== null && why.indexOf("could not be moved aside") >= 0,
           "a folder that cannot be moved aside REFUSES the delete, by its sentence");
    assert(project.list().filter(function (p) { return p.guid === del[0].guid; }).length === 1, "...and the row stays");
    console.log("read-only: ALL OK");
}

// Process 2b: a boot that deletes nothing (the shell checks the start-up sweep took the stale folder).
function boot() {
    assert(project.list().length >= 2, "the library boots");
    var keep = project.list().filter(function (p) { return p.name === "Doors Keep"; });
    assert(keep.length === 1, "the project whose folder a killed delete left aside keeps its row");
    console.log("boot: ALL OK");
}

// Process 2: the open project is refused, the other removed through the one door.
function remove() {
    var rows = project.list();
    var del = rows.filter(function (p) { return p.name === "Doors Delete"; });
    var keep = rows.filter(function (p) { return p.name === "Doors Keep"; });
    assert(del.length === 1 && keep.length === 1, "both projects are listed");
    // A PROJECT WHOSE OPEN IS IN FLIGHT IS NOT DELETED UNDER IT (the audit's W3: the door read sceneOpen only,
    // so the open installed a world for a deleted row).
    assert(project.openAsync(del[0].guid) === true, "the project to delete starts opening (threaded)");
    var opening = refusal(function () { project.remove(del[0].guid); });
    console.log("opening: " + opening);
    assert(opening !== null && opening.indexOf("is opening") >= 0, "removing a project while it opens is REFUSED");
    var turns = 0;
    while (project.openState() === "opening" && turns < 100000) ++turns;   // each call turns the loop
    assert(project.open(keep[0].guid) === true, "the project to keep is opened");
    var why = refusal(function () { project.remove(keep[0].guid); });
    console.log("open: " + why);
    assert(why !== null && why.indexOf("is open") >= 0, "removing the OPEN project is REFUSED by a sentence");
    why = refusal(function () { project.remove("no-such-guid"); });
    assert(why !== null && why.indexOf("no project with guid") >= 0, "an unknown guid is REFUSED by a sentence");
    // ROWS THE LIBRARY REFUSES TO DELETE (the shell's trigger aborts that row's DELETE): nothing is deleted —
    // the rows stay and the folder is put back where it was (the shell checks).
    var undeletable = project.list().filter(function (p) { return p.name === "Doors Undeletable"; });
    var why = refusal(function () { project.remove(undeletable[0].guid); });
    console.log("rows refused: " + why);
    assert(why !== null && why.indexOf("nothing was deleted") >= 0, "a delete whose rows the library refuses is REFUSED");
    assert(project.list().filter(function (p) { return p.guid === undeletable[0].guid; }).length === 1,
           "...and its row stays");
    assert(project.remove(del[0].guid) === true, "the other project is removed");
    assert(project.list().filter(function (p) { return p.guid === del[0].guid; }).length === 0,
           "...its row is gone");
    assert(project.list().filter(function (p) { return p.guid === keep[0].guid; }).length === 1,
           "...and the open one's stays");
    console.log("remove: ALL OK");
}
