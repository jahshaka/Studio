// make_ogre_screenspacereflections.js — the PORT of Ogre-Next's
// ApiUsage/ScreenSpaceReflections (SPECS/OGRE_SAMPLES_TAB_SPEC.md §3 row 9).
//
//   cat $TREE/scenes/tools/ogre_pbs_base.js \
//       $TREE/scenes/tools/ogre_room.js \
//       $TREE/scenes/tools/make_ogre_screenspacereflections.js | sed ... > /tmp/port.js
//
// WHAT THE SAMPLE IS FOR. The LocalCubemap room again, POLISHED — their same
// four datablocks at roughness 0.02 instead of 0.65 — with screen-space
// reflections on. Everything on screen reflects everything else on screen, and
// everything off screen does not: the sample is as much about SSR's failure
// mode as its strength.
//
// WHAT THIS PORT CANNOT SHOW (no note on the tile):
//
//   * THEIR ROUGHNESS KEYS. F2/F3 sweep every material's roughness live so you
//     can watch the reflection blur; ours is a material row per object.
//   * The room's reflection PROBES are refused here for the same reason they
//     are in the Local Cubemaps port (a wall of four panels reads as open —
//     the engine finding in that port's header). SSR does not need them: it is
//     a screen-space pass and this is the sample that shows what that buys
//     without a probe anywhere.

var TREE = "@TREE@";
var ARCHIVE = TREE + "/scenes/ogre/ScreenSpaceReflections.zip";
var PREVIEW = TREE + "/scenes/ogre/preview/screenspacereflections.png";

assert(project.create("Screen Space Reflections").length > 0, "created the project");

// Their ambient here is (0.2,0.4,0.8) x 0.2 above and (0.6,0.5,0.4) x 0.2 below
// — a mean of about 0.09 of radiance, three times the LocalCubemaps room's. The
// grey whose hemispherical integral is 0.09 is sRGB 82, #525252.
ogreSky({ sky: "#525252", sunDir: [-1, -1, -1], sunPower: 1.0, skyLight: 1.0 });
removeNode("Ground");

// THE SAME ROOM, POLISHED: roughness 0.02, which is the one material
// difference between this sample and LocalCubemaps.
ogreRoom(0.02);
ogreRoomLights();
ogreRoomBounds();

// SSR IS A WORLD ROW (Off / Half-Res / Full-Res Rays). Their sample is the
// full-rate one.
assert(world.override({ id: "ssr", value: "hq" }).valueId === "hq",
       "SSR pinned to full-res rays");
// GI stays on the tier the room wants; SSR is a post pass and not GI.
assert(world.photon({ enabled: true, tier: "epic" }).technique === "vct_pcc_hybrid",
       "Photon Epic under it");
assert(world.gi({ pccGrid: { x: 1, y: 1, z: 3 } }), "the room's probe grid, as in LocalCubemaps");

ogreCamera([-0.505, 3.4, 12.5], [-0.505, 3.0, 0.0], 45);
editor.frame(40);
log("giStatus: " + J(world.giStatus()));

ogreFinish(ARCHIVE, PREVIEW, [{ x: 0.5, y: 0.8 }, { x: 0.5, y: 0.4 }]);
console.log("make_ogre_screenspacereflections: PASS");
