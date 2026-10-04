// scripting.e2e.tex_ref — the JS half of tests/scripting/tex_ref.sh (TEX-REF-1): the
// wrapper appends ONE call, arm1(...), arm2(...) or arm3(...), and runs the result
// --headless. See the wrapper's header for what each arm proves.
function assert(c, m) { if (!c) throw new Error("assert failed: " + m); console.log("ok: " + m); }
function J(v) { return JSON.stringify(v); }

// What the scene SAVES for a node's maps (the blob node.serialize returns is the
// writer's own output) and what the session renders from (material.get).
function savedMaps(id) {
    var values = node.serialize(id).node.material.values;
    return { baseColorMap: values.baseColorMap || "", normalMap: values.normalMap || "" };
}
function textureIssues() {
    return editor.issues().filter(function (i) { return i.kind === "texture.missing"; });
}
// THE STATE every round must keep: each map row names the SAME asset it was bound
// to, renders from a file under THIS data root, and the save writes the guid —
// never a path. The emitter's image, the same.
function state(w, root, what) {
    editor.checkScene();
    var issues = editor.issues();
    console.log(what + " issues: " + J(issues.map(function (i) { return i.id + " — " + i.message; })));
    assert(issues.length === 0, what + ": ZERO issues (" + issues.length + ")");
    [["cube", w.cube, w.cubeGuids], ["model", w.model, w.modelGuids]].forEach(function (c) {
        var m = material.get(c[1]);
        var saved = savedMaps(c[1]);
        Object.keys(c[2]).forEach(function (row) {
            assert(m.textureAssets[row] === c[2][row],
                   what + ": the " + c[0] + "'s " + row + " names the asset it was bound to (" +
                   m.textureAssets[row] + ")");
            assert(("" + m[row]).indexOf(root + "/") === 0,
                   what + ": ...and renders from a file under THIS data root (" + m[row] + ")");
            assert(saved[row] === c[2][row],
                   what + ": ...and the save writes that guid, not a path (" + saved[row] + ")");
        });
    });
    var ps = particles.describe(w.emitter);
    assert(ps.textureGuid === w.emitterGuid && ("" + ps.texture).indexOf(root + "/") === 0,
           what + ": the emitter's image is the same asset, drawn from this data root (" +
           ps.textureGuid + " " + ps.texture + ")");
    assert(node.serialize(w.emitter).node.texture === w.emitterGuid,
           what + ": ...and the save writes its guid");
}
function roundTrips(w, root, what) {
    for (var round = 1; round <= 2; round++) {
        assert(project.save() === true, what + " round " + round + ": save");
        assert(project.open(w.p2) === true && project.save() === true,
               what + " round " + round + ": switch to the other project (and save it)");
        assert(project.open(w.p1) === true, what + " round " + round + ": reopen");
        state(w, root, what + " round " + round + " reopened");
    }
}

function arm1(staged, loc, root) {
    var w = {};
    w.p1 = project.create("tex ref one", { location: loc });
    // A FILE bound to a map row is imported first and bound by its guid; an ASSET
    // guid is bound as it is.
    w.cube = scene.addPrimitive("cube", { position: { x: -2, y: 0.5, z: 0 } });
    var normalGuid = assets.importFile(staged + "/normal.png");
    assert(normalGuid && normalGuid.length > 10, "a normal map imports (" + normalGuid + ")");
    assert(material.set(w.cube, { baseColorMap: staged + "/brick.png", normalMap: normalGuid }) === true,
           "material.set binds a FILE (imported on the way) and an asset guid");
    var m = material.get(w.cube);
    w.cubeGuids = { baseColorMap: m.textureAssets.baseColorMap, normalMap: m.textureAssets.normalMap };
    assert(w.cubeGuids.baseColorMap && w.cubeGuids.baseColorMap.length > 10 &&
           w.cubeGuids.normalMap === normalGuid,
           "both rows carry their asset (" + J(w.cubeGuids) + ")");
    // An imported model: its maps are the model's member Texture rows.
    var placed = assets.importAndPlace(staged + "/quad.glb", { position: { x: 2, y: 0, z: 0 } });
    assert(placed && placed.nodeId, "a textured model imports and places (" + J(placed) + ")");
    var meshes = scene.nodes({ subtree: placed.nodeId }).filter(function (n) {
        return n.type === "mesh" && material.get(n.id).baseColorMap;
    });
    assert(meshes.length > 0, "the model has a textured mesh");
    w.model = meshes[0].id;
    var mm = material.get(w.model);
    w.modelGuids = {};
    ["baseColorMap", "normalMap"].forEach(function (row) {
        if (mm[row]) w.modelGuids[row] = mm.textureAssets[row];
    });
    assert(w.modelGuids.baseColorMap && w.modelGuids.baseColorMap.length > 10,
           "the model's maps carry their member assets (" + J(w.modelGuids) + ")");
    // A particle emitter with an image bound by guid.
    w.emitter = scene.addParticles();
    w.emitterGuid = assets.importFile(staged + "/spark.png");
    assert(node.setParticleTexture(w.emitter, w.emitterGuid) === true,
           "an emitter's image binds by asset guid");
    state(w, root, "fresh");
    assert(project.save() === true, "save the first project");
    w.p2 = project.create("tex ref two", { location: loc });
    assert(project.open(w.p1) === true, "back to the first project");
    state(w, root, "first reopen");
    roundTrips(w, root, "R1");
    console.log("TEXREF " + J(w));
    console.log("TEXREF BASEPATH=" + material.get(w.cube).baseColorMap);
    console.log("arm 1: ALL OK");
}

function arm2(w, root) {
    assert(project.open(w.p1) === true, "the relocated data root opens the first project");
    state(w, root, "relocated open");
    roundTrips(w, root, "R2");
    console.log("arm 2: ALL OK");
}

// THE MISS: the cube's base-colour bytes are gone from this data root's store. The
// row keeps the asset it names (the reader binds the guid with no file), the scene
// says texture.missing naming the slot and the asset, and every save writes the
// SAME guid back — never the path the old reader fell back to.
function arm3(w) {
    assert(project.open(w.p1) === true, "the third data root opens the first project");
    editor.checkScene();
    var missing = textureIssues();
    console.log("arm 3 issues: " + J(missing.map(function (i) { return i.id + " — " + i.message; })));
    assert(missing.length === 1 && missing[0].node === w.cube,
           "ONE texture.missing, on the cube (" + missing.length + ")");
    assert(missing[0].message.indexOf("Base Color") >= 0 &&
           missing[0].message.indexOf(w.cubeGuids.baseColorMap) >= 0,
           "...naming the slot and the asset the scene names");
    for (var round = 1; round <= 2; round++) {
        var m = material.get(w.cube);
        assert(m.textureAssets.baseColorMap === w.cubeGuids.baseColorMap,
               "round " + round + ": the row still names its asset (" + m.textureAssets.baseColorMap + ")");
        assert(savedMaps(w.cube).baseColorMap === w.cubeGuids.baseColorMap,
               "round " + round + ": the save writes the same guid, not a path (" +
               savedMaps(w.cube).baseColorMap + ")");
        assert(savedMaps(w.cube).normalMap === w.cubeGuids.normalMap,
               "round " + round + ": ...and the slot that still resolves is untouched");
        assert(project.save() === true && project.open(w.p2) === true && project.open(w.p1) === true,
               "round " + round + ": save -> switch -> reopen");
        editor.checkScene();
        assert(textureIssues().length === 1, "round " + round + ": still ONE texture.missing");
    }
    console.log("arm 3: ALL OK");
}
