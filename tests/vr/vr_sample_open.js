// vr.sample_open — A SAMPLE OPENS IN THE EDITOR ON A `--vr` BOOT TOO (lane
// SMOKE-FIX-1, 2026-09-18).
//
// The desktop half of this is scripting.e2e.sample_open; this is the boot the
// owner actually smoked push #50 on. A `--vr` process starts on the Desktop
// page and the editor page may never have been shown when the browser's open
// finishes, which is exactly the state that used to produce BOTH halves of his
// report — the open landing in the Player (the uninitialised play-mode flag)
// and the Player's window showing the Desktop page's stale pixels (an enabled
// View with no scene bound).
//
// The runtime is Monado's simulated headset (run_vr_app.sh). No VR session is
// begun here: the subject is what a `--vr` BOOT does to the pages, not the
// headset.
var failures = 0;
function ok(cond, msg) {
    if (cond) { console.log("ok:   " + msg); }
    else { console.log("FAIL: " + msg); ++failures; }
}

ok(app.columns().space === "desktop", "a --vr boot starts on the desktop");
console.log("info: vr available = " + JSON.stringify(player.state().vr.available)
            + " (" + player.state().vr.reason + ")");

// ---- the VR button, on the desktop, with nothing open -----------------------
// (SMOKE-FIX-1's fix round, F7.) This icon is LIVE on a `--vr` boot's Desktop
// page, and off the editor page it means "play this world in the headset".
// There is no world: it used to open the Player page over nothing at all and
// start it. The refusal has to come BEFORE the page moves.
// (vr.toggle REFUSES by returning false with the reason in app.lastError — the
// house rule for a question with two answers — rather than throwing.)
var toggled = vr.toggle();
var vrRefusal = String(app.lastError());
ok(toggled === false, "with no world open, vr.toggle() refuses");
ok(vrRefusal.indexOf("no world open") >= 0, "…and says why: " + vrRefusal);
ok(app.columns().space === "desktop", "…and the window did not move to the Player");
ok(player.state().vr.active === false, "…and no session started");
ok(player.state().playing === false, "…and nothing is playing");

ok(project.openSample("Matcaps") === true,
   "project.openSample('Matcaps') was accepted (" + app.lastError() + ")");
var turns = 0;
while (project.archiveState() === "running") { editor.frame(1); if (++turns > 40000) break; }
while (project.openState() === "opening") { editor.frame(1); if (++turns > 40000) break; }
ok(project.openState() === "idle", "the open finished (" + turns + " frames)");

var opened = project.current();
ok(!!opened && opened.name === "Matcaps", "the Matcaps world is open");
ok(app.columns().space === "editor",
   "on a --vr boot the sample landed in the EDITOR (space = " + app.columns().space + ")");
ok(editor.frame(3) === true, "the editor draws");

// …and the Player, entered deliberately, DRAWS rather than showing the page
// underneath.
ok(app.space("player") === true, "app.space('player') switched (" + app.lastError() + ")");
ok(player.frame(3) === true, "the Player drew (" + app.lastError() + ")");
ok(app.space("editor") === true, "back to the editor (" + app.lastError() + ")");

if (failures) throw new Error(failures + " assertion(s) failed");
console.log("vr.sample_open: all assertions passed");
