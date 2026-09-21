// app.reset_library, THE SEED A RESET RUNS (SEED-STAMP-1).
//
// `app.resetLibrary()` ends by starting the very seed a person's launch runs
// (services/libraryreset.cpp) — the ASYNC `MaterialPresetSeeder`, never
// `materials.seedPresets()`. reset.js above asserts the member stamps on the
// synchronous route, which is the one no user ever takes; this run asserts
// them on the route the button really takes, in a session that accepts the
// launch seed too (the shell sets JAHSHAKA_SEED_PRESETS=1 for this run alone,
// so nothing it seeds can move the censuses the other arms compare — it has
// its own data root for the same reason).
//
// Before the fix the maps of all twenty presets came back as the USER'S OWN
// TILES: the seeder mints them through the import pipeline ahead of the row
// pass, so `MaterialPresetAssets::definitionFor` found existing rows, answered
// `minted = false`, and the stamp it writes never fired at all.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

// WAIT BY POLLING A VERB. The script has its own thread and the UI thread is
// free between verbs — which is what lets the seeder run at all. Nothing here
// calls `materials.seedPresets()`: that verb stands the seeder down and does
// the work itself, which is exactly the route this arm is not about.
function waitForSeed(what) {
    var deadline = Date.now() + 120000;
    var mats = [];
    while (Date.now() < deadline) {
        mats = assets.list({ scope: "store", type: "material" });
        if (mats.length >= 20) break;
    }
    assert(mats.length === 20, what + " (" + mats.length + " bundles)");
    return mats;
}

// EVERY MAP THE BUNDLES NAME, and whether the seed marked it a member.
function auditMaps(mats, what) {
    var seen = {}, count = 0, unstamped = [];
    for (var i = 0; i < mats.length; ++i) {
        var list = materials.members(mats[i].guid);
        for (var j = 0; j < list.length; ++j) {
            if (seen.hasOwnProperty(list[j].guid)) continue;
            seen[list[j].guid] = true;
            count++;
            var meta = assets.metadata(list[j].guid);
            if (meta.member !== true || !meta.memberOf) unstamped.push(list[j].name);
        }
    }
    assert(count === 31, what + ": the bundles name 31 map textures (" + count + ")");
    assert(unstamped.length === 0,
           what + ": EVERY ONE IS STAMPED A MEMBER (" + unstamped.join(", ") + ")");
    var tiles = assets.list({ scope: "store", type: "texture" });
    assert(tiles.length === 0, what + ": …so the library shows no texture tile ("
                               + tiles.length + ")");
    assert(assets.list({ scope: "store", type: "texture", members: true }).length === count,
           what + ": …and 'Show member textures' finds all " + count
           + " — one row per picture, none the seed minted twice");
}

// ---- the LAUNCH seed this session took -----------------------------------
var mats = waitForSeed("the launch seed put twenty preset bundles in the library");
auditMaps(mats, "after the launch seed");

// ---- and the one the RESET starts ----------------------------------------
// The launch seed has just finished, and the reset refuses while ANY importer
// is live (a wipe under a commit is how a store loses its rows), so the call
// is retried for a moment rather than asserted on the first try.
var result = {};
var deadline = Date.now() + 30000;
while (Date.now() < deadline) {
    result = app.resetLibrary();
    if (result.ok === true) break;
}
assert(result.ok === true, "app.resetLibrary() -> ok (" + app.lastError() + ")");

mats = waitForSeed("the RESET's own seed put them back");
auditMaps(mats, "after the reset's seed");

console.log("ALL PASS");
