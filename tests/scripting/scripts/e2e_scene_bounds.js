// scripting.e2e.scene_bounds — the scene.bounds verb, and editor.camera's lens.
//
// The two reads the SCENE-SCALE CONVENTION is measured with (owner 2026-09-08:
// "we need a default scene size and to respect it for all sample scenes").
// samples.cleanstart asserts the shipped archives through them; this file asserts
// the VERBS themselves, against primitives whose size is known from the mesh
// files rather than from a sample somebody may re-author:
//
//   cube.obj    -1..1 on every axis   -> scale S is a 2S metre box
//   sphere.obj  -1..1                 -> scale S is a 2S metre ball
//   teapot.obj  3.86 x 1.89 x 2.4     -> the one primitive that is not a unit
//
// (`scale` multiplying HALF extents is the single fact every sample generator
// has a comment about, and the reason a "scale 24" floor is 48 m across.)
//
// It also pins what the verb REFUSES and what it declines to measure, because
// a bounds read that silently includes light positions — or silently excludes a
// rotation — is worse than no bounds read: every number downstream is wrong and
// nothing says so.
//
// Document verbs only -> --headless.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function J(x) { return JSON.stringify(x); }
function near(a, b, eps) { return Math.abs(a - b) <= (eps || 0.02); }
function refused(fn, what) {
    var threw = false;
    try { fn(); } catch (e) { threw = true; }
    assert(threw, what);
}

assert(project.create("bounds").length > 0, "created the project");

// The default scene ships a 100 m ground plane and two lights; measuring the
// whole scene therefore measures the GROUND, which is the first thing this verb
// has to be honest about. (RE-PINNED from 1024 m by SMOKE_FIX S14: app/models/
// ground.obj was re-staged to 100 m — the old number predates 1 u = 1 m and was
// why a new project fitted its GI over a square kilometre.)
var all = scene.bounds();
console.log("default scene bounds: " + J(all));
assert(near(all.size.x, 100, 1), "the default scene measures its 100 m ground plane (" +
       all.size.x.toFixed(1) + " m) — nothing is silently excluded");

// ---- a cube of known size --------------------------------------------------
var cube = scene.addPrimitive("cube", { position: { x: 0, y: 3, z: 0 } });
node.transform(cube, { scale: { x: 2, y: 0.5, z: 1 } });
var b = scene.bounds({ nodes: [cube] });
console.log("cube bounds: " + J(b));
assert(near(b.size.x, 4) && near(b.size.y, 1) && near(b.size.z, 2),
       "scale multiplies HALF extents: scale (2, 0.5, 1) is a 4 x 1 x 2 m box (" + J(b.size) + ")");
assert(near(b.center.y, 3), "the box is centred on the node (" + b.center.y + ")");
assert(near(b.min.y, 2.5) && near(b.max.y, 3.5), "min/max bracket it (" + b.min.y + ".." + b.max.y + ")");
assert(b.nodes === 1, "one node contributed (" + b.nodes + ")");

// ---- rotation is in the box ------------------------------------------------
// The AABB of a ROTATED box is bigger than the box: a 45 degree turn about Y
// takes a 4 x 2 footprint to (4 + 2) / sqrt(2) = 4.24 on both axes. Reporting
// the unrotated size would be the quiet lie.
node.transform(cube, { rotation: { x: 0, y: 45, z: 0 } });
var rb = scene.bounds({ nodes: [cube] });
console.log("rotated cube bounds: " + J(rb.size));
assert(near(rb.size.x, 4.243, 0.05) && near(rb.size.z, 4.243, 0.05),
       "a rotated node measures as its rotated AABB (" + J(rb.size) + ")");
node.transform(cube, { rotation: { x: 0, y: 0, z: 0 } });

// ---- the parent chain rides along ------------------------------------------
var group = scene.addEmpty({ position: { x: 10, y: 0, z: 0 } });
node.transform(group, { scale: { x: 3, y: 3, z: 3 } });
var child = scene.addPrimitive("sphere", { position: { x: 0, y: 1, z: 0 }, parent: group });

// FIRST, the thing that surprises everyone who measures a scene for the first
// time: `{parent}` REPARENTS KEEPING THE WORLD TRANSFORM, so the new child came
// out with local scale 1/3 and is still a 2 m ball. That is the document's
// deliberate behaviour (the reader's keepTransform rule), and a bounds verb
// that reported 6 m here would be reporting the parent's intent rather than
// the scene.
var cb = scene.bounds({ nodes: [child] });
console.log("child-of-scaled-group bounds: " + J(cb));
assert(near(node.info(child).scale.x, 1 / 3),
       "adding under a scaled parent keeps the world size (local scale " +
       node.info(child).scale.x.toFixed(3) + ")");
assert(near(cb.size.x, 2), "so the ball is still 2 m across (" + cb.size.x + ")");
assert(near(cb.center.x, 10) && near(cb.center.y, 3),
       "at the parent's scaled position (" + J(cb.center) + ")");

// NOW give it its own local scale back: 1 x the parent's 3 is a 6 m ball, and
// that is the parent chain in the measurement.
node.transform(child, { scale: { x: 1, y: 1, z: 1 }, position: { x: 0, y: 1, z: 0 } });
cb = scene.bounds({ nodes: [child] });
assert(near(cb.size.x, 6), "the parent's scale is in the measurement (6 m ball, " + cb.size.x + ")");

// Measuring the GROUP measures the group's contents (descendants always come).
var gb = scene.bounds({ nodes: [group] });
assert(near(gb.size.x, 6) && gb.nodes === 1,
       "measuring a parent measures what is under it (" + J(gb.size) + ", " + gb.nodes + " nodes)");
var gp = scene.bounds({ nodes: [group], includePoints: true });
assert(gp.nodes === 2 && near(gp.size.x, 6),
       "{includePoints} counts the empty too, without changing the box (" + J(gp) + ")");

// ---- what does NOT contribute ----------------------------------------------
var light = scene.addLight("point", { position: { x: 0, y: 40, z: 0 } });
var lb = scene.bounds({ nodes: [light] });
assert(lb.nodes === 0 && lb.size === undefined,
       "a light has no geometry, so it measures nothing (" + J(lb) + ")");
var lp = scene.bounds({ nodes: [light], includePoints: true });
assert(lp.nodes === 1 && near(lp.center.y, 40),
       "{includePoints:true} adds its position as a point (" + J(lp) + ")");

var withLights = scene.bounds({ nodes: [cube, light] });
assert(near(withLights.max.y, 3.5),
       "a light in the list does not inflate the box a room measurement asked for (" +
       withLights.max.y + ")");

// ---- refusals --------------------------------------------------------------
refused(function () { scene.bounds({ nodes: ["not-a-guid"] }); }, "an unknown node id is refused");
refused(function () { scene.bounds({ node: cube }); }, "an unknown option key is refused (node vs nodes)");
refused(function () { scene.bounds({ nodes: [] }); }, "an empty node list is refused, not read as 'everything'");
refused(function () { scene.bounds("cube"); }, "a non-object argument is refused");

// ---- the lens, which is why editor.camera is a document verb ---------------
// It used to require a rendering engine, so the number a scene SAVES as its
// camera could not be read without a GPU — and the saved lens is exactly what
// the scene-scale convention is about.
var cam = editor.camera();
console.log("camera: " + J(cam));
assert(typeof cam.fov === "number" && cam.fov > 0,
       "editor.camera() reports the lens under --headless (fov " + cam.fov + ")");
assert(typeof cam.nearClip === "number" && typeof cam.farClip === "number",
       "and the clip planes (" + cam.nearClip + " .. " + cam.farClip + ")");
assert(cam.projection === "perspective", "and the projection mode");
assert(typeof cam.position.y === "number", "and the pose");

console.log("scripting.e2e.scene_bounds: ALL OK");
