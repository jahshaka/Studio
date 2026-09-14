// make_ogre_decals.js — the PORT of Ogre-Next's ApiUsage/Decals
// (SPECS/OGRE_SAMPLES_TAB_SPEC.md §3 row 13).
//
//   cat $TREE/scenes/tools/ogre_pbs_base.js \
//       $TREE/scenes/tools/make_ogre_decals.js | sed ... > /tmp/port.js
//
// WHAT THE SAMPLE IS FOR. The base scene with a DECAL over the middle of it: an
// oriented box that projects a picture — diffuse AND normal — onto every
// surface inside it, so the mark lies on the floor and climbs the cubes it
// touches without anyone editing a material or a UV. Their decal is 10 m across
// at (0, 0.4, 0).
//
// WHAT THIS PORT CANNOT SHOW (no note on the tile):
//
//   * THEIR DEBUG BOX. Their sample draws the decal's wire AABB (a WireAabb
//     tracking the object) and moves the decal with the mouse. Ours draws the
//     box only while the decal is SELECTED in the editor, which is the same
//     information where it belongs.
//   * THEIR IMAGES ARE THEIR MEDIA. The port projects OUR brick preset's colour
//     and normal maps, bound through node.setDecalMaps so both travel inside
//     the archive.
//   * RENDERER LIMIT, ours and theirs: a decal's NORMAL map is only visible on
//     receiving materials that already carry one. The floor here is the ports'
//     marble, which does — and the stone cubes do too, which is why the
//     projection reads on both.

var TREE = "@TREE@";
var ARCHIVE = TREE + "/scenes/ogre/Decals.zip";
var PREVIEW = TREE + "/scenes/ogre/preview/decals.png";
// A HIGH-CONTRAST PAIR, deliberately: a decal that barely differs from what it
// lands on is a decal nobody can see. Brick over marble reads at a glance, and
// the brick normal map is what makes it read as laid rather than painted.


assert(project.create("Decals").length > 0, "created the project");

ogrePbsBase({ sky: "realistic", sunDir: [-1, -1, -1], sunPower: 1.0, skyLight: 1.0 });
photonOff();

// The two images, through the one import pipeline so they are library assets
// with identities (and so the archive carries their bytes). They are the only
// port that pulls the brick pair out of the ports' texture set.
var diffuse = ogreTex("brick_color");
var normal = ogreTex("brick_normal");
assert(diffuse && diffuse.length > 10, "imported the decal's colour map");
assert(normal && normal.length > 10, "imported the decal's normal map");

// THEIR DECAL: at (0, 0.4, 0) with setScale(10) on a unit decal — so the box
// is 10 x 10 x 10 CENTRED on that point, reaching from y = -4.6 to y = 5.4.
// Depth is the PROJECTION THICKNESS and it is not cosmetic: at depth 3 the
// floor (y = -1) sits 0.1 m inside the bottom face and the projection is
// invisible (measured, 2026-09-14). Their 10 is the number that works, for the
// same reason it works for them.
var decal = scene.addDecal(diffuse, { position: { x: 0, y: 0.4, z: 0 },
                                      width: 10, height: 10, depth: 10,
                                      metalness: 0.0, roughness: 0.9 });
assert(!!decal, "the decal");
node.rename(decal, "Decal");
assert(node.setDecalMaps(decal, { diffuse: diffuse, normal: normal }),
       "both maps bound in one call (their setDiffuseTexture + setNormalTexture)");
var maps = node.decalMaps(decal);
assert(maps.diffuse.guid === diffuse && maps.normal.guid === normal,
       "the decal really carries both: " + J(maps));

// LOOKING DOWN, not across: a decal is a mark ON A SURFACE and their eye-level
// pose foreshortens the whole 10 m patch into two rows of pixels.
ogreCamera([0, 11, 11], [0, 0, 0], 45);
editor.frame(30);
ogreFinish(ARCHIVE, PREVIEW, [{ x: 0.5, y: 0.8 }, { x: 0.5, y: 0.5 }]);
console.log("make_ogre_decals: PASS");
