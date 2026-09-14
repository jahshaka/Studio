// make_ogre_tutorial_ssao.js — the PORT of Ogre-Next's Tutorials/Tutorial_SSAO
// (SPECS/OGRE_SAMPLES_TAB_SPEC.md §3 row 11).
//
//   cat $TREE/scenes/tools/ogre_pbs_base.js \
//       $TREE/scenes/tools/make_ogre_tutorial_ssao.js | sed ... > /tmp/port.js
//
// WHAT THE SAMPLE IS FOR. A field of 64 big spheres crowded onto a plane, 2 m
// apart and 1.0 to 1.5 m across, so they nearly touch. AMBIENT OCCLUSION lives
// in exactly those contacts: the creases where two surfaces approach are darker
// than the ambient term alone would make them, and a field of tangent spheres
// is the cheapest way to put a hundred of those creases on screen at once.
//
// WHAT THIS PORT CANNOT SHOW (no note on the tile):
//
//   * THEIR SSAO KNOB SET. Their tutorial drives the pass' own uniforms
//     (radius, power, bias, sample count) from the C++ that built the
//     compositor node. Ours are two scene numbers — world.postFx ssaoRadius and
//     ssaoPower — plus the on/off/half-res World Mode row, which is what the
//     renderer exposes.
//   * Their spheres reflect a cubemap from their media; ours reflect the sky.

var TREE = "@TREE@";
var ARCHIVE = TREE + "/scenes/ogre/Tutorial_SSAO.zip";
var PREVIEW = TREE + "/scenes/ogre/preview/tutorial_ssao.png";

assert(project.create("SSAO").length > 0, "created the project");

// Floor and lights, no 4 x 4 grid — the field is the scene.
ogrePbsBase({ sky: "realistic", sunDir: [-1, -1, -1], sunPower: 1.0, skyLight: 1.0,
              grid: false });

// THEIR FIELD: 8 x 8 at 2 m spacing, their Sphere1000 at meshScale 3 (2 where
// x == z). Our sphere is twice theirs, so our scale is half: 1.5 and 1.0.
ogrePalette({ prefix: "Ball", spacing: 2.0, y: 1.0, scale: 1.5, altScale: 1.0 });

photonOff();

// THE PASS IS THE SAMPLE. Half-res is the tier default and what their
// compositor runs; the two continuous numbers are ours to author.
assert(world.override({ id: "ssao", value: "half" }).valueId === "half",
       "SSAO on at half res, like their pass");
var fx = world.postFx({ ssaoRadius: 2.0, ssaoPower: 1.5 });
assert(Math.abs(fx.ssaoRadius - 2.0) < 1e-4 && Math.abs(fx.ssaoPower - 1.5) < 1e-4,
       "AO reach 2 m at power 1.5 — one sphere spacing, so the contacts darken");

// Their camera sits further back than the default: the field is 14 m across.
ogreCamera([0, 8, 18], [0, 1, 0], 45);
editor.frame(30);
ogreFinish(ARCHIVE, PREVIEW, [{ x: 0.5, y: 0.55 }, { x: 0.25, y: 0.6 }]);
console.log("make_ogre_tutorial_ssao: PASS");
