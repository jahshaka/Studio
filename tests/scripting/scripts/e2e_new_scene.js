// scripting.e2e.new_scene — THE NEW-SCENE VERB AND ITS TWO OPTIONS
// (owner review R1 / R1b, answers Q1 and Q4; lane SMALL-UI-A).
//
// The New Scene dialog has no capability of its own: its Empty scene checkbox
// and its Browse button are `project.create(name, {empty, location})`, which is
// what this drives. Four things, in the order a user meets them:
//
//   1. THE DEFAULT TEMPLATE — a ground, the sun, a Sky Light, and (the owner's
//      Q1 answer) the REALISTIC real-time sky with the sun following the
//      atmosphere. Both --engine-selftest hashes moved for this; here it is as
//      a document assertion, which is the half that cannot be read off a hash.
//   2. `{empty: true}` — the blank world, stated exactly: the root node and
//      NOTHING ELSE. No ground, no lights at all, and the flat default sky.
//   3. `{location}` — the project folder lands under the folder that was named.
//   4. A BAD LOCATION IS REFUSED BY NAME, and refused BEFORE anything is
//      written: no row, no folder, and the project that was open is still open.
//      (The `||` defect this lane removed lived on the other side of the same
//      question — `!name.isEmpty() || !name.isNull()` is true for the empty
//      string, so the desktop page minted NAMELESS projects. An empty name is
//      refused here too, by the one route both surfaces now take.)

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function J(x) { return JSON.stringify(x); }

function typesOf(ids) {
    var out = [];
    for (var i = 0; i < ids.length; ++i) out.push(ids[i].type + ":" + ids[i].name);
    return out;
}

// ---- 1. the default template ------------------------------------------------
var proj = project.create("New Scene Template " + Date.now());
assert(proj.length > 10, "project.create with no options");

var w = world.get();
console.log("world sky: " + J(w.sky));
assert(String(w.sky.type) === "Realistic",
       "the default new scene's sky is the REALISTIC real-time sky (owner Q1)");

var sun = world.sun();
console.log("sun: " + J({ light: sun.light, name: sun.name, reason: sun.reason }));
assert(sun && sun.light && sun.light.length > 0,
       "...and the scene has a sun (the first directional light)");
assert(node.property(sun.light, "followsAtmosphere") === true,
       "...with Sun Follows Atmosphere ON — which is what the realistic sky makes mean something");

var rows = scene.nodes({ depth: 1 });
console.log("template: " + J(typesOf(rows)));
assert(!!scene.find("Ground"), "the template stands on the default ground");
assert(!!scene.find("Sky Light"), "...and carries the Sky Light");

// ---- 2. the blank world -----------------------------------------------------
var empty = project.create("New Scene Empty " + Date.now(), { empty: true });
assert(empty.length > 10, "project.create({empty: true})");

var emptyRows = scene.nodes({ depth: 1 });
console.log("empty: " + J(typesOf(emptyRows)));
// scene.nodes() starts AT the root, so depth 1 is the root plus its children.
assert(emptyRows.length === 1,
       "an empty scene is the root node and nothing else (got " + emptyRows.length + " rows)");
assert(!scene.find("Ground"), "...no ground");
assert(!scene.find("Sky Light") && !scene.find("Directional Light"),
       "...and no lights at all: an empty world is not lit, which is what 'empty' means");
var ew = world.get();
console.log("empty world sky: " + J(ew.sky));
assert(String(ew.sky.type) === "SingleColor",
       "...and no sky beyond the document's own default flat colour");

// ---- 3. a chosen location ---------------------------------------------------
// app.dataRoot() names this run's own folders; `assetStore` is a directory the
// run has certainly created and can certainly write, on every box the gate
// runs on, and it is NOT the projects root — so "it landed where I said"
// distinguishes itself from "it landed where it always does".
var roots = app.dataRoot();
console.log("roots: " + J(roots));
var where = roots.assetStore;
assert(where && where.length > 0, "a location to create into: " + where);
assert(where !== roots.projects, "...that is not the default projects root");

var placed = project.create("New Scene Located " + Date.now(), { location: where });
assert(placed.length > 10, "project.create({location})");
var folder = project.current().folder;
console.log("folder: " + folder);
assert(folder.indexOf(where) === 0,
       "the project folder is under the location that was named, not under the projects root");

// ---- 3b. AND IT CAN BE FOUND AGAIN (fix round F1) ---------------------------
// WHERE IT LANDS IS NOT THE QUESTION — where it is FOUND is. The location is
// recorded with the project row and ProjectService::projectFolderFor is the one
// resolver; before the fix every resolver rebuilt the path from the DEFAULT
// root, so a close and reopen pointed the whole session — baked maps, exports,
// the thumbnail, the delete — at a folder that does not exist.
//
// A file inside the located folder, so the DELETE below can be checked on disk
// and not merely in the catalog.
var proof = folder + "/small-ui-a-proof.zip";
assert(project.exportArchive(proof).path === proof, "an archive written INSIDE the located folder");

assert(project.close() === true, "close the located project");
assert(project.open(placed) === true, "project.open(guid) of a located project");
var reopened = project.current();
console.log("reopened folder: " + reopened.folder);
assert(reopened.guid === placed, "...it is the same project");
assert(reopened.folder.indexOf(where) === 0,
       "...and it comes back pointing UNDER ITS OWN LOCATION, not the default projects root (" +
       reopened.folder + ")");
assert(reopened.folder === folder, "...at exactly the folder it was created in");

// ---- 3c. a located project DELETES its own folder ---------------------------
assert(project.close() === true, "close it again");
assert(project.remove(placed) === true, "project.remove(located)");
var stillListed = project.list().filter(function (p) { return p.guid === placed; });
assert(stillListed.length === 0, "...the row is gone");
var gone = null;
try { project.importArchive(proof); } catch (e) { gone = String(e); }
assert(gone !== null,
       "...AND THE LOCATED FOLDER IS GONE WITH IT — the archive that was inside it " +
       "cannot be read any more. Before the fix the delete removed the DEFAULT root's " +
       "folder of the same guid (nothing) and left this one on disk forever.");
console.log("   -> " + gone);

// ---- 3d. a recorded location that is not there refuses BY NAME --------------
// An unplugged drive, expressed with the verbs alone: the inner project's
// location is the OUTER project's folder, so removing the outer one takes the
// inner one's recorded root with it. Nothing about that arrangement is special
// — it is the same state a disconnected drive leaves behind.
var outer = project.create("New Scene Host " + Date.now());
var outerFolder = project.current().folder;
var inner = project.create("New Scene Orphan " + Date.now(), { location: outerFolder });
assert(project.current().folder.indexOf(outerFolder) === 0, "the inner project lives inside it");
assert(project.close() === true, "close");
assert(project.remove(outer) === true, "remove the project the location was inside");

var orphan = null;
try { project.open(inner); } catch (e) { orphan = String(e); }
assert(orphan !== null, "opening a project whose recorded location is gone is REFUSED");
console.log("   -> " + orphan);
assert(orphan.indexOf(outerFolder) >= 0,
       "...by name, with the missing path in the message — never a silent fall-back to the " +
       "default root (which would open an empty world under the project's own guid) and never " +
       "an auto-created empty folder");
assert(!project.current() || project.current().guid !== inner,
       "...and the refusal did not point the session at it");

// ---- 4. the refusals --------------------------------------------------------
// A world open again first: §3d's refusal deliberately left the session with
// none, and "a refused create leaves the project that was open exactly where it
// was" needs one to mean anything.
assert(project.create("New Scene Refusals " + Date.now()).length > 10,
       "a project to be left alone by the refusals below");
var openBefore = project.current().guid;

function refused(call, what, expect) {
    var threw = null;
    try { call(); } catch (e) { threw = String(e); }
    assert(threw !== null, what + " is refused");
    console.log("   -> " + threw);
    assert(threw.indexOf(expect) >= 0, "...by name (" + expect + ")");
}

refused(function () { project.create("Nowhere", { location: "/definitely/not/here" }); },
        "a location that does not exist", "does not exist");
refused(function () { project.create("   "); },
        "an empty name", "non-empty name");
refused(function () { project.create("Typo", { emtpy: true }); },
        "an unknown option key", "unknown option");

assert(project.current().guid === openBefore,
       "a refused create leaves the project that was open exactly where it was");

console.log("PASS scripting.e2e.new_scene");
