// scripting.e2e.material_tabs — SEVERAL MATERIALS OPEN AT ONCE
// (MATERIALS_TABS_SPEC §5, lane MATERIALS-TABS-1).
//
// The owner's ask, in his words (review 2026-09-18, R-tabs): "currently I have
// to reopen any custom material to edit them". The Materials page held ONE of
// everything — one graph, one canvas, one undo stack, one 1.5 s autosave — so
// opening a material threw the previous one away. This drives the five verbs
// that open, list, activate and close TABS, and it pins the three properties
// that cost real work:
//
//   * IDENTITY. A material already open at a scope is ACTIVATED, not opened
//     twice; the same guid at the two scopes (library original / the project's
//     pinned copy) is two documents by the four-drawer rule.
//   * THE UNDO STACK IS PER DOCUMENT. A deletion made in B is still B's after
//     switching to A and back, and A's stack never saw it.
//   * THE AUTOSAVE IS PER DOCUMENT (the spec's C5 — the defect this lane
//     removes by construction): an edit made less than 1.5 s before leaving a
//     material lands on THAT material. NOTHING HERE IS TIMED: §3 reads which
//     document the edit armed in the same breath as the edit, §7 reads which
//     material the write landed on out of the stored definitions, and §7b
//     proves the close is the writer on a material edited and closed in two
//     adjacent verbs. (Reading "a save is pending" ten verbs after the edit —
//     one of them a preset open with a shader compile — is a wall clock, and
//     a wall clock measures nothing in this engine: TABS-SMALL-1.)
//
// The graphs are authored through `graph.*` on the SCRIPT-local graph first
// (nothing adds a node ON THE PAGE by verb — the palette drag is a rig
// gesture, app.input_keys), then opened; the page's own edit verbs
// (graph.removeNode) work on whichever tab is active, which is the property
// under test.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function tabGuids() { return materials.tabs().map(function (t) { return t.guid; }); }
function tabOf(guid) {
    var rows = materials.tabs().filter(function (t) { return t.guid === guid; });
    return rows.length ? rows[0] : null;
}
// The NON-MASTER node of a two-node graph: the definition's ids are the page's
// (the page deserialises the same file), so an id read here addresses the
// canvas.
function loneNodeId(guid) {
    materials.loadGraph(guid);
    var ids = graph.nodes().filter(function (n) { return !n.master; })
                   .map(function (n) { return n.id; });
    assert(ids.length === 1, "authored graph '" + guid + "' has one node beside the master");
    return ids[0];
}
function nodeCount(guid) { return materials.loadGraph(guid).nodes; }
// ANY non-master node of a material's graph (a preset has several): the
// definition's ids are the page's, so an id read here addresses the canvas.
function loneNodeIdOf(guid) {
    materials.loadGraph(guid);
    var ids = graph.nodes().filter(function (n) { return !n.master; })
                   .map(function (n) { return n.id; });
    assert(ids.length >= 1, "graph '" + guid + "' has a node beside the master");
    return ids[0];
}

// ---- 1. two authored materials -------------------------------------------
var projectGuid = project.create("Material Tabs " + Date.now());
assert(projectGuid.length > 10, "project.create");

var A = materials.create("Tabs A", { graph: true });
assert(A.length > 10, "materials.create A");
assert(graph.addNode("float").length > 0, "A: a float node");
assert(graph.save() === true, "A: saved (a two-node definition)");

var B = materials.create("Tabs B", { graph: true });
assert(B.length > 10, "materials.create B");
assert(graph.addNode("float").length > 0, "B: a float node");
assert(graph.save() === true, "B: saved");

assert(nodeCount(A) === 2 && nodeCount(B) === 2, "both definitions carry two nodes");

// ---- 2. open both: two tabs, B active ------------------------------------
assert(app.space("materials") === true, "app.space('materials')");
// The boot canvas is a document too (the anonymous untitled one), and it
// stands aside for the first material that opens.
assert(materials.tabs().length === 1 && materials.tabs()[0].guid === "",
       "the page boots on the anonymous tab");

var openA = materials.open(A);
assert(openA.guid === A && openA.scope === "library" && openA.readOnly === false,
       "materials.open(A) -> a library tab");
var openB = materials.open(B);
assert(openB.guid === B, "materials.open(B)");

var tabs = materials.tabs();
assert(tabs.length === 2, "two tabs (the anonymous boot tab stood aside)");
assert(tabs[0].guid === A && tabs[1].guid === B, "in bar order: A then B");
assert(materials.activeTab().guid === B, "the material just opened is the active one");

// ---- 3. an edit on the ACTIVE tab lands on its own stack ------------------
var idB = loneNodeId(B);
assert(graph.removeNode(idB) === true, "graph.removeNode on the page (B is active)");
assert(graph.undoState().undoCount === 1, "B's stack carries the deletion");
// THE AUTOSAVE IS ARMED ON THE DOCUMENT THE EDIT WAS MADE ON — read HERE, in
// the same breath as the edit. `dirty` is "a save is pending", and pending is
// a 1.5 s WALL CLOCK: it used to be read ten verbs later, one of them a preset
// open with a shader compile and two disk-cache writes, so under a cold-cache
// gate the timer had legitimately fired and the suite redded on the clock
// rather than on the product (a wall-clock settle measures nothing in this
// engine). What the clock cannot change is WHICH document the edit armed.
assert(tabOf(B).dirty === true, "the edit armed the autosave on B");
assert(tabOf(A).dirty === false, "...and on B alone — A's document is clean");

// ---- 4. the stack is PER DOCUMENT ----------------------------------------
assert(materials.activate(A) === true, "materials.activate(A)");
assert(materials.activeTab().guid === A, "A is active");
assert(graph.undoState().undoCount === 0, "A's stack is empty — it never saw B's edit");
assert(materials.activate(B) === true, "materials.activate(B)");
assert(graph.undoState().undoCount === 1, "B's stack still carries it");

// ---- 5. identity: the same material does not open twice ------------------
materials.open(A);
assert(materials.tabs().length === 2, "opening A again ACTIVATES it (identity is guid+scope)");
assert(materials.activeTab().guid === A, "...and makes it active");

// ---- 6. a shipped preset opens EDITABLE in a project (PRESET-EDIT-1) -----
//
// It used to open READ-ONLY wherever it was found, with Customise beside the
// banner. The owner's rule of 2026-09-21: only the MASTER is locked, and a
// preset a project holds is that project's to edit — so the tab takes edits
// and the FIRST SAVE makes the project its own copy (the copy's full contract
// is scripting.e2e.preset_edit's; what belongs here is what the TAB says).
var preset = materials.open("Brick PBR");
assert(preset.readOnly === false && preset.editable === true,
       "a shipped preset opens EDITABLE with a project open");
assert(preset.master === preset.guid,
       "…and the tab names the shipped master behind it (itself, until a copy exists)");
assert(materials.tabs().length === 3, "three tabs");
assert(tabOf(preset.guid).editable === true, "…the tab list agrees");
assert(materials.closeTab(preset.guid) === true, "materials.closeTab(preset)");
assert(materials.tabs().length === 2, "two tabs again");

// ---- 6b. THE PAGE'S OWN FIRST EDIT OF A PRESET COPIES IT ON WRITE --------
//
// The gesture the owner makes: open a preset the project holds, delete a node,
// let the save land. It is asserted HERE, on the page, because the page is
// where it CRASHED: the copy unpins the master, this page closes project-scope
// tabs whose pin has gone, and the document being saved was the one it closed
// (a use-after-free on the very first try on the rig). A close FLUSHES the
// pending autosave, which is that save landing, with no wall clock in it.
var pageEdit = materials.open("Brick PBR");
assert(pageEdit.editable === true, "the preset opens editable on the page");
var brickNode = loneNodeIdOf(pageEdit.guid);
assert(graph.removeNode(brickNode) === true, "the page's canvas takes the deletion");
assert(materials.closeTab(pageEdit.tab) === true,
       "closing the tab flushes the pending autosave — the first edit landing");
var projectMaterials = assets.list({ scope: "project", type: "material" });
var brickCopy = projectMaterials.filter(function (a) {
    return materials.masterOf(a.guid) === pageEdit.guid && a.guid !== pageEdit.guid;
});
assert(brickCopy.length === 1,
       "…and the project holds its OWN copy of the preset (" + brickCopy.length + ")");
assert(brickCopy[0].name === "Brick PBR", "…under the preset's own name");
assert(assets.list({ scope: "project", type: "material" }).filter(function (a) {
           return a.guid === pageEdit.guid;
       }).length === 0,
       "…and has let go of the shipped master");
assert(materials.tabs().length === 2, "two tabs again");

// ---- 7. the pending save lands on its OWN material ----------------------
// B's deletion (§3) armed B's autosave and nothing else's — that was read at
// the edit. WHERE the write lands is what this lane exists for, and it is read
// off the stored definitions, never off a clock: whichever of the two writers
// ran (the 1.5 s timer, or the close below), the edit reached B and left A
// alone. The page's single timer used to fire against whatever material was
// open 1.5 s later, which this catches as A having lost a node.
assert(materials.activate(B) === true, "B is active again");
assert(materials.closeTab(B) === true, "materials.closeTab(B)");
assert(materials.tabs().length === 1 && tabGuids()[0] === A, "one tab left: A");
assert(nodeCount(B) === 1, "B's stored definition lost the node — the save was B's own");
assert(nodeCount(A) === 2, "A is untouched — the edit never reached the other material");

// ---- 7b. and the CLOSE is what writes it, with nothing in between --------
// On a material of its own, so that no verb at all stands between the edit and
// the close: two adjacent verbs cannot straddle a 1.5 s timer, whatever the
// box is doing, so this arm proves the close FLUSHES (rather than merely
// agreeing with a timer that has already fired) without measuring time.
var closeMe = materials.create("Tabs Close", { graph: true });
assert(graph.addNode("float").length > 0, "C: a float node");
assert(graph.save() === true, "C: saved (a two-node definition)");
assert(nodeCount(closeMe) === 2, "C's stored definition carries two nodes");
assert(materials.open(closeMe).guid === closeMe, "materials.open(C)");
var idC = loneNodeId(closeMe);
assert(graph.removeNode(idC) === true, "C: the node is removed on the page");
assert(tabOf(closeMe).dirty === true, "...and C's autosave is armed (read at the edit)");
assert(materials.closeTab(closeMe) === true, "materials.closeTab(C) — in the next breath");
assert(nodeCount(closeMe) === 1, "the CLOSE wrote it: C's stored definition lost the node");
assert(materials.tabs().length === 1 && tabGuids()[0] === A, "one tab left again: A");

// ---- 8. the project's own copy is a SECOND document ----------------------
// The four-drawer rule: a library original and the project's pinned copy are
// two things, so the same guid open at both scopes is two tabs and neither
// one's save may touch the other.
assert(assets.addToProject(A) === A, "assets.addToProject(A) — the pin, same guid");
var projectTab = materials.open(A, { scope: "project" });
assert(projectTab.scope === "project", "materials.open(A, {scope:'project'})");
assert(materials.tabs().length === 2, "two tabs: A's library copy and the project's");
assert(tabGuids()[0] === A && tabGuids()[1] === A, "the same guid, twice, at two scopes");

// ...and it goes when the project does (§2.7), while the LIBRARY tab stays.
assert(project.close() === true, "project.close()");
app.space("materials");
var afterClose = materials.tabs();
assert(afterClose.filter(function (t) { return t.scope === "project"; }).length === 0,
       "no project-scope tab survives the project it belonged to");
assert(afterClose.filter(function (t) { return t.guid === A; }).length === 1,
       "the LIBRARY tab stays across the switch — the library is the same library");

// Reopening the project brings its own set back.
assert(project.open(projectGuid) === true, "project.open (the same project)");
app.space("materials");
assert(materials.tabs().filter(function (t) { return t.scope === "project"; }).length === 1,
       "the project's tab set is restored with the project");

// ---- 9. refusals -----------------------------------------------------------
var refused = false;
try { materials.open("no such material at all"); } catch (e) { refused = true; }
assert(refused, "materials.open refuses a name nothing answers to");
refused = false;
try { materials.activate(42); } catch (e) { refused = true; }
assert(refused, "materials.activate refuses a tab that is not there");

// ---- 9b. NEW MATERIAL OPENS A TAB, it does not take one over (F3) --------
// A New used to wipe the ACTIVE document's identity while its autosave was
// still armed — so the pending edit fired later under the NEW material's guid,
// and a read-only PRESET tab was silently turned into the user's new material.
var presetTab = materials.open("Gold PBR");
assert(presetTab.editable === true && presetTab.master === presetTab.guid,
       "a preset tab is open, editable, and knows which shipped material it is");
var before = materials.tabs().length;
var minted = materials.newMaterial("Gold PBR", { name: "Tabs New" });
assert(minted.guid.length > 10 && minted.name === "Tabs New",
       "materials.newMaterial -> a library material of its own");
assert(materials.tabs().length === before + 1, "New opened a TAB, it did not take one over");
assert(materials.activeTab().guid === minted.guid, "...and the new material is the active one");
var presetStill = materials.tabs().filter(function (t) { return t.guid === presetTab.guid; });
assert(presetStill.length === 1 && presetStill[0].master === presetTab.guid,
       "the preset tab is still open on the preset — New took no tab over");
// AND THE PRESET ITSELF WAS NOT WRITTEN TO. Asked of the GRAPH, because a
// preset nobody has used has no library row at all (seeding is on first USE):
// it still reads as the shipped material, on its own reserved guid.
var presetGraph = materials.loadGraph(presetTab.guid);
assert(presetGraph.presetMaster === presetTab.guid && presetGraph.nodes >= 2,
       "and the preset itself was not written to");
materials.closeTab(presetTab.guid);
materials.closeTab(minted.guid);

// ---- 9c. A PROJECT COPY WHOSE PIN GOES CLOSES (F1, every door — DRAWERS-1)
// The pin can go while the tab is open, and by several doors: the project
// drawer's Delete (which told the page from the start), the editor tray's,
// this verb, a folder delete that takes its contents out, a project closed
// under a hidden page. The page used to hear about ONE of them and keep the
// others' tabs open over a pin that was gone — where the danger was that a
// save fell through to the LIBRARY original, one project's edits landing on
// the material every other project takes its copies from.
//
// It listens to the ONE announcement they all make now
// (services/projectmembership.h) and re-checks its Project-origin tabs, so
// this verb closes the tab exactly as the drawer's Delete does: nothing is
// left editing a pin that is not there. The save guard underneath is still
// there and is now the SECOND lock, with no route reaching it from this door
// — which is why the assertion below is that NOTHING was written and nothing
// was refused, rather than one refusal.
var beforeLibrary = materials.loadGraph(A).nodes;
assert(materials.open(A, { scope: "project" }).scope === "project", "A's project copy is open");
function projectTabsFor(guid) {
    return materials.tabs().filter(function (t) {
        return t.guid === guid && t.scope === "project";
    });
}
assert(projectTabsFor(A).length === 1, "...as a tab");
assert(assets.removeFromProject(A) === true, "assets.removeFromProject(A) — the pin goes");
assert(projectTabsFor(A).length === 0,
       "...and the tab goes with it, by the door that used to say nothing");
assert(materials.loadGraph(A).nodes === beforeLibrary,
       "the LIBRARY original is untouched — nothing was saved on the way out");
var refusals = editor.issues().filter(function (i) { return i.kind === "material.save"; });
assert(refusals.length === 0,
       "...and no save was even attempted, so the user is told nothing");

// ---- 10. open/close twenty times: the memory comes back ------------------
// A closed document frees its graph, its canvas and its undo history — none of
// which anything ever freed before this lane (NodeGraph had no destructor at
// all, and every open leaked one).
function openCloseOnce() {
    materials.open(B);
    materials.closeTab(B);
}
openCloseOnce();
var rssAfterFirst = app.memoryStats().residentBytes;
for (var i = 0; i < 20; i++) openCloseOnce();
var rssAfter20 = app.memoryStats().residentBytes;
var growth = (rssAfter20 - rssAfterFirst) / rssAfterFirst;
console.log("    RSS after 1 pass: " + rssAfterFirst + ", after 21: " + rssAfter20
            + " (" + (growth * 100).toFixed(2) + "%)");
assert(growth < 0.05, "twenty opens and closes grow the resident set by under 5%");

// ---- 11. what the RELAUNCH half will read --------------------------------
// The tab set is persisted PER PROJECT, plus one for the library, so the set
// this run leaves behind has to be left under the key the next process will
// read: it starts with no project open, which is the LIBRARY's key.
// A MATERIAL THAT IS DELETED WHILE ITS TAB IS OPEN leaves a stale row in the
// saved set (F4): the relaunch must skip it in SILENCE, not open it, fail to
// find a master node and raise a toast plus a permanent scene issue naming a
// raw guid on every show of the page.
var C = materials.create("Tabs Ghost", { graph: true });
graph.addNode("float"); graph.save();
materials.open(C);
assert(materials.tabs().filter(function (t) { return t.guid === C; }).length === 1,
       "a third material is open");
assert(assets.remove(C) === true, "assets.remove(C) — deleted from the library under its own tab");

assert(project.close() === true, "project.close() — the library's set is what a fresh launch reads");
app.space("materials");
while (materials.tabs().length > 1 || materials.tabs()[0].guid !== "")
    materials.closeTab(0);
materials.open(A);
materials.open(B);
materials.activate(B);   // the relaunch asserts THIS tab comes back active (F5)
var left = materials.tabs();
assert(left.length === 2 && left[0].guid === A && left[1].guid === B,
       "the run leaves A and B open, in that order");
assert(materials.activeTab().guid === B, "with B active");
console.log("    left open: " + JSON.stringify(tabGuids()));
console.log("    project: " + projectGuid);

console.log("material_tabs: PASS");
