// pool.doc / project_name_rules — ONE NAME, ONE PROJECT (lane PROJECT-NAMES-1,
// the owner 2026-10-07: "users should not see the double thumbnails").
//
// Projects are identified by GUID; a name is a label no two projects share
// (case-insensitively). The one rule is project.nextFreeName — "<name>",
// "<name> 2", "<name> 3" — and every naming door uses it:
//   1. project.create REFUSES a taken name, naming the free one, and writes
//      nothing;
//   2. project.rename refuses a taken name the same way, allows a case-only
//      change of the project's own name, and never touches a guid;
//   3. New Scene records a location ONLY when it is not the default root (the
//      dialog pre-fills the default root; it used to be recorded on every
//      project, which stopped it following the root).
// (The archive-import door — a reopened sample — is desktop.sample_names, an
// engine-up arm in pool.assets_import.)
//
// Document verbs only -> --headless.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function refusal(fn) {
    try { fn(); } catch (e) { return String(e); }
    return "";
}
function guids() { return project.list().map(function (p) { return p.guid; }).sort(); }
function nameOf(guid) {
    var rows = project.list();
    for (var i = 0; i < rows.length; ++i) if (rows[i].guid === guid) return rows[i].name;
    return null;
}

var base = "Name Rules " + Date.now();

// ---- 0. the rule -------------------------------------------------------------
assert(project.nextFreeName(base) === base, "a free name is itself");
assert(project.nextFreeName("  " + base + "  ") === base, "…trimmed");

// ---- 1. the create door -------------------------------------------------------
var a = project.create(base);
assert(a.length > 10, "create '" + base + "'");
assert(project.nextFreeName(base) === base + " 2", "the next free name is '" + base + " 2'");

var before = guids();
var why = refusal(function () { project.create(base); });
assert(why.length > 0, "a second create of the same name is REFUSED");
assert(why.indexOf("already exists") >= 0 && why.indexOf("'" + base + " 2'") >= 0,
       "…by name, offering the free one: " + why);
assert(JSON.stringify(guids()) === JSON.stringify(before), "…and writes no project row");
assert(refusal(function () { project.create(base.toUpperCase()); }).length > 0,
       "a case-variant of a taken name is refused too");
assert(refusal(function () { project.create("  " + base + " "); }).length > 0,
       "…and a padded one");

var b = project.create(base + " 2");
assert(b.length > 10 && b !== a, "create the offered '" + base + " 2'");
assert(project.nextFreeName(base) === base + " 3", "the family continues at 3");
assert(project.nextFreeName(base + " 2") === base + " 3",
       "a taken '<name> 2' continues the family, never '<name> 2 2'");

// ---- 2. the rename door -------------------------------------------------------
before = guids();
why = refusal(function () { project.rename(b, base); });
// The offer excludes the project being renamed, so b — itself "<base> 2" —
// is offered the name it already has: the first free one of the family for it.
assert(why.indexOf("already exists") >= 0 && why.indexOf("'" + base + " 2'") >= 0,
       "renaming into a taken name is REFUSED, offering the free one: " + why);
assert(nameOf(b) === base + " 2", "…and the project keeps its name");
assert(project.rename(a, base.toLowerCase()) === true,
       "a case-only change of the project's OWN name is allowed (" + app.lastError() + ")");
assert(nameOf(a) === base.toLowerCase(), "…and lands");
assert(project.rename(a, base) === true, "…and back");
assert(project.rename(b, "Renamed " + base) === true, "a rename to a free name lands");
assert(nameOf(b) === "Renamed " + base, "…in the list");
assert(project.current().name === "Renamed " + base, "…and in the open project's caption");
assert(JSON.stringify(guids()) === JSON.stringify(before), "no rename changed a guid");
assert(refusal(function () { project.rename("no-such-guid", "Anything " + base); })
           .indexOf("no project with guid") >= 0, "an unknown guid is refused by name");
assert(refusal(function () { project.rename(a, "   "); }).length > 0, "an empty name is refused");

// ---- 3. the location is recorded only when it was CHOSEN ---------------------
var roots = app.dataRoot();
var onDefault = project.create("Located Default " + base, { location: roots.projects });
assert(onDefault.length > 10, "create with the DEFAULT root as the location (what the dialog sends)");
assert(project.current().location === "",
       "…records no location, so it follows the root (" + JSON.stringify(project.current()) + ")");
var chosen = project.create("Located Chosen " + base, { location: roots.assetStore });
assert(chosen.length > 10, "create with a CHOSEN location");
assert(project.current().location.length > 0 &&
       project.current().location.indexOf(roots.assetStore.replace(/\/+$/, "")) === 0,
       "…records it (" + project.current().location + ")");

// ---- cleanup: this pool's process lives on --------------------------------------
assert(project.close() === true, "close");
[a, b, onDefault, chosen].forEach(function (g) {
    assert(project.remove(g) === true, "remove " + g);
});
console.log("project_name_rules: all assertions passed");
