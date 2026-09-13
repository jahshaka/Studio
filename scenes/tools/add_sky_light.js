// THE SKY LIGHT lands in the eight shipped samples, and the level they were
// authored at is PRESERVED (SPECS/SKY_LIGHT_SPEC.md §9.1, owner pick 1).
//
// Run it, do not hand-edit the archives:
//
//   TREE=<absolute path to the source tree>
//   sed -e "s|@TREE@|$TREE|" -e "s|@MODE@|apply|" -e "s|@OUT@|<a scratch dir>|" \
//       $TREE/scenes/tools/add_sky_light.js > /tmp/skylight.js
//   cd <a scratch dir>
//   DISPLAY=<your Xvfb, 1920x1080> \
//       <build>/bin/Jahshaka --script /tmp/skylight.js --data-root <a scratch root>
//
// @MODE@ = "measure" reads and photographs both states without writing anything
// — no project.save, no preview, no exportArchive. It is how the A/B in the
// report was taken.
//
// NOT --headless: the previews are real frames and the probes are pixels.
//
// WHY. Ambient used to be a flat World-panel colour, and every one of these
// eight samples leaned on it: seven ship a 72-grey SINGLE_COLOR sky with
// `ambientColor` 96,96,96, and World Background rides its cubemap through the
// old 1.4x gain. Both are gone (owner decision D14): a scene's ambient is a SKY
// LIGHT reading the World sky, and a scene without one is dark. So every sample
// needs one, and needs it at a strength that leaves the picture where its author
// put it.
//
// THE LEVEL, and why the sky moves with the light (owner pick 1, option ii):
// the old flat path pushed 96/255 = 0.376 through HlmsPbs' 1/pi split, i.e.
// 0.120 of radiance. A 96-grey SKY, decoded sRGB->linear (the colour-space rule,
// §4) and integrated over the hemisphere, is 0.117 — the owner's ambient number
// becomes the sky itself and the Sky Light's default stays an honest 1.0.
//
// The backdrop's own AUTHORED number goes up one step (72 -> 96) and its
// RENDERED brightness goes down, because the same colour-space rule that makes
// the light land right also decodes the backdrop: 96 sRGB is 0.117 of radiance
// where a raw 72 was 0.282. That is the point of D14 rather than a side effect
// — the grey you pick and the light that comes off it are the same number now —
// and how dark the backdrop should read is a question for the render review,
// which is a parameter of this tool and a re-run, never a hand edit.
//
// WORLD BACKGROUND is the exception because it has a real environment: its
// cubemap sky was multiplied by 0.376 x 1.4 = 0.527 of its own integral, so its
// Sky Light ships at 0.53 rather than 1.0. Its sky is a photograph and is not
// touched.
//
// WHAT IS NOT TOUCHED: the geometry, the materials, the cameras, the tiers, the
// GI settings, the lights that were already there. This adds ONE node per sample
// and (for the seven colour-sky ones) moves two greys.
//
// IDEMPOTENT: a second run finds the Sky Light already there, leaves its
// intensity alone, finds the greys already at 96, re-shoots the same picture and
// re-exports the same archive.

var TREE = "@TREE@";
var MODE = "@MODE@";
var OUT  = "@OUT@";

// name, archive, preview file, preview size, and the Sky Light strength that
// preserves this sample's authored level.
var SAMPLES = [
    { name: "Matcaps",            archive: "Matcaps.zip",            preview: "matcaps.png",    w: 460,  h: 215,  sky: 1.0, lift: true },
    { name: "Particles",          archive: "Particles.zip",          preview: "particles.png",  w: 480,  h: 270,  sky: 1.0, lift: true },
    { name: "Physics",            archive: "Physics.zip",            preview: "physics.png",    w: 1920, h: 1080, sky: 1.0, lift: true },
    { name: "Showroom",           archive: "Showroom.zip",           preview: "showroom.png",   w: 1280, h: 720,  sky: 1.0, lift: true },
    { name: "Showroom 2",         archive: "Showroom 2.zip",         preview: "showroom2.png",  w: 1280, h: 720,  sky: 1.0, lift: true },
    { name: "Mirror Room",        archive: "Mirror Room.zip",        preview: "mirrorroom.png", w: 1280, h: 720,  sky: 1.0, lift: true },
    { name: "Skeletal Animation", archive: "Skeletal Animation.zip", preview: "skeletal.png",   w: 1280, h: 720,  sky: 1.0, lift: true },
    // A cubemap environment, not a grey backdrop: the old gain was 0.527 of it.
    { name: "World Background",   archive: "World Background.zip",   preview: "world.png",      w: 1920, h: 1080, sky: 0.53, lift: false }
];

// Centre plus the four quarters, at the sample's own saved camera.
var PROBES = [{ x: 0.5, y: 0.5 }, { x: 0.25, y: 0.25 }, { x: 0.75, y: 0.25 },
              { x: 0.25, y: 0.75 }, { x: 0.75, y: 0.75 }];

var LIFT = "#606060";        // 96,96,96 — the old Ambient Color, as the sky

function log(m) { console.log("[skylight] " + m); }
function fail(m) { throw new Error("skylight: " + m); }
function J(x) { return JSON.stringify(x); }

// The mean of the five probes, so one number per shot can be compared.
function shoot(sample, tag) {
    var shot = editor.screenshot(OUT + "/" + sample.replace(/ /g, "_") + "-" + tag + ".png",
                                 1280, 720, PROBES, "scene");
    var line = "", sum = 0;
    for (var i = 0; i < shot.probes.length; i++) {
        var p = shot.probes[i];
        line += (i ? "  " : "") + Math.round(p.r) + "," + Math.round(p.g) + "," + Math.round(p.b);
        sum += (p.r + p.g + p.b) / 3;
    }
    var mean = Math.round(sum / shot.probes.length * 100) / 100;
    log(sample + " [" + tag + "] mean " + mean + "   probes " + line);
    return mean;
}

function skyLightOf() {
    var s = world.skyLight();
    return s.light === "" ? null : s;
}

for (var i = 0; i < SAMPLES.length; i++) {
    var S = SAMPLES[i];
    var zip = TREE + "/scenes/" + S.archive;
    log(S.name + ": importing " + zip);

    var imported = project.importArchive(zip);
    if (!imported || !imported.guid) fail(S.name + ": importArchive failed");
    if (project.open(imported.guid) !== true) fail(S.name + ": open failed");

    var nodes = scene.nodes().length;
    if (nodes < 2) fail(S.name + ": opened with " + nodes + " nodes — that is not the sample");

    // The sample's own picture: no selection, no light wires, Game View — the
    // frame its preview ships, which is also where the pixels are read.
    editor.select(null);
    editor.setOverlays({ lightWires: false });
    editor.gameView(true);
    editor.frame(120, 1.0 / 60.0);

    var w0 = world.get();
    log(S.name + ": " + nodes + " nodes, sky " + w0.sky.type +
        (w0.sky.color ? " " + w0.sky.color : "") + ", fog " + w0.fog.color +
        ", skyLight " + J(world.skyLight()));
    var before = shoot(S.name, "before");

    // ---- the edit ----------------------------------------------------------
    var existing = skyLightOf();
    if (existing) {
        log(S.name + ": already carries a Sky Light (" + existing.name +
            " at " + existing.intensity + ") — left alone");
    } else {
        var id = scene.addLight("sky");
        if (!id || id.length < 10) fail(S.name + ": scene.addLight('sky') failed");
        node.rename(id, "Sky Light");
        node.setProperty(id, "intensity", S.sky);
        node.setProperty(id, "lightColor", "#ffffff");
        log(S.name + ": added a Sky Light at " + S.sky);
    }

    if (S.lift) {
        // 72 -> 96 on BOTH the sky and the fog: they are the same grey in every
        // one of these samples, and a fog that did not follow would read as a
        // haze of a different colour from the sky it fades into.
        if (w0.sky.type === "SingleColor" && w0.sky.color !== LIFT) {
            if (world.sky("color", { color: LIFT }) !== true) fail(S.name + ": world.sky failed");
            log(S.name + ": sky " + w0.sky.color + " -> " + LIFT);
        }
        if (w0.fog.color !== LIFT) {
            if (world.fog({ color: LIFT }) !== true) fail(S.name + ": world.fog failed");
            log(S.name + ": fog " + w0.fog.color + " -> " + LIFT);
        }
    }

    editor.frame(180, 1.0 / 60.0);
    var after = shoot(S.name, "after");
    log(S.name + ": MEAN " + before + " -> " + after +
        "  (delta " + (Math.round((after - before) * 100) / 100) + ")");

    if (MODE !== "apply") {
        log(S.name + ": MEASURE ONLY — nothing written");
        editor.gameView(false);
        if (project.close() !== true) fail(S.name + ": close failed");
        continue;
    }

    if (project.save() !== true) fail(S.name + ": save failed");

    // ---- the shipped preview ------------------------------------------------
    // Game View (already on), the SAVED camera, the size and the grade each
    // preview shipped at.
    editor.frame(60, 1.0 / 60.0);
    var shot = editor.screenshot(TREE + "/scenes/preview/" + S.preview,
                                 S.w, S.h, [{ x: 0.5, y: 0.5 }], true);
    log(S.name + ": preview " + S.w + "x" + S.h + " centre " + J(shot.center));
    editor.gameView(false);

    // ---- the archive --------------------------------------------------------
    var out = project.exportArchive(zip);
    if (!out || !out.path) fail(S.name + ": exportArchive failed");
    log(S.name + ": wrote " + out.path + " (" + out.assets + " assets, " +
        out.objects + " objects)");
    if (project.close() !== true) fail(S.name + ": close failed");

    // ---- and it survived the round trip -------------------------------------
    var back = project.importArchive(zip);
    if (!back || !back.guid) fail(S.name + ": re-import failed");
    if (project.open(back.guid) !== true) fail(S.name + ": re-open failed");
    var sl = world.skyLight();
    if (sl.light === "") fail(S.name + ": the re-exported archive has no Sky Light");
    if (Math.abs(sl.intensity - S.sky) > 0.001)
        fail(S.name + ": the Sky Light came back at " + sl.intensity + ", not " + S.sky);
    if (sl.count !== 1) fail(S.name + ": " + sl.count + " Sky Lights after the round trip");
    if (S.lift && world.get().sky.type === "SingleColor" && world.get().sky.color !== LIFT)
        fail(S.name + ": the sky grey did not survive the round trip");
    log(S.name + ": reopened with " + sl.name + " at " + sl.intensity +
        ", sky " + world.get().sky.type);
    if (project.close() !== true) fail(S.name + ": close (after re-import) failed");
}

log(MODE === "apply" ? "the eight samples carry their own skylight now"
                     : "measured only — no archive or preview was written");
