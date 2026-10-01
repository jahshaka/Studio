// RE-AUTHORS ONE SHIPPED SAMPLE ON TODAY'S BUILD (SAMPLES-1, 2026-10-01; the
// owner's forward-only rule: "samples are re-authored when a build finishes").
// Run it, never hand-edit the archives. ONE SAMPLE PER RUN, EACH STEP ON A
// VIRGIN HOME (a library holding a second copy of an archive's bytes would
// answer the content lookups below for both). THREE STEPS per sample:
//
//   TREE=<absolute path to the source tree>   OUT=<a scratch dir>
//   run() {   # $1 = mode, $2 = the archive the run imports
//       sed -e "s|@TREE@|$TREE|" -e "s|@SAMPLE@|$S|" -e "s|@MODE@|$1|" \
//           -e "s|@ARCHIVE@|$2|" -e "s|@OUT@|$OUT|" $TREE/scenes/tools/reauthor_samples.js > $OUT/re.js
//       rm -rf $OUT/home; mkdir -p $OUT/home/run
//       ( cd $OUT/home/run && HOME=$OUT/home DISPLAY=<your Xvfb, 1920x1080> \
//             <build>/bin/Jahshaka --script $OUT/re.js --data-root $OUT/home/data )
//   }
//   for S in Matcaps "Mirror Room" Particles Physics Showroom "Showroom 2" \
//            "Skeletal Animation" "World Background"; do
//       run measure "$TREE/scenes/$S.zip" | tee "$OUT/$S.measure.log"         # 1
//       grep -o '\[import-colours\] .*' "$OUT/$S.measure.log" | cut -d' ' -f2- > "$OUT/$S.colours.json"
//       python3 $TREE/scenes/tools/samples_library_objects.py \
//           "$TREE/scenes/$S.zip" "$OUT/$S.colours.json" "$OUT/$S.converted.zip" # 2
//       run apply "$OUT/$S.converted.zip" | tee "$OUT/$S.apply.log"           # 3
//   done
//
// 1. MEASURE (the shipped archive, read-only): the "before" picture on today's
//    build, and today's importer's colour for every model file the scene uses
//    (each file exported from the store and re-imported in a scratch project) —
//    printed as one `[import-colours]` JSON line for step 2.
// 2. scenes/tools/samples_library_objects.py: the LIBRARY OBJECT blobs (the
//    asset tray's tiles) to today's form — euler rotations to quaternions, the
//    pre-PBR materials to PBR; its header states the mapping. Writes a scratch
//    copy; the app writes the shipped archive in step 3.
// 3. APPLY (the converted copy): THE FLOOR, THE HEIGHT FOG, the preview, the
//    archive, and the round trip — below.
//
// NOT --headless: the previews are real frames, and the drag-in check renders.
//
// ===========================================================================
// THE FLOOR — the Basic template's, never the old 100 m ground mesh
// ===========================================================================
// Every sample stood on `:/models/ground.obj`, a 100 m flat grid that stopped
// being a primitive in WORLD-MODEL-1 and was kept as a seed ONLY for these
// eight. Each one's ground is the unscaled grid — 100 m across, no larger — so
// every sample takes the BASIC floor (scene.addFloor({template: "basic"}): the
// locked 100 x 1 x 100 m cube, top face at y = 0, the default floor material),
// filed where the ground was (under the sample's "World" group). None takes
// the World grid: that is for a ground larger than 100 m, and none is.
//
// WHAT THE SAMPLE AUTHORED ON ITS GROUND IS CARRIED; what it inherited is not.
// The old ground's material is read against the OLD ground's default (below,
// OLD_GROUND) and only the differences move onto the new floor:
//   * the SURFACE LOBE — workflow, ior, specularColor, roughness, metallic —
//     moves as ONE group when any of it was edited (a glossy floor is the five
//     together: Showroom's 0.32 / 0.05 metallic-workflow floor only reflects
//     with its ior and specular, and the new floor's default is the
//     no-reflectance specular workflow). Mirror Room and Showroom 2 authored
//     exactly today's matte default, so carrying their group changes nothing.
//   * the CHECKER SIZE: a texture REPEAT is 16 m / textureScale on the old grid
//     (ground.obj's GRID-2 header) and 100 m / textureScale on the cube's face,
//     so a scale s keeps its size as s x 100 / 16 (the default 4 -> 25, the
//     template's own; Showroom 2's 2 -> 12.5).
//   * a picture other than the shipped tile, a tint, an added normal map
//     (World Background's metal deck plate and its normal map).
//   * the physics: a restitution or friction the sample set (Physics' 0.68
//     bounce), and a ground taken out of the simulation (Skeletal Animation's).
//
// ===========================================================================
// THE WORLD DEFAULTS each sample lacks (its sky and lights are its own)
// ===========================================================================
//   * HEIGHT FOG — the template's (iris::HeightFog's constructor dials, on) on
//     the four OPEN scenes under a procedural sky: Matcaps, Particles, Physics,
//     Skeletal Animation. The three ROOMS (Mirror Room, Showroom, Showroom 2)
//     are walled and roofed: nothing in them is beyond the fog's 100 m start.
//     WORLD BACKGROUND is open but its sky is a PHOTOGRAPH (a cubemap park):
//     the fog paints over the backdrop at the horizon (measured on the first
//     run: the park went to a flat blue-grey), and the photograph carries its
//     own atmosphere — its authored sky is kept, so it stays without fog.
//   * EXPOSURE — nothing to write: all eight are MANUAL at 0 stops, and zero
//     stops IS the re-derived default chain (SKY-DEFAULTS-1:
//     iris::lens::defaultExposureChain, 0.59604 -> 0.97882), which the new
//     previews are shot at. Logged per sample.
//
// IDEMPOTENT: on an archive already re-authored there is no ground to replace
// (the run says so and stops before writing) and step 2 converts nothing.

var TREE    = "@TREE@";
var SAMPLE  = "@SAMPLE@";
var MODE    = "@MODE@";          // "measure" | "apply"
var ARCHIVE = "@ARCHIVE@";       // the archive this run imports
var OUT     = "@OUT@";

// name -> the preview it ships, at the size and grade it ships at, and the
// warm-up its content needs before the shutter (a particle sample photographs
// as an empty scene until the plumes have filled).
var PREVIEWS = {
    "Matcaps":            { file: "matcaps.png",    w: 460,  h: 215,  warm: 120, grade: "scene" },
    "Particles":          { file: "particles.png",  w: 480,  h: 270,  warm: 240, grade: "scene" },
    "Physics":            { file: "physics.png",    w: 1920, h: 1080, warm: 120, grade: "scene" },
    "Skeletal Animation": { file: "skeletal.png",   w: 1280, h: 720,  warm: 120, grade: "scene" },
    "World Background":   { file: "world.png",      w: 1920, h: 1080, warm: 120, grade: "scene" },
    "Mirror Room":        { file: "mirrorroom.png", w: 1280, h: 720,  warm: 120, grade: true },
    "Showroom":           { file: "showroom.png",   w: 1280, h: 720,  warm: 120, grade: true },
    "Showroom 2":         { file: "showroom2.png",  w: 1280, h: 720,  warm: 120, grade: true }
};
var OPEN_SCENES = { "Matcaps": 1, "Particles": 1, "Physics": 1, "Skeletal Animation": 1 };

// The old ground's default material and body (what a scene was born with
// before WORLD-MODEL-1; Matcaps' ground is that default, unedited).
var OLD_GROUND = { baseColor: "#ffffff", workflow: 0, ior: 1.5, specularColor: "#ffffff", roughness: 1, metallic: 0,
                   textureScale: 4, restitution: 0.1, friction: 0.5 };
var LOBE = ["workflow", "ior", "specularColor", "roughness", "metallic"];
var OLD_REPEAT_M = 16;           // ground.obj: u = x / 16
var FLOOR_FACE_M = 100;          // the Basic floor's top face, UV 0..1

var PROBES = [{ x: 0.5, y: 0.5 }, { x: 0.25, y: 0.25 }, { x: 0.75, y: 0.25 },
              { x: 0.25, y: 0.75 }, { x: 0.75, y: 0.75 }];

var P = PREVIEWS[SAMPLE];
var SLUG = SAMPLE.replace(/ /g, "_");
var ZIP = TREE + "/scenes/" + SAMPLE + ".zip";

function log(m) { console.log("[reauthor] " + SAMPLE + ": " + m); }
function fail(m) { throw new Error("reauthor: " + SAMPLE + ": " + m); }
function J(x) { return JSON.stringify(x); }
function r3(v) { return Math.round(v * 1000) / 1000; }
function differs(a, b) {
    if (typeof a === "number" && typeof b === "number") return Math.abs(a - b) > 1e-4;
    return ("" + a).toLowerCase() !== ("" + b).toLowerCase();
}

function compose(warm) {
    editor.select(null);
    editor.setOverlays({ lightWires: false });
    editor.gameView(true);
    editor.frame(warm, 1.0 / 60.0);
}

function shoot(tag) {
    var shot = editor.screenshot(OUT + "/" + SLUG + "-" + tag + ".png", 1280, 720, PROBES, "scene");
    var line = [];
    for (var i = 0; i < shot.probes.length; i++) {
        var p = shot.probes[i];
        line.push(Math.round(p.r) + "," + Math.round(p.g) + "," + Math.round(p.b));
    }
    log("probes [" + tag + "] " + line.join("  "));
}

function groundsOf() {
    return scene.nodes().filter(function (r) {
        return r.type === "mesh" && node.property(r.id, "meshPath") === ":/models/ground.obj";
    });
}

function openArchive(path) {
    var imported = project.importArchive(path);
    if (!imported || !imported.guid) fail("importArchive failed: " + path);
    if (imported.bakeFailures && imported.bakeFailures.length)
        fail("the archive import could not bake " + J(imported.bakeFailures));
    app.openStats({ reset: true });
    if (project.open(imported.guid) !== true) fail("open failed");
    if (scene.nodes().length < 2) fail("opened with " + scene.nodes().length + " nodes");
    return imported;
}

// ===========================================================================
// 1. MEASURE
// ===========================================================================
if (MODE === "measure") {
    openArchive(ARCHIVE);
    compose(P.warm);
    shoot("before");

    // One export + re-import per model file the scene's meshes name (a
    // built-in's ":/..." seed key has no importer colour).
    var meshes = {}, order = [];
    scene.nodes().forEach(function (r) {
        if (r.type !== "mesh") return;
        var m = "" + node.property(r.id, "meshPath");
        if (!m || m.indexOf(":/") === 0 || meshes[m]) return;
        meshes[m] = true;
        order.push(m);
    });
    var files = {};
    order.forEach(function (m, k) {
        var dir = OUT + "/raw/" + SLUG + "/" + k;
        var raw = assets.exportRaw(m, dir, { dependencies: false, hash: false });
        raw.files.forEach(function (f) {
            if (!/\.(png|jpg|jpeg|tga|bmp|dds)$/i.test("" + f)) files[m] = dir + "/" + f;
        });
    });
    editor.gameView(false);
    if (project.close() !== true) fail("close failed");

    project.create("reauthor scratch", { template: "empty" });
    var table = {};
    order.forEach(function (m) {
        if (!files[m]) { log("no model file for " + m); return; }
        assets.importAndPlace(files[m], { position: { x: 0, y: 0, z: 0 } });
        var byIndex = {};
        scene.nodes().forEach(function (r) {
            if (r.type !== "mesh") return;
            var qm = "" + node.property(r.id, "meshPath");
            if (!qm || qm.indexOf(":/") === 0) return;
            byIndex["" + node.property(r.id, "meshIndex")] = material.get(r.id).baseColor;
        });
        table[m] = byIndex;
        log("today's import of " + files[m].split("/").pop() + ": " + J(byIndex));
        scene.nodes().forEach(function (r) {
            if (!r.parent) { try { node.remove(r.id); } catch (e) {} }
        });
    });
    if (project.close() !== true) fail("scratch close failed");
    console.log("[import-colours] " + J(table));
    log("done (measure)");
}

// ===========================================================================
// 3. APPLY
// ===========================================================================
if (MODE === "apply") {
    var imported = openArchive(ARCHIVE);
    var grounds = groundsOf();
    if (grounds.length === 0) { log("no ground.obj floor — already re-authored, nothing written"); }
    else {
        if (grounds.length !== 1) fail(grounds.length + " ground.obj nodes — one floor per sample");
        var ground = grounds[0].id;
        var parent = grounds[0].parent || "";
        var gm = material.get(ground);
        var gp = node.physicsInfo(ground);
        log("old ground: parent " + (node.info(parent) ? node.info(parent).name : "(root)") +
            ", material " + J({ workflow: gm.workflow, ior: gm.ior, specularColor: gm.specularColor,
                                roughness: gm.roughness, metallic: gm.metallic,
                                textureScale: gm.textureScale, normalMap: gm.normalMap }) +
            ", physics " + J({ enabled: gp.enabled, restitution: gp.restitution, friction: gp.friction }));

        var floor = scene.addFloor(parent ? { template: "basic", parent: parent } : { template: "basic" });
        if (!floor) fail("scene.addFloor returned nothing");
        if (node.remove(ground) !== true) fail("could not remove the old ground");

        var write = {}, carried = [];
        var lobeEdited = LOBE.some(function (k) { return differs(gm[k], OLD_GROUND[k]); });
        if (lobeEdited) {
            LOBE.forEach(function (k) { write[k] = gm[k]; });
            carried.push("the surface lobe " + J(write));
        }
        var ts = Array.isArray(gm.textureScale) ? gm.textureScale[0] : gm.textureScale;
        if (differs(ts, OLD_GROUND.textureScale)) {
            write.textureScale = ts * FLOOR_FACE_M / OLD_REPEAT_M;
            carried.push("checker repeat " + r3(OLD_REPEAT_M / ts) + " m (textureScale " + ts +
                         " -> " + r3(write.textureScale) + ")");
        }
        // The picture: the shipped tile resolves BY CONTENT to the row the
        // floor just pinned, so a different path is a picture the sample chose
        // (World Background's metal deck).
        var fm = material.get(floor);
        if (gm.baseColorMap && differs(gm.baseColorMap, fm.baseColorMap)) {
            write.baseColorMap = gm.baseColorMap;
            carried.push("baseColorMap " + gm.baseColorMap);
        }
        if (differs(gm.baseColor, OLD_GROUND.baseColor)) {
            write.baseColor = gm.baseColor;
            carried.push("baseColor " + gm.baseColor);
        }
        if (gm.normalMap) { write.normalMap = gm.normalMap; carried.push("normalMap " + gm.normalMap); }
        if (Object.keys(write).length && material.set(floor, write) !== true)
            fail("material.set refused " + J(write));

        if (!gp.enabled) {
            node.physics(floor, { type: "none" });
            carried.push("no physics body");
        } else {
            var body = {};
            if (differs(gp.restitution, OLD_GROUND.restitution)) body.restitution = gp.restitution;
            if (differs(gp.friction, OLD_GROUND.friction)) body.friction = gp.friction;
            if (Object.keys(body).length) {
                if (node.physics(floor, body) !== true) fail("node.physics refused " + J(body));
                carried.push("physics " + J(body));
            }
        }
        log("FLOOR: the Basic floor (" + floor + ") under " +
            (parent ? node.info(parent).name : "the root") + "; carried: " +
            (carried.length ? carried.join("; ") : "nothing (the sample's ground was the default)"));

        if (OPEN_SCENES[SAMPLE]) {
            var hf = world.heightFog({ enabled: true });
            log("HEIGHT FOG on (an open scene): " + J(hf));
        } else {
            log("HEIGHT FOG not added (a walled, roofed room, or a photographed sky: " +
                J(world.heightFog().enabled) + ")");
        }
        var em = world.settings().exposureMode || {};
        log("EXPOSURE " + J({ mode: em.valueId, source: em.source, ev: world.postFx().exposureEv }) +
            " — 0 stops is the re-derived default chain; nothing written");

        if (project.save() !== true) fail("save failed");

        compose(P.warm);
        shoot("after");
        editor.frame(60, 1.0 / 60.0);
        var shot = editor.screenshot(TREE + "/scenes/preview/" + P.file, P.w, P.h,
                                     [{ x: 0.5, y: 0.5 }], P.grade);
        log("preview " + P.file + " " + P.w + "x" + P.h + " centre " + J(shot.center));
        editor.gameView(false);

        var out = project.exportArchive(ZIP);
        if (!out || !out.path) fail("exportArchive failed");
        log("wrote " + out.path + " (" + out.assets + " assets, " + out.objects + " objects)");
        if (project.close() !== true) fail("close failed");
    }

    // ---- the round trip: what the shipped archive opens as -------------------
    openArchive(ZIP);
    editor.frame(120, 1.0 / 60.0);
    var stats = app.openStats();
    var issues = editor.issues();
    var missing = issues.filter(function (i) { return i.kind === "model.missing"; });
    log("REOPENED: " + scene.nodes().length + " nodes, uiThreadParses " + stats.uiThreadParses +
        ", bakeBuilds " + stats.bakeBuilds + ", model.missing " + missing.length + ", issues " + issues.length + " " +
        J(issues.map(function (i) { return i.kind + ":" + i.nodeName; })));
    if (groundsOf().length) fail("the reopened archive still stands on ground.obj");
    if (stats.uiThreadParses !== 0) fail("the open parsed " + stats.uiThreadParses + " model(s)");
    if (issues.length) fail("the reopened sample has scene issues");
    if (world.heightFog().enabled !== !!OPEN_SCENES[SAMPLE])
        fail("the reopened height fog is " + world.heightFog().enabled);

    // ---- the drag-in: every library object arrives rotated and dressed ------
    var objects = assets.list({ scope: "project", type: "object" }).filter(function (a) {
        return a.type === "object" || a.type === "Object";
    });
    var dragged = 0;
    objects.forEach(function (a) {
        var id;
        try { id = assets.addToScene(a.guid, { position: { x: 0, y: 0, z: 30 } }); }
        catch (e) { return; }                     // a built-in row has no blob to place
        if (!id) return;
        var parts = [{ id: id, name: node.info(id).name, depth: 0 }].concat(node.components(id));
        var imported = parts.some(function (p) {
            return node.info(p.id).type === "mesh"
                && ("" + node.property(p.id, "meshPath")).indexOf(":/") !== 0;
        });
        if (!imported) { node.remove(id); return; }   // a built-in's row: no blob of its own
        ++dragged;
        var rotated = 0, meshes = [];
        parts.forEach(function (p) {
            var r = node.info(p.id).rotation;
            if (Math.abs(r.x) + Math.abs(r.y) + Math.abs(r.z) > 1e-3) ++rotated;
            if (node.info(p.id).type === "mesh") {
                var m = material.get(p.id);
                meshes.push(p.name + " " + m.baseColor + " r" + r3(m.roughness) + " m" + r3(m.metallic));
            }
        });
        log("DRAG-IN " + a.name + ": " + parts.length + " node(s), " + rotated + " rotated; " +
            meshes.join(" | "));
        node.remove(id);
    });
    log(dragged + " library object(s) dragged in");
    if (project.close() !== true) fail("close (round trip) failed");
    log("done (apply)");
}
