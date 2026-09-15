// scripting.e2e.photon — PHOTON's verb surface (SPECS/GI_UNIFIED_SPEC.md §2 / P2,
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
//   4. world.photon round-trips: what it reads is what world.gi and
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

var guid = project.create("Photon " + Date.now());
assert(guid.length > 10, "project.create -> " + guid);

// ---- 1. a new scene is born Realtime-Epic (D2) ------------------------------
var r = world.photon();
console.log("new scene: " + J(r));
assert(r.enabled === true, "a new scene is born with Photon ON");
assert(r.tier === "epic", "at the Epic tier: " + r.tier);
assert(r.technique === "vct_pcc_hybrid", "which resolves the hybrid: " + r.technique);
assert(r.quality === "high", "at high voxel/probe quality: " + r.quality);
assert(r.ddgi === true, "with the irradiance field on");
assert(r.bounces === 3, "three light bounces (Epic's column): " + r.bounces);
assert(r.dynamicProbes === undefined,
       "and NO dynamic-probe column at all (deleted with the feature, REALTIME_REFLECTIONS R2)");
assert(r.row && r.row.tier === "epic" && r.row.bounces === 3 && r.row.dynamicProbes === undefined &&
       r.row.technique === "vct_pcc_hybrid" && r.row.quality === "high" && r.row.ddgi === true,
       "world.photon().row is the effective table row: " + J(r.row));
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
assert(world.get().photon.tier === "epic", "world.get().photon agrees too");

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
    var got = world.photon({ tier: t });
    var want = expect[t];
    assert(got.tier === t, "world.photon({tier:'" + t + "'})");
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
assert(world.photon().tier === "medium", "world.gi's tier key drives the same dial");
// ... and so does the registry row, which is what the World Mode panel uses.
var row = world.override({ id: "photon", value: "high" });
assert(row.valueId === "high" && row.source === "override",
       "world.override({id:'photon'}) pins the dial: " + J([row.valueId, row.source]));
assert(world.photon().tier === "high", "and the dial followed");
world.clearOverride({ id: "photon" });

// An unknown tier is refused, catchably, and changes nothing.
var before = world.photon().tier;
var threw = false;
try { world.photon({ tier: "ultra" }); } catch (e) { threw = true; }
assert(threw, "an unknown tier is refused, catchably");
assert(world.photon().tier === before, "and the refused call changed nothing");
threw = false;
try { world.photon({ quality: "high" }); } catch (e) { threw = true; }
assert(threw, "an unknown KEY is refused too (the knobs are world.gi's)");

// ---- 3. the switch ----------------------------------------------------------
world.photon({ tier: "high" });
var off = world.photon({ enabled: false });
assert(off.enabled === false, "world.photon({enabled:false}) turns it off");
assert(off.technique === "off", "which is the renderer's GI mode off: " + off.technique);
assert(off.tier === "high", "and the tier is REMEMBERED: " + off.tier);
assert(world.get().gi.mode === "off", "world.gi sees it off too");
var on = world.photon({ enabled: true });
assert(on.enabled === true && on.tier === "high" && on.technique === "vct_pcc_hybrid",
       "turning it back on restores the remembered quality: " + J([on.tier, on.technique]));

// ---- 4. an Advanced pin survives a tier switch ------------------------------
// This is the Advanced disclosure's whole contract, driven through the verbs
// the panel calls: an explicit knob PINS itself and the tier stops writing it.
assert(world.gi({ quality: "low" }), "pin the voxel quality to low (an Advanced edit)");
var pinned = world.photon();
assert(pinned.quality === "low", "the field followed the pin");
assert(pinned.custom === true, "the tier row now reads Custom");
assert(pinned.deviations.length === 1, "with one named deviation: " + J(pinned.deviations));
assert(world.settings().giQuality.source === "override",
       "and the registry agrees it is pinned");

var afterSwitch = world.photon({ tier: "epic" });
assert(afterSwitch.technique === "vct_pcc_hybrid" && afterSwitch.ddgi === true,
       "the tier switch moved the unpinned knobs");
assert(afterSwitch.quality === "low", "and the PIN SURVIVED the tier switch");
assert(afterSwitch.custom === true, "so the dial still reads Custom");

// Handing it back is one verb, and it is the same one every other pinned
// quality row uses.
var cleared = world.clearOverride({ id: "giQuality" });
assert(cleared.source === "mode", "clearOverride drops the pin: " + cleared.source);
assert(world.photon().quality === "high", "and Epic's quality came back");
assert(world.photon().custom === false, "the dial is a clean Epic again");

// The irradiance field pins the same way, INCLUDING against Epic, and "auto"
// is how a script hands the decision back to the tier.
assert(world.gi({ ddgi: false }), "pin the field off at Epic");
assert(world.photon().ddgi === false && world.photon().custom === true,
       "Epic without its field is Custom");
assert(world.gi({ ddgi: "auto" }), "world.gi({ddgi:'auto'}) unpins it");
assert(world.photon().ddgi === true && world.photon().custom === false,
       "and the tier decides again");

// Epic's column pins the same way through world.gi's bounces key.
assert(world.gi({ bounces: 2 }), "pin the bounces to 2 at Epic");
assert(world.photon().bounces === 2 && world.photon().custom === true &&
       world.settings().giBounces.source === "override",
       "Epic at 2 bounces is Custom, with the registry agreeing: " + J(world.photon().deviations));
// THE DELETED KEY (R2): world.gi refuses it by name like any other unknown key.
threw = false;
try { world.gi({ dynamicProbes: 4 }); } catch (e) { threw = true; }
assert(threw, "the retired dynamicProbes key is refused by world.gi");
world.photon({ tier: "medium" });
assert(world.photon().bounces === 2, "the pin SURVIVES a tier switch to Medium");
assert(world.clearOverride({ id: "giBounces" }).source === "mode", "clearOverride hands it back");
assert(world.photon().bounces === 1 && world.photon().custom === false,
       "and Medium's 1 bounce comes back");
world.photon({ tier: "epic" });

// ---- 5. one undo step per gesture, and the round trip -----------------------
function pushes() { return editor.undoState().pushes; }
var beforePushes = pushes();
world.photon({ tier: "medium" });
assert(pushes() === beforePushes + 1, "a Photon tier switch records exactly ONE undo step");
beforePushes = pushes();
world.photon({ enabled: false });
assert(pushes() === beforePushes + 1, "and so does the switch");
world.photon({ enabled: true, tier: "high" });

project.save();
project.close();
project.open(guid);
var reopened = world.photon();
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
var afterTrip = world.photon();
assert(afterTrip.quality === "medium" && afterTrip.custom === true,
       "and so does a pin: " + J([afterTrip.quality, afterTrip.custom]));
assert(world.settings().giQuality.source === "override", "reported as pinned after reopen");

// ---- 6. the registry is honest about which dial owns which row --------------
var table = world.modeTable();
var byId = {};
for (var i = 0; i < table.rows.length; ++i) byId[table.rows[i].id] = table.rows[i];
assert(!!byId["photon"], "the registry declares the photon row");
assert(byId["photon"].tierSpace === "world", "which the WORLD mode drives");
assert(byId["giMode"].tierSpace === "photon", "while the technique row is Photon's");
assert(byId["giQuality"].tierSpace === "photon", "and so is the quality row");
assert(byId["giDdgi"].tierSpace === "photon", "and the irradiance-field row");
assert(byId["giBounces"].tierSpace === "photon", "and Epic's column row");
assert(!byId["giDynamicProbes"], "the retired dynamic-probe row is not in the registry at all");
assert(byId["giDdgi"].tiers.medium.valueId === "on" && byId["giDdgi"].tiers.high.valueId === "on" &&
       byId["giDdgi"].tiers.low.valueId === "off",
       "the registry's field column: on from Medium up, off at Low");
assert(byId["giBounces"].tiers.epic.value === 3 && byId["giBounces"].tiers.high.value === 1,
       "and Epic's column reads 3 bounces against High's 1");
assert(byId["giMode"].tiers.epic.valueId === "vct_pcc_hybrid",
       "the Photon columns are the Photon tiers: " + byId["giMode"].tiers.epic.valueId);

// ---- 7. the cascade table is validated, and refused WHOLE -------------------
// PHOTON's chain hands the ray march from each cascade to the coarser one
// behind it, and Ogre derives the hand-over LOD from the ratio of their cells —
// so a table that does not grow outward is not a request the renderer can
// honour halfway (audit B10). The engine falls back to the tier's table; the
// verb refuses by name, which is what a user gets to see.
var threwTable = false;
try {
    world.gi({ cascadeSet: [ { halfSize: 5, resolution: 128 },
                             { halfSize: 4, resolution: 128 } ] });
} catch (e) { threwTable = true; }
assert(threwTable, "world.gi refuses a cascadeSet that does not grow outward");

threwTable = false;
try {
    // same halfSize growth but a FINER outer cascade: the cell must grow too
    world.gi({ cascadeSet: [ { halfSize: 5, resolution: 64 },
                             { halfSize: 10, resolution: 256 } ] });
} catch (e) { threwTable = true; }
assert(threwTable, "...and one whose outer cascade is FINER than the inner");

var good = world.gi({ cascadeSet: [ { halfSize: 5, resolution: 128 },
                                    { halfSize: 20, resolution: 64 } ] });
assert(!!good, "a table that grows outward is accepted");
world.gi({ cascadeSet: [] });

// ---------------------------------------------------------------------------
// THE REFLECTION ROUGHNESS CUTOFF (PHOTON_SPEC §7 R5; owner, ledger §426).
//
// API-FIRST: the row is reached through the SAME generic verb every other World
// row is (`world.override` / `world.clearOverride`), so what is asserted here is
// the capability the panel's row will call — not a second path beside it.
var cut = world.override({ id: "reflectionRoughnessCutoff", value: 65 });
assert(cut.value === 65, "the reflection roughness cutoff takes a value: " + JSON.stringify(cut));
assert(world.settings().reflectionRoughnessCutoff.value === 65,
       "...and world.settings reads it back");

// THE RANGE IS THE ROW'S, and it is enforced where every other row's is — by
// REFUSING, loudly, rather than clamping: a silently clamped value is a setting
// the user typed and the renderer did not take.
var refused = false;
try { world.override({ id: "reflectionRoughnessCutoff", value: 0 }); }
catch (e) { refused = true; }
assert(refused, "a value below the row's minimum is refused");
assert(world.settings().reflectionRoughnessCutoff.value === 65,
       "...and the refusal left the pinned value alone");

// (UNDO is not asserted here. The row writes through `WorldModeCommand`, whose
// snapshot is generic over `worldmodes::rows()` — a new row is inside it by
// construction — and a SCRIPT RUN IS ONE UNDO MACRO, so an `editor.undo()` in
// the middle of this file would be undoing the macro this file is still
// building. `scripting.e2e.undo_macro` is where that path is measured.)

// ...AND IT IS NOT A QUALITY TRADE. Every tier column carries 40: the cost of a
// tier is the resolution and whether rays run at all, both rows of their own.
// Moving the world mode must not silently re-author a description of the
// scene's surfaces.
world.override({ id: "reflectionRoughnessCutoff", value: 20 });
world.mode({ mode: "epic" });
assert(world.settings().reflectionRoughnessCutoff.value === 20,
       "a pinned cutoff survives a tier change");
world.clearOverride({ id: "reflectionRoughnessCutoff" });
assert(world.settings().reflectionRoughnessCutoff.value === 40,
       "clearing the pin returns it to 40 in every tier");

console.log("e2e_photon: ALL OK");
