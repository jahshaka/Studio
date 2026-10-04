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
function state(av, pl, what) {
    var issues = editor.issues();
    console.log(what + " issues: " + JSON.stringify(issues.map(function (i) { return i.id + " — " + i.message; })));
    var a = skeletal(av), p = skeletal(pl);
    assert(issues.length === 0, what + ": ZERO issues (" + issues.length + ")");
    assert(a.skeletal === 3 && a.all === 3,
           what + ": the avatar has its two own clips + the loaded one, all resolved (" + JSON.stringify(a) + ")");
    assert(p.skeletal === 1 && p.all === 1,
           what + ": the placed model has its own clip, resolved (" + JSON.stringify(p) + ")");
}
function roundTrips(p1, p2, av, pl, what) {
    for (var round = 1; round <= 2; round++) {
        assert(project.save() === true, what + " round " + round + ": save");
        assert(project.open(p2) === true && project.save() === true,
               what + " round " + round + ": switch to the other project (and save it)");
        assert(project.open(p1) === true, what + " round " + round + ": reopen");
        state(av, pl, what + " round " + round + " reopened");
    }
}

function arm1(staged, loc) {
    var p1 = project.create("clip ref one", { location: loc });
    var r = avatar.importAvatar(staged + "/Hero.glb", { scope: "project" });
    assert(r && r.asset, "the avatar imports (" + JSON.stringify(r) + ")");
    var av = avatar.spawn(r.asset, { position: { x: 0, y: 0, z: 0 } });
    var loaded = avatar.loadClip(av, staged + "/HeroWalk.glb");
    assert(loaded && loaded.added >= 1, "the extra clip loads onto the spawned avatar");
    var placed = assets.importAndPlace(staged + "/Prop.fbx", { position: { x: 3, y: 0, z: 0 } });
    var pl = placed.nodeId;
    state(av, pl, "fresh");
    assert(project.save() === true, "save the first project");
    var p2 = project.create("clip ref two", { location: loc });
    assert(project.open(p1) === true, "back to the first project");
    state(av, pl, "first reopen");
    roundTrips(p1, p2, av, pl, "R1");
    console.log("CLIPREF P1=" + p1 + " P2=" + p2 + " AV=" + av + " PL=" + pl);
    console.log("arm 1: ALL OK");
}

function arm2(p1, p2, av, pl) {
    assert(project.open(p1) === true, "the relocated data root opens the first project");
    state(av, pl, "relocated open");
    roundTrips(p1, p2, av, pl, "R2");
    console.log("arm 2: ALL OK");
}
