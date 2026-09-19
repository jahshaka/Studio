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

// ---- 4. the refusals --------------------------------------------------------
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
