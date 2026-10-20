// shadercache.save_off_frame — THE SHADER-CACHE SAVE LEAVES THE FRAME (FRAME-MONITOR-DOOR-1 stage 4).
// FREE-RUNNING (JAH_FREE_RUNNING=1, --script-live: the owner's mode — the save is an engine MESSAGE, so it runs inside
// a render frame). The run, twice by its driver (save_off_frame.sh): COLD on a fresh data root, then WARM on the same
// root. A compile burst (a cube selected under the translate gizmo: its outline and gizmo shaders compile even warm),
// then the watchdog's save, waited for in FRAMES (the writer's finished writes, app.shaderCache().save.written).
// THE GUARD COUNTS (tests count frames, never a clock): on the cold arm the copy ran in more messages than saves (it
// chunked); no chunked message took more than K entries (save.chunkMaxEntries <= save.chunkEntries); the writer's
// "saved N files" arrived. THE WALL-CLOCK JUDGEMENTS ARE THE MEASURE SIBLING'S (shadercache.save_off_frame.timing, alone
// on the card; its driver reads the SAVE_OFF_FRAME line printed here): the frames that CARRIED the save's messages carry
// no frame overrun (save.frameOverruns: the driver's attribution — a frame's work over the display period — counted by
// the render loop on exactly those frames), the longest message <= 2 ms, the longest chunk <= 1 ms. A frame overrun is a
// frame's work against the DISPLAY PERIOD, a clock: in the ship's guard pass eight rows share the card and any sibling's
// load overruns any frame, the save's included (ship 1bdb9d4e0, 2026-10-20: 1 of 10 cold frames overran under the
// 8-wide pass, 0 of 10 alone) — so this guard COUNTS the frames and prints the overruns, and the measure row judges them.
// BEFORE (70ab28e3f, measured by hand: spikes/frame-monitor-door/stage4-measure.txt): one message of 22.7 ms cold,
// 12.5 ms warm; the drag row's pad shape 1-2 frame overruns with the save in its window.
function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function J(x) { return JSON.stringify(x); }
var warm = app.shaderCache().hlmsCachesLoaded > 0, tag = warm ? "warm" : "cold";
if (project.create("save off frame " + Date.now(), { template: "empty" }) === false)
    throw new Error("project.create failed: " + app.lastError());
if (app.columns().space !== "editor") app.space("editor");
var cube = scene.addPrimitive("cube", { position: [0, 0.5, 0] });
scene.addLight("directional", { position: { x: 2, y: 8, z: 3 } });
assert(editor.select(cube), tag + ": the cube is selected");
assert(editor.setGizmoMode("translate"), tag + ": the translate gizmo is active");
editor.setCamera({ position: { x: 0, y: 2.5, z: 8 }, lookAt: { x: 0, y: 0.5, z: 0 } });
editor.frame(30);

// THE WINDOW: until the writer has finished one more write (the burst settles, the watchdog's three quiet ticks, the
// copy, the write) — counted in ticks, the shader-cache verb read once every 30 (it is a request: a directory scan).
var c0 = app.shaderCache().save, f0 = app.frameStats(), written = c0.written, ticks = 0;
var lastTicks = f0.ticks, polls = 0;
while (written === c0.written && ticks < 1500 && ++polls < 20000000) {
    var f = app.frameStats();
    if (f.ticks !== lastTicks) {
        ticks += f.ticks - lastTicks;
        lastTicks = f.ticks;
        if (ticks % 30 === 0) written = app.shaderCache().save.written;
    }
}
// ...and a few frames past the write, so the frame that carried its last message is judged.
var t1 = app.frameStats().ticks;
while (app.frameStats().ticks - t1 < 10 && ++polls < 20000000) {}
var f1 = app.frameStats(), c1 = app.shaderCache().save;
var frameOver = f1.submit.frameOverruns - f0.submit.frameOverruns, uiOver = f1.submit.uiOverruns - f0.submit.uiOverruns;
console.log("SAVE_OFF_FRAME " + tag + " ticks=" + ticks + " save=" + J(c1) + " frame_overruns=" + frameOver +
            " ui_overruns=" + uiOver + " checked=" + (f1.submit.checked - f0.submit.checked));
assert(c1.written > c0.written, tag + ": the writer's \"saved N files\" arrived (writes " + c0.written + " -> " +
       c1.written + ", " + ticks + " ticks)");
assert(c1.copied >= 1 && c1.messages >= c1.copied, tag + ": the copies were made on the render thread (" + c1.copied +
       " save(s) in " + c1.messages + " message(s))");
if (!warm)
    assert(c1.messages > c1.copied, tag + ": the copy chunked (" + c1.copied + " save(s) in " + c1.messages + " messages)");
assert(c1.chunkEntries > 0 && c1.chunkMaxEntries <= c1.chunkEntries,
       tag + ": no chunked message took more than K entries (" + c1.chunkMaxEntries + ", K " + c1.chunkEntries + ")");
assert(c1.frames >= 1, tag + ": the save's messages rode " + c1.frames + " frame(s) (overruns on them " +
       c1.frameOverruns + ", the window's driver count " + frameOver + " — judged by the .timing sibling)");
console.log("save_off_frame " + tag + ": PASS");
app.quit();
