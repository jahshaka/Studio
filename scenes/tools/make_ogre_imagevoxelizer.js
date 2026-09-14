// make_ogre_imagevoxelizer.js — the SUBSTITUTE for Ogre-Next's
// ApiUsage/ImageVoxelizer and Tests/Voxelizer
// (SPECS/OGRE_SAMPLES_TAB_SPEC.md §3 row 15: "ACHIEVABLE, substituted").
//
//   cat $TREE/scenes/tools/ogre_pbs_base.js \
//       $TREE/scenes/tools/ogre_room.js \
//       $TREE/scenes/tools/make_ogre_imagevoxelizer.js | sed ... > /tmp/port.js
//
// ============================================================================
// THIS IS A TECHNIQUE COMPARISON, NOT A PICTURE COMPARISON. SAY SO ON THE TILE.
// ============================================================================
//
// Their sample voxelizes IMPORTED MESHES — athene, sibenik, tudorhouse and a
// Cornell box, all Ogre .mesh files — and cone-traces the voxels for bounced
// light. We do not import Ogre meshes (spec §4, owner decision D5: the format
// is not in MODEL_EXTS, assimp's Ogre importer is compiled out, and their
// DebugPack meshes are v2.1 which assimp could not read anyway), so the
// GEOMETRY CANNOT MATCH and a pixel diff against their frame is meaningless.
//
// What CAN be compared is the technique: does voxel cone tracing put colour
// where colour should go? So the substitute is the two pieces of geometry that
// answer that question and are buildable from primitives:
//
//   * THE ROOM (the LocalCubemap room, our shared harness) — a real interior
//     with a floor, a ceiling, a doorway and coloured walls, which is what the
//     voxelizer has to resolve;
//   * A CORNELL BOX inside it — the 1984 radiosity test scene, and still the
//     one everybody reads at a glance: white box, RED left wall, GREEN right
//     wall, two blocks. Colour bleed onto the white floor is the whole result.
//     If the red wall does not tint the floor, the bounce is not happening.
//
// Under `world.photon({tier: "medium"})`, which is VOXEL CONE TRACING — the
// technique their sample is named after.
//
// WHAT THIS PORT CANNOT SHOW (the tile says the first line):
//   * their meshes, therefore their picture;
//   * their voxelizer's IMAGE mode (ImageVoxelizer builds its voxels from a
//     pre-rendered image atlas per mesh rather than from the triangles). Ours
//     voxelizes the scene; there is no document concept for the other route.

var TREE = "@TREE@";
var ARCHIVE = TREE + "/scenes/ogre/ImageVoxelizer.zip";
var PREVIEW = TREE + "/scenes/ogre/preview/imagevoxelizer.png";

assert(project.create("Voxelizer").length > 0, "created the project");

// A dark sky, so what light there is comes from the room's own lamps and the
// bounce is not drowned in ambient. Same derivation as the LocalCubemaps port.
ogreSky({ sky: "#303030", sunDir: [-1, -1, -1], sunPower: 1.0, skyLight: 1.0 });
removeNode("Ground");

ogreRoom();
ogreRoomLights();

// ---- the Cornell box ------------------------------------------------------
// The canonical proportions (a cube open on one face), 4 m on a side, standing
// on the room's floor in the middle of the long chamber and open towards the
// camera. Walls are 0.1 m slabs so they are walls and not planes — the
// voxelizer reads volume.
var CB = { x: -0.5, z: 6.0, s: 2.0, t: 0.1 };   // centre, half-size, half-thickness

function slab(name, pos, half, colour) {
    var id = scene.addPrimitive("cube", {
        position: { x: pos[0], y: pos[1], z: pos[2] },
        scale: { x: half[0], y: half[1], z: half[2] } });
    node.rename(id, name);
    assert(material.set(id, { baseColor: colour, workflow: "Specular", roughness: 0.85,
                              useFresnelColor: true, separateFresnel: false,
                              fresnelColor: srgbHex(0.04, 0.04, 0.04) }),
           "cornell " + name);
    node.setProperty(id, "mobility", "static");
    return id;
}

// Floor, ceiling, back — white. Left — RED. Right — GREEN. Open towards +Z.
slab("Cornell Floor",   [CB.x, CB.t,             CB.z], [CB.s, CB.t, CB.s], "#d9d9d9");
slab("Cornell Ceiling", [CB.x, 2 * CB.s - CB.t,  CB.z], [CB.s, CB.t, CB.s], "#d9d9d9");
slab("Cornell Back",    [CB.x, CB.s, CB.z - CB.s + CB.t], [CB.s, CB.s, CB.t], "#d9d9d9");
slab("Cornell Red Wall",   [CB.x - CB.s + CB.t, CB.s, CB.z], [CB.t, CB.s, CB.s], "#cc1111");
slab("Cornell Green Wall", [CB.x + CB.s - CB.t, CB.s, CB.z], [CB.t, CB.s, CB.s], "#11aa22");

// The two blocks: the tall one turned about 17 degrees, the short one about -17,
// exactly the arrangement everybody recognises.
var tall = scene.addPrimitive("cube", {
    position: { x: CB.x - 0.75, y: 1.2, z: CB.z - 0.45 },
    rotation: { x: 0, y: 17, z: 0 },
    scale: { x: 0.55, y: 1.2, z: 0.55 } });
node.rename(tall, "Cornell Tall Block");
assert(material.set(tall, { baseColor: "#d9d9d9", roughness: 0.85 }), "cornell tall block");
node.setProperty(tall, "mobility", "static");

var shortB = scene.addPrimitive("cube", {
    position: { x: CB.x + 0.8, y: 0.6, z: CB.z + 0.6 },
    rotation: { x: 0, y: -17, z: 0 },
    scale: { x: 0.6, y: 0.6, z: 0.6 } });
node.rename(shortB, "Cornell Short Block");
assert(material.set(shortB, { baseColor: "#d9d9d9", roughness: 0.85 }), "cornell short block");
node.setProperty(shortB, "mobility", "static");

// The box's own light: a small lamp under the ceiling, which is what makes the
// red and green walls bleed onto the white floor.
ogrePointLight("Cornell Lamp", [CB.x, 2 * CB.s - 0.5, CB.z], [1.0, 0.95, 0.85], PI * 1.5);
node.setProperty(scene.find("Cornell Lamp"), "distance", 8);

// ---- the technique --------------------------------------------------------
// MEDIUM IS VCT — voxel cone tracing feeding the irradiance field. The TIER,
// never its columns (naming mode/quality/bounces would pin them and the scene
// would open as "Custom"). No volume is pinned: the lit volume is automatic and
// world.giStatus() reports what the fit decided.
assert(world.photon({ enabled: true, tier: "medium" }).technique === "vct",
       "Photon Medium — voxel cone tracing, their technique");

ogreCamera([CB.x, 2.2, CB.z + 7.0], [CB.x, 1.6, CB.z], 45);
editor.frame(60);
var gi = world.giStatus();
log("giStatus: " + J(gi));
assert(gi.mode === "vct", "the renderer really is cone-tracing voxels (" + gi.mode + ")");
assert(gi.voxelMetres > 0, "it resolved a voxel size: " + gi.voxelMetres.toFixed(3) + " m");
log("lit volume " + J(gi.boundsMin) + " .. " + J(gi.boundsMax));

ogreFinish(ARCHIVE, PREVIEW, [{ x: 0.5, y: 0.8 }, { x: 0.25, y: 0.55 }, { x: 0.75, y: 0.55 }]);
console.log("make_ogre_imagevoxelizer: PASS");
