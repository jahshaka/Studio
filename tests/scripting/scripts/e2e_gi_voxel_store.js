// scripting.e2e.gi_voxel_store — THE VOXEL STORE HOLDS THE FIXED POINT
// (PHOTON-M3, fork ae2ed529f+155a56bf8 (was 0080); spikes/photon-m3/MEASUREMENTS.md).
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

// A closed room of albedo 0.791 (#e6e6e6) with one point lamp inside: the rig PHOTON-M1/M2/M3
// measured. (The document's cube primitive is 2 x 2 x 2, so the scales below make 10 x 0.4 x 10
// slabs overlapping into a room x, z in [-2.3, 2.3], y in [0.2, 4.8] - node.size, measured.)
// ROUGHNESS 0 (PHOTON-WRITER-1): a voxel holds what its surface renders now - the
// normalised Disney diffuse lobe's albedo, 0.69 of a Lambertian surface under the
// lamp and 0.70 of the bounce at roughness 1 - and at roughness 1 this room's fixed
// point (peak 0.78) no longer reaches the ceiling the float store exists to exceed.
// At roughness 0 the lobe is 0.95 / 0.905 of Lambertian and the claim below is
// exercised again (peak 1.24, 6,647 voxels above 1, measured).
var S = 5.0, T = 0.2;
function box(px, py, pz, sx, sy, sz) {
    var id = scene.addPrimitive("cube", { position: { x: px, y: py, z: pz }, scale: { x: sx, y: sy, z: sz } });
    material.set(id, { baseColor: "#e6e6e6", roughness: 0.0, metallic: 0.0 });
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
// THE LIT GEOMETRY PREDICTS THE LIT VOXELS (PHOTON-VOXEL-5): every cascade-0 voxel holding a face
// the lamp sees, and none other - 20,184 on this room's lattice (the camera's, cell 10/128 m;
// gi.voxel_lab storeroom, the ideal: each face piece lit at its centroid, exact visibility). No
// voxel's coverage lies within the store's quantum of zero, so the count is exact. The leaky
// store read 22,476: 2,292 voxels holding only faces hidden inside the slabs, lit through them.
assert(d.voxelsLit === 20184,
       "the direct pass lit exactly the voxels the lit geometry predicts: " + d.voxelsLit + " (20184)");
// THE STORE IS NORMALISED TO THE LAMP'S LIGHT AT 1 m (IMAGE-1): a lamp follows the inverse
// square law now, so a face NEARER than 1 m stands above 1 — the ceiling's inner face is 0.8 m
// above it, and a voxel's centre (where the injection evaluates the law) up to half a cell nearer.
// The bound is the law at that nearest centre times the albedo (the lobe is at most 1).
var NEAREST = 0.8 - 0.5 * (10.0 / 128.0);
var DIRECT_MAX = 0.791 / (NEAREST * NEAREST);
assert(d.peak > 0.5 && d.peak <= DIRECT_MAX * (1 + 1e-3),
       "the direct term is at most the falloff allows at the nearest face: peak " + d.peak +
       " (bound " + DIRECT_MAX.toFixed(3) + ")");
assert(d.voxelsAtMax === 0, "a float store has no top bin: voxelsAtMax " + d.voxelsAtMax);

// (c) three bounces: the fixed point stands above the ceiling and is HELD.
world.gi({ bounces: 3 });
world.refreshGi();
settle();
var b = world.giVoxelStats({ cascade: 0 });
assert(b.available === true && b.format === "PFG_RGBA16_FLOAT", "the bounced volume is the same float store");
assert(b.peakDirect > 0.5 && b.peakDirect <= DIRECT_MAX * (1 + 1e-3),
       "the DIRECT volume beside it holds D within the falloff's bound: peakDirect " + b.peakDirect);
// (IMAGE-1: the direct PEAK is now the inverse square law's hot spot on the ceiling 0.8 m over
// the lamp, which the bounce barely adds to — the fixed point's excess is where the room fills,
// so it is counted there: more voxels above 1 with the bounce than with the direct term alone.)
assert(b.voxelsAboveOne > d.voxelsAboveOne && b.peak >= b.peakDirect,
       "the fixed point stands above the ceiling: " + b.voxelsAboveOne + " voxels above 1 against " +
       d.voxelsAboveOne + " direct; peak " + b.peak + " vs direct " + b.peakDirect +
       " (an 8-bit store reads 1.0 here)");
// ...and HELD: voxels stand above the ceiling (an 8-bit store has none). The count itself is no
// geometric prediction - it is the tail of the fixed point over the ceiling (781 here, 5,115 on
// the leaky store whose hidden faces glowed) - so the claim is that the tail exists.
assert(b.voxelsAboveOne > 0,
       "the energy an 8-bit store clips is held: " + b.voxelsAboveOne + " voxels above 1");
assert(b.voxelsAtMax === 0, "still no top bin: voxelsAtMax " + b.voxelsAtMax);
assert(b.meanLit > d.meanLit * 0.9 || b.voxelsLit > d.voxelsLit,
       "the bounce added energy: lit " + d.voxelsLit + " -> " + b.voxelsLit + ", meanLit " + d.meanLit + " -> " + b.meanLit);
console.log("DONE gi_voxel_store");
