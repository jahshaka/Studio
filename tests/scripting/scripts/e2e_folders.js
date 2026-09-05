// scripting.e2e.folders — OUTLINER FOLDERS (SPECS/SCENEGRAPH_SPEC.md §6b),
// end-to-end through the verbs, before a pixel of panel exists (the API-first
// rule). Runs --headless: every folder verb is a document verb.
//
// What this pins, in order:
//   the vocabulary            scene.folders / createFolder / renameFolder / removeFolder
//                             + node.setFolder / node.folder
//   THE LAW                   filing a node NEVER reparents it, and removing a
//                             folder NEVER deletes a node — those two are the
//                             whole reason folders are metadata and not nodes
//   ancestors                 "Props/Kitchen" implies "Props"
//   empty folders survive     the reason the list is persisted and not derived
//   refusals are LOUD         every bad path fails, none of them silently
//   undo                      one step per gesture (counted; a script run's own
//                             macro is open, so the stack index cannot move —
//                             editor.undoState().pushes is the observable)
//   persistence               save / close / reopen keeps folders AND membership,
//                             and the round trip goes through the EDITOR section

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function fails(fn, msg) {
    var threw = false;
    try { fn(); } catch (e) { threw = true; }
    assert(threw, "refused: " + msg);
}
function pushes() { return editor.undoState().pushes; }

var guid = project.create("Folders Test " + Date.now());
assert(guid.length > 10, "project.create -> " + guid);

// ---- a fresh scene has no folders -------------------------------------------
assert(scene.folders().length === 0, "a new scene has no folders");

// ---- create ------------------------------------------------------------------
var before = pushes();
assert(scene.createFolder("Props") === true, "createFolder('Props')");
assert(pushes() === before + 1, "createFolder recorded ONE undo step");
assert(scene.folders().length === 1 && scene.folders()[0] === "Props",
       "folders() -> " + scene.folders().join(", "));

// An intermediate path implies its ancestors, exactly like Unreal's outliner.
assert(scene.createFolder("Props/Kitchen/Cutlery") === true, "createFolder nested");
var f = scene.folders();
assert(f.indexOf("Props") >= 0 && f.indexOf("Props/Kitchen") >= 0 &&
       f.indexOf("Props/Kitchen/Cutlery") >= 0,
       "every ancestor exists: " + f.join(", "));

fails(function () { scene.createFolder("Props"); }, "createFolder on an existing folder");
fails(function () { scene.createFolder("   "); }, "createFolder with no name");

// Path spelling is normalised, not taken literally.
assert(scene.createFolder("  //Set//Dressing//  ") === true, "createFolder normalises the path");
assert(scene.folders().indexOf("Set/Dressing") >= 0,
       "normalised to 'Set/Dressing': " + scene.folders().join(", "));

// ---- membership: THE LAW — filing is not parenting -----------------------------
var cube = scene.addPrimitive("cube");
var sphere = scene.addPrimitive("sphere");
var root = scene.root();
assert(node.info(cube).parent === root, "the cube starts at the root level");

before = pushes();
assert(node.setFolder(cube, "Props/Kitchen") === true, "node.setFolder");
assert(pushes() === before + 1, "setFolder recorded ONE undo step");
assert(node.folder(cube) === "Props/Kitchen", "node.folder reads it back");
assert(node.info(cube).parent === root,
       "THE LAW: filing the cube did NOT reparent it (parent is still the root)");
assert(node.info(cube).position !== undefined, "the cube still has its transform");

// A folder that does not exist yet is created by filing into it.
assert(node.setFolder(sphere, "Vehicles") === true, "setFolder into a fresh path");
assert(scene.folders().indexOf("Vehicles") >= 0, "the fresh path became a folder");

// An empty path is the root level, and it is not an error.
assert(node.setFolder(sphere, "") === true, "setFolder('') returns to the root level");
assert(node.folder(sphere) === "", "node.folder is empty at the root level");
// ...and the folder it left BEHIND survives empty. This is exactly what an
// implicit-from-membership model cannot express, and why the list is persisted.
assert(scene.folders().indexOf("Vehicles") >= 0, "an emptied folder survives");

fails(function () { node.setFolder("no-such-node", "Props"); }, "setFolder on an unknown node");
fails(function () { node.setFolder(root, "Props"); }, "setFolder on the world root");

// A node INSIDE a real parent chain: the metadata is accepted (it is metadata),
// and §6b is explicit that only root-level entries are grouped on screen.
var child = scene.addPrimitive("cone", { parent: cube });
assert(node.info(child).parent === cube, "the cone is a child of the cube");
assert(node.setFolder(child, "Props") === true, "setFolder on a nested node is recorded");
assert(node.info(child).parent === cube, "...and STILL did not reparent it");

// ---- rename --------------------------------------------------------------------
before = pushes();
assert(scene.renameFolder("Props", "Set Pieces") === true, "renameFolder");
assert(pushes() === before + 1, "renameFolder recorded ONE undo step");
f = scene.folders();
assert(f.indexOf("Props") < 0, "the old path is gone");
assert(f.indexOf("Set Pieces") >= 0 && f.indexOf("Set Pieces/Kitchen") >= 0 &&
       f.indexOf("Set Pieces/Kitchen/Cutlery") >= 0,
       "every SUB-FOLDER came along: " + f.join(", "));
assert(node.folder(cube) === "Set Pieces/Kitchen", "every MEMBER came along");

fails(function () { scene.renameFolder("Nope", "X"); }, "renameFolder on a folder that is not there");
fails(function () { scene.renameFolder("Set Pieces", "A/B"); }, "renameFolder to a PATH, not a name");
fails(function () { scene.renameFolder("Set Pieces", "  "); }, "renameFolder to nothing");
fails(function () { scene.renameFolder("Set Pieces", "Set"); }, "renameFolder onto an existing folder");

// ---- remove: contents move UP, nothing is deleted --------------------------------
var nodeCountBefore = scene.nodes().length;
before = pushes();
assert(scene.removeFolder("Set Pieces/Kitchen") === true, "removeFolder");
assert(pushes() === before + 1, "removeFolder recorded ONE undo step");
assert(scene.nodes().length === nodeCountBefore,
       "THE OTHER LAW: removing a folder deleted NO nodes");
assert(node.folder(cube) === "Set Pieces", "the member moved UP one level");
f = scene.folders();
assert(f.indexOf("Set Pieces/Kitchen") < 0, "the folder is gone");
assert(f.indexOf("Set Pieces/Cutlery") >= 0,
       "the SUB-folder moved up too: " + f.join(", "));

fails(function () { scene.removeFolder("Nope"); }, "removeFolder on a folder that is not there");
fails(function () { scene.removeFolder(""); }, "removeFolder on the root level");

// Removing a top-level folder returns its members to the root level.
assert(scene.removeFolder("Set Pieces") === true, "removeFolder at the top level");
assert(node.folder(cube) === "", "its member is back at the root level");
assert(scene.nodes().length === nodeCountBefore, "still no node was deleted");

// ---- persistence: save / close / reopen -------------------------------------------
assert(scene.createFolder("Lighting") === true, "createFolder('Lighting')");
assert(scene.createFolder("Lighting/Practicals") === true, "createFolder nested for the round trip");
assert(node.setFolder(cube, "Lighting/Practicals") === true, "file the cube for the round trip");
var cubeName = node.info(cube).name;
var foldersBefore = scene.folders().slice().sort();

project.save();
project.close();
project.open(guid);

var reopened = scene.folders().slice().sort();
assert(reopened.join("|") === foldersBefore.join("|"),
       "every folder survived the reopen: " + reopened.join(", "));
assert(reopened.indexOf("Vehicles") >= 0, "an EMPTY folder survived the reopen");

var found = scene.find(cubeName);
assert(found !== null, "the cube is still there after the reopen");
assert(node.folder(found) === "Lighting/Practicals",
       "its FOLDER survived the reopen: '" + node.folder(found) + "'");
assert(node.info(found).parent === scene.root(),
       "and it is still parented to the root — folders never touched the hierarchy");

console.log("e2e_folders: ALL OK");
