// host.error_door — THE HOST'S ERROR DOOR NEVER TOUCHES THE FRAME'S SINK (ENGINE-UPGRADE-U3 decision 44, D1).
// FREE-RUNNING (JAH_FREE_RUNNING=1, --script-live: the owner's mode — the driver kicks every frame onto the render
// thread and its tick drains the error pump at once). Over 300 driver frames every scene message is refused
// (engine.arm("messages.refuse"): the frame writes the engine's error sink every frame) while one message is posted
// per frame and the pump drains each tick: the refusals reach the pump through the snapshot (the frames' refusals),
// the host's door takes only the host's own string, and the process lives.
function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
if (project.create("error door " + Date.now()) === false) throw new Error("project.create failed: " + app.lastError());
if (app.columns().space !== "editor") app.space("editor");
var cube = scene.addPrimitive("cube", { position: [0, 0.5, 0] });
editor.frame(30);
var before = app.engineErrors();
assert(engine.arm("messages.refuse", 1) !== null, "the refusing arm is set");
var driverFrames = 0;
for (var i = 0; i < 300; ++i) {
    node.setProperty(cube, "visible", i % 2 === 0);   // one scene message per frame (setNodeVisible)
    var r0 = app.frameStats().rendered, polls = 0;
    while (app.frameStats().rendered === r0 && ++polls < 200000) {}
    if (app.frameStats().rendered !== r0) ++driverFrames;
}
engine.arm("messages.refuse", 0);
editor.frame(4);
var after = app.engineErrors();
console.log("driver frames " + driverFrames + ", pump drains " + (after.drains - before.drains) + ", recorded " +
            (after.recorded - before.recorded));
assert(driverFrames >= 290, "the driver ticked FREE-RUNNING between the posts (" + driverFrames + " of 300)");
assert(after.drains - before.drains >= 290, "the pump drained every tick (" + (after.drains - before.drains) + ")");
assert(after.recorded - before.recorded >= 290,
       "every frame's refusal reached the pump through the snapshot (" + (after.recorded - before.recorded) + ")");
console.log("host_error_door: PASS");
