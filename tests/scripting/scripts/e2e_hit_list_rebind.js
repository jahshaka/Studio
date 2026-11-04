// pool.host_epic::hit_list_rebind - A VIEW RE-ATTACHED TO A SCENE BUILDS ITS HIT LIST AT ITS CREATION SIZE, AS A FRESH VIEW
// DOES (URGENT #157's engine half, irisgl 4576b132: OgreView::detachScene sets mHitListRows = 0; the landing audit's F7,
// 2026-11-04: no row went red without that line).
// The hit list GROWS when a frame overflows it and is never shrunk within a scene (OgreView::sizeHitList, kHitListWidth's
// law); its growth is THAT scene's. Before the reset the high-water outlived detachScene, so the next scene's graph was
// built at the rows an earlier scene's demand had grown it to - its list, and the rest's record allowance read from it
// (HitListLaw.h), a function of what the view drew before. A project switch is that path: the editor's ONE view is
// detached (setScene(nullptr), enginesceneviewport.cpp) and bound to the next scene - as the Player re-attaches its view.
// THE LAW (exact, in records): scene A, every gather ray a hit record (the arm gather.decodeHits: ~1.25 W H records a
// frame against a list created at 0.5625 W H), grows the editor view's list past its FIRST reading (the fresh view's
// creation size); the arm off, a project switch re-attaches the same view, at the same target, to scene B, whose demand
// fits the creation size: B's first reading of the view's list is EXACTLY that creation size - never A's grown list.
// RED without the reset: B's graph is built at max(A's grown rows, the creation rows) = A's grown list.
function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
}
function editorList() {   // the editor view's entry (mono; the largest target if another live view reads back too)
    var views = world.giStatus().rayQuery.hitViews || [], best = null;
    for (var i = 0; i < views.length; ++i)
        if (views[i].eyes === 1 && (!best || views[i].eyeWidth * views[i].eyeHeight > best.eyeWidth * best.eyeHeight))
            best = views[i];
    return best;
}
function readUntil(pred, cap) {   // frame by frame until the view's list reads back and pred holds; null past the cap
    for (var f = 0; f < cap; ++f) {
        editor.frame(1);
        var v = editorList();
        if (v && pred(v)) return { list: v, frames: f + 1 };
    }
    return null;
}
function world_(name, mover) {
    var g = project.create(project.nextFreeName(name), { template: "empty" });
    assert(g && g.length >= 10, "project.create(" + name + ")");
    world.mode({ mode: "epic" });
    scene.addPrimitive("Cube", { position: [0, -0.1, 0], scale: [8, 0.2, 8] });
    if (mover) {
        scene.addPrimitive("Cube", { position: [0, 1.5, -3], scale: [6, 3, 0.2] });
        var s = scene.addPrimitive("Sphere", { position: [0.4, 1.4, -1.5], scale: [0.5, 0.5, 0.5] });
        assert(node.setProperty(s, "mobility", "movable") === true, "the sphere is a mover");
    }
    editor.setCamera({ position: { x: 0, y: 2, z: 5 }, lookAt: { x: 0, y: 1, z: 0 } });
    editor.frame(10);
}
try {
    world_("hit-list-rebind-a", true);
    assert(engine.arm("gather.decodeHits", 1) === 1, "the arm gather.decodeHits: every gather ray a record");
    var a = readUntil(function (v) { return v.capacity > v.firstCapacity; }, 60);
    assert(a !== null, "scene A's demand grew the editor view's list within 60 frames: " + JSON.stringify(editorList()));
    var created = a.list.firstCapacity, grown = a.list.capacity;
    assert(engine.arm("gather.decodeHits", 0) === 0, "the arm off");
    world_("hit-list-rebind-b", false);   // the same view, detached from A, bound to B
    var b = readUntil(function (v) { return true; }, 60);
    assert(b !== null, "scene B's editor view list read back within 60 frames");
    assert(b.list.eyeWidth === a.list.eyeWidth && b.list.eyeHeight === a.list.eyeHeight,
           "the same target: " + b.list.eyeWidth + " x " + b.list.eyeHeight + " vs " + a.list.eyeWidth + " x " + a.list.eyeHeight);
    assert(b.list.dropped === 0, "scene B's demand fits the creation size (" + b.list.records + " records, " +
           b.list.dropped + " dropped): no growth of its own");
    assert(b.list.capacity === created,
           "the re-attached view's list at its first reading in B is the fresh view's creation size " + created +
           " records, never A's grown " + grown + " (read " + b.list.capacity + ")");
    console.log("ok: hit_list_rebind: the view at " + a.list.eyeWidth + " x " + a.list.eyeHeight + ": created " + created +
                " records, grown to " + grown + " in A (" + a.frames + " frames); re-attached to B: " + b.list.capacity +
                " at its first reading (" + b.frames + " frames), " + b.list.records + " records, 0 dropped");
} finally {
    engine.arm("gather.decodeHits", 0);   // a process-wide arm: never left on for the pool's next arm (the clean-state law)
}
"ok";
