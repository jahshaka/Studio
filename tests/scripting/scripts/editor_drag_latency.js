// editor.drag_latency — A DRAG IN THE EDITOR IS ON SCREEN THE NEXT FRAME (ENGINE-UPGRADE-U3 stage 5, decision 50).
// FREE-RUNNING (JAH_FREE_RUNNING=1, the owner's mode): a translate drag through the REAL viewport path — a press on
// the gizmo's X arrow (editor.gizmoHitTest's pick, the one a mouse press takes) and 60 moves, each a MouseMove the
// viewport's own handler turns into a gizmo write — with the driver ticking between the moves. What is asserted:
//   * every move wrote the node (the gizmo's drag, not a script's write);
//   * the driver ticked after every move, and every record was taken by the next frame the render thread began after
//     its first submit (app.frameStats().submit: recordsLate unchanged, worstLatency 1 — the record submitted at tick
//     k is on screen at k + 1, never k + 2), the render loop beginning at least a frame per tick;
//   * the render thread began a frame for every tick (submit.handOvers moved at least once per tick);
//   * the drag made ZERO synchronous reads of the engine (engine.syncGetters(): no lockstep frame, no wait).
// A count in frames and ticks (trap 7: never a wall clock).

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function ticks() { return app.frameStats().ticks; }
function waitTick() {   // the event loop turns between the verbs (--script-live): the driver's timer fires
    var t0 = ticks(), polls = 0;
    while (ticks() === t0 && ++polls < 200000) {}
    return ticks() !== t0;
}

if (project.create("drag latency " + Date.now()) === false) throw new Error("project.create failed: " + app.lastError());
if (app.columns().space !== "editor") app.space("editor");
var cube = scene.addPrimitive("cube", { position: [0, 0.5, 0] });
assert(editor.select(cube), "the cube is selected");
assert(editor.setGizmoMode("translate"), "the translate gizmo is active");
editor.setCamera({ position: { x: 0, y: 2.5, z: 8 }, lookAt: { x: 0, y: 0.5, z: 0 } });
editor.frame(30);   // the open and the first frames: lockstep, outside the counted window

// THE X ARROW, found by the press's own pick: a grid over the middle of the viewport.
var vs = editor.viewportState();
var cx = Math.round(vs.width / 2), cy = Math.round(vs.height / 2), hits = [];
for (var y = cy - 160; y <= cy + 160; y += 4)
    for (var x = cx; x <= cx + 320; x += 4)
        if (editor.gizmoHitTest(x, y).handle === "x") hits.push([x, y]);
assert(hits.length > 0, "the X arrow is under " + hits.length + " sampled pixels");
var grab = hits[Math.floor(hits.length / 2)];

var x0 = node.transform(cube).position.x;
var sync0 = engine.syncGetters().total, fs0 = app.frameStats();
assert(editor.pointer("press", grab[0], grab[1]), "pressed on the X arrow at " + grab[0] + "," + grab[1]);
var moves = 0, wrote = 0, ticked = 0, last = x0;
for (var i = 0; i < 60; ++i) {
    editor.pointer("move", grab[0] + 3 * (i + 1), grab[1]);
    ++moves;
    var nx = node.transform(cube).position.x;
    if (nx !== last) ++wrote;
    last = nx;
    if (waitTick()) ++ticked;
}
editor.pointer("release", grab[0] + 180, grab[1]);
var fs1 = app.frameStats(), sync1 = engine.syncGetters().total;
var carried = fs1.submit.carried - fs0.submit.carried, late = fs1.submit.recordsLate - fs0.submit.recordsLate,
    handed = fs1.submit.recordsHanded - fs0.submit.recordsHanded, handOvers = fs1.submit.handOvers - fs0.submit.handOvers;
var submitted = fs1.submit.submitted - fs0.submit.submitted, tickCount = fs1.ticks - fs0.ticks;
console.log("EDITOR_DRAG_LATENCY moves=" + moves + " wrote=" + wrote + " ticked=" + ticked + " ticks=" + tickCount +
            " submitted=" + submitted + " handed=" + handed + " late=" + late + " worstLatency=" + fs1.submit.worstLatency + " carried(diag)=" + carried + " handOvers=" + handOvers + " syncReads=" +
            (sync1 - sync0) + " x " + x0.toFixed(3) + " -> " + last.toFixed(3));
assert(wrote === moves, "every move wrote the node through the gizmo (" + wrote + " of " + moves + ")");
assert(last > x0 + 0.1, "the drag moved the cube along +X (" + x0.toFixed(3) + " -> " + last.toFixed(3) + ")");
assert(ticked === moves, "the driver ticked after every move (" + ticked + " of " + moves + ")");
assert(submitted >= moves, "every tick submitted its record (" + submitted + ")");
// (Two ticks inside one frame merge their records into one hand-over — still the next frame begun after the first
// submit: the lateness below is the law, `handed` only says the frames took them.)
assert(handed > 0, "the frames took the drag's records (" + handed + " hand-overs carried them)");
assert(late === 0 && fs1.submit.worstLatency <= 1,
       "every record was drawn by the next frame begun after its submit (" + late + " late, worst " +
       fs1.submit.worstLatency + " frame)");
assert(handOvers >= tickCount, "the render thread began a frame for every tick (" + handOvers + " >= " + tickCount + ")");
assert(sync1 - sync0 === 0, "the drag read the engine synchronously " + (sync1 - sync0) + " time(s)");
console.log("editor_drag_latency: PASS");
