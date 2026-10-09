// <pool>.tier — THE PROCESS'S TEST TIER (lane TEST-TIER-1; services/testtier.h,
// SPECS/audits/GPU_LOSS_AUDIT_2026-09-27.md §A2).
//
// A pool declared `TIER low` runs its process with `--test-tier low`: EVERY scene
// the process binds — a new one, and an opened one AFTER its reader — is put on
// the Low World Mode through the call world.mode makes, and the window boots
// 1280x720. A pool declared `TIER epic` has no test tier: a new scene is the
// product's (Epic) and an opened scene keeps the tier it SAVED. The same script
// runs as an arm in one pool of each tier, and asserts whichever it is in:
//
//   1. app.testTier() names the process's tier ("" = none);
//   2. a new scene is put on that tier (else "epic", the document's);
//   3. a scene saved at Medium and reopened is put on the test tier (else "medium");
//   UNDER A TEST TIER Photon is OFF unless JAHSHAKA_TEST_NEEDS names it (WORLD-MODE-1:
//   each World Mode runs Photon at its own name, and a test passes what it needs), so the
//   mode READS "custom" there and the tier is read from the photon row's tierValue (the
//   picked mode's column: low 1, medium 2, high 3, epic 4).
//   4. the window is 1280x720 under a test tier (the pool's baseline puts back
//      whatever size the boot had, so an arm always starts at it).

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function J(x) { return JSON.stringify(x); }

var tier = app.testTier();
console.log("testTier: " + J(tier));
assert(tier === "" || tier === "low" || tier === "medium" || tier === "high" || tier === "epic",
       "app.testTier() is a World Mode name or '' (got " + J(tier) + ")");

// ---- 1-2. a new scene -----------------------------------------------------------
var guid = project.create("Pool Tier " + Date.now());
assert(guid.length > 10, "project.create");
var COLUMN = { low: 1, medium: 2, high: 3, epic: 4 };
function onTestTier(label) {
    var m = world.mode(), row = world.settings().photon, ph = world.photon();
    assert(m === "custom" && row.tierValue === COLUMN[tier] && ph.enabled === false,
           label + " is put on the process's test tier '" + tier + "' with Photon off (needs: none) — " +
           "mode " + J(m) + ", photon column " + row.tierValue + ", photon " + (ph.enabled ? "on" : "off"));
}
var fresh = world.mode();
if (tier === "")
    assert(fresh === "epic", "a NEW scene starts at the document's Epic (no test tier) (got " + J(fresh) + ")");
else
    onTestTier("a NEW scene");

// ---- 3. an opened scene -----------------------------------------------------------
assert(world.mode({ mode: "medium" }) === "medium", "world.mode({mode:'medium'}) on the open scene");
assert(project.save() === true, "project.save");
assert(project.close() === true, "project.close");
assert(project.open(guid) === true, "project.open of the scene saved at Medium");
var reopened = world.mode();
if (tier === "")
    assert(reopened === "medium", "an OPENED scene keeps the tier it saved (Medium) (got " + J(reopened) + ")");
else
    onTestTier("an OPENED scene, after its reader,");

// ---- 4. the window ------------------------------------------------------------------
var w = app.window();
console.log("window: " + w.width + "x" + w.height);
if (tier !== "")
    assert(w.width === 1280 && w.height === 720,
           "a test-tier process boots its window at 1280x720 (got " + w.width + "x" + w.height + ")");
else
    assert(w.width > 1280 || w.height > 720,
           "a process with no test tier keeps the screen-sized boot window (got " + w.width + "x" + w.height + ")");

var m = app.memoryStats();
console.log("gpuPoolUsed MB: " + Math.round((m.gpuPoolCapacityBytes - m.gpuPoolFreeBytes) / 1048576));
project.close();
0;
