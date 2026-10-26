// scripting.e2e.second_project_capture (PHOTON-7 stage 10-3, R0) - A SECOND PROJECT IN ONE SESSION, SHOT AT ONCE: the
// self-test's fixture B built in a new project, shot, then built AGAIN in a second new project and shot - the shot's
// capture workspace rendering its FIRST frame against new textures. THE DEFECT (driver 595.x, RTX 4080 SUPER; the lead's
// R0): the capture's material depth (jahAtomMaterialDepth), created that frame, was only ever written by its decode
// prepass's render-pass load-op CLEAR - the classify queue drew nothing - and the next pass, the atom decode, LOADED it
// READ-ONLY. That stream is legal Vulkan, and it reset the 3D engine: Xid 13, no shader active, no page fault (Nsight
// Aftermath), VK_ERROR_DEVICE_LOST. A draw into that depth once (or a writable load) cured it; a transfer clear did not.
// The fix: a pass whose depth the next pass loads read-only seeds it with a draw in its first frame (OgreChain.cpp).
// It stays as the guard: a device loss ends the app with code 3 and fails the row.
var SETTLE = 120;
function buildFixtureB() {
    // A FREE NAME (PROJECT-NAMES-1): names are unique in a library, and a data root
    // that has run the self-test before already holds a "selftest-fixture-B".
    var g = project.create(project.nextFreeName("selftest-fixture-B"), { template: "empty" });
    if (!g || g.length < 10) throw new Error("project.create refused");

    // The sun FIRST: the realistic sky takes its sun position from the scene's sun
    // (the first directional light), so the light has to exist before the sky is
    // asked for. A fixed rotation, because a fence may not depend on a default.
    var sun = scene.addLight("directional", { position: { x: 0, y: 12, z: 0 },
                                              rotation: { x: -52, y: 28, z: 0 },
                                              intensity: 3.0 });
    if (!sun) throw new Error("scene.addLight(directional) refused");
    if (!world.sky("realistic", {}))
        throw new Error("world.sky(realistic) refused");

    // THE GLOSSY FLOOR, 60 x 60 m, lifted 2 cm so it is unambiguously above y = 0.
    var floor = scene.addPrimitive("plane", { position: { x: 0, y: 0.02, z: 0 },
                                             scale: { x: 60, y: 1, z: 60 } });
    if (!material.set(floor, { baseColor: "#d8d8d8", metallic: 1.0, roughness: 0.2 }))
        throw new Error("material.set(floor) refused");

    // SEVEN PILLARS at x = -12 .. 12 step 4, z = -2. #4 (index 3) is the emissive
    // above the clip; #7 (index 6) is the mirror.
    var cols = ["#ff3030", "#30ff30", "#3060ff", "#ffffff", "#ff30ff", "#20e0e0", "#ffffff"];
    for (var i = 0; i < 7; ++i) {
        var c = scene.addPrimitive("cube", { position: { x: -12 + i * 4, y: 1.5, z: -2 },
                                            scale: { x: 1, y: 3, z: 1 } });
        if (i === 3) {
            if (!material.set(c, { baseColor: "#101010", roughness: 0.9,
                                   emissiveColor: "#ffffff", emissiveIntensity: 3.0 }))
                throw new Error("material.set(emissive pillar) refused");
        } else if (i === 6) {
            if (!material.set(c, { baseColor: "#ffffff", metallic: 1.0, roughness: 0.0 }))
                throw new Error("material.set(mirror pillar) refused");
        } else {
            if (!material.set(c, { baseColor: cols[i], roughness: 0.6 }))
                throw new Error("material.set(pillar) refused");
        }
    }

    // THE MATTE WALL at z = -9: a receiver for the floor's bounce BEYOND cascade 0's
    // 5 m half-extent.
    var wall = scene.addPrimitive("cube", { position: { x: 0, y: 2, z: -9 },
                                           scale: { x: 12, y: 4, z: 0.3 } });
    if (!material.set(wall, { baseColor: "#f0f0f0", metallic: 0.0, roughness: 1.0 }))
        throw new Error("material.set(wall) refused");

    var ph = world.photon({ enabled: true, tier: "high" });
    if (!ph || ph.enabled !== true || ph.tier !== "high")
        throw new Error("world.photon(high) did not take: " + JSON.stringify(ph));
    // SCREEN-SPACE REFLECTIONS ON, EXPLICITLY. The row being ON is what B1 needs: an
    // SSR row that is off is honoured at every tier — no trace, no march — and the
    // `ssr` row's tier column is the WORLD MODE's, not Photon's, so a Photon tier
    // does not set it. Its VALUE shapes only B2: at High with rays the trace runs at
    // the tier's own resolution whatever lq/hq says (GiQualityFacts::reflectTrace,
    // D4-PHOTON-TIERS), and with the rays off B2 is the screen march at hq.
    var ssr = world.override({ id: "ssr", value: "hq" });
    if (world.settings().ssr.valueId !== "hq")
        throw new Error("world.override(ssr=hq) did not take: " + JSON.stringify(ssr));
    if (world.rayTracing("auto") !== "auto") throw new Error("world.rayTracing(auto) refused");
    editor.setCamera({ position: { x: 0, y: 5, z: 14 }, lookAt: { x: 0, y: 1, z: 0 } });

    // SETTLE IN FRAMES, on the fixed clock, and generously: the cascade scheduler
    // spends one cascade per frame, the incremental settle pays one injection per
    // frame after that, and the ray tier's history has its own warm-up. 300 frames
    // is many times what any of them owes.
    editor.frame(SETTLE);
}

app.resizeWindow(1280, 800);
editor.frame(12);
buildFixtureB();
var a = editor.screenshot("second_project_a.png", 256, 256, [], "viewport");
if (!a || !a.width) throw new Error("the first project's shot failed: " + app.lastError());
console.log("second_project_capture: the first project shot (" + a.settleFrames + " frames)");
editor.frame(12);
buildFixtureB();
var b = editor.screenshot("second_project_b.png", 256, 256, [], "viewport");
if (!b || !b.width) throw new Error("the second project's shot failed: " + app.lastError());
console.log("second_project_capture: the second project shot (" + b.settleFrames + " frames)");
"ok";
