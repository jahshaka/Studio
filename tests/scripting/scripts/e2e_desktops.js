// scripting.e2e.desktops — DESKTOP_SLIDER_SPEC: the desktop.* verbs behind
// the desktop view modes and the slider filmstrip, end to end in the real app
// (windowed --script run, scratch HOME).
//
// Note the scratch HOME persists across ctest reruns: the script forces known
// state instead of asserting first-boot defaults, and uses unique names.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function throws(fn, msg) {
    var threw = false;
    try { fn(); } catch (e) { threw = true; console.log("ok: " + msg + " (" + e.message + ")"); }
    if (!threw) throw new Error("assert failed (no error): " + msg);
}

// ---- THE GRID IS BUILT ONCE PER DESKTOP CHANGE, NEVER FOR ONE PROJECT'S
// (SMALL-FIXES-1 F2, then CREATE-GAP-1) ----
// desktop.gridStats() counts the builds (each also writes one "desktop: grid
// built" line to the log, with its cost). A script boot shows the
// EDITOR page without entering any space, so it must have built NONE. The grid
// is then built ONCE, the first time the Desktop shows (here: the close inside
// the second create), and after that only when the DESKTOP changes (another
// desktop selected). A create, a close, a Desktop entry, a move, an import, a
// rename and a delete each change ONE tile and build nothing — the grid is a
// model (CREATE-GAP-1: the rebuild on every entry cost ~11 ms a tile inside
// every close, 450 ms at 40 tiles).
function gridBuilds() { return desktop.gridStats().builds; }
function outOfStep() { return desktop.gridStats().outOfStep; }
assert(gridBuilds() === 0, "a script boot (editor page) has built no Desktop grid: " + gridBuilds());

// ---- registry: the desktop module is present and documented ----
var mods = api.verbs().filter(function (m) { return m.module === "desktop"; });
assert(mods.length === 1, "desktop module registered");
var names = mods[0].verbs.map(function (v) { return v.name; }).sort().join(",");
assert(names === "exportTile,gridStats,moveTile,setSliderRows,setViewMode,sliderRows,tiles,viewMode",
       "desktop verbs: " + names);

// ---- fixtures: three fresh (never-assigned) projects on desktop 1 ----
app.desktop(1);
var t = Date.now();
var g1 = project.create("Slider A " + t);
assert(gridBuilds() === 0, "a create with no world open builds no grid (nothing closed)");
var g2 = project.create("Slider B " + t);
assert(gridBuilds() === 1,
       "the session's FIRST Desktop showing (the close inside a create) builds the grid once ("
       + gridBuilds() + ")");
var g3 = project.create("Slider C " + t);
assert(g1.length > 10 && g2.length > 10 && g3.length > 10, "three projects created");
assert(gridBuilds() === 1, "a create's close over a built grid rebuilds nothing");

function tileGuids() { return desktop.tiles().map(function (x) { return x.guid; }); }
function onDesktop1() { return project.list({ desktop: 1 }).length; }
var builds = gridBuilds();
app.space("desktop");
assert(gridBuilds() === builds, "a Desktop entry rebuilds nothing");
[g1, g2, g3].forEach(function (g) {
    assert(tileGuids().indexOf(g) >= 0, "the created project " + g + " is a tile (added with its row)");
});
assert(desktop.tiles().length === onDesktop1(),
       "one tile per project on the desktop (" + desktop.tiles().length + "/" + onDesktop1() + ")");

// ---- ONE TILE PER MEMBERSHIP CHANGE, NO BUILD (CREATE-GAP-1) ----
var n = desktop.tiles().length;
var gx = project.create("Member X " + t);
assert(desktop.tiles().length === n + 1 && tileGuids().indexOf(gx) >= 0,
       "a create adds exactly its tile");
assert(tileGuids()[0] === gx, "...first in the order, where a rebuild puts the newest project");
project.close();
assert(project.moveToDesktop(gx, 2) === true, "moveToDesktop(gx, 2)");
assert(desktop.tiles().length === n && tileGuids().indexOf(gx) < 0, "a move away takes its one tile off");
assert(project.moveToDesktop(gx, 1) === true, "moveToDesktop(gx, 1)");
assert(desktop.tiles().length === n + 1 && tileGuids().indexOf(gx) >= 0, "a move back puts it on");
assert(project.rename(gx, "Member Renamed " + t) === true, "rename gx");
assert(desktop.tiles().filter(function (x) { return x.guid === gx; })[0].name === "Member Renamed " + t,
       "a rename changes the tile's caption in place");
assert(project.remove(gx) === true, "remove gx");
assert(desktop.tiles().length === n && tileGuids().indexOf(gx) < 0, "a delete removes exactly its tile");
assert(gridBuilds() === builds, "none of create / close / move / rename / delete built the grid");
app.space("desktop");
assert(outOfStep() === 0, "a Desktop entry found the tiles in step with the library (every path used its tile verb)");

// ---- view mode: valid value, forced reset, round-trip, validation ----
var mode = desktop.viewMode();
assert(["rows", "freeform", "sliders"].indexOf(mode) >= 0, "viewMode returns a valid mode: " + mode);
assert(desktop.setViewMode("rows") === true, "setViewMode(rows)");
assert(desktop.viewMode() === "rows", "viewMode round-trips rows");
throws(function () { desktop.setViewMode("bogus"); }, "setViewMode rejects an unknown mode");
throws(function () { desktop.moveTile(g1, 1, 0); }, "moveTile requires sliders mode");

// ---- enter sliders: every tile gets a filmstrip assignment ----
assert(desktop.setViewMode("sliders") === true, "setViewMode(sliders)");
assert(desktop.viewMode() === "sliders", "viewMode round-trips sliders");

function tileOf(guid) {
    var hits = desktop.tiles().filter(function (t) { return t.guid === guid; });
    if (hits.length !== 1) throw new Error("tile not found: " + guid);
    return hits[0];
}
assert(desktop.tiles().length >= 3, "tiles() lists the desktop");
[g1, g2, g3].forEach(function (g, i) {
    var tile = tileOf(g);
    assert(tile.row >= 1 && tile.index >= 0, "tile " + (i + 1) + " seeded into row " + tile.row);
});

// ---- moveTile: insert-at semantics ----
assert(desktop.moveTile(g1, 2, 0) === true, "moveTile(g1, row 2, index 0)");
assert(tileOf(g1).row === 2 && tileOf(g1).index === 0, "g1 landed at row 2 index 0");

assert(desktop.moveTile(g2, 2, -1) === true, "moveTile(g2, row 2, append)");
var p2 = tileOf(g2);
assert(p2.row === 2 && p2.index > tileOf(g1).index, "append lands after the row's tiles");

assert(desktop.moveTile(g3, 2, 0) === true, "moveTile(g3, row 2, index 0)");
assert(tileOf(g3).index === 0, "insert at 0 takes the head");
assert(tileOf(g1).index === 1, "existing tile shifted right");

throws(function () { desktop.moveTile("not-a-guid", 1, 0); }, "moveTile rejects an unknown guid");

// ---- slider ROWS apply LIVE (VISUAL_PARITY re-audit F8) ----
// Preferences used to write "slider_rows" and stop, so the row count only
// changed on the next populate/mode switch. Both it and this verb now go
// through ProjectManager::setSliderRows -> DynamicGrid::setSliderRowCount,
// which re-lays the live desktop out: the proof below is a tile parked in row
// 8 that is inside 1..2 the moment a shrink returns — no repopulate, no mode
// switch, nothing else touched in between.
var rowsBefore = desktop.sliderRows();
assert(rowsBefore >= 2 && rowsBefore <= 10, "sliderRows reads the live count: " + rowsBefore);
throws(function () { desktop.setSliderRows(1); }, "setSliderRows rejects 1");
throws(function () { desktop.setSliderRows(11); }, "setSliderRows rejects 11");

assert(desktop.setSliderRows(8) === 8, "setSliderRows(8) takes effect");
assert(desktop.sliderRows() === 8, "sliderRows reads back 8");
assert(desktop.moveTile(g1, 8, 0) === true, "park g1 in row 8 of the 8-row layout");
assert(tileOf(g1).row === 8, "g1 sits in row 8");

assert(desktop.setSliderRows(2) === 2, "setSliderRows(2) takes effect");
assert(desktop.sliderRows() === 2, "sliderRows reads back 2");
assert(tileOf(g1).row <= 2, "g1 re-folded into the 2-row layout LIVE (row " + tileOf(g1).row + ")");
desktop.tiles().forEach(function (t) {
    assert(t.row >= 1 && t.row <= 2, "every tile is inside the new row range");
});

// Restore the count this session started with, then re-place the two tiles the
// persistence checks below read.
assert(desktop.setSliderRows(rowsBefore) === rowsBefore, "row count restored");
assert(desktop.moveTile(g3, 2, 0) === true, "re-place g3 after the row-count churn");
assert(desktop.moveTile(g1, 2, 1) === true, "re-place g1 after the row-count churn");

// ---- persistence: survives a desktop switch (full repopulate from the DB) ----
var buildsBeforeSwitch = gridBuilds();
app.desktop(2);
app.desktop(1);
assert(gridBuilds() - buildsBeforeSwitch === 2,
       "a DESKTOP CHANGE is the one thing that builds the grid: one build per switch");
var st = desktop.gridStats();
assert(st.lastBuildTiles === onDesktop1() && st.lastBuildDecodes === 0,
       "a rebuild of a desktop the session has shown decodes NO thumbnail (" + st.lastBuildDecodes
       + " decodes over " + st.lastBuildTiles + " tiles, " + st.lastBuildMs + " ms)");
assert(desktop.viewMode() === "sliders", "view mode persisted per desktop");
assert(tileOf(g3).row === 2 && tileOf(g3).index === 0, "assignment survives a repopulate");
assert(tileOf(g1).row === 2 && tileOf(g1).index === 1, "row order survives a repopulate");

// ---- lossless: leaving sliders discards nothing ----
assert(desktop.setViewMode("freeform") === true, "switch away to freeform");
assert(desktop.setViewMode("sliders") === true, "and back to sliders");
assert(tileOf(g3).row === 2 && tileOf(g3).index === 0, "mode switching is lossless");

// ---- the OPEN project is readable from the desktop (owner request
// 2026-09-08: the open tile wears a dark blue caption bar; the same state is
// the tiles() `open` field, so a script/MCP session can find it too) ----
assert(desktop.tiles().every(function (t) { return typeof t.open === "boolean"; }),
       "every tile carries an open flag");

project.open(g2);
app.space("desktop");
var opened = desktop.tiles().filter(function (t) { return t.open; });
assert(opened.length === 1 && opened[0].guid === g2,
       "exactly the open project's tile reports open");

project.close();
assert(desktop.tiles().every(function (t) { return t.open === false; }),
       "closing the project clears the flag on every tile");

// ---- desktop_tiles: PROJECTS IMPORTED AFTER BOOT ARE TILES AFTER A CLOSE
// (TRAY-REPOP-1). The repro, measured before the fix: export one project, import
// the archive twice, open one import, close it — the grid came back with NO tile
// at all for them (0 tiles over 3 projects on a fresh home): the scripted import
// added no tile, and the Desktop entry after a close rebuilt the grid only while
// a scene was open, which closeProject had just cleared.
var src = project.create("Tiles src " + t);
var archive = project.current().folder + "-tiles.jah";
var exported = project.exportArchive(archive);
assert(exported && exported.path, "desktop_tiles: the source project is exported");
project.close();
var imports = [project.importArchive(exported.path), project.importArchive(exported.path)];
imports.forEach(function (r, i) {
    assert(r && r.guid, "desktop_tiles: import " + (i + 1) + " -> " + (r && r.guid));
});
imports.forEach(function (r) {
    assert(tileGuids().indexOf(r.guid) >= 0, "desktop_tiles: the import is a tile at once");
});
assert(project.open(imports[0].guid) === true, "desktop_tiles: one import opened");
assert(project.close() === true, "desktop_tiles: …and closed");
var onDesktop = project.list({ desktop: 1 }).map(function (p) { return p.guid; });
imports.concat([{ guid: src }]).forEach(function (r) {
    assert(tileGuids().indexOf(r.guid) >= 0, "desktop_tiles: " + r.guid + " is a tile after the close");
});
assert(desktop.tiles().length === onDesktop.length,
       "desktop_tiles: the grid after the close is every project on the desktop ("
       + desktop.tiles().length + " tiles, " + onDesktop.length + " projects)");

// ---- A TILE'S EXPORT NEVER TOUCHES THE OPEN WORLD (CREATE-GAP-1's fix round) ----
// The tile's Export used to re-point the LIVE project at the exported tile and
// save "the scene" — the open world, written into the exported project's row —
// and the pointer stayed there, so every later autosave of the open world
// landed in that row too. A open with an edit, B exported from the Desktop,
// then A saved: B's row is untouched and A's row has the edit.
var expA = project.create("Export A " + t);
var expB = project.create("Export B " + t);
var bNodes = scene.nodes().length;
assert(project.open(expA) === true, "export: A opened");
scene.addPrimitive("cube", { count: 3 });
var aNodes = scene.nodes().length;
app.space("desktop");
var expPath = project.current().folder + "-export-b.zip";
assert(desktop.exportTile(expB, expPath) === true, "export: B exported from its tile (" + app.lastError() + ")");
var turns = 0;
while (project.archiveState() === "running") { editor.frame(1); if (++turns > 40000) break; }
assert(project.archiveResult().ok === true, "export: the archive finished ok (" + project.archiveResult().error + ")");
assert(project.current().guid === expA, "export: the current project is still A");
assert(project.save() === true, "export: A saved after the export");
assert(project.open(expB) === true, "export: B reopened");
assert(scene.nodes().length === bNodes,
       "export: B's row is B's world (" + scene.nodes().length + " nodes, created with " + bNodes + ")");
assert(project.open(expA) === true, "export: A reopened");
assert(scene.nodes().length === aNodes, "export: A's row has A's edit (" + scene.nodes().length + "/" + aNodes + ")");
project.close();

assert(outOfStep() === 0, "no path left the grid out of step with the library");

// leave the desktop in rows mode for whoever runs next
assert(desktop.setViewMode("rows") === true, "restored rows mode");
console.log("desktop slider verbs e2e: all checks passed");
