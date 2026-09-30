// scripting.e2e.new_scene — THE NEW-SCENE VERB AND ITS OPTIONS (owner review
// R1; the TEMPLATES, WORLD-MODEL-1, owner 2026-09-30: "a drop-down of templates
// like Unreal's").
//
// The New Scene dialog has no capability of its own: its Template drop-down
// and its Browse button are `project.create(name, {template, location})`,
// which is what this drives. In the order a user meets them:
//
//   1. BASIC, the default — ONE ordinary floor node "Floor" (the baked cube
//      primitive, 100 x 1 x 100 m, its top face at y = 0, on Atom), the sun, a
//      Sky Light and the REALISTIC real-time sky with the sun following the
//      atmosphere.
//   2. EMPTY — the root node and NOTHING ELSE: no floor, no lights, no sky.
//   3. WORLD — Basic's sky and lights on a group "World Floor" of 25 of Basic's
//      floor cubes, 5 x 5, edge to edge: 500 x 500 m.
//   4. `{location}` — the project folder lands under the folder that was named.
//   1b. THE DEFAULT LOOK (SKY-DEFAULTS-1): the sun 50 degrees up behind the
//      default camera, an 18 % card at mid-grey, the Height Fog on — near
//      surfaces untouched, the horizon smooth, the sky under it blue, the
//      World's far edge fading.
//   5. THE REFUSALS, BY NAME and BEFORE anything is written: a bad location, an
//      empty name, an unknown key — `empty`, the retired option, is one now —
//      and an unknown template.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function J(x) { return JSON.stringify(x); }

function typesOf(ids) {
    var out = [];
    for (var i = 0; i < ids.length; ++i) out.push(ids[i].type + ":" + ids[i].name);
    return out;
}

function near(a, b, tol) { return Math.abs(a - b) <= tol; }
// The frames it takes a new world's baked meshes and textures to reach the
// renderer's split: counted, never timed.
function settleAtom() {
    var st = world.atomStatus();
    for (var i = 0; i < 120 && st.live && st.pending > 0; ++i) { editor.frame(1, 1 / 60); st = world.atomStatus(); }
    return st;
}
function lightsIn(rows) {
    return rows.filter(function (r) { return r.type === "light"; }).length;
}

// ---- 1. Basic, the default ----------------------------------------------------
var proj = project.create("New Scene Template " + Date.now());
assert(proj.length > 10, "project.create with no options");

var w = world.get();
console.log("world sky: " + J(w.sky));
assert(String(w.sky.type) === "Realistic",
       "the default new scene's sky is the REALISTIC real-time sky (owner Q1)");

var sun = world.sun();
console.log("sun: " + J({ light: sun.light, name: sun.name, reason: sun.reason }));
assert(sun && sun.light && sun.light.length > 0,
       "...and the scene has a sun (the first directional light)");
assert(node.property(sun.light, "followsAtmosphere") === true,
       "...with Sun Follows Atmosphere ON — which is what the realistic sky makes mean something");

var rows = scene.nodes({ depth: 1 });
console.log("basic: " + J(typesOf(rows)));
assert(!!scene.find("Sky Light"), "...and carries the Sky Light");
assert(lightsIn(scene.nodes()) === 2, "Basic holds exactly TWO lights (the sun and the Sky Light)");
var floorRows = rows.filter(function (r) { return r.name === "Floor"; });
assert(floorRows.length === 1 && floorRows[0].type === "mesh",
       "Basic stands on ONE floor node, a mesh named Floor");
var floor = floorRows[0].id;
assert(node.property(floor, "meshPath") === ":/content/primitives/cube.obj",
       "...and it is the CUBE primitive, the baked library asset (" + node.property(floor, "meshPath") + ")");
assert(!scene.find("Ground"), "...and there is no hidden 'Ground' any more");
var fb = scene.bounds({ nodes: [floor] });
console.log("floor bounds: " + J(fb));
assert(near(fb.size.x, 100, 0.01) && near(fb.size.y, 1, 0.01) && near(fb.size.z, 100, 0.01),
       "the floor is 100 x 1 x 100 m");
assert(near(fb.max.y, 0, 1e-3), "...with its TOP FACE at y = 0 (" + fb.max.y + ")");
assert(near(fb.center.x, 0, 1e-3) && near(fb.center.z, 0, 1e-3), "...centred on the origin");
// AN ORDINARY NODE THAT SHIPS LOCKED (owner, 2026-09-15, restated 2026-09-30):
// not pickable, so a click on the empty floor selects nothing and a drop on it
// is refused by name; the verb (the outliner's lock) unlocks it.
assert(node.property(floor, "pickable") === false, "...and it ships LOCKED (pickable false)");
editor.setCamera({ position: { x: 0, y: 10, z: 12 }, lookAt: { x: 0, y: 0, z: 0 } });
editor.frame(4, 1 / 60);
var vs = editor.viewportState();
var cx = vs.width * 0.5, cy = vs.height * 0.6;
assert(editor.clickTargetAt(cx, cy) == null,
       "a viewport click on the empty (locked) floor selects NOTHING");
var dropOnFloor = editor.dropTargetAt(cx, cy);
assert(dropOnFloor != null && dropOnFloor.id === floor && dropOnFloor.locked === true,
       "...and a drop there names the Floor as LOCKED (" + J(dropOnFloor) + ")");
assert(node.setProperty(floor, "pickable", true) === true, "unlock it through the verb (the outliner's lock)");
editor.frame(2, 1 / 60);
var clicked = editor.clickTargetAt(cx, cy);
assert(clicked != null && clicked.id === floor, "...unlocked, the same click selects the Floor (" + J(clicked) + ")");
assert(node.setProperty(floor, "pickable", false) === true, "lock it again, as it ships");
var sb = scene.bounds();
assert(sb.size.x >= 100 - 0.01, "...which the scene's own bounds count (" + J(sb.size) + ")");
// ON ATOM: the id pass draws it and the decode shades it.
editor.frame(10, 1 / 60);
var atomBasic = settleAtom();
console.log("basic atom: " + J({ live: atomBasic.live, on: atomBasic.on, atomItems: atomBasic.atomItems,
                                 pbsItems: atomBasic.pbsItems, notWorld: atomBasic.notWorld,
                                 pending: atomBasic.pending }));
if (atomBasic.live && atomBasic.on) {
    assert(atomBasic.atomItems >= 1, "the floor is ON ATOM (atomItems " + atomBasic.atomItems + ")");
    assert(node.setProperty(floor, "visible", false), "hide the floor");
    editor.frame(3, 1 / 60);
    var atomHidden = world.atomStatus();
    assert(atomHidden.atomItems === atomBasic.atomItems - 1,
           "...and it is the floor that atomItems counts (" + atomBasic.atomItems + " -> " +
           atomHidden.atomItems + " with it hidden)");
    assert(node.setProperty(floor, "visible", true), "show it again");
} else {
    console.log("note: the Atom split is not live on this machine (on=" + atomBasic.on +
                ") — the floor's route is not asserted here");
}

// ---- 1b. THE DEFAULT LOOK (SKY-DEFAULTS-1) ------------------------------------
// The owner's Unreal Basic level: the sun high and behind the default camera,
// an 18 % card on the floor at the film's mid-grey, and the Exponential Height
// Fog on — the world's medium, beyond 100 m, over the sky under the horizon.
// Colours are measured in the PLAIN grade's float radiance (linear, no chain);
// the grey card in the "tonemap" grade (the scene's exposure as a constant).
function lum(c) { return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b; }
function dist(a, b) { return Math.sqrt((a.r - b.r) * (a.r - b.r) + (a.g - b.g) * (a.g - b.g) + (a.b - b.b) * (a.b - b.b)); }
function tanD(d) { return Math.tan(d * Math.PI / 180); }
// A probe at `el` degrees of elevation for a LEVEL camera (fov 45, 16:9 shot).
function rowAt(el) { return 0.5 - 0.5 * tanD(el) / tanD(22.5); }
function plain(tag, probes) {
    editor.frame(6, 1 / 60);
    return editor.screenshot("new_scene_" + tag + ".png", 960, 540, probes, "plain", { radiance: true }).probes;
}
editor.setCamera({ position: { x: 0, y: 5, z: 14 }, lookAt: { x: 0, y: 0, z: 0 } });   // the default pose
var sunDir = world.sun().direction;
var sunElev = Math.asin(-sunDir.y) * 180 / Math.PI;
assert(Math.abs(sunElev - 50) <= 1, "the sun stands at 50 degrees of elevation (" + sunElev.toFixed(2) + ")");
// The default camera looks down -Z; the sun stands where its light comes FROM
// (-direction): behind the camera is +Z.
assert(-sunDir.z > 0.5 && Math.abs(sunDir.x) < 1e-3,
       "...and BEHIND the default camera, on its back azimuth (towards the sun " + J({ x: -sunDir.x, z: -sunDir.z }) + ")");
var hf = world.heightFog();
console.log("height fog: " + J(hf));
assert(hf.enabled === true && near(hf.density, 0.02, 1e-6) && near(hf.heightFalloff, 0.2, 1e-6) &&
       near(hf.baseHeight, 0, 1e-6) && near(hf.startDistance, 100, 1e-4),
       "the Height Fog is ON at Unreal's density 0.02 / falloff 0.2, from 100 m");
assert(world.get().fog.enabled === false, "...and the World (scene) fog stays OFF");
assert(hf.live && hf.live.on === true && hf.live.colourFromSky === true,
       "...the renderer draws it, coloured by the sky it captured (" + J(hf.live) + ")");
var fogC = { r: hf.live.colour[0], g: hf.live.colour[1], b: hf.live.colour[2] };
assert(fogC.b > fogC.r * 1.5, "...a sky BLUE (" + J(fogC) + ")");
// THE GREY CARD: an 18 % card on the floor under the default lights develops at
// the film's mid-grey (display 0.18 = code 46) within 1/3 of a stop (35..59).
var card = scene.addPrimitive("plane", { position: { x: 0, y: 0.02, z: 0 } });
material.set(card, { baseColor: "#767676", roughness: 1.0, metallic: 0.0 });
editor.selectNone();
editor.frame(10, 1 / 60);
var cardShot = editor.screenshot("new_scene_card.png", 480, 270, [{ x: 0.5, y: 0.5 }], "tonemap").probes[0];
var cardCode = 0.2126 * cardShot.r + 0.7152 * cardShot.g + 0.0722 * cardShot.b;
assert(cardCode >= 35 && cardCode <= 59,
       "an 18% card on the Basic floor reads the film's mid-grey within 1/3 stop (" + cardCode.toFixed(1) + " of 35..59)");
// NEARER THAN THE START DISTANCE NOTHING MOVES: the card, fog on and off.
var nearOn = plain("near_on", [{ x: 0.5, y: 0.5 }])[0].radiance;
world.heightFog({ enabled: false });
var nearOff = plain("near_off", [{ x: 0.5, y: 0.5 }])[0].radiance;
world.heightFog({ enabled: true });
assert(dist(nearOn, nearOff) === 0, "a surface nearer than the start distance is IDENTICAL with the fog on and off (" +
       J(nearOn) + " vs " + J(nearOff) + ")");
node.remove(card);
// THE HORIZON, level at 2 m with the floor hidden: the sky alone.
editor.setCamera({ position: { x: 0, y: 2, z: 14 }, lookAt: { x: 0, y: 2, z: -1000 } });
node.setProperty(floor, "visible", false);
var els = [1, 1, 0.5, -5];   // [0] unused (30 degrees is read below, pitched)
var col = [];
for (var k = 0; k <= 40; ++k) col.push({ x: 0.5, y: 0.40 + k * 0.005 });
var probes = els.map(function (e) { return { x: 0.5, y: rowAt(e) }; }).concat(col);
var on = plain("horizon_on", probes);
world.heightFog({ enabled: false });
var off = plain("horizon_off", probes);
world.heightFog({ enabled: true });
node.setProperty(floor, "visible", true);
// 30 degrees up is off a level frame (its top edge is 22.5): a camera pitched
// up 30 degrees reads it at the centre.
editor.setCamera({ position: { x: 0, y: 2, z: 14 }, lookAt: { x: 0, y: 2 + tanD(30) * 100, z: 14 - 100 } });
var at30on = plain("up30_on", [{ x: 0.5, y: 0.5 }])[0].radiance;
world.heightFog({ enabled: false });
var at30off = plain("up30_off", [{ x: 0.5, y: 0.5 }])[0].radiance;
world.heightFog({ enabled: true });
var d30 = Math.abs(lum(at30on) - lum(at30off)) / lum(at30off);
// UNREAL'S DEFAULTS ARE NOT NOTHING AT 30 DEGREES: density 0.02 / falloff 0.2
// put an optical depth of D/(k sin 30) = 0.195 on that ray (12.6 % opacity),
// towards a colour close to the sky's there — measured 3.3 % of luminance. The
// bound is 5 %: the zenith side of the sky stays the sky.
assert(d30 < 0.05, "30 degrees above the horizon the sky moves by under 5% (" + (100 * d30).toFixed(2) + "%)");
var at1on = on[1].radiance, at1off = off[1].radiance;
assert(dist(at1on, fogC) < 0.25 * dist(at1off, fogC),
       "1 degree above the horizon the sky is PULLED towards the fog colour (" + dist(at1on, fogC).toFixed(4) +
       " from it, " + dist(at1off, fogC).toFixed(4) + " with the fog off)");
var below = on[3].radiance, above = on[2].radiance;
assert(below.b - below.r > 0.1, "5 degrees BELOW the horizon is BLUE, b - r = " + (below.b - below.r).toFixed(3) +
       " (> 0.1; the planet's grey band with the fog off: " + (off[3].radiance.b - off[3].radiance.r).toFixed(3) + ")");
assert(dist(below, above) <= 0.05 * Math.sqrt(above.r * above.r + above.g * above.g + above.b * above.b),
       "...and within 5% of the sky just above the horizon (0.5 degrees) — one smooth colour across it");
function maxStep(p) {
    var worst = 0;
    for (var i = els.length + 1; i < p.length; ++i)
        worst = Math.max(worst, dist(p[i].radiance, p[i - 1].radiance));   // colour, not luminance: the
                                                                          // physical step is a hue step
    return worst;
}
var stepOn = maxStep(on), stepOff = maxStep(off);
console.log("horizon column: largest row-to-row colour step " + stepOn.toFixed(4) + " fog on, " +
            stepOff.toFixed(4) + " fog off");
assert(stepOn < 0.5 * stepOff, "the horizon is SMOOTH with the fog on: its largest step is under half the physical " +
       "sky's sharp one (" + stepOn.toFixed(4) + " vs " + stepOff.toFixed(4) + ")");

// ---- 2. Empty ---------------------------------------------------------------
var empty = project.create("New Scene Empty " + Date.now(), { template: "empty" });
assert(empty.length > 10, "project.create({template: \"empty\"})");

var emptyRows = scene.nodes({ depth: 1 });
console.log("empty: " + J(typesOf(emptyRows)));
// scene.nodes() starts AT the root, so depth 1 is the root plus its children.
assert(emptyRows.length === 1,
       "an empty scene is the root node and nothing else (got " + emptyRows.length + " rows)");
assert(!scene.find("Floor"), "...no floor");
assert(lightsIn(scene.nodes()) === 0,
       "...and no lights at all: an empty world is not lit, which is what 'empty' means");
var ew = world.get();
console.log("empty world sky: " + J(ew.sky));
assert(String(ew.sky.type) === "None",
       "...and NO SKY: SkyType::NONE, a black background that lights nothing (SKY-ATMOSPHERE-1)");
assert(world.heightFog().enabled === false, "...and NO Height Fog: Empty is nothing");
// ...AND "NO SKY" SURVIVES A SAVE AND A REOPEN (the merge read's W1): the type is
// written by ordinal and read back as the same value, not as a single colour.
assert(project.save() === true, "the empty project saves");
assert(project.close() === true && project.open(empty) === true, "...closes and reopens");
assert(String(world.get().sky.type) === "None",
       "...and its sky is still NONE after the reopen (" + J(world.get().sky) + ")");

// ---- 3. World ---------------------------------------------------------------
var wp = project.create("New Scene World " + Date.now(), { template: "world" });
assert(wp.length > 10, "project.create({template: \"world\"})");
var group = scene.find("World Floor");
assert(!!group, "World holds a group named 'World Floor'");
var tiles = scene.nodes().filter(function (r) { return r.type === "mesh" && /^Floor \d+$/.test(r.name); });
console.log("world floors: " + tiles.length);
assert(tiles.length === 25, "...of 25 floor nodes (Floor 1 .. Floor 25)");
var under = tiles.filter(function (r) { return node.info(r.id).parent === group; });
assert(under.length === 25, "...every one of them under World Floor");
var unlocked = tiles.filter(function (r) { return node.property(r.id, "pickable") !== false; });
assert(unlocked.length === 0 && node.property(group, "pickable") === false,
       "...all 25 floors AND the World Floor group ship LOCKED (" + unlocked.length + " unlocked)");
var wb = scene.bounds({ nodes: [group] });
console.log("world floor bounds: " + J(wb));
assert(near(wb.size.x, 500, 0.05) && near(wb.size.z, 500, 0.05),
       "...5 x 5 of the 100 m floors, edge to edge: 500 x 500 m");
assert(near(wb.max.y, 0, 1e-3) && near(wb.center.x, 0, 0.01) && near(wb.center.z, 0, 0.01),
       "...top at y = 0, centred on the origin");
assert(lightsIn(scene.nodes()) === 2 && String(world.get().sky.type) === "Realistic",
       "...under Basic's sky and its two lights");
assert(world.heightFog().enabled === true, "...and Basic's Height Fog");
// THE FAR EDGE FADES (SKY-DEFAULTS-1): the 250 m edge seen from the default
// camera, just under the horizon, moves towards the fog colour with it on.
editor.setCamera({ position: { x: 0, y: 5, z: 14 }, lookAt: { x: 0, y: 0, z: 0 } });
var pitchW = -Math.atan2(5, 14) * 180 / Math.PI;
var edgeY = 0.5 - 0.5 * tanD(-1.08 - pitchW) / tanD(22.5) + 0.003;
var wf = world.heightFog().live;
var wFog = { r: wf.colour[0], g: wf.colour[1], b: wf.colour[2] };
var edgeOn = plain("world_edge_on", [{ x: 0.5, y: edgeY }])[0].radiance;
world.heightFog({ enabled: false });
var edgeOff = plain("world_edge_off", [{ x: 0.5, y: edgeY }])[0].radiance;
world.heightFog({ enabled: true });
assert(dist(edgeOn, wFog) < 0.85 * dist(edgeOff, wFog),
       "the World's far floor edge FADES into the fog (" + dist(edgeOn, wFog).toFixed(4) + " from its colour, " +
       dist(edgeOff, wFog).toFixed(4) + " without it)");
// ONE TILE, PINNED ONCE: every floor wears its OWN material instance, all naming the
// same pinned checker — one Tile.png row in the project, not twenty-five.
var maps = {};
for (var t = 0; t < tiles.length; ++t) maps[material.get(tiles[t].id).baseColorMap] = true;
assert(Object.keys(maps).length === 1 && String(Object.keys(maps)[0]).length > 0,
       "the 25 floors all wear the one pinned checker (" + J(Object.keys(maps)) + ")");
assert(assets.list({ scope: "project", type: "texture", query: "Tile.png" }).length === 1,
       "...one Tile.png row in the project");
assert(material.set(tiles[0].id, { roughness: 0.3 }) === true &&
       material.get(tiles[1].id).roughness === 1,
       "...and editing one floor's material leaves its neighbour's alone (own instances)");

// ---- 4. a chosen location ---------------------------------------------------
// app.dataRoot() names this run's own folders; `assetStore` is a directory the
// run has certainly created and can certainly write, on every box the gate
// runs on, and it is NOT the projects root — so "it landed where I said"
// distinguishes itself from "it landed where it always does".
var roots = app.dataRoot();
console.log("roots: " + J(roots));
var where = roots.assetStore;
assert(where && where.length > 0, "a location to create into: " + where);
assert(where !== roots.projects, "...that is not the default projects root");

var placed = project.create("New Scene Located " + Date.now(), { location: where });
assert(placed.length > 10, "project.create({location})");
var folder = project.current().folder;
console.log("folder: " + folder);
assert(folder.indexOf(where) === 0,
       "the project folder is under the location that was named, not under the projects root");

// ---- 4b. AND IT CAN BE FOUND AGAIN (fix round F1) ---------------------------
// WHERE IT LANDS IS NOT THE QUESTION — where it is FOUND is. The location is
// recorded with the project row and ProjectService::projectFolderFor is the one
// resolver; before the fix every resolver rebuilt the path from the DEFAULT
// root, so a close and reopen pointed the whole session — baked maps, exports,
// the thumbnail, the delete — at a folder that does not exist.
//
// A file inside the located folder, so the DELETE below can be checked on disk
// and not merely in the catalog.
var proof = folder + "/small-ui-a-proof.zip";
assert(project.exportArchive(proof).path === proof, "an archive written INSIDE the located folder");

assert(project.close() === true, "close the located project");
assert(project.open(placed) === true, "project.open(guid) of a located project");
var reopened = project.current();
console.log("reopened folder: " + reopened.folder);
assert(reopened.guid === placed, "...it is the same project");
assert(reopened.folder.indexOf(where) === 0,
       "...and it comes back pointing UNDER ITS OWN LOCATION, not the default projects root (" +
       reopened.folder + ")");
assert(reopened.folder === folder, "...at exactly the folder it was created in");

// ---- 4c. a located project DELETES its own folder ---------------------------
assert(project.close() === true, "close it again");
assert(project.remove(placed) === true, "project.remove(located)");
var stillListed = project.list().filter(function (p) { return p.guid === placed; });
assert(stillListed.length === 0, "...the row is gone");
var gone = null;
try { project.importArchive(proof); } catch (e) { gone = String(e); }
assert(gone !== null,
       "...AND THE LOCATED FOLDER IS GONE WITH IT — the archive that was inside it " +
       "cannot be read any more. Before the fix the delete removed the DEFAULT root's " +
       "folder of the same guid (nothing) and left this one on disk forever.");
console.log("   -> " + gone);

// ---- 4d. a recorded location that is not there refuses BY NAME --------------
// An unplugged drive, expressed with the verbs alone: the inner project's
// location is the OUTER project's folder, so removing the outer one takes the
// inner one's recorded root with it. Nothing about that arrangement is special
// — it is the same state a disconnected drive leaves behind.
var outer = project.create("New Scene Host " + Date.now());
var outerFolder = project.current().folder;
var inner = project.create("New Scene Orphan " + Date.now(), { location: outerFolder });
assert(project.current().folder.indexOf(outerFolder) === 0, "the inner project lives inside it");
assert(project.close() === true, "close");
assert(project.remove(outer) === true, "remove the project the location was inside");

var orphan = null;
try { project.open(inner); } catch (e) { orphan = String(e); }
assert(orphan !== null, "opening a project whose recorded location is gone is REFUSED");
console.log("   -> " + orphan);
assert(orphan.indexOf(outerFolder) >= 0,
       "...by name, with the missing path in the message — never a silent fall-back to the " +
       "default root (which would open an empty world under the project's own guid) and never " +
       "an auto-created empty folder");
assert(!project.current() || project.current().guid !== inner,
       "...and the refusal did not point the session at it");

// ---- 5. the refusals --------------------------------------------------------
// A world open again first: §4d's refusal deliberately left the session with
// none, and "a refused create leaves the project that was open exactly where it
// was" needs one to mean anything.
assert(project.create("New Scene Refusals " + Date.now()).length > 10,
       "a project to be left alone by the refusals below");
var openBefore = project.current().guid;

function refused(call, what, expect) {
    var threw = null;
    try { call(); } catch (e) { threw = String(e); }
    assert(threw !== null, what + " is refused");
    console.log("   -> " + threw);
    assert(threw.indexOf(expect) >= 0, "...by name (" + expect + ")");
}

refused(function () { project.create("Nowhere", { location: "/definitely/not/here" }); },
        "a location that does not exist", "does not exist");
refused(function () { project.create("   "); },
        "an empty name", "non-empty name");
refused(function () { project.create("Typo", { emtpy: true }); },
        "an unknown option key", "unknown option");
refused(function () { project.create("Retired", { empty: true }); },
        "the retired `empty` option (it is `template: \"empty\"` now)", "unknown option 'empty'");
refused(function () { project.create("Nope", { template: "sample" }); },
        "an unknown template (Sample is not built yet)", "unknown template 'sample'");

assert(project.current().guid === openBefore,
       "a refused create leaves the project that was open exactly where it was");

console.log("PASS scripting.e2e.new_scene");
