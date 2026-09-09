// scripting.e2e.memory — the MEMORY verbs (riders lane R4): app.memoryStats,
// app.reclaimMemory and the profiler switch app.profiling. Headless: the NULL
// render system has a VaoManager and scene managers like Vulkan's, and the
// document's nodes live in the engine's staging manager, so the SIMD half is
// real here; the GPU rows are the NULL manager's (zero) and only their SHAPE
// is asserted. Magnitudes are never pinned: they are a property of the box.
//
// WHAT IS ASSERTED: shape; that a burst of nodes is counted; that
// reclaimMemory answers with a before/after pair whose counts match the
// world (the shrink is a capacity change the backend does not expose in
// bytes — residentBytes is where it shows on a real project, and a 200-node
// burst is below page granularity, so it is REPORTED, not asserted); that
// the profiler switch reads back and is off by default.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

var KEYS = ["gpuPoolCapacityBytes", "gpuPoolFreeBytes", "gpuPools", "gpuPoolsIncludeTextures",
            "sceneManagers", "simdNodes", "simdObjects", "simdNodeDepths", "residentBytes"];

var guid = project.create("Memory Verbs " + Date.now());
assert(guid.length > 10, "project.create");

// ---- shape ----------------------------------------------------------------
var m0 = app.memoryStats();
for (var i = 0; i < KEYS.length; ++i)
    assert(m0[KEYS[i]] !== undefined, "app.memoryStats has '" + KEYS[i] + "'");
assert(typeof m0.gpuPoolsIncludeTextures === "boolean", "gpuPoolsIncludeTextures is a bool");
assert(m0.gpuPoolFreeBytes <= m0.gpuPoolCapacityBytes, "free <= capacity");
assert(m0.sceneManagers >= 1, "at least the staging manager is walked (" + m0.sceneManagers + ")");
console.log("    baseline: " + JSON.stringify(m0));

// ---- textureMemory: the attribution behind the GPU rows --------------------
// Headless the NULL render system still owns a TextureGpuManager, so the
// walk is real; how many textures it knows is a property of the boot path,
// never pinned. Shape, ordering and the totals' arithmetic are what is
// asserted; `top` and `resident` narrow the list, never the totals.
var tm = app.textureMemory();
var TKEYS = ["count", "totalBytes", "residentBytes", "pooledBytes", "renderTargetBytes", "entries"];
for (var t = 0; t < TKEYS.length; ++t)
    assert(tm[TKEYS[t]] !== undefined, "app.textureMemory has '" + TKEYS[t] + "'");
assert(tm.entries.length <= 50 && tm.entries.length <= tm.count, "default top is 50, never above count");
var sum = 0, sorted = true;
var all = app.textureMemory({ top: 0 });
assert(all.entries.length === all.count, "top 0 lists every entry (" + all.count + ")");
for (var r = 0; r < all.entries.length; ++r) {
    var row = all.entries[r];
    assert(typeof row.name === "string" && typeof row.bytes === "number" && typeof row.residency === "string",
           r === 0 ? "entry rows carry name/bytes/residency" : "row " + r);
    sum += row.bytes;
    if (r > 0 && all.entries[r - 1].bytes < row.bytes) sorted = false;
}
assert(sorted, "entries are largest first");
assert(sum === all.totalBytes, "totalBytes is the sum over every entry (" + sum + ")");
assert(all.residentBytes <= all.totalBytes && all.pooledBytes <= all.totalBytes,
       "resident/pooled never exceed the total");
var res = app.textureMemory({ top: 0, resident: true });
for (var q = 0; q < res.entries.length; ++q)
    if (res.entries[q].residency !== "Resident") throw new Error("resident filter leaked " + res.entries[q].residency);
assert(res.totalBytes === all.totalBytes, "resident filter narrows the list, not the totals");
var threw = false;
try { app.textureMemory({ top: -1 }); } catch (e) { threw = String(e).indexOf("top must be") >= 0; }
assert(threw, "top -1 is a caller error: it throws with the reason");
console.log("    textures: " + all.count + ", total " + all.totalBytes + " B, resident " + all.residentBytes + " B");

// ---- a burst of nodes is counted ------------------------------------------
// Empties, on purpose: a document node is an engine scene NODE, which is what
// the node pools hold, and headless there is no Item behind a primitive (the
// object pools stay at 0 under the NULL render system). Also on purpose:
// scene.addPrimitive costs ~130 ms headless and grows with the scene (200 of
// them took 231 s on the box that wrote this — a pre-existing Studio defect,
// reported, not this suite's); addEmpty is ~6 ms.
var ids = [];
for (var n = 0; n < 200; ++n) ids.push(scene.addEmpty());
assert(ids.length === 200 && ids[199].length > 10, "200 empties added");
var m1 = app.memoryStats();
assert(m1.simdNodes >= m0.simdNodes + 200,
       "the burst is in the node pools: " + m0.simdNodes + " -> " + m1.simdNodes);

// ---- remove them: the undo stack keeps them alive (by design) ---------------
// node.remove is undoable, so the removed nodes stay in the staging manager
// until the history lets go of them — the count must not GROW, and it need
// not fall. The moment that actually frees them is the project close, which
// is also where the editor calls reclaim (MainWindow::closeProject).
var removed = 0;
for (var k = 0; k < ids.length; ++k) if (node.remove(ids[k])) ++removed;
assert(removed === 200, "the 200 empties are removed again");
var mR = app.memoryStats();
assert(mR.simdNodes <= m1.simdNodes, "removal never grows the pools (" + m1.simdNodes + " -> " + mR.simdNodes + ")");
console.log("    after node.remove x200 (undo stack holds them): " + mR.simdNodes + " nodes");

// ---- close, reclaim, and the pair matches the world ------------------------
assert(project.close(), "project.close");
var r = app.reclaimMemory();
assert(r.before !== undefined && r.after !== undefined, "reclaimMemory answers before/after");
// NOT asserted — REPORTED (riders lane, 2026-09-09): in a --script run the
// close does NOT free the burst. Measured over three create / +200 / close
// cycles: 294 -> 411 -> 615 -> 819 nodes, RSS 334 -> 367 -> 392 MB — every
// node a verb created survives project.close() for the rest of the run. The
// likely holder is the script run's own open undo macro (one macro per run,
// closed after the script ends, its AddNodeCommands keeping the nodes); the
// live-app close path (MainWindow::closeProject clears the history first) is
// what needs measuring next. This line is the ruler that found it.
console.log("    close freed " + (m1.simdNodes - r.before.simdNodes) + " of the 200 (see the note above)");
assert(r.after.simdNodes === r.before.simdNodes,
       "a shrink relocates, it does not lose nodes (" + r.after.simdNodes + ")");
assert(r.after.simdObjects === r.before.simdObjects, "...nor objects");
assert(r.after.sceneManagers === r.before.sceneManagers, "every manager was walked both times");
console.log("    reclaim: RSS " + r.before.residentBytes + " -> " + r.after.residentBytes +
            " bytes; nodes " + r.before.simdNodes + " -> " + r.after.simdNodes +
            "; depths " + r.before.simdNodeDepths + " -> " + r.after.simdNodeDepths);
var m2 = app.memoryStats();
assert(m2.simdNodes === r.after.simdNodes, "memoryStats after the reclaim agrees with it");

// ---- the profiler switch ----------------------------------------------------
assert(app.profiling() === false, "the pass profiler is OFF by default");
assert(app.profiling(true) === true, "app.profiling(true) turns it on");
assert(app.profiling() === true, "and it reads back on");
assert(app.profiling(false) === false, "app.profiling(false) turns it off");
assert(app.profiling() === false, "and it reads back off");

console.log("memory verbs: all checks passed");
