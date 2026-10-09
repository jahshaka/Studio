// make_demo_bistro.js — DEMO-SCENES-1: the AMAZON LUMBERYARD BISTRO (exterior + interior) as one
// project, from NVIDIA's ORCA archive, through OUR model door, polished by verbs only.
//
//   python3 -I scenes/tools/demo_bistro_textures.py <Bistro_v5_2/Textures> <png dir>   (maps capped at 1024: VRAM)
//   python3 -I scenes/tools/demo_fbx_prepare.py <Bistro_v5_2> <png dir> <prepared dir>
//   scenes/tools/demo_scene_run.sh scenes/tools/make_demo_bistro.js <build>/bin <data root> \
//       <prepared dir> <evidence dir> :NN
//
// THE SOURCE AND WHAT THE MODEL DOOR MAKES OF IT (measured; the full list is the lane report's
// "scene-import gaps"):
//  - the FBX names its maps `Textures\Foo_BaseColor.dds`: Windows separators (resolve to nothing on
//    Linux) and BC-compressed DDS (no reader of ours decodes it). The two prep tools above make a
//    copy that names `Textures/Foo_BaseColor.png` beside full-resolution PNGs (Pillow's decoder).
//  - Falcor's convention: BaseColor RGB + OPACITY in alpha; Specular R = occlusion, G = roughness,
//    B = metalness; Normal is DirectX (green flipped to OpenGL by the prep); Emissive RGB.
//    The import binds the base colour (DiffuseColor) and the normal (NormalMap) maps; it drops the
//    Specular pack and the Emissive map of a non-glTF material. This script binds the prep's
//    <Foo>_Specular_roughness.png / _metallic.png and <Foo>_Emissive.png per node, cuts the alpha
//    (Masked, both faces) where the base colour's alpha is a cutout, and lights the emissives.
//  - lights: the FBX carries ONE directional (exterior, 61 deg up, warm) and FOUR point lights
//    (interior, 40000 units each, warm); the door imports none of them. They are re-made here. The
//    lamps' own emitters (street lamps, lanterns, wall/ceiling lamps, signs, string lights: 125 nodes)
//    glow through their Emissive maps. Per-lamp point lights are NOT added: on this pin every point
//    light paints a horizontal band across the view at its height in the post-chain grades (the lead
//    files it; spikes/demo-scenes-1/bistro-streak/).
//  - TWO PROJECTS from one run (the interior set sits inside the exterior's corner cafe, aligned):
//    "Bistro" — the exterior by day (the sun on the cafe front, ORCA's Exterior_1 pose), and
//    "Bistro Interior" — the dining room by the same day, opened up 2 stops (ORCA's Interior_1 pose).
//    Both carry both models. A DUSK interior is not built: it needs the lamps as lights, and EVERY
//    punctual light (point or spot) paints the post-chain band on this pin, so the four FBX lights
//    are written (as downward spots) but switched off by interiorLights: false until it is fixed.
// The table below is GENERATED (demo_bistro_textures.py --js <png dir>), keyed by the texture
// prefix `foo` of Foo_BaseColor — the identity a placed node keeps (its baseColorMap's asset name).
var SRC = "@SRC@", OUT = "@OUT@";
var DEMO_SHOT_W = 0, DEMO_SHOT_H = 0, DEMO_PLAN = false;   // 0 = 1920 x 1080
var EMISSIVE_INTENSITY = 1.5, INTERIOR_POINT_INTENSITY = 1.5;
var J = JSON.stringify;
function fail(m) { throw new Error("bistro: " + m); }
function log() { console.log(Array.prototype.join.call(arguments, " ")); }

//@TABLE-BEGIN (generated; do not edit by hand)
var BISTRO_MAPS = {"antenna_metal":{"c":0.0,"s":1},"antenna_plastic":{"c":0.0,"s":1},"antenna_plastic_blue":{"c":0.0,"s":1},"ashtray":{"c":0.0,"s":1},"awnings_beams":{"c":0.0,"s":1},"awnings_fabric":{"c":0.0,"s":1},"awnings_hotel_fabric":{"c":0.0,"s":1},"balcony_concrete":{"c":0.0,"s":1},"balcony_green_wood":{"c":0.0,"s":1},"balcony_ornaments":{"c":0.0,"s":1},"balcony_trims":{"c":0.0,"s":1},"banner_metal":{"c":0.0,"s":1},"bistro_sign_letters":{"c":0.0,"s":1},"bistro_sign_main":{"c":0.0,"s":1},"bollards":{"c":0.0,"s":1},"chimneys_metal":{"c":0.0,"s":1},"cloth":{"c":0.0,"s":1},"concrete":{"c":0.0,"s":1},"concrete2":{"c":0.0,"s":1},"concrete3":{"c":0.0,"s":1},"concrete_striped":{"c":0.0,"s":1},"cookiejar_cookies":{"c":0.0,"s":1},"curtaina":{"c":0.0,"s":1},"curtainb1":{"c":0.0,"s":1},"cutlery_chrome":{"c":0.0,"s":1},"cutlery_details":{"c":0.0,"s":1},"electricbox":{"c":0.0,"s":1},"emissive_streetlight":{"c":0.0,"e":[1.0,0.569,0.278],"s":1},"foliage_bux_hedges46":{"c":0.426,"s":1},"foliage_flowers":{"c":0.742,"s":1},"foliage_ivy_branches":{"c":0.0,"s":1},"foliage_ivy_leaf_a":{"c":0.515,"s":1},"foliage_leaves":{"c":0.756,"s":1},"foliage_linde_tree_large_green_leaves":{"c":0.742,"s":1},"foliage_linde_tree_large_orange_leaves":{"c":0.742,"s":1},"foliage_linde_tree_large_trunk":{"c":0.0,"s":1},"foliage_paris_flowers":{"c":0.0,"s":1},"foliage_trunk":{"c":0.0,"s":1},"lantern":{"c":0.117,"s":1},"lanternemissive":{"c":0.117,"e":[0.177,0.177,0.177],"s":1},"m_statueglass":{"c":0.0,"s":1},"master_awning_beams":{"c":0.0,"s":1},"master_awning_fabric_cyan":{"c":0.0,"s":1},"master_awning_fabric_red":{"c":0.0,"s":1},"master_bistro_main_door":{"c":0.0,"s":1},"master_black_metal":{"c":0.0,"s":1},"master_book_covers":{"c":0.0,"s":1},"master_boulangerie":{"c":0.0,"s":1},"master_brick_large_beige_blendshader":{"c":0.0,"s":1},"master_brick_large_white":{"c":0.0,"s":1},"master_brick_small_red":{"c":0.0,"s":1},"master_brick_small_red_blendshader":{"c":0.0,"s":1},"master_bronze_blendshader":{"c":0.0,"s":1},"master_building_details":{"c":0.0,"s":1},"master_concrete":{"c":0.0,"s":1},"master_concrete1":{"c":0.0,"s":1},"master_concrete_grooved":{"c":0.0,"s":1},"master_concrete_plaster":{"c":0.0,"s":1},"master_concrete_plaster1_blendshader":{"c":0.0,"s":1},"master_concrete_smooth":{"c":0.0,"s":1},"master_concrete_smooth_blendshader":{"c":0.0,"s":1},"master_concrete_white":{"c":0.0,"s":1},"master_concrete_yellow":{"c":0.0,"s":1},"master_curtains":{"c":0.331,"s":1},"master_details_dark":{"c":0.0,"s":1},"master_doors":{"c":0.0,"s":1},"master_focus":{"c":0.0,"s":1},"master_focus_glass":{"c":0.0,"s":1},"master_focus_ornament":{"c":0.0,"s":1},"master_forge_metal":{"c":0.854,"s":1},"master_frosted_glass":{"c":0.0,"s":1},"master_glass_dirty":{"c":0.0,"s":1},"master_glass_dirty_masked":{"c":1.0,"s":1},"master_glass_exterior":{"c":0.0,"s":1},"master_grain_metal":{"c":0.0,"s":1},"master_interior_01_brushed_metal":{"c":0.0,"s":1},"master_interior_01_floor_tile_hexagonal_blendshader":{"c":0.0,"s":1},"master_interior_01_frozen_glass":{"c":0.0,"s":1},"master_interior_01_grid":{"c":0.0,"s":1},"master_interior_01_grid1":{"c":0.0,"s":1},"master_interior_01_material":{"c":0.0,"s":1},"master_interior_01_paris_bartrim":{"c":0.0,"s":1},"master_interior_01_paris_lantern":{"c":0.117,"e":[0.177,0.177,0.177],"s":1},"master_interior_01_plaster":{"c":0.0,"s":1},"master_interior_01_plaster2":{"c":0.0,"s":1},"master_interior_01_plaster_red":{"c":0.0,"s":1},"master_interior_01_white_plastic":{"c":0.0,"s":1},"master_interior_01_wood":{"c":0.0,"s":1},"master_interior_01_wooden_stuco":{"c":0.0,"s":1},"master_light_bulb":{"c":0.0,"s":1},"master_metal":{"c":0.0,"s":1},"master_metal_pipe":{"c":0.0,"s":1},"master_plastic":{"c":0.0,"s":1},"master_rollup_door":{"c":0.0,"s":1},"master_roofing_metal_01":{"c":0.0,"s":1},"master_roofing_shingle_green":{"c":0.0,"s":1},"master_roofing_shingle_grey":{"c":0.0,"s":1},"master_room_interior":{"c":0.0,"s":1},"master_side_letters":{"c":0.653,"s":1},"master_trim_cornice":{"c":0.0,"s":1},"master_wood_brown":{"c":0.0,"s":1},"master_wood_painted3":{"c":0.0,"s":1},"master_wood_painted_cyan":{"c":0.0,"s":1},"master_wood_painted_green":{"c":0.0,"s":1},"master_wood_painted_green_blendshader":{"c":0.0,"s":1},"master_wood_polished":{"c":0.0,"s":1},"menusign_01":{"c":0.0,"s":1},"menusign_02_glass":{"c":0.0,"s":1},"menusign_02_mesh":{"c":0.0,"s":1},"metal_bronze_01":{"c":0.0,"s":1},"metal_chrome1":{"c":0.0,"s":1},"metal_worn_01":{"c":0.0,"s":1},"napkinholder":{"c":0.0,"s":1},"napkinholder_01":{"c":0.0,"s":1},"paris_barstool":{"c":0.0,"s":1},"paris_beertap":{"c":0.0,"s":1},"paris_cashregister":{"c":0.0,"s":1},"paris_cashregister_buttons":{"c":0.0,"s":1},"paris_cashregister_glass":{"c":0.0,"s":1},"paris_ceiling_lamp":{"c":0.0,"e":[0.045,0.045,0.045],"s":1},"paris_ceilingfan":{"c":0.0,"e":[0.035,0.03,0.02],"s":1},"paris_chair_01":{"c":0.0,"s":1},"paris_coasters_01":{"c":0.0,"s":1},"paris_doormat":{"c":0.0,"s":1},"paris_liquorbottle_01_caps":{"c":0.0,"s":1},"paris_liquorbottle_01_glass":{"c":0.0,"s":1},"paris_liquorbottle_01_glass_wine":{"c":0.0,"s":1},"paris_liquorbottle_01_labels":{"c":1.0},"paris_liquorbottle_02_glass":{"c":0.0,"s":1},"paris_liquorbottle_03_glass":{"c":0.0,"s":1},"paris_painting_metal":{"c":0.0,"s":1},"paris_paintings":{"c":0.0,"s":1},"paris_paintings_glass":{"c":0.0,"s":1},"paris_radiator":{"c":0.0,"s":1},"paris_streetpivot":{"c":0.0,"s":1},"paris_streetsign_01":{"c":0.0,"s":1},"paris_stringlights_01_blue_color":{"c":0.0,"s":1},"paris_stringlights_01_blue_color_emissive":{"c":0.0,"e":[0.0,0.137,1.0],"s":1},"paris_stringlights_01_green_color":{"c":0.0,"s":1},"paris_stringlights_01_green_color_emissive":{"c":0.0,"e":[0.0,1.0,0.02],"s":1},"paris_stringlights_01_orange_color":{"c":0.0,"s":1},"paris_stringlights_01_orange_color_emissive":{"c":0.0,"e":[1.0,0.459,0.075],"s":1},"paris_stringlights_01_pink_color":{"c":0.0,"s":1},"paris_stringlights_01_pink_color_emissive":{"c":0.0,"e":[1.0,0.008,0.776],"s":1},"paris_stringlights_01_red_color":{"c":0.0,"s":1},"paris_stringlights_01_red_color_emissive":{"c":0.0,"e":[1.0,0.004,0.0],"s":1},"paris_stringlights_01_white_color":{"c":0.0,"s":1},"paris_stringlights_01_white_color_emissive":{"c":0.0,"e":[1.0,1.0,1.0],"s":1},"paris_table_03":{"c":0.0,"s":1},"paris_table_cloth_01":{"c":1.0},"paris_table_terrace":{"c":0.0,"s":1},"paris_trafficsign_a":{"c":0.0,"s":1},"paris_wall_bulb_light":{"c":0.0,"s":1},"paris_wall_light_interior":{"c":0.0,"e":[0.303,0.303,0.303],"s":1},"pavement_brick_blendshader":{"c":0.0,"s":1},"pavement_cobble_leaves_blendshader":{"c":0.0,"s":1},"pavement_cobblestone_01_blendshader":{"c":0.0,"s":1},"pavement_cobblestone_02":{"c":0.0,"s":1},"pavement_cobblestone_big_blendshader":{"c":0.0,"s":1},"pavement_cobblestone_small_blendshader":{"c":0.0,"s":1},"pavement_cobblestone_wet_blendshader":{"c":0.0,"s":1},"pavement_cobblestone_wet_leaves_blendshader":{"c":0.0,"s":1},"pavement_curbstones":{"c":0.0,"s":1},"pavement_ground_wet":{"c":0.0,"s":1},"pavement_manhole_cover":{"c":0.0,"s":1},"plantpots":{"c":0.0,"s":1},"plants_metal_base_01":{"c":0.0,"s":1},"plants_plants":{"c":0.643,"s":1},"plaster":{"c":0.0,"s":1},"plastic_01":{"c":0.0,"s":1},"plastic_02":{"c":0.0,"s":1},"plates_ceramic":{"c":0.0,"s":1},"plates_details":{"c":0.492,"s":1},"rubber_bar_mat_01":{"c":0.0,"s":1},"shopsign_bakery":{"c":0.0,"s":1},"shopsign_book_store":{"c":0.0,"s":1},"shopsign_book_store_emissive":{"c":0.0,"e":[0.74,0.403,0.404],"s":1},"shopsign_pharmacy":{"c":0.0,"s":1},"shopsign_pharmacy_emissive":{"c":0.0,"e":[0.121,0.16,0.12],"s":1},"shopsign_ties_shop":{"c":0.0,"s":1},"sidewalkbarrier":{"c":0.0,"s":1},"spotlight_emissive":{"c":0.0,"s":1},"spotlight_glass":{"c":0.0,"s":1},"spotlight_glass_emissive":{"c":0.0,"e":[1.0,1.0,1.0],"s":1},"spotlight_main":{"c":0.0,"s":1},"streetlight_chains":{"c":0.0,"s":1},"streetlight_metal":{"c":0.0,"s":1},"streetlight_support_bulb":{"c":0.0,"s":1},"stringlights":{"c":0.0,"s":1},"toffeejar_label":{"c":0.0,"s":1},"toffeejar_metal":{"c":0.0,"s":1},"toffeejar_toffee":{"c":0.0,"s":1},"transparentglass":{"c":0.0,"s":1},"trashcan":{"c":0.0,"s":1},"trolley_plastic":{"c":0.0,"s":1},"trolley_wheels":{"c":0.0,"s":1},"trolley_wood_painted":{"c":0.0,"s":1},"vespa":{"c":0.0,"s":1},"vespa_headlight":{"c":0.0,"s":1},"vespa_odometer":{"c":0.0,"s":1},"vespa_odometer_glass":{"c":0.0,"s":1},"wall_lamp":{"c":0.0,"s":1},"wickerbasket":{"c":0.0,"s":1},"wood":{"c":0.0,"s":1}};
//@TABLE-END

var PROJECTS = [
    {name: "Bistro", sunElevation: 61, sunAzimuth: 270, ev: 0.5, interiorLights: false,
     views: [
        {name: "first", position: {x: 14.0, y: 5.0, z: 22.0}, lookAt: {x: 2.0, y: 0.5, z: 4.0}},   // ORCA Exterior_1
        {name: "walk1", position: {x: 30, y: 2, z: 25}, lookAt: {x: 5, y: 3, z: 0}},
        {name: "walk2", position: {x: 0, y: 1.7, z: 20}, lookAt: {x: 0, y: 2, z: 0}}]},
    {name: "Bistro Interior", sunElevation: 48, sunAzimuth: 270, ev: 2.0, interiorLights: false,
     views: [
        {name: "first", position: {x: 12.0, y: 1.7, z: 4.0}, lookAt: {x: 3.0, y: 1.3, z: -8.0}},   // ORCA Interior_1
        {name: "walk1", position: {x: 2, y: 1.7, z: -2}, lookAt: {x: 8, y: 1.6, z: -10}},
        {name: "walk2", position: {x: 5, y: 1.7, z: -3}, lookAt: {x: 6, y: 1.5, z: -8}}]}
];

// ---- the model door, once: a library row an earlier run made is reused ----
var timing = {}, tAll = Date.now();
function importModel(file, rowName) {
    var t0 = Date.now(), lib = null;
    var have = assets.list({scope: "store", type: "object", query: rowName});
    for (var h = 0; h < have.length && !lib; ++h)
        if (have[h].name === rowName && assets.metadata(have[h].guid).textures > 0 &&
            assets.importSettings(have[h].guid).settings.skeleton !== false) lib = have[h].guid;
    var reused = !!lib;
    // THE DEFAULT DOOR, deliberately (measured both arms, DEMO-SCENES-1): the FBX carries an AnimationStack,
    // so the default import rigs both models and every node resolves MOVABLE (reason "skeleton"; a
    // "static" pin does not override it) — the voxel cascades and cards then hold nothing and the screen
    // gather lights the set, which reads like ORCA's references. Imported {skeleton: false, clips: false}
    // the set is static, 4 cascades build, the cards overflow their atlas (1066 cards on 256 of 256
    // pages, never at rest) and the picture goes dark and grey with white edges
    // (spikes/demo-scenes-1/bistro/static-import-arm/): the lead's call, filed.
    if (!lib) lib = assets.import(SRC + "/" + file, {units: "auto"});
    var meta = assets.metadata(lib);
    timing[rowName] = {reused: reused, importMs: Date.now() - t0, triangles: meta.triangles, meshes: meta.meshes,
                       materials: meta.materials, textures: meta.textures, extent: meta.extent};
    return lib;
}

var texName = {};
function nameOfTexture(guid) {
    if (!guid) return "";
    if (texName[guid] === undefined) {
        var m = assets.metadata(guid);
        texName[guid] = ((m && m.name) ? String(m.name) : "").replace(/\.[^.]*$/, "").toLowerCase();
    }
    return texName[guid];
}
var imported = {};      // each prep picture imported ONCE for both projects
function tex(file) {
    if (imported[file] === undefined) {
        try { imported[file] = assets.importFile(SRC + "/Textures/" + file); }
        catch (e) { log("texture refused", file, e); imported[file] = null; }
    }
    return imported[file];
}
function hex(c) { return "#" + c.map(function (x) {
    var v = x <= 0.0031308 ? 12.92 * x : 1.055 * Math.pow(x, 1 / 2.4) - 0.055;
    var s = Math.round(Math.max(0, Math.min(1, v)) * 255).toString(16); return s.length < 2 ? "0" + s : s; }).join(""); }

// ---- the polish: Falcor's packing into our material, per node ----
function polish(rootId, stats) {
    var rows = scene.nodes({subtree: rootId});
    for (var i = 0; i < rows.length; ++i) {
        var row = rows[i];
        if (row.type !== "mesh") continue;
        var m;
        try { m = material.get(row.id); } catch (e) { continue; }
        stats.meshes++;
        var baseAsset = m.textureAssets ? m.textureAssets.baseColorMap : "";
        var key = nameOfTexture(baseAsset).replace(/_basecolor$/, "");
        var t = BISTRO_MAPS[key];
        if (!t) { stats.unmapped++; continue; }
        stats.mapped++;
        var srcName = String(assets.metadata(baseAsset).name).replace(/\.[^.]*$/, "").replace(/_BaseColor$/i, "");
        var set = {};
        if (t.s) {          // Specular: G = roughness, B = metalness (R occlusion: no slot)
            var r = tex(srcName + "_Specular_roughness.png"), mt = tex(srcName + "_Specular_metallic.png");
            if (r) { set.roughnessMap = r; set.roughness = 1.0; }
            if (mt) { set.metallicMap = mt; set.metallic = 1.0; }
            stats.packed++;
        }
        if (t.c > 0.02) { set.alphaMode = 1; set.alphaCutoff = 0.5; stats.cutouts++; }   // BaseColor alpha = opacity
        if (t.e) {
            var em = tex(srcName + "_Emissive.png");
            if (em) { set.emissiveMap = em; set.emissiveColor = "#ffffff"; set.emissiveIntensity = EMISSIVE_INTENSITY;
                      stats.emissive++; }
        }
        if (!material.set(row.id, set)) log("material.set refused", row.name, J(set));
        if (t.c > 0.02) node.setProperty(row.id, "faceCullingMode", "none");
    }
}

function look(v) { return editor.setCamera({position: v.position, lookAt: v.lookAt}); }
var extLib = importModel("BistroExterior.fbx", "BistroExterior");
var intLib = importModel("BistroInterior.fbx", "BistroInterior");
log("import", J(timing));

for (var pi = 0; pi < PROJECTS.length; ++pi) {
    var P = PROJECTS[pi];
    var t0 = Date.now();
    project.create(project.nextFreeName(P.name), {template: "basic"});
    var floor = scene.find("Floor");
    if (floor) node.remove(floor);       // the street is the model's own ground
    var ext = assets.addToScene(assets.addToProject(extLib), {position: {x: 0, y: 0, z: 0}});
    var inn = assets.addToScene(assets.addToProject(intLib), {position: {x: 0, y: 0, z: 0}});   // aligned with the cafe
    node.rename(ext, "Bistro Exterior"); node.rename(inn, "Bistro Interior");
    var stats = {meshes: 0, mapped: 0, unmapped: 0, packed: 0, cutouts: 0, emissive: 0};
    polish(ext, stats); polish(inn, stats);
    log("mobility", P.name, J(node.mobility(ext)));

    // The sun: the FBX's directionalLight1 colour (0.73/0.58/0.38, normalised); its 61-degree noon is
    // kept for the exterior (61 deg, the source's own) and the interior's daylight. Rotation x = 90 - elevation (measured); below 0 is dusk.
    var sun = world.sunLight();
    if (!sun) fail("the Basic template brought no sun");
    node.transform(sun, {rotation: {x: 90 - P.sunElevation, y: P.sunAzimuth, z: 0}});
    node.setProperty(sun, "lightColor", hex([1.0, 0.794, 0.514]));
    world.shadows({enabled: true});
    world.postFx({exposureEv: P.ev});
    // The FBX's four interior point lights (metres, warm 0.70/0.57/0.31; Maya's 40000 has no unit here).
    var lights = 0;
    var FBX_POINTS = [[4.991, 3.031, -0.489], [7.989, 3.074, 1.015], [4.263, 3.095, -4.920], [6.107, 2.939, -8.468]];
    if (P.interiorLights) for (var k = 0; k < FBX_POINTS.length; ++k) {
        // A downward SPOT (each FBX point light hangs where a pendant lamp is): on this pin a POINT light
        // paints a horizontal band across the post-chain grades at its height (the lead files it).
        var id = scene.addLight("spot", {position: {x: FBX_POINTS[k][0], y: FBX_POINTS[k][1], z: FBX_POINTS[k][2]},
                                         rotation: {x: 0, y: 0, z: 0}});
        node.rename(id, "pointLight" + (k + 1) + " (spot)");
        node.setProperty(id, "lightColor", hex([1.0, 0.807, 0.442]));
        node.setProperty(id, "intensity", INTERIOR_POINT_INTENSITY);
        node.setProperty(id, "distance", 8);
        node.setProperty(id, "spotCutOff", 70);
        node.setProperty(id, "spotCutOffSoftness", 0.5);
        lights++;
    }
    world.mode({mode: "high"});
    world.photon({enabled: true, tier: "high"});
    look(P.views[0]);
    if (project.save() !== true) fail("save failed: " + P.name);
    var buildMs = Date.now() - t0;

    var W = DEMO_SHOT_W || 1920, H = DEMO_SHOT_H || 1080;
    var slug = P.name.replace(/ /g, "_").toLowerCase();
    for (var v = 0; v < P.views.length; ++v) {
        look(P.views[v]);
        var shot = editor.screenshot(OUT + "/" + slug + "_" + P.views[v].name + ".png", W, H, [], "scene");
        log("shot", P.name, P.views[v].name, J(shot.center));
    }
    look(P.views[0]);
    editor.frame(120);
    var rs = app.renderStats(), gs = world.giStatus();
    log("REPORT", P.name, J({buildMs: buildMs, polish: stats, lights: lights, frameMs: rs.frameMs,
        sceneTriangles: rs.sceneTriangles, giAtRest: gs.giAtRest, cascades: (gs.cascades || []).length,
        cards: gs.cards ? {cards: gs.cards.cards, pagesUsed: gs.cards.pagesUsed, pages: gs.cards.pages} : null,
        mode: gs.mode}));
}
log("TOTAL", J({timing: timing, totalMs: Date.now() - tAll}));
0;
