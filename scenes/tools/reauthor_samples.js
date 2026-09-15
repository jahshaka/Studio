// BRINGS ONE SHIPPED SAMPLE UP TO DATE with the three changes that landed
// after its archive was last written, and re-shoots its preview — one
// re-authoring pass, so the eight archives are regenerated once.
//
// Run it, do not hand-edit the archives. ONE SAMPLE PER RUN, ON A VIRGIN HOME
// (the reason is under "the checker" below):
//
//   TREE=<absolute path to the source tree>
//   for S in Matcaps "Mirror Room" Particles Physics Showroom "Showroom 2" \
//            "Skeletal Animation" "World Background"; do
//       sed -e "s|@TREE@|$TREE|" -e "s|@SAMPLE@|$S|" -e "s|@MODE@|apply|" \
//           -e "s|@OUT@|<a scratch dir>|" $TREE/scenes/tools/reauthor_samples.js > /tmp/re.js
//       rm -rf <a virgin home>; mkdir -p <a virgin home>/run
//       ( cd <a virgin home>/run && HOME=<a virgin home> DISPLAY=<your Xvfb, 1920x1080> \
//             <build>/bin/Jahshaka --script /tmp/re.js --data-root <a virgin home>/data )
//   done
//
// @MODE@ = "measure" reads and photographs everything and writes NOTHING — no
// material edit, no save, no preview, no archive. It is how the before/after
// tables in spikes/content-1/INDEX.md were taken.
//
// NOT --headless: the GI fit is a RENDERER measurement and the previews are
// real frames.
//
// ===========================================================================
// 1. THE LIT VOLUME — re-measured, not re-authored
// ===========================================================================
// Nothing in the document says how big a room is any more (BOUNDS-CRUD): the
// renderer measures the lit volume from the content the ground supports, and
// ENGINE-4 item 5 changed that measurement after the UNPIN-1 previews and
// giStatus tables were taken. So every sample's preview is re-shot on today's
// fit and its volume recorded. scenes/tools/unpin_sample_gi.js's measure mode
// reads the same numbers for the three rooms; this tool reads them for all
// eight.
//
// ===========================================================================
// 2. IMPORTED COLOURS — re-encoded, and ONLY the imported ones
// ===========================================================================
// Since LIGHTS-2 a QColor in the document is sRGB and is DECODED on its way to
// the renderer, and the importer ENCODES the linear factors glTF and assimp
// hand it (iris::srgbOf). The samples' materials, however, live in their scene
// JSON and are not re-derived on load: a colour the OLD importer wrote — a raw
// linear factor pushed straight into a QColor — now decodes a second time and
// renders a gamma too dark. Skeletal Animation's character is the visible one.
//
// WHICH COLOURS THOSE ARE IS MEASURED, NEVER GUESSED. For every mesh node whose
// mesh is a library MODEL ASSET (a built-in primitive's `:/models/...` mesh can
// never have come from an importer), this tool exports that asset's own source
// file and RE-IMPORTS IT with today's importer in a scratch project. That gives
// the colour today's pipeline produces for that exact file and mesh index; the
// value the OLD pipeline would have produced for the same file is its linear
// decode. A stored colour that equals that old value (within 2/255 of the
// re-quantisation) IS what the importer put there and is replaced by today's;
// anything else is the author's and is left alone.
//
// Measured on the eight, 2026-09-13: it matches Skeletal Animation's three
// character materials (#5ebfe7 -> #a4e1f5 twice, #0b0b0b -> #3d3d3d) and
// NOTHING else — the Matcaps dragon's #ffd700, the World Background dragons'
// #f5f5f7 / #ffd700 / #eef4f8, the Physics pipes and ball, the Particles fire
// pit's #cccccc are all values a person chose (their files import as #cbcbcb,
// assimp's default grey, whose old spelling is #989898). The fire pit is the
// one that shows why the rule has to be the provenance and not a resemblance:
// #cccccc sits ONE step from srgbOf's answer for that file and is still not it.
//
// ===========================================================================
// 3. THE CHECKER IS THE PLATFORM'S, NOT THE USER'S
// ===========================================================================
// BAR-1 marked the shipped checker's library row `{"type": "platform"}` so the
// editor's asset tray drops it (services/assettray.h rule 4): it is furniture
// the app pins behind the user's back, not something they added. It stamped the
// rows a FLOOR RESOLVES TO at creation, so the sample archives — written before
// it — still carry an unstamped Tile.png row and an opened sample showed the
// checker in its tray.
//
// The stamp is not applied by hand here. Creating a project runs the default
// floor, which pins the shipped tile through
// ShippedAssets::pinTexture(Ownership::Platform), and that call resolves the
// row BY CONTENT — so on a library whose only copy of those bytes is the one
// this sample's archive just brought in, it stamps the sample's own row, which
// the export then carries. THAT is why this tool runs one sample per virgin
// HOME: with two samples in one library the content lookup answers one row for
// both and only one archive would be stamped.
//
// ===========================================================================
// WHAT IS NOT TOUCHED: the camera, the lights, the sky, the exposure, the probe
// grid, the tier, the geometry, the hand-authored colours, and each sample's
// own preview SIZE and GRADE (the five that ship a "scene"-graded preview keep
// it; the three rooms keep the viewport grade they were photographed with —
// re-developing a picture is not this pass's business).
//
// IDEMPOTENT: a second run finds the colours already encoded, the row already
// stamped, measures the same fit and re-exports the same archive.

var TREE   = "@TREE@";
var SAMPLE = "@SAMPLE@";
var MODE   = "@MODE@";
var OUT    = "@OUT@";

// name -> the preview it ships, at the size and grade it ships at, and the
// warm-up its content needs before the shutter (a particle sample photographs
// as an empty scene until the plumes have filled).
var PREVIEWS = {
    "Matcaps":            { file: "matcaps.png",   w: 460,  h: 215,  warm: 120, grade: "scene" },
    "Particles":          { file: "particles.png", w: 480,  h: 270,  warm: 240, grade: "scene" },
    "Physics":            { file: "physics.png",   w: 1920, h: 1080, warm: 120, grade: "scene" },
    "Skeletal Animation": { file: "skeletal.png",  w: 1280, h: 720,  warm: 120, grade: "scene" },
    "World Background":   { file: "world.png",     w: 1920, h: 1080, warm: 120, grade: "scene" },
    "Mirror Room":        { file: "mirrorroom.png", w: 1280, h: 720, warm: 120, grade: true },
    "Showroom":           { file: "showroom.png",  w: 1280, h: 720,  warm: 120, grade: true },
    "Showroom 2":         { file: "showroom2.png", w: 1280, h: 720,  warm: 120, grade: true }
};

// Centre plus the four quarters, at the sample's own saved camera.
var PROBES = [{ x: 0.5, y: 0.5 }, { x: 0.25, y: 0.25 }, { x: 0.75, y: 0.25 },
              { x: 0.25, y: 0.75 }, { x: 0.75, y: 0.75 }];

// The material colour slots a file format can carry (assimp's COLOR_*, glTF's
// factors). Roughness and metallic are scalars and were never colour-decoded.
var SLOTS = ["baseColor", "specularColor", "fresnelColor", "emissiveColor"];

var ZIP = TREE + "/scenes/" + SAMPLE + ".zip";
var P   = PREVIEWS[SAMPLE];
var SLUG = SAMPLE.replace(/ /g, "_");

function log(m) { console.log("[reauthor] " + SAMPLE + ": " + m); }
function fail(m) { throw new Error("reauthor: " + SAMPLE + ": " + m); }
function J(x) { return JSON.stringify(x); }
function r2(v) { return Math.round(v * 100) / 100; }
function vec(v) { return "(" + r2(v.x) + ", " + r2(v.y) + ", " + r2(v.z) + ")"; }
function size(a, b) { return "(" + r2(b.x - a.x) + " x " + r2(b.y - a.y) + " x " + r2(b.z - a.z) + ")"; }

// ---- colour arithmetic, mirroring irisgl/core/color.h ----------------------
function linearOfChannel(c) {                    // sRGB 0..1 -> linear 0..1
    if (c <= 0) return 0;
    if (c >= 1) return 1;
    return c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4);
}
function hexToRgb(hex) {
    var h = ("" + hex).replace("#", "");
    return [parseInt(h.substr(0, 2), 16), parseInt(h.substr(2, 2), 16), parseInt(h.substr(4, 2), 16)];
}
// The 8-bit colour the PRE-srgbOf importer would have written for the file that
// today imports as `hex`: today's value is the sRGB encoding of the file's
// linear factor, so the old one is its decode.
function oldSpellingOf(hex) {
    var c = hexToRgb(hex), out = [];
    for (var i = 0; i < 3; i++) out.push(Math.round(linearOfChannel(c[i] / 255) * 255));
    return out;
}
function within(a, b, tol) {
    for (var i = 0; i < 3; i++) if (Math.abs(a[i] - b[i]) > tol) return false;
    return true;
}

function giLine(tag) {
    var st = world.giStatus();
    log(tag + " GI: bounds " + vec(st.boundsMin) + " .. " + vec(st.boundsMax) +
        " size " + size(st.boundsMin, st.boundsMax) + " voxelMetres " + r2(st.voxelMetres) +
        " | probes " + st.probeCount + " region " + vec(st.probeRegionMin) + " .. " +
        vec(st.probeRegionMax) + " dropped " + st.probesDropped +
        " clamped " + st.probesClampedToRegion + " mode " + st.mode);
    return st;
}

// The sample's own picture: no selection, no light wires, Game View — the frame
// its preview ships, which is also where the pixels are read.
function compose(warm) {
    editor.select(null);
    editor.setOverlays({ lightWires: false });
    editor.gameView(true);
    editor.frame(warm, 1.0 / 60.0);
}

function shoot(tag) {
    var shot = editor.screenshot(OUT + "/" + SLUG + "-" + tag + ".png", 1280, 720, PROBES, "scene");
    var line = "", sum = 0;
    for (var i = 0; i < shot.probes.length; i++) {
        var p = shot.probes[i];
        line += (i ? "  " : "") + Math.round(p.r) + "," + Math.round(p.g) + "," + Math.round(p.b);
        sum += p.r + p.g + p.b;
    }
    log("probes [" + tag + "] scene grade: " + line + "   mean " + r2(sum / 15));
    return shot.probes;
}

// Every mesh node's material, with the library model asset it came from (empty
// for a built-in primitive).
function inventory() {
    var out = [], nodes = scene.nodes();
    for (var i = 0; i < nodes.length; i++) {
        if (nodes[i].type !== "mesh") continue;
        var id = nodes[i].id;
        var mesh = "" + node.property(id, "meshPath");
        var rec = { id: id, name: nodes[i].name, mesh: mesh,
                    index: node.property(id, "meshIndex"), colours: {} };
        var m = material.get(id);
        for (var s = 0; s < SLOTS.length; s++) rec.colours[SLOTS[s]] = m[SLOTS[s]];
        out.push(rec);
    }
    return out;
}

// ---------------------------------------------------------------------------
// 1. the sample as it ships
// ---------------------------------------------------------------------------
var imported = project.importArchive(ZIP);
if (!imported || !imported.guid) fail("importArchive failed");
if (project.open(imported.guid) !== true) fail("open failed");
var nodeCount = scene.nodes().length;
if (nodeCount < 2) fail("opened with " + nodeCount + " nodes — that is not the sample");
log(nodeCount + " nodes, camera " + J(editor.camera().position) + " fov " + editor.camera().fov);

compose(P.warm);
giLine("AS SHIPPED");
shoot("before");

var stored = inventory();
// One export per model asset the scene actually uses.
var sources = {}, order = [];
for (var i = 0; i < stored.length; i++) {
    var mesh = stored[i].mesh;
    if (!mesh || mesh.indexOf(":/") === 0) continue;   // a built-in primitive
    if (sources[mesh]) continue;
    var dir = OUT + "/raw/" + SLUG + "/" + order.length;
    var raw = assets.exportRaw(mesh, dir, { dependencies: false, hash: false });
    var file = "";
    for (var f = 0; f < raw.files.length; f++)
        if (!/\.(png|jpg|jpeg|tga|bmp|dds)$/i.test("" + raw.files[f])) file = dir + "/" + raw.files[f];
    sources[mesh] = file;
    order.push(mesh);
    log("model asset " + mesh + " -> " + J(raw.files));
}
editor.gameView(false);
if (project.close() !== true) fail("close failed");

// ---------------------------------------------------------------------------
// 2. the scratch project: the platform stamp, and today's importer
// ---------------------------------------------------------------------------
// Creating it builds a default floor, and THAT is what stamps the sample's
// checker row `{"type": "platform"}` — see the header, section 3.
project.create("reauthor scratch");
var freshOf = {};                                   // mesh asset -> index -> colours
for (var k = 0; k < order.length; k++) {
    var file = sources[order[k]];
    if (!file) { log("no model file for " + order[k] + " — left alone"); continue; }
    assets.importAndPlace(file, { position: { x: 0, y: 0, z: 0 } });
    var table = {};
    var ns = scene.nodes();
    for (var q = 0; q < ns.length; q++) {
        if (ns[q].type !== "mesh") continue;
        var qmesh = "" + node.property(ns[q].id, "meshPath");
        if (!qmesh || qmesh.indexOf(":/") === 0) continue;
        var qm = material.get(ns[q].id), rec = {};
        for (var s2 = 0; s2 < SLOTS.length; s2++) rec[SLOTS[s2]] = qm[SLOTS[s2]];
        table[node.property(ns[q].id, "meshIndex")] = rec;
    }
    freshOf[order[k]] = table;
    // clear the scratch scene before the next file
    var all = scene.nodes();
    for (var d = 0; d < all.length; d++)
        if (!all[d].parent) { try { node.remove(all[d].id); } catch (e) {} }
}
if (project.close() !== true) fail("scratch close failed");

// ---------------------------------------------------------------------------
// 3. the re-authoring
// ---------------------------------------------------------------------------
if (project.open(imported.guid) !== true) fail("re-open failed");
var live = inventory(), edits = 0, kept = 0;
for (var i2 = 0; i2 < live.length; i2++) {
    var rec = live[i2];
    var table = freshOf[rec.mesh];
    if (!table) continue;                            // built-in primitive, or no source file
    var fresh = table[rec.index];
    if (!fresh) { log(rec.name + ": today's import has no mesh " + rec.index + " — left alone"); continue; }
    var write = {};
    for (var s3 = 0; s3 < SLOTS.length; s3++) {
        var key = SLOTS[s3];
        var have = rec.colours[key], now = fresh[key];
        if (have === now) continue;                  // already today's spelling
        var old = oldSpellingOf(now);
        if (within(hexToRgb(have), old, 2)) {
            write[key] = now;
            log(rec.name + " [mesh " + rec.index + "] " + key + " " + have + " -> " + now +
                " (the importer's, whose old spelling is #" +
                old.map(function (v) { return ("0" + v.toString(16)).slice(-2); }).join("") + ")");
        } else {
            ++kept;
            log(rec.name + " [mesh " + rec.index + "] " + key + " " + have +
                " KEPT — authored (today's import of this file says " + now +
                ", whose old spelling is #" +
                old.map(function (v) { return ("0" + v.toString(16)).slice(-2); }).join("") + ")");
        }
    }
    var keys = Object.keys(write);
    if (keys.length) {
        // Written in BOTH modes: measure mode never saves, exports or re-shoots
        // a preview, but the "after" pixels it reports have to be the pixels
        // the edit produces.
        if (material.set(rec.id, write) !== true) fail(rec.name + ": material.set refused " + J(write));
        edits += keys.length;
    }
}
log(edits + " importer-sourced colour(s) re-encoded, " + kept + " authored colour(s) left alone");

compose(P.warm);
giLine("RE-AUTHORED");
shoot("after");

if (MODE !== "apply") {
    log("MEASURE ONLY — nothing written");
    editor.gameView(false);
    if (project.close() !== true) fail("close failed");
} else {
    if (project.save() !== true) fail("save failed");

    // ---- the shipped preview ----------------------------------------------
    // Game View (already on), the SAVED camera, the size and the grade this
    // sample's preview ships at.
    editor.frame(60, 1.0 / 60.0);
    var shot = editor.screenshot(TREE + "/scenes/preview/" + P.file, P.w, P.h,
                                 [{ x: 0.5, y: 0.5 }], P.grade);
    log("preview " + P.w + "x" + P.h + " centre " + J(shot.center));
    editor.gameView(false);

    // ---- the archive -------------------------------------------------------
    var out = project.exportArchive(ZIP);
    if (!out || !out.path) fail("exportArchive failed");
    log("wrote " + out.path + " (" + out.assets + " assets, " + out.objects + " objects)");
    if (project.close() !== true) fail("close failed");

    // ---- and it survived the round trip ------------------------------------
    var back = project.importArchive(ZIP);
    if (!back || !back.guid) fail("re-import failed");
    if (project.open(back.guid) !== true) fail("re-open (after export) failed");
    compose(P.warm);
    giLine("REOPENED");
    var again = inventory();
    for (var i3 = 0; i3 < again.length; i3++) {
        var was = null;
        for (var j = 0; j < live.length; j++) if (live[j].name === again[i3].name) was = live[j];
        if (!was) continue;
        var table2 = freshOf[again[i3].mesh];
        if (!table2 || !table2[again[i3].index]) continue;
        for (var s4 = 0; s4 < SLOTS.length; s4++) {
            var key2 = SLOTS[s4], want = table2[again[i3].index][key2];
            var had = was.colours[key2];
            var expect = within(hexToRgb(had), oldSpellingOf(want), 2) ? want : had;
            if (again[i3].colours[key2] !== expect)
                fail(again[i3].name + ": " + key2 + " reopened as " + again[i3].colours[key2] +
                     ", expected " + expect);
        }
    }
    log("colours survived the round trip");

    // THE TRAY: the checker is the platform's furniture, so an opened sample
    // shows every asset it uses EXCEPT that row — while the floor still wears
    // it (the pin and the bytes are untouched; only the row is marked).
    var tray = assets.list({ scope: "project", tray: true });
    for (var t = 0; t < tray.length; t++)
        if (("" + tray[t].name).toLowerCase() === "tile.png")
            fail("the shipped checker is still a tray tile after the re-export");
    var ground = scene.find("Ground");
    if (ground) {
        var map = "" + material.get(ground).baseColorMap;
        if (!map.length) fail("the ground lost its checker");
        log("tray " + tray.length + " tile(s), no checker; the ground still wears " + map);
    }
    if (project.close() !== true) fail("close (after re-import) failed");
}

log("done (" + MODE + ")");
