// MEASURING ARM (SCRIPT-IN-ENGINE-1, the lead's last item (b)): MANY CALLS PER FRAME as a script - 200 nodes given a
// transform every frame for 300 frames; the calls' own time (performance.now around the 200 calls) and the frames'.
// Its C++ twin: measure_calls.cpp (the same scene, the same calls through Scene::setNodeTransform). Never registered.
const N = 200, F = 300;
const view = engine.view.createOffscreen({name: "calls", width: 256, height: 144});
const scene = engine.scene.create("calls");
engine.view.setScene(view, {scene});
const cube = engine.mesh.unitCube(scene);
const mat = engine.material.createPbr(scene, {albedo: [0.8, 0.8, 0.8]});
const nodes = [];
for (let i = 0; i < N; ++i) {
    const n = engine.node.create(scene);
    engine.node.attachMesh(scene, n, {mesh: cube, material: mat});
    nodes.push(n);
}
engine.view.lookAt(view, {eye: [0, 5, 20], target: [0, 0, 0]});
engine.frames(2);
let callsMs = 0, framesMs = 0;
for (let f = 0; f < F; ++f) {
    const t0 = performance.now();
    for (let i = 0; i < N; ++i)
        engine.node.setTransform(scene, nodes[i], {position: [(i % 20) - 10, Math.floor(i / 20) * 0.5, Math.sin(f * 0.05 + i)]});
    const t1 = performance.now();
    engine.frames(1);
    framesMs += performance.now() - t1;
    callsMs += t1 - t0;
}
console.log("MEASURE calls script us_per_call=" + (callsMs * 1000 / (N * F)).toFixed(3) + " calls_ms=" + callsMs.toFixed(1) +
            " frames_ms=" + framesMs.toFixed(1));
host.check({ok: true, what: "measured"});
