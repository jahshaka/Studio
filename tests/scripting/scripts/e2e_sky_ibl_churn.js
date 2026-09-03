// scripting.e2e.sky_ibl_churn — the regression guard for the intermittent
// SIGSEGV that showed up as "editor.screenshot(postFx=true) crashes on scenes
// with planar reflectors" (2026-09-03).
//
// WHAT IT GUARDS, so nobody weakens it by accident: swapping the sky re-builds
// the IBL/environment cubemap (OgreScene::destroyReflection +
// buildReflectionCubemapFrom). The replacement routinely lands on the freed
// TextureGpu's ADDRESS, and Ogre keys its Vulkan image-view cache
// (VulkanTextureGpuManager::mCachedTex) and its descriptor-set equality
// (DescriptorSetTexture::operator!=) on that pointer. If the old texture is
// unbound from the datablocks BEFORE it is destroyed, TextureGpuListener::Deleted
// reaches nobody, the stale descriptor set survives the swap and every later
// draw binds an image view whose VkImage is gone — an invalid descriptor write
// that segfaults inside the driver.
//
// So the shape below is deliberate and all of it matters:
//   * metallic/low-roughness materials, which actually SAMPLE texEnvProbeMap;
//   * planar reflectors, because each active mirror is a whole extra scene
//     render per frame that re-binds those descriptor sets (that is why the
//     crash was first blamed on planar reflections);
//   * a sky re-bake per iteration, which is what recycles the cubemap;
//   * offscreen post-fx screenshots, which add views, passes and allocator
//     churn between the free and the re-alloc.
// Surviving the loop with pixels still coming back IS the assertion.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

var guid = project.create("Sky IBL churn " + Date.now());
assert(guid.length > 10, "project.create -> " + guid);

world.mode({ mode: "epic" });
world.sky("realistic", { turbidity: 3, azimuth: 205, elevation: 12, detail: 256 });
world.ambientFromSky(true);
editor.frame(4);

// Two reflectors: a glossy floor and a mirror panel.
var floor = scene.addPrimitive("plane", { position: { x: 0, y: 0, z: 0 },
                                          scale: { x: 14, y: 1, z: 14 } });
material.set(floor, { baseColor: "#cfcfcf", roughness: 0.18, metallic: 0.0 });
assert(node.setPlanarReflector(floor, true), "floor armed as a reflector");

var mirror = scene.addPrimitive("cube", { position: { x: 0, y: 2.6, z: -7 },
                                          scale: { x: 6, y: 4, z: 0.15 } });
material.set(mirror, { baseColor: "#ffffff", roughness: 0.02, metallic: 1.0 });
assert(node.setPlanarReflector(mirror, true), "mirror armed as a reflector");

// Metals: these are the datablocks whose descriptor set carries texEnvProbeMap.
for (var i = 0; i < 4; i++) {
    var s = scene.addPrimitive("sphere", { position: { x: -4.5 + i * 3, y: 1.6, z: 0 },
                                           scale: { x: 1.4, y: 1.4, z: 1.4 } });
    material.set(s, { baseColor: "#ffd700", metallic: 1.0, roughness: 0.05 + i * 0.2 });
}
// A refractive pane, the other pass-sensitive material in the crashing scene.
var pane = scene.addPrimitive("cube", { position: { x: 0, y: 2.0, z: 2.5 },
                                        scale: { x: 4, y: 2.4, z: 0.2 } });
material.set(pane, { baseColor: "#e8f2f6", alphaMode: 6, alpha: 0.25,
                     roughness: 0.02, refractionStrength: 0.06 });
scene.addLight("directional", { position: { x: 0, y: 8, z: 4 } });

world.setPlanarReflections({ budget: 2 });
editor.gameView(true);
editor.frame(6);
assert(world.planarReflections().activeActors > 0,
       "planar reflections are actually rendering (" +
       world.planarReflections().activeActors + " active)");

// The loop. Each turn: a fresh Preetham bake (new sky + new IBL cubemap, the
// old one freed) and then an offscreen post-fx readback.
var sizes = [[320, 180], [400, 400], [256, 144]];
for (var k = 0; k < 6; k++) {
    world.sky("realistic", { turbidity: 2.2 + k * 0.6, azimuth: 205 + k * 7,
                             elevation: 12, detail: 256 });
    editor.frame(6);
    var d = sizes[k % sizes.length];
    var shot = editor.screenshot("sky_ibl_churn_" + k + ".png", d[0], d[1], [], true);
    assert(shot && shot.width === d[0] && shot.height === d[1],
           "post-fx shot " + k + " came back at " + d[0] + "x" + d[1]);
    // A rendered frame, not a black or blown-out one: the readback proves the
    // descriptor sets that survived the cubemap swap were still valid.
    var c = shot.center;
    assert(c.r + c.g + c.b > 0, "shot " + k + " is not pure black");
}

// Still armed and still rendering after all that churn.
editor.frame(6);
assert(world.planarReflections().activeActors > 0,
       "planar reflections still render after the churn");
assert(node.planarReflector(floor) === true, "the floor is still a reflector");

console.log("scripting.e2e.sky_ibl_churn: all checks passed");
