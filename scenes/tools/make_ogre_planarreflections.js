// make_ogre_planarreflections.js — the PORT of Ogre-Next's
// ApiUsage/PlanarReflections (SPECS/OGRE_SAMPLES_TAB_SPEC.md §3 row 8).
//
//   cat $TREE/scenes/tools/ogre_pbs_base.js \
//       $TREE/scenes/tools/make_ogre_planarreflections.js | sed ... > /tmp/port.js
//
// WHAT THE SAMPLE IS FOR. The base scene between two upright mirrors: a perfect
// one on the right (10 x 10 m at x = +5, facing -X) and a rough, glassy one on
// the left (7.5 x 5 m at x = -5, facing +X). A planar reflection is a WHOLE
// EXTRA SCENE RENDER per mirror per frame — this is the sample you point at
// when someone asks why two mirrors halve the frame rate.
//
// WHAT THIS PORT CANNOT SHOW (no note on the tile — nothing is missing):
//
//   * THEIR PERFECT MIRROR IS AN UNLIT DATABLOCK sampling the reflection RTT
//     directly. Ours is an ordinary PBR surface made a reflector by
//     node.setPlanarReflector, so it is lit and fresnel-weighted like any other
//     material — a touch less mirror-perfect at grazing angles, and physically
//     more honest.
//   * THE ACTOR/ACTIVATION API. They add PlanarReflectionActors by hand with
//     activation priorities and a max-actor count. We derive the plane, its
//     size and its normal FROM THE MESH (it must be flat: thinnest extent no
//     more than a tenth of the next) and the budget is a world setting.

var TREE = "@TREE@";
var ARCHIVE = TREE + "/scenes/ogre/PlanarReflections.zip";
var PREVIEW = TREE + "/scenes/ogre/preview/planarreflections.png";

assert(project.create("Planar Reflections").length > 0, "created the project");

// Their scene: floor + the 4 x 4 grid + the two spots. No sphere palette.
ogrePbsBase({ sky: "realistic", sunDir: [-1, -1, -1], sunPower: 1.0, skyLight: 1.0 });
photonOff();

// TWO MIRRORS, at their places and sizes. Their plane mesh is 10 x 10 with its
// normal on +Z, turned +-90 degrees about Y to face across the scene; ours is a
// 2 x 2 XZ plane, so it is pitched upright first and then yawed the same way.
// The REFLECTING FACE is the object's positive thin axis, which after a +90
// pitch is +Z, and the yaw then aims it.
function mirror(name, pos, yaw, halfW, halfH) {
    var id = scene.addPrimitive("plane", {
        position: { x: pos[0], y: pos[1], z: pos[2] },
        rotation: { x: 90, y: yaw, z: 0 },
        scale: { x: halfW, y: 1, z: halfH } });
    node.rename(id, name);
    return id;
}

// Right-hand mirror: 10 x 10 at (5, 5, 0), facing -X (their -90 degrees about Y).
var perfect = mirror("Mirror", [5, 5, 0], -90, 5, 5);
assert(material.set(perfect, { baseColor: "#0d0d0d", workflow: "Specular", roughness: 0.02,
                               useFresnelColor: true, separateFresnel: false,
                               fresnelColor: srgbHex(0.9, 0.9, 0.9) }),
       "the perfect mirror: smooth and highly reflective");
assert(node.setPlanarReflector(perfect, true), "the perfect mirror is a reflection plane");

// Left-hand mirror: their GlassRoughness at (-5, 2.5, 0) scaled (0.75, 0.5) of
// the same 10 x 10, facing +X (their +90 degrees about Y).
var rough = mirror("Rough Mirror", [-5, 2.5, 0], 90, 3.75, 2.5);
assert(material.set(rough, { baseColor: "#1a1a1a", workflow: "Specular", roughness: 0.28,
                             useFresnelColor: true, separateFresnel: false,
                             fresnelColor: srgbHex(0.6, 0.6, 0.6) }),
       "the rough mirror: their GlassRoughness");
assert(node.setPlanarReflector(rough, true), "the rough mirror is a reflection plane");

// TWO PLANES, TWO RENDERS: the budget is the cost, and it is the sample.
var pr = world.setPlanarReflections({ budget: 2 });
assert(pr.budget === 2, "the planar budget is 2 — two whole extra scene renders");
log("planarReflections: " + J(pr));

ogreCamera();
editor.frame(40);
var live = world.planarReflections();
log("active actors: " + live.activeActors);
assert(live.activeActors >= 1, "at least one mirror is actually rendering (" + live.activeActors + ")");

ogreFinish(ARCHIVE, PREVIEW, [{ x: 0.78, y: 0.45 }, { x: 0.5, y: 0.7 }]);
console.log("make_ogre_planarreflections: PASS");
