// cameras.pip_rebuild — VIEWS-XID-1: A PIP REQUEST DIES WITH ITS SCENE.
//
// The owner's smoke instance and three pool runs lost the device (Xid 13
// "3D WIDTH ZT Violation", Xid 31) the same way: a camera's selection preview
// was up, the project closed, and the next scene's first bind rebuilt the
// inset from the View's stale request — on a scene that never asked for it —
// and the editor's synchronous cover frames drew it. The engine now drops the
// request on detach, and the viewport withdraws it before its mirror goes.
//
// NEGATIVE CONTROL (spikes/views-xid-1/table.txt): before the fix the same
// sequence rebuilt the inset on the new scene in 14/14 runs of the old
// runner's re-show and lost the device in every one; with the engine reset
// alone, 0/31. Here the discriminating reading is `builds` — the View's own
// count of inset builds, which the rebuild raised by one at the new scene's
// bind. The pool reads the kernel journal per process, so an Xid from this
// arm is a CRASH of it, never a pass.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function isNone(v) { return v === null || v === undefined; }

var preference = editor.pip().enabled;

assert(project.create("PipRebuild A " + Date.now()).length > 10, "project.create (scene A)");
assert(editor.setPip({ enabled: true }).enabled === true, "the preview preference is on");
var cam = scene.addCamera({ position: { x: 3, y: 2, z: 6 }, name: "Shot A" });
assert(cam.length > 10, "scene.addCamera -> " + cam);
assert(editor.select(cam), "editor.select(camera)");
editor.frame(2);
var up = editor.pip();
assert(up.camera === cam, "the inset previews the selected camera");
assert(up.onView === true, "the engine View holds the inset request");
assert(up.builds >= 1, "the View built the inset (" + up.builds + " builds)");

project.close();
var closed = editor.pip();
assert(closed.onView === false, "the close withdrew the inset from the View");
assert(closed.builds === up.builds, "the close built nothing (" + closed.builds + ")");

assert(project.create("PipRebuild B " + Date.now()).length > 10, "project.create (scene B)");
editor.frame(20);
var after = editor.pip();
assert(isNone(after.camera), "nothing is selected on the new scene, so nothing is previewed");
assert(after.onView === false, "the new scene's View holds no inset request");
assert(after.builds === up.builds,
       "NO inset was built on a scene that never asked for one (builds " + up.builds + " -> " +
       after.builds + ")");
assert(after.enabled === true, "the PREFERENCE is untouched — it is a setting, not a request");

// ...and the preview still works on the new scene when a camera is selected.
var camB = scene.addCamera({ position: { x: -3, y: 2, z: 6 }, name: "Shot B" });
assert(editor.select(camB), "editor.select(camera B)");
editor.frame(2);
var again = editor.pip();
assert(again.camera === camB && again.onView === true, "the inset comes back when asked for");
assert(again.builds > after.builds, "…built for THIS request (" + after.builds + " -> " + again.builds + ")");

editor.setPip({ enabled: preference });
0
