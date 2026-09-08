// scripting.e2e.gi_ddgi — the DDGI verb surface (GI_UNIFIED_SPEC.md §4 P1),
// end to end in the real app with the engine viewport up.
//
// The API-FIRST half of the feature: `world.gi({ddgi, ddgiIntensity})` and the
// four `world.giStatus()` readings that say what the renderer did with them.
// The pixel half is the gi.ddgi engine suite; what this one owns is everything
// a script or a panel can see — defaults, the tri-state, refusals, the
// save/reopen round trip, and the invariant that ASKING for DDGI and GETTING it
// are two different readings (the field needs a voxel volume to be built from,
// so requesting it outside a VCT mode is honoured as "not bound").
//
// Phase A: the defaults every existing scene holds — off, and therefore
//          nothing bound. This is the compatibility statement: DDGI is opt-in
//          until the Rayon tier lands (P2).
// Phase B: on, under plain VCT — bound, converged, probes fitted, batch sane.
// Phase C: the intensity scalar, including the A/B value 0.
// Phase D: refusals and the tri-state.
// Phase E: save / close / reopen keeps both fields.

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
// UPDATED BY THE RAYON UNIFICATION (GI_UNIFIED_SPEC §2 / P2, owner decision
// D2). This phase used to assert that the field defaults to the string "auto"
// and resolves OFF "while there is no quality tier". The tier exists now, new
// scenes are born on its top rung, and Epic is the one rung that turns the
// field on — so a NEW scene's default is a resolved `true`. The compatibility
// statement that used to live here has not been dropped, it has moved to where
// it can still be true: an EXISTING document derives the tier its serialized
// settings correspond to and never lands on Epic unless it already asked for a
// field (gi.tiers' migration cases, plus the byte-identical sample opens the
// lane measured).
var gi = world.get().gi;
console.log("gi defaults = " + JSON.stringify(gi));
assert(gi.tier === "epic", "a new scene is born at the Epic tier: " + gi.tier);
assert(gi.ddgi === true, "and Epic is the tier that turns the irradiance field on");
assert(Math.abs(gi.ddgiIntensity - 1.0) < 1e-4,
       "ddgiIntensity defaults to 1.0 (the renderer's raw brightness, measured to be the "
       + "right one: the field lands at ~86% of the cone-traced diffuse it replaces)");

// The rest of this suite is about the VERB, so the scene is put where every
// pre-Rayon document sits: a plain-VCT tier, whose field is off. That the tier
// switch alone turns the field off — no explicit ddgi key anywhere — is itself
// the write-through statement this phase makes.
assert(world.gi({ tier: "medium", mode: "vct", quality: "low", bounces: 1,
                  boundsMin: { x: -6, y: -1, z: -6 },
                  boundsMax: { x: 6, y: 6, z: 6 } }), "world.gi(vct at the Medium tier)");
assert(world.get().gi.ddgi === false, "the Medium tier resolves the field OFF");
editor.frame(4);
var st = world.giStatus();
console.log("giStatus(vct, field off) = " + JSON.stringify(st));
assert(st.live === true, "giStatus is LIVE (the engine viewport answered)");
assert(st.vctBound === true, "VCT is bound");
assert(st.ifdBound === false,
       "and no field is bound below Epic — which is what makes every scene written before "
       + "this feature render unchanged");
assert(st.ifdProbes === 0 && st.ifdProbesPerFrame === 0 && st.ifdConverged === false,
       "no field means no probes, no batch, and nothing converged");

// ---- phase B: on --------------------------------------------------------
assert(world.gi({ ddgi: true }), "world.gi({ddgi:true})");
editor.frame(4);
st = world.giStatus();
console.log("giStatus(ddgi on) = " + JSON.stringify(st));
assert(st.ifdBound === true, "the irradiance field is BOUND to the PBR shader");
assert(st.vctBound === true,
       "VCT stays bound — the field replaces its DIFFUSE, not the whole arm");
assert(st.ifdProbes === 8192, "the field holds 8192 probes");
assert(st.ifdConverged === true,
       "a field is converged on the frame it binds (the build converges it in one dispatch)");
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

// ---- phase C: the intensity ---------------------------------------------
assert(world.gi({ ddgiIntensity: 0 }), "world.gi({ddgiIntensity:0})");
editor.frame(3);
st = world.giStatus();
assert(st.ifdBound === true,
       "intensity 0 leaves the field BOUND — it is a shader scalar, not a second switch "
       + "(this is the A/B measurement, not an off state)");
assert(Math.abs(world.get().gi.ddgiIntensity) < 1e-6, "the document echoes intensity 0");
assert(world.gi({ ddgiIntensity: 2.5 }), "world.gi({ddgiIntensity:2.5})");
editor.frame(3);
assert(Math.abs(world.get().gi.ddgiIntensity - 2.5) < 1e-4, "the document echoes intensity 2.5");

// ---- phase C2: the ambient sky-visibility dial ---------------------------
// The Rayon ambient fix's one knob. The PIXEL contract is gi.ddgi_ambient's
// (recovery on an open scene, invariance in a sealed one); what belongs here is
// that the verb exists, round-trips through the document, and refuses nonsense
// — the API-first half.
assert(Math.abs(world.get().gi.ddgiAmbient - 1.0) < 1e-6,
       "ddgiAmbient defaults to 1.0 — the fix is ON, because it corrects a term every "
       + "DDGI scene was MISSING");
assert(world.gi({ ddgiAmbient: 0 }), "world.gi({ddgiAmbient:0})");
editor.frame(3);
st = world.giStatus();
assert(st.ifdBound === true,
       "ambient 0 leaves the field BOUND — like the intensity, it is a shader scalar and "
       + "0 is exactly 'DDGI as it behaved before the fix'");
assert(Math.abs(world.get().gi.ddgiAmbient) < 1e-6, "the document echoes ambient 0");
assert(world.gi({ ddgiAmbient: 1 }), "world.gi({ddgiAmbient:1}) — back to the reconstruction");
editor.frame(3);
assert(Math.abs(world.get().gi.ddgiAmbient - 1.0) < 1e-4, "the document echoes ambient 1");
var threw = "";
try { world.gi({ ddgiAmbient: 9 }); } catch (e) { threw = String(e); }
assert(threw.indexOf("ddgiAmbient") >= 0,
       "world.gi refuses an out-of-range ddgiAmbient: " + threw);

// ---- phase D: refusals and the tri-state --------------------------------
try { world.gi({ ddgiIntensity: 100 }); } catch (e) { threw = String(e); }
assert(threw.indexOf("ddgiIntensity") >= 0,
       "world.gi refuses an out-of-range ddgiIntensity: " + threw);
threw = "";
try { world.gi({ ddgiIntensity: -1 }); } catch (e) { threw = String(e); }
assert(threw.indexOf("ddgiIntensity") >= 0,
       "world.gi refuses a negative ddgiIntensity: " + threw);
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
// the decision back to the Rayon tier, which then WRITES ITS ANSWER THROUGH
// (services/worldmodes.h — a backing field is always the resolved value). The
// scene is on Medium here, so the answer is off.
assert(world.get().gi.ddgi === false,
       "auto hands the decision to the tier, and the tier's answer is what the document holds");
assert(world.giStatus().ifdBound === false, "so the field is unbound again");
assert(world.gi({ ddgi: "on" }), "world.gi({ddgi:\"on\"}) — the string form");
editor.frame(4);
assert(world.giStatus().ifdBound === true, "\"on\" binds it");

// NO DDGI-ONLY MODE, and this is a deliberate refusal recorded in the verb's
// own documentation: with no voxel lighting bound the shader's ambient gate
// disappears and the sky/flat ambient would be counted twice on top of the
// field. Asking for DDGI outside a VCT mode is therefore honoured as
// "requested, not bound" rather than as a second GI technique.
assert(world.gi({ mode: "instant_radiosity", ddgi: true }),
       "world.gi({mode:'instant_radiosity', ddgi:true}) is accepted...");
editor.frame(4);
st = world.giStatus();
console.log("giStatus(IR + ddgi) = " + JSON.stringify(st));
assert(st.ifdBound === false && st.ifdProbes === 0,
       "...and builds NO field: there is no voxel volume to feed one, and a DDGI-only mode "
       + "would double-count the ambient (P0 spike §5)");
assert(world.get().gi.ddgi === true, "the request is still remembered on the document");

// ---- phase E: the round trip --------------------------------------------
assert(world.gi({ mode: "vct", ddgi: true, ddgiIntensity: 1.75 }), "set up for the round trip");
editor.frame(3);
assert(project.save() === true, "project.save");
assert(project.close() === true, "project.close");
assert(project.open(guid) === true, "project.open (REOPEN)");
editor.frame(4);
gi = world.get().gi;
console.log("gi after reopen = " + JSON.stringify(gi));
assert(gi.ddgi === true, "ddgi survives save/close/reopen");
assert(Math.abs(gi.ddgiIntensity - 1.75) < 1e-4, "ddgiIntensity survives save/close/reopen");
st = world.giStatus();
assert(st.ifdBound === true, "the reopened scene binds its field again");
console.log("gi.ddgi e2e complete");
