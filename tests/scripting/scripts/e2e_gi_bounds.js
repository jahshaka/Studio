// scripting.e2e.gi_bounds — THERE IS NO FIXED GI VOLUME (owner decision D8,
// 2026-09-13; the single scene-fitted voxel volume deleted by D4-PHOTON-TIERS).
//
// The voxels are always the camera's cascade chain. What is left of "the fit" is
// the REFLECTION-PROBE GRID's placement region, used where a machine does not
// trace its reflections — a placement heuristic pending A9, never a lighting
// volume. This suite is it:
//
//   * the AUTOMATIC PLACEMENT FIT — a scene with a big ground plane and small
//     objects must NOT spread its probe grid over the ground;
//   * world.giStatus()'s probeFitMin/Max (the fit) and probeRegionMin/Max (the
//     scout's space inside it);
//   * node.setProperty(id, "probeGridExcluded", true), the per-object escape
//     hatch (OgreGi.cpp giItemBoundsRaw);
//   * world.refreshGi(), the on-demand re-solve;
//   * and the four retired keys, REFUSED BY NAME rather than ignored.

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

// ---- the automatic placement fit ----------------------------------------------
// The hybrid at LOW, whose reflections are not traced on any machine: a probe
// grid is placed, so its fit is live.
assert(world.gi({ mode: "vct_pcc_hybrid", quality: "low", bounces: 1 }),
       "world.gi(the hybrid at low: a probe grid is placed)");
editor.frame(4);
var st = world.giStatus();
console.log("giStatus = " + JSON.stringify(st));
assert(st.live === true, "giStatus is LIVE");
assert(st.mode === "vct_pcc_hybrid", "mode reads vct_pcc_hybrid");
var autoSize = extent(st.probeFitMin, st.probeFitMax);
console.log("automatic probe placement fit extent = " + autoSize);
assert(autoSize < 30, "the automatic fit rejects the 120-unit ground plane as an outlier");
assert(st.probeFitMin.x <= -1.5 && st.probeFitMax.x >= 1.5,
       "...while still containing every small object");

// ---- the four retired keys -----------------------------------------------
// Refused BY NAME, with the reading that replaced them, rather than swallowed
// by the unknown-key list: a script that still passes one is asking for a
// volume of its own and has to find out that there is no longer any such thing.
["boundsMin", "boundsMax", "autoBoundsMax", "cascades"].forEach(function (key) {
    var threw = "";
    var params = {};
    params[key] = key === "autoBoundsMax" ? 128 : (key === "cascades" ? false : { x: 0, y: 0, z: 0 });
    try { world.gi(params); } catch (e) { threw = String(e); }
    assert(threw.indexOf(key) >= 0 && threw.indexOf("giStatus") >= 0,
           "world.gi refuses '" + key + "' by name and says what to read instead");
});
assert(typeof world.fitGiBounds === "undefined",
       "world.fitGiBounds is gone from the registry altogether");
var g = world.get().gi;
assert(g.boundsMin === undefined && g.boundsMax === undefined && g.autoBoundsMax === undefined &&
       g.cascades === undefined,
       "and world.get().gi carries no bounds fields and no cascade switch at all");

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
assert(node.property(ground, "probeGridExcluded") === false, "the flag starts off");
assert(node.setProperty(ground, "probeGridExcluded", true),
       "node.setProperty(ground, probeGridExcluded, true)");
assert(node.property(ground, "probeGridExcluded") === true, "...and reads back");
editor.frame(4);
world.refreshGi();
editor.frame(4);
st = world.giStatus();
console.log("giStatus, ground excluded = " + JSON.stringify(st.probeFitMin) + " .. " +
            JSON.stringify(st.probeFitMax));
assert(extent(st.probeFitMin, st.probeFitMax) < 30,
       "the excluded ground stays out of the placement fit (now by the flag, not the heuristic)");
assert(st.probeFitMin.x <= -1.5 && st.probeFitMax.x >= 1.5, "...and the objects are still in it");
assert(node.setProperty(ground, "probeGridExcluded", false), "the flag clears again");
editor.frame(4);
world.refreshGi();
editor.frame(4);
st = world.giStatus();
assert(extent(st.probeFitMin, st.probeFitMax) < 30,
       "clearing it hands the job back to the heuristic, which reaches the same answer");

// ---- the scout's probe region sits inside the fit ---------------------------
// A ROOM, because a probe grid needs an enclosure and there is no pin to state
// one with any more: the renderer measures it out of the layout (owner probe
// rule, 2026-09-13), and this scene of loose cubes on a slab reads as open.
node.setProperty(ground, "visible", false);
(function buildRoom(half, height) {
    function slab(name, px, py, pz, sx, sy, sz) {
        var id = scene.addPrimitive("cube", { position: { x: px, y: py, z: pz } });
        node.setProperty(id, "name", name);
        node.transform(id, { scale: { x: sx / 2, y: sy / 2, z: sz / 2 } });
    }
    var w = half * 2;
    slab("Floor", 0, -0.25, 0, w, 0.5, w);
    slab("Roof",  0, height + 0.25, 0, w, 0.5, w);
    slab("WallW", -half, height / 2, 0, 0.5, height, w);
    slab("WallE",  half, height / 2, 0, 0.5, height, w);
    slab("WallS", 0, height / 2, -half, w, height, 0.5);
    slab("WallN", 0, height / 2,  half, w, height, 0.5);
})(6, 5);
assert(world.gi({ mode: "vct_pcc_hybrid", quality: "low",
                  pccGrid: { x: 2, y: 1, z: 2 } }), "world.gi(vct_pcc_hybrid)");
editor.frame(10);
st = world.giStatus();
console.log("hybrid giStatus = " + JSON.stringify(st));
assert(st.probeCount === 4 && st.pccBound === true, "the probe grid built and bound");
assert(extent(st.probeRegionMin, st.probeRegionMax) > 0,
       "the hybrid reports a probe region");
assert(st.probeRegionMin.x >= st.probeFitMin.x - 0.001 &&
       st.probeRegionMax.x <= st.probeFitMax.x + 0.001,
       "the probe region sits INSIDE the placement fit");

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
