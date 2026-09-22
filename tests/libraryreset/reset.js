// app.resetLibrary() ON A POPULATED LIBRARY — the run that does the work.
//
// It opens on the library populate.js left (a project, an imported texture,
// stored objects and sidecars, a project folder, and the staging temp the
// shell planted), resets it, and then answers the three questions the shell
// cannot ask from outside the process:
//
//   * what came back in `removed` — the counts of what WAS there;
//   * the CENSUS afterwards, printed in the same lines census.js prints on a
//     fresh data root, so "exactly a first launch" is a comparison;
//   * that the CATALOG IS USABLE — `wipeDatabase` DROPs the tables, and the
//     old button left creating them again to a restart that never happened,
//     so a session that carried on ran on a database with no tables at all.
//     Creating a project here is that assertion.
//
// And then a SECOND reset, which must be a clean no-op with zeroes.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

// What we are about to lose (proof the run really opened the populated root).
var FIXTURE_PNG = RESET_FIXTURE_PNG;   // the prelude the shell prepends
var before = assets.list({ scope: "store" }).length;
var beforeProjects = project.list().length;
assert(before > 0, "the library opened with " + before + " asset rows");
assert(beforeProjects > 0, "…and " + beforeProjects + " project(s)");

// A PROJECT IS OPEN WHEN THE RESET IS ASKED FOR — the shape the button is
// pressed in. The reset closes it, discarding: every row it is made of is
// about to go. (Created rather than re-opened so the run needs no engine:
// project.create leaves its project open, and this suite is document verbs
// only.)
var live = project.create("Open When Reset");
assert(live.length > 10, "a project is open when the reset is asked for");

var result = app.resetLibrary();
assert(result.ok === true, "app.resetLibrary() -> ok (" + app.lastError() + ")");
assert(result.restarted === false, "…and it did NOT restart (nobody asked it to)");
console.log("REMOVED_OBJECTS=" + result.removed.objects);
console.log("REMOVED_SIDECARS=" + result.removed.sidecars);
console.log("REMOVED_PROJECTS=" + result.removed.projects);
console.log("REMOVED_THUMBNAILS=" + result.removed.thumbnails);
console.log("REMOVED_STAGING=" + result.removed.staging);
assert(result.removed.objects >= 1, "it removed the stored object(s)");
assert(result.removed.sidecars >= 1, "…and the sidecar(s)");
assert(result.removed.projects >= 1, "…and the project folder(s)");
assert(result.removed.staging === 1, "…and the abandoned staging temp, counted apart");
assert(result.removed.thumbnails >= 1, "…and the stored thumbnails");

// THE BUILT-INS, SEEDED AGAIN — the half of "exactly a first launch" that is
// not an absence. A driven session seeds nothing at launch and nothing here
// (by design), so the suite asks for the same seed a person's launch runs, on
// both sides of the comparison: census.js does it on the fresh root, this does
// it after the reset, and the two censuses are then a real count of shipped
// content rather than 0 == 0.
var seeded = materials.seedPresets();

// THE ONE PLACE IN THE TREE THAT NAMES THE SHIPPED TABLE'S SIZE
// (SEED-SMALL-1). Three suites used to carry 20, 31 and 30 as literals, so a
// twenty-first preset — or one map added to an existing one — made all three
// red about a product that was working perfectly. Everything else now derives:
// the bundle count from `materials.presets()` (the table itself, read live) and
// the map censuses from the bundles' own membership. Add a preset and this line
// is the only one to update.
var PRESET_TABLE = materials.presets();
assert(PRESET_TABLE.length === 20,
       "the shipped preset table has twenty entries (" + PRESET_TABLE.length + ")");
assert(seeded === PRESET_TABLE.length,
       "the built-ins seed again after the reset (" + seeded + " of " + PRESET_TABLE.length + ")");

// AND THEIR MAPS ARE MEMBERS AGAIN, not the user's tiles (V-2): the stamp is
// written by the seed, so a reset library that forgot it would put twenty
// presets' worth of pictures back in the tray.
var brickMaps = materials.members("00000000-0000-0000-0000-000000002014");
assert(brickMaps.length === 3, "a seeded preset has its three maps (" + brickMaps.length + ")");
var stamp = assets.metadata(brickMaps[0].guid);
assert(stamp.member === true, "…and the first one is stamped a member after the reset");
assert(stamp.memberOf === "00000000-0000-0000-0000-000000002014",
       "…with memberOf naming the preset it came in through (" + stamp.memberOf + ")");

// THE CENSUS, in census.js's own lines.
var store = assets.list({ scope: "store" });
console.log("CENSUS_ASSETS=" + store.length);
console.log("CENSUS_MATERIALS=" + assets.list({ scope: "store", type: "material" }).length);
console.log("CENSUS_TEXTURES=" + assets.list({ scope: "store", type: "texture" }).length);
console.log("CENSUS_PROJECTS=" + project.list().length);

// THE TABLES ARE REALLY THERE. This is the assertion the old button could not
// have passed: it dropped them and never created them again.
var fresh = project.create("After The Reset");
assert(fresh.length > 10, "a project can be created on the reset library");
assert(project.list().length === 1, "…and it is the only one");
var again = assets.importFile(FIXTURE_PNG);
assert(!!again, "…and an import works: the store is open with a fresh identity");
assert(project.close() === true, "close the project the assertion made");

// A SECOND RESET IS A NO-OP — bar the one project and one texture the
// assertion above just made, which it takes as well.
var second = app.resetLibrary();
assert(second.ok === true, "a second reset also succeeds");
console.log("SECOND_OBJECTS=" + second.removed.objects);
console.log("SECOND_STAGING=" + second.removed.staging);
assert(second.removed.staging === 0, "…with no staging temps left to find");
var third = app.resetLibrary();
assert(third.ok === true, "a third reset on an empty library succeeds");
// THE STEADY STATE, not zero (ATOM P2). A reset ends as a FIRST LAUNCH, and a
// first launch holds the shipped geometry: the twelve primitives, the Ground and
// the Teapot as baked library assets (a source and a bake object each). So a reset
// of a library with nothing of the user's in it removes exactly what the last one
// seeded and seeds it again — the invariant is that the number stops moving, and
// that nothing of the user's is in it.
var fourth = app.resetLibrary();
assert(fourth.ok === true, "a fourth reset succeeds");
assert(fourth.removed.objects === third.removed.objects
       && fourth.removed.sidecars === third.removed.sidecars,
       "…and removes the same as the one before it: the seed, and nothing else ("
       + third.removed.objects + " object(s), " + third.removed.sidecars + " sidecar(s))");
assert(fourth.removed.projects === 0 && fourth.removed.thumbnails === 0
       && fourth.removed.staging === 0,
       "…with no project, thumbnail or staging temp left to take");

// {restart: true} IS REFUSED IN A DRIVEN SESSION, and this assertion is the
// box safety one: a --script run respawns with `--script` still in its
// arguments, so a child would run this same file, reset the library and spawn
// another — an unbounded chain of processes each wiping what the last made.
// The refusal is the rule (the flags are never stripped), and it is what makes
// it safe for a ctest row to ask at all.
var projectsBefore = project.list().length;
var refused = app.resetLibrary({ restart: true });
assert(Object.keys(refused).length === 0, "{restart: true} answers an empty map in a driven session");
assert(("" + app.lastError()).indexOf("driven session") >= 0,
       "…and says why: " + app.lastError());
assert(project.list().length === projectsBefore, "…and nothing was reset by the refusal");
console.log("ALL PASS");
