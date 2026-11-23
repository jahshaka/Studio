// library.scale's session script (LARGE-LIBRARY-1 stage 2; tests/assettray/library_scale.sh writes the prefix:
// MODE, OPEN, PNG). One --headless session per generated library; every line it prints starts "SCALE ".
//   MODE "first": the library's FIRST session — the seed of the shipped primitives and, on a library written with
//                 the previous index set, the boot's one-time index gate. Its boot is printed, never barred.
//   MODE "law":   a RETURNING user's session: the boot, then the steps, each read around by app.databaseReads
//                 (TRACED statements and pages of the default connection) with the query log on (the named view:
//                 the workers' tile reads are printed by name in it).
//   MODE "control": the boot alone, on root A with its bake rows dropped - the sweep's zero's control (its report).
// Every mode ends with the boot's report (app.bootReport(): its results, never a log line) and "end".
function reads() { return app.databaseReads(); }
function line(text) { console.log("SCALE " + text); }

var boot = reads();
if (!boot.available) { line("unavailable " + boot.why); }
line("step=boot statements=" + boot.statements + " bytes=" + boot.bytes + " traced=" + boot.traced);

function step(name, fn) {
    app.queryLog({ on: true });
    var r0 = reads();
    var t0 = Date.now();
    var out = fn();
    var ms = Date.now() - t0;
    var r1 = reads();
    var log = app.queryLog({ on: false });
    var nonGuid = 0;
    (log.thumbnailSelects || []).forEach(function (t) { if (!t.byGuid) nonGuid += t.count; });
    var names = (log.byName || []).map(function (e) { return e.name + "=" + e.count; }).join(",");
    line("step=" + name + " statements=" + (r1.statements - r0.statements) + " bytes=" + (r1.bytes - r0.bytes) +
         " logged=" + log.statements + " ms=" + ms + " nonGuidThumbnailSelects=" + nonGuid + " lastError=" +
         app.lastError() + " byName=" + names);
    return out;
}

if (MODE === "first") {
    // A USER WHO HAS WORKED IN MATERIALS: the open project's tab set saved with a material of the Materials home
    // open in it (made here by the module's own verb: a generated material's store payload is not a graph the page
    // opens) — the returning session's first entry RESTORES it (EffectsPage::showEvent).
    project.open(OPEN);
    app.space("materials");
    // A SECOND material, never opened here: the returning session opens it FROM THE DRAWER (step openFromDrawer).
    materials.create("library.scale drawer", { graph: true });
    var made = materials.create("library.scale tab", { graph: true });
    materials.open(made);
    var open = materials.tabs().map(function (t) { return t.guid; });
    line("tabs " + (open.indexOf(made) >= 0 ? "saved" : "missing") + " " + JSON.stringify(open) + " lastError=" + app.lastError());
    app.space("desktop");
}
if (MODE === "law") {
    app.heartbeat(100);
    step("open", function () { return project.open(OPEN); });
    var rows = step("list", function () { return assets.list(); });
    step("listMembers", function () { return assets.list({ members: true }); });
    step("selected", function () { return assets.selected(); });
    step("materials", function () { return app.space("materials"); });   // its first entry: residue (B), printed
    // ...which RESTORED the saved tab set, and the restored material's tile is in the drawer the entry filled.
    var restored = materials.tabs().map(function (t) { return t.guid; }).filter(function (g) { return g; });
    var drawer = materials.customDrawer().map(function (t) { return t.guid; });
    line("restored " + (restored.length > 0 && drawer.indexOf(restored[0]) >= 0 ? "ok" : "missing") + " tabs=" +
         JSON.stringify(restored) + " drawer=" + drawer.length);
    // THE USER's PATH: a material opened from the custom drawer (the verb is the drawer's door: EffectsPage::openDocument).
    var fromDrawer = materials.customDrawer().filter(function (t) { return t.name === "library.scale drawer"; });
    line("drawerMaterial " + (fromDrawer.length === 1 ? "ok" : "missing"));
    if (fromDrawer.length === 1) step("openFromDrawer", function () { return materials.open(fromDrawer[0].guid); });
    step("desktop", function () { return app.space("desktop"); });
    var grid = desktop.gridStats();
    line("grid " + JSON.stringify(grid));
    step("create", function () { return project.create("library.scale create"); });
    // THE NEGATIVE CONTROL: a step made N+1 ON PURPOSE (assets.metadata, a by-guid read, for every 100th listed
    // row) — the comparer must see it grow with the library.
    step("negativeControl", function () {
        for (var i = 0; i < rows.length; i += 100) assets.metadata(rows[i].guid);
        return rows.length;
    });
    line("heartbeat " + JSON.stringify(app.heartbeatStats()));
    app.heartbeat(0);
    if (PNG) {
        var g = assets.importFile(PNG);
        line("import " + g + " lastError=" + app.lastError());
    }
}
// THE BOOT's FACTS, AS RESULTS (app.bootReport(): the shell never reads a log line). `staleRebuilds` is written by the
// bake sweep's worker once its plan is complete (-1 before): read again until it is published, BOUNDED by a count of
// polls - the bound hit prints -1 and the shell names it "the sweep never planned", never a 0.
var report = app.bootReport();
var polls = 0;
while (report.staleRebuilds < 0 && polls < 20000) { ++polls; report = app.bootReport(); }
line("report gateBuilt=" + report.indexGate.built + " gateMs=" + report.indexGate.ms + " indexAtOpen=" + report.indexAtOpen
     + " staleRebuilds=" + report.staleRebuilds + " polls=" + polls);
line("end");
