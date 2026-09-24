// scripting.e2e.gather_row — THE SCREEN-PROBE GATHER IS A ROW OF THE PROJECT
// (SPECS/SCREEN_PROBE_GATHER_SPEC.md phase 1; GATHER-1a).
//
// The API-first law's half of this lane: a new editor capability lands as a
// VERB with a test driving it, and only then as UI. The verb is
// `world.gi({gather})` and it takes the same off|auto|on word every other
// three-state GI row takes — with `auto` meaning THE TIER'S since
// PHOTON-GATHER-1d (the engine's tier table: on at Medium and above, off at Low;
// gi.gather_default drives the rule through every tier).
//
// WHAT THIS MEASURES, and why each one is here:
//   1. the key is ACCEPTED — for one round it was not in `world.gi`'s known-key
//      list, so the verb refused it and the handler behind it was unreachable:
//      the lane's one user door, dead, with every suite passing (they drive the
//      engine through its own API, not through the verb);
//   2. the write is UNDOABLE — it rides WorldEdit like every other world field,
//      which needs the key in `giPlainKeys()` AND in the scene-property
//      registry, or the value is written outside the undo step and a later
//      refusal in the same call does not roll it back;
//   3. a bad value is REFUSED, loudly, rather than guessed at;
//   4. `world.giStatus().gather` answers for the MACHINE: `on` is the row
//      resolved against it, never what the document asked for.
function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

// ---- 1. the key is accepted, and the row reads back -----------------------
assert(world.gi({ gather: "off" }), "world.gi accepts gather: off");
// (the row is READ through `world.get().gi` — `world.gi()` is a SETTER and
// answers a bool, like every other world setter; and the three-state rows read
// back as true / false / "auto", which is `giToggleToJs`.)
assert(world.get().gi.gather === false, "...and the row reads back off");

assert(world.gi({ gather: "on" }), "world.gi accepts gather: on");
assert(world.get().gi.gather === true, "...and the row reads back on");

// ---- 2. the write is ONE undo step, recorded -----------------------------
// A SCRIPT RUN IS ITSELF ONE UNDO MACRO, so `editor.undo()` inside the run does
// not unwind a single verb — what a script can measure is that the verb PUSHED
// a step, which is what says the write went through WorldEdit and not around
// it. That is the whole of the defect this case exists for: the row was missing
// from `giPlainKeys()` and from the scene-property registry, so the value was
// written outside the undo step (not undoable, and not rolled back when a later
// key of the same call is refused).
function pushes() { return editor.undoState().pushes; }
var beforePushes = pushes();
assert(world.gi({ gather: "off" }), "the row goes off again");
assert(pushes() === beforePushes + 1,
       "THE WRITE IS INSIDE AN UNDO STEP: world.gi records exactly one push for it");
assert(world.get().gi.gather === false, "...and the row moved");
assert(world.gi({ gather: "on" }), "...and back on");
assert(world.get().gi.gather === true, "...which moved it again");

// ---- 3. a bad value is refused -------------------------------------------
var refused = false;
try { refused = !world.gi({ gather: "sometimes" }); } catch (e) { refused = true; }
assert(refused, "an unknown value is REFUSED rather than guessed at");
assert(world.get().gi.gather === true, "...and the row is untouched by the refusal");

// ---- 4. the status answers for the machine -------------------------------
// A FRAME FIRST, and it is not a formality: `giStatus()` is the RENDERER's
// answer, and the row reaches the renderer through the mirror, which runs when
// something draws. Reading it in the same breath as the write reads the state
// before the push — which is exactly right, and exactly why this line is here.
editor.frame(2);
var st = world.giStatus().gather;
assert(typeof st === "object" && st !== null, "world.giStatus().gather is an object");
var rays = world.giStatus().rayQuery;
console.log("   gather: on " + st.on + ", running " + st.running + ", probes " + st.probes +
            " (" + st.probesX + "x" + st.probesY + "), adaptive " + st.adaptive +
            " of " + st.adaptiveCap + " (requested " + st.adaptiveRequested + "), rays/probe " +
            st.raysPerProbe + ", atlas " + (st.atlasBytes / 1048576).toFixed(2) + " MB; " +
            "rayQuery available " + rays.available + ", enabled " + rays.enabled);
if (rays.available && rays.enabled) {
    assert(st.on, "THE ROW RESOLVED ON: this machine traces, so the project's row is honoured");
} else {
    assert(!st.on,
           "THE MACHINE ANSWERS, NOT THE DOCUMENT: no ray query here, so the row resolves off " +
           "and the picture is the one this machine already drew");
}

// ---- 5. auto is the tier's (PHOTON-GATHER-1d) ------------------------------
// A new project is born at Epic, whose gather row is on: "auto" resolves ON on
// a machine that traces, exactly as "on" did.
assert(world.gi({ gather: "auto" }), "world.gi accepts gather: auto");
assert(world.get().gi.gather === "auto", "...and the row reads back auto");
editor.frame(2);
if (rays.available && rays.enabled)
    assert(world.giStatus().gather.on, "AUTO IS THE TIER'S: Epic's gather row resolves on here");

// ...and off again leaves the status off, whatever the machine is.
assert(world.gi({ gather: "off" }), "the row goes back off");
editor.frame(2);
assert(!world.giStatus().gather.on, "...and the status says so, a frame later");
assert(!world.giStatus().gather.running,
       "...and nothing is running one: the Component gives its atlases back on the " +
       "first frame the row is off");
console.log("PASSED");
