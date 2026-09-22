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

// `world.tierTable()` — what each Photon tier physically is, its VR column and
// its ATOM column — is scripting.e2e.tier_table_atom's subject and was MOVED
// there whole. This suite is about the registry and how a mode resolves.

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
// THE BLOOM AMOUNT (owner review R17). A new scene is at 1 — the amount this
// renderer has always drawn — so the row cannot regrade anybody's saved
// picture; the range is 0 to 2 and the verb clamps to it, like every other
// parameter in the table.
assert(Math.abs(world.postFx().bloomAmount - 1.0) < 0.001,
       "a new scene's bloom amount is 1x: " + world.postFx().bloomAmount);
assert(Math.abs(world.postFx({ bloomAmount: 1.6 }).bloomAmount - 1.6) < 0.001,
       "world.postFx sets the bloom amount");
assert(world.postFx({ bloomAmount: 9 }).bloomAmount === 2,
       "…and clamps it to 2x (twice the bloom is the top of the dial)");
assert(world.postFx({ bloomAmount: -1 }).bloomAmount === 0,
       "…and to 0, which is the bloom-off picture with the chain still standing");
world.postFx({ bloomAmount: 1 });
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

// ---- THE METER (EXPOSURE-2): the pattern is a row, the clips are tuning -----
// WHERE the meter looks is a CHOICE, so it is a row beside Exposure Mode and a
// TierSpace::None one for the same reason (metering is an art decision, not a
// scalability question). WHICH SLICE of what it sees it believes is two
// numbers, so they are postFx tuning like the window.
assert(byId["exposureMetering"] && byId["exposureMetering"].tierSpace === "none",
       "the Metering row exists and no tier resolves it");
assert(world.settings().exposureMetering.valueId === "centreWeighted",
       "a new scene meters CENTRE WEIGHTED (the classic camera default): " +
       world.settings().exposureMetering.valueId);
world.override({ id: "exposureMetering", value: "spot" });
assert(world.settings().exposureMetering.valueId === "spot",
       "world.override sets the metering pattern");
world.override({ id: "exposureMetering", value: "average" });
assert(world.settings().exposureMetering.valueId === "average",
       "…and every one of the three patterns round-trips");
world.override({ id: "exposureMetering", value: "centreWeighted" });
var meter = world.postFx();
assert(Math.abs(meter.exposureMeterLow - 10) < 0.001 &&
       Math.abs(meter.exposureMeterHigh - 90) < 0.001,
       "the meter's percentile clips default to 10/90: " + meter.exposureMeterLow +
       ".." + meter.exposureMeterHigh);
meter = world.postFx({ exposureMeterLow: 2.5, exposureMeterHigh: 97.5 });
assert(Math.abs(meter.exposureMeterLow - 2.5) < 0.001 &&
       Math.abs(meter.exposureMeterHigh - 97.5) < 0.001,
       "world.postFx sets them");
// A REVERSED pair is ORDERED, not obeyed: the renderer walks a cumulative
// weight and a reversed pair would select nothing at all.
meter = world.postFx({ exposureMeterLow: 80, exposureMeterHigh: 20 });
assert(meter.exposureMeterLow === 20 && meter.exposureMeterHigh === 80,
       "a reversed percentile pair is put back in order: " + meter.exposureMeterLow +
       ".." + meter.exposureMeterHigh);
assert(world.postFx({ exposureMeterHigh: 999 }).exposureMeterHigh === 100,
       "out-of-range percentiles clamp");
world.postFx({ exposureMeterLow: 10, exposureMeterHigh: 90 });
// ...and a tier switch leaves the meter exactly where it was, like the rest of
// the exposure model.
world.override({ id: "exposureMetering", value: "spot" });
world.postFx({ exposureMeterLow: 5 });
world.mode({ mode: "low" });
assert(world.settings().exposureMetering.valueId === "spot" &&
       Math.abs(world.postFx().exposureMeterLow - 5) < 0.001,
       "a tier switch regrades nothing about the meter");
world.mode({ mode: "high" });
world.clearOverride({ id: "exposureMetering" });
world.postFx({ exposureMeterLow: 10 });

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
