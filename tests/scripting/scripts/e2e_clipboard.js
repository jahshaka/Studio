// scripting.e2e.clipboard — THE CLIPBOARD, through the verbs
// (SPECS/CLIPBOARD_SPEC.md P0, gate table §7).
//
// What this proves, none of which the old in-app clipboard could do:
//
//   1. THE PAYLOAD IS TEXT, self-identifying, and it is on the SYSTEM
//      clipboard. `clipboard.text()` hands back the exact bytes another
//      instance (or a text editor, or a chat window) would receive, and the
//      first bytes say what it is.
//   2. PASTE IS A COPY: fresh guids, the Unreal naming rule (Cube -> Cube2),
//      placement beside the primary, and the pasted objects become the
//      selection.
//   3. CUT is copy + a delete that is one undo step, and the clipboard still
//      holds the objects afterwards.
//   4. TOLERANCE: an item kind this build has no paste for is SKIPPED with a
//      reason, never a refusal of the whole payload; junk on the clipboard is
//      refused with a reason and pastes nothing.
//   5. The three older `editor.*` verbs are aliases onto the SAME clipboard.
//
// Document verbs only -> --headless (offscreen platform, NULL render system,
// no display). Throwing exits non-zero.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function J(x) { return JSON.stringify(x); }
function allNodes() { return scene.nodes(); }
function exists(id) {
    return allNodes().some(function (n) { return n.id === id; });
}
function rootChildren() {
    var root = scene.root();
    return allNodes().filter(function (n) { return n.parent === root; })
                     .map(function (n) { return n.id; });
}
function nameOf(id) { return node.info(id).name; }

var projectGuid = project.create("Clipboard " + Date.now());
assert(projectGuid.length > 10, "project created");

// ---- 1. copy: the payload is text, and it is on the system clipboard -------
var cube = scene.addPrimitive("cube");
var sphere = scene.addPrimitive("sphere");
editor.select([cube, sphere]);
var copied = clipboard.copy();
console.log("copy -> " + J(copied));
assert(copied.items === 2, "two objects copied");
assert(copied.assets === 0, "a builtin primitive references no library asset, so the closure " +
                            "is empty (its mesh is a ':' builtin, not a guid)");

var text = clipboard.text();
assert(text.indexOf('{"format":"jahshaka.clipboard","version":1') === 0,
       "the payload LEADS with its format marker — a human pasting it into a text editor sees " +
       "what it is");
var payload = JSON.parse(text);
assert(payload.items.length === 2 && payload.items[0].kind === "node",
       "the payload parses as ordinary JSON and carries two node items");
assert(payload.sceneFormat === 2, "it records the node-object format version it was written in");
assert(payload.app.length > 0, "and the build that wrote it");
assert(payload.items[0].node.name === "Cube", "the node object travels whole");
assert(typeof payload.items[0].index === "number", "with the anchor a paste-in-place would need");

var described = clipboard.contents();
assert(described.items.length === 2, "contents() describes the items");
assert(described.items[0].name === "Cube", "by name");
assert(described.assets.total === 0, "and reports the closure size");
assert(described.text === undefined, "contents() never carries the payload itself");

// ---- 2. paste: fresh guids, the naming rule, placement --------------------
editor.select(sphere);                       // the primary decides placement
var pasted = clipboard.paste();
console.log("paste -> " + J(pasted));
assert(pasted.pasted.length === 2, "two objects pasted");
assert(pasted.missing.length === 0 && pasted.skipped.length === 0, "nothing missing, nothing skipped");
assert(pasted.pasted.indexOf(cube) < 0 && pasted.pasted.indexOf(sphere) < 0,
       "the pasted nodes have FRESH guids — a paste is a copy, not a second reference");
var names = pasted.pasted.map(nameOf);
assert(names.indexOf("Cube2") >= 0 && names.indexOf("Sphere2") >= 0,
       "the Unreal naming rule applied on paste: " + J(names));
var order = rootChildren();
assert(order.indexOf(pasted.pasted[0]) === order.indexOf(sphere) + 1,
       "the first pasted object landed right AFTER the primary (D7)");
assert(editor.selectionSet().length === 2 &&
       editor.selectionSet().indexOf(pasted.pasted[0]) >= 0,
       "the pasted objects became the selection");

// The clipboard SURVIVES the paste — pasting twice gives two copies.
var again = clipboard.paste();
assert(again.pasted.length === 2, "the payload is still there: a second paste made two more");
var thirdNames = again.pasted.map(nameOf);
assert(thirdNames.indexOf("Cube3") >= 0, "and the naming rule kept counting: " + J(thirdNames));

// ---- 3. explicit ids and explicit placement -------------------------------
var target = scene.addEmpty();
var one = clipboard.copy([cube]);
assert(one.items === 1, "copy takes explicit ids, not just the selection");
var intoTarget = clipboard.paste({ parent: target });
assert(intoTarget.pasted.length === 1, "pasted into an explicit parent");
assert(node.info(intoTarget.pasted[0]).parent === target, "and it landed there");

// ---- 4. cut ---------------------------------------------------------------
var doomed = scene.addPrimitive("cone");
editor.select(doomed);
var cutResult = clipboard.cut();
console.log("cut -> " + J(cutResult));
assert(cutResult.items === 1, "one object cut");
assert(cutResult.removed.length === 1 && cutResult.removed[0] === doomed, "and removed");
assert(!exists(doomed), "the node left the document");
var pastedBack = clipboard.paste();
assert(pastedBack.pasted.length === 1, "a cut object is still on the clipboard and pastes back");
assert(nameOf(pastedBack.pasted[0]).indexOf("Cone") === 0,
       "as the same thing: " + nameOf(pastedBack.pasted[0]));

// ---- 5. the editor.* aliases share this one clipboard ---------------------
editor.select([cube]);
assert(editor.copy() === 1, "editor.copy still answers with a count");
var aliasText = clipboard.text();
assert(JSON.parse(aliasText).items.length === 1,
       "and it wrote the SAME system clipboard — one clipboard, three names");
var aliasFragments = editor.clipboard();
assert(aliasFragments.length === 1 && aliasFragments[0].format === "jahshaka.scene",
       "editor.clipboard still reports the fragment shape node.serialize returns");
assert(aliasFragments[0].node.name === "Cube", "with the node object in it");
var aliasPasted = editor.paste();
assert(aliasPasted.length === 1 && !!nameOf(aliasPasted[0]), "editor.paste pastes the same payload");

// ---- 6. tolerance: a kind this build cannot paste -------------------------
var graphEnvelope = {
    format: "jahshaka.clipboard", version: 1, app: "0.9.1b",
    items: [{ kind: "graph", graph: { jahshaka_nodes: true, nodes: [{ id: 1 }], connections: [] } }]
};
var accepted = clipboard.setText(JSON.stringify(graphEnvelope));
assert(accepted.valid === true && accepted.items === 1, "a graph payload is VALID: " + J(accepted));
var refused = clipboard.paste();
assert(refused.pasted.length === 0, "but the editor pastes none of it");
assert(refused.skipped.length === 1 && refused.skipped[0].kind === "graph",
       "it is reported as skipped, with a reason: " + J(refused.skipped));

// ---- 7. junk on the clipboard --------------------------------------------
var junk = clipboard.setText("Dear Bob, here is that scene.");
assert(junk.valid === false && junk.error.length > 0,
       "prose is refused with a reason, not an exception: " + J(junk));
assert(app.lastError().indexOf("clipboard.setText") >= 0, "and the reason is readable afterwards");
// The refusal left the previous payload alone.
assert(JSON.parse(clipboard.text()).items[0].kind === "graph",
       "a refused setText did NOT clobber what the clipboard held");

// ---- 8. a paste whose target does not want these items --------------------
editor.select([cube]);
clipboard.copy();
var wrongTarget = clipboard.paste({ target: "assets" });
assert(wrongTarget.pasted.length === 0 && wrongTarget.skipped.length === 1,
       "scene objects are not pasted into the Assets page: " + J(wrongTarget.skipped));

// ---- 9. copying nothing keeps what you copied a moment ago ----------------
editor.selectNone();
var empty = clipboard.copy();
assert(empty.items === 0, "copying nothing returns 0 — a refusal, not an exception");
assert(JSON.parse(clipboard.text()).items.length === 1,
       "and the previous payload survived it");

console.log("scripting.e2e.clipboard: OK");
