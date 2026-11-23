// samples.resave_bytes — the JS half (tests/samples/resave_bytes.py says the law). Two phases, one per --headless
// process; the wrapper prepends `var PHASE = "...", ARCHIVES = [...], GUIDS = {...};` and the python half reads the
// saved blobs out of the library between them (no verb hands a script a scene blob: the projects row is the record).
//   import: each shipped archive imported (project.importArchive: its rows, its models baked), opened and saved
//           ONCE; one line `RESAVE <archive> <guid>` per archive.
//   again:  each of those projects opened and saved a SECOND time.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
}

if (PHASE === "import") {
    assert(ARCHIVES.length > 0, "the shipped archives are listed: " + ARCHIVES.length);
    ARCHIVES.forEach(function (path) {
        var name = path.substring(path.lastIndexOf("/") + 1);
        var imported = project.importArchive(path);
        assert(imported && imported.guid, "importArchive(" + name + ")");
        assert(project.open(imported.guid, { discard: true }) === true,
               "project.open(" + name + "): " + project.openRefusal());
        assert(project.save() === true, "project.save(" + name + ")");
        console.log("RESAVE " + name + " " + imported.guid);
    });
} else if (PHASE === "again") {
    Object.keys(GUIDS).forEach(function (name) {
        assert(project.open(GUIDS[name], { discard: true }) === true,
               "project.open(" + name + ") again: " + project.openRefusal());
        assert(project.save() === true, "project.save(" + name + ") again");
        console.log("RESAVE " + name + " " + GUIDS[name]);
    });
} else {
    assert(false, "unknown PHASE " + PHASE);
}
console.log("resave_bytes " + PHASE + ": done");
