// Re-writes the six shipped sample archives in SCENE FORMAT V2
// (SPECS/SCENEGRAPH_SPEC.md §3 step 4; src/io/sceneformat.h).
//
// Run it, do not hand-edit the archives:
//
//   TREE=<absolute path to the source tree>
//   sed "s|@TREE@|$TREE|" $TREE/scenes/tools/resave_samples_v2.js > /tmp/resave.js
//   cd <a scratch dir>
//   HOME=<a scratch home> DISPLAY=<your Xvfb> \
//       <build>/bin/Jahshaka --script /tmp/resave.js
//
// (@TREE@ is substituted rather than derived: a --script run has no argv and no
// notion of where the tree is, and hardcoding a path in a committed tool is how
// it silently rewrites somebody else's archives.)
//
// For each sample it imports the shipped archive into a fresh library, opens
// it, saves it (which is what rewrites the scene blob through the v2 writer)
// and re-exports over the original .zip. Everything else in the archive — the
// catalog rows, the content-addressed objects, the manifest — is reproduced by
// the exporter from the same catalog it just imported, so the only intended
// difference is the scene blob's format.
//
// IDEMPOTENT: running it on already-v2 archives rewrites them to the same
// shape (the v2 reader and the v2 writer are a round trip; the JSON key order
// is QJsonObject's, i.e. sorted, so even the bytes settle).
//
// WHY A RE-SAVE AND NOT A CONVERTER SCRIPT (the convert_legacy_materials.py
// route): the difference between v1 and v2 is not a key rename that a Python
// pass can do — it is what the READER derives (the SCENE_STATIC classification
// is re-applied, the rotation is re-normalised from euler to the quaternion the
// node actually holds, the root node's identity is adopted). The app's own
// reader and writer are the only two things that agree on all of it.

var SAMPLES = ["Matcaps", "Particles", "Physics", "Showroom",
               "Skeletal Animation", "World Background"];

// The tree, passed in as an argument or derived from the script's own path.
var TREE = "@TREE@";

function log(m) { console.log("[resave] " + m); }
function fail(m) { throw new Error("resave: " + m); }

for (var i = 0; i < SAMPLES.length; i++) {
    var name = SAMPLES[i];
    var zip = TREE + "/scenes/" + name + ".zip";
    log(name + ": importing " + zip);

    var imported = project.importArchive(zip);
    if (!imported || !imported.guid) fail(name + ": importArchive failed");
    log(name + ": imported " + imported.guid +
        " (" + imported.assets + " assets, " + imported.objects + " objects)");

    if (project.open(imported.guid) !== true) fail(name + ": open failed");
    var nodes = scene.nodes().length;
    log(name + ": opened, " + nodes + " nodes");
    if (nodes < 2) fail(name + ": opened with " + nodes + " nodes — that is not the sample");

    // THE POINT: a save runs SceneWriter, which now emits v2.
    if (project.save() !== true) fail(name + ": save failed");

    var out = project.exportArchive(zip);
    if (!out || !out.path) fail(name + ": exportArchive failed");
    log(name + ": wrote " + out.path +
        " (" + out.assets + " assets, " + out.objects + " objects)");

    if (project.close() !== true) fail(name + ": close failed");
}

log("ALL SIX SAMPLES REWRITTEN IN FORMAT V2");
