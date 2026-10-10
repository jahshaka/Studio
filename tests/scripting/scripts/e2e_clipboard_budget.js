// scripting.e2e.clipboard_budget's script (clipboard_budget.sh prepends the MODE and its inputs):
// the copy under the inline budget, through the verbs and the setting the app reads
// (clipboard/inlineLimitBytes, seeded into each run's jahsettings.ini by the shell half — never a
// test-only verb). THE RULE (STAGE-CLOSE-TRIAGE-1 round 8): a row's node tree is the row — never
// budgeted, it always travels; the budget bounds FILES, which inline while they fit and then go by oid.
//   MODE "copy":  library A, budget 64 KB: a 600-part model (node tree ~2.2 MB, file over 64 KB) COPIES
//                 — items 1, its file by reference; the payload and the source subtree's node count
//                 are printed for the shell half.
//   MODE "paste": library B (a fresh data root): the payload is put back with clipboard.setText and
//                 pasted; the pasted subtree has the SOURCE's node count (the tree travelled whole; the
//                 file was found by its oid in A's store, the payload's sibling store).
//   MODE "floor": budget 1,000 bytes (under the 64 KB floor): a copy is REFUSED with that reason
//                 (items 0 + app.lastError — the verbs refuse, they do not throw), and a refused CUT
//                 deletes nothing and leaves the clipboard as it was.
function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
project.create("clipboard budget " + MODE + " " + Date.now());
if (MODE === "copy") {
    var placed = assets.importAndPlace(MODEL);
    assert(placed && placed.nodeId, "the many-part model is placed");
    var count = scene.nodes({ subtree: placed.nodeId }).length;
    var r = clipboard.copy([placed.nodeId]);
    assert(r.items === 1, "the 600-part model COPIES under a 64 KB budget (" + JSON.stringify(r) + ")");
    assert(r.referenced >= 1, "...its file, over the budget, travels by reference (" + r.referenced + " referenced)");
    console.log("CLIPBOARD_SOURCE_COUNT " + count);
    console.log("CLIPBOARD_PAYLOAD " + clipboard.text());
    console.log("CLIPBOARD_BUDGET copy PASS");
} else if (MODE === "paste") {
    assert(clipboard.setText(JSON.stringify(PAYLOAD)).valid, "library B accepts library A's payload");
    var p = clipboard.paste({ target: "tree" });
    assert(p.pasted && p.pasted.length === 1 && (!p.missing || p.missing.length === 0),
           "the model pastes into a library that has never seen it (" + JSON.stringify(p.missing) + ")");
    var got = scene.nodes({ subtree: p.pasted[0] }).length;
    assert(got === SOURCE_COUNT, "the pasted subtree has the source's node count (" + got + " = " + SOURCE_COUNT + ")");
    console.log("CLIPBOARD_BUDGET paste PASS");
} else {
    var cube = scene.addPrimitive("cube", { position: [0, 0.5, 0] });
    var before = clipboard.text();
    var f = clipboard.copy([cube]);
    assert(!f.items, "a copy under a budget below the floor copies nothing (" + JSON.stringify(f) + ")");
    assert(app.lastError().indexOf("64 KB minimum") >= 0, "...and says why: " + app.lastError());
    assert(clipboard.text() === before, "the clipboard is as it was");
    var n = scene.nodes().length;
    var c = clipboard.cut([cube]);
    assert(!c.items && (!c.removed || c.removed.length === 0), "a refused CUT removes nothing (" + JSON.stringify(c) + ")");
    assert(scene.nodes().length === n, "the node count is unchanged (" + n + ")");
    assert(clipboard.text() === before, "...and the clipboard is still as it was");
    console.log("CLIPBOARD_BUDGET floor PASS");
}
