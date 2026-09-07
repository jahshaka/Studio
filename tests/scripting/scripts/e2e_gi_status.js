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

// ---- phase D: the probe-capture knobs (P3a/P3b/P3c) ----------------------
// These are verb-only by design (the World panel stays the quality dial), so
// this suite is the ONLY place they are exercised end to end. The two toggles
// are tri-state: "auto" means follow the quality dial, which is the default and
// the value almost every scene should hold.
// The defaults a fresh scene holds, read back through world.get().gi. The two
// toggles come back as the STRING "auto" rather than as a number, because
// "follow the quality dial" is a third state a bool cannot carry.
var giNow = world.get().gi;
console.log("gi defaults = " + JSON.stringify(giNow));
assert(giNow.probeHdr === "auto" && giNow.probeShadows === "auto",
       "the probe toggles default to \"auto\"");
assert(Math.abs(giNow.overlap - 1.25) < 1e-4,
       "probe overlap defaults to 1.25 (upstream's sample value), not the pin's 1.5");
assert(Math.abs(giNow.snapDeviation - 0.05) < 1e-4 &&
       Math.abs(giNow.snapSidesMin - 0.25) < 1e-4 &&
       Math.abs(giNow.snapSidesMax - 0.25) < 1e-4,
       "the snap tolerances are set explicitly to the values that used to be inherited");

// Unknown keys are still refused after the phase added six of them, and the
// refusal names the key.
var threw = "";
try { world.gi({ probeHDR: true }); } catch (e) { threw = String(e); }
assert(threw.indexOf("probeHDR") >= 0,
       "world.gi REFUSES an unknown key by name (probeHDR is not probeHdr): " + threw);

// Range refusals: these are the knobs a script is most likely to get wrong.
threw = "";
try { world.gi({ overlap: 0 }); } catch (e) { threw = String(e); }
assert(threw.indexOf("overlap") >= 0, "world.gi refuses overlap 0: " + threw);
threw = "";
try { world.gi({ snapDeviation: -1 }); } catch (e) { threw = String(e); }
assert(threw.indexOf("snapDeviation") >= 0, "world.gi refuses a negative snapDeviation: " + threw);
threw = "";
try { world.gi({ probeHdr: "sometimes" }); } catch (e) { threw = String(e); }
assert(threw.indexOf("probeHdr") >= 0,
       "world.gi refuses a probeHdr that is neither a bool nor \"auto\": " + threw);

// The happy path, and the resolved reading that follows it. quality stays low
// (probes are 128px, six faces each) — probeHdr/probeShadows are pinned ON
// rather than reached through quality:"high", which is the whole point of the
// tri-state.
assert(world.gi({ probeHdr: true, probeShadows: false, overlap: 1.4,
                  snapDeviation: 0.08, snapSidesMin: 0.3, snapSidesMax: 0.3 }),
       "world.gi accepts the probe-capture knobs");
editor.frame(6);
st = world.giStatus();
console.log("giStatus(probeHdr on) = " + JSON.stringify(st));
assert(st.probeHdr === true, "giStatus reports the HDR capture the scene pinned");
assert(st.probeShadows === false, "giStatus reports the shadows the scene turned off");
assert(st.probeCount === 8 && st.pccBound === true,
       "the probe grid survived the knob change");

// "auto" at low quality means both off; the same "auto" at high means both on.
assert(world.gi({ probeHdr: "auto", probeShadows: "auto" }), "world.gi back to auto");
editor.frame(6);
st = world.giStatus();
assert(st.probeHdr === false && st.probeShadows === false,
       "auto at LOW quality resolves to LDR, unshadowed captures");
assert(world.gi({ quality: "high", pccGrid: { x: 1, y: 1, z: 2 } }),
       "world.gi(quality high) — a small grid, High probes are 512px");
editor.frame(8);
st = world.giStatus();
console.log("giStatus(auto at high) = " + JSON.stringify(st));
assert(st.probeHdr === true && st.probeShadows === true,
       "the SAME auto at HIGH quality resolves to HDR, shadowed captures");

// The knobs are document state, so they survive a save and a reopen.
assert(world.gi({ quality: "low", probeHdr: false, overlap: 1.4 }), "world.gi pins for the round trip");
assert(project.save() === true, "project.save");
assert(project.close() === true, "project.close");
assert(project.open(guid) === true, "project.open");
editor.frame(4);
var reopened = world.get().gi;
console.log("reopened gi = " + JSON.stringify(reopened));
assert(reopened.probeHdr === false, "probeHdr survived the round trip as a real false, not 'auto'");
assert(reopened.probeShadows === "auto", "probeShadows survived as 'auto'");
assert(Math.abs(reopened.overlap - 1.4) < 1e-4, "overlap survived the round trip");
assert(Math.abs(reopened.snapDeviation - 0.08) < 1e-4, "snapDeviation survived");
assert(Math.abs(reopened.snapSidesMin - 0.3) < 1e-4, "snapSidesMin survived");
assert(Math.abs(reopened.snapSidesMax - 0.3) < 1e-4, "snapSidesMax survived");

// ---- phase E: EPIC REACHES THE HYBRID (P6/F8) ----------------------------
// The hybrid was unreachable from the quality tiers until this phase: Epic's
// giMode row said plain VCT. This is the assertion that the flip is real all
// the way down — not just a row value, but a bound probe grid in the renderer.
assert(world.mode({ mode: "epic" }) === "epic", "world.mode(epic)");
editor.frame(8);
st = world.giStatus();
console.log("giStatus(Epic) = " + JSON.stringify(st));
assert(st.requestedMode === "vct_pcc_hybrid", "the Epic tier selects the VCT+PCC hybrid");
assert(st.mode === "vct_pcc_hybrid", "...and the renderer is running it");
assert(st.pccBound === true && st.probeCount > 0,
       "...with a real probe grid bound to the PBR shader, not a silent fallback");

// ---- teardown ------------------------------------------------------------
assert(world.gi({ mode: "off" }), "world.gi(off) again");
editor.frame(3);
st = world.giStatus();
console.log("giStatus(off again) = " + JSON.stringify(st));
assert(st.probeCount === 0 && st.pccBound === false && st.vctBound === false,
       "turning GI off unbinds everything");

console.log("e2e_gi_status: all ok");
