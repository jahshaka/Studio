// script.trace_rays_mask — engine.gi.traceRays's INSTANCE MASK IS ITS BITS (SCRIPT-IN-ENGINE-1 2f, the train's card: P4's
// red). Run by jahengine (the engine's own script host, no Studio, no Qt).
//
// THE LAW: Scene::traceRays reads each ray's 9th float as the unsigned word its BITS spell (the engine's own row,
// test_gi_rayquery.cpp:111-121, memcpy's the mask into the float); the command takes the mask as an unsigned integer
// 0..255 and copies its bits the same way. So a ray straight down from y 5 onto a floor whose top is y 0 HITS at exactly
// 5 m with mask 31 (bits 0-4: every near-field class and the far copies' bit, the near field traced) - the geometry is
// exact and the traversal's float precision the only slack (a millimetre, test_gi_rayquery's own bar).
// THE CONTROL: the same ray with mask 0 names no instance class and MISSES (distance < 0) - the bits are read, not a
// value that happens to pass. RED ONCE (kept: spikes/script-in-engine-1/red-once/script.trace_rays_mask.by_value.txt):
// the command's by-value conversion put back (31 -> 31.0f, bits 0x41F80000: mask & 0xFF = 0) - the ray misses.
// THE FIXTURE: a 20 x 0.2 x 20 floor (its top at y 0), a sun, GI vct low (a ray tier: the scene's acceleration structure
// is built by the frames drawn); 4 frames, then the trace.

const view = engine.view.createOffscreen({name: "tracemask", width: 64, height: 64, background: [0, 0, 0, 1]});
const scene = engine.scene.create("tracemask");
engine.view.setScene(view, {scene});
engine.scene.setAmbient(scene, {upper: [0.2, 0.2, 0.2, 1], lower: [0.2, 0.2, 0.2, 1]});
const floor = engine.node.create(scene);
host.check({ok: engine.node.attachMesh(scene, floor, {mesh: engine.mesh.unitCube(scene),
                                                      material: engine.material.createPbr(scene, {roughness: 0.9})}),
            what: "the floor exists"});
engine.node.setTransform(scene, floor, {position: [0, -0.1, 0], scale: [20, 0.2, 20]});
const sun = engine.node.create(scene);
engine.node.setTransform(scene, sun, {rotation: [0, 0, Math.sin(Math.PI / 8), Math.cos(Math.PI / 8)]});
engine.light.set(scene, sun, {type: "directional", colour: [1, 1, 1, 1], intensity: 2});
engine.view.lookAt(view, {eye: [5, 3, 5], target: [0, 0, 0]});
host.check({ok: engine.gi.set(scene, {mode: "vct", quality: "low"}), what: "GI builds (a ray tier)"});
engine.frames(4);

const ray = mask => [3, 5, 3, 0, 0, -1, 0, 100, mask, 0, 0, 0];
const hit = engine.gi.traceRays(scene, {rays: ray(31)});
host.check({ok: hit !== null && Math.abs(hit.hits[0] - 5) < 0.001,
            what: "mask 31: the ray down from y 5 hits the floor's top at 5 m within a millimetre (" +
                  (hit ? JSON.stringify(hit.hits) : host.lastError()) + ")"});
const none = engine.gi.traceRays(scene, {rays: ray(0)});
host.check({ok: none !== null && none.hits[0] < 0,
            what: "THE CONTROL, mask 0: the same ray names no instance class and misses (" +
                  (none ? JSON.stringify(none.hits) : host.lastError()) + ")"});
