// scripting.e2e.mobility — MOVEMENT AS A VERB
// (SPECS/REALTIME_REFLECTIONS_SPEC.md §3.3.5, API-first).
//
// Mobility is how the renderer decides what can be baked into the room's
// lighting (reflection probes, bounce light, cached shadow maps) and what has
// to be handled every frame. It is set as a reflected property —
// node.setProperty(id, 'mobility', 'auto'|'static'|'movable') — and read back
// with node.mobility(id), which also says what `auto` worked out to and why.
//
// This is the verb surface's half of the contract:
//   * every driver kind resolves movable, with its own reason;
//   * a channel-less Animation does NOT (the shipped-content shape);
//   * a light with nothing driving it resolves static;
//   * rule 2: a child of a mover travels with it;
//   * a user setting wins on an undriven node, and a DRIVER openly beats a
//     user setting (recorded, reported, and true again once the driver goes);
//   * AN EDITOR MOVE DOES NOT PROMOTE — the claim the design rests on, because
//     a promotion costs a from-scratch GI rebuild;
//   * the setting survives a save and a reopen, and a scene written before
//     mobility existed (the legacy "static" key) still opens with the same
//     meaning;
//   * the write is one undo command, like every other reflected row.
//
// Document verbs only -> --headless (NULL render system, no display).

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function reads(id, resolved, reason, msg) {
    var m = node.mobility(id);
    assert(m.resolved === resolved && m.reason === reason,
           msg + " [" + m.resolved + "/" + m.reason + "]");
}

project.create("Mobility " + Date.now());

// ---- the default ---------------------------------------------------------
var prop = scene.addPrimitive("cube", { position: { x: 0, y: 0, z: 0 } });
assert(node.mobility(prop).setting === "auto", "a new object's setting is auto");
reads(prop, "static", "default", "a plain prop resolves static/default");
assert(node.mobility(prop).graphStatic === true,
       "...and it really is in the never-moving half of the scene graph");

// ---- the reflected row is the verb --------------------------------------
var rows = node.properties(prop).filter(function (r) { return r.name === "mobility"; });
assert(rows.length === 1, "node.properties reports a `mobility` row");
assert(rows[0].type === "list" && rows[0].writable === true,
       "...as a writable enum row");
assert(rows[0].options.join(",") === "auto,static,movable",
       "...offering exactly auto/static/movable");
assert(rows[0].value === "auto", "...with the NAME as its value, never an ordinal");
assert(node.property(prop, "mobility") === "auto", "node.property reads the name back");

var ordinalRefused = false;
try { node.setProperty(prop, "mobility", 2); } catch (e) { ordinalRefused = true; }
assert(ordinalRefused || node.mobility(prop).setting === "auto",
       "an ORDINAL is refused (this enum travels as a name)");

// ---- drivers, each with its own reason -----------------------------------
var body = scene.addPrimitive("cube", { position: { x: 3, y: 0, z: 0 } });
assert(node.physics(body, { type: "rigidbody", shape: "cube", mass: 1 }) === true,
       "made a physics body");
reads(body, "movable", "physics", "a SIMULATED physics body resolves movable/physics");
// AN IMMOVABLE BODY IS NOT A MOVER: the default scene's ground is a physics
// body of type "static" (what a character walks on), and treating the floor as
// moving would take it out of the room's reflections and bounce light.
assert(node.physics(body, { type: "static" }) === true, "make it an immovable body");
reads(body, "static", "default", "a STATIC physics body (the ground) does not move");
// ...and NEITHER IS A BODY WITH NO TYPE: the panel's Collision Shape row alone
// marks a node a physics body, leaving the type at "none" and the mass at its
// default — and a scene saved before the file carried a type reads back the
// same way. That must not say "it is a physics object" over a Physics section
// reading "None".
assert(node.physics(body, { type: "none", shape: "cube" }) === true,
       "a shape with no type (what the panel's shape row alone produces)");
reads(body, "static", "default", "a SHAPE-only body (type none) does not move");
assert(node.physics(body, { type: "rigidbody", mass: 1 }) === true, "back to a simulated one");
reads(body, "movable", "physics", "...and it moves again");

var emitter = scene.addParticles();
reads(emitter, "movable", "particles", "a particle emitter resolves movable/particles");

var animated = scene.addPrimitive("cube", { position: { x: 6, y: 0, z: 0 } });
anim.create(animated, "Animation");
reads(animated, "static", "default",
      "a CHANNEL-LESS animation drives nothing (the shipped-content shape)");
assert(anim.keyframe(animated, "position", 0) === true, "keyed a real position track");
reads(animated, "movable", "animation", "a real channel resolves movable/animation");

var lamp = scene.addLight("point", {});
reads(lamp, "static", "default", "a light nothing drives resolves static");
assert(node.mobility(lamp).graphStatic === false,
       "...while never being in the graph's static half (a different question)");

// ---- rule 2: it travels with its parent ----------------------------------
var carrier = scene.addEmpty({ position: { x: 9, y: 0, z: 0 } });
var rider = scene.addPrimitive("sphere", { parent: carrier });
reads(rider, "static", "default", "a child of a still parent is static");
anim.create(carrier, "Move");
assert(anim.keyframe(carrier, "position", 0) === true, "keyed the carrier");
reads(carrier, "movable", "animation", "the carrier moves");
reads(rider, "movable", "parent", "...and its child resolves movable/parent");

// ---- the user's setting --------------------------------------------------
assert(node.setProperty(prop, "mobility", "movable") === true, "set the prop Movable by hand");
reads(prop, "movable", "user", "an explicit Movable on a plain object wins");
assert(node.mobility(prop).graphStatic === false, "...and it leaves the static graph half");

assert(node.setProperty(body, "mobility", "static") === true, "set a PHYSICS body Static by hand");
reads(body, "movable", "physics", "a driver openly beats the setting");
assert(node.mobility(body).setting === "static",
       "...and the setting is still recorded (honest, not silently dropped)");
assert(node.physics(body, { type: "none" }) === true, "take the body out of the simulation");
reads(body, "static", "user", "...and the recorded setting becomes true at once");

// ---- ONE UNDO STEP, like every other reflected row -----------------------
var pushesBefore = editor.undoState().pushes;
assert(node.setProperty(prop, "mobility", "static") === true, "another mobility write");
assert(editor.undoState().pushes === pushesBefore + 1,
       "node.setProperty('mobility') pushed exactly one undo command");

// ---- AN EDITOR MOVE DOES NOT PROMOTE ------------------------------------
// The claim the whole design rests on: flipping an object's class rebuilds the
// room's lighting from scratch, so a drag must never do it.
var dragged = scene.addPrimitive("cube", { position: { x: 12, y: 0, z: 0 } });
reads(dragged, "static", "default", "a fresh prop is static");
node.transform(dragged, { position: { x: 13, y: 0, z: 0 } });
reads(dragged, "static", "default",
      "MOVING IT IN THE EDITOR DOES NOT PROMOTE IT (rule 4 is not a promotion)");
assert(node.mobility(dragged).graphStatic === false,
       "...though the cheap graph class did demote, as it always has");

var pinned = scene.addPrimitive("cube", { position: { x: 15, y: 0, z: 0 } });
assert(node.setProperty(pinned, "mobility", "static") === true, "pin one Static");
node.transform(pinned, { position: { x: 16, y: 0, z: 0 } });
assert(node.mobility(pinned).setting === "static",
       "A MOVE DOES NOT CLEAR THE USER'S SETTING (it used to, before mobility)");

// ---- it survives a save and a reopen ------------------------------------
project.close();
var reopenName = "Mobility Reopen " + Date.now();
var reopenGuid = project.create(reopenName);
function named(id, n) { node.setProperty(id, "name", n); return id; }
named(scene.addPrimitive("cube", { position: { x: 0, y: 0, z: 0 } }), "M_auto");
var mStatic = named(scene.addPrimitive("cube", { position: { x: 2, y: 0, z: 0 } }), "M_static");
var mMovable = named(scene.addPrimitive("cube", { position: { x: 4, y: 0, z: 0 } }), "M_movable");
assert(node.setProperty(mStatic, "mobility", "static") === true, "pinned Static");
assert(node.setProperty(mMovable, "mobility", "movable") === true, "pinned Movable");
assert(project.save() === true, "saved");
assert(project.close() === true, "closed");
assert(project.open(reopenGuid) === true, "REOPENED");

function byName(n) {
    var all = scene.nodes();
    for (var i = 0; i < all.length; i++) if (all[i].name === n) return all[i].id;
    throw new Error("assert failed: no node named " + n + " after reopen");
}
assert(node.mobility(byName("M_auto")).setting === "auto",
       "REOPENED: auto is not written to the file and re-derives");
assert(node.mobility(byName("M_static")).setting === "static", "REOPENED: Static came back");
assert(node.mobility(byName("M_movable")).setting === "movable", "REOPENED: Movable came back");
reads(byName("M_movable"), "movable", "user", "REOPENED: ...and resolves from the setting");
assert(node.mobility(byName("M_movable")).graphStatic === false,
       "REOPENED: ...and the load-time pass kept it out of the static graph half");

// ---- A SCENE WRITTEN BEFORE MOBILITY STILL OPENS THE SAME WAY ------------
// v2 wrote a boolean "static" key (true = the user pinned it static, false =
// the user pinned it dynamic). The reader still understands it; the writer
// never produces it again. node.serialize/deserialize is the same
// writer/reader pair the scene file uses, so editing a fragment by hand is the
// honest way to build "a file from before the change" inside a script.
var fragment = node.serialize(byName("M_static"));
assert(fragment.node.mobility === "static",
       "the writer emits the NEW key (and only when a human set it)");
delete fragment.node.mobility;
fragment.node["static"] = true;                    // the v2 spelling
var legacyStatic = node.deserialize(fragment, "", -1);
assert(node.mobility(legacyStatic).setting === "static",
       "LEGACY: the old `static: true` still reads as Static");

var fragment2 = node.serialize(byName("M_movable"));
delete fragment2.node.mobility;
fragment2.node["static"] = false;                  // v2's "Dynamic"
var legacyDynamic = node.deserialize(fragment2, "", -1);
assert(node.mobility(legacyDynamic).setting === "movable",
       "LEGACY: the old `static: false` (Dynamic) reads as Movable — same meaning");

// A SCENE SAVED BEFORE THE PHYSICS BLOCK CARRIED A TYPE reads back as type
// "none" (a missing key is 0), so it must classify like the shape-only body
// above rather than as a mover.
var legacyBodyFrag = node.serialize(byName("M_auto"));
legacyBodyFrag.node["physicsObject"] = true;
legacyBodyFrag.node["physicsProperties"] = { mass: 1, shape: 3 };   // no "type" at all
var legacyBody = node.deserialize(legacyBodyFrag, "", -1);
assert(node.physicsInfo(legacyBody).type === "none",
       "LEGACY: a physics block with no type reads back as none");
reads(legacyBody, "static", "default",
      "LEGACY: ...and such a body does not resolve as moving");

// A fragment with NEITHER key re-derives, which is what almost every node in
// every file says.
var fragment3 = node.serialize(byName("M_auto"));
assert(fragment3.node.mobility === undefined, "an `auto` node writes no key at all");
var plain = node.deserialize(fragment3, "", -1);
assert(node.mobility(plain).setting === "auto", "...and comes back as auto");

console.log("mobility: all assertions passed");
