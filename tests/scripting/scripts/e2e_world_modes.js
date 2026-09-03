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
assert(!!byId["planarBudget"], "row planarBudget is declared as a contract");
assert(byId["planarBudget"].available === false,
       "planarBudget renders disabled until the planar lane lands");
for (var k = 0; k < table.rows.length; ++k) {
    var r = table.rows[k];
    assert(r.label.length > 0 && r.cost.length > 0, "row " + r.id + " has a label and a cost note");
    assert(!!r.tiers.low && !!r.tiers.epic, "row " + r.id + " declares all four tier values");
}

// ---- a fresh scene starts on EPIC ------------------------------------------
// POST_CHAIN_SPEC §12 decision 8 (owner call): new scenes are Epic, and a
// document written before World Modes existed reads as Epic too.
assert(world.mode() === "epic", "a new scene starts on Epic: " + world.mode());
var fresh = world.settings();
assert(fresh.hdr.value === 1, "Epic turns HDR on");
// Hardware MSAA is 1x in EVERY tier: with the post chain on it either crashes
// the driver (HDR) or renders black (ambient occlusion), both reproduced in
// tests/engine. SMAA does the anti-aliasing instead.
assert(fresh.msaa.value === 1, "Epic leaves hardware MSAA off: " + fresh.msaa.valueId);
assert(fresh.smaa.valueId === "ultra", "Epic anti-aliases with SMAA Ultra: " + fresh.smaa.valueId);
assert(fresh.giMode.valueId === "vct", "Epic turns VCT GI on");
assert(fresh.refractions.valueId === "auto", "Epic sets refractions to Auto");

// ---- applying a tier writes THROUGH to the backing fields -------------------
assert(world.mode({ mode: "low" }) === "low", "world.mode({mode:'low'})");
var s = world.settings();
assert(s.msaa.value === 1, "Low sets MSAA off: " + s.msaa.valueId);
assert(s.hdr.value === 0, "Low turns HDR off");
assert(s.ssao.valueId === "off", "Low turns ambient occlusion off");
assert(s.refractions.valueId === "off", "Low turns refractions off");
assert(s.shadowResolution.value === 512, "Low sets a 512 shadow atlas: " + s.shadowResolution.value);
assert(s.giMode.valueId === "off", "Low turns GI off: " + s.giMode.valueId);
assert(s.msaa.source === "mode", "an untouched row reports source 'mode'");
// The invariant: the backing field IS the resolved value, so every existing
// reader (SceneMirror, the serializer, the old verbs) sees the same number.
assert(world.get().antiAliasing === 1, "world.get().antiAliasing follows the tier");
assert(world.get().shadowResolution === 512, "world.get().shadowResolution follows the tier");

assert(world.mode({ mode: "epic" }) === "epic", "world.mode({mode:'epic'})");
s = world.settings();
assert(s.msaa.value === 1, "Epic leaves hardware MSAA off: " + s.msaa.valueId);
assert(s.shadowResolution.value === 4096, "Epic sets a 4096 shadow atlas: " + s.shadowResolution.value);
assert(s.giMode.valueId === "vct", "Epic turns VCT GI on: " + s.giMode.valueId);
assert(s.shadowFilter.valueId === "verysoft", "Epic uses the softest shadow filter");
assert(s.hdr.value === 1 && s.bloom.value === 1, "Epic turns HDR and bloom on");
assert(s.ssao.valueId === "full", "Epic runs ambient occlusion at full resolution");
assert(s.smaa.valueId === "ultra", "Epic anti-aliases with SMAA Ultra");
assert(world.get().antiAliasing === 4, "the backing field followed Epic too");

// ---- a pin survives a mode switch -------------------------------------------
var pinned = world.override({ id: "msaa", value: "2x" });
assert(pinned.value === 2 && pinned.source === "override", "world.override pins MSAA to 2x");
assert(world.get().antiAliasing === 2, "the pin wrote through to the backing field");
assert(pinned.tierValue === 1, "the row still reports what Epic would give it");

world.mode({ mode: "low" });
s = world.settings();
assert(s.msaa.value === 2, "the pin SURVIVED the switch to Low: " + s.msaa.valueId);
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
assert(cleared.value === 1, "and puts Low's value back: " + cleared.value);
assert(world.get().antiAliasing === 1, "the backing field followed");

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

console.log("e2e_world_modes: ALL OK");
