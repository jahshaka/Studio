// make_ogre_refractions.js — the PORT of Ogre-Next's ApiUsage/Refractions
// (SPECS/OGRE_SAMPLES_TAB_SPEC.md §3 row 3).
//
//   TREE=<absolute path to the source tree>
//   cat $TREE/scenes/tools/ogre_pbs_base.js \
//       $TREE/scenes/tools/make_ogre_refractions.js \
//     | sed "s|@TREE@|$TREE|" > /tmp/port.js
//   cd <a scratch dir>
//   DISPLAY=<your Xvfb, 1920x1080> \
//       <build>/bin/Jahshaka --script /tmp/port.js --data-root <a scratch root>
//
// WHAT THE SAMPLE IS FOR. The PBS base scene again, with everything in front of
// it made REFRACTIVE: a thin glass wall standing on edge through the middle,
// and the 8 x 8 sphere palette turned into glass balls. Refraction is a
// separate render pass over a copy of the frame (their
// Refractions.compositor), which is why the sample exists at all — it is the
// one PBS feature that needs the compositor to cooperate.
//
// WHAT THIS PORT CANNOT SHOW (the tile carries no note — nothing is missing):
//
//   * THE TRANSPARENCY SLIDER. Their +/- keys sweep every refractive material's
//     transparency live and their F2 cycles the refraction modes. Ours is the
//     material's Alpha row, authored at their starting 0.15.
//   * THE RENDER-QUEUE ORDER. They push the wall to render queue 200 and the
//     spheres after it, by hand, to avoid sorting artefacts. Our renderer owns
//     queue assignment; a port has no verb for it and needs none — the
//     artefact their comment describes does not appear here.
//   * THEIR NORMAL MAP. The wall's ripple comes from floor_bump.PNG in their
//     media; ours comes from the normal map of OUR shipped stone preset,
//     imported through the one pipeline so it travels inside the archive.

var TREE = "@TREE@";
var ARCHIVE = TREE + "/scenes/ogre/Refractions.zip";
var PREVIEW = TREE + "/scenes/ogre/preview/refractions.png";
var BUMP = TREE + "/app/content/materials/presets/stone/Stone_04_UV_H_CM_1_NRM.png";

assert(project.create("Refractions").length > 0, "created the project");

// ---- the base scene -------------------------------------------------------
// Identical to PbsMaterials: their RefractionsGameState::createScene01 is that
// file's createScene01 with the refractive objects inserted.
ogrePbsBase({ sky: "realistic", sunDir: [-1, -1, -1], sunPower: 1.0, skyLight: 1.0 });
photonOff();

// REFRACTIONS ON, explicitly. The World Mode row resolves to "Auto" by default,
// which is the renderer deciding; this scene is ABOUT the pass, so it pins it.
assert(world.override({ id: "refractions", value: "on" }).valueId === "on",
       "refractions pinned on (their Refractions.compositor, as a scene row)");

// The one texture this port imports: a normal map for the glass wall, so the
// refraction has something to bend (their comment: "assign a normal map so the
// refractions are much more visually pleasing (and obvious)").
var bump = assets.importFile(BUMP);
assert(bump && bump.length > 10, "imported the wall's normal map -> " + bump);

// ---- the refractive wall --------------------------------------------------
// Their Cube_d at (0, 2, 0) scaled (0.05, 2, 3.75) — a 0.1 x 4 x 7.5 m pane
// standing on edge across the scene. Scale maps 1:1 (both cubes are +-1).
var wall = scene.addPrimitive("cube", { position: { x: 0, y: 2, z: 0 },
                                        scale: { x: 0.05, y: 2, z: 3.75 } });
node.rename(wall, "Glass Wall");
assert(material.set(wall, {
           baseColor: "#ffffff",
           workflow: "Specular",
           roughness: 0.1,
           useFresnelColor: true,
           separateFresnel: false,
           fresnelColor: srgbHex(0.5, 0.5, 0.5),   // their setFresnel(0.5, false)
           normalMap: bump,
           alphaMode: "Refractive",
           alpha: 0.15,                            // their setTransparency(0.15, Refractive)
           refractionStrength: 0.2 }),             // their setRefractionStrength(0.2)
       "the glass wall is refractive at 15% with their F0 and strength");
// "Do not cast shadows!" — their comment, and the same call here.
assert(node.setCastShadow(wall, false) !== undefined, "the wall casts no shadow, like theirs");

// ---- the refractive palette ----------------------------------------------
// The PbsMaterials palette, made of glass: same 8 x 8 roughness x fresnel
// sweep, every sphere refractive at 0.15 and casting no shadow.
var PAL_N = 8;
for (var px = 0; px < PAL_N; ++px) {
    for (var pz = 0; pz < PAL_N; ++pz) {
        var rough = Math.max(0.02, px / (PAL_N - 1));
        var f0 = pz / (PAL_N - 1);
        var id = scene.addPrimitive("sphere", {
            position: { x: px - 3.5, y: 1.0, z: pz - 3.5 },
            scale: { x: 0.5, y: 0.5, z: 0.5 } });
        node.rename(id, "Glass_r" + px + "_f" + pz);
        assert(material.set(id, {
                   baseColor: "#00ff00",
                   workflow: "Specular",
                   roughness: rough,
                   useFresnelColor: true,
                   separateFresnel: false,
                   fresnelColor: srgbHex(f0, f0, f0),
                   alphaMode: "Refractive",
                   alpha: 0.15 }),
               "glass " + px + "," + pz + ": roughness " + rough.toFixed(3) + " F0 " + f0.toFixed(3));
        node.setCastShadow(id, false);
    }
}
var probe = material.get(scene.find("Glass_r3_f3"));
assert(probe.alphaMode === 6 || probe.alphaMode === "Refractive",
       "the palette is refractive (alphaMode " + probe.alphaMode + ")");
assert(Math.abs(probe.alpha - 0.15) < 1e-3, "at their 15% transparency (" + probe.alpha + ")");

// ---- the saved camera -----------------------------------------------------
editor.setCamera({ position: { x: 0, y: 5, z: 15 }, lookAt: { x: 0, y: 0, z: 0 }, fov: 45 });
editor.select(null);
editor.frame(30);

assert(project.save(), "saved");

// ---- the shipped preview --------------------------------------------------
editor.gameView(true);
editor.frame(60);
var shot = editor.screenshot(PREVIEW, 1280, 720,
                             [{ x: 0.5, y: 0.55 }, { x: 0.3, y: 0.5 }], "scene");
log("preview centre: " + J(shot.center) + " probes " + J(shot.probes));
assert(shot.center.r + shot.center.g + shot.center.b > 12, "the preview is not black");
editor.gameView(false);

// ---- the archive ----------------------------------------------------------
var out = project.exportArchive(ARCHIVE);
assert(out && out.assets > 0, "exported " + ARCHIVE + " (" + J(out) + ")");
assert(project.close(), "closed");
console.log("make_ogre_refractions: PASS");
