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
    project.create("Doors Keep", { template: "empty" });
    console.log("make: ALL OK");
}

// Process 2: the open project is refused, the other removed through the one door.
function remove() {
    var rows = project.list();
    var del = rows.filter(function (p) { return p.name === "Doors Delete"; });
    var keep = rows.filter(function (p) { return p.name === "Doors Keep"; });
    assert(del.length === 1 && keep.length === 1, "both projects are listed");
    assert(project.open(keep[0].guid) === true, "the project to keep is opened");
    var why = refusal(function () { project.remove(keep[0].guid); });
    console.log("open: " + why);
    assert(why !== null && why.indexOf("is open") >= 0, "removing the OPEN project is REFUSED by a sentence");
    why = refusal(function () { project.remove("no-such-guid"); });
    assert(why !== null && why.indexOf("no project with guid") >= 0, "an unknown guid is REFUSED by a sentence");
    assert(project.remove(del[0].guid) === true, "the other project is removed");
    assert(project.list().filter(function (p) { return p.guid === del[0].guid; }).length === 0,
           "...its row is gone");
    assert(project.list().filter(function (p) { return p.guid === keep[0].guid; }).length === 1,
           "...and the open one's stays");
    console.log("remove: ALL OK");
}
