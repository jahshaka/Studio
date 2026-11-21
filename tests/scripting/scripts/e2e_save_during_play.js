// scripting.e2e.save_during_play (an arm of the editor pool) — A SAVE DURING PLAY WRITES THE EDITED SCENE
// (STUDIO-HYGIENE-1, player/editpose.h). Three things move one document's nodes: the editor's run, the Player page's
// run and Simulate Physics; the save door swaps the document's edit pose (PlayBack::documentEditPose) in for the walk
// and puts the moved pose back. Engine up: play and simulate step only on frames.
//
// THE LAW, exact (no tolerance: the snapshot is the very floats the run found): a rigid body dropped from 5 m,
// stepped, saved or closed (auto-save on, the default) mid-fall -> the moved pose is still on screen after a save,
// and the reopened row holds the PRE-PLAY (pre-simulate) pose bit for bit. Red before the door's swap: the row held
// where the fall had got to; red before the final read's B1 fix for the Player's run and for Simulate.
// A SECTION THAT SAVES DURING THE RUN CLOSES WITH {discard: true} AFTER THE STOP (the final read 2's M3): a close
// with auto-save on saves AGAIN, after Stop has put the edit pose back, so the reopened row was the close's write
// and those sections passed with no swap at all. Discarded, the row is what the save DURING the run wrote.

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
function dirty() { return editor.undoState().dirty; }
// A saved project holding one rigid body 5 m up: {guid, body, edited (its pose as saved)}.
function bodyProject(label) {
    var g = project.create("Save During Play " + label + " " + Date.now(), { template: "basic" });
    var body = scene.addPrimitive("cube", { position: { x: 0, y: 5, z: 0 } });
    node.rename(body, "Falling Body");
    assert(node.physics(body, { type: "rigidbody", shape: "convexhull", mass: 1 }) === true, label + ": a rigid body");
    assert(project.save() === true, label + ": the edited scene is saved");
    return { guid: g, body: body, edited: pose(body) };
}
function reopenedAtEditPose(p, label) {
    assert(project.open(p.guid) === true, label + ": reopen");
    var reopened = byName("Falling Body");
    assert(reopened !== null, label + ": the body is in the row");
    // MEASURING ARM (final read 2 M3): every section's verdict printed, none stops the run - reverted next commit.
    console.log((pose(reopened) === p.edited ? "ARMCHECK GREEN " : "ARMCHECK RED ") + label + ": reopened " +
                pose(reopened) + " (edited " + p.edited + ")");
}

// 1. The editor's run, saved mid-fall.
var p = bodyProject("editor save");
assert(editor.play() === true, "play");
editor.frame(60);
var played = pose(p.body);
assert(played !== p.edited, "60 frames of play moved the body (" + played + ")");
assert(project.save() === true, "a save DURING play succeeds");
assert(pose(p.body) === played, "...and the play pose is still the one on screen (put back after the walk)");
assert(editor.stop() === true, "stop");
assert(project.close({ discard: true }) === true, "close, discarding (the row is the mid-play save's)");
reopenedAtEditPose(p, "editor save");

// 2. The Player page's run (its own PlayBack on the same document), saved mid-fall, then closed mid-fall.
p = bodyProject("player save");
app.space("player");
assert(player.playing() === true, "the Player page plays the scene");
player.frame(30);
assert(pose(p.body) !== p.edited, "30 Player frames moved the body");
assert(project.save() === true, "a save during the PLAYER's run succeeds");
assert(player.stop() === true, "the Player stops");
app.space("editor");
assert(project.close({ discard: true }) === true, "close, discarding (the row is the mid-run save's)");
reopenedAtEditPose(p, "player save");

p = bodyProject("player close");
app.space("player");
player.frame(30);
assert(pose(p.body) !== p.edited, "30 Player frames moved the body");
assert(project.close() === true, "a close during the PLAYER's run (its auto-save) closes");
reopenedAtEditPose(p, "player close");

// 3. Both runs at once (the editor's first, then the Player page: nothing stops the editor's run): the EARLIEST
//    run's snapshot is the edit pose; the Player's is the editor's play pose.
p = bodyProject("both runs");
assert(editor.play() === true, "the editor plays");
editor.frame(30);
app.space("player");
player.frame(10);
assert(project.save() === true, "a save with both runs playing succeeds");
player.stop();
app.space("editor");
editor.stop();
assert(project.close({ discard: true }) === true, "close, discarding (the row is the mid-run save's)");
reopenedAtEditPose(p, "both runs");

// 4. Simulate Physics (no run): a save and a close mid-fall write the pre-simulate pose.
p = bodyProject("simulate save");
assert(editor.simulate(true) === true, "simulate");
editor.frame(30);
var simulated = pose(p.body);
assert(simulated !== p.edited, "30 simulated frames moved the body");
assert(project.save() === true, "a save DURING Simulate succeeds");
assert(pose(p.body) === simulated, "...and the simulated pose is still the one on screen");
assert(editor.simulate(false) === true, "simulate off");
assert(project.close({ discard: true }) === true, "close, discarding (the row is the mid-simulate save's)");
reopenedAtEditPose(p, "simulate save");

p = bodyProject("simulate close");
assert(editor.simulate(true) === true, "simulate");
editor.frame(30);
assert(project.close() === true, "a close DURING Simulate (its auto-save) closes");
reopenedAtEditPose(p, "simulate close");

// 5. Simulate first, then Play: the run's snapshot takes the bodies' PRE-SIMULATE pose (the run's physics start
//    overwrites the environment's copy with the simulated one).
p = bodyProject("simulate then play");
assert(editor.simulate(true) === true, "simulate");
editor.frame(30);
assert(editor.play() === true, "play over the simulation");
editor.frame(10);
assert(project.save() === true, "a save during that run succeeds");
editor.stop();
editor.simulate(false);
assert(project.close({ discard: true }) === true, "close, discarding (the row is the mid-run save's)");
reopenedAtEditPose(p, "simulate then play");

// 6. A NODE ADDED DURING PLAY (the final read's B2) is not part of the document a save writes: the save succeeds
//    and the scene stays UNSAVED; after Stop it is part of the document, and the next save writes it.
p = bodyProject("added in play");
assert(editor.play() === true, "play");
editor.frame(10);
var added = scene.addPrimitive("sphere", { position: { x: 3, y: 1, z: 0 } });
node.rename(added, "Added In Play");
assert(project.save() === true, "a save with a node added during play succeeds");
assert(dirty() === true, "...and the scene stays UNSAVED (it was marked saved without the node)");
var closeWhy = null;
try { project.close(); } catch (e) { closeWhy = String(e && e.message ? e.message : e); }
assert(closeWhy !== null && closeWhy.indexOf("is not saved whole") >= 0 && closeWhy.indexOf("discard") >= 0,
       "...so the close (auto-save on) is REFUSED, naming {discard: true}: its auto-save left the node out (" +
       closeWhy + ")");
assert(project.current() && project.current().guid === p.guid, "...and the world stays");
assert(editor.stop() === true, "stop");
assert(byName("Added In Play") !== null, "Stop keeps the added node");
assert(project.save() === true && dirty() === false, "saved after Stop: clean");
assert(project.close() === true, "close");
assert(project.open(p.guid) === true, "reopen");
assert(byName("Added In Play") !== null, "the node added during play is in the reopened scene");
console.log("save during play: ALL OK");
