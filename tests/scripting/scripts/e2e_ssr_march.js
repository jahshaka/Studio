// scripting.e2e.ssr_march — THE SCREEN-SPACE MARCH'S PHASE RULE, AS A VERB
// (SSR-RINGS-1; SMOKE-41 item 1a, the render audit's SYNTHESIS item 12).
//
// The march walks a reflected ray in FIXED STEPS and asks two questions at
// whichever sample it lands on — did I cross a surface, and how much does the
// depth buffer vouch for the crossing — so both answers carry the step's own
// phase. On a curved glossy surface that is the Showroom's nested concentric
// crescents: measured on the Showroom itself, the accepted hit region covers
// 15.3 % of the crop at a 1.04 m step, 29.5 % at the shipped 0.26 m, 34.3 % at
// 0.13 m and 58.4 % when the thickness tolerance is widened instead.
//
// This suite is the DOCUMENT half of the knob that lets the owner choose from
// the pictures: the verb, its three spellings, its refusals, its one undo step
// and its survival of save/close/open. The PIXELS are `ssr.rings` (the engine
// suite that measures the step's grip on the footprint) and the lane's evidence
// directory (the Showroom pairs).
//
// THE DEFAULT IS `checker` — the shipped march, exactly — so nothing that ships
// moves until a project asks for something else. That is the assertion this
// file opens with and the one that matters most.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function throws(fn, msg) {
    var threw = false;
    try { fn(); } catch (e) { threw = true; console.log("ok: " + msg + " (" + e.message + ")"); }
    if (!threw) throw new Error("assert failed (no error): " + msg);
}

// ---- the registry ----------------------------------------------------------
var worldModule = api.verbs().filter(function (m) { return m.module === "world"; })[0];
var worldNames = worldModule.verbs.map(function (v) { return v.name; });
assert(worldNames.indexOf("ssrMarch") >= 0, "world.ssrMarch is registered");

var guid = project.create("SSR march " + Date.now());
assert(guid.length > 10, "project.create -> " + guid);

// ---- 1. the default and the three rules ------------------------------------
assert(world.ssrMarch() === "checker",
       "a new project marches with `checker` — the shipped rule, so no scene moves");
assert(world.ssrMarch("refined") === "refined", "set refined");
assert(world.ssrMarch() === "refined", "...and it reads back");
assert(world.ssrMarch("dither") === "dither", "set dither");
assert(world.ssrMarch("checker") === "checker", "set checker");
assert(world.ssrMarch("  REFINED ") === "refined", "the rule is trimmed and case-insensitive");
throws(function () { world.ssrMarch("bisect"); }, "an unknown rule is REFUSED, not guessed");
throws(function () { world.ssrMarch("true"); }, "...and so is a boolean spelling");
assert(world.ssrMarch() === "refined", "a refused set left the rule exactly as it was");

// ---- 2. one gesture, one undo step -----------------------------------------
// (A script run is ONE open macro, so editor.undo() cannot reach the run's own
// edits — undoState().pushes is the honest measure, as in e2e_ray_row.)
function pushes() { return editor.undoState().pushes; }
var before = pushes();
world.ssrMarch("checker");
assert(pushes() === before + 1, "a change records exactly ONE undo step");
before = pushes();
world.ssrMarch("checker");
assert(pushes() === before, "setting the rule it already has records NOTHING");
before = pushes();
try { world.ssrMarch("nonsense"); } catch (e) {}
assert(pushes() === before, "and a refused set records nothing either");

// ---- 3. it is saved with the scene -----------------------------------------
world.ssrMarch("refined");
project.save();
project.close();
project.open(guid);
assert(world.ssrMarch() === "refined", "the rule survived save / close / open");
world.ssrMarch("checker");
project.save();
project.close();
project.open(guid);
assert(world.ssrMarch() === "checker", "...and so does the default, written explicitly");

// ---- 4. the scene renders under every rule ---------------------------------
// The rule is a UNIFORM, not a graph change: switching it must not rebuild a
// workspace and must not disturb a frame. Three rules, three frames, no error.
var rules = ["checker", "refined", "dither", "checker"];
for (var i = 0; i < rules.length; ++i) {
    world.ssrMarch(rules[i]);
    editor.frame(2);
}
assert(true, "the scene renders under every rule");

// ---- 5. it is NOT a World Mode row, deliberately ---------------------------
// A tier says how much a reflection may COST; this says how the march decides,
// which no tier has an opinion about. So a mode switch must leave it alone —
// and there is no panel row for it yet, by the lane's own rule: the pictures go
// to the owner first.
world.ssrMarch("refined");
world.mode({ mode: "low" });
assert(world.ssrMarch() === "refined", "a World Mode switch does not touch the march rule");
world.mode({ mode: "epic" });
assert(world.ssrMarch() === "refined", "...in either direction");
var rowIds = world.modeTable().rows.map(function (r) { return r.id; });
assert(rowIds.indexOf("ssrMarch") < 0,
       "and it is not a World Mode row (no tier column, no panel row yet)");

console.log("PASS ssr_march");
