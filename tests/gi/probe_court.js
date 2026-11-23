// gi.probe_court — A PROBE THAT PHOTOGRAPHED A FLOOR AND A WALL IS KEPT (PHOTON-5 stage 1; SPECS/audits/
// PHOTON_AUDIT_2026-10-21.md F1, BUGS-8's "probe placement keeps no probe"). Run by jahengine (SCRIPT-IN-ENGINE-1 2f: the
// engine's own script host, no C++ fixture, no Qt; it replaces test_gi_probe_court.cpp, proved equal to it picture by
// picture and count by count: spikes/script-in-engine-1/train/equality/).
//
// THE LAW. On a tier that does not trace its reflections (Medium) the reflection-probe grid is placed in the scene's
// region and every probe is kept or dropped by what IT photographed: the placement reads one averaged depth ratio per
// cube face (1 = on the region's face, 2 = the encoding's saturation, i.e. sky). The rule counts the faces under
// saturation and keeps a probe that saw geometry in at least two directions; one that saw a single direction of geometry
// and sky elsewhere IS the sky, which stays the fallback where no probe covers a pixel (SKY-FALLBACK-1). How many a tier
// pays for is the VRAM budget's ranking, not this rule. The rule it replaced dropped every probe with one sky-facing face
// - a court, an atrium, a street (Bistro Exterior at Low/Medium: probeCount 0, every shop window reflects the sky).
// THREE FIXTURES at Medium, the region fitted automatically (no pin), ONE SCENE AT A TIME on one view:
//   1. an open court: a 20 m floor and four 3 m walls, no roof   keeps >= 1 (and Medium places a grid: not by rays)
//   2. the same floor alone                                     PRINTED, NOT A LAW (one averaged texel per face: a
//      horizontal face over a floor reads ~1.1 with or without a wall, so a bare floor's probes count five faces of
//      geometry, like the court's; whatever the rule keeps there, the VRAM budget ranks)
//   3. the court with a roof (a sealed room)                    THE CONTROL: 18 of 18 of the shipped 3 x 2 x 3 grid kept
// GI: vctPccHybrid at Medium with its bounces, the update budget 0 (deterministic: no re-capture mid-read); the fixed
// 10-frame settle; the counts from the scene's published GI status. THE ROUTE (URGENT #146, ROUTE-ROWS-1): the view of
// this ray scene runs the GATHER (every view does), over the voxel store, paused - declared, and judged from frame 1.
// The arithmetic the C++ fixture did in float is done here in float (Math.fround): the poses are the same floats.

const f = Math.fround;
const HALF = 10, WALL_H = 3, THICK = f(0.2);   // the court 20 m across, its walls 3 m high

// a node whose -Y is aimed along `dir`, carrying a directional light of `power` (the C++ helper's float arithmetic:
// -Y rotated onto the direction by the shortest arc; setLight scales the intensity by pi, so it is power / pi)
function addDirectionalLight(scene, dir, power) {
    const n = engine.node.create(scene);
    const dx = f(dir[0]), dy = f(dir[1]), dz = f(dir[2]);
    const len = f(Math.sqrt(f(f(f(dx * dx) + f(dy * dy)) + f(dz * dz))));
    const d = [f(dx / len), f(dy / len), f(dz / len)];
    const from = [0, -1, 0];
    const cross = [f(f(from[1] * d[2]) - f(from[2] * d[1])), f(f(from[2] * d[0]) - f(from[0] * d[2])),
                   f(f(from[0] * d[1]) - f(from[1] * d[0]))];
    const dot = f(f(f(from[0] * d[0]) + f(from[1] * d[1])) + f(from[2] * d[2]));
    let q;
    if (dot < f(-0.999999)) q = [0, 0, 1, 0];
    else {
        const w = f(1 + dot);
        const nn = f(Math.sqrt(f(f(f(f(w * w) + f(cross[0] * cross[0])) + f(cross[1] * cross[1])) + f(cross[2] * cross[2]))));
        q = [f(cross[0] / nn), f(cross[1] / nn), f(cross[2] / nn), f(w / nn)];
    }
    engine.node.setTransform(scene, n, {rotation: q});
    host.check({ok: engine.light.set(scene, n, {type: "directional", colour: [1, 1, 1, 1], intensity: f(power / f(Math.PI))}),
                what: "the sun exists"});
}

function addBox(scene, albedo, position, scale) {
    const n = engine.node.create(scene);
    host.check({ok: engine.node.attachMesh(scene, n, {mesh: engine.mesh.unitCube(scene),
                    material: engine.material.createPbr(scene, {albedo, metalness: 0, roughness: 0.9})}),
                what: "a box exists"});
    engine.node.setTransform(scene, n, {position, scale});
}
const OUTER = f(f(2 * HALF) + f(2 * THICK));
function addFloor(s) { addBox(s, [0.45, 0.45, 0.45, 1], [0, -0.1, 0], [OUTER, 0.2, OUTER]); }
function addWalls(s) {
    const white = [0.85, 0.85, 0.85, 1], y = f(WALL_H * 0.5);
    addBox(s, white, [-HALF, y, 0], [THICK, WALL_H, OUTER]);
    addBox(s, white, [+HALF, y, 0], [THICK, WALL_H, OUTER]);
    addBox(s, white, [0, y, -HALF], [OUTER, WALL_H, THICK]);
    addBox(s, white, [0, y, +HALF], [OUTER, WALL_H, THICK]);
}
function addRoof(s) { addBox(s, [0.85, 0.85, 0.85, 1], [0, f(WALL_H + f(0.1)), 0], [OUTER, 0.2, OUTER]); }

const view = engine.view.createOffscreen({name: "court", width: 128, height: 128, background: [0, 0, 0, 1]});

function court(name, walls, roof) {
    const s = engine.scene.create(name);
    engine.view.setScene(view, {scene: s});
    engine.scene.setAmbient(s, {upper: [0, 0, 0, 1], lower: [0, 0, 0, 1]});
    addFloor(s);
    if (walls) addWalls(s);
    if (roof) addRoof(s);
    addDirectionalLight(s, [-0.3, -0.8, -0.5], 5);
    engine.view.lookAt(view, {eye: [0, 1.7, 6], target: [0, 1.2, 0]});
    host.check({ok: engine.gi.set(s, {mode: "vctPccHybrid", quality: "medium", bounces: true, updateBudget: 0}),
                what: name + ": GI builds"});
    engine.view.declareRoutes(view, {gather: true, store: true, paused: true});
    engine.frames(9);
    const shot = engine.view.readPixels({view});                                         // MEASURING ARM - reverted
    console.log("ARM picture fnv1a64 " + engine.picture.hash(shot.picture));           // MEASURING ARM - reverted
    engine.picture.release(shot.picture);                                               // MEASURING ARM - reverted
    const gi = engine.scene.published(s, {select: ["gi"]}).gi;
    const v = {probes: gi.probeCount, dropped: gi.probesDropped, overBudget: gi.probesOverBudget, byRays: gi.probeGridByRays};
    engine.scene.destroy(s);
    return v;
}
function report(title, v) {
    console.log("-- " + title + "\n   probes=" + v.probes + " dropped=" + v.dropped + " overBudget=" + v.overBudget +
                " byRays=" + (v.byRays ? 1 : 0));
}

const open = court("court", true, false);
report("1. an open court: a 20 m floor, four 3 m walls, no roof", open);
host.check({ok: !open.byRays, what: "1: Medium places a probe grid (no ray reflections)"});
host.check({ok: open.probes >= 1, what: "1: a probe in a court photographed the floor and the walls: at least one is kept (" +
                                        open.probes + ")"});

const flat = court("flat", false, false);
report("2. the same floor alone, open sky", flat);
console.log("   (printed, not asserted: the bare floor keeps " + flat.probes + ")");

const sealed = court("sealed", true, true);
report("3. the court with a roof (a sealed room)", sealed);
host.check({ok: sealed.probes === 18 && sealed.dropped === 0,
            what: "3, THE CONTROL: a sealed room keeps what it kept before the rule changed (18 of 18 of the shipped 3x2x3 " +
                  "grid; read " + sealed.probes + ", dropped " + sealed.dropped + ")"});
