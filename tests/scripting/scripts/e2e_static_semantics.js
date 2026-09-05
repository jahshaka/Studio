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

console.log("static_semantics: all assertions passed");
