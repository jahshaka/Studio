// <pool>.tier — THE PROCESS'S TEST TIER AND ITS NEEDS (lanes TEST-TIER-1, TEST-NEEDS-1;
// services/testtier.h, SPECS/audits/GPU_LOSS_AUDIT_2026-09-27.md §A2).
//
// A pool declared `TIER low NEEDS NONE` runs its process under JAHSHAKA_TEST_TIER=low /
// JAHSHAKA_TEST_NEEDS=none: EVERY scene the process binds — a new one, and an opened one AFTER
// its reader — is put on the Low World Mode through the call world.mode makes, every switchable
// feature the list does not name (Photon among them) is off, and (TEST_WINDOW) the window boots 1280x720. A
// pool declared `TIER document` has no test tier: a new scene is the product's (Epic) and an
// opened scene keeps the tier it SAVED. The same script runs as an arm in one pool of each, and
// asserts whichever it is in:
//
//   1. app.testTier() names the process's tier ("" = none) and its needs list;
//   2. a new scene reports world.mode() == that tier (else "epic", the document's), and Photon is
//      on iff the list names it;
//   3. a scene saved at Medium and reopened reports the test tier (else "medium");
//   4. the window is 1280x720 when the pool declares TEST_WINDOW (the pool's baseline puts back
//      whatever size the boot had, so an arm always starts at it).
//
// world.mode() may read "custom" in a test-tier process whose list leaves Photon off on a mode
// whose column runs it (WORLD-MODE-1: the mode reads Custom once Photon leaves its column).

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function J(x) { return JSON.stringify(x); }

var tt = app.testTier();
console.log("testTier: " + J(tt));
var tier = tt.tier, needs = tt.needs;
// a host list reaches the script as an array-like: read it by length, never Array.isArray
assert(tier === "" || tier === "low" || tier === "medium" || tier === "high" || tier === "epic",
       "app.testTier().tier is a World Mode name or '' (got " + J(tier) + ")");
assert(needs && typeof needs.length === "number" && (tier !== "" || needs.length === 0),
       "app.testTier().needs is a list, empty with no test tier (got " + J(needs) + ")");
function atTier(m) { return m === tier || (m === "custom" && needs.indexOf("photon") < 0); }

// ---- 1-2. a new scene -----------------------------------------------------------
var guid = project.create("Pool Tier " + Date.now());
assert(guid.length > 10, "project.create");
var fresh = world.mode();
assert(tier === "" ? fresh === "epic" : atTier(fresh),
       "a NEW scene starts at " + (tier === "" ? "the document's Epic (no test tier)"
                                               : "the process's test tier '" + tier + "'")
       + " (got " + J(fresh) + ")");
if (tier !== "")
    assert(world.photon().enabled === (needs.indexOf("photon") >= 0),
           "Photon is " + (needs.indexOf("photon") >= 0 ? "on: the list names it" : "off: the list does not name it")
           + " (enabled " + world.photon().enabled + ")");

// ---- 3. an opened scene -----------------------------------------------------------
assert(world.mode({ mode: "medium" }) === "medium", "world.mode({mode:'medium'}) on the open scene");
assert(project.save() === true, "project.save");
assert(project.close() === true, "project.close");
assert(project.open(guid) === true, "project.open of the scene saved at Medium");
var reopened = world.mode();
assert(tier === "" ? reopened === "medium" : atTier(reopened),
       "an OPENED scene " + (tier === "" ? "keeps the tier it saved (Medium)"
                                         : "is put on the test tier '" + tier + "' after its reader")
       + " (got " + J(reopened) + ")");

// ---- 4. the window ------------------------------------------------------------------
var w = app.window();
console.log("window: " + w.width + "x" + w.height);
if (tt.window === "1280x720")
    assert(w.width === 1280 && w.height === 720,
           "a process whose row declares the test window boots at 1280x720 (got " + w.width + "x" + w.height + ")");
else
    assert(w.width > 1280 || w.height > 720,
           "a process with no test window keeps the screen-sized boot window (got " + w.width + "x" + w.height + ")");

var m = app.memoryStats();
console.log("gpuPoolUsed MB: " + Math.round((m.gpuPoolCapacityBytes - m.gpuPoolFreeBytes) / 1048576));
project.close();
0;
