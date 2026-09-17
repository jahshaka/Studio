// scripting.e2e.world_modes — POST_CHAIN_SPEC.md §9 end-to-end proof.
//
// Runs headless (--headless: every World Modes verb is a document verb). Drives
// the whole system through the API exactly as the World panel does:
//   the registry (world.modeTable)
//   tier resolution and write-through into the backing fields (world.mode)
//   pins that survive a mode switch (world.override)
//   reset, single row and all (world.clearOverride / clearOverrides)
//   the invariant: a backing field is ALWAYS the resolved value
//   round-trip through save/close/open (mode + pins are serialized)
//   the legacy setters (world.setAntiAliasing) recording their own pin

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function refuses(fn, msg) {
    var threw = false;
    try { fn(); } catch (e) { threw = true; }
    assert(threw, msg);
}

var guid = project.create("World Modes Test " + Date.now());
assert(guid.length > 10, "project.create -> " + guid);

// ---- the registry -----------------------------------------------------------
var table = world.modeTable();
assert(table.modes.length === 4, "four tiers: " + table.modes.join(", "));
assert(table.modes[0] === "low" && table.modes[3] === "epic", "tiers are ordered low..epic");
assert(table.rows.length >= 12, "registry has rows: " + table.rows.length);

var byId = {};
for (var i = 0; i < table.rows.length; ++i) byId[table.rows[i].id] = table.rows[i];
assert(!!byId["msaa"], "row msaa is declared");
assert(!!byId["shadowResolution"], "row shadowResolution is declared");
assert(!!byId["shadowFilter"], "row shadowFilter is declared");
assert(!!byId["giMode"], "row giMode is declared");
assert(!!byId["hdr"], "row hdr is declared");
assert(!!byId["bloom"], "row bloom is declared");
assert(!!byId["ssao"], "row ssao is declared");
assert(!!byId["smaa"], "row smaa is declared");
assert(!!byId["refractions"], "row refractions is declared");
assert(!!byId["planarBudget"], "row planarBudget is declared");
// This row was the registry's proof that a CONTRACT row works: it was declared
// here with available = false before any renderer could serve it, and the
// planar-reflection lane later filled in its backing field and flipped the flag
// without touching a single consumer of the table. It is now live, so this
// assertion is the other half of that proof.
assert(byId["planarBudget"].available === true,
       "planarBudget is served by the renderer");
for (var k = 0; k < table.rows.length; ++k) {
    var r = table.rows[k];
    assert(r.label.length > 0 && r.cost.length > 0, "row " + r.id + " has a label and a cost note");
    // A row in the "none" TIER SPACE has no tier columns and must not pretend
    // to (EXPOSURE-1): nothing resolves it, so four identical cells would be a
    // claim about a dial that does not own it. Every other row declares four.
    if (r.tierSpace === "none")
        assert(!r.tiers, "row " + r.id + " is in no tier space and declares no tier values");
    else
        assert(!!r.tiers.low && !!r.tiers.epic, "row " + r.id + " declares all four tier values");
}

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
assert(byTier.low.voxelResolution === 32 && byTier.low.chain[0].resolution === 64,
       "Low's CHAIN is 64 per axis while its single scene-fitted volume is 32");
assert(byTier.high.chain[0].resolution === 128 &&
       byTier.high.chain[byTier.high.chain.length - 1].resolution === 64,
       "High's chain is 128 near the eye and 64 far away — not \"128^3\"");
assert(byTier.low.technique === "vct" && byTier.high.technique === "vct_pcc_hybrid" &&
       byTier.medium.technique === "vct",
       "only High and Epic build a reflection-probe grid");
assert(byTier.high.probeFaceSize === 512 && byTier.high.probeHdr === true &&
       byTier.high.probeShadows === true,
       "High's probes are 512 px per face, HDR and shadowed");
assert(byTier.epic.bounces === 3 && byTier.high.bounces === 1,
       "Epic's one column over High is the bounce count");
// The World mode -> Photon tier mapping, by name rather than by ordinal.
var worldMap = {};
for (var wi = 0; wi < tiers.world.length; ++wi) worldMap[tiers.world[wi].mode] = tiers.world[wi].photon;
assert(worldMap.low === "off" && worldMap.medium === "off" &&
       worldMap.high === "low" && worldMap.epic === "epic",
       "World Low/Medium leave Photon off, High selects Photon Low, Epic selects Epic");

// ---- a fresh scene starts on EPIC ------------------------------------------
// POST_CHAIN_SPEC §12 decision 8 (owner call): new scenes are Epic, and a
// document written before World Modes existed reads as Epic too.
assert(world.mode() === "epic", "a new scene starts on Epic: " + world.mode());
var fresh = world.settings();
assert(fresh.hdr.value === 1, "Epic turns HDR on");
// Hardware MSAA is 2x in EVERY tier (owner 2026-09-15): with the post chain
// on it cannot touch the SCENE (HDR crashes the driver, ambient occlusion
// renders black — both reproduced in tests/engine; SMAA smooths the scene),
// but the gizmos, light wires and helpers drawn into the window still get the
// samples, and at Low — chain off — 2x is the scene's only anti-aliasing.
assert(fresh.msaa.value === 2, "Epic asks for 2x MSAA (the helpers' edges; the chain renders the scene at 1x): " + fresh.msaa.valueId);
// The TIER decides here, not the document: a NEW PROJECT applies Epic
// (MainWindow::createDefaultScene), so the backing field is the tier's value
// and reports source "mode". The two numbers are equal today (both 2x), which
// is why the SOURCE is what this assertion pins — a document default of 2x
// with a tier of 1x would still read "mode" and still be 1x.
assert(world.get().antiAliasing === 2 && fresh.msaa.source === "mode",
       "a new project's backing field is the TIER's 2x, from the mode, not the document's default: " +
       world.get().antiAliasing + " / " + fresh.msaa.source);
assert(fresh.smaa.valueId === "ultra", "Epic anti-aliases with SMAA Ultra: " + fresh.smaa.valueId);
// EPIC = the VCT+PCC hybrid since REFLECTIONS_ADOPTION_SPEC P6 (2026-09-07).
// It was plain "vct" until P1+P2 fixed probe placement, the helper channel and
// the per-drag-frame re-solve, and ogre-patch 0017 removed the overlapping-
// probe division that made every probe reflection up to 8x too dark.
assert(fresh.giMode.valueId === "vct_pcc_hybrid",
       "Epic turns the VCT+PCC hybrid on: " + fresh.giMode.valueId);
assert(fresh.refractions.valueId === "auto", "Epic sets refractions to Auto");

// ---- applying a tier writes THROUGH to the backing fields -------------------
assert(world.mode({ mode: "low" }) === "low", "world.mode({mode:'low'})");
var s = world.settings();
assert(s.msaa.value === 2, "Low sets MSAA to 2x (the chain is off there — real anti-aliasing): " + s.msaa.valueId);
assert(s.hdr.value === 0, "Low turns HDR off");
assert(s.ssao.valueId === "off", "Low turns ambient occlusion off");
assert(s.refractions.valueId === "off", "Low turns refractions off");
assert(s.shadowResolution.value === 512, "Low sets a 512 shadow atlas: " + s.shadowResolution.value);
assert(s.giMode.valueId === "off", "Low turns GI off: " + s.giMode.valueId);
assert(s.msaa.source === "mode", "an untouched row reports source 'mode'");
// The invariant: the backing field IS the resolved value, so every existing
// reader (SceneMirror, the serializer, the old verbs) sees the same number.
assert(world.get().antiAliasing === 2, "world.get().antiAliasing follows the tier (2x at Low)");
assert(world.get().shadowResolution === 512, "world.get().shadowResolution follows the tier");

assert(world.mode({ mode: "epic" }) === "epic", "world.mode({mode:'epic'})");
s = world.settings();
assert(s.msaa.value === 2, "Epic asks for 2x MSAA again: " + s.msaa.valueId);
// EPIC'S RETUNED ROWS (fps audit F6, perf wave 2026-09-06): the three
// heavyweights that were not earning their cost. Everything else about Epic is
// unchanged, which is what the assertions around these pin.
assert(s.shadowResolution.value === 2048, "Epic sets a 2048 shadow atlas: " + s.shadowResolution.value);
assert(s.giMode.valueId === "vct_pcc_hybrid",
       "Epic turns the VCT+PCC hybrid on: " + s.giMode.valueId);
assert(s.shadowFilter.valueId === "soft", "Epic filters shadows with PCF 4x4 (Soft)");
assert(s.hdr.value === 1 && s.bloom.value === 1, "Epic turns HDR and bloom on");
assert(s.ssao.valueId === "half", "Epic runs ambient occlusion at half resolution");
assert(s.smaa.valueId === "ultra", "Epic anti-aliases with SMAA Ultra");
assert(world.get().antiAliasing === 2, "the backing field followed Epic too (MSAA at the tier's 2x)");
assert(world.get().shadowResolution === 2048, "and Epic's shadow atlas landed in the field");

// ---- a pin survives a mode switch -------------------------------------------
var pinned = world.override({ id: "msaa", value: "4x" });
assert(pinned.value === 4 && pinned.source === "override", "world.override pins MSAA to 4x");
assert(world.get().antiAliasing === 4, "the pin wrote through to the backing field (4x)");
assert(pinned.tierValue === 2, "the row still reports what Epic would give it");

world.mode({ mode: "low" });
s = world.settings();
assert(s.msaa.value === 4, "the pin SURVIVED the switch to Low: " + s.msaa.valueId);
assert(s.msaa.source === "override", "and still reports itself as pinned");
assert(s.shadowResolution.value === 512, "unpinned rows still follow the new tier");

// values may also be given as raw numbers, and bad ones are refused
var threw = false;
try { world.override({ id: "msaa", value: "3x" }); } catch (e) { threw = true; }
assert(threw, "an invalid value is refused, catchably");
try { threw = false; world.override({ id: "nosuchrow", value: 1 }); } catch (e) { threw = true; }
assert(threw, "an unknown row is refused, catchably");
assert(world.override({ id: "shadowResolution", value: 2048 }).value === 2048,
       "a raw number works as well as the id spelling");

// ---- clearing a pin puts the tier's value back ------------------------------
var cleared = world.clearOverride({ id: "msaa" });
assert(cleared.source === "mode", "clearOverride drops the pin: " + cleared.source);
assert(cleared.value === 2, "and puts Low's value (2x) back: " + cleared.value);
assert(world.get().antiAliasing === 2, "the backing field followed (2x)");

s = world.clearOverrides();
assert(s.shadowResolution.source === "mode", "clearOverrides drops the rest");
assert(s.shadowResolution.value === 512, "and re-applies the mode everywhere");

// ---- the legacy setters record their own pins -------------------------------
// A direct field write that skipped the bookkeeping is the bug this design
// invites: the next mode switch would silently undo the user's choice.
world.setAntiAliasing(8);
assert(world.settings().msaa.source === "override",
       "world.setAntiAliasing records a pin: " + world.settings().msaa.source);
world.mode({ mode: "high" });
assert(world.get().antiAliasing === 8,
       "so switching to High leaves it alone: " + world.get().antiAliasing);
assert(world.get().shadowResolution === 2048, "while High moves the unpinned rows");

// ---- the continuous tuning is a verb, not a tier ----------------------------
var fxTuning = world.postFx();
// EXPOSURE-1: the exposure is in STOPS and a new scene is at ZERO of them —
// which is the grade its own lights derive, not a tuned constant.
assert(Math.abs(fxTuning.exposureEv) < 0.001,
       "a new scene's exposure is 0 stops (the derived default grade): " + fxTuning.exposureEv);
assert(world.settings().exposureMode.valueId === "manual",
       "…and the Exposure Mode row is MANUAL");
assert(byId["exposureMode"] && byId["exposureMode"].tierSpace === "none",
       "…a row NO tier resolves, so no mode switch can regrade a scene");
assert(!byId["exposureMode"].tiers,
       "…and it carries no tier columns at all, because there are none to carry");
// The old chain-unit key is REFUSED BY NAME rather than translated — the same
// number means two different pictures in the two units.
refuses(function () { world.postFx({ exposure: 1.25 }); },
        "world.postFx refuses the old chain-unit 'exposure' key");
fxTuning = world.postFx({ exposureEv: 1.25, bloomThreshold: 3.0, ssaoPower: 2.5, ssaoRadius: 4 });
assert(Math.abs(fxTuning.exposureEv - 1.25) < 0.001, "world.postFx sets the exposure");
assert(Math.abs(fxTuning.bloomThreshold - 3.0) < 0.001, "world.postFx sets the bloom threshold");
assert(Math.abs(fxTuning.ssaoPower - 2.5) < 0.001, "world.postFx sets the AO power");
assert(world.postFx({ exposureEv: 999 }).exposureEv === 16, "out-of-range exposure clamps");
world.postFx({ exposureEv: 1.25 });
// A mode switch must NOT regrade the scene: tuning is not a tier. NOR is the
// MODE a tier — it is a TierSpace::None row, so a mode switch leaves it too.
world.override({ id: "exposureMode", value: "auto" });
world.mode({ mode: "medium" });
assert(Math.abs(world.postFx().exposureEv - 1.25) < 0.001,
       "a mode switch leaves the tuning alone: " + world.postFx().exposureEv);
assert(world.settings().exposureMode.valueId === "auto",
       "…and leaves the exposure MODE alone too (no tier owns it)");
world.override({ id: "exposureMode", value: "manual" });
world.mode({ mode: "high" });

// ---- serialization round-trip ----------------------------------------------
assert(world.get().mode === "high", "world.get() reports the mode too");
project.save();
project.close();
project.open(guid);
assert(world.mode() === "high", "the mode survived save/close/open: " + world.mode());
s = world.settings();
assert(s.msaa.value === 8 && s.msaa.source === "override",
       "the pin survived too: " + s.msaa.valueId + " / " + s.msaa.source);
assert(s.shadowResolution.value === 2048, "and the tier-driven rows came back unchanged");

// ---- a tier switch is ONE undo step, and it is reversible ------------------
// The owner-reported defect of 2026-09-06 had two halves. This is the second:
// switching Epic -> High rewrites thirteen backing fields and, until
// WorldModeCommand, recorded NOTHING — the tier dropdown was a one-way door.
// (The first half was a UI-refresh bug in the World panel and is not reachable
// from a script; see SceneNodePropertiesWidget's worldSettingsChanged handler.)
function pushes() { return editor.undoState().pushes; }

world.clearOverrides();
world.mode({ mode: "epic" });
var epic = world.settings();
var beforePushes = pushes();
world.mode({ mode: "low" });
assert(pushes() === beforePushes + 1,
       "a tier switch records exactly ONE undo step, not one per row");
var low = world.settings();
assert(low.shadowResolution.value !== epic.shadowResolution.value,
       "Low really did move the rows: " + epic.shadowResolution.value +
       " -> " + low.shadowResolution.value);

beforePushes = pushes();
world.override({ id: "msaa", value: "4x" });
assert(pushes() === beforePushes + 1, "world.override records one undo step");
beforePushes = pushes();
world.clearOverride({ id: "msaa" });
assert(pushes() === beforePushes + 1, "world.clearOverride records one undo step");
beforePushes = pushes();
world.clearOverrides();
assert(pushes() === beforePushes + 1, "world.clearOverrides records one undo step");

world.mode({ mode: "high" });

console.log("e2e_world_modes: ALL OK");
