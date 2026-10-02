// scene.reopen_fidelity — the save -> close -> reopen round trip, in pixels
// and in fields.
//
// THE DEFECT THIS GATES (owner-reported, root-caused 2026-09-04): create a
// fresh project and the default ground renders 65,65,65. Save it, close it,
// reopen it, change nothing — and the same ground renders 255,255,255. It was
// never a lighting bug: the scene's environment (ambient, exposure, world
// mode, sky, every light) round-tripped perfectly. What was lost was the
// ground's DIFFUSE TEXTURE. MainWindow::createDefaultScene copied Tile.png
// into the project folder and registered a bare catalog row, so the asset had
// no store object and no pin; SceneWriter::assetGuidForTexturePath still
// recovered its guid through a by-name catalog lookup, but the reader's
// matching branch had been deleted when the pin world landed — so the saved
// guid resolved to an empty path and the floor reopened as bare white diffuse.
// First fix (2026-09-04): the reader grew its half of that legacy fallback.
// Real fix (plan item 15c): the tile is a pinned library texture imported
// through the one pipeline, the writer and both readers resolve it through
// the CAS like any texture, and every by-name fallback is deleted — so this
// gate now proves the round trip with no name-matching left anywhere
// (scripting.e2e.shipped_assets asserts the identity half: guid, pin, store).
//
// Three more round-trip defects fell out of the field diff and are gated here
// too: the Shadow Caster flag was never serialized at all (the Ground is
// created with casting off and reopened with it on), node rotations went
// through a quaternion -> euler -> quaternion detour that is not a fixed point
// in float (every save moved a rotated node ~2e-6 degrees, without bound), and
// the World root node's own guid was re-minted on every open (so an untouched
// scene wrote a different blob every time it was saved).
//
// The shape of the gate: a fresh scene is the reference, and THREE save/close/
// reopen cycles must reproduce it exactly — pixels within a hair, document
// fields to the byte. Two cycles is what proves a defect is not a one-time
// default mismatch; the third is cheap and proves it is not compounding.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

// The document state that a round trip has to preserve: the whole world/
// environment block (ambient, exposure, tonemap, sky, shadows, GI, world mode
// and every quality row), plus every node's identity, transform, properties
// and material. JSON.stringify of this is the field-diff assertion.
function snapshot() {
    var nodes = scene.nodes();
    var arr = [];
    // (`shadowAlpha` and `shadowBias` were in this list and are GONE with the
    // document fields — render audit I-6: the renderer never read either.)
    var keys = ["intensity", "color", "distance", "spotCutOff", "shadowType",
                "shadowMapResolution", "castShadow", "visible", "lightType"];
    for (var i = 0; i < nodes.length; i++) {
        var n = nodes[i];
        var rec = { id: n.id, name: n.name, type: n.type };
        for (var k = 0; k < keys.length; k++) {
            try {
                var v = node.property(n.id, keys[k]);
                if (v !== undefined && v !== null) rec[keys[k]] = v;
            } catch (e) { /* the node has no such property — fine */ }
        }
        var inf = node.info(n.id);
        rec.pos = inf.position; rec.rot = inf.rotation; rec.scale = inf.scale;
        if (n.type === "mesh") rec.mat = material.get(n.id);
        arr.push(rec);
    }
    return JSON.stringify({ world: documentOnly(world.get()), root: scene.root(), nodes: arr });
}

// THE GATE IS DOCUMENT FIELDS TO THE BYTE, and a `live` sub-object is not one.
// world.get() carries runtime READOUTS beside the document (world.clouds.live:
// the sheet's measured clear-sky mean, its scroll, its capture counters) —
// numbers the renderer MEASURES from what it drew, which move in the fourth
// decimal between two opens of the same document (the capture's readback of a
// sky built again from its tables) without any field having changed. So every
// `live` object anywhere in the tree is dropped from the compared snapshot, and
// collected into LIVE_READOUTS so the run prints what it dropped (liveDiff
// below): a real field that hid in a `live` object would show there.
var LIVE_READOUTS = [];
function documentOnly(v, path) {
    path = path || "world";
    if (v === null || typeof v !== "object") return v;
    if (Array.isArray(v)) return v.map(function (x, i) { return documentOnly(x, path + "[" + i + "]"); });
    var out = {};
    Object.keys(v).forEach(function (k) {
        if (k === "live") { LIVE_READOUTS.push({ path: path + ".live", value: v[k] }); return; }
        out[k] = documentOnly(v[k], path + "." + k);
    });
    return out;
}
function liveDiff(a, b, path, out) {
    if (a === b) return out;
    if (a === null || b === null || typeof a !== "object" || typeof b !== "object") {
        out.push(path + ": " + JSON.stringify(a) + " -> " + JSON.stringify(b));
        return out;
    }
    var keys = {};
    Object.keys(a).forEach(function (k) { keys[k] = 1; });
    Object.keys(b).forEach(function (k) { keys[k] = 1; });
    Object.keys(keys).forEach(function (k) { liveDiff(a[k], b[k], path + "." + k, out); });
    return out;
}

// The ground, dead centre-bottom of the framed view. 0.8 is below the cube and
// on the tiled floor in this framing.
// SETTLED, NOT TIMED. A fixed frame count is a wall-clock settle in disguise and
// it measures nothing (the cameras.exposure lesson, CLAUDE.md): with Photon's
// camera-centred cascades on at every tier the irradiance field re-places itself
// when cascade 0 moves — which is what `focusSelection` does — and re-converges
// progressively at the update budget. This waits for the renderer to say it is
// done: GI at rest (the one settle predicate) and no probe still owes a capture. The cap is a
// guard, not a budget.
function settle() {
    for (var i = 0; i < 40; i++) {
        editor.frame(10, 1 / 60);
        var st = world.giStatus();
        if (st.giAtRest !== false && !st.staleProbes) return i;
    }
    return -1;
}
function probe(tag) {
    var cube = scene.find("Cube");
    editor.select(cube); editor.frame(2); editor.focusSelection();
    settle();
    var shot = editor.screenshot(tag + ".png", 640, 480, [{ x: 0.5, y: 0.8 }]);
    var p = shot.probes[0];
    console.log("probe " + tag + ": rgb " + p.r + "," + p.g + "," + p.b);
    return p;
}

// Same view, same frame count, same machine: a re-render of an identical
// document is exact. 2 is a hair for dither/driver rounding, and nowhere near
// enough to let the 65 -> 255 blowout through.
function samePixels(a, b, tag) {
    var d = Math.max(Math.abs(a.r - b.r), Math.abs(a.g - b.g), Math.abs(a.b - b.b));
    assert(d <= 2, tag + " (" + a.r + "," + a.g + "," + a.b + " vs " +
                  b.r + "," + b.g + "," + b.b + ")");
}

function groundOf(snap) {
    var s = JSON.parse(snap);
    for (var i = 0; i < s.nodes.length; i++)
        if (s.nodes[i].name === "Floor") return s.nodes[i];
    return null;
}

// ---------------------------------------------------------------------------
var guid = project.create("Reopen Fidelity " + Date.now());
assert(guid.length > 10, "project.create -> the default scene");
var cube = scene.addPrimitive("cube", { position: { x: 0, y: 0, z: 0 } });
assert(cube.length > 10, "scene.addPrimitive(cube)");

// A DELIBERATELY NON-UNIFORM tiling on the cube (SMOKE_FIX S11's other half):
// the fix must not turn "tile 4x across and 1x down" into uniform tiling on the
// way through the file. The floor's uniform (25, 25) and this (4, 1) are the two
// sides of the same round trip.
assert(material.set(cube, { textureScale: [4, 1] }) === true,
       "material.set(cube, textureScale [4, 1])");

var p0 = probe("fresh");
var s0 = snapshot();

// The root cause, asserted directly: the default ground is TEXTURED, and the
// document holds a resolved path to a file that exists. An empty string here
// is the whole defect.
//
// The row is `baseColorMap`, not `diffuseTexture`: the default ground is a
// PbrMaterial since HLMS_ADOPTION P4b (it was a Default.shader CustomMaterial,
// and the builtin shaders were retired). Same file, same defect, same gate.
var g0 = groundOf(s0);
assert(g0 !== null, "the template's Floor node is in the scene");
assert(g0.mat.baseColorMap && g0.mat.baseColorMap.length > 0,
       "fresh: Ground carries a resolved baseColorMap path");
assert(g0.castShadow === false, "fresh: Ground has Shadow Caster OFF (createDefaultScene)");

// SMOKE_FIX S11 — THE ROWS ARE WHAT GETS SAVED. createDefaultScene builds the
// floor with setValue("textureScale", 25), which is UNIFORM tiling: both fields
// 25, and therefore both rows 25. It used to sync only the row it was named with,
// so the file said textureScale 4, textureScaleV 1 while the live material
// tiled 4x4 — and the floor's checkers came back squashed along V on reopen.
// The whole-document diff below could not see it: BOTH sessions reported the
// same stale rows, and only the pixels disagreed.
assert(g0.mat.textureScale === 25 && g0.mat.textureScaleV === 25,
       "fresh: the Floor's UV tiling is uniform (25, 25) in the rows that get saved");
function cubeUv(snap) {
    var s = JSON.parse(snap);
    for (var i = 0; i < s.nodes.length; i++)
        if (s.nodes[i].id === cube) return s.nodes[i].mat;
    return null;
}
var c0 = cubeUv(s0);
assert(c0 !== null && c0.textureScale === 4 && c0.textureScaleV === 1,
       "fresh: the cube's deliberate (4, 1) tiling is what the rows say");
assert(p0.r < 200, "fresh: the ground is the mid-grey tile, not blown out (" + p0.r + ")");

for (var cycle = 1; cycle <= 3; cycle++) {
    assert(project.save() === true, "cycle " + cycle + ": project.save");
    assert(project.close() === true, "cycle " + cycle + ": project.close");
    assert(project.open(guid) === true, "cycle " + cycle + ": project.open (REOPEN)");

    var p = probe("reopen" + cycle);
    samePixels(p0, p, "cycle " + cycle + ": the ground renders what it rendered before the save");

    var liveFresh = LIVE_READOUTS;
    LIVE_READOUTS = [];
    var s = snapshot();
    // THE DROPPED READOUTS, diffed and printed once per cycle: what differs
    // there is a measurement, and the list says exactly which ones.
    var ld = [];
    for (var li = 0; li < liveFresh.length; li++) {
        var other = null;
        for (var lj = 0; lj < LIVE_READOUTS.length; lj++)
            if (LIVE_READOUTS[lj].path === liveFresh[li].path) other = LIVE_READOUTS[lj].value;
        liveDiff(liveFresh[li].value, other, liveFresh[li].path, ld);
    }
    console.log("LIVE DIFF cycle " + cycle + " (" + liveFresh.length + " live object(s), dropped from the gate): " +
                (ld.length ? ld.join(" | ") : "none"));
    // THE REOPENED SKY IS THE FRESH ONE (REOPEN-SKY-1): the environment capture's mean
    // is a function of the document, not of the camera's history — the capture sees
    // the sky from its own observer. Float tolerance (the capture's half-float SH).
    var skyFresh = null, skyNow = null;
    for (var si = 0; si < liveFresh.length; si++)
        if (liveFresh[si].path === "world.clouds.live") skyFresh = liveFresh[si].value.skyMean;
    for (var sj = 0; sj < LIVE_READOUTS.length; sj++)
        if (LIVE_READOUTS[sj].path === "world.clouds.live") skyNow = LIVE_READOUTS[sj].value.skyMean;
    assert(skyFresh && skyNow && skyFresh.length === 3 && skyNow.length === 3,
           "cycle " + cycle + ": the sky's mean is read fresh and reopened");
    var skyWorst = 0;
    for (var sc = 0; sc < 3; sc++)
        skyWorst = Math.max(skyWorst, Math.abs(skyNow[sc] - skyFresh[sc]) / Math.max(Math.abs(skyFresh[sc]), 1e-6));
    assert(skyWorst <= 1e-5, "cycle " + cycle + ": the reopened sky's mean equals the fresh one (worst relative " +
           skyWorst.toExponential(2) + ", bar 1e-5)");
    LIVE_READOUTS = liveFresh;
    var g = groundOf(s);
    assert(g.mat.baseColorMap === g0.mat.baseColorMap,
           "cycle " + cycle + ": Ground's baseColorMap survived the round trip");
    assert(g.castShadow === false,
           "cycle " + cycle + ": Ground's Shadow Caster flag survived the round trip");
    assert(g.mat.textureScale === 25 && g.mat.textureScaleV === 25,
           "cycle " + cycle + ": the Floor reopens with uniform (25, 25) tiling, not squashed");
    var c = cubeUv(s);
    assert(c.textureScale === 4 && c.textureScaleV === 1,
           "cycle " + cycle + ": the cube's explicit (4, 1) tiling survived the round trip");

    // The field diff. Not "close enough" — identical, including the World
    // root's guid, every light's power and colour, every transform, the whole
    // world/post-fx block. Cycle 2 and 3 are what turn "one-time default
    // mismatch" into "compounding drift" if anything moves.
    if (s !== s0) {
        // Report the first differing character with context: a bare "not equal"
        // on a 10 KB JSON string is useless to whoever has to fix it.
        var i = 0;
        while (i < s.length && i < s0.length && s[i] === s0[i]) i++;
        console.log("FIELD DIFF at " + i);
        console.log("  fresh:   ..." + s0.substr(Math.max(0, i - 60), 160));
        console.log("  cycle " + cycle + ": ..." + s.substr(Math.max(0, i - 60), 160));
    }
    assert(s === s0, "cycle " + cycle + ": the whole document is field-identical to the fresh scene");
}

// ---------------------------------------------------------------------------
// THE REOPENED SKY ABOVE 50 M, DRAWN AND CAPTURED (REOPEN-SKY-1, the merge read's
// fix round). The cycles above keep the template's camera under 50 m, where the
// environment is photographed from the ground's 2 m whatever the camera did; the
// drawn sky's tables and, above 50 m, the environment's own altitude follow the
// camera, and with hysteresis they followed its HISTORY — a fresh scene and the
// same scene reopened could draw and capture different skies. At rest both are
// functions of the camera's altitude alone (OgreScene::noteAtmosphereObserver). The
// fresh scene's camera gets there by a HISTORY that leaves both bands elsewhere —
// 200 m (the environment captured from 200 m), then 110 m (the drawn sky rebuilt at
// 110 m; the environment kept: under an octave from 200 m), then 120 m (inside the
// drawn sky's quarter octave of 110 m) — and the reopened scene's camera is loaded
// at 120 m. Without the rest rule the fresh scene would keep 110 m / 200 m and the
// reopened one draw 120 m / capture 100 m (measured: the arm fails with the rule
// off). Two reopen cycles must read the same drawn observer and environment observer
// to the bit, the same capture mean to float tolerance and the same sky pixels.
function skyLive() {
    var w = world.get();
    return w.clouds && w.clouds.live ? w.clouds.live : null;
}
function highSky(tag, path) {
    for (var hy = 0; hy < path.length; hy++) {
        editor.setCamera({ position: { x: 0, y: path[hy], z: 90 }, lookAt: { x: 0, y: path[hy] - 20, z: 0 } });
        settle();
        editor.frame(40, 1 / 60);   // the camera's rest (8 frames) and the capture it asks for land
    }
    var live = skyLive();
    var shot = editor.screenshot(tag + ".png", 640, 480, [{ x: 0.5, y: 0.2 }, { x: 0.2, y: 0.4 }, { x: 0.8, y: 0.4 }]);
    console.log("high sky " + tag + ": drawn observer " + (live && live.drawnObserverM) + " m, environment observer " +
                (live && live.environmentObserverM) + " m, skyMean " + JSON.stringify(live && live.skyMean) +
                ", sky probes " + JSON.stringify(shot.probes));
    return { live: live, probes: shot.probes };
}
var hi0 = highSky("high-fresh", [200, 110, 120]);
assert(hi0.live && hi0.live.drawnObserverM !== undefined && hi0.live.environmentObserverM !== undefined &&
       hi0.live.skyMean, "high camera: the sky's live readouts are there (the atmosphere is the sky)");
assert(Math.abs(hi0.live.drawnObserverM - 120) < 0.01,
       "high camera at rest: the drawn sky's observer is the camera's altitude (" + hi0.live.drawnObserverM + " m)");
assert(hi0.live.environmentObserverM === 100,
       "high camera at rest: the environment is captured from the octave lattice's 100 m (" +
       hi0.live.environmentObserverM + " m)");
for (var hc = 1; hc <= 2; hc++) {
    assert(project.save() === true, "high cycle " + hc + ": project.save");
    assert(project.close() === true, "high cycle " + hc + ": project.close");
    assert(project.open(guid) === true, "high cycle " + hc + ": project.open");
    var hi = highSky("high-reopen" + hc, [120]);
    assert(hi.live.drawnObserverM === hi0.live.drawnObserverM && hi.live.environmentObserverM === hi0.live.environmentObserverM,
           "high cycle " + hc + ": the reopened sky is drawn and captured from the fresh scene's altitudes (" +
           hi.live.drawnObserverM + " / " + hi.live.environmentObserverM + " m)");
    var hw = 0;
    for (var hk = 0; hk < 3; hk++)
        hw = Math.max(hw, Math.abs(hi.live.skyMean[hk] - hi0.live.skyMean[hk]) / Math.max(Math.abs(hi0.live.skyMean[hk]), 1e-6));
    assert(hw <= 1e-5, "high cycle " + hc + ": the reopened capture's mean equals the fresh one (worst relative " +
           hw.toExponential(2) + ", bar 1e-5)");
    for (var hp = 0; hp < hi0.probes.length; hp++)
        assert(hi.probes[hp].r === hi0.probes[hp].r && hi.probes[hp].g === hi0.probes[hp].g && hi.probes[hp].b === hi0.probes[hp].b,
               "high cycle " + hc + ": the drawn sky's pixel " + hp + " is the fresh one (" + JSON.stringify(hi.probes[hp]) +
               " vs " + JSON.stringify(hi0.probes[hp]) + ")");
}

// ---------------------------------------------------------------------------
// SCALED NODES (added 2026-09-04 by the clean-start sample audit). The scene
// above is entirely scale-1, and that is exactly the blind spot a whole class
// of round-trip defect lived in: SceneReader added the root's children with
// addChild's default keepTransform=TRUE, which makes SceneNode::insertChild
// recompose the child's local TRS from parentGlobal^-1 * childGlobal and
// extract the rotation with fromRotationMatrix(diff.normalMatrix()) — the
// inverse-transpose, R*S^-1, which equals R only when S is 1.
//
// So every top-level node with any other scale came back ROTATED on open, by
// an amount that grows with the scale's distance from 1 and with its
// anisotropy — and because closing a project autosaves, the wrong rotation was
// persisted and the error compounded on every single open. Measured on the
// shipped Showroom sample before the fix: a 0.16/0.75/0.16 wall panel moved
// 0.66 degrees per open and a 1.5-scaled torus 10 degrees per open, without
// bound. Nothing gated it because nothing saved a rotated, scaled node.
//
// Three shapes, three scale flavours: uniform-1 (the old blind spot's only
// case), uniform-not-1, and anisotropic.
var rotGuid = project.create("Reopen Scaled " + Date.now());
function scaledNode(prim, rot, scl) {
    var id = scene.addPrimitive(prim, { position: { x: 0, y: 1, z: 0 } });
    node.transform(id, { rotation: rot, scale: scl });
    return id;
}
var cases = [
    { prim: "cube",   name: "Cube",   rot: { x: 76.8, y: -14.17, z: 14.17 }, scale: { x: 1, y: 1, z: 1 } },
    { prim: "sphere", name: "Sphere", rot: { x: 14, y: -34.9, z: -1.05 },    scale: { x: 0.5, y: 0.5, z: 0.5 } },
    { prim: "plane",  name: "Plane",  rot: { x: 12, y: 40, z: 28.64 },       scale: { x: 0.16, y: 0.75, z: 0.34 } }
];
var before = [];
for (var c = 0; c < cases.length; c++) {
    scaledNode(cases[c].prim, cases[c].rot, cases[c].scale);
    before.push(node.info(scene.find(cases[c].name)).rotation);
}
console.log("scaled nodes as authored: " + JSON.stringify(before));

for (cycle = 1; cycle <= 3; cycle++) {
    assert(project.save() === true, "scaled cycle " + cycle + ": project.save");
    assert(project.close() === true, "scaled cycle " + cycle + ": project.close");
    assert(project.open(rotGuid) === true, "scaled cycle " + cycle + ": project.open");
    for (c = 0; c < cases.length; c++) {
        var now = node.info(scene.find(cases[c].name)).rotation;
        // 1e-3 degrees is far above the float32 round-trip residue (~2e-6) and
        // far below the smallest real drift this ever produced (0.03 degrees).
        var d = Math.max(Math.abs(now.x - before[c].x),
                         Math.abs(now.y - before[c].y),
                         Math.abs(now.z - before[c].z));
        assert(d < 1e-3, "scaled cycle " + cycle + ": " + cases[c].name + " scale " +
               JSON.stringify(cases[c].scale) + " kept its rotation (" +
               JSON.stringify(before[c]) + " -> " + JSON.stringify(now) + ", delta " + d + ")");
    }
}

console.log("reopen_fidelity: ALL OK");
