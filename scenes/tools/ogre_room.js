// ogre_room.js — THE SHARED ROOM: Ogre-Next's LocalCubemapScene, as Jahshaka
// scene data plus the builder that places it (SPECS/OGRE_SAMPLES_TAB_SPEC.md
// §5.3). Three of their samples stand in this room — LocalCubemaps,
// ScreenSpaceReflections and InstantRadiosity — so it is authored once here.
//
// CONCATENATE IT AFTER ogre_pbs_base.js, which carries the prelude (assert,
// srgbHex, quatToEuler, eulerForDirection, powerScale, ogreSky, ogreSpot):
//
//   cat $TREE/scenes/tools/ogre_pbs_base.js \
//       $TREE/scenes/tools/ogre_room.js \
//       $TREE/scenes/tools/make_ogre_localcubemaps.js \
//     | sed "s|@TREE@|$TREE|" > /tmp/port.js
//
// ---------------------------------------------------------------------------
// THE DATA BELOW IS TRANSCRIBED, NOT RETYPED. It was extracted mechanically
// from Samples/2.0/ApiUsage/LocalCubemaps/LocalCubemapScene.h — 23 createItem
// calls, every one a Cube_d box, every one SCENE_STATIC, across four
// datablocks (Green x13, Cream x6, Blue x3, Red x1) — and rounded to six
// significant digits, which turns their float epsilons (7.5e-08 and friends)
// into the exact zeros they mean. `p` is setPosition, `s` is setScale, `q` is
// setOrientation IN OGRE'S ORDER (w, x, y, z), `m` is the datablock.
//
// THE GEOMETRY MAPS 1:1 (spec §4, measured): their Cube_d is +-1 on every axis
// and so is our `cube` primitive, and `scale` multiplies HALF extents on both
// sides — so their scale numbers are ours, untouched. The orientations are
// quaternions and our node.transform takes euler degrees, which is what
// quatToEuler (ogre_pbs_base.js) is for; it is iris::Quat::getEulerAngles
// transcribed, so the round trip is exact.
//
// WHAT THE ROOM IS. A 12 x 13 x 6 m sealed interior — two chambers joined by a
// doorway — with a deliberately reflective floor and walls, lit by one
// directional light through the opening and two wide spots. Their point is
// that a cubemap probe captured in one chamber must not leak into the other,
// which is what parallax-corrected probes buy. It is a room a person fits in
// (the scene-scale convention, owner 2026-09-08) and it stands as a scene.
//
// THEIR FOUR DATABLOCKS, verbatim: setBackgroundDiffuse(<colour>),
// setFresnel(0.1, false) and setRoughness(0.65). "Background diffuse" with no
// diffuse texture is simply the base colour; F0 0.1 read as a scalar is the
// SPECULAR workflow with useFresnelColor and separateFresnel false — the same
// pair of rows the PbsMaterials palette sweeps. Their ColourValue constants are
// LINEAR, so they are encoded to sRGB on the way in (srgbHex), never pasted.
var OGRE_ROOM_BOXES = [
    { p: [-0.505, -0.499984, 5.005], s: [0.5, 10.185, 6.00511], q: [0.5, 0.5, 0.5, 0.5], m: "Cream" },
    { p: [-6.01, 6.61, -0.570189], s: [0.5, 0.19, 0.740971], q: [0, -0, 1, 0], m: "Green" },
    { p: [-6.01, 6.61, 3.02981], s: [0.5, 0.19, 0.593249], q: [0, -0, 1, 0], m: "Green" },
    { p: [-6.01, 6.61, 6.62981], s: [0.5, 0.19, 0.593249], q: [0, -0, 1, 0], m: "Green" },
    { p: [3.03, 7.3, 5.49], s: [0.5, 2.47, 2.97253], q: [0, 0.707107, 0.707107, -0], m: "Cream" },
    { p: [-4.287, 7.3, 5.49], s: [0.5, 2.223, 2.97253], q: [0, 0.707107, 0.707107, -0], m: "Cream" },
    { p: [-0.5, 7.3, -1.228], s: [0.5, 3.952, 6.00511], q: [0.5, 0.5, 0.5, 0.5], m: "Cream" },
    { p: [-0.5, 7.3, 11.732], s: [0.5, 3.458, 6.00511], q: [0.5, 0.5, 0.5, 0.5], m: "Cream" },
    { p: [-6.01, 3.61, -0.570189], s: [0.5, 0.19, 0.593249], q: [0, -0, 1, 0], m: "Green" },
    { p: [-6.01, 3.61, 3.02981], s: [0.5, 0.19, 0.593249], q: [0, -0, 1, 0], m: "Green" },
    { p: [-6.01, 3.61, 6.62981], s: [0.5, 0.19, 0.593249], q: [0, -0, 1, 0], m: "Green" },
    { p: [-0.505116, 3.40002, 14.69], s: [0.5, 3.40002, 6.00511], q: [0.707107, -0, 0.707107, 0], m: "Cream" },
    { p: [-6.01, 0.189999, 6.62981], s: [0.5, 0.19, 0.593249], q: [0, -0, 1, 0], m: "Green" },
    { p: [-6.01, 0.189999, 3.02981], s: [0.5, 0.19, 0.593249], q: [0, -0, 1, 0], m: "Green" },
    { p: [-6.01, 0.189999, -0.570189], s: [0.5, 0.19, 0.593249], q: [0, -0, 1, 0], m: "Green" },
    { p: [-6.01, 3.40001, 10.7171], s: [0.5, 3.40002, 3.49392], q: [0, -0, 1, 0], m: "Green" },
    { p: [-6.01, 3.40001, 4.828], s: [0.5, 3.40002, 1.2048], q: [0, -0, 1, 0], m: "Green" },
    { p: [-6.01, 3.40001, 1.228], s: [0.5, 3.40002, 1.2048], q: [0, -0, 1, 0], m: "Green" },
    { p: [-6.01, 3.40001, -3.172], s: [0.5, 3.40002, 2.008], q: [0, -0, 1, 0], m: "Green" },
    { p: [-0.008, 4.92258, -4.68], s: [0.5, 1.88258, 0.5016], q: [0.707107, -0, 0.707107, 0], m: "Blue" },
    { p: [-3.008, 3.40002, -4.68], s: [0.5, 3.40002, 2.508], q: [0.707107, -0, 0.707107, 0], m: "Blue" },
    { p: [2.992, 3.40002, -4.68], s: [0.5, 3.40002, 2.508], q: [0.707107, -0, 0.707107, 0], m: "Blue" },
    { p: [5, 3.40002, 5.00502], s: [0.5, 3.40002, 9.18502], q: [1, 0, 0, 0], m: "Red" },
];

/// Their four datablocks, as our material rows (see the header).
var OGRE_ROOM_MATERIALS = {
    Red:   { baseColor: "#ff0000" },
    Green: { baseColor: "#00ff00" },
    Blue:  { baseColor: "#0000ff" },
    Cream: { baseColor: "#ffffff" }
};

/// Their roughness is 0.65 in LocalCubemaps and 0.02 in ScreenSpaceReflections —
/// the SAME room, polished, because a screen-space reflection needs a mirror to
/// be visible in. It is the only material difference between those two samples.
function ogreRoomMaterial(id, which, roughness) {
    var m = OGRE_ROOM_MATERIALS[which];
    return material.set(id, {
        baseColor: m.baseColor,
        workflow: "Specular",
        roughness: roughness === undefined ? 0.65 : roughness,
        useFresnelColor: true,
        separateFresnel: false,
        fresnelColor: srgbHex(0.1, 0.1, 0.1) });
}

/// Places the 23 boxes. Returns their ids in the source order.
///
/// STATIC, LIKE THEIRS. Every box is SCENE_STATIC in their scene and nothing
/// in the room ever moves, so each carries mobility "static" — which is the
/// classification the renderer reads to keep a room's stored lighting
/// (REALTIME_REFLECTIONS_SPEC §3.3) and the honest answer for a wall.
function ogreRoom(roughness) {
    var ids = [];
    for (var i = 0; i < OGRE_ROOM_BOXES.length; ++i) {
        var b = OGRE_ROOM_BOXES[i];
        var e = quatToEuler(b.q[0], b.q[1], b.q[2], b.q[3]);
        var id = scene.addPrimitive("cube", {
            position: { x: b.p[0], y: b.p[1], z: b.p[2] },
            rotation: e,
            scale: { x: b.s[0], y: b.s[1], z: b.s[2] } });
        node.rename(id, "Room" + (i < 10 ? "0" : "") + i + "_" + b.m);
        assert(ogreRoomMaterial(id, b.m, roughness), "room box " + i + ": " + b.m);
        node.setProperty(id, "mobility", "static");
        ids.push(id);
    }
    assert(ids.length === 23, "23 boxes, their whole room");
    return ids;
}

/// The room's lights: their directional through the opening and two 80-degree
/// spots. Positions, directions and colours are theirs; the range is ours
/// (substitution 4 in ogre_pbs_base.js).
function ogreRoomLights() {
    ogreSpot("Warm Spot", [-12, 6, 8], [1.5, -1, -0.5], [0.8, 0.4, 0.2], PI, 40);
    ogreSpot("Cold Spot", [2, 6, -3], [-0.5, -1, 0.5], [0.2, 0.4, 0.8], PI, 40);
}

/// The room's own measured extent, for the probe grid and the camera. Read off
/// the document rather than transcribed, so it follows the data above.
function ogreRoomBounds() {
    var b = scene.bounds();
    log("room bounds " + J(b.min) + " .. " + J(b.max) + "  size " + J(b.size));
    return b;
}
