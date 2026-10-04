// scripting.e2e.engine_arms — THE MEASUREMENT ARMS' VERBS (lane TEST-1, the perf
// audit's A2). engine.arm(name, value?) / engine.arms() over Engine::setArm and
// Engine::arms: the registry that replaced the ray tier's per-frame environment
// doors (JAH_R5_NO_MOTION, JAH_R7_NO_POSED, JAH_R6_NO_ALPHA, JAH_R7_EDGE_CLASSES,
// JAH_R5_MONO_EYES, JAH_RQ_REFIT) and the per-frame JAHSHAKA_GATHER_NO_TEMPORAL,
// JAHSHAKA_ATOM_DECODE_OFF, JAHSHAKA_CARD_FOOTPRINT_K and JAHSHAKA_GI_FIELD_NO_SCROLL doors.
// What a harness relies on, asserted:
//   * the table: every arm listed with its default = the shipped picture, its
//     range and a sentence of what it changes;
//   * THE LATCH: a value set between two frames is read from the NEXT frame,
//     never half-way through one;
//   * the refusals: an unknown name and an out-of-range value answer null, name
//     the reason in app.lastError() and change nothing;
//   * the reset: the run leaves every arm at its default (a pool runs later
//     scripts in this same process, and an arm left set is a measurement left
//     running).
// The arms' EFFECTS are their suites' subject (gi.reflect_mover's paired cost arms,
// gi.rt_alpha_tested --cost, scripts/perf-ab.py), not this one's.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

var EXPECTED = { "reflect.motion": 1, "reflect.posed": 1, "reflect.alphaTested": 1,
                 "reflect.edgeClasses": 0, "reflect.monoEyes": 0, "rayquery.tlasRefit": 0,
                 "gather.temporal": 1, "atom.decode": 1, "cards.footprintTexels": 4, "gi.fieldScroll": 1 };

var guid = project.create("Engine Arms " + Date.now());
assert(guid.length > 10, "project.create -> " + guid);
editor.frame(2);

var list = engine.arms();
try {
    assert(list.length === Object.keys(EXPECTED).length,
           "engine.arms() lists the " + Object.keys(EXPECTED).length + " registered arms (" + list.length + ")");
    for (var i = 0; i < list.length; ++i) {
        var a = list[i];
        assert(EXPECTED.hasOwnProperty(a.name), "arm " + a.name + " is one of the registered names");
        assert(a["default"] === EXPECTED[a.name] && a.value === a["default"],
               a.name + ": default " + a["default"] + " (the shipped picture) and the frame reads it");
        assert(a.min <= a["default"] && a["default"] <= a.max, a.name + ": the default is inside [min, max]");
        assert(typeof a.what === "string" && a.what.length > 40, a.name + ": says what it changes");
    }

    // THE LATCH: set now, read the old value until a frame begins.
    assert(engine.arm("reflect.motion") === 1, "engine.arm(name) reads the frame's value (1)");
    assert(engine.arm("reflect.motion", 0) === 0, "engine.arm(name, 0) answers the value set");
    assert(engine.arm("reflect.motion") === 1, "...which the CURRENT frame does not see (latched at the next)");
    editor.frame(1);
    assert(engine.arm("reflect.motion") === 0, "...and the next frame does");
    assert(engine.arm("reflect.edgeClasses", 2) === 2, "a non-boolean arm takes its range's value (2)");
    editor.frame(1);
    assert(engine.arm("reflect.edgeClasses") === 2, "reflect.edgeClasses latched at 2");

    // THE REFUSALS: null, a reason, nothing changed.
    assert(engine.arm("no.such.arm", 1) === null, "an unknown name is refused (null)");
    assert(app.lastError().indexOf("no arm named") >= 0, "...and app.lastError() names why: " + app.lastError());
    assert(engine.arm("no.such.arm") === null, "reading an unknown name is refused too");
    assert(engine.arm("reflect.posed", 2) === null, "a value outside the arm's range is refused (2 > max 1)");
    assert(app.lastError().indexOf("outside") >= 0, "...with the range in the reason: " + app.lastError());
    editor.frame(1);
    assert(engine.arm("reflect.posed") === 1, "...and changed nothing (reflect.posed still 1)");
} finally {
    // THE RESET, whatever happened above: every arm back to its default.
    for (var name in EXPECTED) engine.arm(name, EXPECTED[name]);
    editor.frame(1);
}
var after = engine.arms();
for (var j = 0; j < after.length; ++j)
    assert(after[j].value === after[j]["default"], after[j].name + " is back at its default");
console.log("engine_arms: PASS");
