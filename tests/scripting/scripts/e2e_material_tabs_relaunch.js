// scripting.e2e.material_tabs.relaunch — THE TABS ARE STILL THERE TOMORROW
// (MATERIALS_TABS_SPEC §2.7 / §5 step 9, lane MATERIALS-TABS-1).
//
// A SECOND PROCESS on the same home and data root as scripting.e2e.material_tabs,
// which left two materials open (A then B) in the library's tab set. "The page
// comes back to what was open on it" is not something one process can assert
// about itself: the set is written to the app's settings, and only a relaunch
// proves it is READ.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

// The set is restored when the page is first LOOKED AT (deserialising graphs is
// not boot work), so the space switch is the gesture under test as much as the
// settings key is.
assert(app.space("materials") === true, "app.space('materials')");

var tabs = materials.tabs();
console.log("    restored: " + JSON.stringify(tabs.map(function (t) {
    return t.name + " [" + t.scope + "]";
})));
assert(tabs.length === 2, "two tabs came back from the previous run");
assert(tabs[0].name === "Tabs A" && tabs[1].name === "Tabs B",
       "in the order they were left, by name");
assert(tabs[0].scope === "library" && tabs[1].scope === "library",
       "both at the scope they were opened at");
assert(tabs.filter(function (t) { return t.guid === ""; }).length === 0,
       "no anonymous canvas beside them — a restored set replaces it");
// A STALE ROW IS SKIPPED IN SILENCE (fix round F4): the previous run deleted
// "Tabs Ghost" from the library while its tab was open, so the saved set names
// a material that is not there. Opening it would read an empty definition, be
// refused for having no master node, and leave a toast plus a PERMANENT scene
// issue naming a raw guid — on every show of the page, for ever.
assert(tabs.filter(function (t) { return t.name === "Tabs Ghost"; }).length === 0,
       "the deleted material's row is gone from the restored set");
var legacy = editor.issues().filter(function (i) { return i.kind === "material.legacy"; });
assert(legacy.length === 0,
       "and it was skipped SILENTLY — no refusal issue was raised: "
       + JSON.stringify(legacy.map(function (i) { return i.message; })));
// The tab that was active comes back active (fix round F5 persists the ACTIVE
// ROW, not an index into a list the anonymous canvas is also in).
assert(materials.activeTab().name === "Tabs B", "the tab that was active is active again");

// They are real documents, not a list of names: the active one's canvas takes
// an edit and its own stack records it.
assert(materials.activate(tabs[0].guid) === true, "materials.activate(the first)");
assert(graph.undoState().undoCount === 0, "a restored tab starts with a clean stack");
var open = materials.loadGraph(tabs[0].guid);
assert(open.nodes === 2, "'Tabs A' still carries its two nodes");

console.log("material_tabs relaunch: PASS");
