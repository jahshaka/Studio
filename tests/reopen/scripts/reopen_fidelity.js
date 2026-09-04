// scene.reopen_fidelity — the save -> close -> reopen round trip, in pixels
// and in fields.
//
// THE DEFECT THIS GATES (owner-reported, root-caused 2026-09-04): create a
// fresh project and the default ground renders 65,65,65. Save it, close it,
// reopen it, change nothing — and the same ground renders 255,255,255. It was
// never a lighting bug: the scene's environment (ambient, exposure, world
// mode, sky, every light) round-tripped perfectly. What was lost was the
// ground's DIFFUSE TEXTURE. MainWindow::createDefaultScene copies Tile.png
// into the project folder and registers a bare catalog row, so the asset has
// no store object and no pin; SceneWriter::assetGuidForTexturePath still
// recovered its guid through the by-name catalog lookup, but the reader's
// matching branch had been deleted when the pin world landed — so the saved
// guid resolved to an empty path and the floor reopened as bare white diffuse.
// Fix: MaterialReader::resolveTextureGuid grew the reader's half of that same
// legacy fallback.
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
    var keys = ["intensity", "color", "distance", "spotCutOff", "shadowType",
                "shadowAlpha", "shadowBias", "castShadow", "visible", "lightType"];
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
    return JSON.stringify({ world: world.get(), root: scene.root(), nodes: arr });
}

// The ground, dead centre-bottom of the framed view. 0.8 is below the cube and
// on the tiled floor in this framing.
function probe(tag) {
    var cube = scene.find("Cube");
    editor.select(cube); editor.frame(2); editor.focusSelection(); editor.frame(30, 1 / 60);
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
        if (s.nodes[i].name === "Ground") return s.nodes[i];
    return null;
}

// ---------------------------------------------------------------------------
var guid = project.create("Reopen Fidelity " + Date.now());
assert(guid.length > 10, "project.create -> the default scene");
var cube = scene.addPrimitive("cube", { position: { x: 0, y: 0, z: 0 } });
assert(cube.length > 10, "scene.addPrimitive(cube)");

var p0 = probe("fresh");
var s0 = snapshot();

// The root cause, asserted directly: the default ground is TEXTURED, and the
// document holds a resolved path to a file that exists. An empty string here
// is the whole defect.
var g0 = groundOf(s0);
assert(g0 !== null, "the default Ground node is in the scene");
assert(g0.mat.diffuseTexture && g0.mat.diffuseTexture.length > 0,
       "fresh: Ground carries a resolved diffuseTexture path");
assert(g0.castShadow === false, "fresh: Ground has Shadow Caster OFF (createDefaultScene)");
assert(p0.r < 200, "fresh: the ground is the mid-grey tile, not blown out (" + p0.r + ")");

for (var cycle = 1; cycle <= 3; cycle++) {
    assert(project.save() === true, "cycle " + cycle + ": project.save");
    assert(project.close() === true, "cycle " + cycle + ": project.close");
    assert(project.open(guid) === true, "cycle " + cycle + ": project.open (REOPEN)");

    var p = probe("reopen" + cycle);
    samePixels(p0, p, "cycle " + cycle + ": the ground renders what it rendered before the save");

    var s = snapshot();
    var g = groundOf(s);
    assert(g.mat.diffuseTexture === g0.mat.diffuseTexture,
           "cycle " + cycle + ": Ground's diffuseTexture survived the round trip");
    assert(g.castShadow === false,
           "cycle " + cycle + ": Ground's Shadow Caster flag survived the round trip");

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
