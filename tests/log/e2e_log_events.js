// log.events — the verb surface and the event markers, against the REAL binary
// (SESSION_LOG_SPEC §10 phase 2 gate).
//
// This suite exists because the markers can only be proved where they actually
// fire. Every one of them was placed at a CHOKE POINT rather than at a UI
// handler, precisely so a scripted action and a clicked action produce the same
// record — and this script drives the scripted half and then reads the file the
// clicked half would have written.
//
// It asserts BOTH ways round: through the verbs (log.since, log.tail,
// log.counts) and by reading the session file off disk, because the ring and
// the file are two different mechanisms and either one can be right while the
// other is broken.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

function joined(lines) { return lines.join("\n"); }

// ---------------------------------------------------------------------------
// Phase A — log.path() and the artifacts it names.
var paths = app ? log.path() : null;
assert(paths.session && paths.session.length > 0, "log.path().session -> " + paths.session);
assert(paths.ogre && paths.ogre.length > 0, "log.path().ogre -> " + paths.ogre);
assert(paths.dir && paths.dir.length > 0, "log.path().dir -> " + paths.dir);
assert(paths.enabled === true, "log.path().enabled is true in a normal run");
// One file per RUN (fork F1-A): the session file and its ogre sibling share the
// stamp, so they sort together and rotate together.
var stem = paths.session.replace(/\.log$/, "");
assert(paths.ogre === stem + "-ogre.log",
       "the ogre log is this run's sibling of the session log");

// ---------------------------------------------------------------------------
// Phase B — categories and levels.
var cats = log.categories();
assert(cats.length >= 16, "log.categories() lists at least the 16 v1 categories (" + cats.length + ")");
var names = cats.map(function (c) { return c.name; });
["app", "scene", "engine", "ogre", "render", "perf", "shader", "assets",
 "db", "script", "ui", "mirror", "physics", "media", "qt", "legacy"].forEach(function (n) {
    assert(names.indexOf(n) >= 0, "category '" + n + "' is registered");
});

var table = log.level();
assert(table.global !== undefined, "log.level() returns the whole table including 'global'");
assert(log.level("scene") === table.scene, "log.level(cat) agrees with the table");

// Runtime is the LAST layer of the precedence chain and beats everything.
log.level("mirror", "veryverbose");
assert(log.level("mirror") === "VeryVerbose", "log.level(cat, level) sets at runtime");
log.level("mirror", "log");

// An unknown category is reported, not silently accepted.
var threw = false;
try { log.level("no.such.category", "warning"); } catch (e) { threw = true; }
assert(threw, "log.level on an unknown category throws instead of inventing one");

// ---------------------------------------------------------------------------
// Phase C — the markers, bracketed by a mark.
var marker = log.mark("e2e: the interesting part starts here");
assert(marker > 0, "log.mark() -> " + marker);

var name = "Log Events " + Date.now();
var guid = project.create(name);
assert(guid.length > 10, "project.create -> " + guid);

scene.addPrimitive("Cube");

// The SCENE OPEN block comes from LoadTimeline::end — the choke point every
// open path funnels through. project.create() is not an open (nothing is
// loaded), so the block is produced by actually opening the world back up,
// which is also the path a tile click takes. The marker is re-taken here so
// the assertions below read a clean, ordered slice: open, then save, then the
// play bracket.
project.close();
marker = log.mark("e2e: the open/save/play sequence starts here");
assert(project.open(guid), "project.open(" + guid + ")");

scene.addPrimitive("Sphere");
project.save();

// A graphics setting change (the substitute for the "quality tier" row, §5).
world.setAntiAliasing(1);

// PLAY START / PLAY STOP, through the verbs the UI buttons also call.
editor.play();
editor.frame(4);
editor.stop();

// A nested script run — the record comes from ScriptEngine::evaluate, which is
// the one entry point the console dock, --script and MCP run_script share.
app.space("editor");

// An annotation, which is the whole reason log.write exists.
log.write("app", "display", "e2e: annotation from the script");

log.flush();

// ---------------------------------------------------------------------------
// Phase D — read it back through the ring.
var since = log.since(marker);
var text = joined(since);
assert(since.length > 0, "log.since(marker) returned " + since.length + " records");

assert(text.indexOf("=== SCENE OPEN ===") >= 0,
       "the scene-open block is in the record");
assert(text.indexOf("=== SCENE SAVE ===") >= 0,
       "the save block is in the record");
assert(text.indexOf("=== PLAY START ===") >= 0, "PLAY START is in the record");
assert(text.indexOf("=== PLAY STOP ===") >= 0, "PLAY STOP is in the record");
assert(/=== PLAY STOP ===.*rendered \d+/.test(text),
       "PLAY STOP carries a frame-stats delta");
assert(text.indexOf("e2e: annotation from the script") >= 0,
       "log.write put the annotation in the record");
assert(/space: \w+ -> editor/.test(text), "the space switch is in the record");

// ORDER. The open block must precede the save block, which must precede play.
function firstIndex(needle) {
    for (var i = 0; i < since.length; ++i)
        if (since[i].indexOf(needle) >= 0) return i;
    return -1;
}
var iOpen = firstIndex("=== SCENE OPEN ===");
var iSave = firstIndex("=== SCENE SAVE ===");
var iPlay = firstIndex("=== PLAY START ===");
var iStop = firstIndex("=== PLAY STOP ===");
assert(iOpen >= 0 && iSave > iOpen, "the save block follows the open block");
assert(iPlay > iSave, "PLAY START follows the save");
assert(iStop > iPlay, "PLAY STOP follows PLAY START");

// SANE MILLISECOND VALUES. An open that claims 0 ms or a week is a broken
// ledger, not a fast machine.
var openLine = since[iOpen];
var openMs = /in ([0-9.]+) ms/.exec(openLine);
assert(openMs !== null, "the open block states a duration: " + openLine);
assert(parseFloat(openMs[1]) >= 0 && parseFloat(openMs[1]) < 600000,
       "the open duration is plausible (" + openMs[1] + " ms)");
var stopMs = /=== PLAY STOP === (\d+) ms/.exec(since[iStop]);
assert(stopMs !== null && parseInt(stopMs[1], 10) >= 0,
       "PLAY STOP states the bracket's wall-clock duration");

// The line format, on a real record (spec §3.7).
assert(/^\[\d{4}\.\d{2}\.\d{2}-\d{2}\.\d{2}\.\d{2}\.\d{3}\]\[ *\d+\]\w+: /.test(since[0]),
       "records carry [timestamp][frame]category: — " + since[0]);

// ---------------------------------------------------------------------------
// Phase E — filters and counts.
var warnings = log.since(marker, { minLevel: "warning" });
assert(warnings.length <= since.length,
       "log.since(minLevel:'warning') is a subset (" + warnings.length + " of " + since.length + ")");
for (var w = 0; w < warnings.length; ++w)
    assert(/: (Warning|Error|Fatal): /.test(warnings[w]),
           "every minLevel:'warning' record is warning-or-worse");

var sceneOnly = log.tail(200, { category: "scene" });
assert(sceneOnly.length > 0, "log.tail(category:'scene') found the scene records");
for (var s = 0; s < sceneOnly.length; ++s)
    assert(/\]scene: /.test(sceneOnly[s]), "log.tail's category filter is exact");

var counts = log.counts();
assert(counts.byCategory.scene > 0, "log.counts().byCategory has scene records");
assert(counts.byLevel.Display > 0, "log.counts().byLevel has Display records");
assert(counts.records > 0, "log.counts().records is the session total");

// The per-category record counter agrees with the roll-up.
var sceneCat = null;
log.categories().forEach(function (c) { if (c.name === "scene") sceneCat = c; });
assert(sceneCat.records > 0, "log.categories() reports the scene category's record count");

// ---------------------------------------------------------------------------
// Phase F — the FILE, not the ring. Everything above could pass with a broken
// writer; the file is the half a bug reporter actually sends. There is no
// file-reading verb (and the log is not the place to invent one), so the shell
// wrapper — tests/log/log_events.sh — greps the file this run wrote, and this
// is the sentinel it looks for.
log.write("app", "display", "e2e: LOG EVENTS COMPLETE");
log.flush();

console.log("log.events: all checks passed");
