// ogre_pbs_base.js — THE SHARED PRELUDE + the PBS base scene, for the ports of
// Ogre-Next's demo scenes (SPECS/OGRE_SAMPLES_TAB_SPEC.md §5.3).
//
// This file defines functions and NOTHING ELSE — it builds no scene on its own.
// The script engine has no `require`, so a port is assembled by CONCATENATION,
// in this order, with @TREE@ substituted exactly once at the end:
//
//   TREE=<absolute path to the source tree>
//   cat $TREE/scenes/tools/ogre_pbs_base.js \
//       $TREE/scenes/tools/make_ogre_pbsmaterials.js \
//     | sed "s|@TREE@|$TREE|" > /tmp/port.js
//   cd <a scratch dir>
//   DISPLAY=<your Xvfb, 1920x1080> \
//       <build>/bin/Jahshaka --script /tmp/port.js --data-root <a scratch root>
//
// ALWAYS FIRST, even for the room: ogre_room.js uses the helpers below.
// ENGINE-UP (never --headless): the previews are real frames.
//
// ---------------------------------------------------------------------------
// WHAT THE BASE SCENE IS (Showcase/PbsMaterials, Samples/2.0)
//
//   * a 50 x 50 floor at y = -1 wearing their `Marble` datablock,
//   * a 4 x 4 grid of alternating spheres and cubes at y = 2, 2.5 apart,
//     scaled 0.65 and rolled `index` RADIANS, alternating `Rocks` / `Marble`
//     by row,
//   * one directional light aimed (-1,-1,-1), two spots (warm at (-10,10,10),
//     cold at (10,10,-10)) and a hemisphere ambient,
//   * their `createAtmosphere()` sky.
//
// Three samples stand on it: PbsMaterials adds the 8 x 8 roughness x fresnel
// sphere palette, Refractions adds a refractive wall and makes the palette
// refractive, Hdr and AreaApproxLights (round 2) re-light it.
//
// ---------------------------------------------------------------------------
// THE FOUR SUBSTITUTIONS THIS HARNESS MAKES, and why (full argument and the
// measurements: ~/Developer/spikes/ogre-samples/FINDINGS.md)
//
// 1. THEIR TEXTURES ARE NOT OURS TO SHIP. `Rocks` is Rocks_Diffuse.tga +
//    Rocks_Normal.tga + Rocks_Spec.tga and `Marble` is MRAMOR6X6.jpg +
//    MRAMOR-bump.jpg, all from Samples/Media, each under its own licence. The
//    ports use OUR shipped presets — "Stone Wall PBR" for Rocks, "Marble PBR"
//    for Marble — which are the same two materials in spirit (a rough normal-
//    mapped rock, a polished veined stone) and travel inside the archive like
//    every other sample asset (the portability law).
//
// 2. AMBIENT IS A SKY LIGHT, NOT A COLOUR. They call setAmbientLight() with an
//    upper and a lower hemisphere colour; we deleted the flat ambient dial
//    (owner decision D14) and a scene's ambient IS a Sky Light reading the
//    World sky. So the port's ambient comes from its sky and is physically
//    consistent with the background, where theirs is two numbers unrelated to
//    the picture. Our ports read slightly more ambient-lit in the shadows.
//
// 3. ONE FLOOR, AND IT IS THE DEFAULT GROUND. Every new scene stands on a
//    Ground (100 m, matte, reads as infinite). Laying their 50 x 50 plane over
//    it would be two floors z-fighting, so the port MOVES the Ground to their
//    y = -1 and dresses it in Marble at their tiling (they tile 4 times across
//    50 m = one repeat per 12.5 m; our 100 m ground needs textureScale 8 for
//    the same repeat). Their floor is a 50 m plaza with an edge; ours has none.
//
// 4. RANGE IS BOTH THE CURVE AND THE REACH. Their spots call
//    setAttenuationBasedOnRadius(10, 0.01), which means "the 1/(0.5 + 0.5 d²/r²)
//    curve of radius 10" AND "cut the light off at sqrt(199) x 10 = 141 units".
//    Our engine ties the two together — the authored range IS the range
//    (OgreScene.cpp) — and the Forward+ fade ramps across [0, R]. At their
//    curve radius the spots would be dead before they reached the plaza, so the
//    port authors `distance` = SPOT_RANGE below, which keeps them alive over
//    the objects they are there to light. Their falloff is flatter than ours.
//
// INTENSITY IS NOT ARBITRARY: Ogre's `powerScale` and our `intensity` differ by
// exactly pi (OgreScene.cpp: powerScale = intensity * pi, because HlmsPbs
// divides diffuse by pi and IrisGL does not), so every light below is authored
// as theirPowerScale / pi and the two engines are handed the same number.

// ---- the prelude ----------------------------------------------------------

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function log(m) { console.log("[ogre-port] " + m); }
function J(x) { return JSON.stringify(x); }

var PI = Math.PI;

/// LINEAR -> "#rrggbb". Every colour we pick is decoded sRGB -> linear on the
/// way in (the colour-space rule, LIGHTS-2), and every number in Ogre's sample
/// sources is already LINEAR — setDiffuse, setFresnel, ColourValue::Green. So a
/// port that pasted their floats into a hex string would render a different
/// scene. This is the one conversion that makes the two sides comparable.
function srgbHex(r, g, b) {
    function enc(v) {
        v = Math.max(0, Math.min(1, v));
        var s = v <= 0.0031308 ? v * 12.92 : 1.055 * Math.pow(v, 1.0 / 2.4) - 0.055;
        var i = Math.round(s * 255);
        return (i < 16 ? "0" : "") + i.toString(16);
    }
    return "#" + enc(r) + enc(g) + enc(b);
}

/// QUATERNION -> EULER DEGREES, in the document's own convention.
/// node.transform takes euler degrees and iris::Quat::fromEulerAngles is Qt's
/// order (pitch about X, yaw about Y, roll about Z, applied as Qt applies
/// them), so this is iris::Quat::getEulerAngles transcribed — including its
/// 0.99999 gimbal branch. Round-tripping through it is exact by construction,
/// which is what lets the room's 23 authored orientations survive the port.
/// Ogre spells a quaternion (w, x, y, z) and so does this.
function quatToEuler(w, x, y, z) {
    var len = Math.sqrt(w * w + x * x + y * y + z * z);
    if (len > 1e-12) { w /= len; x /= len; y /= len; z /= len; }
    var deg = 180.0 / PI;
    var xx = x * x;
    var sinp = (y * z - w * x) * -2.0;
    var pitch, yaw, roll;
    if (Math.abs(sinp) < 0.99999) {
        pitch = Math.asin(sinp);
        yaw = Math.atan2(2.0 * (z * x + w * y), 1.0 - 2.0 * (y * y + xx));
        roll = Math.atan2(2.0 * (x * y + z * w), 1.0 - 2.0 * (z * z + xx));
    } else {
        pitch = (sinp < 0 ? -1 : 1) * (PI / 2);
        yaw = 2.0 * Math.atan2(y, w);
        roll = 0.0;
    }
    return { x: pitch * deg, y: yaw * deg, z: roll * deg };
}

function norm3(v) {
    var l = Math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    return [v[0] / l, v[1] / l, v[2] / l];
}

/// THE EULER THAT AIMS A LIGHT DOWN `dir`.
/// A document light points down its own -Y (LightNode::getLightDir), so aiming
/// one is the shortest arc from (0,-1,0) to the direction their sample calls
/// setDirection() with. Built as a quaternion and converted, rather than solved
/// as two angles, because the shortest arc has no convention to get wrong.
function eulerForDirection(dir) {
    var d = norm3(dir);
    var a = [0, -1, 0];
    var dot = a[0] * d[0] + a[1] * d[1] + a[2] * d[2];
    if (dot > 0.999999) return { x: 0, y: 0, z: 0 };
    if (dot < -0.999999) return quatToEuler(0, 1, 0, 0);   // 180 degrees about X
    var c = [a[1] * d[2] - a[2] * d[1], a[2] * d[0] - a[0] * d[2], a[0] * d[1] - a[1] * d[0]];
    var s = Math.sqrt((1 + dot) * 2);
    return quatToEuler(s * 0.5, c[0] / s, c[1] / s, c[2] / s);
}

/// Their `powerScale` as our `intensity` (see the header: they differ by pi).
function powerScale(ps) { return ps / PI; }

function removeNode(name) {
    var id = scene.find(name);
    if (id) assert(node.remove(id), "removed '" + name + "'");
    return !!id;
}

/// A port never pins a GI volume: the lit volume is automatic and a scene that
/// pinned one would be lying about the physics (owner, 2026-09-13). This is the
/// only GI call the harnesses make besides the tier.
function photonOff() {
    assert(world.photon({ enabled: false }).enabled === false, "Photon off (their sample has no GI)");
}

// ---- the shared world -----------------------------------------------------

/// The sun, the sky and the Sky Light every port shares.
/// `opts`: { sky: "realistic" | "#rrggbb", sunDir: [x,y,z], sunPower: <theirs>,
///           skyLight: <intensity> }
function ogreSky(opts) {
    if (opts.sky === "realistic") {
        assert(world.sky("realistic"), "sky: realistic (Ogre's AtmosphereNpr, same model as their createAtmosphere)");
    } else {
        assert(world.sky("color", { color: opts.sky }), "sky: colour " + opts.sky);
    }

    // THE SUN IS THE FIRST DIRECTIONAL LIGHT — a role, not a type — and the
    // template ships one. Aim it where their sample aims theirs instead of
    // adding a second (a second directional is a secondary light that casts no
    // shadow and raises a scene issue).
    var sun = world.sun().light;
    assert(!!sun, "the template's sun is there");
    node.rename(sun, "Sun");
    node.transform(sun, { rotation: eulerForDirection(opts.sunDir) });
    node.setProperty(sun, "intensity", powerScale(opts.sunPower));
    node.setProperty(sun, "lightColor", "#ffffff");
    var aimed = world.sun().direction;
    var want = norm3(opts.sunDir);
    var err = Math.abs(aimed.x - want[0]) + Math.abs(aimed.y - want[1]) + Math.abs(aimed.z - want[2]);
    assert(err < 1e-3, "the sun points " + J(want) + " (measured " + J(aimed) + ")");

    // AMBIENT IS THE SKY (substitution 2): the template's Sky Light stays and
    // only its strength is authored. 1.0 is the sky at full physical strength.
    var sl = world.skyLight();
    assert(!!sl.light, "the template's Sky Light is there");
    node.setProperty(sl.light, "intensity", opts.skyLight);
    assert(Math.abs(world.skyLight().intensity - opts.skyLight) < 1e-4,
           "Sky Light at " + opts.skyLight);
}

/// Their two spots, verbatim except for the range (substitution 4).
var SPOT_RANGE = 30.0;

function ogreSpot(name, pos, dir, colour, theirPower, halfAngleDeg) {
    var id = scene.addLight("spot", { position: { x: pos[0], y: pos[1], z: pos[2] },
                                      rotation: eulerForDirection(dir) });
    assert(!!id, "spot '" + name + "'");
    node.rename(id, name);
    node.setProperty(id, "lightColor", srgbHex(colour[0], colour[1], colour[2]));
    node.setProperty(id, "intensity", powerScale(theirPower));
    node.setProperty(id, "distance", SPOT_RANGE);
    // Ogre's outer angle is the FULL apex angle; ours is the HALF angle
    // (OgreScene.cpp doubles it). Their default spot is 40 degrees full.
    node.setProperty(id, "spotCutOff", halfAngleDeg);
    return id;
}

/// The floor (substitution 3): their 50 x 50 Marble plane, IN PLACE OF the
/// default Ground.
///
/// MEASURED, NOT ASSUMED (2026-09-14). The first cut of this harness kept the
/// Ground and dressed IT in Marble, to honour the one-floor rule the shipped
/// samples follow. It produced a seam: the Ground's companion HORIZON PLANE
/// (mirror-owned, 2 km, the thing that makes the default floor read as
/// infinite) keeps the DEFAULT floor material and does not follow a material
/// applied to the Ground, so the plaza ended at 100 m against a differently-lit
/// slab of horizon. Recorded as a defect in the FINDINGS; here the answer is
/// also the faithful one — their floor is a 50 x 50 plaza WITH AN EDGE, over
/// the sky, and that is what this builds. Our plane primitive is 2 x 2, so
/// scale 25 is 50 m, and their plane tiles its texture 4 times across it.
function ogreFloor() {
    removeNode("Ground");
    var id = scene.addPrimitive("plane", { position: { x: 0, y: -1, z: 0 },
                                           scale: { x: 25, y: 1, z: 25 } });
    node.rename(id, "Floor");
    assert(material.apply(id, "Marble PBR"), "floor: Marble PBR (their `Marble`, ours)");
    assert(material.set(id, { textureScale: [4, 4] }), "floor: their tiling (4 repeats across 50 m)");
    var size = node.size(id);
    assert(Math.abs(size.x - 50) < 0.01 && Math.abs(size.z - 50) < 0.01,
           "floor is their 50 x 50 m (" + size.x + " x " + size.z + ")");
    return id;
}

/// The 4 x 4 alternating grid. Returns the ids in their index order.
function ogreGrid() {
    var ARMS = 2.5, ids = [];
    for (var i = 0; i < 4; ++i) {
        for (var j = 0; j < 4; ++j) {
            var idx = i * 4 + j;
            var sphere = (i === j);
            // THE PRIMITIVE MAPPING (spec §4, measured): their Cube_d is
            // +-1 and so is ours, so their scale is ours; their Sphere1000 is
            // +-0.5 against our unit sphere, so ours is half theirs.
            var s = sphere ? 0.65 * 0.5 : 0.65;
            var id = scene.addPrimitive(sphere ? "sphere" : "cube", {
                position: { x: (i - 1.5) * ARMS, y: 2.0, z: (j - 1.5) * ARMS },
                // roll(idx radians) about the node's own Z
                rotation: { x: 0, y: 0, z: idx * 180.0 / PI },
                scale: { x: s, y: s, z: s } });
            node.rename(id, (sphere ? "GridSphere" : "GridCube") + idx);
            assert(material.apply(id, (i % 2 === 0) ? "Stone Wall PBR" : "Marble PBR"),
                   "grid " + idx + ": " + ((i % 2 === 0) ? "Rocks" : "Marble"));
            ids.push(id);
        }
    }
    return ids;
}

/// The whole base scene: sky, sun, floor, grid, the two spots.
/// `opts` is ogreSky's, plus nothing else — the riders add their own geometry.
function ogrePbsBase(opts) {
    ogreSky(opts);
    ogreFloor();
    ogreGrid();
    // Warm, then cold — their order, their positions, their directions.
    ogreSpot("Warm Spot", [-10, 10, 10], [1, -1, -1], [0.8, 0.4, 0.2], PI, 20);
    ogreSpot("Cold Spot", [10, 10, -10], [-1, -1, 1], [0.2, 0.4, 0.8], PI, 20);
}
