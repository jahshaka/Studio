// demo_spiral.js — THE LAB's DEMO SCRIPT (LAB-SCRIPTS-1 stage 1): `jahlab --js tests/lab/demo_spiral.js` opens the lab's
// window and draws this script's own scene in it - a hub cube and a spiral of 48 cubes that turn and wind frame by
// frame while the camera circles in. Only the engine's own commands (jahengine --list) and the lab's lab.view(): no
// demo scene, no asset. Edit it and press F9 to run it again; F10 loads the next script of this folder.
//
// IT IS ALSO lab.js_window_view's SUBJECT (js_window_view.sh, `jahlab --headless --js`): at its end it reads the view it
// drew into. In the window that read is refused (a window's picture is the screen's: readPixels reads offscreen views)
// and the script checks nothing ("ran: no checks"); offscreen it judges two laws in exact display codes:
//   (1) THE BACKGROUND IS THE CLEARED COLOUR: the top rows see above the camera's horizon, where no cube reaches (every
//       cube's top is below the eye: the camera's 45-degree frustum looks 9.5 degrees down, so its top edge is above
//       the horizon) - they read the lab view's clear, black: 0 codes in R, G and B;
//   (2) THE CUBES ARE DRAWN: the centre (the hub, where the camera looks, or a spiral cube in front of it) reads above
//       that in every channel - its own emissive and the ambient - a CONTROL above zero.
// The centre's mean is printed (`centre r g b a`) so the driver can hold the lab's own shot after the run to it.
// Time is frames (engine.frames): the script has no clock.

const FRAMES = 240, N = 48;
const view = lab.view();   // the lab's view, handed over detached
const scene = engine.scene.create("demo-spiral");
engine.view.setScene(view, {scene});
engine.scene.setAmbient(scene, {upper: [0.55, 0.6, 0.7], lower: [0.12, 0.1, 0.08]});
const cube = engine.mesh.unitCube(scene);

const hub = engine.node.create(scene);
engine.node.attachMesh(scene, hub, {
    mesh: cube, material: engine.material.createPbr(scene, {albedo: [0.9, 0.5, 0.2, 1], roughness: 0.6, emissive: [0.5, 0.18, 0.04, 1]})});
engine.node.setTransform(scene, hub, {position: [0, 1, 0], scale: [2, 2, 2]});

// the spiral's cubes, each its own colour round the hue circle
const nodes = [];
for (let i = 0; i < N; ++i) {
    const h = i / N * 2 * Math.PI;
    const albedo = [0.5 + 0.4 * Math.cos(h), 0.5 + 0.4 * Math.cos(h - 2.094), 0.5 + 0.4 * Math.cos(h + 2.094)];
    const node = engine.node.create(scene);
    engine.node.attachMesh(scene, node, {
        mesh: cube, material: engine.material.createPbr(scene, {albedo: [...albedo, 1], roughness: 0.5, emissive: [...albedo.map(c => 0.15 * c), 1]})});
    nodes.push(node);
}

// frame f's poses: cube i on the spiral (radius 2.5 .. 7 m, height 0.25 .. 2.3 m: every top below the eye's 3 m), the
// spiral winding round the hub, each cube turning about its own tilted axis
const positions = new Array(3 * N), rotations = new Array(4 * N), scales = new Array(3 * N).fill(0.45);
function pose(f) {
    const wind = f * 0.02;
    for (let i = 0; i < N; ++i) {
        const t = i / N, a = t * 4 * Math.PI + wind, r = 2.5 + 4.5 * t;
        positions[3 * i] = r * Math.cos(a);
        positions[3 * i + 1] = 0.25 + 2.05 * t;
        positions[3 * i + 2] = r * Math.sin(a);
        const spin = f * 0.05 + i, ax = [Math.sin(i), 1, Math.cos(i)];
        const l = Math.hypot(ax[0], ax[1], ax[2]), s = Math.sin(spin / 2) / l;
        rotations[4 * i] = ax[0] * s;
        rotations[4 * i + 1] = ax[1] * s;
        rotations[4 * i + 2] = ax[2] * s;
        rotations[4 * i + 3] = Math.cos(spin / 2);
    }
    engine.node.setTransforms(scene, {nodes, positions, rotations, scales});
    // the camera circles a quarter turn and closes in from 14 m to 11 m, at the eye's 3 m, looking at the hub
    const c = f / FRAMES, ca = 0.6 + c * Math.PI / 2, cr = 14 - 3 * c;
    engine.view.lookAt(view, {eye: [cr * Math.cos(ca), 3, cr * Math.sin(ca)], target: [0, 1, 0]});
}

for (let f = 0; f < FRAMES; ++f) {
    pose(f);
    engine.frames(1);
}

// THE READ (offscreen only: in the window the request is refused and answers null - nothing is checked)
const shot = engine.view.readPixels({view});
if (shot) {
    const W = shot.width, H = shot.height, cw = Math.max(1, W >> 4), ch = Math.max(1, H >> 4);
    const band = engine.picture.mean(shot.picture, {x0: 0, y0: 0, x1: W, y1: Math.max(1, H >> 5)});
    const centre = engine.picture.mean(shot.picture, {x0: (W >> 1) - cw, y0: (H >> 1) - ch, x1: (W >> 1) + cw, y1: (H >> 1) + ch});
    console.log("centre " + centre.join(" "));
    console.log("band " + band.join(" "));
    host.check({ok: band[0] === 0 && band[1] === 0 && band[2] === 0,
                what: "the top rows are the lab view's clear colour, 0 codes (they read " + band.slice(0, 3).join(", ") + ")"});
    host.check({ok: centre[0] > band[0] && centre[1] > band[1] && centre[2] > band[2],
                what: "the cubes are drawn: the centre reads above the background in R, G and B (" +
                      centre.slice(0, 3).map(v => v.toFixed(2)).join(", ") + ")"});
    engine.picture.release(shot.picture);
}
