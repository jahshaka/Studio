// scripting.e2e.desktops — DESKTOP_SLIDER_SPEC: the desktop.* verbs behind
// the desktop view modes and the slider filmstrip, end to end in the real app
// (windowed --script run, scratch HOME).
//
// Note the scratch HOME persists across ctest reruns: the script forces known
// state instead of asserting first-boot defaults, and uses unique names.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function throws(fn, msg) {
    var threw = false;
    try { fn(); } catch (e) { threw = true; console.log("ok: " + msg + " (" + e.message + ")"); }
    if (!threw) throw new Error("assert failed (no error): " + msg);
}

// ---- registry: the desktop module is present and documented ----
var mods = api.verbs().filter(function (m) { return m.module === "desktop"; });
assert(mods.length === 1, "desktop module registered");
var names = mods[0].verbs.map(function (v) { return v.name; }).sort().join(",");
assert(names === "moveTile,setViewMode,tiles,viewMode", "desktop verbs: " + names);

// ---- fixtures: three fresh (never-assigned) projects on desktop 1 ----
var t = Date.now();
var g1 = project.create("Slider A " + t);
var g2 = project.create("Slider B " + t);
var g3 = project.create("Slider C " + t);
assert(g1.length > 10 && g2.length > 10 && g3.length > 10, "three projects created");

app.desktop(1);
app.space("desktop");

// ---- view mode: valid value, forced reset, round-trip, validation ----
var mode = desktop.viewMode();
assert(["rows", "freeform", "sliders"].indexOf(mode) >= 0, "viewMode returns a valid mode: " + mode);
assert(desktop.setViewMode("rows") === true, "setViewMode(rows)");
assert(desktop.viewMode() === "rows", "viewMode round-trips rows");
throws(function () { desktop.setViewMode("bogus"); }, "setViewMode rejects an unknown mode");
throws(function () { desktop.moveTile(g1, 1, 0); }, "moveTile requires sliders mode");

// ---- enter sliders: every tile gets a filmstrip assignment ----
assert(desktop.setViewMode("sliders") === true, "setViewMode(sliders)");
assert(desktop.viewMode() === "sliders", "viewMode round-trips sliders");

function tileOf(guid) {
    var hits = desktop.tiles().filter(function (t) { return t.guid === guid; });
    if (hits.length !== 1) throw new Error("tile not found: " + guid);
    return hits[0];
}
assert(desktop.tiles().length >= 3, "tiles() lists the desktop");
[g1, g2, g3].forEach(function (g, i) {
    var tile = tileOf(g);
    assert(tile.row >= 1 && tile.index >= 0, "tile " + (i + 1) + " seeded into row " + tile.row);
});

// ---- moveTile: insert-at semantics ----
assert(desktop.moveTile(g1, 2, 0) === true, "moveTile(g1, row 2, index 0)");
assert(tileOf(g1).row === 2 && tileOf(g1).index === 0, "g1 landed at row 2 index 0");

assert(desktop.moveTile(g2, 2, -1) === true, "moveTile(g2, row 2, append)");
var p2 = tileOf(g2);
assert(p2.row === 2 && p2.index > tileOf(g1).index, "append lands after the row's tiles");

assert(desktop.moveTile(g3, 2, 0) === true, "moveTile(g3, row 2, index 0)");
assert(tileOf(g3).index === 0, "insert at 0 takes the head");
assert(tileOf(g1).index === 1, "existing tile shifted right");

throws(function () { desktop.moveTile("not-a-guid", 1, 0); }, "moveTile rejects an unknown guid");

// ---- persistence: survives a desktop switch (full repopulate from the DB) ----
app.desktop(2);
app.desktop(1);
assert(desktop.viewMode() === "sliders", "view mode persisted per desktop");
assert(tileOf(g3).row === 2 && tileOf(g3).index === 0, "assignment survives a repopulate");
assert(tileOf(g1).row === 2 && tileOf(g1).index === 1, "row order survives a repopulate");

// ---- lossless: leaving sliders discards nothing ----
assert(desktop.setViewMode("freeform") === true, "switch away to freeform");
assert(desktop.setViewMode("sliders") === true, "and back to sliders");
assert(tileOf(g3).row === 2 && tileOf(g3).index === 0, "mode switching is lossless");

// leave the desktop in rows mode for whoever runs next
assert(desktop.setViewMode("rows") === true, "restored rows mode");
console.log("desktop slider verbs e2e: all checks passed");
