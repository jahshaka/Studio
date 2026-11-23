// js_cube.js — the smallest script that DRAWS IN THE LAB's VIEW (LAB-SCRIPTS-1; the subject of lab.js_clear_restores and
// lab.js_no_overlay): it takes the lab's view (lab.view(), handed over detached), binds its own scene - one emissive cube
// under an ambient light, the camera looking at it - and draws 3 frames. One check: the run reached its end.
const view = lab.view();
const scene = engine.scene.create("js-cube");
engine.view.setScene(view, {scene});
engine.scene.setAmbient(scene, {upper: [0.55, 0.6, 0.7, 1], lower: [0.12, 0.1, 0.08, 1]});
const cube = engine.node.create(scene);
engine.node.attachMesh(scene, cube, {
    mesh: engine.mesh.unitCube(scene),
    material: engine.material.createPbr(scene, {albedo: [0.9, 0.5, 0.2, 1], roughness: 0.6, emissive: [0.5, 0.18, 0.04, 1]})});
engine.node.setTransform(scene, cube, {position: [0, 0, 0], scale: [2, 2, 2]});
engine.view.lookAt(view, {eye: [4, 3, 5], target: [0, 0, 0]});
engine.frames(3);
host.check({ok: true, what: "js_cube.js drew its 3 frames"});
