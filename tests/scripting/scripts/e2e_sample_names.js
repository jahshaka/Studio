// pool.assets_import / sample_names — desktop.sample_names (lane PROJECT-NAMES-1,
// the owner 2026-10-07: "users should not see the double thumbnails").
//
// THE DEFECT THIS PINS (spikes/double-thumb/). Opening a shipped sample imports
// its archive as a NEW project (fresh guid), and the import kept the archive's
// name — so a sample opened twice put two identical "Matcaps" tiles on the
// Desktop. The owner's rule: a reopened sample is ALWAYS a fresh copy, UNIQUELY
// NAMED — "Matcaps", "Matcaps 2", "Matcaps 3" (project.nextFreeName).
//
// Engine-up: project.openSample is the sample browser's route (a window verb),
// and the tiles are the Desktop page's.
var failures = 0;
function ok(cond, msg) {
    if (cond) { console.log("ok:   " + msg); }
    else { console.log("FAIL: " + msg); ++failures; }
}
var family = /^matcaps( \d+)?$/i;

// ---- 0. a clean family: this pool's process lives on, and another arm opens
// Matcaps too ------------------------------------------------------------------
if (project.current()) project.close();
project.list().forEach(function (p) {
    if (family.test(p.name)) ok(project.remove(p.guid) === true, "cleared a left-over '" + p.name + "'");
});

function openMatcaps(expected) {
    ok(project.openSample("Matcaps") === true,
       "project.openSample('Matcaps') accepted (" + app.lastError() + ")");
    var imported = project.waitArchive();
    ok(imported.ok === true, "…the import finished (" + JSON.stringify(imported) + ")");
    var turns = 0;
    while (project.openState() === "opening") { editor.frame(1); if (++turns > 40000) break; }
    ok(project.openState() === "idle", "…the open finished (" + turns + " frames)");
    var cur = project.current();
    ok(!!cur && cur.name === expected, "…as '" + expected + "' (got '" + (cur && cur.name) + "')");
    return cur ? cur.guid : "";
}

var g1 = openMatcaps("Matcaps");
var g2 = openMatcaps("Matcaps 2");
ok(g2 !== g1, "the reopen is a FRESH COPY (a new guid)");
var g3 = openMatcaps("Matcaps 3");
ok(g3 !== g1 && g3 !== g2, "…and so is the third");

// ---- the Desktop: three tiles, three names ---------------------------------
project.close();
var tiles = desktop.tiles();
var mine = tiles.filter(function (t) { return t.guid === g1 || t.guid === g2 || t.guid === g3; });
ok(mine.length === 3, "three Matcaps tiles on the Desktop (" + mine.length + ")");
var names = mine.map(function (t) { return t.name; }).sort();
ok(JSON.stringify(names) === JSON.stringify(["Matcaps", "Matcaps 2", "Matcaps 3"]),
   "…named Matcaps, Matcaps 2, Matcaps 3: " + JSON.stringify(names));
var seen = {}, dupes = [];
tiles.forEach(function (t) {
    var k = String(t.name).toLowerCase();
    if (seen[k]) dupes.push(t.name);
    seen[k] = true;
});
ok(dupes.length === 0, "no two Desktop tiles carry one name (" + JSON.stringify(dupes) + ")");

// ---- cleanup --------------------------------------------------------------
[g1, g2, g3].forEach(function (g) { if (g) ok(project.remove(g) === true, "remove " + g); });

if (failures) throw new Error(failures + " assertion(s) failed");
console.log("sample_names: all assertions passed");
