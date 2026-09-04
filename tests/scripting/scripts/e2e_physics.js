// scripting.e2e.physics — AI_SURFACE_PROGRAM_SPEC lane D #14.
//
// Physics was the last UI-with-no-verb domain: iris::PhysicsProperty carries
// mass/restitution/friction/damping/margin/shape/type, SceneWriter serializes
// all of it, and the ONLY writer was the Properties panel's physics section.
// node.physics / node.physicsInfo are the verbs; this is their gate.
//
// Four halves:
//   1. every scalar and both enums round-trip through the verb pair;
//   2. every refusal is LOUD (unknown key, unknown enum name, non-number,
//      geometry shape on a node with no geometry, static + a mass);
//   3. the values survive save -> close -> reopen (the scenewriter.cpp path);
//   4. THE BEHAVIOURAL ASSERTION: what the verb wrote actually reaches Bullet.
//      Two identical spheres, one made a rigid body and one made static by the
//      verb, simulated in the same world for the same frames — one falls, one
//      does not. A document round trip alone would pass even if the fields
//      never reached the simulation.
//
// ENGINE UP (no --headless): the physics stepper lives in the engine
// viewport's syncFrame, and the headless stand-in viewport's
// startPhysicsSimulation is an empty override. The UNDO half of the verb is
// asserted in mcp.e2e — a script run holds ONE open undo macro, and QUndoStack
// freezes its index while a macro is open (see scripting.e2e.undo_macro), so
// undo is only observable across two runs, which only the MCP path gives us.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function caught(fn) {
    try { fn(); } catch (e) { return "" + e; }
    return null;
}
function near(a, b, eps) { return Math.abs(a - b) < (eps || 1e-4); }
function nodeById(id) {
    var all = scene.nodes();
    for (var i = 0; i < all.length; ++i) if (all[i].id === id) return all[i];
    return null;
}

var proj = project.create("Physics Verbs " + Date.now());
assert(proj.length > 10, "project.create -> " + proj);

// ---- 1. defaults, then the full round trip ---------------------------------
var box = scene.addPrimitive("cube", { position: { x: 0, y: 3, z: 0 } });
assert(box.length > 10, "a cube -> " + box);

var fresh = node.physicsInfo(box);
assert(fresh.enabled === false, "a fresh node is NOT a physics body");
assert(fresh.type === "none", "type defaults to 'none'");
assert(fresh.shape === "none", "shape defaults to 'none'");
assert(near(fresh.mass, 1.0), "mass defaults to 1 (the struct's default)");
assert(fresh.constraints === 0, "no constraints");

assert(node.physics(box, {
    type: "rigidbody",
    shape: "convexhull",
    mass: 2.5,
    restitution: 0.75,
    friction: 0.25,
    damping: 0.125,
    collisionMargin: 0.05
}) === true, "node.physics writes the whole set");

var set = node.physicsInfo(box);
assert(set.enabled === true, "the node IS a physics body now (the isPhysicsBody flag)");
assert(set.type === "rigidbody", "type round-trips as a NAME");
assert(set.shape === "convexhull", "shape round-trips as a NAME");
assert(near(set.mass, 2.5), "mass " + set.mass);
assert(near(set.restitution, 0.75), "restitution " + set.restitution);
assert(near(set.friction, 0.25), "friction " + set.friction);
assert(near(set.damping, 0.125), "damping " + set.damping);
assert(near(set.collisionMargin, 0.05), "collisionMargin " + set.collisionMargin);
assert(set.isStatic === false, "isStatic is derived false for a massive rigid body");

// A partial change leaves everything else alone.
assert(node.physics(box, { friction: 0.9 }) === true, "a one-key change");
var partial = node.physicsInfo(box);
assert(near(partial.friction, 0.9), "friction moved");
assert(near(partial.mass, 2.5) && partial.shape === "convexhull",
       "and nothing else did");

// type "static" is the mass-0 case, derived exactly like the panel derives it.
assert(node.physics(box, { type: "static" }) === true, "type -> static");
var statics = node.physicsInfo(box);
assert(statics.type === "static" && near(statics.mass, 0.0),
       "a static body is forced to mass 0");
assert(statics.isStatic === true, "and isStatic follows");
assert(statics.enabled === true, "a static body is still a physics body");

// type "none" takes it out of the simulation entirely (the serializer's gate).
assert(node.physics(box, { type: "none" }) === true, "type -> none");
assert(node.physicsInfo(box).enabled === false, "isPhysicsBody cleared");

// ---- 2. the refusals -------------------------------------------------------
var empty = caught(function () { node.physics(box, {}); });
assert(empty !== null && empty.indexOf("physicsInfo") >= 0,
       "an empty change is refused, and points at the reader: " + empty);

var badKey = caught(function () { node.physics(box, { bounciness: 0.5 }); });
assert(badKey !== null && badKey.indexOf("unknown key") >= 0,
       "an unknown key is REFUSED, not swallowed: " + badKey);

var visible = caught(function () { node.physics(box, { isVisible: false }); });
assert(visible !== null && visible.indexOf("not serialized") >= 0,
       "isVisible is refused WITH the reason (it does not survive a save): " + visible);

var badType = caught(function () { node.physics(box, { type: "ragdoll" }); });
assert(badType !== null && badType.indexOf("rigidbody") >= 0,
       "an unknown type name is refused with the list: " + badType);

var softbody = caught(function () { node.physics(box, { type: "softbody" }); });
assert(softbody !== null,
       "softbody is refused: the enum has it, nothing implements it (a silent no-op otherwise)");

var badShape = caught(function () { node.physics(box, { shape: "capsule" }); });
assert(badShape !== null && badShape.indexOf("trianglemesh") >= 0,
       "an unknown shape name is refused with the list: " + badShape);

var badNumber = caught(function () { node.physics(box, { mass: "heavy" }); });
assert(badNumber !== null && badNumber.indexOf("must be a number") >= 0,
       "a non-numeric scalar is refused: " + badNumber);

var boolNumber = caught(function () { node.physics(box, { mass: true }); });
assert(boolNumber !== null && boolNumber.indexOf("must be a number") >= 0,
       "a BOOLEAN scalar is refused too (true converts to 1.0 and would land silently): " + boolNumber);

var negative = caught(function () { node.physics(box, { mass: -1 }); });
assert(negative !== null && negative.indexOf(">= 0") >= 0,
       "a negative mass is refused: " + negative);

var staticMass = caught(function () { node.physics(box, { type: "static", mass: 4 }); });
assert(staticMass !== null && staticMass.indexOf("mass 0") >= 0,
       "static + a non-zero mass is refused rather than silently zeroed: " + staticMass);

// The geometry shapes need geometry. PhysicsHelper's convexhull/trianglemesh
// branches read the node's mesh through an unchecked cast; a light has none.
var lamp = scene.addLight("point", { position: { x: 0, y: 5, z: 0 } });
var noGeometry = caught(function () { node.physics(lamp, { shape: "trianglemesh" }); });
assert(noGeometry !== null && noGeometry.indexOf("geometry") >= 0,
       "a geometry shape on a light is refused: " + noGeometry);
assert(node.physics(lamp, { type: "static", shape: "sphere" }) === true,
       "...but an analytic shape on a light is fine (it only needs a transform)");
assert(node.physics(lamp, { type: "none" }) === true, "the light leaves the simulation again");

// A refused call changes NOTHING (everything is validated into a copy first).
assert(node.physics(box, { type: "rigidbody", shape: "sphere", mass: 3 }) === true, "a known-good set");
var beforeRefusal = node.physicsInfo(box);
caught(function () { node.physics(box, { mass: 9, shape: "banana" }); });
var afterRefusal = node.physicsInfo(box);
assert(near(afterRefusal.mass, beforeRefusal.mass) && afterRefusal.shape === beforeRefusal.shape,
       "a call refused on its SECOND key wrote none of its first");

var noNode = caught(function () { node.physicsInfo("not-a-guid"); });
assert(noNode !== null, "physicsInfo on an unknown id fails cleanly");

// ---- 3. save / close / reopen ---------------------------------------------
assert(node.physics(box, {
    type: "rigidbody", shape: "sphere", mass: 7.5,
    restitution: 0.6, friction: 0.4, damping: 0.05, collisionMargin: 0.02
}) === true, "the values to persist");

assert(project.save() === true, "project.save");
assert(project.close() === true, "project.close");
assert(project.open(proj) === true, "project.open (REOPEN)");
assert(nodeById(box) !== null, "the cube survived the reopen");

var loaded = node.physicsInfo(box);
assert(loaded.enabled === true, "isPhysicsBody survived");
assert(loaded.type === "rigidbody", "type survived");
assert(loaded.shape === "sphere", "shape survived");
assert(near(loaded.mass, 7.5, 1e-3), "mass survived: " + loaded.mass);
assert(near(loaded.restitution, 0.6, 1e-3), "restitution survived: " + loaded.restitution);
assert(near(loaded.friction, 0.4, 1e-3), "friction survived: " + loaded.friction);
assert(near(loaded.damping, 0.05, 1e-3), "damping survived: " + loaded.damping);
assert(near(loaded.collisionMargin, 0.02, 1e-3), "collisionMargin survived: " + loaded.collisionMargin);

// ---- 4. the behavioural assertion ------------------------------------------
// Two spheres side by side at the same height, identical in every way except
// what node.physics wrote. Only the simulation can tell them apart.
var faller = scene.addPrimitive("sphere", { position: { x: -2, y: 12, z: 0 } });
var anchor = scene.addPrimitive("sphere", { position: { x: 2, y: 12, z: 0 } });
assert(node.physics(faller, { type: "rigidbody", shape: "sphere", mass: 1 }) === true,
       "faller: a dynamic rigid body");
assert(node.physics(anchor, { type: "static", shape: "sphere" }) === true,
       "anchor: static (mass 0) — the ONLY difference");

var fallerY0 = nodeById(faller).position.y;
var anchorY0 = nodeById(anchor).position.y;

assert(editor.simulate(true) === true, "editor.simulate(true)");
editor.frame(120, 1.0 / 60.0);   // 2 deterministic seconds of gravity
assert(editor.simulate(false) === true, "editor.simulate(false)");

var fallerY1 = nodeById(faller).position.y;
var anchorY1 = nodeById(anchor).position.y;
console.log("    faller y: " + fallerY0 + " -> " + fallerY1);
console.log("    anchor y: " + anchorY0 + " -> " + anchorY1);
assert(fallerY1 < fallerY0 - 5.0,
       "THE DYNAMIC BODY FELL under gravity (" + fallerY0 + " -> " + fallerY1 + ")");
assert(near(anchorY1, anchorY0, 1e-2),
       "and the static one did not move at all (" + anchorY0 + " -> " + anchorY1 + ")");

// The verb's own values are untouched by the run.
var afterSim = node.physicsInfo(faller);
assert(afterSim.type === "rigidbody" && near(afterSim.mass, 1.0),
       "the simulation did not rewrite the document's physics settings");

console.log("scripting.e2e.physics PASSED");
