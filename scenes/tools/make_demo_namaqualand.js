// make_demo_namaqualand.js — DEMO-SCENES-1: a NAMAQUALAND dry-river-bed vignette as a project, built
// from Poly Haven's CC0 scans (8 models + a ground material), through OUR model door, by verbs only.
//
//   python3 -I scenes/tools/demo_polyhaven_prepare.py <poly-haven/extracted> <poly-haven/prepared>
//   scenes/tools/demo_scene_run.sh scenes/tools/make_demo_namaqualand.js <build>/bin <data root> \
//       <poly-haven/extracted> <evidence dir> :NN
//
// The World template (a 5 x 5 grid of 100 m floor cubes, top face at y = 0) wears ONE Poly Haven ground
// — gravelly_sand: the pale, pebbly sand of a dry wash; the other four (cliff_side, rock_face,
// tiger_rock: rock faces; sandy_gravel_02: darker, coarser) read as walls or road. Its maps are bound
// by hand: diffuse, OpenGL normal and the roughness the prep split out of the packed ARM (occlusion
// has no slot). The eight scans are imported ONCE each (glTF: the door reads its metal/rough, normal
// and two-sided flags; its occlusion is dropped) and placed as dozens of instances — a seeded
// scatter, so every run builds the same vignette: rocks, stones and boulders IN the bed, succulents
// and rooibos on its banks, quiver trees (and a dead one) up the banks, the cliff behind. The
// leaves arrive glTF BLEND (Translucent); foliage is cut out instead (Masked, both faces) so it
// sorts and shadows like leaves. A low sun from behind the cliff's shoulder, the physical sky.
// HDRI: the library takes no .exr/.hdr image (Constants::IMAGE_EXTS is png/jpg/jpeg/bmp/tga and no
// float reader exists), so the sky is the physical one.
var SRC = "@SRC@", OUT = "@OUT@";
var PREP = SRC.replace(/\/extracted\/?$/, "") + "/prepared";
var DEMO_SHOT_W = 0, DEMO_SHOT_H = 0, DEMO_PLAN = false;   // 0 = 1920 x 1080
var SUN_ELEVATION = 22, SUN_AZIMUTH = 200, DEMO_EV = 1.0, GROUND_TILE_M = 2.4;
var J = JSON.stringify;
function fail(m) { throw new Error("namaqualand: " + m); }
function log() { console.log(Array.prototype.join.call(arguments, " ")); }

var timing = {}, tAll = Date.now();
project.create(project.nextFreeName("Namaqualand"), {template: "world"});

// ---- the ground ----
var G = SRC + "/materials/gravelly_sand/textures/gravelly_sand_";
var groundSet = {baseColorMap: assets.importFile(G + "diff_4k.jpg"), normalMap: assets.importFile(G + "nor_gl_4k.jpg"),
                 roughnessMap: assets.importFile(PREP + "/gravelly_sand_rough_4k.png"), roughness: 1.0,
                 metallic: 0.0, textureScale: 100 / GROUND_TILE_M};
var floors = 0;
for (var f = 1; f <= 25; ++f) {
    var fid = scene.find("Floor " + f);
    if (!fid) continue;
    if (!material.set(fid, groundSet)) fail("ground refused on Floor " + f);
    floors++;
}
if (floors !== 25) fail("expected the World template's 25 floors, found " + floors);

// ---- the scans ----
var MODELS = {
    quiver: "quiver_tree_01", deadTrunk: "dead_quiver_trunk", succulent: "cheiridopsis_succulent",
    rooibos: "wild_rooibos_bush", boulder: "namaqualand_boulder_02", rocks: "namaqualand_rocks_01",
    cliff: "namaqualand_cliff_01", stones: "namaqualand_stones_01"
};
var lib = {};
for (var k in MODELS) {
    var t0 = Date.now(), name = MODELS[k], g = null;
    var have = assets.list({scope: "store", type: "object", query: name});
    for (var h = 0; h < have.length && !g; ++h) if (have[h].name === name + "_4k") g = have[h].guid;
    var reused = !!g;
    if (!g) g = assets.import(SRC + "/models/" + name + "/" + name + "_4k.gltf", {units: "auto"});
    var meta = assets.metadata(g);
    timing[name] = {reused: reused, importMs: Date.now() - t0, triangles: meta.triangles, extent: meta.extent};
    lib[k] = assets.addToProject(g);
}
log("import", J(timing));

// ---- the scatter (seeded: the same vignette every run) ----
var seed = 20261009;
function rnd() { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return seed / 0x7fffffff; }
function range(a, b) { return a + (b - a) * rnd(); }
// The wash runs along X, its bed |z| < 3 m, meandering; banks 3..12 m out, the cliff ~16 m back.
function bedZ(x) { return 2.0 * Math.sin(x * 0.11) + 0.8 * Math.sin(x * 0.37 + 1.3); }
var placed = 0, perKind = {};
function place(kind, x, z, s, yaw) {
    var id = assets.addToScene(lib[kind], {position: {x: x, y: 0, z: z}, onSurface: true});
    var info = node.info(id);
    node.transform(id, {rotation: {x: 0, y: yaw === undefined ? range(0, 360) : yaw, z: 0},
                        scale: {x: s, y: s, z: s}});
    placed++; perKind[kind] = (perKind[kind] || 0) + 1;
    return id;
}
var X0 = -18, X1 = 18;   // the vignette: 36 m of wash, dense enough to read from the path
for (var i = 0; i < 30; ++i) { var x = range(X0, X1); place("stones", x, bedZ(x) + range(-2.5, 2.5), range(1.5, 2.5)); }
for (var i = 0; i < 24; ++i) { var x = range(X0, X1); place("rocks", x, bedZ(x) + range(-3, 3), range(1.5, 3.0)); }
for (var i = 0; i < 9; ++i)  { var x = range(X0, X1); var side = rnd() < 0.5 ? -1 : 1;
                               place("boulder", x, bedZ(x) + side * range(2.5, 6), range(0.8, 1.5)); }
for (var i = 0; i < 30; ++i) { var x = range(X0, X1); var side = rnd() < 0.5 ? -1 : 1;
                               place("succulent", x, bedZ(x) + side * range(3.0, 9), range(0.9, 1.6)); }
for (var i = 0; i < 22; ++i) { var x = range(X0, X1); var side = rnd() < 0.5 ? -1 : 1;
                               place("rooibos", x, bedZ(x) + side * range(3.0, 10), range(1.0, 1.8)); }
for (var i = 0; i < 7; ++i)  { var x = range(X0 + 4, X1 - 4); var side = i % 2 ? -1 : 1;
                               place("quiver", x, bedZ(x) + side * range(6, 13), range(1.2, 2.0)); }
for (var i = 0; i < 3; ++i)  { var x = range(X0, X1); place("deadTrunk", x, bedZ(x) + range(4, 9) * (i % 2 ? -1 : 1), range(1.1, 1.6)); }
for (var i = 0; i < 4; ++i)  { place("cliff", -27 + i * 17 + range(-2, 2), -24 - range(0, 3), range(1.0, 1.3)); }
log("scatter", placed, J(perKind));

// ---- foliage: cut out, both faces ----
// The door ignores glTF alphaMode (BLEND/MASK) and alphaCutoff, and Poly Haven's glTF binds a JPG for the
// BLEND leaves anyway: the rooibos twigs/leaves and the cheiridopsis flowers would draw as solid black
// cards. The prep composed each diffuse with the asset's published Alpha map; every node wearing that
// diffuse takes the RGBA picture, Masked, both faces (the bush body's alpha is opaque there).
var CUTOUT = {"wild_rooibos_bush_diff_4k": "wild_rooibos_bush_diff_alpha_2k.png",
              "cheiridopsis_succulent_diff_4k": "cheiridopsis_succulent_diff_alpha_2k.png"};
var cutGuid = {}, texName = {}, cut = 0;
var rows = scene.nodes();
for (var r = 0; r < rows.length; ++r) {
    var row = rows[r];
    if (row.type !== "mesh") continue;
    var m = material.get(row.id), g0 = m.textureAssets ? m.textureAssets.baseColorMap : "";
    if (!g0) continue;
    if (texName[g0] === undefined) texName[g0] = String(assets.metadata(g0).name || "").replace(/\.[^.]*$/, "");
    var file = CUTOUT[texName[g0]];
    if (!file) continue;
    if (!cutGuid[file]) cutGuid[file] = assets.importFile(PREP + "/" + file);
    material.set(row.id, {baseColorMap: cutGuid[file], alphaMode: 1, alphaCutoff: 0.5});
    node.setProperty(row.id, "faceCullingMode", "none");
    cut++;
}
log("foliage cut", cut);

// ---- the light: a low sun ----
var sun = world.sunLight();
if (!sun) fail("the World template brought no sun");
node.transform(sun, {rotation: {x: 90 - SUN_ELEVATION, y: SUN_AZIMUTH, z: 0}});
world.shadows({enabled: true});
world.postFx({exposureEv: DEMO_EV});

// ---- the tier, the first view, the save ----
world.mode({mode: "high"});
world.photon({enabled: true, tier: "high"});
var VIEWS = [
    {name: "first", position: {x: -16, y: 1.5, z: 4.0}, lookAt: {x: 4, y: 0.6, z: -3}},
    {name: "walk1", position: {x: 4, y: 1.5, z: 9}, lookAt: {x: -2, y: 1.0, z: -8}},
    {name: "walk2", position: {x: 14, y: 2.0, z: -2}, lookAt: {x: -4, y: 0.5, z: 2}}
];
if (DEMO_PLAN) VIEWS.push({name: "plan", position: {x: 0, y: 70, z: 1}, lookAt: {x: 0, y: 0, z: 0}});
function look(v) { return editor.setCamera({position: v.position, lookAt: v.lookAt}); }
look(VIEWS[0]);
if (project.save() !== true) fail("save failed");
timing.totalMs = Date.now() - tAll;

var W = DEMO_SHOT_W || 1920, H = DEMO_SHOT_H || 1080;
for (var v = 0; v < VIEWS.length; ++v) {
    look(VIEWS[v]);
    var shot = editor.screenshot(OUT + "/namaqualand_" + VIEWS[v].name + ".png", W, H, [], "scene");
    log("shot", VIEWS[v].name, J(shot.center));
}
look(VIEWS[0]);
editor.frame(120);
var rs = app.renderStats(), gs = world.giStatus();
log("REPORT", J({timing: timing, placed: placed, perKind: perKind, foliageCut: cut, frameMs: rs.frameMs,
                 sceneTriangles: rs.sceneTriangles, giAtRest: gs.giAtRest, cascades: (gs.cascades || []).length,
                 cards: gs.cards, mode: gs.mode}));
0;
