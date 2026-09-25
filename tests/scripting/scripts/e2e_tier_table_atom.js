// scripting.e2e.tier_table_atom — WHAT EACH PHOTON TIER PHYSICALLY IS, and what
// the ATOM column of that table means (A4 §3's tolerance table; ATOM P3's
// SUB-ERROR).
//
// IT LIVED INSIDE scripting.e2e.world_modes, which is a suite about the World
// Mode REGISTRY — resolution, pins, write-through, undo, the save/open
// round-trip. `world.tierTable()` is a different claim with a different owner:
// it is the renderer's own tables read back (worldmodes::kPhotonTable and the
// engine's giQualityFacts), and it is the door a tooltip, a doc and a suite all
// check the same numbers through. The block is MOVED here, not copied — a second
// copy of a table's assertions is how a tooltip drifted from the renderer for
// months in the first place.
//
// Runs headless: every verb here is a document verb and nothing is drawn.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

// The verb needs a document (world.tierTable is a Needs::Document verb — it
// reads the renderer's tables through the open scene's world).
var guid = project.create("Tier Table Atom Test " + Date.now());
assert(guid.length > 10, "project.create -> " + guid);

// ---- world.tierTable(): WHAT A TIER IS, from the renderer's own tables ------
//
// The cure for render audit A5 — five tier tooltips that described a renderer
// which did not exist, for months, because nothing compared a tier's
// DESCRIPTION with what the tier DOES. The verb is generated from
// worldmodes::kPhotonTable and the engine's own giQualityFacts, and the panel's
// tooltips are generated from the same two tables, so this is the door a test
// (or a doc, or a person) checks the claim through.
var tiers = world.tierTable();
assert(tiers.photon.length === 4, "four Photon tiers: " + tiers.photon.length);
assert(tiers.world.length === 4, "four World modes mapped onto Photon");
var byTier = {};
for (var t = 0; t < tiers.photon.length; ++t) byTier[tiers.photon[t].tier] = tiers.photon[t];
assert(!!byTier.low && !!byTier.medium && !!byTier.high && !!byTier.epic,
       "the four tiers are named low/medium/high/epic");
for (var tn in byTier) {
    var row = byTier[tn];
    assert(row.description.length > 20, tn + " carries a generated description");
    assert(row.ddgi === 1, tn + " turns the irradiance field on (it said \"Low cannot\" for months)");
    assert(row.cascades === 1 && row.chain.length >= 2,
           tn + " builds a camera-centred chain of at least two cascades");
    // The chain must grow OUTWARD in reach AND in cell or the cone march
    // cannot hand one cascade over to the next.
    for (var c = 1; c < row.chain.length; ++c)
        assert(row.chain[c].halfSize > row.chain[c - 1].halfSize &&
               row.chain[c].cell > row.chain[c - 1].cell,
               tn + " cascade " + c + " is bigger and coarser than the one inside it");
    // The cell is the number that says what a cascade can resolve.
    assert(Math.abs(row.chain[0].cell -
                    2 * row.chain[0].halfSize / row.chain[0].resolution) < 1e-4,
           tn + " reports each cascade's cell as 2*halfSize/resolution");
}
// THE THREE CLAIMS THE OLD TOOLTIPS GOT WRONG, now assertable:
assert(byTier.low.chain[0].resolution === byTier.medium.chain[0].resolution,
       "Low and Medium voxelise at the SAME resolution — \"Medium is twice Low\" was never true");
assert(byTier.low.voxelResolution === 64 && byTier.low.chain[0].resolution === 64,
       "Low's CHAIN and its single scene-fitted volume are both 64 per axis (PHOTON-VOXEL-4)");
assert(byTier.high.chain[0].resolution === 128 &&
       byTier.high.chain[byTier.high.chain.length - 1].resolution === 64,
       "High's chain is 128 near the eye and 64 far away — not \"128^3\"");
assert(byTier.low.technique === "vct" && byTier.high.technique === "vct_pcc_hybrid" &&
       byTier.medium.technique === "vct",
       "only High and Epic run the hybrid (a probe grid only where the scene does not trace)");
assert(byTier.high.probeFaceSize === 512 && byTier.high.probeHdr === true &&
       byTier.high.probeShadows === true,
       "High's probes are 512 px per face, HDR and shadowed");
assert(byTier.epic.bounces === 3 && byTier.high.bounces === 1,
       "Epic's bounce count is 3 against High's 1");

// ---- THE GATHER COLUMN (PHOTON-GATHER-1d, the rule T-A) ---------------------
// A PROJECTION of the engine's tier table (Types.h GiGatherFacts through
// giQualityFacts), never a copy: High and Epic on, Medium on at 36 rays, Low off;
// Epic's density keyed on the tier (a probe per 8 px), not on the SSR row; the
// VR column off (GA-VR).
function g(t) { return byTier[t].gather; }
assert(g("low").on === false, "Low: no gather — the cones and the field are its diffuse");
assert(g("medium").on === true && g("medium").raysPerProbe === 36 && g("medium").stride === 16,
       "Medium: the gather at 36 rays a probe, a probe per 16 px " + JSON.stringify(g("medium")));
assert(g("high").on === true && g("high").raysPerProbe === 64 && g("high").stride === 16,
       "High: the gather at 64 rays, a probe per 16 px " + JSON.stringify(g("high")));
assert(g("epic").on === true && g("epic").raysPerProbe === 64 && g("epic").stride === 8,
       "Epic: four times the probes (a probe per 8 px) " + JSON.stringify(g("epic")));
for (var gt in byTier) {
    assert(byTier[gt].gather.adaptiveCapDivisor === 4,
           gt + ": the adaptive probes are capped at a quarter of the grid");
    assert(byTier[gt].vrGather.on === false, gt + ": the VR column keeps the gather OFF (GA-VR)");
}
assert(byTier.high.description.indexOf("screen-probe gather") >= 0 &&
       byTier.low.description.indexOf("screen-probe gather") < 0,
       "the generated tier sentence names the gather where the table has it and nowhere else");

// ---- THE ATOM COLUMN (ATOM P3's SUB-ERROR) ---------------------------------
//
// The tier's GEOMETRIC TOLERANCE, in samples: the one input of the level rule
// that is a matter of taste (the rest is the quality currency's arithmetic —
// jahshaka/engine/Types.h). It is reported here from the engine's own
// giQualityFacts, like every other derived column, so a tooltip and a suite read
// one number.
//
// WHAT IT DOES AND DOES NOT DO TODAY, asserted so nobody has to guess: its only
// consumer is a GPU cull request's level output. The SHIPPED draw path stays on
// one pixel at every tier, because wiring the tier into it would move the
// picture of every Low-tier scene — a lane with a pixel gate of its own.
for (var tn2 in byTier)
    assert(typeof byTier[tn2].pixelTolerance === "number" && byTier[tn2].pixelTolerance > 0,
           tn2 + " declares a positive Atom tolerance");
assert(Math.abs(byTier.low.pixelTolerance - 2.0) < 1e-6, "Low tolerates 2 px of deviation");
assert(Math.abs(byTier.medium.pixelTolerance - 1.0) < 1e-6,
       "Medium tolerates 1 px — the shipped kLodBudgetPixels, which is why it is the middle row");
assert(Math.abs(byTier.high.pixelTolerance - 0.5) < 1e-6, "High halves it to 0.5 px");
assert(byTier.epic.pixelTolerance === byTier.high.pixelTolerance,
       "Epic shares High's tolerance: GiQuality is the RESOLUTION dial and Epic changes no resolution");
assert(byTier.low.pixelTolerance > byTier.medium.pixelTolerance &&
       byTier.medium.pixelTolerance > byTier.high.pixelTolerance,
       "the column is monotone: a higher tier tolerates less geometric error");

// ---- THE SAME TABLE'S VR COLUMN (lane V1-RIG item 4) ------------------------
//
// A headset renders the chain five times over for half the frame (2160x2376 per
// eye against a desktop 1080p is 10.26 against 2.07 megapixels; 11.1 ms at 90 Hz
// against 16.7 at 60), and the march costs a MEASURED 0.31 ms per eye per
// cascade at that size. So `vrChain` is the tier's own chain with the redundant
// middle cascade dropped and the outermost step doubled. It is asserted here,
// with no runtime and no headset in sight, because it is arithmetic on one table
// — the same reason the desktop column is asserted above.
//
// EVERY ASSERTION READS `vrChain`, never `chain`: the whole point of the column
// is that the two differ.
Object.keys(byTier).forEach(function (tn) {
    var row = byTier[tn];
    // `row.vrChain && row.vrChain.length`, NOT `Array.isArray`: a QVariantList
    // handed to QJSEngine indexes and reports `length` like an array but is not
    // one by `Array.isArray` (measured — the first version of this assertion
    // failed on a table that was perfectly correct). The rest of this file reads
    // `row.chain.length` the same way for the same reason.
    assert(row.vrChain && row.vrChain.length >= 2,
           tn + " carries a VR chain of at least two cascades");
    // FEWER OR EQUAL CASCADES than the desktop column, never more: the column
    // exists to buy pixel march back.
    assert(row.vrChain.length <= row.chain.length,
           tn + "'s VR chain is not longer than its desktop chain (" +
           row.vrChain.length + " vs " + row.chain.length + ")");
    // THE SAME TWO MONOTONICITIES the desktop column must satisfy — Ogre derives
    // the hand-over LOD from the ratio of the cells, so an outer cascade that is
    // smaller or finer makes the march never hand over.
    for (var c = 1; c < row.vrChain.length; ++c)
        assert(row.vrChain[c].halfSize > row.vrChain[c - 1].halfSize &&
               row.vrChain[c].cell > row.vrChain[c - 1].cell,
               tn + "'s VR chain grows outward in reach AND in cell at row " + c);
    // THE REACH AND THE INNER CELL ARE UNTOUCHED: what the column gives up is a
    // hand-over in the middle, never the near field or the far horizon.
    assert(Math.abs(row.vrChain[0].halfSize - row.chain[0].halfSize) < 1e-4 &&
           Math.abs(row.vrChain[0].cell - row.chain[0].cell) < 1e-4,
           tn + "'s VR chain keeps the desktop inner cascade exactly");
    assert(Math.abs(row.vrChain[row.vrChain.length - 1].halfSize -
                    row.chain[row.chain.length - 1].halfSize) < 1e-4,
           tn + "'s VR chain keeps the desktop REACH exactly");
    // THE OUTERMOST STEP IS PINNED AT 16 CELLS — the engine's own outer default
    // is 8, so this is the doubling, stated as the number the table writes.
    assert(row.vrChain[row.vrChain.length - 1].stepCells === 16,
           tn + "'s VR chain pins the outermost step at 16 cells, got " +
           row.vrChain[row.vrChain.length - 1].stepCells);
    // ...and every OTHER row carries the step the engine DERIVES, reported
    // resolved rather than as the tier row's 0 (CASCADE-STEP-1: the verb runs
    // the renderer's own giResolveCascadeSteps, so a chain a tooltip promises
    // is the chain that gets built, down to the step).
    [row.chain, row.vrChain].forEach(function (ch, col) {
        var what = tn + (col ? " VR" : " desktop");
        for (var i = 0; i < ch.length; ++i) {
            assert(ch[i].stepCells > 0 && ch[i].stepCells <= ch[i].resolution / 2,
                   what + " row " + i + " carries a resolved step in cells (" +
                   ch[i].stepCells + " of " + ch[i].resolution + ")");
            assert(Math.abs(ch[i].step - ch[i].stepCells * ch[i].cell) < 1e-4,
                   what + " row " + i + "'s step in metres is its cells times its cell");
        }
        // THE NEAR-FIELD GUARANTEE (CASCADE-STEP-1, owner 2026-09-18): a
        // cascade covers a radius around the head — halfSize - step - cell,
        // against 0.45 of its own half-size — inside which the near field is
        // voxelised by IT and never by the coarser cascade behind it. Before
        // the rule every tier but Low guaranteed a NEGATIVE radius on cascade
        // 0: the walker reached its own face before it re-centred and the metre
        // in front of them was built by a 2-3x coarser cascade.
        //
        // EVERY ROW, not only cascade 0: the rule is a property of a cascade,
        // and a mid cascade below its own fraction becomes the chain's binding
        // constraint (the nearest distance at which ANY hand-over can happen)
        // however good the innermost one is.
        for (var j = 0; j < ch.length; ++j)
            assert(ch[j].guaranteedRadius >= ch[j].nearFieldRadius - 1e-4,
                   what + " row " + j + " guarantees its near-field radius (" +
                   ch[j].guaranteedRadius.toFixed(3) + " >= " +
                   ch[j].nearFieldRadius.toFixed(3) + " m)");
    });
});
// THE TWO HALVES OF THE TRANSFORM APPLY INDEPENDENTLY, which is exactly where
// the first note was wrong: LOW has no middle cascade to drop and keeps its two
// rows, but its outermost step doubles with everybody else's — so NO tier's VR
// column equals its desktop one.
assert(byTier.low.vrChain.length === byTier.low.chain.length &&
       byTier.low.vrChain.length === 2,
       "Low keeps both its cascades in VR (there is no middle to drop)");
assert(byTier.low.chain[byTier.low.chain.length - 1].stepCells !== 16,
       "...and its DESKTOP outer step is not the VR one (the columns differ at Low too)");
["medium", "high", "epic"].forEach(function (tn) {
    assert(byTier[tn].vrChain.length === byTier[tn].chain.length - 1,
           tn + " drops exactly one cascade in VR (" + byTier[tn].chain.length +
           " -> " + byTier[tn].vrChain.length + ")");
    // The row that went is the SECOND one, the one closest in reach to the row
    // outside it.
    assert(Math.abs(byTier[tn].vrChain[1].halfSize - byTier[tn].chain[2].halfSize) < 1e-4,
           tn + "'s VR chain drops row 1, so its second row is the desktop's third");
});
// The World mode -> Photon tier mapping, by name rather than by ordinal.
var worldMap = {};
for (var wi = 0; wi < tiers.world.length; ++wi) worldMap[tiers.world[wi].mode] = tiers.world[wi].photon;
assert(worldMap.low === "off" && worldMap.medium === "off" &&
       worldMap.high === "low" && worldMap.epic === "epic",
       "World Low/Medium leave Photon off, High selects Photon Low, Epic selects Epic");

// ---- THE ATOM COLUMN NAMES THE CURRENCY IT FEEDS ----------------------------
//
// `pixelTolerance` is the `tolerance` argument of the quality currency
// (jahshaka/engine/Types.h: `allowedWorldError(tolerance, footprint,
// meshToWorldScale)`) — the one input of the level rule that is taste and not
// arithmetic — and the verb reports it from `GiQualityFacts::pixelTolerance`
// like every other derived column, so the name is asserted BY NAME here: a
// second spelling of the same dial on this row would be exactly the drift this
// suite exists to prevent.
var atomKeys = [];
Object.keys(byTier.high).forEach(function (k) {
    if (/toleran|budget|pixelerr|suberr/i.test(k)) atomKeys.push(k);
});
assert(atomKeys.length === 1 && atomKeys[0] === "pixelTolerance",
       "the tier row carries exactly ONE tolerance column and it is named pixelTolerance: [" +
       atomKeys.join(", ") + "]");
// The currency's units: SAMPLES. Medium's cell is the shipped draw budget
// (engine kLodBudgetPixels = 1 px), which is why Medium is the middle row, and
// every tier's value is a small positive number of samples rather than a
// distance or a triangle count.
Object.keys(byTier).forEach(function (tn) {
    var t = byTier[tn].pixelTolerance;
    assert(t > 0.0 && t <= 8.0,
           tn + "'s tolerance is a sample count, not a length or a count of triangles: " + t);
});
// AND WHAT IT DOES NOT DO YET, asserted so nobody has to guess: its only
// consumer is a GPU cull request's level output; the shipped DRAW path stays on
// one pixel at every tier (the tier-aware draw path would move the picture of
// every Low scene and is a lane with a pixel gate of its own). That is why
// Medium's 1.0 is the number the renderer actually draws with, and it is the
// assertion above that pins it.
console.log("tier table (Atom column): low " + byTier.low.pixelTolerance + ", medium " +
            byTier.medium.pixelTolerance + ", high " + byTier.high.pixelTolerance + ", epic " +
            byTier.epic.pixelTolerance + " samples");
console.log("PASSED");
