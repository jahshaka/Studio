// scripting.e2e.static_semantics — the verb surface's SCENE_STATIC contract
// plus the document raycast verb. Born from the 2026-09-05 scripting audit:
// F1 (scripted adds with options lost the static default — every MCP-built
// scene was fully dynamic), F7 (reparent demoted a static subtree through the
// keep-world-pose write), F2 (reparenting the active camera cleared it),
// F12 (isStatic answered the ask, not the outcome), F4 (picking had no verb,
// and the document fallback disagreed with the engine about hidden nodes).
// Document verbs only -> --headless (NULL render system, no display).
//
// NOT asserted here (wave-2 territory, add when undo v1.5 lands): undo of a
// transform restores the static hint; undo of a reparent restores the sibling
// index. See the audit's F3.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function near(a, b) { return Math.abs(a - b) < 1e-3; }

project.create("Static Semantics " + Date.now());

// ---- F1: the static default survives creation options ----
var plain = scene.addPrimitive("cube");
assert(node.isStatic(plain) === true, "a bare add is static by default");

var placed = scene.addPrimitive("cube", { position: { x: 3, y: 0, z: 0 } });
assert(node.isStatic(placed) === true,
       "an add WITH a position is still static (create-at is placement, not a move)");
var t = node.transform(placed);
assert(near(t.position.x, 3), "the position option was applied (x=3)");

var group = scene.addEmpty({});
var parented = scene.addPrimitive("cube", { parent: group, position: { x: 1, y: 0, z: 0 } });
assert(node.isStatic(group) === true, "the empty parent is static");
assert(node.isStatic(parented) === true, "an add with {parent, position} is static");

// ---- rule 4 still fires for a REAL move ----
node.transform(placed, { position: { x: 5, y: 0, z: 0 } });
assert(node.isStatic(placed) === false, "a transform write demotes (rule 4)");
assert(node.setStatic(placed, true) === true, "an explicit re-mark works");
assert(node.isStatic(placed) === true, "…and reads back true");

// ---- F7: reparent preserves the static hint and the world pose ----
node.reparent(placed, group);
assert(node.isStatic(placed) === true,
       "reparent under a static parent keeps the node static (world pose unchanged is not a move)");
t = node.transform(placed);
assert(near(t.position.x, 5), "world position survived the reparent (x=5)");

// ---- F12: isStatic is the OUTCOME ----
// A child born under a static parent inherits static physically; the verb
// answers what the graph did, not what was asked by name.
var child = scene.addPrimitive("sphere", { parent: group });
assert(node.isStatic(child) === true, "a child under a static parent reads static (inheritance)");
// A light can never be static; the outcome stays false and setStatic refuses.
var light = scene.addLight("point", {});
assert(node.isStatic(light) === false, "a light is never static");

// ---- F2: the active camera survives a reparent ----
var cam = scene.addCamera({});
assert(scene.setActiveCamera(cam) === true, "camera set active");
var camCarrier = scene.addEmpty({});
node.reparent(cam, camCarrier);
var active = scene.activeCamera();
assert(active === cam, "activeCamera survives node.reparent (was: silently cleared)");

// ---- F4: scene.raycast ----
// The teapot from earlier sits at x=5 under `group`; put a fresh cube at a
// known spot and shoot at it.
var target = scene.addPrimitive("cube", { position: { x: 0, y: 0.5, z: 0 } });
var hits = scene.raycast({ x: 0, y: 0.5, z: -10 }, { x: 0, y: 0, z: 1 });
assert(hits.length >= 1, "raycast returns hits");
assert(hits[0].id === target, "nearest hit is the cube in front of the ray");
assert(hits[0].triangleIndex >= 0, "hit carries a triangle index");
assert(hits[0].distance > 0 && hits[0].distance < 10.5, "distance is sane");
assert(typeof hits[0].point.z === "number", "hit point is {x,y,z}");

// Hidden nodes are never hit — same answer as the engine broad phase.
node.setProperty(target, "visible", false);
var hitsHidden = scene.raycast({ x: 0, y: 0.5, z: -10 }, { x: 0, y: 0, z: 1 });
for (var i = 0; i < hitsHidden.length; i++)
    assert(hitsHidden[i].id !== target, "a hidden node is not hit");
node.setProperty(target, "visible", true);

// pickable=false honored, includeUnpickable overrides.
node.setProperty(target, "pickable", false);
var hitsUnpick = scene.raycast({ x: 0, y: 0.5, z: -10 }, { x: 0, y: 0, z: 1 });
for (var j = 0; j < hitsUnpick.length; j++)
    assert(hitsUnpick[j].id !== target, "an unpickable node is not hit by default");
var hitsForced = scene.raycast({ x: 0, y: 0.5, z: -10 }, { x: 0, y: 0, z: 1 },
                               { includeUnpickable: true });
var found = false;
for (var k = 0; k < hitsForced.length; k++) if (hitsForced[k].id === target) found = true;
assert(found, "includeUnpickable reaches it");

// A miss is an empty array, not an error.
var misses = scene.raycast({ x: 0, y: 500, z: -10 }, { x: 0, y: 0, z: 1 });
assert(misses.length === 0, "a clean miss returns []");

// ---- F4 (2026-09-06 verb-coverage audit): every hit carries its PICK ROOT ----
// The verb's doc promised "the semantics the viewport's click uses" while
// reporting the raw hit node: a click resolves the ATTACHED chain and selects
// the whole asset, so a scripted raycast + select landed on a sub-mesh where a
// user's click never does. `rootId` is that answer, from the picker's own rule.
node.setProperty(target, "pickable", true);
var plain = scene.raycast({ x: 0, y: 0.5, z: -10 }, { x: 0, y: 0, z: 1 });
assert(plain.length >= 1, "raycast hits the target again");
assert(plain[0].rootId === plain[0].id,
       "an unattached node is its own pick root (rootId === id)");

// The imported-asset shape: a part ATTACHED to its parent. A click there
// selects the parent, so rootId must climb the attached chain — through TWO
// levels, because assets nest.
var assetRoot = scene.addEmpty({ position: { x: 20, y: 0, z: 0 } });
// Explicit local zero: an empty added with NO position is dropped in front of
// the camera (a local offset that cancels its parent's), which would move the
// part off the ray.
var assetMid = scene.addEmpty({ parent: assetRoot, position: { x: 0, y: 0, z: 0 } });
var assetPart = scene.addPrimitive("cube", { parent: assetMid, position: { x: 0, y: 0.5, z: 0 } });
assert(node.attached(assetPart) === false, "a hand-built node is NOT attached by default");
assert(node.setAttached(assetPart, true) === true, "node.setAttached(part, true)");
assert(node.setAttached(assetMid, true) === true, "…and its parent group");
assert(node.attached(assetPart) === true, "node.attached reads back true");

var partHits = scene.raycast({ x: 20, y: 0.5, z: -10 }, { x: 0, y: 0, z: 1 });
assert(partHits.length >= 1, "the attached part is hit");
assert(partHits[0].id === assetPart, "…and `id` is still the part the ray hit");
assert(partHits[0].rootId === assetRoot,
       "rootId climbs the whole attached chain to the asset root (what a click selects)");

// Detaching makes the part its own object again — same rule, live.
assert(node.setAttached(assetPart, false) === true, "detach the part");
var detachedHits = scene.raycast({ x: 20, y: 0.5, z: -10 }, { x: 0, y: 0, z: 1 });
assert(detachedHits[0].rootId === assetPart, "a detached part is its own pick root");

// The flag is UNDOABLE like every other node flag. A script run IS one open
// macro, so undo cannot be driven from inside it (e2e_undo_macro documents
// that) — what IS observable is that the write PUSHED a command instead of
// editing the document behind the undo stack's back.
var pushesBefore = editor.undoState().pushes;
assert(node.setAttached(assetPart, true) === true, "re-attach (undo-record test)");
assert(editor.undoState().pushes === pushesBefore + 1,
       "node.setAttached pushed exactly one undo command");
assert(node.attached(assetPart) === true, "…and the flag is set");

var attachRefused = false;
try { node.setAttached("no-such-node", true); } catch (e) { attachRefused = true; }
assert(attachRefused, "node.setAttached refuses an unknown id");

// ---------------------------------------------------------------------------
// A LOADED SCENE CLASSIFIES LIKE A BUILT ONE (2026-09-06, perf wave B).
//
// THE DEFECT: a freshly opened Showroom reported ZERO static nodes out of 241,
// while the very same scene's Add-menu additions classified fine — every
// project any user has ever opened was fully dynamic, and the whole
// SCENE_STATIC program was doing nothing for loaded content.
//
// The reader's applyStaticDefaults pass was running and was not the problem;
// `isStaticEligible()` was. It refused any node carrying an Animation OBJECT,
// and the animation panel gives every node it is shown a channel-less
// `Animation` which the writer then persists (31 of Showroom's 32 animations
// had no properties and no skeletal clip at all). Rule 2 turned those misses
// into a wipe-out: an ineligible TOP-LEVEL node stays dynamic, and
// `canBeStatic` then refuses its entire subtree.
//
// Both open paths are the same four stages (MainWindow::openStage*), so this
// covers the async open too — the sync/async split was ruled out by
// measurement, not assumed.
project.close();
var reproName = "Static Reopen " + Date.now();
var reproGuid = project.create(reproName);

// Unique names, because the reopened document mints new ids and every
// addPrimitive would otherwise leave three nodes called "Cube".
function named(id, n) { assert(node.setProperty(id, "name", n) === true, "named " + n); return id; }

var rGround = named(scene.addPrimitive("ground"), "R_ground");
var rProp   = named(scene.addPrimitive("cube", { position: { x: 2, y: 0, z: 0 } }), "R_prop");
named(scene.addPrimitive("sphere", { parent: rProp }), "R_propChild");
// SHIPPED-CONTENT SHAPE: the animation panel leaves a channel-less "Animation"
// on every node it is shown, so a real world's top-level nodes carry one. Both
// of these did before the fix — and took the ground, the prop AND the prop's
// child down with them.
anim.create(rGround, "Animation");
anim.create(rProp, "Animation");
// The defect's shape: an animation with no channels at all. It drives nothing
// (updateAnimation writes a transform only through hasPropertyAnim), so it must
// not cost the node — or its subtree — its classification.
var rIdle = named(scene.addPrimitive("cube", { position: { x: 4, y: 0, z: 0 } }), "R_emptyAnim");
anim.create(rIdle, "Animation");
named(scene.addPrimitive("sphere", { parent: rIdle }), "R_emptyAnimChild");
// ...and the shape that genuinely moves: a real position track.
var rMoving = named(scene.addPrimitive("cube", { position: { x: 6, y: 0, z: 0 } }), "R_animated");
anim.create(rMoving, "Move");
assert(anim.keyframe(rMoving, "position", 0) === true, "keyed a real position track");
assert(anim.keyframe(rMoving, "position", 1) === true, "…and a second key");
// A USER decision, which the file DOES carry and the policy must never
// overrule (StaticOverride).
var rPinned = named(scene.addPrimitive("cube", { position: { x: 8, y: 0, z: 0 } }), "R_pinnedDynamic");
assert(node.setStatic(rPinned, false) === true, "pinned Dynamic by hand");

assert(project.save() === true, "repro project saved");
assert(project.close() === true, "repro project closed");
assert(project.open(reproGuid) === true, "repro project REOPENED");

function byName(n) {
    var all = scene.nodes();
    for (var i = 0; i < all.length; i++) if (all[i].name === n) return all[i].id;
    throw new Error("assert failed: no node named " + n + " after reopen");
}

assert(node.isStatic(byName("R_ground")) === true,
       "REOPENED: the ground reads static (was false — the whole defect)");
assert(node.isStatic(byName("R_prop")) === true, "REOPENED: a loaded prop reads static");
assert(node.isStatic(byName("R_propChild")) === true,
       "REOPENED: a child of a loaded prop reads static (rule 2 cascades DOWN, not out)");
assert(node.isStatic(byName("R_emptyAnim")) === true,
       "REOPENED: a node carrying a CHANNEL-LESS animation is still static");
assert(node.isStatic(byName("R_emptyAnimChild")) === true,
       "REOPENED: …and so is its child (rule 2 would have refused the whole branch)");
assert(node.isStatic(byName("R_animated")) === false,
       "REOPENED: a node with a real position track is NOT static");
assert(node.isStatic(byName("R_pinnedDynamic")) === false,
       "REOPENED: a user's Dynamic pin beats the policy");

console.log("static_semantics: all assertions passed");
