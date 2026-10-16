// editor.drag_latency — A DRAG IN THE EDITOR IS ON SCREEN THE NEXT FRAME (ENGINE-UPGRADE-U3 stage 5, decision 50).
// FREE-RUNNING (JAH_FREE_RUNNING=1, the owner's mode): a translate drag through the REAL viewport path — a press on
// the gizmo's X arrow (editor.gizmoHitTest's pick, the one a mouse press takes) and 60 moves, each a MouseMove the
// viewport's own handler turns into a gizmo write — with the driver ticking between the moves. What is asserted:
//   * every move wrote the node (the gizmo's drag, not a script's write);
//   * THE LAW, OFF THE SNAPSHOT, every tick (decisions 60, 63): at tick k+1's head, after its latch, the published
//     frame applied at least the serial tick k's record was submitted as — for every tick that neither overran a
//     display period itself (post to submit) nor submitted while a frame had been in flight longer than one; those
//     overruns are counted and printed (the owner's hitch budget). The tick is paced by the present at high priority;
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

// THE THREE SHAPES OF THE LOOP'S BLOCK (decision 65): the hand-over is the last thing before the drain, the publish the
// first after the frame's work, so the tick posted at the publish lands in whichever block follows — THE PAD (this
// rig's present does not block), A START-BLOCK (the runtime's xrWaitFrame: the test door vr.fakeRuntimePace at one
// display period) and AN END-BLOCK (a FIFO present: the test door view.fakePresentBlock at one display period).
var period = app.frameStats().displayPeriodMs;
var home = node.transform(cube).position;
function dragShape(shape, armName) {
    if (armName) assert(engine.arm(armName, period) !== null, shape + ": the arm " + armName + " = " + period.toFixed(2) + " ms");
    node.transform(cube, { position: { x: home.x, y: home.y, z: home.z } });
    editor.frame(4);
    for (var w = 0; w < 10; ++w) waitTick();   // the shape's own frames run before the window opens
    var x0 = node.transform(cube).position.x;
    var sync0 = engine.syncGetters().total, fs0 = app.frameStats();
    assert(editor.pointer("press", grab[0], grab[1]), shape + ": pressed on the X arrow at " + grab[0] + "," + grab[1]);
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
    if (armName) engine.arm(armName, 0);
    var carried = fs1.submit.carried - fs0.submit.carried, checked = fs1.submit.checked - fs0.submit.checked;
    var uiOver = fs1.submit.uiOverruns - fs0.submit.uiOverruns, frameOver = fs1.submit.frameOverruns - fs0.submit.frameOverruns;
    var excused = fs1.submit.carriedExcused - fs0.submit.carriedExcused, lawTicks = checked - uiOver - frameOver;
    var handOvers = fs1.submit.handOvers - fs0.submit.handOvers;
    var submitted = fs1.submit.submitted - fs0.submit.submitted, tickCount = fs1.ticks - fs0.ticks;
    console.log("EDITOR_DRAG_LATENCY shape=" + shape + " moves=" + moves + " wrote=" + wrote + " ticked=" + ticked +
                " ticks=" + tickCount + " submitted=" + submitted + " checked=" + checked + " law_ticks=" + lawTicks +
                " carried=" + carried + " ui_overruns=" + uiOver + " frame_overruns=" + frameOver +
                " carried_in_overruns=" + excused + " handOvers=" + handOvers + " syncReads=" + (sync1 - sync0) +
                " x " + x0.toFixed(3) + " -> " + last.toFixed(3));
    assert(wrote === moves, shape + ": every move wrote the node through the gizmo (" + wrote + " of " + moves + ")");
    assert(last > x0 + 0.1, shape + ": the drag moved the cube along +X (" + x0.toFixed(3) + " -> " + last.toFixed(3) + ")");
    assert(ticked === moves, shape + ": the driver ticked after every move (" + ticked + " of " + moves + ")");
    assert(submitted >= moves, shape + ": every tick submitted its record (" + submitted + ")");
    assert(checked >= moves, shape + ": the law was judged at every tick of the drag (" + checked + " ticks)");
    // THE LAW, ATTRIBUTED (decisions 63, 65): a tick that overran a display period itself (a: post to submit) or
    // submitted while a frame had been in flight longer than one (b) is the owner's hitch budget — PRINTED above;
    // every other tick's record is on screen by the next tick: 60 OF 60, carried 0.
    // (decision 65's bar: 60 of 60 among them — the overruns' misses are the owner's hitch budget, printed.)
    assert(carried === 0,
           shape + ": EVERY tick's record was on screen by the next tick: " + (checked - carried - excused) + " of " +
           checked + " (" + carried + " carried among the " + lawTicks + " law ticks; " + uiOver + " UI overruns and " +
           frameOver + " frame overruns, " + excused + " of them carried)");
    // Paced by the present each tick is posted by a frame's publish: the frame that posted the window's FIRST tick
    // began before the window opened, so the window holds a frame begun for every tick after it (ticks - 1 at least).
    assert(handOvers >= tickCount - 1, shape + ": the render thread began a frame for every tick (" + handOvers +
           " frames for " + tickCount + " ticks)");
    assert(sync1 - sync0 === 0, shape + ": the drag read the engine synchronously " + (sync1 - sync0) + " time(s)");
}
dragShape("pad", null);
dragShape("start-block", "vr.fakeRuntimePace");
dragShape("end-block", "view.fakePresentBlock");
console.log("editor_drag_latency: PASS");
