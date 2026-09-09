// scripting.e2e.refusals — A REFUSAL IS AN ANSWER, NOT AN EXCEPTION
// (hygiene lane, 2026-09-09).
//
// The registry's documented return type is the contract. A verb that says
// `-> bool` answers false, `-> n` answers 0 and `-> id | null` answers null
// when the answer is "no" — it does not throw, because a throw aborts the
// caller's whole run over a question it deliberately asked. The reason is not
// lost: app.lastError() carries it.
//
// Three verbs were wrong (and app.input_keys had to route around the
// first of them): node.info() raised for a node that was gone,
// editor.selection() answered `undefined` where it documents `null`, and
// editor.copy() raised on an empty selection although its own doc says copying
// nothing is fine. Document verbs only -> --headless.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function throws(fn, what) {
    var threw = false;
    try { fn(); } catch (e) { threw = true; }
    assert(threw, what);
}

project.create("Refusals " + Date.now());

// ---- node.info on a node that is GONE ---------------------------------------
var cube = scene.addPrimitive("cube");
assert(node.info(cube).id === cube, "node.info reads a live node");
assert(node.remove(cube) === true, "the node is removed");

var info = node.info(cube);            // must NOT throw: this is the whole point
assert(info === null, "node.info on a deleted node is null, not an exception (got " + info + ")");
assert(String(app.lastError()).indexOf("no node with id") >= 0,
       "app.lastError explains the refusal: " + app.lastError());

// A stale id that never existed answers the same way.
assert(node.info("not-a-guid") === null, "node.info on an unknown id is null");

// ...and the sequence a delete-then-check script actually writes now runs to
// the end instead of aborting on the first missing node.
var live = scene.addPrimitive("cube");
var ids = [cube, live, "not-a-guid"];
var found = 0;
for (var i = 0; i < ids.length; i++) if (node.info(ids[i])) found++;
assert(found === 1, "a walk over stale ids completes and finds exactly the live one");

// ---- editor.selection() is NULL, not undefined ------------------------------
editor.select(live);
assert(editor.selection() === live, "the selection round-trips");
editor.select(null);
var sel = editor.selection();
assert(sel === null, "editor.selection() with nothing selected is null (got " + typeof sel + ")");

// ---- editor.copy() with an empty selection ----------------------------------
var n = editor.copy();                 // must NOT throw
assert(n === 0, "editor.copy() with nothing selected returns 0 (got " + n + ")");
assert(String(app.lastError()).indexOf("nothing is selected") >= 0,
       "app.lastError explains it: " + app.lastError());
editor.select(live);
assert(editor.copy() === 1, "and copying a real selection still reports 1");

// ---- the other half: real MISUSE still throws -------------------------------
// The correction is about refusals, not about swallowing errors. A verb asked
// to CHANGE something that does not exist is still an error.
throws(function () { node.setProperty("not-a-guid", "name", "x"); },
       "node.setProperty on an unknown id still throws");
throws(function () { app.space("no-such-space"); }, "an unknown space still throws");

console.log("refusals: all assertions passed");
