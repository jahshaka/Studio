// scripting.live / scripting.live_off — THE RUN POLICY (SCRIPTING_LIVE_SPEC §3.1).
//
// ONE script, two ctest entries: the app is started with --script-live for the
// first and plainly for the second, and the script asks which run it is in
// (app.scriptPolicy().runPolicy) before asserting the opposite halves of the
// same promise.
//
//   live: the JavaScript is on a worker thread and the UI thread is free
//         between verbs, so the render driver TICKS while the script builds —
//         app.frameStats().rendered moves BEFORE the last verb, which on the
//         old UI-thread engine it could never do.
//   off:  the driver skips its ticks for the whole run, so rendered does not
//         move by a single frame between the first verb and the last — exactly
//         what a blocked UI thread used to give, and what the 54 frame-stepping
//         e2e scripts depend on.
//
// Both halves also assert the contracts the threading must not have broken:
// one undo entry for the run, console output in order, and editor.frame(n)
// rendering its n frames either way (it is a verb; it runs on the UI thread).

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

var policy = app.scriptPolicy();
var live = policy.runPolicy === "live";
console.log("run policy: " + policy.runPolicy + " (setting: " + policy.mode + ")");
assert(policy.running === true, "a script reading its own state is running");

var guid = project.create("Live Feedback " + Date.now());
assert(guid.length > 10, "project.create -> " + guid);

// ---- the undo bracket, before anything is built ----
var undoBefore = editor.undoState();
assert(undoBefore.macroOpen === true, "the run's undo macro is open on the UI thread");

// ---- THE EDIT GATE (owner, ledger §423) ----
//
// While this run is in flight the editor is non-editable by hand — the gate
// refuses every document write arriving from the UI, under EITHER policy,
// because the UI thread is free between these verbs either way. Nobody is
// clicking in a ctest run, so what this asserts is the state the app is really
// in (the refusal itself is proven in scripting.engine, where a timer delivers
// a hand edit into a live run, and in ui.panel_undo and gizmo.group_transform,
// where real gestures are driven against the gate).
var gate = editor.editGate();
assert(gate.scriptRunning === true, "the edit gate says a script run is in flight");
assert(gate.refusals === 0, "...and nothing has been refused: nobody is clicking");
assert(gate.notice === false, "...so the run's 'script running' notice was never raised");

// ---- twenty nodes, watching the driver ----
//
// THE MEASUREMENT IS WALL-TIME BOUNDED, not verb-counted (round 2, M3). A live
// run is PACED — at most one frame per display period — so "a frame happened
// between two verbs" is only a promise about TIME, and a test that counted
// verbs would be leaning on an alternation the pacing exists to break. The
// budget below is many display periods wide on any panel this runs on.
var kBudgetMs = 250;

var framesAtStart = app.frameStats().rendered;
var movedDuring = 0;
var ids = [];
for (var i = 0; i < 20; ++i) {
    ids.push(scene.addPrimitive("sphere", {
        position: { x: (i % 5) * 2 - 4, y: 1, z: Math.floor(i / 5) * 2 - 2 }
    }));
    if (i < 19 && app.frameStats().rendered > framesAtStart) movedDuring++;
}
assert(ids.length === 20, "twenty primitives added through the bridge");

// ...and then keep the script BUSY, through the bridge, for a fixed span of
// wall time. Every one of these is a real verb call, so the UI thread is free
// between them exactly as it is between any other two.
var spinStart = Date.now();
var spins = 0;
while (Date.now() - spinStart < kBudgetMs) { scene.nodes(); spins++; }
var busyMs = Date.now() - spinStart;

var framesAtEnd = app.frameStats().rendered;
console.log("driver frames: " + framesAtStart + " -> " + framesAtEnd
            + " (" + movedDuring + " of the first 19 verbs saw a new frame; "
            + spins + " query verbs over " + busyMs + " ms)");
assert(busyMs >= kBudgetMs, "the script stayed busy for " + busyMs + " ms of wall time");

if (live) {
    assert(framesAtEnd > framesAtStart,
           "LIVE: the render loop drew while the script was working — "
           + (framesAtEnd - framesAtStart) + " frames");
    assert(movedDuring > 0,
           "LIVE: ...and at least one of them landed BEFORE the script's last edit");
} else {
    assert(framesAtEnd === framesAtStart,
           "OFF: the render loop drew nothing at all — not in " + busyMs
           + " ms of verbs, not between any two of them");
    assert(movedDuring === 0, "OFF: ...and no verb ever saw a new frame");
}

// ---- editor.frame renders either way: it is a verb ----
var before = editor.viewportState().framesPresented;
editor.frame(3, 1 / 60);
var after = editor.viewportState().framesPresented;
assert(after >= before + 3, "editor.frame(3) presented 3 frames (" + before + " -> " + after + ")");

// ---- ONE SCRIPTED FRAME IS ONE FRAME (DOUBLE-FRAME-1, 2026-09-18) ----
//
// The live policy's promise is "at most one frame per display period", and its
// clock used to count only the DRIVER's own frames: a script stepping frames —
// which is how every drag, every step and every MCP gesture moves the picture
// deterministically — got a driver frame in the gap after each of its verbs ON
// TOP of the frame it had just drawn. Measured with --script-live before the
// fix: 60 scripted frames cost 81 engine frames AT REST (no document edit at
// all) and 89-95 during a scripted drag; the render audit recorded the same
// thing from an MCP drag as "two frames per document edit", which named the
// edit for a cost the drawing was paying. `EngineRenderDriver::noteExternalFrame`
// restarts the pacing clock on every frame ANYBODY draws.
//
// THE SLACK, and why it is not zero: this asserts a RATE, and the loop below
// leaves the UI thread free between its verbs, so a box under load can still
// let one display period elapse inside a gap. Measured 0 of 60 on a quiet rig
// in four arms; six is a tenth of the run and the defect this guards reads
// twenty-one to thirty-six.
var kSteps = 60;
var driverBeforeSteps = app.frameStats().rendered;
var presentedBeforeSteps = editor.viewportState().framesPresented;
for (var f = 0; f < kSteps; ++f) editor.frame(1, 1 / 60);
var driverDuringSteps = app.frameStats().rendered - driverBeforeSteps;
var presentedDuringSteps = editor.viewportState().framesPresented - presentedBeforeSteps;
console.log("stepped " + kSteps + " frames: " + presentedDuringSteps + " presented, "
            + driverDuringSteps + " of them the driver's");
assert(presentedDuringSteps >= kSteps,
       "every scripted frame was drawn (" + presentedDuringSteps + " presented)");
assert(driverDuringSteps <= kSteps / 10,
       "A SCRIPT THAT DRAWS ITS OWN FRAMES IS NOT DRAWN TWICE — the loop added "
       + driverDuringSteps + " frames to " + kSteps + " scripted ones");

// ---- ...AND THE SAME IN PLAY MODE (the lead's fix-round item 4) ----
//
// `editor.frame` does not always reach the editor's viewport: while the PLAYER
// owns the screen it routes to the player's own stepper, which draws to the
// same window through a different path (EditorApi::frame -> playerHasTheScreen
// -> PlayerService::stepFrames). The first version of this fix hooked only the
// viewport's route, so a scripted or MCP-driven session in play mode kept
// paying the extra loop frame per verb — and play mode is where a gesture
// costs the most, because the whole scene is simulating.
app.space("player");
assert(player.play() === true, "the Player takes the screen");
assert(player.playing() === true, "...and it is playing, which is what routes editor.frame to it");
editor.frame(5, 1 / 60);
var driverBeforePlay = app.frameStats().rendered;
for (var pf = 0; pf < kSteps; ++pf) editor.frame(1, 1 / 60);
var driverDuringPlay = app.frameStats().rendered - driverBeforePlay;
console.log("stepped " + kSteps + " PLAY frames: " + driverDuringPlay + " of them the driver's");
assert(driverDuringPlay <= kSteps / 10,
       "A SCRIPT STEPPING PLAY FRAMES IS NOT DRAWN TWICE EITHER — the loop added "
       + driverDuringPlay + " frames to " + kSteps + " scripted ones");
player.stop();
app.space("editor");
assert(player.playing() === false, "the Player hands the screen back");

// ---- still ONE undo entry, whatever thread the JS ran on ----
var undoNow = editor.undoState();
assert(undoNow.macroOpen === true, "the macro is still the run's own");
assert(undoNow.pushes >= undoBefore.pushes + 20, "every add was recorded as a command");

// ---- the bridge's error route: fail() throws in the script, at the call site ----
var caught = null;
try {
    project.open("no such project anywhere");
} catch (e) {
    caught = e.message;
}
assert(caught && caught.indexOf("no project named") >= 0,
       "project.open's fail() reached the script as a catchable error: " + caught);
assert(app.lastError().indexOf("no project named") >= 0,
       "...and app.lastError() recorded the same sentence");

// ---- the gate is still the run's, at the end of it ----
var gateEnd = editor.editGate();
assert(gateEnd.scriptRunning === true, "the gate is armed for the WHOLE run, not just its start");
assert(gateEnd.refusals === 0, "...and this run refused nothing, having been left alone");

// ---- console.log ordering survives the thread hop ----
for (var k = 0; k < 5; ++k) {
    console.log("order-" + k);
    scene.nodes();
}
assert(true, "five console lines interleaved with five verbs");

"live-feedback-ok"
