// world_light.shadow_fit_receivers — THE SUN'S SHADOW FALLS WHERE THE GEOMETRY
// SAYS, WHATEVER CASTS AND WHATEVER IS DRAWN (lane SHADOW-FIT-1; the owner's
// smoke bug #1, spikes/shadow-cut-1/).
//
// THE DEFECT. The two near PSSM splits are STABLE (concentric) splits, and
// upstream's ConcentricShadowCamera fitted their depth range to the CASTERS'
// box alone: the far plane sat at the casters' downstream extent, and a
// receiver past it read the clear depth = lit. On the Basic template the floor
// casts nothing (L13), so the casters' box was the sphere: a resting sphere's
// shadow was cut by a straight line, a floating one had NO shadow at all, and
// any other object further downstream moved the cut. In the editor viewport
// the light's icon (an editor helper still carrying Ogre's caster bit) was a
// caster too, which put the far plane somewhere else again: the viewport and
// the screenshot disagreed.
//
// THE CLAIM, per sphere height (0.4 / 1.5 / 3 / 6 m) and sun elevation (the
// template's default and two others): looking down at the floor, every
// floor point whose ray to the sun passes through the sphere is in shadow and
// every other one is lit — the closed-form projection of the sphere along the
// sun (an ellipse) — in BOTH pictures a user sees: grade "scene" (the editor's
// Screenshot, helpers hidden) and grade "viewport" (the whole chain with the
// editor's helpers kept, the live viewport's channel set). Then: a cube added
// anywhere (upstream, above, beside, below the floor) leaves the footprint
// unchanged.
//
// THE TOLERANCE. A probe is the 5x5 mean of the picture; probes whose sun ray
// passes within kEdge of the sphere's silhouette (the PCF penumbra plus the
// atlas texel, the ellipse's rim) are not judged; every other probe must agree
// with the closed form, with no exception. A probe whose camera ray hits the
// sphere (or the cube) is the object, not the floor, and is not judged either.
// Shadowed vs lit is read against the SAME frame with the sphere hidden (the
// floor's checker cancels): shadowed below kShadowRatio, lit above kLitRatio.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

var kEdge = 0.10;          // metres, measured perpendicular to the sun ray
var kShadowRatio = 0.80;   // a shadowed probe reads below this x the unshadowed floor
var kLitRatio = 0.93;      // ...and a lit one above this
var N = 56;                // N x N probes over the frame
var SHOT = 320;            // square shots: the aspect is 1
var FOV = 40;              // vertical = horizontal field of view, degrees

project.create("Shadow fit " + Date.now());       // the Basic template: a floor that casts nothing
var ids = scene.nodes();
var floorId = null, sunId = null;
for (var i = 0; i < ids.length; ++i) {
    if (ids[i].name === "Floor") floorId = ids[i].id;
    if (node.info(ids[i].id).type === "light" && node.property(ids[i].id, "lightType") === 1) sunId = ids[i].id;
    // the Basic template carries no other geometry; anything else would cast
}
assert(floorId !== null && sunId !== null, "the Basic template has its Floor and its sun");
assert(node.property(floorId, "castShadow") === false, "...and the floor casts no shadow (L13, the premise)");
// The sun's icon stays in the scene, far outside every frame: in the "viewport"
// grade it is drawn, and it is the helper whose caster bit used to move the fit.
node.transform(sunId, { position: { x: 0, y: 6, z: 30 } });

var sphere = scene.addPrimitive("sphere", { position: { x: 0, y: 3, z: 0 } });
var sb = scene.bounds({ nodes: [sphere] });
var R = sb.size.x / 2;
console.log("sphere radius " + R.toFixed(4));
assert(R > 0.2 && R < 5, "the sphere primitive has a sane radius");

function sub(a, b) { return [a[0] - b[0], a[1] - b[1], a[2] - b[2]]; }
function dot(a, b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
function cross(a, b) { return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]; }
function len(a) { return Math.sqrt(dot(a, a)); }
function qrot(q, v) {   // rotate v by the unit quaternion {x,y,z,scalar}
    var u = [q.x, q.y, q.z], s = q.scalar;
    var t = cross(u, v); t = [2 * t[0], 2 * t[1], 2 * t[2]];
    var c = cross(u, t);
    return [v[0] + s * t[0] + c[0], v[1] + s * t[1] + c[1], v[2] + s * t[2] + c[2]];
}
// distance from point c to the ray o + t*dir (t >= 0), dir a unit vector
function rayPointDist(o, dir, c) {
    var oc = sub(c, o), t = Math.max(0, dot(oc, dir));
    return len(sub(oc, [dir[0] * t, dir[1] * t, dir[2] * t]));
}
function rayHitsBox(o, dir, bmin, bmax) {
    var t0 = 0, t1 = 1e9;
    for (var a = 0; a < 3; ++a) {
        if (Math.abs(dir[a]) < 1e-9) { if (o[a] < bmin[a] || o[a] > bmax[a]) return false; continue; }
        var ta = (bmin[a] - o[a]) / dir[a], tb = (bmax[a] - o[a]) / dir[a];
        if (ta > tb) { var tmp = ta; ta = tb; tb = tmp; }
        t0 = Math.max(t0, ta); t1 = Math.min(t1, tb);
        if (t0 > t1) return false;
    }
    return true;
}

var PROBES = [];
for (var py = 0; py < N; ++py)
    for (var px = 0; px < N; ++px)
        PROBES.push({ x: (px + 0.5) / N, y: (py + 0.5) / N });

function luma(p) { return 0.2126 * p.r + 0.7152 * p.g + 0.0722 * p.b; }
var SHOTS = 0;
var FAILED = [];
function shot(grade) {
    return editor.screenshot("fit-" + (++SHOTS) + "-" + grade + ".png", SHOT, SHOT, PROBES, grade);
}

// One frame of the claim: the sphere at `cy`, the sun as it is now, optional
// extra box(es) that must not change anything. Returns the worst disagreement.
function judge(label, cy, extras) {
    node.transform(sphere, { position: { x: 0, y: cy, z: 0 } });
    var d = world.sun().direction;               // the light's travel direction, unit
    var D = [d.x, d.y, d.z];
    assert(D[1] < -0.2, label + ": the sun shines down (" + JSON.stringify(d) + ")");
    var C = [0, cy, 0];
    // The footprint's centre on the floor (y = 0), and its half-length along
    // the sun's ground track: the frame is fitted to it with a margin.
    var tc = -cy / D[1];
    var ctr = [C[0] + D[0] * tc, 0, C[2] + D[2] * tc];
    var sinE = -D[1];
    var halfLong = R / sinE + 0.4;
    // From the side of the sun's ground track, 37 degrees off vertical: a
    // steep sun puts the footprint under the sphere, where a straight-down
    // view cannot see it. The exact pose is read back and unprojected below.
    var tl = Math.sqrt(D[0] * D[0] + D[2] * D[2]);
    var T = tl > 1e-3 ? [D[0] / tl, 0, D[2] / tl] : [0, 0, -1];
    var S = [-T[2], 0, T[0]];
    var dist = halfLong / Math.tan(FOV * Math.PI / 360) + 1.0;
    var cam = editor.setCamera({ position: { x: ctr[0] + S[0] * dist * 0.6, y: dist * 0.8, z: ctr[2] + S[2] * dist * 0.6 },
                                 lookAt: { x: ctr[0], y: 0, z: ctr[2] }, fov: FOV });
    var E = [cam.position.x, cam.position.y, cam.position.z];
    var q = cam.rotation;
    var th = Math.tan(cam.fov * Math.PI / 360);

    var paths = ["scene", "viewport"];
    var worst = { n: 0, judged: 0 };
    for (var g = 0; g < paths.length; ++g) {
        node.setProperty(sphere, "visible", false);
        var ref = shot(paths[g]);
        node.setProperty(sphere, "visible", true);
        var got = shot(paths[g]);
        var bad = 0, judged = 0, inside = 0, firstBad = "";
        var minShadow = 9, maxShadow = 0, minLit = 9;
        for (var k = 0; k < PROBES.length; ++k) {
            var ndc = [2 * PROBES[k].x - 1, 1 - 2 * PROBES[k].y];
            var dir = qrot(q, [ndc[0] * th, ndc[1] * th, -1]);
            var dl = len(dir); dir = [dir[0] / dl, dir[1] / dl, dir[2] / dl];
            if (dir[1] >= -1e-6) continue;
            if (rayPointDist(E, dir, C) < R + 0.12) continue;          // the camera sees the sphere (and its anti-aliased rim)
            var t = -E[1] / dir[1];
            var P = [E[0] + dir[0] * t, 0, E[2] + dir[2] * t];
            var toSun = [-D[0], -D[1], -D[2]];
            var seen = false, extraShadow = false;
            for (var x = 0; x < extras.length; ++x) {
                var bmin = [extras[x].min.x - 0.05, extras[x].min.y - 0.05, extras[x].min.z - 0.05];
                var bmax = [extras[x].max.x + 0.05, extras[x].max.y + 0.05, extras[x].max.z + 0.05];
                if (rayHitsBox(E, dir, bmin, bmax)) seen = true;
                var bmin2 = [bmin[0] - kEdge, bmin[1] - kEdge, bmin[2] - kEdge];
                var bmax2 = [bmax[0] + kEdge, bmax[1] + kEdge, bmax[2] + kEdge];
                if (rayHitsBox(P, toSun, bmin2, bmax2)) extraShadow = true;
            }
            if (seen || extraShadow) continue;
            var s = rayPointDist(P, toSun, C);
            if (Math.abs(s - R) < kEdge) continue;                  // the rim: not judged
            var a = luma(got.probes[k]), b = luma(ref.probes[k]);
            if (b < 8) continue;                                     // nothing to read against
            var ratio = a / b;
            var want = s < R;
            ++judged;
            if (want) { ++inside; minShadow = Math.min(minShadow, ratio); maxShadow = Math.max(maxShadow, ratio); }
            else minLit = Math.min(minLit, ratio);
            var ok = want ? ratio < kShadowRatio : ratio > kLitRatio;
            if (!ok) {
                ++bad;
                if (!firstBad) firstBad = " first at floor (" + P[0].toFixed(2) + "," + P[2].toFixed(2) +
                                          ") ratio " + ratio.toFixed(3) + (want ? " want shadow" : " want lit");
            }
        }
        console.log("   " + label + " [" + paths[g] + "] judged " + judged + " (in the ellipse " + inside +
                    "), shadow ratio " + minShadow.toFixed(3) + ".." + maxShadow.toFixed(3) +
                    ", lit min " + minLit.toFixed(3) + ", disagreements " + bad + firstBad);
        // The framing's own check. A sphere SUNK into the floor (y 0.4 < R) under
        // a steep sun throws almost nothing beyond its own floor section: what
        // is left is narrower than the rim band, and only the lit half is judged.
        if (cy >= R) assert(inside >= 20, label + " [" + paths[g] + "]: the ellipse is in the frame (" + inside + " probes)");
        else console.log("   (the sphere is sunk into the floor: " + inside + " judgeable shadow probes beyond its own section)");
        // Every case runs before the verdict, so a red names the whole matrix.
        if (bad === 0) console.log("ok: " + label + " [" + paths[g] + "]: the footprint is the closed-form ellipse");
        else FAILED.push(label + " [" + paths[g] + "]: " + bad + " of " + judged + " probes disagree" + firstBad);
        worst.n += bad; worst.judged += judged;
    }
    return worst;
}

// ---- 1. heights x three suns ---------------------------------------------------
var sunRot = node.info(sunId).rotation;              // the template's default
var suns = [
    { name: "default", rot: { x: sunRot.x, y: sunRot.y, z: sunRot.z } },
    { name: "low", rot: { x: -60, y: 25, z: 0 } },
    { name: "high", rot: { x: -20, y: -40, z: 0 } }
];
var heights = [0.4, 1.5, 3, 6];
for (var si = 0; si < suns.length; ++si) {
    node.transform(sunId, { rotation: suns[si].rot });
    var el = Math.asin(-world.sun().direction.y) * 180 / Math.PI;
    console.log("sun " + suns[si].name + ": elevation " + el.toFixed(1) + " deg");
    for (var hi = 0; hi < heights.length; ++hi)
        judge("sun " + suns[si].name + ", sphere y " + heights[hi], heights[hi], []);
}

// ---- 2. a second object anywhere leaves the footprint alone --------------------
node.transform(sunId, { rotation: suns[0].rot });
var cube = scene.addPrimitive("cube", { position: { x: 0, y: 0, z: 0 } });
var spots = [
    { name: "upstream and high", p: { x: 6, y: 6, z: 15 } },
    { name: "directly above the sphere", p: { x: 0, y: 5.5, z: 0 } },
    { name: "beside, on the floor", p: { x: 8, y: 0.5, z: 0 } },
    { name: "far downstream", p: { x: 6, y: 2, z: -12 } },
    { name: "under the floor", p: { x: 6, y: -5, z: -2 } }
];
for (var ci = 0; ci < spots.length; ++ci) {
    node.transform(cube, { position: spots[ci].p });
    var cbox = scene.bounds({ nodes: [cube] });
    judge("cube " + spots[ci].name + ", sphere y 1.5", 1.5, [cbox]);
}
for (var fi = 0; fi < FAILED.length; ++fi) console.log("FAILED: " + FAILED[fi]);
assert(FAILED.length === 0, "every footprint is the closed-form ellipse (" + FAILED.length + " cases disagree)");
true
