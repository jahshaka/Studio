// Tidies the five CONTENT samples (the ones no make_*.js can regenerate) in
// place: prunes the catalog rows their scenes never use, rebuilds missing
// thumbnails, and marks each scene's ground as its DEFAULT FLOOR — then
// re-exports each archive over itself (lane L13, 2026-09-12).
//
// Run it, do not hand-edit the archives:
//
//   TREE=<absolute path to the source tree>
//   sed "s|@TREE@|$TREE|" $TREE/scenes/tools/tidy_content_samples.js > /tmp/tidy.js
//   cd <a scratch dir>
//   HOME=<a scratch home> JAHSHAKA_DATA_ROOT=<scratch>/data DISPLAY=<your Xvfb> \
//       <build>/bin/Jahshaka --script /tmp/tidy.js
//
// (@TREE@ is substituted rather than derived — see resave_samples_v2.js for why
// a committed tool never hardcodes a tree path.)
//
// WHY THE PRUNE. The editor's asset tray shows every asset the open project's
// scene uses, once (owner, 2026-09-12; services/assettray.h). Until then it hid
// every row something depended on, which happened to hide most of the junk
// these archives had accumulated over a decade of re-saves — a matcap the
// dragon no longer wears, a bump map and a particle image nothing references,
// two legacy particle-system rows from the pre-PFX2 sample, six sky faces the
// World Background sky replaced, glass shaders from the retired GLSL pipeline,
// a primitive row whose node was deleted. With the rule honest, every one of
// them would be a tile. scripting.e2e.tray_panel asserts each sample's tray is
// exactly its scene's asset closure; this is the data half of that.
//
// SAFE BY CONSTRUCTION: a row is dropped only if it is named below AND its guid
// appears nowhere in the scene document (every top-level node serialized, plus
// world.get()) — a named row the scene does use stops the tool. The drop is a
// CATALOG edit (assets.remove {force: true}: the row, its pins, its edges).
//
// WHY THE FLOOR. "ALL the demo scenes should have that default floor" (owner,
// 2026-09-12): each of these scenes already stands on the default scene's
// ground plane, but written before the document could SAY so — the
// `defaultFloor` flag (services/defaultfloor.h) is what material.reset and the
// material panel's "Reset to Default Floor" look for. The tool finds the one
// ground-plane mesh, refuses a scene with two floors, sets the flag and checks
// the floor wears the checker (the shipped tile) — except where the demo's own
// design is a different floor, listed in FLOOR_DESIGN with the reason. Setting
// the flag is a document edit, so a marked scene is SAVED before the export.
//
// ENGINE-UP, because the export measures the manifest's scene-scale block from
// the live scene (stamp_sample_scale.js). IDEMPOTENT: a second run finds no
// rows to drop and floors already marked, rebuilds the same thumbnails and
// re-exports unchanged content.

var TREE = "@TREE@";

var PRUNE = {
    "Matcaps": [["texture", "mc16.jpg"]],
    "Particles": [["texture", "fire_pit_bump.png"], ["texture", "Glowing Particle.jpg"],
                  ["particles", "Particle System"], ["particles", "Particle System"]],
    "Physics": [["object", "Cube"]],
    "Skeletal Animation": [],
    "World Background": [["texture", "Tile.png"], ["texture", "back.png"], ["texture", "bottom.png"],
                         ["texture", "front.png"], ["texture", "left.png"], ["texture", "right.png"],
                         ["texture", "top.png"], ["texture", "mc15.jpg"], ["texture", "mc16.jpg"],
                         ["texture", "SpecularMap.png"], ["shader", "Real Glass.shader"],
                         ["shader", "Refraction.shader"], ["file", "refraction.vert"],
                         ["file", "refraction.frag"]]
};

// A demo whose floor is part of its design keeps its own floor material (the
// node is still the default floor; "Reset to Default Floor" gives the checker).
var FLOOR_DESIGN = {
    "World Background": "the metal deck under the cube sky (a tiled metal plate with a " +
                        "normal map) is the demo's design"
};

// Rows the scene DOES use that shipped without a thumbnail (the tray showed the
// generic "empty object" icon for them): the thumbnail is rebuilt from the
// stored image, which is document-only (assets.refreshThumbnail).
var RETHUMB = {
    "World Background": ["hamarikyu_front.png", "hamarikyu_back.png", "hamarikyu_left.png",
                         "hamarikyu_right.png", "hamarikyu_top.png", "hamarikyu_bottom.png"]
};

function log(m) { console.log("[tidy] " + m); }
function fail(m) { throw new Error("tidy: " + m); }

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

    // ---- ONE DEFAULT FLOOR ----------------------------------------------------
    var grounds = scene.nodes().filter(function (r) {
        return r.type === "mesh" && node.property(r.id, "meshPath") === ":/models/ground.obj";
    });
    if (grounds.length !== 1)
        fail(name + ": expected ONE ground-plane floor, found " + grounds.length);
    var floor = grounds[0].id;
    var dirty = false;
    if (node.property(floor, "defaultFloor") !== true) {
        if (node.setProperty(floor, "defaultFloor", true) !== true)
            fail(name + ": could not mark the floor: " + app.lastError());
        dirty = true;
        log(name + ": marked '" + grounds[0].name + "' as the default floor");
    }
    var fm = material.get(floor);
    if (FLOOR_DESIGN[name]) {
        log(name + ": keeps its own floor material — " + FLOOR_DESIGN[name]);
    } else if (!(("" + fm.baseColorMap).length > 0 && Math.abs(fm.textureScale - 4) < 1e-4)) {
        fail(name + ": the floor does not wear the checker: " + JSON.stringify(fm));
    }
    if (dirty && project.save() !== true) fail(name + ": save failed");

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
