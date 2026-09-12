// scripting.e2e.page_gi — ENGINE_CACHE_POLICY_SPEC §2 P10: a page return never
// rebuilds GI from scratch.
//
// Every return to the editor used to re-push the scene's GI configuration:
// EngineSceneViewport::begin() dropped the mirror's GI latch so the process-wide
// HlmsPbs binding would be re-asserted, and the only way the mirror knew to
// re-assert it was setGlobalIllumination — which tears the voxel arm, the probe
// grid and the irradiance field down and builds them again. The owner's
// session log shows what that cost: 2.1-2.9 s of blocked UI right after
// "materials -> editor". The binding is a POINTER; the arms were still valid.
//
// Asserted through the counters that would move if anything were rebuilt:
// editor.mirrorStats().giPushes (the mirror's full pushes) and
// world.giStatus().rebuilds (the engine's from-scratch builds), and the whole
// giStatus reading before and after.
//
// Phase 1: editor -> assets -> editor and editor -> materials -> editor (pages
// with no document scene of their own; the second is the owner's logged case).
// NOTHING may move: no push, no rebuild, the same giStatus.
// Phase 2: editor -> player -> editor. The mirror must not re-push, and the
// editor scene must own the GI binding again once back. What it CANNOT assert
// is "no rebuild": the player page drives its own engine scene over the SAME
// document graph, the graph migrates between the two engine scenes on every
// space switch (scripting.e2e.space_switch), and the editor scene's items are
// destroyed and re-adopted with it — a destroyed item is a from-scratch GI
// rebuild by the engine's own rule (raw Item* in the voxelizer). That cost is
// the graph migration's (recorded by the lead for a later lane) and is pinned
// at its current value, two, below. Phase 1 pins ZERO.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

function vecEq(a, b) {
    return Math.abs(a.x - b.x) < 1e-4 && Math.abs(a.y - b.y) < 1e-4 && Math.abs(a.z - b.z) < 1e-4;
}

// The fields a rebuild could move. A reading taken on the same scene with
// nothing rebuilt must agree on every one of them.
function sameGi(a, b, what) {
    var keys = ["mode", "probeCount", "pccBound", "probeGridRefused", "probeEnclosedAxes",
                "vctBound", "ifdBound", "ifdProbes",
                "probeUpdatesPerFrame", "cubemapProbeSlotsPerCell",
                "probesClampedToRegion", "probeHdr", "probeShadows", "rebuilds"];
    for (var i = 0; i < keys.length; ++i) {
        var k = keys[i];
        assert(a[k] === b[k], what + ": giStatus." + k + " unchanged (" + a[k] + " -> " + b[k] + ")");
    }
    var boxes = ["boundsMin", "boundsMax", "probeRegionMin", "probeRegionMax",
                 "probeShapeMin", "probeShapeMax"];
    for (var j = 0; j < boxes.length; ++j) {
        var b2 = boxes[j];
        assert(vecEq(a[b2], b[b2]), what + ": giStatus." + b2 + " unchanged");
    }
}

// Frames until the probe grid has caught up (the post-build catch-up and the
// settle re-solve it may arm), bounded.
function settle(what) {
    for (var i = 0; i < 40; ++i) {
        editor.frame(10);
        var st = world.giStatus();
        if (st.staleProbes === 0 && st.probeCapturesLastFrame === 0) {
            editor.frame(20);
            st = world.giStatus();
            if (st.staleProbes === 0 && st.probeCapturesLastFrame === 0) return st;
        }
    }
    throw new Error(what + ": the probe grid never caught up: " + JSON.stringify(world.giStatus()));
}

var guid = project.create("Page GI " + Date.now());
assert(guid.length > 10, "project.create -> " + guid);
var box = scene.addPrimitive("cube", { position: { x: 0, y: 0.5, z: 0 } });
assert(box.length > 10, "a cube in the default scene");

var st0 = settle("fresh project");
var m0 = editor.mirrorStats();
console.log("before: " + JSON.stringify(st0) + " mirror " + JSON.stringify(m0));
assert(st0.live === true, "giStatus is live");
assert(st0.mode === "vct_pcc_hybrid", "the default project runs the VCT + probes hybrid");
// THE DEFAULT PROJECT IS AN OPEN SCENE, AND SINCE 2026-09-13 THAT MEANS NO
// PROBE GRID (owner decision Q3: "a user starts in the editor in a new project
// with an open scene ... I would think the sky is your first reflection
// asset."). The renderer MEASURES the enclosure — a ground plane and a cube are
// enclosed on no axis — and declines the grid, leaving the sky cubemap bound as
// the reflection source and cone tracing carrying the bounce. So the binding
// this page-switch suite follows is the VOXEL one, and `probeGridRefused` is
// asserted here so that "pccBound false" can never quietly become the old
// silent hybrid-degradation failure instead.
assert(st0.vctBound, "the editor scene owns the GI binding");
assert(st0.pccBound === false && st0.probeCount === 0 && st0.probeGridRefused === true,
       "...and the default open scene has no probe grid, by decision: " +
       JSON.stringify({ enclosedAxes: st0.probeEnclosedAxes, refused: st0.probeGridRefused }));
assert(m0.available === true, "mirrorStats available");

// ---- phase 1: editor -> assets / materials -> editor ------------------------
["assets", "materials"].forEach(function (page) {
    assert(app.space(page), "to the " + page + " page");
    assert(app.space("editor"), "back to the editor");
    editor.frame(10);
    var st1 = world.giStatus();
    var m1 = editor.mirrorStats();
    console.log("after " + page + ": " + JSON.stringify(st1) + " mirror " + JSON.stringify(m1));
    assert(m1.giPushes === m0.giPushes,
           page + " round trip: NO GI re-push (" + m0.giPushes + " -> " + m1.giPushes + ")");
    assert(m1.giRefreshes === m0.giRefreshes, page + " round trip: no GI re-solve either");
    sameGi(st0, st1, page + " round trip");
    assert(st1.staleProbes === 0 && st1.probeCapturesLastFrame === 0,
           page + " round trip: nothing stale, nothing re-captured");
});

// ---- phase 2: editor -> player -> editor ----------------------------------
assert(app.space("player"), "to the player page");
editor.frame(20);
var inPlayer = world.giStatus();
console.log("editor scene while the player runs: pccBound=" + inPlayer.pccBound +
            " vctBound=" + inPlayer.vctBound);
assert(app.space("editor"), "back to the editor");
var st2 = settle("player round trip");
var m2 = editor.mirrorStats();
console.log("after player: " + JSON.stringify(st2) + " mirror " + JSON.stringify(m2));
assert(st2.vctBound && st2.pccBound === st0.pccBound,
       "player round trip: the editor scene owns the GI binding again");
assert(m2.giPushes === m0.giPushes,
       "player round trip: NO GI re-push from the mirror (" + m0.giPushes + " -> " + m2.giPushes + ")");
// THE RECORDED COST OF THE GRAPH MIGRATION (lead ledger, a later lane): the
// editor scene's arm is rebuilt EXACTLY twice — once when the graph leaves for
// the player's engine scene (its items are destroyed; nothing is left to
// voxelize) and once when it comes back. Pinned at 2 so that a fix shows up as
// a diff here and a regression (a third rebuild, e.g. a re-push) fails.
assert(st2.rebuilds - st0.rebuilds === 2,
       "player round trip: exactly the graph migration's two rebuilds (" + st0.rebuilds +
       " -> " + st2.rebuilds + ")");
var st2same = JSON.parse(JSON.stringify(st2));
st2same.rebuilds = st0.rebuilds;       // compared above; the rest must match field for field
sameGi(st0, st2same, "player round trip (the same arms, rebuilt)");

console.log("page_gi: all assertions passed");
