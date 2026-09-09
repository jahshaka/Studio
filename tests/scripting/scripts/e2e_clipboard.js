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
//   6. NODE-GUID REFERENCES inside a copied subtree follow the copy: a tracking
//      camera's focus target and a physics constraint's endpoints name the
//      COPIES (iris::SceneNode::remapNodeReferences — the recorded gap, which
//      was open on Duplicate too).
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

// ---- 6. NODE-GUID REFERENCES FOLLOW THE COPY ------------------------------
//
// iris::SceneNode::remapNodeReferences, live: a pasted subtree's internal
// references must point at the COPIES, never at the originals. Socket owners
// were remapped before this lane; a camera's focus target and a physics
// constraint's endpoints were the recorded gap, open on Duplicate as well —
// a duplicated pair of constrained bodies stayed bolted to the originals and a
// duplicated tracking camera kept watching the original subject.

// (a) a camera that TRACKS a node
var subject = scene.addPrimitive("cube");
var tracker = scene.addCamera();
assert(camera.settings(tracker, { focusMode: "track", focusTarget: subject }),
       "the camera tracks the cube");
assert(camera.settings(tracker).focusTarget === subject, "focusTarget is set");
editor.select([subject, tracker]);
clipboard.copy();
var trackPaste = clipboard.paste();
assert(trackPaste.pasted.length === 2, "both pasted");
// camera.settings THROWS on a non-camera (it is misuse, not a refusal), so the
// copies are told apart by the camera LIST rather than by asking each one.
var cameraIds = scene.cameras().map(function (c) { return c.id || c; });
var pastedCam = null, pastedSubject = null;
for (var i = 0; i < trackPaste.pasted.length; ++i) {
    if (cameraIds.indexOf(trackPaste.pasted[i]) >= 0) pastedCam = trackPaste.pasted[i];
    else pastedSubject = trackPaste.pasted[i];
}
assert(!!pastedCam && !!pastedSubject, "the copies are a camera and a cube");
assert(camera.settings(pastedCam).focusTarget === pastedSubject,
       "the PASTED camera tracks the PASTED cube, not the original: " +
       camera.settings(pastedCam).focusTarget + " vs original " + subject);

// (b) a physics constraint between two bodies. Constraints have no verb yet
// (node.physicsInfo reports only their count), so the pair is authored the one
// way a text clipboard makes possible: by editing the payload. That is a fair
// test of the remap — the reader, the fresh-guid pass and the writer are all
// the real ones.
var bodyA = scene.addPrimitive("cube");
var bodyB = scene.addPrimitive("sphere");
assert(node.physics(bodyA, { type: "rigidbody", shape: "cube" }), "body A is a rigid body");
assert(node.physics(bodyB, { type: "rigidbody", shape: "sphere" }), "body B is a rigid body");
editor.select([bodyA, bodyB]);
clipboard.copy();
var rigged = JSON.parse(clipboard.text());
var itemA = null;
for (var n = 0; n < rigged.items.length; ++n)
    if (rigged.items[n].node.guid === bodyA) itemA = rigged.items[n];
assert(!!itemA, "the payload carries body A's node object");
assert(!!itemA.node.physicsProperties, "with its physics block: " + J(Object.keys(itemA.node)));
itemA.node.physicsProperties.constraints = [
    { constraintFrom: bodyA, constraintTo: bodyB, constraintType: 1 }
];
assert(clipboard.setText(JSON.stringify(rigged)).valid, "the rewritten payload is accepted");

var riggedPaste = clipboard.paste();
assert(riggedPaste.pasted.length === 2, "both bodies pasted");
var pastedA = null, pastedB = null;
for (var b = 0; b < riggedPaste.pasted.length; ++b) {
    var info = node.serialize(riggedPaste.pasted[b]);
    if (info.node.physicsProperties && info.node.physicsProperties.constraints &&
        info.node.physicsProperties.constraints.length === 1) pastedA = riggedPaste.pasted[b];
    else pastedB = riggedPaste.pasted[b];
}
assert(!!pastedA && !!pastedB, "one copy carries the constraint");
var constraint = node.serialize(pastedA).node.physicsProperties.constraints[0];
console.log("pasted constraint -> " + J(constraint));
assert(constraint.constraintFrom === pastedA,
       "the constraint's FROM end names the copy, not the original (" +
       constraint.constraintFrom + " vs " + bodyA + ")");
assert(constraint.constraintTo === pastedB,
       "and its TO end names the other copy (" + constraint.constraintTo + " vs " + bodyB + ")");

// The clipboard is put back the way section 7 expects it.
editor.select([cube]);
clipboard.copy();

// ---- 7. tolerance: a kind this build cannot paste -------------------------
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

// ---- 8. junk on the clipboard --------------------------------------------
var junk = clipboard.setText("Dear Bob, here is that scene.");
assert(junk.valid === false && junk.error.length > 0,
       "prose is refused with a reason, not an exception: " + J(junk));
assert(app.lastError().indexOf("clipboard.setText") >= 0, "and the reason is readable afterwards");
// The refusal left the previous payload alone.
assert(JSON.parse(clipboard.text()).items[0].kind === "graph",
       "a refused setText did NOT clobber what the clipboard held");

// ---- 9. a paste whose target does not want these items --------------------
editor.select([cube]);
clipboard.copy();
var wrongTarget = clipboard.paste({ target: "assets" });
assert(wrongTarget.pasted.length === 0 && wrongTarget.skipped.length === 1,
       "scene objects are not pasted into the Assets page: " + J(wrongTarget.skipped));

// ---- 10. copying nothing keeps what you copied a moment ago ---------------
editor.selectNone();
var empty = clipboard.copy();
assert(empty.items === 0, "copying nothing returns 0 — a refusal, not an exception");
assert(JSON.parse(clipboard.text()).items.length === 1,
       "and the previous payload survived it");

console.log("scripting.e2e.clipboard: OK");
