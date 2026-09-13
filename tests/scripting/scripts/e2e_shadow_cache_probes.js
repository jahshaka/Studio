// scripting.e2e.shadow_cache_probes — THE LAMP-MAP CACHE UNDER REFLECTION
// PROBES, WITH A COLD SHADER CACHE (ENGINE_CACHE_POLICY_SPEC P2/P4, ogre-patch
// 0025). Runs in the real app on a FRESH home (the CMake fixture wipes it), so
// every shader this scene needs is GENERATED and COMPILED in this run — which
// is the only condition under which the defect it guards is visible.
//
// THE DEFECT: Ogre rebuilds a shadow node's light list at most once per camera
// per compositor frame, and that build is the only place
// mNumActiveShadowMapCastingLights is computed — while setLightFixedToShadowMap
// (the lamp-map cache's assignment) writes the SLOT ARRAY without it. HlmsPbs
// declares hlms_num_shadow_map_lights from the COUNT and indexes shadow maps
// from the ARRAY, so a lamp fixed after a node has already built for the frame
// produces a pixel shader that references a shadow map it never declared:
// "'hlms_shadowmap3' : undeclared identifier", no shader, and — on a cold
// shader cache — a SIGSEGV in HlmsDiskCache when the cache is saved.
// A reflection probe is where it bit: probes capture from the frame's update
// half, around the cache's own per-frame work.
//
// The assertion is the engine's own self-check, world.shadowStatus()
// .shaderLightMismatches: 0 is the only healthy value. The run reaching its end
// is the other half (the crash took the process down mid-script).

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

var guid = project.create("Shadow Cache Probes " + Date.now());
assert(guid.length > 10, "project.create -> " + guid);

// AN ENCLOSURE, BECAUSE A PROBE GRID NEEDS ONE (owner probe rule, 2026-09-13).
// The renderer measures the enclosure out of the scene's own LAYOUT and builds
// no probes at all in a scene it reads as open. This suite used to say where
// the space was by PINNING the lit volume, and that pin is gone (owner decision
// D8: the volume is the renderer's automatic fit and nothing else), so the
// scene has to be a room for real. The default Ground goes with it: a 100 m
// plane under a 12 m room is the outermost slab on the floor's own axis.
function buildRoom(half, height) {
    var ids = scene.nodes();
    for (var i = 0; i < ids.length; ++i)
        if (ids[i].name === "Ground") node.setProperty(ids[i].id, "visible", false);
    function slab(name, px, py, pz, sx, sy, sz) {
        var id = scene.addPrimitive("cube", { position: { x: px, y: py, z: pz } });
        node.setProperty(id, "name", name);
        node.transform(id, { scale: { x: sx / 2, y: sy / 2, z: sz / 2 } });
        return id;
    }
    var w = half * 2;
    slab("Floor", 0, -0.25, 0, w, 0.5, w);
    slab("Roof",  0, height + 0.25, 0, w, 0.5, w);
    slab("WallW", -half, height / 2, 0, 0.5, height, w);
    slab("WallE",  half, height / 2, 0, 0.5, height, w);
    slab("WallS", 0, height / 2, -half, w, height, 0.5);
    slab("WallN", 0, height / 2,  half, w, height, 0.5);
}
// A room the probes can capture and a lamp can shade.
buildRoom(6, 5);
var box = scene.addPrimitive("cube", { position: { x: 0, y: 1, z: 0 },
                                       scale: { x: 2, y: 2, z: 2 } });
assert(box.length > 10, "the room and its box are up");
editor.frame(4);

// THE HYBRID WITH SHADOWED PROBES: the probe grid captures the room, and each
// capture instantiates the probe shadow node the cache assigns lamps into.
assert(world.gi({ mode: "vct_pcc_hybrid", quality: "low", probeShadows: true,
                  pccGrid: { x: 2, y: 1, z: 2 }, updateBudget: 4 }),
       "world.gi(hybrid + probe shadows)");
editor.frame(60);
var g = world.giStatus();
assert(g.probeCount > 0, "the probe grid exists (" + g.probeCount + " probes)");
var st = world.shadowStatus();
assert(st.shaderLightMismatches === 0,
       "no shader/light-count mismatch while the probes capture with no cached lamp (" +
       st.shaderLightMismatches + ")");

// ---- the lamp ARRIVES while the probes are already capturing --------------
var spot = scene.addLight("spot", { position: { x: -3, y: 4, z: -3 } });
node.setProperty(spot, "castShadow", true);
node.setProperty(spot, "distance", 12);
editor.frame(60);
st = world.shadowStatus();
console.log("   after the spot: casters " + st.casters + ", focusedMaps " + st.focusedMaps +
            ", cachedInstances " + st.cachedInstances + ", mismatches " + st.shaderLightMismatches);
assert(st.shaderLightMismatches === 0,
       "a lamp arriving under the probes hashes no broken pass (" + st.shaderLightMismatches + ")");
assert(st.cachedInstances > 0, "and it IS cached somewhere (" + st.cachedInstances + " instances)");

// ---- a SECOND lamp, then one of them hidden, then shown -------------------
var point = scene.addLight("point", { position: { x: 3, y: 3, z: 3 } });
node.setProperty(point, "castShadow", true);
node.setProperty(point, "distance", 12);
editor.frame(45);
node.setProperty(spot, "visible", false);
editor.frame(45);
node.setProperty(spot, "visible", true);
editor.frame(45);
st = world.shadowStatus();
console.log("   after the churn: casters " + st.casters + ", cached " + st.cachedInstances +
            ", uncached " + st.uncachedInstances + ", mismatches " + st.shaderLightMismatches);
assert(st.shaderLightMismatches === 0,
       "adding, hiding and showing lamps hashes no broken pass (" + st.shaderLightMismatches + ")");

// ---- a GI refresh re-captures every probe with the lamps cached -----------
assert(world.refreshGi(), "world.refreshGi()");
editor.frame(60);
st = world.shadowStatus();
assert(st.shaderLightMismatches === 0,
       "a full probe re-capture with cached lamps hashes no broken pass (" +
       st.shaderLightMismatches + ")");
console.log("ok: the run completed — the shader cache saved without crashing");
