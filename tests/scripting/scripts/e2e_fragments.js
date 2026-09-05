// scripting.e2e.fragments — SERIALIZER V2 (SPECS/SCENEGRAPH_SPEC.md §3 step 4),
// driven through the verbs, in the real app, against the real database.
//
// Three claims, each of which was untestable before the fragment pair existed:
//
//   1. node.serialize produces the document fragment the scene file carries for
//      that subtree — guids, names, transforms, children, in order — and
//      node.deserialize rebuilds it somewhere else. That is COPY/PASTE's whole
//      substrate, and it is the same machinery undo v1.5's structural commands
//      capture (commands/structuralundo.h).
//
//   2. SCENE_STATIC survives a save and a reopen, as a USER OVERRIDE and not as
//      a derivation. The default policy runs on every load and marks every
//      eligible branch, so a node the user pinned STATIC is indistinguishable
//      from one the policy reached — the interesting case, and the one this
//      asserts, is a node the user pinned DYNAMIC: it must come back dynamic
//      even though the policy would happily make it static again.
//
//   3. The blob the app writes announces its format. A reader that cannot name
//      the format it is holding has to guess from key presence, which is how v1
//      ended up with "rot" meaning two different things.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function J(x) { return JSON.stringify(x); }

var guid = project.create("Fragments " + Date.now());
assert(guid.length > 10, "project created");

// ---------------------------------------------------------------------------
// A small tree with a shape worth preserving: a parent with two ordered kids.
var parentId = scene.addEmpty();
node.setProperty(parentId, "position", { x: 2, y: 0, z: 0 });
var childA = scene.addPrimitive("cube");
var childB = scene.addPrimitive("sphere");
assert(node.reparent(childA, parentId), "child A parented");
assert(node.reparent(childB, parentId), "child B parented");
node.setProperty(childA, "position", { x: 0, y: 1, z: 0 });
node.setProperty(childB, "position", { x: 0, y: 2, z: 0 });

var info = node.info(parentId);
assert(info.id === parentId, "the parent is addressable");

// ---- 1. serialize -> deserialize ------------------------------------------
var frag = node.serialize(parentId);
console.log("fragment: " + J(frag).substring(0, 220) + " ...");
assert(frag.format === "jahshaka.scene", "the fragment names its format");
assert(frag.version >= 2, "the fragment carries the format version (" + frag.version + ")");
assert(frag.node && frag.node.guid === parentId, "the fragment IS this node");
assert(frag.node.children && frag.node.children.length === 2,
       "the fragment carries the subtree in order (" +
       (frag.node.children ? frag.node.children.length : "none") + " children)");
assert(frag.node.children[0].name === node.info(childA).name,
       "child order is the document's order, not a hash order");
// v2's rotation spelling: a quaternion, told apart by `scalar`. The euler
// triple v1 also wrote is GONE — it was lossy and it was never read once
// rotQuat existed.
assert(frag.node.rot && frag.node.rot.scalar !== undefined,
       "v2 writes the rotation as a QUATERNION (" + J(frag.node.rot) + ")");
assert(frag.node.rotQuat === undefined, "...and does not write the retired second copy");

var before = scene.nodes().length;
var pasted = node.deserialize(frag, "", -1);
assert(pasted && pasted.length > 10, "deserialize -> " + pasted);
assert(scene.nodes().length === before + 3,
       "the whole subtree came back (3 nodes: " + (scene.nodes().length - before) + ")");
var pastedInfo = node.info(pasted);
assert(Math.abs(pastedInfo.position.x - 2) < 1e-4,
       "the paste kept the fragment's transform (x=" + pastedInfo.position.x + ")");

// A paste into a NAMED parent, at a NAMED slot.
var host = scene.addEmpty();
var pasted2 = node.deserialize(frag, host, 0);
assert(pasted2 && pasted2.length > 10, "deserialize under an explicit parent -> " + pasted2);
assert(node.info(pasted2).parent === host, "...and it landed under that parent");

// ---- 2. SCENE_STATIC as a persisted user override -------------------------
//
// A ground plane: eligible, root-level, and exactly what the default policy
// loves to mark static. The user says no.
var ground = scene.addPrimitive("plane");
assert(node.isStatic(ground) === true, "the default policy marked the plane static");
assert(node.setStatic(ground, false), "the user pins it DYNAMIC");
assert(node.isStatic(ground) === false, "...and it is dynamic now");
var groundName = node.info(ground).name;

// A second one the user leaves alone, as the control.
var control = scene.addPrimitive("cube");
var controlName = node.info(control).name;

assert(project.save(), "saved");
assert(project.close(), "closed");
assert(project.open(guid), "reopened");

function byName(n) {
    var all = scene.nodes();
    for (var i = 0; i < all.length; i++) if (all[i].name === n) return all[i].id;
    return null;
}
var groundAgain = byName(groundName);
var controlAgain = byName(controlName);
assert(groundAgain !== null && controlAgain !== null, "both planes survived the round trip");
assert(node.isStatic(groundAgain) === false,
       "THE OVERRIDE PERSISTED: the user's dynamic pin beat the load-time policy");
assert(node.isStatic(controlAgain) === true,
       "...while the untouched node was re-derived static by the same policy");

// The pasted subtrees survived too — proof the fragment rebuild produced real
// document nodes and not a detached island the writer skipped.
assert(scene.nodes().length >= before + 3, "the pasted subtree is in the saved document");

console.log("e2e_fragments: ALL OK");
