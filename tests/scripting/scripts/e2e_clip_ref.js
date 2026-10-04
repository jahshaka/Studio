// scripting.e2e.clip_ref — the JS half of tests/scripting/clip_ref.sh (CLIP-REF-1): the
// wrapper appends ONE call, arm1(...) or arm2(...), and runs the result --headless.
// See the wrapper's header for what each arm proves.
function assert(c, m) { if (!c) throw new Error("assert failed: " + m); console.log("ok: " + m); }
function skeletal(root) {
    var nodes = scene.nodes({ subtree: root }), n = 0, all = 0;
    for (var i = 0; i < nodes.length; i++) {
        var clips = anim.list(nodes[i].id);
        for (var k = 0; k < clips.length; k++) { all++; if (clips[k].skeletal) n++; }
    }
    return { skeletal: n, all: all };
}
function state(av, pl, what, dup) {
    var issues = editor.issues();
    console.log(what + " issues: " + JSON.stringify(issues.map(function (i) { return i.id + " — " + i.message; })));
    var a = skeletal(av), p = skeletal(pl);
    assert(issues.length === 0, what + ": ZERO issues (" + issues.length + ")");
    assert(a.skeletal === 3 && a.all === 3,
           what + ": the avatar has its two own clips + the loaded one, all resolved (" + JSON.stringify(a) + ")");
    assert(p.skeletal === 1 && p.all === 1,
           what + ": the placed model has its own clip, resolved (" + JSON.stringify(p) + ")");
    if (dup) {
        var d = skeletal(dup);
        assert(d.skeletal === 3 && d.all === 3,
               what + ": the DUPLICATED avatar has the same three clips, resolved (" + JSON.stringify(d) + ")");
    }
}
function roundTrips(p1, p2, av, pl, what, dup) {
    for (var round = 1; round <= 2; round++) {
        assert(project.save() === true, what + " round " + round + ": save");
        assert(project.open(p2) === true && project.save() === true,
               what + " round " + round + ": switch to the other project (and save it)");
        assert(project.open(p1) === true, what + " round " + round + ": reopen");
        state(av, pl, what + " round " + round + " reopened", dup);
    }
}

function arm1(staged, loc) {
    var p1 = project.create("clip ref one", { location: loc });
    var r = avatar.importAvatar(staged + "/Hero.glb", { scope: "project" });
    assert(r && r.asset, "the avatar imports (" + JSON.stringify(r) + ")");
    // assets.clips: the names the bake holds — the only names a reference may use.
    var names = assets.clips(r.asset);
    assert(names.length === 2 && names.indexOf("Idle") >= 0 && names.indexOf("mixamo.com") >= 0,
           "assets.clips names the rig's two baked clips (" + JSON.stringify(names) + ")");
    var av = avatar.spawn(r.asset, { position: { x: 0, y: 0, z: 0 } });
    var loaded = avatar.loadClip(av, staged + "/HeroWalk.glb");
    assert(loaded && loaded.added >= 1, "the extra clip loads onto the spawned avatar");
    var placed = assets.importAndPlace(staged + "/Prop.fbx", { position: { x: 3, y: 0, z: 0 } });
    var pl = placed.nodeId;
    state(av, pl, "fresh");
    // A DUPLICATE of the animated avatar keeps every clip (SceneNode::duplicateInto).
    var dup = node.duplicate(av);
    assert(dup && dup !== av, "the avatar duplicates (" + dup + ")");
    state(av, pl, "duplicated", dup);
    assert(project.save() === true, "save the first project");
    var p2 = project.create("clip ref two", { location: loc });
    assert(project.open(p1) === true, "back to the first project");
    state(av, pl, "first reopen", dup);
    roundTrips(p1, p2, av, pl, "R1", dup);
    console.log("CLIPREF P1=" + p1 + " P2=" + p2 + " AV=" + av + " PL=" + pl + " DUP=" + dup);
    console.log("arm 1: ALL OK");
}

function arm2(p1, p2, av, pl, dup) {
    assert(project.open(p1) === true, "the relocated data root opens the first project");
    state(av, pl, "relocated open", dup);
    roundTrips(p1, p2, av, pl, "R2", dup);
    console.log("arm 2: ALL OK");
}
