// script.batch_equals_single — ONE engine.node.setTransforms CALL PUTS N NODES WHERE N engine.node.setTransform CALLS PUT
// THEM (SCRIPT-IN-ENGINE-1 2a a4). Run by jahengine (the engine's own script host, no Studio, no Qt).
//
// THE LAW: two twin scenes, each 64 cubes plus one, each seen by its own offscreen view at one pose; scene A places the
// 64 by single calls, scene B by ONE batch of flat arrays (positions, rotations, scales - every node's different) after a
// first batch elsewhere (each node TURNED as well); the 65th cube is moved and turned away and set back to the identity
// in both - by setTransform with every part left out in A, by setTransforms with every part left out in B. The two pictures are BYTE-EQUAL: the same engine
// state (a stride or an order slip moves a cube; a part left out that is not the identity's moves the 65th).
// THE CONTROL: the picture is not the background (the cubes are drawn: the frame's centre against its top rows).
// Frames: 30, then one for each view's picture: 32.

const W = 192, H = 108, N = 64;

function twin(name) {
    const view = engine.view.createOffscreen({name, width: W, height: H, background: [0.02, 0.02, 0.03]});
    const scene = engine.scene.create(name);
    engine.view.setScene(view, {scene});
    engine.scene.setAmbient(scene, {upper: [0.6, 0.6, 0.65], lower: [0.1, 0.08, 0.06]});
    const cube = engine.mesh.unitCube(scene);
    const material = engine.material.createPbr(scene, {albedo: [0.8, 0.5, 0.3, 1], roughness: 0.8});
    const nodes = [];
    for (let i = 0; i <= N; ++i) {
        const node = engine.node.create(scene);
        engine.node.attachMesh(scene, node, {mesh: cube, material});
        nodes.push(node);
    }
    engine.view.lookAt(view, {eye: [0, 7, 12], target: [0, 0, 0]});
    return {view, scene, nodes};
}

// node i's pose: an 8 x 8 grid, each cube turned about its own axis and stretched its own way
function pose(i) {
    const a = 0.37 * i, axis = [Math.sin(i), 1, Math.cos(i)];
    const l = Math.hypot(axis[0], axis[1], axis[2]), s = Math.sin(a / 2) / l;
    return {position: [(i % 8 - 3.5) * 1.4, 0.15 * (i % 3), (Math.floor(i / 8) - 3.5) * 1.4],
            rotation: [axis[0] * s, axis[1] * s, axis[2] * s, Math.cos(a / 2)],
            scale: [0.4 + 0.05 * (i % 5), 0.3 + 0.1 * (i % 4), 0.5 + 0.04 * (i % 7)]};
}

const A = twin("single"), B = twin("batch");

for (let i = 0; i < N; ++i) engine.node.setTransform(A.scene, A.nodes[i], pose(i));
// (away: turned 30 degrees about (1, 1, 0) - so a rotation left out that is NOT the identity's shows: the 2a final read's L2)
const AWAY = [0.183013, 0.183013, 0, 0.9659258];
engine.node.setTransform(A.scene, A.nodes[N], {position: [5, 5, 5], rotation: AWAY, scale: [2, 2, 2]});
engine.node.setTransform(A.scene, A.nodes[N], {});

// B: first ONE batch elsewhere (every node, the 65th too), then ONE batch to the same poses as A, flat
const away = {nodes: B.nodes, positions: [], rotations: [], scales: []};
for (let i = 0; i <= N; ++i) away.positions.push(i, 4, -10), away.rotations.push(...AWAY), away.scales.push(2, 2, 2);
engine.node.setTransforms(B.scene, away);
const flat = {nodes: B.nodes.slice(0, N), positions: [], rotations: [], scales: []};
for (let i = 0; i < N; ++i) {
    const p = pose(i);
    flat.positions.push(...p.position);
    flat.rotations.push(...p.rotation);
    flat.scales.push(...p.scale);
}
engine.node.setTransforms(B.scene, flat);
engine.node.setTransforms(B.scene, {nodes: [B.nodes[N]]});   // every part left out: the identity's

engine.frames(30);
const a = engine.view.readPixels({view: A.view}), b = engine.view.readPixels({view: B.view});
const ha = engine.picture.hash(a.picture), hb = engine.picture.hash(b.picture);
const centre = engine.picture.mean(a.picture, {x0: W / 4, y0: H / 3, x1: W * 3 / 4, y1: H * 5 / 6});
const top = engine.picture.mean(a.picture, {x0: 0, y0: 0, x1: W, y1: 4});
const lift = (centre[0] + centre[1] + centre[2]) / 3 - (top[0] + top[1] + top[2]) / 3;
host.check({ok: lift > 10, what: "THE CONTROL: the cubes are drawn - the centre is " + lift.toFixed(1) +
                                 " codes above the top rows (> 10)"});
host.check({ok: ha === hb, what: "one batch of " + N + " (and a batch of one with every part left out) gives the picture " +
                                 N + " single calls give: " + ha + " / " + hb});
