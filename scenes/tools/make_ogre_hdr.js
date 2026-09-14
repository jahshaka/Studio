// make_ogre_hdr.js — the PORT of Ogre-Next's Showcase/Hdr
// (SPECS/OGRE_SAMPLES_TAB_SPEC.md §3 row 4).
//
//   cat $TREE/scenes/tools/ogre_pbs_base.js \
//       $TREE/scenes/tools/make_ogre_hdr.js | sed "s|@TREE@|$TREE|" > /tmp/port.js
//
// WHAT THE SAMPLE IS FOR. The PBS base scene lit at REAL PHOTOMETRIC LEVELS and
// developed by an auto-exposure + bloom chain, with four presets their keys
// cycle: bright sunny day, slightly hazy day, heavy overcast, gibbous moon
// night. The point is that one scene, one set of materials, holds up across
// five orders of magnitude of light because the exposure moves, not the art.
//
// THEIR CALIBRATION, which is why the numbers look strange: their HDR is
// calibrated to multiply by 1024 (-10 stops, for the range of a 16-bit float
// target), so direct sunlight at ~100,000 lux becomes a light power of ~97.
// This port authors the FIRST preset — "Bright, sunny day" — at their numbers.
//
// WHAT THIS PORT CANNOT SHOW (the tile says the first line):
//
//   * THE PRESET CYCLE. Theirs is a keyboard walk over four rows; ours is a
//     scene setting, so the port IS one preset. The other three are three
//     numbers away (world.postFx exposure/min/max + the sun's intensity) and
//     are written out in this file if anyone wants to re-author them.
//   * THEIR HDR SKY COLOUR. Their preset sets an ambient/sky colour ABOVE 1.0
//     ((0.2,0.4,0.6) x 60) — a colour we cannot pick, because a picked colour
//     is an sRGB triple and tops out at white. The port uses the realistic sky
//     instead, which is HDR by construction and is the physically honest way
//     to say "the sky is 60x brighter than a grey card".
//   * THEIR ENVMAP SCALE (16.0 here). We have no per-scene reflection gain;
//     ours comes from the same sky the ambient does.

var TREE = "@TREE@";
var ARCHIVE = TREE + "/scenes/ogre/Hdr.zip";
var PREVIEW = TREE + "/scenes/ogre/preview/hdr.png";

assert(project.create("HDR").length > 0, "created the project");

// THEIR "Bright, sunny day" PRESET, row by row:
//   sun power        97.0      -> intensity 97/pi (the pi rule, see the harness)
//   spot power       1.5       -> intensity 1.5/pi
//   exposure         0.0
//   min auto expos. -1.0
//   max auto expos.  2.5
//   bloom threshold  5.0
// The other three presets, for a re-author: hazy day {48, 0, -2, 2.5, 5},
// overcast {6.0625, 0, -2.5, 1, 5}, moon night {0.0009251, 0.65, -2.5, 3, 5}.
ogrePbsBase({ sky: "realistic", sunDir: [-1, -1, -1], sunPower: 97.0, skyLight: 1.0,
              spots: false });

// Their spots at the preset's 1.5, not the base scene's pi.
ogreSpot("Warm Spot", [-10, 10, 10], [1, -1, -1], [0.8, 0.4, 0.2], 1.5, 20);
ogreSpot("Cold Spot", [10, 10, -10], [-1, -1, 1], [0.2, 0.4, 0.8], 1.5, 20);

// Their palette, with the one material difference this sample makes:
// setBrdf(DefaultHasDiffuseFresnel) on every sphere — which IS one of our six
// BRDF names, so this row ports exactly where the PbsMaterials BRDF cycle
// could not. The enum travels by its DISPLAY LABEL, which is what
// material.properties(id) lists: "Default (Diffuse Fresnel)".
ogrePalette({ prefix: "Palette", spacing: 1.0, y: 1.0, scale: 0.5,
              extra: { brdf: "Default (Diffuse Fresnel)" } });

photonOff();

// THE POST CHAIN IS THE SAMPLE. HDR and bloom are World Mode rows; the
// continuous numbers are world.postFx.
assert(world.override({ id: "hdr", value: "on" }).valueId === "on", "HDR on");
assert(world.override({ id: "bloom", value: "on" }).valueId === "on", "bloom on");
var fx = world.postFx({ exposure: 0.0, exposureMin: -1.0, exposureMax: 2.5, bloomThreshold: 5.0 });
assert(Math.abs(fx.exposureMin + 1.0) < 1e-4 && Math.abs(fx.exposureMax - 2.5) < 1e-4,
       "their bright-day exposure window (-1.0 .. 2.5)");
assert(Math.abs(fx.bloomThreshold - 5.0) < 1e-4, "their bloom threshold (5.0)");

// THE SUN IS 97, and that is the whole point: prove it landed rather than
// being clamped somewhere on the way to the renderer.
var sunId = world.sun().light;
var sunI = node.property(sunId, "intensity");
assert(Math.abs(sunI - 97.0 / Math.PI) < 1e-3,
       "the sun carries their 97.0 power scale (intensity " + sunI.toFixed(3) + ")");

ogreCamera();
editor.frame(30);
ogreFinish(ARCHIVE, PREVIEW, [{ x: 0.5, y: 0.62 }, { x: 0.2, y: 0.2 }]);
console.log("make_ogre_hdr: PASS");
