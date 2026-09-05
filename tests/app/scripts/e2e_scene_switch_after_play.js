// app.scene_switch_after_play — the crash-1788594910 regression gate.
//
// Play a scene, leave play state armed, CLOSE the project (which tears the
// scene down without routing through the playback stop), then create a new
// one. The scene switch unwinds the stale play state against the half-dead
// previous scene: PlayBack::stopScene ran physics restore against a null
// root (frame 1) and Scene::updateSceneAnimation dereferenced the null root
// (frame 2). Both frames are now guarded; this script IS the proof — it
// simply must finish alive. Runs in the real binary (--script, scratch HOME).

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

// Round 1: play -> close -> create (the owner's exact crash flow, verb form).
var a = project.create("switch-a");
assert(a && a.length > 0, "project A created");
scene.addPrimitive("cube", { position: [0, 1, 0] });
assert(editor.play(), "play armed");
assert(project.close(), "project A closed while play state was armed");
var b = project.create("switch-b");
assert(b && b.length > 0, "project B created after play+close — no crash");
assert(scene.nodes().length >= 3, "project B has its default scene");

// Round 2: the same switch with SIMULATE armed instead of play.
assert(editor.simulate(true), "simulate armed");
assert(project.close(), "project B closed while simulating");
var c = project.create("switch-c");
assert(c && c.length > 0, "project C created after simulate+close — no crash");

// Round 3: play and STOP properly, then switch — the healthy path must still
// restore transforms (the recursive-restore F4 fix rides the same seam).
scene.addPrimitive("cube", { position: [2, 1, 0] });
assert(editor.play(), "play again");
assert(editor.stop(), "stop cleanly");
assert(project.close(), "clean close");
var d = project.create("switch-d");
assert(d && d.length > 0, "project D created after clean stop — no crash");

console.log("scene_switch_after_play: PASS");
