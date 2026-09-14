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
/// THE PORTS' OWN TEXTURE SET (scenes/ogre/textures/, and its README says why).
///
/// Their `Rocks` and `Marble` datablocks are their media and are not ours to
/// ship, so the ports wear OUR marble and stone — but NOT by applying the
/// shipped presets: every archive is self-contained, eight of the thirteen
/// ports stand on this same base scene, and at the presets' 1024 x 1024 that
/// came to 97 MB of duplicated PNG in scenes/ogre. The set below is the same
/// three materials resampled to 512, which is ~35 MB for the folder and
/// indistinguishable at the distance these scenes are photographed from.
///
/// ONE IMAGE AT A TIME, memoised. Not a batch: an archive carries the bytes of
/// every image the PROJECT has pinned, so importing all eight up front put the
/// brick pair inside twelve archives that never project a decal — 1.3 MB each,
/// measured, for nothing.
var OGRE_TEX = {};
function ogreTex(name) {
    if (OGRE_TEX[name]) return OGRE_TEX[name];
    var g = assets.importFile(TREE + "/scenes/ogre/textures/" + name + ".png");
    assert(g && g.length > 10, "imported " + name + ".png");
    OGRE_TEX[name] = g;
    return g;
}

/// Their `Marble`, as our rows. The roughness BOUNDS are the shipped preset's
/// (mix(0.55, 0.15, sampled) — inverted on purpose, because the map is a legacy
/// SPEC map and those read bright-is-smooth).
function ogreMarble(id, tile) {
    return material.set(id, { baseColor: "#ffffff", baseColorMap: ogreTex("marble_color"),
                              metallic: 0.0, roughness: 1.0,
                              roughnessMap: ogreTex("marble_spec"),
                              roughnessLowerBound: 0.55, roughnessUpperBound: 0.15,
                              normalMap: ogreTex("marble_normal"), normalFactor: 1.0,
                              textureScale: [tile, tile] });
}

/// Their `Rocks`, as our rows (the shipped stone preset's bounds).
function ogreRocks(id, tile) {
    return material.set(id, { baseColor: "#ffffff", baseColorMap: ogreTex("stone_color"),
                              metallic: 0.0, roughness: 1.0,
                              roughnessMap: ogreTex("stone_spec"),
                              roughnessLowerBound: 0.95, roughnessUpperBound: 0.7,
                              normalMap: ogreTex("stone_normal"), normalFactor: 1.0,
                              textureScale: [tile, tile] });
}

/// `plain` builds their OTHER floor instead: the `MirrorLike` datablock of
/// IesProfiles is a plain polished surface with no maps at all, and a textured
/// floor hides exactly what that sample is about (the shape each light throws).
function ogreFloor(plain) {
    removeNode("Ground");
    var id = scene.addPrimitive("plane", { position: { x: 0, y: -1, z: 0 },
                                           scale: { x: 25, y: 1, z: 25 } });
    node.rename(id, "Floor");
    if (plain) {
        assert(material.set(id, { baseColor: "#b4b4b4", workflow: "Specular",
                                  roughness: 0.75, useFresnelColor: true,
                                  separateFresnel: false,
                                  fresnelColor: srgbHex(0.1, 0.1, 0.1) }),
               "floor: plain and polished (their `MirrorLike`)");
        return id;
    }
    // Their `Marble`, ours, at their tiling: they repeat 4 times across 50 m.
    assert(ogreMarble(id, 4), "floor: marble at their tiling (4 repeats across 50 m)");
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
            assert((i % 2 === 0) ? ogreRocks(id, 1) : ogreMarble(id, 1),
                   "grid " + idx + ": " + ((i % 2 === 0) ? "Rocks" : "Marble"));
            ids.push(id);
        }
    }
    return ids;
}

/// The whole base scene: sky, sun, floor, grid, the two spots.
/// `opts` is ogreSky's, plus:
///   grid:  false to leave the 4 x 4 grid out (Tutorial_SSAO and IesProfiles
///          stand on the floor alone)
///   spots: false to leave the two spots out (AreaApproxLights replaces them
///          with area lights in the same places)
function ogrePbsBase(opts) {
    ogreSky(opts);
    ogreFloor();
    if (opts.grid !== false) ogreGrid();
    if (opts.spots === false) return;
    // Warm, then cold — their order, their positions, their directions.
    ogreSpot("Warm Spot", [-10, 10, 10], [1, -1, -1], [0.8, 0.4, 0.2], PI, 20);
    ogreSpot("Cold Spot", [10, 10, -10], [-1, -1, 1], [0.2, 0.4, 0.8], PI, 20);
}

/// THE 8 x 8 SPHERE PALETTE that four of their samples put in front of the base
/// scene: roughness sweeps one axis, F0 (fresnel) the other, every sphere green.
///
/// It is the same loop in PbsMaterials, Hdr, Refractions, AreaApproxLights and
/// Tutorial_SSAO, with only the SPACING, the SPHERE SIZE and a per-sample
/// material tweak changing — so it is one function with those three as options
/// rather than five copies with a subtle difference each.
///
///   opts.prefix   node-name prefix ("Palette" -> Palette_r3_f5)
///   opts.spacing  their armsLengthSphere (1.0 in PbsMaterials, 2.0 in SSAO)
///   opts.scale    OUR sphere scale — their Sphere1000 is +-0.5 against our
///                 unit sphere, so this is HALF their meshScale
///   opts.altScale optional second scale used where x == z (SSAO's 2 vs 3)
///   opts.y        their height (1.0 everywhere so far)
///   opts.extra    extra material rows merged into every sphere (Hdr's brdf,
///                 Refractions' alphaMode)
///   opts.n        grid size (8)
///
/// F0 IS ENCODED, NOT PASTED: their setFresnel(Vector3(f), false) is a LINEAR
/// F0 and our fresnelColor is decoded sRGB -> linear, so srgbHex() stands
/// between them (see the header).
function ogrePalette(opts) {
    var n = opts.n || 8;
    var spacing = opts.spacing === undefined ? 1.0 : opts.spacing;
    var y = opts.y === undefined ? 1.0 : opts.y;
    var start = (n - 1) / 2.0;
    var ids = [];
    for (var x = 0; x < n; ++x) {
        for (var z = 0; z < n; ++z) {
            var rough = Math.max(0.02, x / Math.max(1, n - 1));
            var f0 = z / Math.max(1, n - 1);
            var s = (opts.altScale !== undefined && x === z) ? opts.altScale : opts.scale;
            var id = scene.addPrimitive("sphere", {
                position: { x: spacing * x - start * spacing, y: y, z: spacing * z - start * spacing },
                scale: { x: s, y: s, z: s } });
            node.rename(id, (opts.prefix || "Palette") + "_r" + x + "_f" + z);
            var rows = { baseColor: "#00ff00",
                         workflow: "Specular",
                         roughness: rough,
                         useFresnelColor: true,
                         separateFresnel: false,
                         fresnelColor: srgbHex(f0, f0, f0) };
            if (opts.extra) for (var k in opts.extra) rows[k] = opts.extra[k];
            assert(material.set(id, rows),
                   (opts.prefix || "Palette") + " " + x + "," + z +
                   ": roughness " + rough.toFixed(3) + " F0 " + f0.toFixed(3));
            if (opts.noShadow) node.setCastShadow(id, false);
            ids.push(id);
        }
    }
    assert(ids.length === n * n, (n * n) + " palette spheres");
    return ids;
}

/// One AREA light, their way: a rectangle of `w` x `h` at `pos` facing `dir`.
/// AREA LIGHTS NEVER CAST SHADOWS — on either side, an engine rule and not a
/// setting — and only the fast approximation samples a mask texture.
function ogreAreaLight(name, pos, dir, colour, theirPower, w, h) {
    var id = scene.addLight("area", { position: { x: pos[0], y: pos[1], z: pos[2] },
                                      rotation: eulerForDirection(dir) });
    assert(!!id, "area light '" + name + "'");
    node.rename(id, name);
    node.setProperty(id, "lightColor", srgbHex(colour[0], colour[1], colour[2]));
    node.setProperty(id, "intensity", powerScale(theirPower));
    node.setProperty(id, "distance", SPOT_RANGE);
    node.setProperty(id, "rectWidth", w);
    node.setProperty(id, "rectHeight", h);
    node.setProperty(id, "accurate", false);   // their LT_AREA_APPROX
    return id;
}

/// One POINT light, their way.
function ogrePointLight(name, pos, colour, theirPower) {
    var id = scene.addLight("point", { position: { x: pos[0], y: pos[1], z: pos[2] } });
    assert(!!id, "point light '" + name + "'");
    node.rename(id, name);
    node.setProperty(id, "lightColor", srgbHex(colour[0], colour[1], colour[2]));
    node.setProperty(id, "intensity", powerScale(theirPower));
    node.setProperty(id, "distance", SPOT_RANGE);
    return id;
}

/// The saved camera every port shares unless it says otherwise: THEIR default.
/// GraphicsSystem puts the camera at (0, 5, 15) looking at the origin with
/// Ogre::Frustum's default 45-degree VERTICAL fov, and editor.setCamera's `fov`
/// is vertical degrees too — a literal match at 16:9, which is the rig's
/// resolution law.
function ogreCamera(pos, look, fov) {
    var p = pos || [0, 5, 15], l = look || [0, 0, 0];
    editor.setCamera({ position: { x: p[0], y: p[1], z: p[2] },
                       lookAt: { x: l[0], y: l[1], z: l[2] }, fov: fov || 45 });
    editor.select(null);
}

/// Save, shoot the preview at the saved camera, export. The tail every port
/// repeats: Game View (the tile is the SCENE, not the editor), a frame COUNT
/// rather than a wall-clock settle (this engine has no wall clock), the
/// editor's own grade.
function ogreFinish(archive, preview, probes) {
    assert(project.save(), "saved");
    editor.gameView(true);
    editor.frame(60);
    var pts = probes || [{ x: 0.5, y: 0.5 }];
    var shot = editor.screenshot(preview, 1280, 720, pts, "scene");
    log("preview centre: " + J(shot.center) + " probes " + J(shot.probes));
    // THE PROBES, NOT THE CENTRE PIXEL. A port's centre can legitimately land
    // on a shadowed wall (IesProfiles did); what has to be true is that the
    // PICTURE carries light, so this reads the probe set the port chose.
    var lit = 0;
    for (var i = 0; i < shot.probes.length; ++i) {
        var q = shot.probes[i];
        lit = Math.max(lit, (q.r + q.g + q.b) / 3);
    }
    assert(lit > 6, "the preview carries light (brightest probe " + Math.round(lit) + ")");
    editor.gameView(false);
    var out = project.exportArchive(archive);
    assert(out && out.assets > 0, "exported " + archive + " (" + J(out) + ")");
    assert(project.close(), "closed");
    return shot;
}
