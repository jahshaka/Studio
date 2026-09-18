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

// ---- 3. the Player is still a deliberate statement -------------------------
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
