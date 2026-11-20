// scripting.e2e.save_during_play (an arm of the editor pool) — A SAVE DURING PLAY WRITES THE EDITED SCENE
// (STUDIO-HYGIENE-1 audit, player/editpose.h). Play moves the document's own nodes; the save door swaps the
// play-start snapshot in for the walk and puts the play pose back. Engine up: play steps only on frames.
//
// THE LAW, exact (no tolerance: the snapshot is the very floats Play found): a rigid body dropped from 5 m,
// stepped 60 frames, saved mid-fall -> the play pose is still on screen after the save, and the reopened row
// holds the PRE-PLAY pose bit for bit. Red before the door's swap: the row held where the fall had got to.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function pose(id) {
    var n = scene.nodes().filter(function (r) { return r.id === id; })[0];
    return JSON.stringify({ p: n.position, r: n.rotation, s: n.scale });
}
function byName(name) {
    var n = scene.nodes().filter(function (r) { return r.name === name; });
    return n.length === 1 ? n[0].id : null;
}

var g = project.create("Save During Play " + Date.now(), { template: "basic" });
var body = scene.addPrimitive("cube", { position: { x: 0, y: 5, z: 0 } });
node.rename(body, "Falling Body");
assert(node.physics(body, { type: "rigidbody", shape: "convexhull", mass: 1 }) === true, "the cube is a rigid body");
assert(project.save() === true, "the edited scene is saved");
var edited = pose(body);

assert(editor.play() === true, "play");
editor.frame(60);
var played = pose(body);
assert(played !== edited, "60 frames of play moved the body (" + played + ")");
assert(project.save() === true, "a save DURING play succeeds");
assert(pose(body) === played, "...and the play pose is still the one on screen (put back after the walk)");
assert(editor.stop() === true, "stop");
assert(project.close() === true, "close");
assert(project.open(g) === true, "reopen");
var reopened = byName("Falling Body");
assert(reopened !== null, "the body is in the row");
assert(pose(reopened) === edited,
       "the row holds the PRE-PLAY pose exactly: " + pose(reopened) + " (edited " + edited + ", played " + played + ")");
console.log("save during play: ALL OK");
