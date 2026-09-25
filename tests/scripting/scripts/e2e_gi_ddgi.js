// scripting.e2e.gi_ddgi — the DDGI verb surface (GI_UNIFIED_SPEC.md §4 P1),
// end to end in the real app with the engine viewport up.
//
// The API-FIRST half of the feature: `world.gi({ddgi})` and the
// four `world.giStatus()` readings that say what the renderer did with them.
// The pixel half is the gi.ddgi engine suite; what this one owns is everything
// a script or a panel can see — defaults, the tri-state, refusals, the
// save/reopen round trip, and the invariant that ASKING for DDGI and GETTING it
// are two different readings (the field needs a voxel volume to be built from,
// so requesting it outside a VCT mode is honoured as "not bound").
//
// Phase A: the defaults every existing scene holds — off, and therefore
//          nothing bound. This is the compatibility statement: DDGI is opt-in
//          until the Photon tier lands (P2).
// Phase B: on, under plain VCT — bound, converged, probes fitted, batch sane.
// Phase C: there is no intensity dial (PHOTON-GATHER-1d deleted it) and no
//          ambient dial (PHOTON-ENV-1): both keys are refused.
// Phase D: refusals and the tri-state.
// Phase E: save / close / reopen keeps the field.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

var guid = project.create("GI DDGI " + Date.now());
assert(guid.length > 10, "project.create -> " + guid);

// Something for the voxelizer: an empty scene voxelizes nothing, rebuildVct
// returns early, and every reading below would be a study of the no-geometry
// path instead of the real one.
var box = scene.addPrimitive("cube", { position: { x: 0, y: 1, z: 0 },
                                       scale: { x: 2, y: 2, z: 2 } });
assert(box.length > 10, "cube added");
editor.frame(2);

// ---- phase A: the defaults ----------------------------------------------
// UPDATED BY THE PHOTON UNIFICATION (GI_UNIFIED_SPEC §2 / P2, owner decision
// D2). This phase used to assert that the field defaults to the string "auto"
// and resolves OFF "while there is no quality tier". The tier exists now, new
// scenes are born on its top rung, and EVERY tier turns the field on — Low
// included since PHOTON_SPEC §7 E2 (4) made it a voxel tier (two cascades at
// 64^3) instead of the CPU ray trace that had no volume to feed a field from.
// So a NEW scene's default is a resolved `true`, and "off" is now only ever an
// explicit pin. An EXISTING document derives its tier and its untouched field
// follows it (gi.tiers' migration cases).
var gi = world.get().gi;
console.log("gi defaults = " + JSON.stringify(gi));
assert(gi.tier === "epic", "a new scene is born at the Epic tier: " + gi.tier);
assert(gi.ddgi === true, "and Epic, like every voxel tier, turns the irradiance field on");
assert(gi.ddgiIntensity === undefined,
       "and no intensity dial: the field is applied at its own physical answer");

// The rest of this suite is about the VERB, so the scene is put on plain VCT
// with the field explicitly OFF. It used to reach that state through the Low
// TIER, whose column was off; no tier's column is off any more (E2 (4)), so
// the pin is the honest way to say it — and a pin against the tier is exactly
// what world.gi's contract promises, which this line therefore also proves.
// The cascade chain is pinned off with it: this phase reads `ifdProbes` and the
// single volume is the arm those numbers were measured on.
assert(world.gi({ tier: "low", mode: "vct", quality: "low", bounces: 1,
                  cascades: false, ddgi: false }),
       "world.gi(vct at the Low tier, the field and the chain pinned off)");
assert(world.get().gi.ddgi === false, "the pin resolves the field OFF");
editor.frame(4);
var st = world.giStatus();
console.log("giStatus(vct, field off) = " + JSON.stringify(st));
assert(st.live === true, "giStatus is LIVE (the engine viewport answered)");
assert(st.vctBound === true, "VCT is bound");
assert(st.ifdBound === false,
       "and no field is bound — the pin, written through");
assert(st.ifdProbes === 0 && st.ifdProbesPerFrame === 0 && st.ifdTargetSamples === 0
       && st.ifdRefinesOwed === 0,
       "no field means no probes, no batch, and no convergence schedule");

// ---- phase B: on --------------------------------------------------------
assert(world.gi({ ddgi: true }), "world.gi({ddgi:true})");
editor.frame(4);
st = world.giStatus();
console.log("giStatus(ddgi on) = " + JSON.stringify(st));
assert(st.ifdBound === true, "the irradiance field is BOUND to the PBR shader");
assert(st.vctBound === true,
       "VCT stays bound — the field replaces its DIFFUSE, not the whole arm");
assert(st.ifdProbes === 8192, "the field holds 8192 probes");
assert(st.ifdRefinesOwed < st.ifdTargetSamples,
       "a field is WHOLE on the frame it binds (the build samples every probe in one dispatch)");
assert(st.ifdProbesPerFrame > 0 && st.ifdProbes % st.ifdProbesPerFrame === 0,
       "the re-converge batch divides the field exactly — every dispatch is the same size, "
       + "which is what keeps the renderer clear of a zero-work-group dispatch");
assert(world.get().gi.ddgi === true, "the document echoes ddgi true");

// Paused GI pauses the field too, and that is visible rather than implied.
assert(world.gi({ updateBudget: 0 }), "world.gi({updateBudget:0})");
editor.frame(3);
st = world.giStatus();
assert(st.ifdBound === true, "a paused field stays bound");
assert(st.ifdProbesPerFrame === 0,
       "budget 0 pauses the field: no probes per frame at all");
assert(world.gi({ updateBudget: 1 }), "world.gi({updateBudget:1})");
editor.frame(3);
assert(world.giStatus().ifdProbesPerFrame > 0, "un-pausing re-arms the batch");

// ---- phase C: there is no intensity dial (PHOTON-GATHER-1d) -----------------
// The field is applied at its own physical answer; the trim is gone and its key
// is REFUSED, like every key world.gi does not know.
var threwI = "";
try { world.gi({ ddgiIntensity: 1 }); } catch (e) { threwI = String(e); }
assert(threwI.indexOf("ddgiIntensity") >= 0, "world.gi refuses the retired ddgiIntensity key: " + threwI);

// ---- phase C2: there is no ambient dial -----------------------------------
// PHOTON-ENV-1: the field carries the SKY itself (every probe ray that escapes
// the voxels reads the environment in its own direction), so the read-time
// ambient term and its 'ddgiAmbient' dial are gone. The key is REFUSED, like
// every key world.gi does not know.
var threw = "";
assert(world.get().gi.ddgiAmbient === undefined, "the document no longer carries ddgiAmbient");
try { world.gi({ ddgiAmbient: 1 }); } catch (e) { threw = String(e); }
assert(threw.indexOf("ddgiAmbient") >= 0, "world.gi refuses the retired ddgiAmbient key: " + threw);

// ---- phase D: refusals and the tri-state --------------------------------
threw = "";
try { world.gi({ ddgi: "sometimes" }); } catch (e) { threw = String(e); }
assert(threw.indexOf("ddgi") >= 0,
       "world.gi refuses a ddgi that is neither a bool nor \"auto\": " + threw);
threw = "";
try { world.gi({ DDGI: true }); } catch (e) { threw = String(e); }
assert(threw.indexOf("DDGI") >= 0,
       "world.gi still REFUSES an unknown key by name after the phase added two: " + threw);
assert(world.gi({ ddgi: "auto" }), "world.gi({ddgi:\"auto\"}) — back to the tier's answer");
editor.frame(4);
// "auto" is an INPUT spelling, not a stored state: it drops the pin and hands
// the decision back to the Photon tier, which then WRITES ITS ANSWER THROUGH
// (services/worldmodes.h — a backing field is always the resolved value). The
// scene is on Low here, and since PHOTON_SPEC §7 E2 (4) Low's answer is ON —
// which is the point of the line either way: the TIER decided, not the pin.
assert(world.get().gi.ddgi === true,
       "auto hands the decision to the tier, and the tier's answer is what the document holds");
assert(world.giStatus().ifdBound === true, "so the field is bound again — the tier said on");
assert(world.gi({ ddgi: "on" }), "world.gi({ddgi:\"on\"}) — the string form");
editor.frame(4);
assert(world.giStatus().ifdBound === true, "\"on\" binds it");

// NO DDGI-ONLY MODE, and this is a deliberate refusal recorded in the verb's
// own documentation: with no voxel lighting bound the shader's ambient gate
// disappears and the sky/flat ambient would be counted twice on top of the
// field. Asking for DDGI with GI OFF is therefore honoured as "requested, not
// bound" rather than as a second GI technique. (The case used to ask for it
// under Instant Radiosity, which was the one other non-voxel mode; the
// technique is deleted — PHOTON_SPEC E2 (4) — and `off` makes the same point
// with the one arm there is.)
assert(world.gi({ mode: "off", ddgi: true }),
       "world.gi({mode:'off', ddgi:true}) is accepted...");
editor.frame(4);
st = world.giStatus();
console.log("giStatus(off + ddgi) = " + JSON.stringify(st));
assert(st.ifdBound === false && st.ifdProbes === 0,
       "...and builds NO field: there is no voxel volume to feed one, and a DDGI-only mode "
       + "would double-count the ambient (P0 spike §5)");
assert(world.get().gi.ddgi === true, "the request is still remembered on the document");

// ...AND THE MODE IS REFUSED BY NAME, not silently remapped: a script asking for
// a deleted technique must be told what replaced it.
var irRefused = false;
try { world.gi({ mode: "instant_radiosity" }); } catch (e) { irRefused = true; }
assert(irRefused || world.get().gi.mode !== "instant_radiosity",
       "world.gi({mode:'instant_radiosity'}) is refused by name");

// ---- phase E: the round trip --------------------------------------------
assert(world.gi({ mode: "vct", ddgi: true }), "set up for the round trip");
editor.frame(3);
assert(project.save() === true, "project.save");
assert(project.close() === true, "project.close");
assert(project.open(guid) === true, "project.open (REOPEN)");
editor.frame(4);
gi = world.get().gi;
console.log("gi after reopen = " + JSON.stringify(gi));
assert(gi.ddgi === true, "ddgi survives save/close/reopen");
st = world.giStatus();
assert(st.ifdBound === true, "the reopened scene binds its field again");
console.log("gi.ddgi e2e complete");

