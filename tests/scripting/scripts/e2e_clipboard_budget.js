// scripting.e2e.clipboard_budget's script (clipboard_budget.sh prepends `var MODE = "...";`):
// the copy's REFUSALS, through the verbs and the setting the app reads (clipboard/inlineLimitBytes,
// seeded into this run's jahsettings.ini by the shell half — never a test-only verb).
//   MODE "blob":  the budget is 64 KB; a model whose node tree is larger refuses clipboard.copy BY NAME,
//                 the clipboard text is unchanged, and a refused clipboard.cut deletes nothing.
//   MODE "floor": the budget is set under its 64 KB minimum; any copy refuses with that reason.
// The verbs REFUSE (items 0 + app.lastError), they do not throw: that is their documented contract.
function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
project.create("clipboard budget " + Date.now());
var cube = scene.addPrimitive("cube", { position: [0, 0.5, 0] });
if (MODE === "floor") {
    var f = clipboard.copy([cube]);
    assert(!f.items, "a copy under a budget below the floor copies nothing (" + JSON.stringify(f) + ")");
    assert(app.lastError().indexOf("64 KB minimum") >= 0, "...and says why: " + app.lastError());
    console.log("CLIPBOARD_BUDGET floor PASS");
} else {
    var ok = clipboard.copy([cube]);
    assert(ok.items === 1, "the small cube copies under the 64 KB budget");
    var before = clipboard.text();
    var placed = assets.importAndPlace(MODEL);
    assert(placed && placed.nodeId, "the many-part model is placed: " + JSON.stringify(placed));
    var r = clipboard.copy([placed.nodeId]);
    assert(!r.items, "the model whose node tree is over the budget is NOT copied (" + JSON.stringify(r) + ")");
    assert(app.lastError().indexOf("budget_parts") >= 0 && app.lastError().indexOf("does not fit") >= 0,
           "the refusal names the row: " + app.lastError());
    assert(clipboard.text() === before, "the clipboard still holds the previous copy, byte for byte");
    var count = scene.nodes().length;
    var c = clipboard.cut([placed.nodeId]);
    assert(!c.items && (!c.removed || c.removed.length === 0), "a refused CUT removes nothing (" + JSON.stringify(c) + ")");
    assert(scene.nodes().length === count, "the node count is unchanged (" + count + ")");
    assert(clipboard.text() === before, "...and the clipboard is still the previous copy");
    console.log("CLIPBOARD_BUDGET blob PASS");
}
