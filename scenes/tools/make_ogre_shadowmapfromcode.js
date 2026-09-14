// make_ogre_shadowmapfromcode.js — the PORT of Ogre-Next's
// ApiUsage/ShadowMapFromCode (SPECS/OGRE_SAMPLES_TAB_SPEC.md §3 row 7).
//
//   cat $TREE/scenes/tools/ogre_pbs_base.js \
//       $TREE/scenes/tools/make_ogre_shadowmapfromcode.js | sed ... > /tmp/port.js
//
// WHAT THE SAMPLE IS FOR. The base scene under FIVE shadow-casting lights at
// once — a directional, two spots and two points — with a shadow node built by
// hand in C++ rather than by a script, and keys that switch the filter and the
// map resolution live. It is the sample you look at to answer "how many
// shadows can this thing draw, and what do they cost".
//
// WHAT THIS PORT CANNOT SHOW (the tile says the first two lines):
//
//   * OUR SHADOW NODE IS FIXED; theirs is authored per sample. We have one
//     atlas with a per-scene resolution (world.setShadowResolution) and a
//     budget of point/spot maps (world.shadows mapBudget) — the picture is the
//     same, the authoring is not, and there is no verb for "build me a shadow
//     node" because there is no document concept for one.
//   * THE LIVE FILTER/RESOLUTION KEYS. Ours are scene settings
//     (world.override shadowFilter, world.setShadowResolution); this port pins
//     their starting pair (PCF -> our "Soft", 2048). Their key cycles the
//     kernel by NAME (PCF 2x2/3x3/4x4, ESM); our row is a softness
//     vocabulary and the renderer picks the kernel.
//   * A DIRECTIONAL LIGHT IS THE ONLY ONE THAT CAN CAST A DIRECTIONAL SHADOW
//     here: the renderer has a single directional slot, so a second
//     directional would be a secondary light. Their sample has one, so this
//     costs the port nothing.

var TREE = "@TREE@";
var ARCHIVE = TREE + "/scenes/ogre/ShadowMapFromCode.zip";
var PREVIEW = TREE + "/scenes/ogre/preview/shadowmapfromcode.png";

assert(project.create("Shadow Maps").length > 0, "created the project");

// Their scene is the base scene's floor and 4 x 4 grid — no sphere palette.
ogrePbsBase({ sky: "realistic", sunDir: [-1, -1, -1], sunPower: 1.0, skyLight: 1.0 });

// ...plus two POINT lights their sample adds, which is what makes five casters.
ogrePointLight("Red Point", [-10, -10, 10], [0.8, 0.0, 0.0], PI);
ogrePointLight("Green Point", [10, -10, -10], [0.0, 0.8, 0.0], PI);

photonOff();

// THE SHADOW SETTINGS ARE THE SAMPLE. Five casters need a budget that admits
// the four lamps (the directional does not use an atlas slot); the filter and
// the resolution are their starting pair.
assert(world.shadows({ enabled: true, mapBudget: 8 }), "shadows on, budget 8 lamps");
assert(world.setShadowResolution(2048) === 2048, "their 2048 atlas");
// Their starting filter is PCF 2x2. Our row is a SOFTNESS vocabulary
// (auto|hard|soft|verysoft) rather than a filter-kernel name — the renderer
// owns the kernel — and "soft" is the PCF middle of it.
assert(world.override({ id: "shadowFilter", value: "soft" }).valueId === "soft",
       "their PCF, as our Soft");

ogreCamera();
editor.frame(30);

// THE READ-BACK IS THE POINT of a shadow port: what the ATLAS is doing, not
// what was asked for. A budget is a ceiling, and the renderer fills it with the
// casters nearest the camera — so a port that shipped with fewer mapped lamps
// than lights would be lying about its own subject.
var sh = world.shadowStatus();
log("shadowStatus: " + J(sh));
assert(sh.resolution === 2048, "the renderer really is at 2048 (" + sh.resolution + ")");
assert(sh.sun && sh.sun.length > 0, "the sun casts the directional shadow");
assert(sh.maps >= 4, "four lamps hold a shadow map (" + sh.maps + " of budget " + sh.budget + ")");

ogreFinish(ARCHIVE, PREVIEW, [{ x: 0.5, y: 0.7 }, { x: 0.2, y: 0.8 }]);
console.log("make_ogre_shadowmapfromcode: PASS");
