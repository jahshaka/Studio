// scripting.e2e.shader_thumbnail — shader (graph) assets get REAL thumbnails
// (VISUAL_PARITY_SPEC item 5). Before this, materials.create + graph.save
// stored an empty blob and assets.refreshThumbnail refused Shader rows outright
// ("only object and material assets are supported"), so every graph tile in the
// Assets page fell back to a generic file icon.
//
// The chain under test, through the real binary with the engine up:
//   materials.create -> a Vector4 wired into the master's Base Color
//   -> graph.save (serialize + bake) -> assets.refreshThumbnail (renders the
//   evaluated PbrMaterial on the preview sphere) -> assets.thumbnail reads the
//   stored PNG back and its centre pixel is the graph's colour, not the
//   view background and not a grey stand-in.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

project.create("shader_thumbnail");

var shaderGuid = materials.create("Lane C Red", { graph: true });
assert(shaderGuid && shaderGuid.length > 10, "materials.create -> " + shaderGuid);

// A fresh graph is a bare PbrMaterial master; give it a colour a pixel can prove.
var master = null;
var nodes = graph.nodes();
for (var i = 0; i < nodes.length; i++) if (nodes[i].master) master = nodes[i].id;
assert(master !== null, "the new graph has a PbrMaterial master");

var colour = graph.addNode("vector4");
assert(colour && colour.length > 0, "vector4 constant added");
assert(graph.setValue(colour, { x: 0.85, y: 0.08, z: 0.08, w: 1.0 }) === true, "colour set to red");
assert(graph.connect(colour, 0, master, "Base Color") === true, "wired into Base Color");

var evaluated = graph.evaluate();
assert(evaluated.values.baseColor.r > 0.5, "the graph folds to a red baseColor");

assert(graph.save() === true, "graph.save writes the definition back to the asset");

// THE MINT ALREADY RENDERS ONE (MATPREVIEW-ENV-1, owner review R9(a)): every
// route that makes a material asks for the studio sphere, so a fresh graph
// starts with a tile of the material it currently IS — a plain sphere, not the
// red one it becomes below. (It used to start EMPTY, which is what the Assets
// page papered over with a file icon.) What this suite proves is the rest of
// the chain: the refresh re-renders the SAVED definition, and the picture
// changes to the graph's colour.
var before = assets.thumbnail(shaderGuid);
assert(before.empty === false, "a freshly created graph already carries a tile (R9(a))");
assert(!(before.centre.r > before.centre.g + 40),
       "…and it is the plain material, not red: " + JSON.stringify(before.centre));

assert(assets.refreshThumbnail(shaderGuid).ok === true, "assets.refreshThumbnail accepts a shader asset");

var after = assets.thumbnail(shaderGuid);
console.log("stored thumbnail: " + JSON.stringify(after));
assert(after.empty === false, "the stored blob is no longer empty");
assert(after.bytes > 1000, "the blob is a real PNG (" + after.bytes + " bytes)");
assert(after.width === 512 && after.height === 512, "stored at the render size");
assert(after.centre.r > after.centre.g + 40 && after.centre.r > after.centre.b + 40,
       "the thumbnail shows the graph's colour, not a grey stand-in: "
       + JSON.stringify(after.centre));

console.log("e2e_shader_thumbnail: ALL OK");
