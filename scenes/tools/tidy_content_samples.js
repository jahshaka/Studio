// Drops the catalog rows the four CONTENT samples carry but their scenes never
// use (lane L13, 2026-09-12), and re-exports each archive over itself.
//
// Run it, do not hand-edit the archives:
//
//   TREE=<absolute path to the source tree>
//   sed "s|@TREE@|$TREE|" $TREE/scenes/tools/prune_sample_rows.js > /tmp/prune.js
//   cd <a scratch dir>
//   HOME=<a scratch home> JAHSHAKA_DATA_ROOT=<scratch>/data DISPLAY=<your Xvfb> \
//       <build>/bin/Jahshaka --script /tmp/prune.js
//
// (@TREE@ is substituted rather than derived — see resave_samples_v2.js for why
// a committed tool never hardcodes a tree path.)
//
// WHY. The editor's asset tray shows every asset the open project's scene uses,
// once (owner, 2026-09-12; services/assettray.h). Until then it hid every row
// something depended on, which happened to hide most of the junk these
// archives had accumulated over a decade of re-saves — a matcap the dragon no
// longer wears, a bump map and a particle image nothing references, two
// legacy particle-system rows from the pre-PFX2 sample, six sky faces the
// World Background sky replaced, glass shaders from the retired GLSL pipeline,
// a primitive row whose node was deleted. With the rule honest, every one of
// them would be a tile. scripting.e2e.tray_panel asserts each sample's tray is
// exactly its scene's asset closure; this is the data half of that.
//
// SAFE BY CONSTRUCTION: a row is dropped only if it is named below AND its guid
// appears nowhere in the scene document (every top-level node serialized, plus
// world.get()) — a named row the scene does use stops the tool. It is a CATALOG
// edit only (assets.remove {force: true}: the row, its pins, its edges): the
// scene is not saved, so the document goes back out as it came in. ENGINE-UP,
// because the export measures the manifest's scene-scale block from the live
// scene (stamp_sample_scale.js).
//
// IDEMPOTENT: a second run finds none of the rows, rebuilds the same
// thumbnails and re-exports unchanged content.

var TREE = "@TREE@";

var PRUNE = {
    "Matcaps": [["texture", "mc16.jpg"]],
    "Particles": [["texture", "fire_pit_bump.png"], ["texture", "Glowing Particle.jpg"],
                  ["particles", "Particle System"], ["particles", "Particle System"]],
    "Physics": [["object", "Cube"]],
    "World Background": [["texture", "Tile.png"], ["texture", "back.png"], ["texture", "bottom.png"],
                         ["texture", "front.png"], ["texture", "left.png"], ["texture", "right.png"],
                         ["texture", "top.png"], ["texture", "mc15.jpg"], ["texture", "mc16.jpg"],
                         ["texture", "SpecularMap.png"], ["shader", "Real Glass.shader"],
                         ["shader", "Refraction.shader"], ["file", "refraction.vert"],
                         ["file", "refraction.frag"]]
};

// Rows the scene DOES use that shipped without a thumbnail (the tray showed the
// generic "empty object" icon for them): the thumbnail is rebuilt from the
// stored image, which is document-only (assets.refreshThumbnail).
var RETHUMB = {
    "World Background": ["hamarikyu_front.png", "hamarikyu_back.png", "hamarikyu_left.png",
                         "hamarikyu_right.png", "hamarikyu_top.png", "hamarikyu_bottom.png"]
};

function log(m) { console.log("[prune] " + m); }
function fail(m) { throw new Error("prune: " + m); }

// Every string the scene document and the world carry — a row whose guid is
// in here is USED and is never dropped.
function documentText() {
    var root = scene.root();
    var parts = [JSON.stringify(world.get())];
    var rows = scene.nodes({ depth: 1 });
    for (var r = 0; r < rows.length; r++) {
        if (rows[r].id === root) continue;
        parts.push(JSON.stringify(node.serialize(rows[r].id)));
    }
    return parts.join("\n");
}

for (var name in PRUNE) {
    var zip = TREE + "/scenes/" + name + ".zip";
    var imported = project.importArchive(zip);
    if (!imported || !imported.guid) fail(name + ": importArchive failed");
    if (project.open(imported.guid) !== true) fail(name + ": open failed");
    var text = documentText();

    var rows = assets.list({ scope: "project" });
    var dropped = 0;
    PRUNE[name].forEach(function (want) {
        var hit = null;
        for (var i = 0; i < rows.length; i++) {
            if (rows[i].type === want[0] && rows[i].name === want[1] && !rows[i].taken) { hit = rows[i]; break; }
        }
        if (!hit) { log(name + ": no " + want[0] + " '" + want[1] + "' (already pruned)"); return; }
        hit.taken = true;
        if (text.indexOf(hit.guid) >= 0)
            fail(name + ": " + want[0] + " '" + want[1] + "' (" + hit.guid + ") IS USED by the scene — not dropping it");
        if (assets.remove(hit.guid, { force: true }) !== true)
            fail(name + ": could not drop " + want[1] + ": " + app.lastError());
        log(name + ": dropped " + want[0] + " '" + want[1] + "'");
        dropped++;
    });

    (RETHUMB[name] || []).forEach(function (want) {
        var hits = assets.list({ scope: "project", type: "texture" })
            .filter(function (r) { return r.name === want; });
        if (hits.length !== 1) fail(name + ": expected one texture '" + want + "', found " + hits.length);
        if (assets.refreshThumbnail(hits[0].guid) !== true)
            fail(name + ": could not rebuild the thumbnail of " + want + ": " + app.lastError());
        log(name + ": rebuilt the thumbnail of '" + want + "'");
    });

    var out = project.exportArchive(zip);
    if (!out || !out.path) fail(name + ": exportArchive failed");
    log(name + ": dropped " + dropped + " row(s); wrote " + out.path + " (" + out.assets +
        " assets, " + out.objects + " objects)");
    if (project.close() !== true) fail(name + ": close failed");
}

log("done");
