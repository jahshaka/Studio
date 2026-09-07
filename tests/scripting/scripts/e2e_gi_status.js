// scripting.e2e.gi_status — REFLECTIONS_ADOPTION_SPEC.md §3: world.giStatus(),
// the verb that reports what global illumination ACHIEVED rather than what
// world.gi requested. Runs inside the real app (--script, engine viewport up,
// scratch HOME), because the whole point of the verb is the measurement — the
// engine is what knows how many reflection probes exist and whether the PBR
// shader is sampling them.
//
// It exists because the VCT+PCC hybrid can degrade to plain VCT SILENTLY (the
// probe arm logs a line and returns), and until this verb landed no caller,
// panel or test could tell the difference. The pixel half of the gate is the
// gi.pcc_mirror suite; this is the API-first half: the verb, its live reading,
// and the invariant that binds them.
//
// Phase A: off — nothing bound, no probes.
// Phase B: plain VCT — VCT bound, still no probes.
// Phase C: the hybrid — probeCount == the pccGrid product, and pccBound.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

var guid = project.create("GI Status " + Date.now());
assert(guid.length > 10, "project.create -> " + guid);

// Something for the voxelizer to chew on: an empty scene voxelizes nothing and
// rebuildVct returns early ("stay armed"), which would make every reading below
// a study of the no-geometry path instead of the real one.
var box = scene.addPrimitive("cube", { position: { x: 0, y: 1, z: 0 },
                                       scale: { x: 2, y: 2, z: 2 } });
assert(box.length > 10, "cube added");
editor.frame(2);

// ---- phase A: GI off -----------------------------------------------------
assert(world.gi({ mode: "off" }), "world.gi(off)");
editor.frame(3);
var st = world.giStatus();
console.log("giStatus(off) = " + JSON.stringify(st));
assert(st.live === true, "giStatus is LIVE (the engine viewport answered)");
assert(st.mode === "off", "mode reads off");
assert(st.requestedMode === "off", "requestedMode reads off");
assert(st.probeCount === 0, "no probes with GI off");
assert(st.pccBound === false && st.vctBound === false, "nothing bound with GI off");

// ---- phase B: plain VCT --------------------------------------------------
// Explicit bounds: the auto heuristic unions every lit item (the sample
// Ground plane included), which is P1a's subject — pinning them here keeps
// this suite about the STATUS verb and not about bounds policy.
assert(world.gi({ mode: "vct", quality: "low", bounces: 1,
                  boundsMin: { x: -6, y: -1, z: -6 },
                  boundsMax: { x: 6, y: 6, z: 6 } }), "world.gi(vct)");
editor.frame(4);
st = world.giStatus();
console.log("giStatus(vct) = " + JSON.stringify(st));
assert(st.mode === "vct", "mode reads vct");
assert(st.vctBound === true, "plain VCT binds this scene's voxel lighting");
assert(st.pccBound === false, "plain VCT binds no reflection probes");
assert(st.probeCount === 0, "plain VCT builds no probes");

// ---- phase C: the hybrid -------------------------------------------------
// 2 x 1 x 2 = 4 probes. The assertion that matters is probeCount === the grid
// product: a silent bail leaves it at 0 with pccBound false while `mode` still
// cheerfully reads vct_pcc_hybrid.
assert(world.gi({ mode: "vct_pcc_hybrid", quality: "low",
                  pccGrid: { x: 2, y: 1, z: 2 } }), "world.gi(vct_pcc_hybrid)");
editor.frame(6);
st = world.giStatus();
console.log("giStatus(hybrid) = " + JSON.stringify(st));
assert(st.mode === "vct_pcc_hybrid", "mode reads vct_pcc_hybrid");
assert(st.requestedMode === "vct_pcc_hybrid", "requestedMode agrees");
assert(st.probeCount === 4, "the hybrid built the requested 2 x 1 x 2 probe grid");
assert(st.pccBound === true,
       "the probe grid is BOUND — the hybrid did not silently degrade to plain VCT");
assert(st.vctBound === true, "the hybrid keeps the VCT binding too");

// The grid is honoured, not merely non-zero.
assert(world.gi({ pccGrid: { x: 2, y: 2, z: 2 } }), "world.gi(pccGrid 2x2x2)");
editor.frame(6);
st = world.giStatus();
console.log("giStatus(hybrid 2x2x2) = " + JSON.stringify(st));
assert(st.probeCount === 8, "a bigger grid rebuilds to a bigger probe count");
assert(st.pccBound === true, "still bound after the rebuild");

// ---- teardown ------------------------------------------------------------
assert(world.gi({ mode: "off" }), "world.gi(off) again");
editor.frame(3);
st = world.giStatus();
console.log("giStatus(off again) = " + JSON.stringify(st));
assert(st.probeCount === 0 && st.pccBound === false && st.vctBound === false,
       "turning GI off unbinds everything");

console.log("e2e_gi_status: all ok");
