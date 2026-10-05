// make_tornado.js — THE TORNADO demo (TORNADO-1), re-authored by verbs from the
// archive's 2019 project (SPECS/audits/ANIMATED_MATERIALS_AUDIT_2026-10-04.md
// §1.2-1.3). Forward-only: nothing reads the old format; the values below were
// read out of it once, by hand, and are the whole of what carries over.
//
//   TREE=<the source tree>
//   SRC=<the unpacked tornado.zip: tornado.obj + 231.jpg>
//   sed -e "s|@TREE@|$TREE|g" -e "s|@SRC@|$SRC|g" -e "s|@OUT@|<shots dir>|g" \
//       $TREE/scenes/tools/make_tornado.js > /tmp/tornado.js
//   DISPLAY=<your Xvfb, 1920x1080> <build>/bin/Jahshaka --script /tmp/tornado.js \
//       --data-root <a scratch root>
//
// ENGINE-UP (never --headless): the frames and the preview are real renders.
//
// WHAT THE ORIGINAL IS (the audit's decode):
//   * ONE mesh, tornado.obj: an open, slightly twisted cone, 34 rings x 160
//     segments, v running up it. The funnel is the mesh; all motion is shader.
//   * THREE layers of it, each with its own copy of one graph:
//       Main         the glowing core            back-culled, no cut-out
//       Top Shell    a 5 % larger torn shell     two-sided, cut at 0.56
//       Bottom Wave  the cone squashed to a disc two-sided, cut at 0.56
//   * THE GRAPH: a grey cloud noise, tiled 1x2, scrolling by ScrollDir, times
//     orange (1, 0.5, 0.035) plus a fresnel rim -> EMISSION; the noise's red
//     channel -> ALPHA (the shells' cut-out); and a travelling sine up the
//     funnel, sin(Waves * (v + WaveSpeed * t)) * WaveHeight * v -> VERTEX
//     EXTRUSION. Unlit in the original (acceptLighting false): here it is our
//     ONE PBR master with a black base, so the picture is the emissive (a black
//     dielectric still keeps an F0 0.04 specular — the audit's risk, accepted).
//   * THE OLD ENGINE APPLIED THE WORLD MATRIX TWICE (verified in its generated
//     vertex source), so with no translation every scale acted SQUARED. The
//     scales below are the squares, or the funnel is half the height of the
//     still. The extrusion moved in the mesh's own space before that double
//     matrix, so its height is scaled by the same square (ours extrudes in
//     world units along the world normal).
//
// THE LIVE PATHS IT RIDES (TORNADO-1): the scrolling noise is the material's
// UV fold (G1 — the emissive map and the cut-out scroll on the shader clock);
// the rim is a live emissive term through the fork's custom_ps_emissive hook
// (G2) computing the REAL fresnel (G3); the extrusion is the vertex piece.
// graph.emitInfo says so for each material, or this script stops.

var TREE = "@TREE@";
var SRC = "@SRC@";
var OUT = "@OUT@";
var ARCHIVE = TREE + "/scenes/Tornado.zip";
var PREVIEW = TREE + "/scenes/preview/tornado.png";

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function log(m) { console.log("[tornado] " + m); }
function J(x) { return JSON.stringify(x); }

// THE HDR GAIN on the orange: the core must bloom like the still's. Tuned by eye
// against reference.png at the original camera.
var GLOW = 4.0;

// The three layers (audit §1.2). height = the authored 0.05 x the squared
// radial scale (see the header).
var LAYERS = [
    { name: "Main", scale: { x: 4.0, y: 4.0, z: 4.0 },
      waveSpeed: -0.56, waveHeight: 0.05 * 4.0, waves: 31.5, scroll: [-0.5, -0.5],
      cutoff: 0.0, fresnel: 1.17, twoSided: false },
    { name: "Top Shell", scale: { x: 4.41, y: 4.0, z: 4.41 },
      waveSpeed: -0.1, waveHeight: 0.05 * 4.41, waves: 40.0, scroll: [-0.5, -0.5],
      cutoff: 0.56, fresnel: 1.0, twoSided: true },
    { name: "Bottom Wave", scale: { x: 5.39, y: 0.34, z: 4.75 },
      waveSpeed: -0.1, waveHeight: 0.05 * 5.07, waves: 40.0, scroll: [-0.5, -0.2],
      cutoff: 0.56, fresnel: 1.0, twoSided: true },
];

// ---- the world ---------------------------------------------------------------
assert(project.create("Tornado", { template: "basic" }).length > 0, "created the project (Basic: one floor)");

// THE REFERENCE LOOK: a grey single-colour sky (grey 72) and grey fog. The Sky
// Light the template carries reads that sky: the ambient is grey, physically.
assert(world.sky("color", { color: "#484848" }), "the grey single-colour sky (72)");
assert(world.fog({ enabled: true, color: "#484848", density: 0.006 }), "grey fog");

// THE SUN at the original's direction: its directional light sat at (4,4,0)
// rotated 15 degrees about X. Shadows on (every light is born casting).
var sun = world.sunLight();
assert(sun && sun.length > 0, "the template's sun");
node.transform(sun, { position: { x: 4, y: 4, z: 0 }, rotation: { x: 15, y: 0, z: 0 } });

// ---- the inputs, through the one import pipeline -----------------------------
// The archive's two noise images are byte-identical (md5 738562860cd7...): ONE ships.
var meshGuid = assets.importFile(SRC + "/tornado.obj");
assert(meshGuid && meshGuid.length > 10, "imported tornado.obj");

// ---- one graph material per layer -------------------------------------------
function buildGraph(L) {
    var guid = materials.create("Tornado " + L.name, { graph: true });
    assert(guid.length > 10, L.name + ": materials.create");
    var noise = materials.addTexture(guid, SRC + "/231.jpg");
    assert(noise && noise.length > 10, L.name + ": the noise is a library texture of the material");
    var master = null;
    graph.nodes().forEach(function (n) { if (n.master) master = n.id; });

    // the noise lookup: uv(1 x 2) -> panner(ScrollDir, the clock) -> the texture's UV
    var tex = graph.addNode("texture");
    assert(graph.setValue(tex, noise), L.name + ": noise on a texture node");
    var tile = graph.addNode("uv");
    assert(graph.setValue(tile, { tileX: 1, tileY: 2 }), L.name + ": tiled 1 x 2");
    var scrollDir = graph.addNode("vector2");
    assert(graph.setValue(scrollDir, { x: L.scroll[0], y: L.scroll[1] }), L.name + ": ScrollDir");
    var pan = graph.addNode("panner");
    assert(graph.connect(tile, 0, pan, 0) && graph.connect(scrollDir, 0, pan, 1), L.name + ": panner");
    // the Texture node samples itself at its UV input (its RGBA out)
    assert(graph.connect(pan, 0, tex, 0), L.name + ": panner -> texture UV");
    var sample = tex;

    // EMISSIVE = fresnel(FresnelPower) + noise x (orange x GLOW)
    var orange = graph.addNode("color");
    assert(graph.setValue(orange, { r: 1.0, g: 0.50, b: 0.035, a: 1.0 }), L.name + ": orange");
    var gain = graph.addNode("float");
    assert(graph.setValue(gain, GLOW), L.name + ": glow");
    var hot = graph.addNode("multiply");
    assert(graph.connect(orange, 0, hot, 0) && graph.connect(gain, 0, hot, 1), L.name + ": orange x glow");
    var lit = graph.addNode("multiply");
    assert(graph.connect(sample, 0, lit, 0) && graph.connect(hot, 0, lit, 1), L.name + ": noise x orange");
    var power = graph.addNode("float");
    assert(graph.setValue(power, L.fresnel), L.name + ": FresnelPower");
    var rim = graph.addNode("fresnel");
    assert(graph.connect(power, 0, rim, 1), L.name + ": fresnel");
    var glow = graph.addNode("add");
    assert(graph.connect(rim, 0, glow, 0) && graph.connect(lit, 0, glow, 1), L.name + ": rim + noise");
    assert(graph.connect(glow, 0, master, "Emissive"), L.name + ": -> Emissive");

    // BASE COLOUR black: the picture is the emission.
    var black = graph.addNode("color");
    assert(graph.setValue(black, { r: 0, g: 0, b: 0, a: 1 }), L.name + ": black");
    assert(graph.connect(black, 0, master, "Base Color"), L.name + ": -> Base Color");

    // THE SHELLS' CUT-OUT: the noise's red channel, cut at AlphaCutoff.
    if (L.cutoff > 0) {
        var red = graph.addNode("splitvector");
        assert(graph.connect(sample, 0, red, 0), L.name + ": noise -> split");
        assert(graph.connect(red, 0, master, "Alpha"), L.name + ": red -> Alpha");
        var cut = graph.addNode("float");
        assert(graph.setValue(cut, L.cutoff), L.name + ": AlphaCutoff");
        assert(graph.connect(cut, 0, master, "Alpha Cutoff"), L.name + ": -> Alpha Cutoff");
    }

    // VERTEX EXTRUSION = sin(Waves * (v + WaveSpeed * t)) * WaveHeight * v
    var uv = graph.addNode("uv");
    var speed = graph.addNode("vector2");
    assert(graph.setValue(speed, { x: L.waveSpeed, y: L.waveSpeed }), L.name + ": WaveSpeed");
    var wpan = graph.addNode("panner");
    assert(graph.connect(uv, 0, wpan, 0) && graph.connect(speed, 0, wpan, 1), L.name + ": wave panner");
    var waves = graph.addNode("float");
    assert(graph.setValue(waves, L.waves), L.name + ": Waves");
    var phase = graph.addNode("multiply");
    assert(graph.connect(wpan, 0, phase, 0) && graph.connect(waves, 0, phase, 1), L.name + ": x Waves");
    var py = graph.addNode("splitvector");
    assert(graph.connect(phase, 0, py, 0), L.name + ": phase -> split");
    var sine = graph.addNode("sine");
    assert(graph.connect(py, 1, sine, 0), L.name + ": .y -> sine");
    var height = graph.addNode("float");
    assert(graph.setValue(height, L.waveHeight), L.name + ": WaveHeight");
    var amp = graph.addNode("multiply");
    assert(graph.connect(sine, 0, amp, 0) && graph.connect(height, 0, amp, 1), L.name + ": x WaveHeight");
    var vs = graph.addNode("splitvector");
    assert(graph.connect(uv, 0, vs, 0), L.name + ": uv -> split");
    var grow = graph.addNode("multiply");
    assert(graph.connect(amp, 0, grow, 0) && graph.connect(vs, 1, grow, 1), L.name + ": x v");
    assert(graph.connect(grow, 0, master, "Vertex Extrusion"), L.name + ": -> Vertex Extrusion");

    // THE CONTRACT: both live sockets EMITTED, nothing frozen.
    var info = graph.emitInfo();
    log(L.name + " emitInfo: route " + info.route + ", emitted " + J(info.emitted) +
        ", fallback " + J(info.fallback) + ", baked " + J(info.baked) + ", fold " + J(info.fold));
    assert(info.route === "live", L.name + ": the LIVE route");
    assert(info.emitted.indexOf("Emissive") >= 0 && info.emitted.indexOf("Vertex Extrusion") >= 0,
           L.name + ": Emissive and Vertex Extrusion are EMITTED");
    assert(Object.keys(info.fallback).length === 0, L.name + ": NO fallback " + J(info.fallback));
    assert(info.fold && Math.abs(info.fold.velocity[0] - L.scroll[0]) < 1e-6 &&
           Math.abs(info.fold.velocity[1] - L.scroll[1]) < 1e-6,
           L.name + ": the noise scrolls through the material's UV fold " + J(info.fold));
    assert(graph.save(), L.name + ": graph.save");
    return guid;
}

var nodes = {};
var guids = {};
LAYERS.forEach(function (L) {
    var guid = buildGraph(L);
    guids[L.name] = guid;
    var id = assets.addToScene(meshGuid, { position: { x: 0, y: 0, z: 0 } });
    assert(id && id.length > 0, L.name + ": placed");
    node.rename(id, L.name);
    node.transform(id, { position: { x: 0, y: 0, z: 0 }, scale: L.scale });
    // the mesh node under the placed model (an import is a group over its meshes)
    var mesh = id;
    scene.nodes().forEach(function (n) { if (n.type === "mesh" && n.parent === id) mesh = n.id; });
    assert(graph.toMaterial(mesh), L.name + ": the graph applied to the mesh");
    if (L.twoSided) assert(node.setProperty(mesh, "faceCullingMode", "none"), L.name + ": two-sided");
    var m = material.get(mesh);
    assert(typeof m.customPiecePixel === "string" && m.customPiecePixel.length > 0 &&
           typeof m.customPieceVertex === "string" && m.customPieceVertex.length > 0,
           L.name + ": the material carries both generated pieces");
    assert(m.textureVelocity && Math.abs(m.textureVelocity[0] - L.scroll[0]) < 1e-6, L.name + ": the material scrolls");
    nodes[L.name] = mesh;
});

// ---- FROM THE LIBRARY: the Main material dragged onto a fresh cube stays LIVE --
// (its pieces travel with its definition), then the cube goes again.
(function () {
    var probe = scene.addPrimitive("cube", { position: { x: 6, y: 1, z: 0 } });
    assert(material.apply(probe, guids["Main"]), "Main applied from the library to a fresh cube");
    var pm = material.get(probe);
    assert(pm.customPiecePixel && pm.customPieceVertex && pm.textureVelocity,
           "...and it arrives LIVE: both pieces and the scroll " + J(pm.textureVelocity));
    assert(materials.open(guids["Main"]) && graph.emitInfo().route === "live", "...graph.emitInfo: live");
    var a = [], b = [];
    [1.0, 0.0].forEach(function (t, k) {
        world.shaderTime({ t: t });
        editor.select(null);
        editor.frameNode(probe, { yaw: 20, pitch: 15, distance: 3.0 });
        editor.frame(20);
        (k ? b : a).push(editor.screenshot(OUT + "/library_cube_t" + t + ".png", 160, 160,
                                           [{ x: 0.5, y: 0.5 }, { x: 0.42, y: 0.58 }]).probes);
    });
    var worst = 0;
    for (var i = 0; i < a[0].length; ++i)
        worst = Math.max(worst, Math.abs(a[0][i].r - b[0][i].r), Math.abs(a[0][i].g - b[0][i].g),
                         Math.abs(a[0][i].b - b[0][i].b));
    assert(worst > 0, "...and animates: t=1.0 differs from t=0 (worst " + worst + ")");
    world.shaderTime({ t: null });
    assert(node.remove(probe), "the probe cube removed");
})();

// ---- the camera: the original editor camera ----------------------------------
editor.setCamera({ position: { x: 2.25, y: 6.02, z: 8.74 }, rotation: { x: -16.8, y: 10.8, z: 0 }, fov: 45 });
editor.select(null);
assert(project.save(), "saved");

// ---- the frames beside the reference, at a PINNED shader time ----------------
editor.gameView(true);
[0, 1.5].forEach(function (t) {
    var st = world.shaderTime({ t: t });
    assert(st.pinned && st.live, "the shader clock pinned at " + t + " and read by the scene " + J(st));
    editor.frame(60);
    var shot = editor.screenshot(OUT + "/tornado_t" + String(t).replace(".", "_") + ".png", 920, 430,
                                 [{ x: 0.5, y: 0.4 }, { x: 0.5, y: 0.85 }], "scene");
    log("t=" + t + " probes " + J(shot.probes));
});

// ---- the sample: preview + archive --------------------------------------------
var shotP = editor.screenshot(PREVIEW, 1280, 720, [{ x: 0.5, y: 0.5 }], "scene");
log("preview centre " + J(shotP.center));
world.shaderTime({ t: null });
editor.gameView(false);
var out = project.exportArchive(ARCHIVE);
assert(out && out.assets > 0, "exported " + ARCHIVE + " (" + J(out) + ")");
assert(project.close(), "closed");
console.log("make_tornado: PASS");
