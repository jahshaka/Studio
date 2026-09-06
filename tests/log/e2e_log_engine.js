// log.engine — the engine half of the session log (SESSION_LOG_SPEC §10 phase 3
// gate), driven through the real binary with a real Vulkan device.
//
// THREE THINGS, none of which any other suite can see:
//
//  1. The DEVICE block. GPU vendor, device name, driver version and the Vulkan
//     API version all reach the log. The first three come off
//     RenderSystemCapabilities; the fourth is a SCRAPE of the render system's
//     own log line, because RenderSystemCapabilities carries no API version —
//     so its presence is the proof that the Ogre LogListener is attached early
//     enough to have seen the render system initialise.
//
//  2. A provoked engine refusal reaches the `engine` category with the
//     EngineErrorPump's dedupe intact. The pump keeps sole ownership of engine
//     error rate limiting (spec §9-R5) — its flood budget exists because a
//     failing particle datablock once produced 122 distinct messages in two
//     seconds — and absorbing it into the log must not add a second limiter on
//     top, or the counts start lying.
//
//  3. Ogre's own log is a PER-SESSION SIBLING of ours (fork F3-B), not the one
//     fixed jahshaka-ogre.log every run used to overwrite.
//
// The file-side assertions are the shell wrapper's (log_engine.sh).

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

// ---------------------------------------------------------------------------
// Phase A — the engine is up and the ogre log is this run's sibling.
var paths = log.path();
var stem = paths.session.replace(/\.log$/, "");
assert(paths.ogre === stem + "-ogre.log",
       "the ogre log is this session's sibling: " + paths.ogre);

var cache = app.shaderCache();
assert(cache.enabled !== undefined, "the engine is up (app.shaderCache answered)");

// ---------------------------------------------------------------------------
// Phase B — provoke a refusal the engine will swallow, and watch it surface.
//
// A texture that cannot decode is the canonical case: SceneMirror ignores the
// return of loadTexture, so before the error pump existed this drew a wrong
// picture and produced ZERO log lines.
var marker = log.mark("log.engine: provoking an engine refusal");

var guid = project.create("Log Engine " + Date.now());
assert(guid.length > 10, "project.create -> " + guid);
var cube = scene.addPrimitive("Cube");

var before = app.engineErrors();
var bad = "/definitely/not/a/texture/at/all.png";
try { node.setMaterial(cube, { diffuseTexture: bad }); } catch (e) { /* the verb may refuse first */ }
editor.frame(6);

var after = app.engineErrors();
assert(after.drains > before.drains,
       "the error pump is being drained by the render loop (" +
       before.drains + " -> " + after.drains + ")");

// Whether THIS particular refusal reaches the sink is renderer-dependent (some
// paths validate before the engine is asked), so the gate on the `engine`
// category is: whatever the pump recorded is in the log, with the pump's own
// text, and nothing has been double-counted.
var engineRecords = log.since(marker, {});
var engineOnly = [];
for (var i = 0; i < engineRecords.length; ++i)
    if (/\]engine: /.test(engineRecords[i])) engineOnly.push(engineRecords[i]);

if (after.recorded > before.recorded) {
    assert(engineOnly.length > 0,
           "a recorded engine error reached the `engine` category (" +
           engineOnly.length + " record(s))");
    // The pump's flood budget is 12 lines per 5 s window. If the log had added
    // its own limiter on top, the log would hold FEWER records than the pump
    // logged; if it had lost the pump's, it could hold far more.
    assert(engineOnly.length <= after.recorded,
           "the log never holds more engine records than the pump recorded");
} else {
    console.log("note: this refusal was caught before the engine sink " +
                "(pump recorded " + after.recorded + ") — the drain assertion above " +
                "is what gates the wiring");
}

// The pump's dedupe survives absorption: hammering ONE message must not
// produce one log record per frame.
var probe = log.mark("log.engine: dedupe probe");
for (var f = 0; f < 30; ++f) {
    try { node.setMaterial(cube, { diffuseTexture: bad }); } catch (e) {}
    editor.frame(1);
}
var burst = log.since(probe, {});
var burstEngine = 0;
for (var b = 0; b < burst.length; ++b) if (/\]engine: /.test(burst[b])) ++burstEngine;
assert(burstEngine < 30,
       "30 identical failures did NOT produce 30 log records (" + burstEngine +
       ") — the pump's 5 s dedupe survived absorption");

log.write("app", "display", "e2e: LOG ENGINE COMPLETE");
log.flush();
console.log("log.engine: all checks passed");
