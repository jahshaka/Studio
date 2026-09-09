// Re-stages scenes/"Skeletal Animation".zip onto the SCENE-SCALE CONVENTION
// (owner 2026-09-08: "we need a default scene size and to respect it for all
// sample scenes"; 1 unit = 1 metre, human-scale content, the default explorer
// lens on the saved camera). Run it, do not hand-edit the archive:
//
//   TREE=<absolute path to the source tree>
//   sed "s|@TREE@|$TREE|" $TREE/scenes/tools/restage_skeletal_scale.js > /tmp/skel.js
//   cd <a scratch dir>
//   HOME=<a scratch home> DISPLAY=<your Xvfb> \
//       <build>/bin/Jahshaka --script /tmp/skel.js
//
// (@TREE@ is substituted rather than derived — same convention as
// make_mirror_room.js / reauthor_particles.js: a hardcoded tree path in a
// committed tool is how it silently rewrites somebody else's archives.)
//
// ENGINE-UP: it re-shoots scenes/preview/skeletal.png, which is a real frame.
//
// WHAT WAS WRONG. The sample's rig is authored in CENTIMETRES (Mixamo/Collada:
// the bind pose measures 180.6 units from the floor to the top of the head, the
// arms span 193.9) and the imported root carries scale 0.04 — so the dancer
// stood 7.2 METRES tall and 7.8 m wide, four times life size, with the saved
// camera 15.2 m away to fit her in. cm -> m is 0.01, and that is the only
// number this script changes: the CHARACTER shrinks 4x and the camera comes in
// 4x with her, which is the identical framing (a uniform scale of the subject
// and the eye is the same photograph). Her clips ride the root, so the dance is
// untouched; the point light's position and RANGE scale with her because the
// engine's falloff 1 / (0.5 + 0.5 * d^2 / r^2) is scale-invariant only when the
// range scales with the distances.
//
// WHAT DOES CHANGE VISIBLY, on purpose: the Ground is a metric 1024 m plane
// with a metric tile and it does NOT move — so the floor tiles now read four
// times larger relative to the dancer, which is what a 1.8 m person standing on
// that floor actually looks like. That is the convention doing its job.
//
// IDEMPOTENT: it computes the correction from the CURRENT root scale (target /
// current), so a second run is a no-op multiply by 1.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function J(x) { return JSON.stringify(x); }

var TREE = "@TREE@";
var ARCHIVE = TREE + "/scenes/Skeletal Animation.zip";
var PREVIEW = TREE + "/scenes/preview/skeletal.png";

// The rig's own unit: Collada centimetres. 1 unit = 1 m means 0.01.
var TARGET_ROOT_SCALE = 0.01;

var imported = project.importArchive(ARCHIVE);
assert(imported && imported.guid, "imported " + ARCHIVE + " -> " + imported.guid);
assert(project.open(imported.guid), "opened the project");

// ---- the character root ----------------------------------------------------
var root = scene.find("capoeira");
assert(!!root, "the 'capoeira' character root is in the scene");
var before = node.info(root);
console.log("character root scale before: " + J(before.scale));
var k = TARGET_ROOT_SCALE / before.scale.x;
console.log("correction factor k = " + k);

assert(node.transform(root, { scale: { x: TARGET_ROOT_SCALE, y: TARGET_ROOT_SCALE,
                                       z: TARGET_ROOT_SCALE } }),
       "character root scaled to " + TARGET_ROOT_SCALE);

var height = scene.bounds({ nodes: [root] });
console.log("character bounds after: " + J(height));
assert(height.size.y > 1.4 && height.size.y < 2.2,
       "the dancer is human-height (" + height.size.y.toFixed(2) + " m)");

// ---- the lights ------------------------------------------------------------
// Position AND range, for the reason in the header. The directional light has
// no position that matters and no range at all; it is left alone.
var pl = scene.find("Point Light");
if (pl) {
    var p = node.info(pl).position;
    assert(node.transform(pl, { position: { x: p.x * k, y: p.y * k, z: p.z * k } }),
           "point light moved with the subject");
    var r = node.property(pl, "distance");
    assert(node.setProperty(pl, "distance", r * k),
           "point light range " + r + " -> " + (r * k));
}

// ---- the saved camera ------------------------------------------------------
// Scaled, not re-framed: same direction, same lens (this sample already used
// the convention's 45 degrees), distance multiplied by k. Identical picture.
var cam = editor.camera();
console.log("camera before: " + J(cam));
assert(cam.fov === 45, "the saved lens is already the convention's 45 degrees (" + cam.fov + ")");
editor.setCamera({ position: { x: cam.position.x * k, y: cam.position.y * k,
                               z: cam.position.z * k },
                   rotation: cam.rotation, fov: cam.fov });
console.log("camera after: " + J(editor.camera()));

editor.select(null);
editor.frame(20);
assert(project.save(), "saved");

// ---- the shipped preview ---------------------------------------------------
editor.gameView(true);
editor.frame(60);
var shot = editor.screenshot(PREVIEW, 1280, 720, [{ x: 0.5, y: 0.5 }], true);
console.log("preview centre: " + J(shot.center));
editor.gameView(false);

// ---- the archive -----------------------------------------------------------
var out = project.exportArchive(ARCHIVE);
assert(out && out.assets > 0, "exported " + ARCHIVE + " (" + J(out) + ")");
assert(project.close(), "closed");
console.log("restage_skeletal_scale: PASS");
