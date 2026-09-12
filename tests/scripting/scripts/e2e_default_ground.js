// scripting.e2e.default_ground — THE DEFAULT GROUND, IN PIXELS (owner,
// 2026-09-13, testing push #18: "the ground should not be reflective, it should
// have 0 specular" and "should the default ground not also be infinite in the
// Grand Showroom 2? It seems cut off").
//
// Two owner reports, one object, one engine-up suite — the document half of the
// floor (its flag, its own material, material.reset) is scripting.e2e.
// default_floor and stays there; this suite is what a CAMERA sees:
//
//   1. MATTE. At a grazing angle the floor shows no sheen. Fail-before on the
//      binary this lane started from, same camera, same probes: 66 65 67 81
//      against 61 59 59 72 here — the 4% dielectric reflection every metallic-
//      workflow material has (Hlms/Pbs 800.PixelShader_piece_ps.any:330) plus
//      the direct-light rim. The assertion is the comparison the owner made:
//      the floor is not brighter than its own diffuse lighting, and raising
//      Specular Color back to white brings the sheen back — because a mirror
//      floor has to stay possible.
//   2. NO EDGE. From a high oblique view the checker reaches every corner of
//      the frame instead of ending in a square with sky around it (that is
//      exactly the picture the owner reported). The floor's own mesh is 100 m;
//      what fills the rest is the mirror's horizon plane (SceneMirror::
//      syncGroundHorizon). AND IT IS NOT A TEXTURE SEAM EITHER: the checker
//      crosses all four edges of that square in phase, probed from straight
//      above (2b) — the trap this feature fell into on its first cut.
//   3. AND IT CHANGES NO LIGHTING. The horizon is an engine helper, not
//      geometry: the automatic GI volume, its voxel size and the probe grid are
//      the same numbers with it as without it — which is the constraint lane L3
//      left behind (GiParams::autoBoundsMax) and the reason "just make the
//      ground bigger" was measured and rejected: at 2.4 km the outlier trim
//      drops the floor out of the lit volume altogether (+-23.1 m -> +-1.8 m,
//      0.362 -> 0.029 m per voxel).
//   4. The document never hears about any of it: the scene still has exactly
//      one floor, scene.bounds still measures the 100 m plane, and nothing new
//      is selectable.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function J(x) { return JSON.stringify(x); }
function near(a, b, tol) { return Math.abs(a - b) <= tol; }
function lum(p) { return (p.r + p.g + p.b) / 3; }

var proj = project.create("Default Ground " + Date.now());
assert(proj.length > 10, "project.create");
var ground = scene.find("Ground");
assert(ground && node.property(ground, "defaultFloor") === true, "a new scene stands on the default floor");

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
// at all: kS white is the master switch (defaultfloor.h says so), and it has to
// brighten every one of these probes.
assert(material.set(ground, { specularColor: "#ffffff", ior: 1.5 }),
       "a user makes the floor reflective again: Specular Color white, IOR 1.5");
var shiny = grazing("shiny");
var brighter = 0;
for (var i = 0; i < matte.length; i++) if (shiny[i] > matte[i]) brighter++;
assert(brighter === matte.length,
       "every grazing probe is BRIGHTER with specular on (" + J(matte) + " -> " + J(shiny) + ")");
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

// ---- 2. no edge, from the view the owner flew -------------------------------
// A high oblique over the scene: the floor's own 100 m mesh covers barely the
// middle of this frame, so every corner probe is the horizon's.
var CORNERS = [ { x: 0.06, y: 0.08 }, { x: 0.94, y: 0.08 },
                { x: 0.06, y: 0.92 }, { x: 0.94, y: 0.92 },
                { x: 0.5,  y: 0.06 }, { x: 0.5,  y: 0.94 } ];
// FOG OFF for this measurement, and only for this one: a new scene fogs
// everything between 100 m and 180 m into the sky's own colour, which is a
// second (and much older) reason the ground reads as endless — but it also
// makes "is this pixel ground or sky" unanswerable at the top of the frame.
// With it off, the sky is a flat 72 and the checker is not.
assert(world.fog({ enabled: false }), "fog off, so ground and sky can be told apart");
// The camera pitches down 35 degrees over a 45-degree lens, so the WHOLE frame
// is below the horizon: every pixel of it is ground or it is a hole. The far
// corners sit ~176 m out, which the floor's own 100 m mesh cannot reach.
editor.setCamera({ position: { x: 40, y: 40, z: 40 }, lookAt: { x: 0, y: 0, z: 0 } });
editor.frame(60, 1 / 60);
var obl = editor.screenshot("ground_oblique.png", 960, 540, CORNERS, "raw");
var corners = obl.probes.map(function (p) { return Math.round(lum(p)); });
console.log("oblique corners: " + J(corners));
// The sky in a new scene reads ~72 flat; the checker reads ~20-45 and VARIES
// between its light and dark squares. "Every corner is ground" is therefore two
// assertions: darker than the sky, and not all the same value.
var spread = Math.max.apply(null, corners) - Math.min.apply(null, corners);
for (var c = 0; c < corners.length; c++)
    assert(corners[c] < 64, "the frame's corner " + c + " is ground, not sky (" + corners[c] + ")");
assert(spread >= 2, "...and it is the CHECKER, not a flat fill (spread " + spread + ")");

// ---- 2b. AND THE CHECKER CROSSES THAT EDGE IN PHASE -------------------------
//
// The corner probes above only say "not sky", and a horizon with the right
// checker DENSITY but the wrong PHASE passes them while replacing the geometry
// edge with a texture seam — which is exactly what the first cut of this
// feature did (a hand-picked UV constant with no offset: 0.195 of a repeat out,
// 0.78 m at the default textureScale 4). So: straddle the seam, and compare it
// against an interior line of the same floor.
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

// The floor itself is still 100 m: the horizon is not the document growing.
var b = scene.bounds({ nodes: [ground] });
assert(near(b.size.x, 100, 1) && near(b.size.z, 100, 1),
       "the default floor is still the 100 m plane lane L3 left (" + J(b.size) + ")");
var floors = scene.nodes().filter(function (r) {
    return r.type === "mesh" && node.property(r.id, "defaultFloor") === true;
});
assert(floors.length === 1, "and the scene has exactly ONE floor node (" + floors.length + ")");
assert(scene.nodes().length === 4,
       "nothing new is in the document: the floor, two lights and the root's own row (" +
       scene.nodes().length + ")");

// ---- 3. the lighting is untouched -------------------------------------------
// The numbers L3 pinned, read from the renderer: a 100 m ground clamped to the
// 64 m automatic ceiling, centred on the content.
world.gi({ mode: "vct", quality: "high" });
editor.frame(180, 1 / 60);
var st = world.giStatus();
console.log("giStatus: " + J({ voxelMetres: st.voxelMetres, min: st.boundsMin, max: st.boundsMax,
                               probes: st.probeCount }));
assert(st.live === true, "giStatus is live");
var extent = Math.max(st.boundsMax.x - st.boundsMin.x, st.boundsMax.y - st.boundsMin.y,
                      st.boundsMax.z - st.boundsMin.z);
assert(extent <= 66.0 && extent >= 60.0,
       "the automatic volume is still the 64 m ceiling, not a horizon-sized one (" +
       extent.toFixed(2) + " m)");
assert(near(st.voxelMetres, extent / 128.0, 0.01),
       "...at High's 128^3, i.e. " + st.voxelMetres.toFixed(4) + " m per voxel");
assert(st.boundsMin.x <= -30 && st.boundsMax.x >= 30,
       "...and it is the FLOOR's volume: the ground is still in the lit volume (" +
       st.boundsMin.x.toFixed(1) + " .. " + st.boundsMax.x.toFixed(1) + ")");

// ---- 4. a save round trip changes none of it --------------------------------
assert(project.save() === true, "save");
assert(project.close() === true, "close");
assert(project.open(proj) === true, "reopen");
var floor2 = scene.find("Ground");
var m2 = material.get(floor2);
assert(m2.workflow === 1 && near(m2.ior, 1.0, 1e-4) &&
       String(m2.specularColor).toLowerCase() === "#000000",
       "the matte floor survived save/reopen");
assert(world.fog({ enabled: false }), "fog off again (the reopened scene brought its own back)");
editor.setCamera({ position: { x: 40, y: 40, z: 40 }, lookAt: { x: 0, y: 0, z: 0 } });
editor.frame(60, 1 / 60);
var obl2 = editor.screenshot("ground_oblique_reopen.png", 960, 540, CORNERS, "raw");
var corners2 = obl2.probes.map(function (p) { return Math.round(lum(p)); });
console.log("oblique corners after reopen: " + J(corners2));
for (var d = 0; d < corners2.length; d++)
    assert(corners2[d] < 64, "...and so did the horizon, corner " + d + " (" + corners2[d] + ")");

// ---- 5. no floor, no horizon ------------------------------------------------
// Hiding the floor takes its horizon with it: what is left is sky.
assert(node.setProperty(floor2, "visible", false), "hide the floor");
editor.frame(30, 1 / 60);
var hidden = editor.screenshot("ground_hidden.png", 960, 540, CORNERS, "raw");
var hc = hidden.probes.map(function (p) { return Math.round(lum(p)); });
console.log("oblique corners, floor hidden: " + J(hc));
for (var h = 0; h < hc.length; h++)
    assert(hc[h] >= 64, "a hidden floor has no horizon either, corner " + h + " (" + hc[h] + ")");
assert(node.setProperty(floor2, "visible", true), "show it again");
editor.frame(30, 1 / 60);
var shown = editor.screenshot("ground_shown.png", 960, 540, CORNERS, "raw");
for (var g = 0; g < CORNERS.length; g++)
    assert(Math.round(lum(shown.probes[g])) < 64, "...and it comes back, corner " + g);

console.log("e2e_default_ground: all sections passed");
