// scripting.e2e.ogre_samples — app.ogreSamples() and app.launchOgreSample()
// (SPECS/OGRE_SAMPLES_TAB_SPEC.md §6, the API-first half of the samples tab).
//
// Runs HEADLESS, with no display at all (jah_no_display), and that is the
// point: the ONLY thing this suite is allowed to prove is that the verbs
// REFUSE, in words, without throwing and without spawning anything. Ogre's own
// sample binaries are an opt-in developer convenience (OGRE_SAMPLES=1
// ./irisgl/scripts/build-ogre.sh) that no CI tree and no macOS box has, so a
// test that needed them would be a test that is skipped everywhere — and one
// that launched one would be a second Vulkan application on somebody's display
// out of a test suite.
//
// What is asserted:
//   * the inventory is a CURATED CATALOG, not a directory scan: it is non-empty
//     even here, every row carries the fields the tab reads, and `available`
//     is a boolean per row rather than the list being short;
//   * a headless session refuses to launch, with a reason naming the cause —
//     it does not throw, because "the samples are not built" is an ordinary
//     state of the world and the button shows the sentence;
//   * a junk name, an empty name and a path-traversal name are refused the
//     same way (the name becomes a file name under a directory we hand to
//     QProcess);
//   * nothing was started: every row still reports running:false.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

// ---- the inventory ------------------------------------------------------
var list = app.ogreSamples();
assert(list.length >= 8, "app.ogreSamples() -> " + list.length + " samples");

var names = [];
for (var i = 0; i < list.length; ++i) {
    var s = list[i];
    assert(typeof s.name === "string" && s.name.length > 0, "row " + i + " has a name: " + s.name);
    assert(typeof s.title === "string" && s.title.length > 0, "row " + i + " has a title: " + s.title);
    assert(typeof s.note === "string", "row " + i + " has a note field");
    assert(typeof s.available === "boolean", "row " + i + " available is a boolean");
    assert(typeof s.portAvailable === "boolean", "row " + i + " portAvailable is a boolean");
    assert(s.portArchive.indexOf("ogre") >= 0 && s.portArchive.indexOf(s.name + ".zip") >= 0,
           "row " + i + " portArchive is scenes/ogre/<name>.zip: " + s.portArchive);
    assert(s.running === false, "row " + i + " is not running");
    // available:false must come with no path; available:true (a comparison
    // tree that opted in) must come with one.
    if (s.available) assert(s.path.length > 0, s.name + " available -> has a path");
    else assert(s.path === "", s.name + " unavailable -> no path");
    names.push(s.name);
}

// The three ports of the proving slice are in the catalog by name, which is
// what the tab's tiles and the authoring scripts key off.
assert(names.indexOf("PbsMaterials") >= 0, "catalog contains PbsMaterials");
assert(names.indexOf("LocalCubemaps") >= 0, "catalog contains LocalCubemaps");
assert(names.indexOf("Refractions") >= 0, "catalog contains Refractions");

// ---- the refusal --------------------------------------------------------
// THE TEST. A headless run has no display to put a sample window on, so the
// verb declines and says why. It returns a map; it does not throw.
var r = app.launchOgreSample("PbsMaterials");
assert(r.launched === false, "headless: launchOgreSample refused");
assert(typeof r.reason === "string" && r.reason.length > 0,
       "headless: refusal carries a reason -> " + r.reason);
assert(r.pid === 0, "headless: nothing was spawned (pid 0)");

// Junk names are refused identically — never thrown, never spawned.
var bad = ["", "no such sample", "../../../bin/sh", "PbsMaterials;rm -rf /"];
for (var b = 0; b < bad.length; ++b) {
    var rj = app.launchOgreSample(bad[b]);
    assert(rj.launched === false, "refused: '" + bad[b] + "' (" + rj.reason + ")");
    assert(rj.pid === 0, "refused: '" + bad[b] + "' spawned nothing");
}

// Options are accepted (and ignored) rather than being a type error.
var ro = app.launchOgreSample("Refractions", { width: 1280, height: 720, fullscreen: false });
assert(ro.launched === false, "headless: options form also refused (" + ro.reason + ")");

// ---- nothing ran --------------------------------------------------------
var after = app.ogreSamples();
for (var k = 0; k < after.length; ++k)
    assert(after[k].running === false, after[k].name + " still not running");

console.log("ogre samples verbs: OK");
