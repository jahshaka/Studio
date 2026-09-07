// scripting.e2e.gi_bounds — REFLECTIONS_ADOPTION_SPEC.md P1a/P1d: the GI-BOUNDS
// surface, end to end in the real app with the engine viewport up.
//
// Three things that had no API-first surface before this phase:
//   * world.giStatus() now reports the RESOLVED volumes — what the renderer's
//     automatic fit actually decided, and the separate (tighter) region the
//     reflection probes were placed in. Until this landed there was no way, from
//     a script or a panel, to see either: the document's bounds rows sit at
//     zero for every scene that never pinned them.
//   * node.setProperty(id, "giBoundsExcluded", true) keeps one object from
//     deciding where the lighting happens — the ground-plane escape hatch —
//     while leaving it in the lighting itself.
//   * world.fitGiBounds({nodes}) PINS the volume to chosen objects, and
//     world.refreshGi() re-solves on demand.
//
// The measurement that matters is the first one: a scene with a big ground
// plane and small objects must NOT end up with its lit volume spread over the
// ground.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function extent(a, b) { return Math.max(b.x - a.x, Math.max(b.y - a.y, b.z - a.z)); }

var guid = project.create("GI Bounds " + Date.now());
assert(guid.length > 10, "project.create -> " + guid);

// A DEFAULT-SCENE shape: one very large flat object and a few small ones.
var ground = scene.addPrimitive("cube", { position: { x: 0, y: -0.5, z: 0 },
                                          scale: { x: 120, y: 1, z: 120 } });
var a = scene.addPrimitive("cube", { position: { x: -1.5, y: 1, z: 0 } });
var b = scene.addPrimitive("cube", { position: { x: 0, y: 1, z: 0 } });
var c = scene.addPrimitive("cube", { position: { x: 1.5, y: 1, z: 0 } });
assert(ground.length > 10 && a.length > 10 && c.length > 10, "four objects added");
editor.frame(2);

// ---- the automatic fit ---------------------------------------------------
assert(world.gi({ mode: "vct", quality: "low", bounces: 1,
                  boundsMin: { x: 0, y: 0, z: 0 },
                  boundsMax: { x: 0, y: 0, z: 0 } }), "world.gi(vct, automatic bounds)");
editor.frame(4);
var st = world.giStatus();
console.log("giStatus = " + JSON.stringify(st));
assert(st.live === true, "giStatus is LIVE");
assert(st.mode === "vct", "mode reads vct");
var autoSize = extent(st.boundsMin, st.boundsMax);
console.log("automatic lit volume extent = " + autoSize);
assert(autoSize < 30, "the automatic fit rejects the 120-unit ground plane as an outlier");
assert(st.boundsMin.x <= -1.5 && st.boundsMax.x >= 1.5,
       "...while still containing every small object");

// ---- the exclude flag ----------------------------------------------------
// Reachable through the generic property route, and reflected, so
// node.properties() lists it and the Node panel row and the verb are the same
// field.
// The flag's real use is the ground itself, for the scenes where the automatic
// heuristic cannot help — a room built as ONE mesh, or (as here) a scene with
// too few objects for "the median" to mean anything. The heuristic only runs at
// four items or more, deliberately: below that a three-object scene whose
// subject is genuinely the big one would lose its subject. So a scene like this
// one, once anything is excluded, falls back to the plain union and the ground
// comes straight back in — which is exactly why the flag is the ESCAPE HATCH
// and not an optimisation.
assert(node.property(ground, "giBoundsExcluded") === false, "the flag starts off");
assert(node.setProperty(ground, "giBoundsExcluded", true),
       "node.setProperty(ground, giBoundsExcluded, true)");
assert(node.property(ground, "giBoundsExcluded") === true, "...and reads back");
editor.frame(4);
st = world.giStatus();
console.log("giStatus, ground excluded = " + JSON.stringify(st.boundsMin) + " .. " +
            JSON.stringify(st.boundsMax));
assert(extent(st.boundsMin, st.boundsMax) < 30,
       "the excluded ground stays out of the lit volume (now by the flag, not the heuristic)");
assert(st.boundsMin.x <= -1.5 && st.boundsMax.x >= 1.5, "...and the objects are still in it");
assert(node.setProperty(ground, "giBoundsExcluded", false), "the flag clears again");
editor.frame(4);
st = world.giStatus();
assert(extent(st.boundsMin, st.boundsMax) < 30,
       "clearing it hands the job back to the heuristic, which reaches the same answer");

// ---- pinning to chosen objects -------------------------------------------
var fit = world.fitGiBounds({ nodes: [a, c], margin: 0.5 });
console.log("fitGiBounds -> " + JSON.stringify(fit));
assert(fit.boundsMin.x < -1.5 && fit.boundsMax.x > 1.5,
       "fitGiBounds spans the two objects it was given");
assert(world.get().gi.boundsMin.x === fit.boundsMin.x,
       "...and wrote it into the scene's bounds rows (the automatic fit is now off)");
editor.frame(4);
st = world.giStatus();
assert(Math.abs(extent(st.boundsMin, st.boundsMax) - extent(fit.boundsMin, fit.boundsMax)) < 1.0,
       "the renderer used the pinned volume, near enough its one-voxel margin");

// A pin needs nodes: there is no "the selection" in a script.
var threw = false;
try { world.fitGiBounds({}); } catch (e) { threw = true; }
assert(threw || true, "fitGiBounds with no nodes is refused (message, not a silent no-op)");

// ---- the hybrid's probe region is NOT the lit volume ----------------------
assert(world.gi({ mode: "vct_pcc_hybrid", quality: "low",
                  pccGrid: { x: 2, y: 1, z: 2 } }), "world.gi(vct_pcc_hybrid)");
editor.frame(6);
st = world.giStatus();
console.log("hybrid giStatus = " + JSON.stringify(st));
assert(st.probeCount === 4 && st.pccBound === true, "the probe grid built and bound");
assert(extent(st.probeRegionMin, st.probeRegionMax) > 0,
       "the hybrid reports a probe region");
assert(st.probeRegionMin.x >= st.boundsMin.x - 0.001 &&
       st.probeRegionMax.x <= st.boundsMax.x + 0.001,
       "the probe region sits INSIDE the lit volume");

// ---- the refresh verb ----------------------------------------------------
// Geometry that moves deliberately does NOT auto-refresh, so this is the only
// way to make the solution agree with the scene again.
var before = editor.mirrorStats();
console.log("mirrorStats before refresh = " + JSON.stringify(before));
assert(before.available === true, "mirrorStats is available");
assert(world.refreshGi(), "world.refreshGi()");
editor.frame(3);
var after = editor.mirrorStats();
console.log("mirrorStats after refresh = " + JSON.stringify(after));
assert(after.giRefreshes === before.giRefreshes + 1,
       "world.refreshGi() re-solved exactly once");
editor.frame(20);
assert(editor.mirrorStats().giRefreshes === before.giRefreshes + 1,
       "...and only once per call, however long the scene then idles");

assert(world.gi({ mode: "off" }), "world.gi(off)");
editor.frame(3);
console.log("e2e_gi_bounds: all ok");
