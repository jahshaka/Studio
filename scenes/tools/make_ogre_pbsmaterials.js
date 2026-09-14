// make_ogre_pbsmaterials.js — the PORT of Ogre-Next's Showcase/PbsMaterials,
// as a Jahshaka scene (SPECS/OGRE_SAMPLES_TAB_SPEC.md §3 row 1).
//
// Run it, do not hand-edit the archive (the harness header has the full recipe):
//
//   TREE=<absolute path to the source tree>
//   cat $TREE/scenes/tools/ogre_pbs_base.js \
//       $TREE/scenes/tools/make_ogre_pbsmaterials.js \
//     | sed "s|@TREE@|$TREE|" > /tmp/port.js
//   cd <a scratch dir>
//   DISPLAY=<your Xvfb, 1920x1080> \
//       <build>/bin/Jahshaka --script /tmp/port.js --data-root <a scratch root>
//
// WHAT THE SAMPLE IS FOR. It is Ogre's materials showcase: the base scene (a
// marble plaza, a 4 x 4 grid of rock and marble solids) with an 8 x 8 palette
// of spheres in front of it that sweeps ROUGHNESS along one axis and FRESNEL
// (F0, the specular reflectance at normal incidence) along the other. Read
// left to right the highlights broaden; read front to back the whole sphere
// turns into a mirror. One picture that says what the two numbers mean.
//
// WHAT THIS PORT CANNOT SHOW, and why (the tile says the same):
//
//   * THE BRDF CYCLE. Their F4 key walks six BRDFs — Default, CookTorrance,
//     BlinnPhong, BlinnPhongLegacyMath, BlinnPhongFullLegacy,
//     DefaultUncorrelated. Our material's `brdf` row offers six too, but only
//     THREE are the same three (Default, CookTorrance, BlinnPhong); the other
//     three of ours are the separate-diffuse-fresnel variants. The port is
//     authored on Default, which is both engines' default.
//   * THE REFLECTION CUBE. Their palette reflects SaintPetersBasilica.dds, a
//     cubemap from their media. Ours reflects THE SKY, through the same
//     HlmsPbs reflection slot — the sky capture is our environment map, so
//     the sweep reads the same and the picture in the mirrors is our sky
//     rather than their basilica.
//   * DETAIL MAPS. Their `Rocks`/`Marble` are plain; their AllSettings
//     datablock (which nothing in the scene uses) is the one with four detail
//     layers. We have detail layers (material.setDetail, two of the renderer's
//     four) and this port does not need them.
//   * THE ANIMATION. Their F2 spins the 4 x 4 grid. A port is a saved scene;
//     the grid stands still at the roll angles their sample starts from.

var TREE = "@TREE@";
var ARCHIVE = TREE + "/scenes/ogre/PbsMaterials.zip";
var PREVIEW = TREE + "/scenes/ogre/preview/pbsmaterials.png";

assert(project.create("PBS Materials").length > 0, "created the project");

// ---- the base scene -------------------------------------------------------
// Their sky IS an atmosphere (createAtmosphere(light)) and so is ours now —
// world.sky("realistic") runs on Ogre's own AtmosphereNpr since the SKY-GPU
// merge (2026-09-14), so this row of the spec's table ("ours is a Preetham
// bake") is history: the two samples now ask the SAME sky model for their
// background, driven by the same sun direction.
ogrePbsBase({ sky: "realistic", sunDir: [-1, -1, -1], sunPower: 1.0, skyLight: 1.0 });

// GI OFF, like the original. Their sample has no global illumination at all —
// direct light plus an ambient term — so the port turns Photon off rather than
// inheriting the template's Epic tier. What the spheres reflect is the sky's
// own capture, which is not GI.
photonOff();

// ---- the 8 x 8 roughness x fresnel palette --------------------------------
// Theirs: 64 datablocks, diffuse (0,1,0), roughness = max(0.02, x/(numX-1)),
// fresnel = z/(numZ-1) set as F0 DIRECTLY (setFresnel(v, false) — the second
// argument is "separate per channel", and false means one scalar F0).
//
// Ours, row for row: baseColor #00ff00 (which DECODES to their linear (0,1,0)),
// the SPECULAR workflow — F0 is only read there, the Metallic workflow derives
// it from metalness — with useFresnelColor + separateFresnel false, which is
// exactly their (v, false).
var PAL_N = 8;
var palette = [];
for (var px = 0; px < PAL_N; ++px) {
    for (var pz = 0; pz < PAL_N; ++pz) {
        var rough = Math.max(0.02, px / (PAL_N - 1));
        var f0 = pz / (PAL_N - 1);
        var id = scene.addPrimitive("sphere", {
            position: { x: px - 3.5, y: 1.0, z: pz - 3.5 },
            scale: { x: 0.5, y: 0.5, z: 0.5 } });     // their Sphere1000 at scale 1
        node.rename(id, "Palette_r" + px + "_f" + pz);
        assert(material.set(id, {
                   baseColor: "#00ff00",
                   workflow: "Specular",
                   roughness: rough,
                   useFresnelColor: true,
                   separateFresnel: false,
                   fresnelColor: srgbHex(f0, f0, f0) }),
               "palette " + px + "," + pz + ": roughness " + rough.toFixed(3) + " F0 " + f0.toFixed(3));
        palette.push(id);
    }
}
assert(palette.length === 64, "64 palette spheres");

// The sweep is the whole sample: prove the two axes really landed on the
// document rather than on one shared material instance.
var lo = material.get(scene.find("Palette_r0_f0"));
var hi = material.get(scene.find("Palette_r7_f7"));
assert(Math.abs(lo.roughness - 0.02) < 1e-3 && Math.abs(hi.roughness - 1.0) < 1e-3,
       "the roughness axis sweeps 0.02 -> 1.0 (" + lo.roughness + " -> " + hi.roughness + ")");
assert(lo.fresnelColor === "#000000" && hi.fresnelColor === "#ffffff",
       "the fresnel axis sweeps F0 0 -> 1 (" + lo.fresnelColor + " -> " + hi.fresnelColor + ")");
// material.get reports an ENUM ROW AS ITS ORDINAL (1 = Specular in
// PbrMaterial::workflowNames()), where node.property reports enum rows by NAME
// — an inconsistency on the read side only; the WRITE above took the name.
assert(lo.workflow === 1 || lo.workflow === "Specular",
       "the palette is on the Specular workflow (F0 is read there): " + lo.workflow);

// ---- the saved camera -----------------------------------------------------
// THEIR CAMERA, exactly: GraphicsSystem puts it at (0, 5, 15) looking at the
// origin with Ogre::Frustum's default 45-degree VERTICAL fov, and
// editor.setCamera's `fov` is vertical degrees too — so 45 is a literal match
// at 16:9, which is the rig's resolution law (1920x1080).
editor.setCamera({ position: { x: 0, y: 5, z: 15 }, lookAt: { x: 0, y: 0, z: 0 }, fov: 45 });
editor.select(null);
editor.frame(30);
log("giStatus: " + J(world.giStatus()));

assert(project.save(), "saved");

// ---- the shipped preview --------------------------------------------------
editor.gameView(true);
editor.frame(60);
var shot = editor.screenshot(PREVIEW, 1280, 720,
                             [{ x: 0.5, y: 0.62 }, { x: 0.2, y: 0.2 }], "scene");
log("preview centre: " + J(shot.center) + " probes " + J(shot.probes));
assert(shot.center.r + shot.center.g + shot.center.b > 12, "the preview is not black");
editor.gameView(false);

// ---- the archive ----------------------------------------------------------
var out = project.exportArchive(ARCHIVE);
assert(out && out.assets > 0, "exported " + ARCHIVE + " (" + J(out) + ")");
assert(project.close(), "closed");
console.log("make_ogre_pbsmaterials: PASS");
