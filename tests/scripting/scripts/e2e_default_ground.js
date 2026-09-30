// scripting.e2e.default_ground — THE TEMPLATE'S FLOOR AND THE GROUND PLANE
// WIDGET, IN PIXELS (owner, 2026-09-13: "the ground should not be reflective, it
// should have 0 specular"; WORLD-MODEL-1, 2026-09-30: the hidden built-in ground
// and the painted horizon under it were "a hack" — every floor is an ordinary
// node, and the infinite ground is an EDITOR WIDGET toggled in View Options).
//
// The document half of the floor (its flag, its own material, material.reset)
// is scripting.e2e.default_floor; this suite is what a CAMERA sees:
//
//   1. MATTE. At a grazing angle the Basic template's Floor shows no sheen, and
//      raising Specular Color back to white brings the sheen back — because a
//      mirror floor has to stay possible.
//   2. THE FLOOR ENDS; THE WIDGET DOES NOT. From a high oblique view the frame's
//      corners lie ~176 m out, past the Floor's own 100 m: with the Ground plane
//      OFF (the default) they are SKY; with it ON (editor.setOverlays) they are
//      the CHECKER, and it crosses the Floor's edges in phase (2b).
//   3. AND IT CHANGES NO LIGHTING AND NOTHING ON ATOM: the probe region is the
//      same numbers with the widget as without it, the renderer's split counts
//      the plane under notWorld (a backdrop) and atomItems does not move — the
//      Floor itself IS on Atom.
//   4. The document never hears about it: nothing new in the scene or its
//      bounds; the setting is per scene and survives a save and reopen.
//   5. The widget does not depend on a floor: with the Floor hidden it still
//      grounds the frame, and with both off the frame is the uniform sky.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function J(x) { return JSON.stringify(x); }
function near(a, b, tol) { return Math.abs(a - b) <= tol; }
function lum(p) { return (p.r + p.g + p.b) / 3; }

var proj = project.create("Default Ground " + Date.now());
assert(proj.length > 10, "project.create");
var ground = scene.find("Floor");
assert(ground && node.property(ground, "defaultFloor") === true,
       "a new (Basic) scene stands on its Floor, wearing the default floor material");
assert(editor.overlays().groundPlane === false, "...and the Ground plane widget is OFF by default");

// THE SUITE STATES ITS OWN SKY (owner answer Q1, 2026-09-18). A new scene's sky
// is the REALISTIC atmosphere now, and every number below was baselined against
// the flat 96-grey one: the corner probes ask whether the sky is UNIFORM (an
// atmosphere is a gradient, by definition), the grazing probes ask what a matte
// floor does with a 4% sheen, and the "no floor, no horizon" step asks the bare
// sky to be DARKER than the lit ground (an atmosphere above the horizon is not).
// None of that is what this suite is about — the FLOOR is — so it names the sky
// it measures against instead of inheriting the template's, which is free to
// change again. rgb(96,96,96) is exactly what iris::Scene's constructor sets.
world.sky("color", { color: "#606060" });
editor.frame(3);

// ---- 1. the floor's material is authored matte ------------------------------
var m = material.get(ground);
console.log("floor material: " + J({ workflow: m.workflow, ior: m.ior, specular: m.specularColor,
                                     roughness: m.roughness, metallic: m.metallic }));
assert(m.workflow === 1 && near(m.ior, 1.0, 1e-4),
       "the floor is a SPECULAR-workflow material at ior 1.0 — setFresnel gets ((1-ior)/(1+ior))^2 = 0");
assert(String(m.specularColor).toLowerCase() === "#000000",
       "...and its specular colour (kS, every specular path's multiplier) is black");
assert(near(m.roughness, 1.0, 1e-4) && near(m.metallic, 0.0, 1e-4), "...roughness 1, metallic 0");

// ---- 1b. and it MEASURES matte, at a grazing angle --------------------------
// The camera lies almost in the floor plane looking down it: this is where a 4%
// fresnel term is at its loudest, and it is the view the owner was flying.
var PROBES = [ { x: 0.5, y: 0.55 }, { x: 0.5, y: 0.62 }, { x: 0.5, y: 0.75 }, { x: 0.25, y: 0.9 } ];
function grazing(tag) {
    editor.setCamera({ position: { x: 0, y: 1.6, z: 8 }, lookAt: { x: 0, y: 1.45, z: -20 } });
    editor.frame(30, 1 / 60);
    var s = editor.screenshot("ground_" + tag + ".png", 960, 540, PROBES, "raw");
    var v = s.probes.map(function (p) { return Math.round(lum(p)); });
    console.log("grazing " + tag + ": " + J(v));
    return v;
}
var matte = grazing("matte");

// The way back, and the proof that the probes are looking at a specular signal
// at all: kS white is the master switch (defaultfloormaterial.h says so), and it has to
// brighten every one of these probes.
assert(material.set(ground, { specularColor: "#ffffff", ior: 1.5 }),
       "a user makes the floor reflective again: Specular Color white, IOR 1.5");
var shiny = grazing("shiny");
var brighter = 0;
for (var i = 0; i < matte.length; i++) if (shiny[i] > matte[i]) brighter++;
assert(brighter === matte.length,
       "every grazing probe is BRIGHTER with specular on (" + J(matte) + " -> " + J(shiny) + ")");
// 5/255, RESTORED (lane SKY-FALLBACK-1, 2026-09-15). It was lowered to 3 for a
// day, and the lowering was this defect measured rather than a property of the
// scene: R5-ROOM made the probe grid a per-probe decision, so this project keeps
// a PARTIAL grid — and a partial grid used to take the sky cubemap off every
// datablock (the shader's environment slot has one occupant), leaving the
// grazing floor pixels no probe box contains with nothing but cone tracing.
// fork 4d5fbef16+8f09c0cd4 (was 0048) gives the sky its own pass-level slot and hands it back to
// exactly those pixels; the margin measures 5 again, the number this read for
// the whole life of the case before the regression.
assert(shiny[3] - matte[3] >= 5,
       "...and the near-field one by a visible margin (" + (shiny[3] - matte[3]) + "/255)");

// material.reset takes the matte default back — the reset is the one route the
// owner was told about, and it must restore THIS and not the 2026-09-12 default.
assert(material.reset(ground) === true, "material.reset(the floor)");
var back = material.get(ground);
assert(back.workflow === 1 && near(back.ior, 1.0, 1e-4) &&
       String(back.specularColor).toLowerCase() === "#000000",
       "...restores the matte default (" + J({ workflow: back.workflow, ior: back.ior,
                                               specular: back.specularColor }) + ")");
// In pixels, and measured back to back rather than against the reading taken
// before the detour: the room's bounce keeps converging while a script runs, so
// the honest comparison of "matte" and "shiny" is the one made in the same
// breath — the same reason the floor's own sheen is asserted as a DIFFERENCE.
var again = grazing("reset");
assert(material.set(ground, { specularColor: "#ffffff", ior: 1.5 }), "specular back on, one more time");
var shiny2 = grazing("shiny2");
for (var k = 0; k < again.length; k++)
    assert(shiny2[k] > again[k],
           "the reset floor is the MATTE one, probe " + k + " (" + again[k] + " vs " + shiny2[k] + ")");
assert(material.reset(ground) === true, "reset it back for the rest of the suite");

// ---- 2. the Floor ends; the widget does not ---------------------------------
// A high oblique over the scene: the Floor's own 100 m covers the middle of this
// frame and every corner lies ~176 m out.
var CORNERS = [ { x: 0.06, y: 0.08 }, { x: 0.94, y: 0.08 },
                { x: 0.06, y: 0.92 }, { x: 0.94, y: 0.92 },
                { x: 0.5,  y: 0.06 }, { x: 0.5,  y: 0.94 } ];
// FOG OFF, and the air's haze with it (world.sky's aerialScale is only the
// realistic sky's; this suite's sky is a flat colour): "is this pixel ground or
// sky" has to be answerable at the top of the frame.
assert(world.fog({ enabled: false }), "fog off, so ground and sky can be told apart");
// THE SKY IS MADE UNMISTAKABLE for these reads: BLACK, so it reads zero and lights
// nothing, while every ground pixel carries the sun (the matte ground's far corners
// read within a code of a GREY sky's radiance, which is no discriminator at all).
assert(world.sky("color", { color: "#000000" }) === true, "a black sky for the ground/sky reads");
function isSky(p) { return p.r + p.g + p.b <= 3; }
function obliqueCorners(tag) {
    editor.setCamera({ position: { x: 40, y: 40, z: 40 }, lookAt: { x: 0, y: 0, z: 0 } });
    editor.frame(60, 1 / 60);
    var shot = editor.screenshot("ground_" + tag + ".png", 960, 540, CORNERS, "raw");
    var v = shot.probes.map(function (p) { return { r: p.r, g: p.g, b: p.b, sky: isSky(p) }; });
    console.log("oblique corners (" + tag + "): " + J(v));
    return v;
}
function skyCount(v) { return v.filter(function (p) { return p.sky; }).length; }
// The top three probes (0, 1, 4) lie ~176 m out, past the Floor's 50 m half-width;
// the bottom three are the Floor's own near field.
var FAR = [0, 1, 4];
var off = obliqueCorners("plane_off");
for (var f = 0; f < FAR.length; f++)
    assert(off[FAR[f]].sky, "with the Ground plane OFF the Floor ENDS: far probe " + FAR[f] + " is sky");
assert(!off[5].sky, "...while the near probe is the Floor");
assert(editor.setOverlays({ groundPlane: true }) === true, "editor.setOverlays({groundPlane: true})");
assert(editor.overlays().groundPlane === true && editor.overlays().menu.groundPlane === true,
       "...the verb and the View Options checkmark both read it");
var on = obliqueCorners("plane_on");
assert(skyCount(on) === 0, "with it ON the ground reaches every corner of the frame (" + J(on) + ")");

// ---- 2b. AND THE CHECKER CROSSES THAT EDGE IN PHASE -------------------------
//
// The corner probes above only say "ground", and a plane with the right checker
// DENSITY but the wrong PHASE passes them while replacing the Floor's geometry
// edge with a texture seam. The plane maps 1/100 UV per metre — a template
// floor's own top-face map — so the default floor material registers across
// the edge. So: straddle the edge, and compare it against an interior line of
// the same floor.
//
// SELF-CALIBRATING, because the shipped tile is not an axis-aligned checker at
// all — it is a diamond lattice, so "are these two points the same colour" says
// as much about where the diamonds fall as about the seam. Instead: the mean
// |difference| between pairs of points 0.15 m either side of a LINE, measured
// once over an interior line (no seam there, so this is what the pattern itself
// costs) and once over the floor's edge. A phase break moves the second well
// past the first; a continuous checker cannot.
//
// The camera looks STRAIGHT DOWN from 20 m, so both sides are at the same scale
// and the same light; at 45 degrees over a 16:9 shot the frame is 29.44 m wide
// and 16.56 m deep, so 0.15 m is 6.5 px — clear of the 5x5 probe boxes either
// side. Ten pairs down the frame are ten positions ALONG the line, which is
// what catches a v axis that cycles.
// THE CAMERA IS DELIBERATELY NOT STRAIGHT DOWN. A lookAt along -Y leaves the up
// vector degenerate and the view rolls by an arbitrary angle (45 degrees, as it
// happens), which turns the floor's edge into a diagonal across the frame and
// makes every "straddle the centre line" probe miss it. Standing 6 m back and
// 20 m up, looking at a point ON the edge, is roll-free: the edge's direction
// has no component along the camera's right axis, so it projects to the frame's
// vertical centre line at every depth, and pairs 1.2% of the width either side
// straddle it at every height.
var SEAM_DX = 0.012;
function lineMetric(cx, cz, alongZ, tag) {
    var eye = alongZ ? { x: cx, y: 20, z: cz - 6 } : { x: cx - 6, y: 20, z: cz };
    editor.setCamera({ position: eye, lookAt: { x: cx, y: 0, z: cz } });
    editor.frame(45, 1 / 60);
    var pts = [], i;
    for (i = 0; i < 10; i++) {
        var t = 0.08 + i * 0.09;
        pts.push({ x: 0.5 - SEAM_DX, y: t });
        pts.push({ x: 0.5 + SEAM_DX, y: t });
    }
    var shot = editor.screenshot("ground_seam_" + tag + ".png", 1280, 720, pts, "raw");
    var v = shot.probes.map(function (p) { return Math.round(lum(p)); });
    var sum = 0;
    for (i = 0; i < 10; i++) sum += Math.abs(v[i * 2] - v[i * 2 + 1]);
    var mean = sum / 10;
    console.log("seam " + tag + ": mean|d| = " + mean.toFixed(2) + "  " + J(v));
    return mean;
}
// Fog is still off from the block above; a plain view of the floor's own
// checker is all this needs.
var interior = Math.max(lineMetric(30, 0, true, "interior_x"),
                        lineMetric(0, 30, false, "interior_z"));
console.log("the pattern's own cost across the same gap: " + interior.toFixed(2) + "/255");
var EDGES = [ { x: 50, z: 0, alongZ: true,  tag: "east" },
              { x: -50, z: 0, alongZ: true,  tag: "west" },
              { x: 0, z: 50, alongZ: false, tag: "north" },
              { x: 0, z: -50, alongZ: false, tag: "south" } ];
for (var e = 0; e < EDGES.length; e++) {
    var m = lineMetric(EDGES[e].x, EDGES[e].z, EDGES[e].alongZ, EDGES[e].tag);
    assert(m <= interior + 2.0,
           "the checker crosses the " + EDGES[e].tag + " edge IN PHASE — no texture seam where "
           + "the geometry one was (" + m.toFixed(2) + " vs the pattern's own " + interior.toFixed(2) + ")");
}

// The Floor itself is still 100 m: the widget is not the document growing.
var b = scene.bounds({ nodes: [ground] });
assert(near(b.size.x, 100, 1) && near(b.size.z, 100, 1),
       "the Floor is still 100 m (" + J(b.size) + ")");
assert(scene.nodes().length === 4,
       "nothing new is in the document: the Floor, two lights and the root's own row (" +
       scene.nodes().length + ")");

// ---- 3. no lighting, nothing on Atom ---------------------------------------
// The FIT of the reflection-probe grid's placement region, read from the
// renderer with the widget ON and OFF, paired: a backdrop is in no capture and
// no GI geometry, so the region is the same numbers. (The ray row goes off for
// the read — a grid is placed where reflections are not traced.)
var rayRow = world.rayTracing();
world.rayTracing("off");
function region(tag) {
    editor.frame(180, 1 / 60);
    var st = world.giStatus();
    console.log("giStatus (" + tag + "): " + J({ min: st.probeRegionMin, max: st.probeRegionMax,
                                                 probes: st.probeCount }));
    assert(st.live === true, "giStatus is live (" + tag + ")");
    return st;
}
var withPlane = region("plane on");
assert(editor.setOverlays({ groundPlane: false }) === true, "the widget off");
var without = region("plane off");
assert(near(withPlane.probeRegionMin.x, without.probeRegionMin.x, 1e-3) &&
       near(withPlane.probeRegionMax.x, without.probeRegionMax.x, 1e-3) &&
       near(withPlane.probeRegionMin.y, without.probeRegionMin.y, 1e-3) &&
       near(withPlane.probeRegionMax.y, without.probeRegionMax.y, 1e-3) &&
       near(withPlane.probeRegionMin.z, without.probeRegionMin.z, 1e-3) &&
       near(withPlane.probeRegionMax.z, without.probeRegionMax.z, 1e-3) &&
       withPlane.probeCount === without.probeCount,
       "the probe region is the SAME with the Ground plane as without it — it is in no capture");
assert(without.probeRegionMin.x <= -30 && without.probeRegionMax.x >= 30,
       "...and it is the FLOOR's region: the Floor is in it (" +
       without.probeRegionMin.x.toFixed(1) + " .. " + without.probeRegionMax.x.toFixed(1) + ")");
world.rayTracing(rayRow);

// ATOM. The Floor is an ordinary baked cube, so the id pass draws it; the plane
// is a backdrop, so the split counts it as notWorld and draws it through PBS.
function atomSettled() {
    var st = world.atomStatus();
    for (var i = 0; i < 120 && st.live && st.pending > 0; ++i) { editor.frame(1, 1 / 60); st = world.atomStatus(); }
    return st;
}
editor.frame(4, 1 / 60);
var aOff = atomSettled();
assert(editor.setOverlays({ groundPlane: true }) === true, "the widget on again");
editor.frame(4, 1 / 60);
var aOn = atomSettled();
console.log("atom: off " + J({ atomItems: aOff.atomItems, notWorld: aOff.notWorld, on: aOff.on }) +
            " on " + J({ atomItems: aOn.atomItems, notWorld: aOn.notWorld, on: aOn.on }));
if (aOn.live && aOn.on) {
    assert(aOn.notWorld === aOff.notWorld + 1,
           "the Ground plane is NOT on Atom: the split counts it under notWorld (" +
           aOff.notWorld + " -> " + aOn.notWorld + ")");
    assert(aOn.atomItems === aOff.atomItems, "...and atomItems does not move (" + aOn.atomItems + ")");
    assert(node.setProperty(ground, "visible", false), "hide the Floor");
    editor.frame(3, 1 / 60);
    var aNoFloor = world.atomStatus();
    assert(aNoFloor.atomItems === aOn.atomItems - 1,
           "...while the Floor IS on Atom (atomItems " + aOn.atomItems + " -> " + aNoFloor.atomItems +
           " with it hidden)");
    assert(node.setProperty(ground, "visible", true), "show the Floor again");
} else {
    console.log("note: the Atom split is not live here (on=" + aOn.on + "); the route is mirror.ground_plane's");
}

// ---- 4. a save round trip keeps the setting ---------------------------------
assert(project.save() === true, "save");
assert(project.close() === true, "close");
assert(project.open(proj) === true, "reopen");
assert(editor.overlays().groundPlane === true,
       "the Ground plane setting is PER SCENE and came back with it");
assert(scene.nodes().length === 4,
       "...and the SAVED scene holds no node for the plane: the Floor, two lights and the root (" +
       scene.nodes().length + ")");
var floor2 = scene.find("Floor");
var m2 = material.get(floor2);
assert(m2.workflow === 1 && near(m2.ior, 1.0, 1e-4) &&
       String(m2.specularColor).toLowerCase() === "#000000",
       "the matte floor survived save/reopen");
assert(world.fog({ enabled: false }), "fog off again (the reopened scene brought its own back)");
var reopened = obliqueCorners("reopen");
assert(skyCount(reopened) === 0, "...and so did the widget's ground (" + J(reopened) + ")");

// ---- 5. the widget does not need a floor --------------------------------------
assert(node.setProperty(floor2, "visible", false), "hide the Floor");
var noFloor = obliqueCorners("no_floor_plane_on");
assert(skyCount(noFloor) === 0,
       "with the Floor hidden the Ground plane still grounds every corner (" + J(noFloor) + ")");

// ---- 5b. A VISUAL GIZMO, NOT A PROJECT ASSET (owner, 2026-09-30) -------------
// With the plane ON and nothing else under the cursor (the Floor hidden), the
// plane is nothing a user can touch: a viewport click there selects NOTHING, a
// material drop (and the hover preview, which asks the same question) finds NO
// target, and the plane is in neither the outliner nor the saved scene.
var vs5 = editor.viewportState();
var px = vs5.width * 0.5, py = vs5.height * 0.8;
editor.setCamera({ position: { x: 0, y: 6, z: 8 }, lookAt: { x: 0, y: 0, z: 0 } });
editor.frame(4, 1 / 60);
var under5 = editor.screenshot("ground_gizmo_pick.png", 320, 180, [{ x: 0.5, y: 0.8 }], "raw").probes[0];
assert(!isSky(under5), "the probed pixel IS the Ground plane (" + J(under5) + ")");
assert(editor.clickTargetAt(px, py) == null, "a viewport click on the Ground plane selects NOTHING");
assert(editor.dropTargetAt(px, py) == null,
       "...and a material drop / hover preview there finds NO target — the plane takes no material");
var rowsOn = editor.outlinerRows().length;
assert(editor.setOverlays({ groundPlane: false }) === true, "the widget off (for the comparison)");
assert(editor.outlinerRows().length === rowsOn, "the outliner does not change with the plane (" + rowsOn + " rows)");
assert(editor.setOverlays({ groundPlane: true }) === true, "the widget on again");
assert(scene.nodes().filter(function (r) { return /ground ?plane/i.test(r.name); }).length === 0,
       "...and no node for it exists in the document the save writes");
assert(editor.setOverlays({ groundPlane: false }) === true, "the widget off too");
var bare = obliqueCorners("no_floor_plane_off");
assert(skyCount(bare) === CORNERS.length, "...and with both gone the frame is all sky (" + J(bare) + ")");
assert(node.setProperty(floor2, "visible", true), "show the Floor again");

console.log("e2e_default_ground: all sections passed");
