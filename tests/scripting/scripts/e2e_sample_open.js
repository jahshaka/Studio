// scripting.e2e.sample_open — A SAMPLE OPENS IN THE EDITOR (lane SMOKE-FIX-1,
// 2026-09-18; the owner's smoke of push #50: "every sample I open lands in the
// player").
//
// THE DEFECT THIS PINS. `ProjectManager::openInPlayMode` was an UNINITIALISED
// bool whose only writer was the desktop tile's open (openProjectFromWidget) and
// whose only reader was loadProjectAssets — which the SAMPLE BROWSER and the
// archive import reach too, by a road that never wrote it. On the owner's box
// and on the rig it read `true`, so every sample-browser open went straight to
// the Player: `=== PLAY START ===` four milliseconds before `=== SCENE OPEN ===`
// in his log, and 2,667 Player frames after it. The open mode is an ARGUMENT
// now (ProjectOpenMode, projectmanager.h), stated by each route.
//
// It drives THE REAL ROUTE: `project.openSample` is the verb the browser's tiles
// call (API-first — the dialog and the script take the same road), so a
// regression that re-reads somebody else's state fails here.
//
// Engine-up: the second half asserts the PLAYER really draws afterwards, which
// is a frame, not a counter.
var failures = 0;
function ok(cond, msg) {
    if (cond) { console.log("ok:   " + msg); }
    else { console.log("FAIL: " + msg); ++failures; }
}

// ---- 0. the catalogue -----------------------------------------------------
var names = project.samples();
ok(names.length > 0, "project.samples() lists the shipped samples (" + names.length + ")");
ok(names.indexOf("Matcaps") >= 0, "…including Matcaps");

ok(app.columns().space === "desktop", "the process boots on the desktop");

// ---- 1. the sample browser's own route ------------------------------------
ok(project.openSample("Matcaps") === true,
   "project.openSample('Matcaps') was accepted (" + app.lastError() + ")");

// BOTH halves are threaded and they run in that order: the archive import
// first, then the sliced open. A frame in each poll body (project.openState's
// note: a poll with no frame starves the very install it waits for).
var turns = 0;
while (project.archiveState() === "running") { editor.frame(1); if (++turns > 40000) break; }
ok(project.archiveState() === "idle", "the sample's import finished");
while (project.openState() === "opening") { editor.frame(1); if (++turns > 40000) break; }
ok(project.openState() === "idle", "the open finished (" + turns + " frames)");

var opened = project.current();
ok(!!opened && opened.name === "Matcaps", "the Matcaps world is open");
ok(scene.nodes().length > 0, "…with its nodes");

// THE ASSERTION THIS SUITE EXISTS FOR.
ok(app.columns().space === "editor",
   "the sample landed in the EDITOR, not the Player (space = " + app.columns().space + ")");

// ---- 2. a refusal names its subject ---------------------------------------
// A verb's refusal reaches a script as a thrown Error (the house idiom — see
// project.openAsync's in open.frames).
var refusal = "";
try { project.openSample("No Such Sample"); } catch (e) { refusal = String(e); }
ok(refusal.length > 0, "an unknown sample name is refused");
ok(refusal.indexOf("No Such Sample") >= 0, "…and the refusal names it: " + refusal);
ok(app.columns().space === "editor", "a refusal moves nothing (space = " + app.columns().space + ")");

// ---- 3. a sample opened OVER an open world closes that world properly -------
// (SMOKE-FIX-1's fix round, F6.) The import-open path re-pointed `project` at
// the new world and simply dropped the old one: no autosave under `auto_save`,
// no undo-stack reset. Both are what closeProject does, and it is now on this
// road too — `project.openSample` is callable from any page, and its contract
// says it behaves like project.open/openAsync.
var firstGuid = project.current().guid;
var addedId = scene.addPrimitive("cube", { position: { x: 3, y: 1, z: -2 } });
ok(!!addedId, "a cube was added to the open sample");
var namesBefore = scene.nodes().length;

ok(project.openSample("Mirror Room") === true,
   "a SECOND sample opens over the first (" + app.lastError() + ")");
turns = 0;
while (project.archiveState() === "running") { editor.frame(1); if (++turns > 40000) break; }
while (project.openState() === "opening") { editor.frame(1); if (++turns > 40000) break; }
ok(project.openState() === "idle", "the second open finished");
ok(project.current().name === "Mirror Room", "the Mirror Room is open now");
ok(project.current().guid !== firstGuid, "…and it is a different world");
// THE UNDO STACK BELONGS TO THE WORLD THAT LEFT. Nothing on it may name a node
// of a project that is closed (closeProject clears it; this path skipped that).
var u = editor.undoState();
ok(u.canUndo === false,
   "the undo stack was cleared with the world that left (count " + u.count + ")");

// …and the edit went with it, not into the void: auto_save is on by default, so
// reopening the first world finds the cube.
ok(project.openAsync(firstGuid) === true, "the first world reopens");
turns = 0;
while (project.openState() === "opening") { editor.frame(1); if (++turns > 40000) break; }
ok(project.current().guid === firstGuid, "…and it is the one that was edited");
ok(scene.nodes().length === namesBefore,
   "the edit made before the second open was SAVED with it ("
   + scene.nodes().length + " nodes, expected " + namesBefore + ")");

// ---- 4. SAVE IS ALWAYS OFFERED (owner, 2026-09-18) --------------------------
// "Show it even with auto save, as I may want a force save." Auto-save is ON by
// default, and the Save button's visibility used to be `!auto_save` — so on a
// stock install the one control that writes the world down at the moment the
// user chooses was not there at all.
function toolbarAction(id) {
    var bar = editor.toolbar();
    for (var i = 0; i < bar.length; ++i) if (bar[i].id === id) return bar[i];
    return null;
}
var autoSaveOn = true;   // the shipped default for `auto_save`
var save = toolbarAction("saveScene");
ok(!!save, "the editor toolbar has a Save control");
ok(save && save.visible === true,
   "…and it is VISIBLE with auto-save on (the default)");
ok(save && save.enabled === true, "…and enabled, because a world is open");
// …and it saves: the capability behind the button, with a world open.
ok(project.save() === true, "the force save writes the world (" + app.lastError() + ")");

// ---- 5. the Player is still a deliberate statement -------------------------
// Entering it is the user's choice, and when they make it the page must DRAW:
// player.frame refuses when the view could not be bound to the editor's scene,
// so `true` here is real frames on the player's own window.
ok(app.space("player") === true, "app.space('player') switched (" + app.lastError() + ")");
ok(app.columns().space === "player", "the window is on the Player");
ok(player.frame(3) === true, "the Player drew (" + app.lastError() + ")");
ok(app.space("editor") === true, "…and the editor takes the screen back");
ok(app.columns().space === "editor", "space = " + app.columns().space);

if (failures) throw new Error(failures + " assertion(s) failed");
console.log("sample_open: all assertions passed");
