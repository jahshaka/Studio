// make_ogre_areaapproxlights.js — the PORT of Ogre-Next's
// ApiUsage/AreaApproxLights (SPECS/OGRE_SAMPLES_TAB_SPEC.md §3 row 5).
//
//   cat $TREE/scenes/tools/ogre_pbs_base.js \
//       $TREE/scenes/tools/make_ogre_areaapproxlights.js | sed ... > /tmp/port.js
//
// WHAT THE SAMPLE IS FOR. The base scene with its two spots replaced by two
// AREA lights — rectangles of light, 15 x 15 and 5 x 5, in the same two places.
// A rectangle has a SIZE, so its highlight is a rectangle and its shadow edge
// softens with distance, which a point or a spot can never do.
//
// WHAT THIS PORT CANNOT SHOW (the tile says the first line):
//
//   * AREA LIGHTS NEVER CAST SHADOWS. That is an engine rule on BOTH sides,
//     not a gap in the port: HlmsPbs has no shadow path for them.
//   * THEIR LIGHT-SHAPE PLANES. Their sample draws an unlit quad where each
//     area light is, so you can see the emitter. We have no unlit "show me the
//     light" helper beyond the editor's light wires; the port leaves the
//     emitters invisible, which is what the renderer actually does.
//   * THEIR MASK TEXTURE. Their warm light carries a generated area mask
//     (a gobo). node.setLightTexture binds one here too, and the port uses OUR
//     own tile image rather than their generated one; only the fast
//     approximation samples a mask, which is the mode this port authors.
//   * THE F5 SWAP between approximate and LTC. Ours is the `accurate` row on
//     the light, authored false (their LT_AREA_APPROX).

var TREE = "@TREE@";
var ARCHIVE = TREE + "/scenes/ogre/AreaApproxLights.zip";
var PREVIEW = TREE + "/scenes/ogre/preview/areaapproxlights.png";
var MASK = TREE + "/app/content/textures/tile.png";   // 2.4 KB — ours, and tiny

assert(project.create("Area Lights").length > 0, "created the project");

// The base scene WITHOUT its spots — the area lights take their place.
ogrePbsBase({ sky: "realistic", sunDir: [-1, -1, -1], sunPower: 1.0, skyLight: 1.0,
              spots: false });
ogrePalette({ prefix: "Palette", spacing: 1.0, y: 1.0, scale: 0.5 });
photonOff();

// Their two area lights, verbatim: 15 x 15 warm at (-10, 6, 10) aimed
// (1,-1,-1), 5 x 5 cold at (5, 4, -5) aimed (-1,-1,1), both powerScale pi.
var warm = ogreAreaLight("Warm Area", [-10, 6, 10], [1, -1, -1], [0.8, 0.4, 0.2], PI, 15, 15);
var cold = ogreAreaLight("Cold Area", [5, 4, -5], [-1, -1, 1], [0.2, 0.4, 0.8], PI, 5, 5);

// THE MASK, on the warm one only, like theirs. Ours, not theirs: their mask is
// generated in C++ from their media; this is our own shipped tile image, bound
// through the ordinary binding path so it travels inside the archive.
var mask = assets.importFile(MASK);
assert(mask && mask.length > 10, "imported the area mask -> " + mask);
assert(node.setLightTexture(warm, mask), "the warm area light wears a mask");
var bound = node.lightTexture(warm);
assert(bound.applies === true,
       "the renderer really samples it (approximate + not accurate): " + J(bound));

// The two rows that make an area light an area light, read back off the
// document so a reader can see the port did not just add two points.
assert(Math.abs(node.property(warm, "rectWidth") - 15) < 1e-4 &&
       Math.abs(node.property(warm, "rectHeight") - 15) < 1e-4, "the warm light is 15 x 15 m");
assert(Math.abs(node.property(cold, "rectWidth") - 5) < 1e-4, "the cold light is 5 x 5 m");

ogreCamera();
editor.frame(30);
ogreFinish(ARCHIVE, PREVIEW, [{ x: 0.5, y: 0.62 }, { x: 0.25, y: 0.75 }]);
console.log("make_ogre_areaapproxlights: PASS");
