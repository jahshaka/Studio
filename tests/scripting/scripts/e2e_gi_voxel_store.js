// scripting.e2e.gi_voxel_store — THE VOXEL STORE HOLDS THE FIXED POINT
// (PHOTON-M3, ogre-patch 0080; spikes/photon-m3/MEASUREMENTS.md).
//
// The bounce solves L = D + rho * G(L). Its fixed point is D / (1 - rho*f) and
// has no bound a fixed-range store can hold: in a closed room of albedo 0.79 it
// is 2.05x the direct term, at albedo 1.0 it climbs past 2.7x. The shipped 8-bit
// store clipped it — 5.8 % of the lit voxels on the top bin at the first bounce
// pass in this very room, 15-26 % in the shipped samples — and a clipped voxel
// draws a picture that is merely dimmer, so no pixel suite could see it. This
// one reads the VOLUME (world.giVoxelStats) and asserts what the picture cannot:
//   (a) the total volume is a float (no top bin exists);
//   (b) the DIRECT term is normalised to at most 1 (the auto multiplier's own
//       contract) and nothing of it is above 1 — the ceiling is where D lives;
//   (c) after three bounces the total EXCEEDS 1 in a real set of voxels — the
//       energy an 8-bit store would have clipped is held.
// An 8-bit store fails (a) and (c); a broken normalisation fails (b).

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function settle() {
    for (var g = 0; g < 400; ++g) {
        editor.frame(1, 1 / 60);
        var s = world.giStatus();
        var p = 0;
        for (var i = 0; i < s.cascades.length; ++i) p += s.cascades[i].pending;
        if (p === 0 && s.staleProbes === 0) break;
    }
    editor.frame(8, 1 / 60);
}

var guid = project.create("GI voxel store " + Date.now());
assert(guid.length > 10, "project.create -> " + guid);
editor.setOverlays({ grid: false, lightWires: false });
editor.gameView(true);
world.sky("color", { color: "#000000" });
var ns = scene.nodes({ depth: 1 });
for (var i = 0; i < ns.length; ++i)
    if (ns[i].parent !== null && ns[i].id !== scene.root()) node.remove(ns[i].id);

// A closed 5 m cube of albedo 0.791 (#e6e6e6) with one point lamp inside: the
// rig PHOTON-M1/M2/M3 measured, so the numbers here are the measured ones.
var S = 5.0, T = 0.2;
function box(px, py, pz, sx, sy, sz) {
    var id = scene.addPrimitive("cube", { position: { x: px, y: py, z: pz }, scale: { x: sx, y: sy, z: sz } });
    material.set(id, { baseColor: "#e6e6e6", roughness: 1.0, metallic: 0.0 });
    return id;
}
box(0, 0, 0, S, T, S); box(0, S, 0, S, T, S);
box(-S / 2, S / 2, 0, T, S, S); box(S / 2, S / 2, 0, T, S, S);
box(0, S / 2, -S / 2, S, S, T); box(0, S / 2, S / 2, S, S, T);
var lamp = scene.addLight("point", { position: { x: 0, y: S - 1.0, z: 0 } });
node.setProperty(lamp, "distance", 30.0);
node.setProperty(lamp, "intensity", 0.12);
editor.setCamera({ position: { x: 0, y: S / 2, z: 1.8 }, lookAt: { x: 0, y: S / 2, z: -3.0 } });
world.photon({ enabled: true, tier: "epic" });
world.gi({ ddgi: false });
editor.warmUpShaders();

// (b) the direct term alone: bounded by the ceiling, nothing above it.
world.gi({ bounces: 1 });
world.refreshGi();
settle();
var d = world.giVoxelStats({ cascade: 0 });
assert(d.available === true, "giVoxelStats available on cascade 0 (" + JSON.stringify(d) + ")");
assert(d.format === "PFG_RGBA16_FLOAT", "the total volume is RGBA16F (got " + d.format + ")");
assert(d.voxelsLit > 1000, "the direct pass lit the room: " + d.voxelsLit + " voxels");
assert(d.peak > 0.5 && d.peak <= 1.0 + 1e-3,
       "the direct term is normalised to at most the ceiling: peak " + d.peak);
assert(d.voxelsAboveOne === 0, "nothing of the direct term is above 1 (" + d.voxelsAboveOne + ")");
assert(d.voxelsAtMax === 0, "a float store has no top bin: voxelsAtMax " + d.voxelsAtMax);

// (c) three bounces: the fixed point stands above the ceiling and is HELD.
world.gi({ bounces: 3 });
world.refreshGi();
settle();
var b = world.giVoxelStats({ cascade: 0 });
assert(b.available === true && b.format === "PFG_RGBA16_FLOAT", "the bounced volume is the same float store");
assert(b.peakDirect > 0.5 && b.peakDirect <= 1.0 + 1e-3,
       "the DIRECT volume beside it holds D at most 1: peakDirect " + b.peakDirect);
assert(b.peak > 1.2 * b.peakDirect,
       "the fixed point stands above the ceiling: peak " + b.peak + " vs direct " + b.peakDirect +
       " (measured 2.05x; an 8-bit store reads 1.0 here)");
assert(b.voxelsAboveOne > 1000,
       "the energy an 8-bit store clips is held: " + b.voxelsAboveOne + " voxels above 1 " +
       "(measured ~9,700 on the top bin at the first bounce pass in this room)");
assert(b.voxelsAtMax === 0, "still no top bin: voxelsAtMax " + b.voxelsAtMax);
assert(b.meanLit > d.meanLit * 0.9 || b.voxelsLit > d.voxelsLit,
       "the bounce added energy: lit " + d.voxelsLit + " -> " + b.voxelsLit + ", meanLit " + d.meanLit + " -> " + b.meanLit);
console.log("DONE gi_voxel_store");
