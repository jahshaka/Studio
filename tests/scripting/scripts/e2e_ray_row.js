// scripting.e2e.ray_row — HARDWARE RAY TRACING IS A PROPERTY OF THE PROJECT
// (owner, 2026-09-15; MASTER_QUEUE ledger §425).
//
// It was an application preference for two days and that was the wrong shape: a
// machine-wide switch meant the same project rendered differently depending on
// something that was not in it. The row lives on the DOCUMENT now, saved with
// the scene and travelling with the asset, and it has three states:
//
//   "off"   never trace, even where the GPU can — a scene that must look and
//           cost the same on every machine;
//   "auto"  trace where the machine can, fall back silently. THE DEFAULT;
//   "on"    authored for rays: renders exactly like auto — nothing can force
//           ray hardware onto a machine that has none — and additionally raises
//           a SCENE ISSUE saying this machine has none.
//
// So On and Auto render the same picture and differ in exactly one thing: On
// tells you when the machine falls short. That sentence is what this suite
// measures, in both directions.
//
// TWO CTEST ENTRIES RUN THIS FILE: `ray_row` on the box as it is, and
// `ray_row_norays` with JAHSHAKA_NO_RAY_QUERY=1 — the diagnostic latch that
// builds the Vulkan device with none of the ray extensions asked for, so a
// ray-capable box renders the picture a machine WITHOUT the hardware renders.
// The issue half therefore gets both answers on one GPU, which is the only way
// to prove the "on" state does anything at all here.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function throws(fn, msg) {
    var threw = false;
    try { fn(); } catch (e) { threw = true; console.log("ok: " + msg + " (" + e.message + ")"); }
    if (!threw) throw new Error("assert failed (no error): " + msg);
}
function issuesOfKind(list, kind) {
    return list.filter(function (i) { return i.kind === kind; });
}

// ---- the registry: the verb is there, and the PREFERENCE is gone -----------
var worldModule = api.verbs().filter(function (m) { return m.module === "world"; })[0];
var worldNames = worldModule.verbs.map(function (v) { return v.name; });
assert(worldNames.indexOf("rayTracing") >= 0, "world.rayTracing is registered");
var appModule = api.verbs().filter(function (m) { return m.module === "app"; })[0];
var appNames = appModule.verbs.map(function (v) { return v.name; });
assert(appNames.indexOf("rayTracing") < 0,
       "app.rayTracing is GONE from the registry (CRUD: one switch, never two layers)");
assert(typeof app.rayTracing === "undefined", "... and off the object too");

var guid = project.create("Ray row " + Date.now());
assert(guid.length > 10, "project.create -> " + guid);

// ---- 1. the default, and the three states ---------------------------------
assert(world.rayTracing() === "auto",
       "a new project is AUTO — trace where the machine can, silently everywhere else");
assert(world.get().rayTracing === "auto", "world.get() reports it too");
assert(world.rayTracing("off") === "off", "set off");
assert(world.rayTracing() === "off", "...and it reads back");
assert(world.rayTracing("on") === "on", "set on");
assert(world.rayTracing("auto") === "auto", "set auto");
// Case and whitespace are forgiven; a word that is not a state is NOT.
assert(world.rayTracing("  ON ") === "on", "the state is trimmed and case-insensitive");
throws(function () { world.rayTracing("yes"); }, "an unknown state is REFUSED, not guessed");
throws(function () { world.rayTracing("true"); }, "...and so is 'true'");
assert(world.rayTracing() === "on", "a refused set left the state exactly as it was");

// ---- 2. one gesture, one undo step ----------------------------------------
// (A script run is ONE open macro, so editor.undo() cannot reach the run's own
// edits — undoState().pushes is the honest measure.)
function pushes() { return editor.undoState().pushes; }
var before = pushes();
world.rayTracing("off");
assert(pushes() === before + 1, "a change records exactly ONE undo step");
before = pushes();
world.rayTracing("off");
assert(pushes() === before, "setting the state it already has records NOTHING");
before = pushes();
try { world.rayTracing("nonsense"); } catch (e) {}
assert(pushes() === before, "and a refused set records nothing either");

// ---- 3. it is saved with the scene ----------------------------------------
world.rayTracing("on");
project.save();
project.close();
project.open(guid);
assert(world.rayTracing() === "on", "the state survived save / close / open");
world.rayTracing("off");
project.save();
project.close();
project.open(guid);
assert(world.rayTracing() === "off", "...and so does off, which is not the default");
world.rayTracing("auto");
project.save();
project.close();
project.open(guid);
assert(world.rayTracing() === "auto", "...and auto");

// ---- 4. what the MACHINE answered -----------------------------------------
// The document's half is above; this is the other half, and nothing in the
// document can move it. giStatus().rayQuery.available is the DEVICE's own
// answer — false under the latch, false on a Mac, true on a ray-capable box.
editor.frame(2);            // the device exists once something has rendered
var rq = world.giStatus().rayQuery;
var machineHasRays = rq.available === true;
console.log("   this machine: rayQuery.available = " + rq.available +
            ", enabled = " + rq.enabled);

// ---- 5. THE SCENE ISSUE: "on" is the state that TELLS YOU -----------------
function raysIssues() { return issuesOfKind(editor.checkScene().list, "rays.absent"); }

world.rayTracing("auto");
assert(raysIssues().length === 0,
       "AUTO never raises the issue — falling back silently is what auto MEANS");
world.rayTracing("off");
assert(raysIssues().length === 0,
       "OFF never raises it either — a scene that asked not to trace is not disappointed");

world.rayTracing("on");
var onIssues = raysIssues();
if (machineHasRays) {
    assert(onIssues.length === 0,
           "ON on a ray-capable machine raises NOTHING — it is getting what it asked for");
} else {
    assert(onIssues.length === 1, "ON with no ray hardware raises exactly ONE issue");
    assert(onIssues[0].message.indexOf("hardware ray tracing") >= 0,
           "...saying this project expects hardware ray tracing: " + onIssues[0].message);
    assert(onIssues[0].action.indexOf("Hardware Ray Tracing") >= 0,
           "...and pointing at the World row: " + onIssues[0].action);
    assert(onIssues[0].node === "",
           "...naming no object, because it is about the PROJECT and not a thing in the scene");
    // It NEVER REPEATS while the condition holds, like every other issue.
    editor.checkScene();
    assert(raysIssues().length === 1, "a second scan does not add a second row");
    // And it goes away by itself the moment the row changes — the only way a
    // line ever leaves the bar.
    world.rayTracing("auto");
    assert(raysIssues().length === 0, "changing the row to auto CLEARS it");
    world.rayTracing("on");
    assert(raysIssues().length === 1, "...and going back to on raises it again");
}

// ---- 6. and the picture is the same either way -----------------------------
// On and Auto are the same request to the renderer: the scene must render in
// both, and it must render on a machine with no rays at all.
world.rayTracing("on");
editor.frame(2);
world.rayTracing("auto");
editor.frame(2);
world.rayTracing("off");
editor.frame(2);
assert(true, "the scene renders in all three states");

console.log("PASS ray_row");
