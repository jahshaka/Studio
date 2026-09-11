// scripting.e2e.hierarchy_visibility — A HIDDEN PARENT HIDES ITS SUBTREE FROM
// EVERYTHING, AND SHOWING IT RESTORES EACH CHILD TO ITS OWN FLAG
// (RENDER_PIPELINE_AUDIT 1.1/1.2, smoke round lane L12).
//
// The rule: a node is on screen iff it AND every ancestor are visible; each
// node's own `visible` is the user's and no ancestor's change rewrites it.
// Before the fix the mirror pushed the node's OWN flag and the engine leaned on
// Ogre's setVisible cascade, so (1) hiding a model's root hid it on screen but
// its parts stayed voxelised and went on defining the lit volume ("bounds
// unchanged"), and (2) showing the root again re-drew every part — including a
// part the user had hidden itself, which then took clicks while the document
// still said hidden.
//
// Full stack at the tier a new project is born with (Epic: the hybrid, three
// bounces, the irradiance field), read back through world.giStatus (the lit
// volume the renderer resolved), scene.raycast (the viewport click's
// semantics) and the visibility report.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function J(x) { return JSON.stringify(x); }

var guid = project.create("Hierarchy Visibility " + Date.now());
assert(guid.length > 10, "project.create");
var r = world.rayon();
assert(r.tier === "epic" && r.technique === "vct_pcc_hybrid",
       "the scene renders at Epic (" + r.tier + ", " + r.technique + ")");

// A model the way an import lands: an EMPTY root with parts under it.
var root = scene.addEmpty({ position: { x: 0, y: 0, z: 0 } });
var tall = scene.addPrimitive("cube", { position: { x: 0, y: 6, z: 0 },
                                        scale: { x: 1, y: 12, z: 1 } });
var high = scene.addPrimitive("cube", { position: { x: 3, y: 30, z: 0 } });
node.reparent(tall, root);
node.reparent(high, root);
// The user hides ONE PART itself.
assert(node.setProperty(high, "visible", false), "the user hides the high part itself");
var tallTop = node.transform(tall).position.y + node.size(tall).height / 2;
console.log("    tall part top y = " + tallTop);

function settle() {
    editor.frame(3);
    world.refreshGi();
    editor.frame(6);
    return world.giStatus();
}
function vis(id) {
    return scene.nodes({ subtree: id, depth: 0, include: ["visibility"] })[0];
}
function hits(x, id) {
    var h = scene.raycast({ x: x, y: 60, z: 0 }, { x: 0, y: -1, z: 0 });
    for (var i = 0; i < h.length; i++) if (h[i].id === id) return true;
    return false;
}

// ---- 1. everything shown but the part the user hid ------------------------
var shown = settle();
console.log("    root shown:  bounds " + J(shown.boundsMin) + " .. " + J(shown.boundsMax));
assert(shown.live === true, "giStatus is live");
assert(shown.boundsMax.y > tallTop - 1.0, "the tall part is in the lit volume");
assert(shown.boundsMax.y < 25.0, "the part the user hid is not (it sits at y 30)");
assert(hits(0, tall) && !hits(3, high), "the tall part takes a click, the hidden one does not");

// ---- 2. hide the ROOT: every part leaves the lit volume and the clicks ----
assert(node.setProperty(root, "visible", false), "hide the root");
var hidden = settle();
console.log("    root hidden: bounds " + J(hidden.boundsMin) + " .. " + J(hidden.boundsMax));
assert(hidden.boundsMax.y < tallTop - 4.0,
       "hiding the ROOT takes its parts out of the lit volume (max y " + hidden.boundsMax.y + ")");
assert(!hits(0, tall), "a part under the hidden root takes no click");
var v = vis(tall);
assert(v.visible === true && v.visibleInScene === false,
       "the part's own flag is untouched; it is not visible in the scene " + J(v));
// The World panel's Fit button (world.fitGiBounds) agrees with the renderer:
// a part hidden by its root has no extent to pin a volume to. A refusal
// writes nothing, so the automatic fit below is untouched.
var fitThrew = "";
try { world.fitGiBounds({ nodes: [tall] }); } catch (e) { fitThrew = String(e); }
assert(fitThrew.indexOf("fitGiBounds") >= 0,
       "fitGiBounds refuses a part hidden by its root: " + fitThrew);

// ---- 3. show the ROOT: the parts come back, the one the user hid does not --
assert(node.setProperty(root, "visible", true), "show the root");
var again = settle();
console.log("    root shown:  bounds " + J(again.boundsMin) + " .. " + J(again.boundsMax));
assert(again.boundsMax.y > tallTop - 1.0, "the tall part is back in the lit volume");
assert(again.boundsMax.y < 25.0, "the part the user hid stays out of it");
assert(hits(0, tall), "the tall part takes clicks again");
assert(!hits(3, high), "the part the user hid is STILL not clickable (it was re-drawn before the fix)");
v = vis(high);
assert(v.visible === false && v.visibleInScene === false,
       "and the document still says hidden, own flag and all " + J(v));

// ---- 4. only its own flag brings it back ----------------------------------
assert(node.setProperty(high, "visible", true), "show the high part itself");
var all = settle();
console.log("    all shown:   bounds " + J(all.boundsMin) + " .. " + J(all.boundsMax));
assert(all.boundsMax.y > 29.0, "now it is in the lit volume");
assert(hits(3, high), "and takes a click");

console.log("e2e hierarchy visibility: all assertions passed");
