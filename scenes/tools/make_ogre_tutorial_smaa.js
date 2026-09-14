// make_ogre_tutorial_smaa.js — the PORT of Ogre-Next's Tutorials/Tutorial_SMAA
// (SPECS/OGRE_SAMPLES_TAB_SPEC.md §3 row 12).
//
//   cat $TREE/scenes/tools/ogre_pbs_base.js \
//       $TREE/scenes/tools/make_ogre_tutorial_smaa.js | sed ... > /tmp/port.js
//
// WHAT THE SAMPLE IS FOR. The base scene, deliberately full of near-horizontal
// and near-vertical cube edges at odd angles, rendered through SMAA — a
// post-process edge filter that finds the staircases a rasterizer leaves and
// re-shapes them, without the memory cost of multisampling.
//
// WHAT THIS PORT CANNOT SHOW (the tile carries the first line):
//
//   * OUR OFFSCREEN SCREENSHOTS ARE 1x MSAA BY DESIGN — an offscreen view never
//     raises the sample count, so the preview beside this port is the
//     un-multisampled picture SMAA is there to rescue. To judge SMAA, OPEN the
//     port and look at the viewport, or compare on-screen against on-screen;
//     comparing an on-screen MSAA'd frame against an offscreen one says
//     nothing about either.
//   * THEIR QUALITY KEYS cycle SMAA presets live. Ours is a World Mode row
//     (off / low / high / ultra), authored here at Ultra, which is their
//     sample's own preset.

var TREE = "@TREE@";
var ARCHIVE = TREE + "/scenes/ogre/Tutorial_SMAA.zip";
var PREVIEW = TREE + "/scenes/ogre/preview/tutorial_smaa.png";

assert(project.create("SMAA").length > 0, "created the project");

ogrePbsBase({ sky: "realistic", sunDir: [-1, -1, -1], sunPower: 1.0, skyLight: 1.0 });
photonOff();

// THE TWO ROWS THAT ARE THE SAMPLE: SMAA on at its highest preset, and MSAA
// OFF — the comparison SMAA exists to win is against no multisampling at all,
// and leaving 4x MSAA on underneath would hide the edges it is fixing.
assert(world.override({ id: "smaa", value: "ultra" }).valueId === "ultra", "SMAA Ultra");
assert(world.override({ id: "msaa", value: "off" }).valueId === "off",
       "MSAA off — SMAA is the whole anti-aliasing budget here");
assert(world.antiAliasing() === 1, "the renderer really is at 1x (" + world.antiAliasing() + ")");

// Closer than the default pose: SMAA is judged on edges, and edges need pixels.
ogreCamera([0, 4, 9], [0, 2, 0], 45);
editor.frame(30);
ogreFinish(ARCHIVE, PREVIEW, [{ x: 0.5, y: 0.45 }, { x: 0.3, y: 0.7 }]);
console.log("make_ogre_tutorial_smaa: PASS");
