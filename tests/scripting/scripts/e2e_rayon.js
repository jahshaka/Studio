// scripting.e2e.rayon — RAYON's verb surface (SPECS/GI_UNIFIED_SPEC.md §2 / P2,
// owner decisions D2/D3/D5/D6).
//
// Headless (--headless): every verb here is a document verb. It drives exactly
// what the World panel's GI section drives, through the same API, and asserts
// the four things the unification promised:
//
//   1. a NEW scene is born Realtime-Epic (D2), and Epic means hybrid + high +
//      the irradiance field + three bounces (dynamic probes 0 since R0; its own
//      column since owner option (b), 2026-09-09);
//   2. the tier switch changes the underlying knobs, all of them, correctly —
//      the alias, world.gi's `tier` key and world.override all write the SAME
//      resolved model;
//   3. an Advanced pin SURVIVES a tier switch, is reported as a deviation, and
//      can be handed back;
//   4. world.rayon round-trips: what it reads is what world.gi and
//      world.settings say, and it survives save/close/open.
//
// The pixel half lives in the engine suites (gi.modes, gi.ddgi); the resolution
// half in gi.tiers. This is the API-first half — the verb existing, refusing
// nonsense, and being undoable.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function J(x) { return JSON.stringify(x); }

var guid = project.create("Rayon " + Date.now());
assert(guid.length > 10, "project.create -> " + guid);

// ---- 1. a new scene is born Realtime-Epic (D2) ------------------------------
var r = world.rayon();
console.log("new scene: " + J(r));
assert(r.enabled === true, "a new scene is born with Rayon ON");
assert(r.tier === "epic", "at the Epic tier: " + r.tier);
assert(r.technique === "vct_pcc_hybrid", "which resolves the hybrid: " + r.technique);
assert(r.quality === "high", "at high voxel/probe quality: " + r.quality);
assert(r.ddgi === true, "with the irradiance field on");
assert(r.bounces === 3, "three light bounces (Epic's column): " + r.bounces);
assert(r.dynamicProbes === undefined,
       "and NO dynamic-probe column at all (deleted with the feature, REALTIME_REFLECTIONS R2)");
assert(r.row && r.row.tier === "epic" && r.row.bounces === 3 && r.row.dynamicProbes === undefined &&
       r.row.technique === "vct_pcc_hybrid" && r.row.quality === "high" && r.row.ddgi === true,
       "world.rayon().row is the effective table row: " + J(r.row));
assert(Math.abs(r.ddgiIntensity - 1.0) < 1e-6, "at the calibrated intensity 1.0");
assert(r.ddgiSource === "auto", "the field's source is auto (voxel at every tier)");
assert(r.custom === false, "and nothing pinned: " + J(r.deviations));
// world.gi is the same model, read through the full surface.
var gi = world.get().gi;
assert(gi.tier === "epic" && gi.mode === "vct_pcc_hybrid" && gi.quality === "high" &&
       gi.bounces === 3 && gi.dynamicProbes === undefined,
       "world.get().gi agrees, without the deleted key: " + J([gi.tier, gi.mode, gi.quality, gi.bounces]));
var gs = world.giStatus();
assert(gs.dynamicProbes === undefined && gs.dynamicProbeUpdates === undefined,
       "and giStatus has dropped both dynamic-probe fields");
assert(world.get().rayon.tier === "epic", "world.get().rayon agrees too");

// ---- 2. the tier switch moves the knobs -------------------------------------
// THE TABLE (owner option (b)): Medium and High are DDGI-fed; Epic's column
// is the bounces — the only one left since R2 deleted the dynamic probes.
var expect = {
    low:    { technique: "instant_radiosity", quality: "low",    ddgi: false, bounces: 1 },
    medium: { technique: "vct",               quality: "medium", ddgi: true,  bounces: 1 },
    high:   { technique: "vct_pcc_hybrid",    quality: "high",   ddgi: true,  bounces: 1 },
    epic:   { technique: "vct_pcc_hybrid",    quality: "high",   ddgi: true,  bounces: 3 }
};
for (var t in expect) {
    var got = world.rayon({ tier: t });
    var want = expect[t];
    assert(got.tier === t, "world.rayon({tier:'" + t + "'})");
    assert(got.technique === want.technique, "  technique -> " + got.technique);
    assert(got.quality === want.quality, "  quality -> " + got.quality);
    assert(got.ddgi === want.ddgi, "  irradiance field -> " + got.ddgi);
    assert(got.bounces === want.bounces, "  bounces -> " + got.bounces);
    assert(got.row.bounces === want.bounces && got.row.ddgi === want.ddgi,
           "  and the row readback matches the table");
    // The write-through invariant, seen from the OTHER verb: the backing
    // fields the mirror and the serializer read are the resolved values.
    var w = world.get().gi;
    assert(w.mode === want.technique && w.quality === want.quality &&
           w.bounces === want.bounces, "  and world.gi reads the same fields back");
    assert(got.custom === false, "  no deviation after a clean tier switch");
}

// world.gi's `tier` key is the same write (the full surface gains the key; the
// alias is only a shorthand — D6).
assert(world.gi({ tier: "medium" }), "world.gi({tier:'medium'})");
assert(world.rayon().tier === "medium", "world.gi's tier key drives the same dial");
// ... and so does the registry row, which is what the World Mode panel uses.
var row = world.override({ id: "rayon", value: "high" });
assert(row.valueId === "high" && row.source === "override",
       "world.override({id:'rayon'}) pins the dial: " + J([row.valueId, row.source]));
assert(world.rayon().tier === "high", "and the dial followed");
world.clearOverride({ id: "rayon" });

// An unknown tier is refused, catchably, and changes nothing.
var before = world.rayon().tier;
var threw = false;
try { world.rayon({ tier: "ultra" }); } catch (e) { threw = true; }
assert(threw, "an unknown tier is refused, catchably");
assert(world.rayon().tier === before, "and the refused call changed nothing");
threw = false;
try { world.rayon({ quality: "high" }); } catch (e) { threw = true; }
assert(threw, "an unknown KEY is refused too (the knobs are world.gi's)");

// ---- 3. the switch ----------------------------------------------------------
world.rayon({ tier: "high" });
var off = world.rayon({ enabled: false });
assert(off.enabled === false, "world.rayon({enabled:false}) turns it off");
assert(off.technique === "off", "which is the renderer's GI mode off: " + off.technique);
assert(off.tier === "high", "and the tier is REMEMBERED: " + off.tier);
assert(world.get().gi.mode === "off", "world.gi sees it off too");
var on = world.rayon({ enabled: true });
assert(on.enabled === true && on.tier === "high" && on.technique === "vct_pcc_hybrid",
       "turning it back on restores the remembered quality: " + J([on.tier, on.technique]));

// ---- 4. an Advanced pin survives a tier switch ------------------------------
// This is the Advanced disclosure's whole contract, driven through the verbs
// the panel calls: an explicit knob PINS itself and the tier stops writing it.
assert(world.gi({ quality: "low" }), "pin the voxel quality to low (an Advanced edit)");
var pinned = world.rayon();
assert(pinned.quality === "low", "the field followed the pin");
assert(pinned.custom === true, "the tier row now reads Custom");
assert(pinned.deviations.length === 1, "with one named deviation: " + J(pinned.deviations));
assert(world.settings().giQuality.source === "override",
       "and the registry agrees it is pinned");

var afterSwitch = world.rayon({ tier: "epic" });
assert(afterSwitch.technique === "vct_pcc_hybrid" && afterSwitch.ddgi === true,
       "the tier switch moved the unpinned knobs");
assert(afterSwitch.quality === "low", "and the PIN SURVIVED the tier switch");
assert(afterSwitch.custom === true, "so the dial still reads Custom");

// Handing it back is one verb, and it is the same one every other pinned
// quality row uses.
var cleared = world.clearOverride({ id: "giQuality" });
assert(cleared.source === "mode", "clearOverride drops the pin: " + cleared.source);
assert(world.rayon().quality === "high", "and Epic's quality came back");
assert(world.rayon().custom === false, "the dial is a clean Epic again");

// The irradiance field pins the same way, INCLUDING against Epic, and "auto"
// is how a script hands the decision back to the tier.
assert(world.gi({ ddgi: false }), "pin the field off at Epic");
assert(world.rayon().ddgi === false && world.rayon().custom === true,
       "Epic without its field is Custom");
assert(world.gi({ ddgi: "auto" }), "world.gi({ddgi:'auto'}) unpins it");
assert(world.rayon().ddgi === true && world.rayon().custom === false,
       "and the tier decides again");

// Epic's column pins the same way through world.gi's bounces key.
assert(world.gi({ bounces: 2 }), "pin the bounces to 2 at Epic");
assert(world.rayon().bounces === 2 && world.rayon().custom === true &&
       world.settings().giBounces.source === "override",
       "Epic at 2 bounces is Custom, with the registry agreeing: " + J(world.rayon().deviations));
// THE DELETED KEY (R2): world.gi refuses it by name like any other unknown key.
threw = false;
try { world.gi({ dynamicProbes: 4 }); } catch (e) { threw = true; }
assert(threw, "the retired dynamicProbes key is refused by world.gi");
world.rayon({ tier: "medium" });
assert(world.rayon().bounces === 2, "the pin SURVIVES a tier switch to Medium");
assert(world.clearOverride({ id: "giBounces" }).source === "mode", "clearOverride hands it back");
assert(world.rayon().bounces === 1 && world.rayon().custom === false,
       "and Medium's 1 bounce comes back");
world.rayon({ tier: "epic" });

// ---- 5. one undo step per gesture, and the round trip -----------------------
function pushes() { return editor.undoState().pushes; }
var beforePushes = pushes();
world.rayon({ tier: "medium" });
assert(pushes() === beforePushes + 1, "a Rayon tier switch records exactly ONE undo step");
beforePushes = pushes();
world.rayon({ enabled: false });
assert(pushes() === beforePushes + 1, "and so does the switch");
world.rayon({ enabled: true, tier: "high" });

project.save();
project.close();
project.open(guid);
var reopened = world.rayon();
assert(reopened.enabled === true && reopened.tier === "high",
       "the dial survived save/close/open: " + J([reopened.enabled, reopened.tier]));
assert(reopened.technique === "vct_pcc_hybrid" && reopened.quality === "high",
       "with its resolved knobs intact");

// A pin survives the round trip too — it is a worldOverrides entry like any
// other, which is the whole reason the tier rides the registry.
world.gi({ quality: "medium" });
project.save();
project.close();
project.open(guid);
var afterTrip = world.rayon();
assert(afterTrip.quality === "medium" && afterTrip.custom === true,
       "and so does a pin: " + J([afterTrip.quality, afterTrip.custom]));
assert(world.settings().giQuality.source === "override", "reported as pinned after reopen");

// ---- 6. the registry is honest about which dial owns which row --------------
var table = world.modeTable();
var byId = {};
for (var i = 0; i < table.rows.length; ++i) byId[table.rows[i].id] = table.rows[i];
assert(!!byId["rayon"], "the registry declares the rayon row");
assert(byId["rayon"].tierSpace === "world", "which the WORLD mode drives");
assert(byId["giMode"].tierSpace === "rayon", "while the technique row is Rayon's");
assert(byId["giQuality"].tierSpace === "rayon", "and so is the quality row");
assert(byId["giDdgi"].tierSpace === "rayon", "and the irradiance-field row");
assert(byId["giBounces"].tierSpace === "rayon", "and Epic's column row");
assert(!byId["giDynamicProbes"], "the retired dynamic-probe row is not in the registry at all");
assert(byId["giDdgi"].tiers.medium.valueId === "on" && byId["giDdgi"].tiers.high.valueId === "on" &&
       byId["giDdgi"].tiers.low.valueId === "off",
       "the registry's field column: on from Medium up, off at Low");
assert(byId["giBounces"].tiers.epic.value === 3 && byId["giBounces"].tiers.high.value === 1,
       "and Epic's column reads 3 bounces against High's 1");
assert(byId["giMode"].tiers.epic.valueId === "vct_pcc_hybrid",
       "the Rayon columns are the Rayon tiers: " + byId["giMode"].tiers.epic.valueId);

console.log("e2e_rayon: ALL OK");
