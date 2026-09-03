// scripting.e2e.planar — PLANAR_REFLECTIONS_SPEC.md §8: the world.* and node.*
// verbs, the World Mode row behind them, and a full save/close/open round trip.
// Runs inside the real app (--script, engine viewport up, scratch HOME).
//
// Phase A: the reflector flag on a plane, refused on a sphere.
// Phase B: budget/resolution/shadows, "auto" and the achieved actor count.
// Phase C: the World Mode tiers drive the budget, an explicit value pins it.
// Phase D: everything survives save -> close -> open.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

var guid = project.create("Planar Reflections " + Date.now());
assert(guid.length > 10, "project.create -> " + guid);

// ---- phase A: the per-node flag ----------------------------------------
// A plane primitive is the canonical reflector: flat by construction.
var floor = scene.addPrimitive("plane", { position: { x: 0, y: 0, z: 0 },
                                          scale: { x: 8, y: 1, z: 8 } });
assert(floor.length > 10, "plane added");
editor.frame(2);

assert(node.planarReflector(floor) === false, "a new plane is not a reflector");
assert(node.setPlanarReflector(floor, true), "the plane accepts the reflector flag");
assert(node.planarReflector(floor) === true, "planarReflector reads back true");

// A sphere cannot be a mirror: the plane would be derived from an arbitrary
// axis and the 20-degree matching rule would make it look broken. The verb
// must REFUSE it, and must not leave the flag set.
var ball = scene.addPrimitive("sphere", { position: { x: 3, y: 2, z: 0 } });
assert(ball.length > 10, "sphere added");
editor.frame(2);
var refused = false;
try { node.setPlanarReflector(ball, true); } catch (e) { refused = true; }
assert(refused, "a sphere is refused as a reflector");
assert(node.planarReflector(ball) === false, "the refused sphere is not a reflector");

// ---- phase B: the scene-level settings ---------------------------------
var pr = world.setPlanarReflections({ budget: 2, resolution: 1024, shadows: false });
assert(pr.budget === 2, "budget 2 applied");
assert(pr.resolution === 1024, "resolution 1024 applied");
assert(pr.shadows === false, "shadows off applied");
assert(pr.enabled === true, "enabled reads true at budget 2");

editor.frame(3);
var live = world.planarReflections();
assert(live.budget === 2, "budget reads back 2");
// The achieved count: one reflector exists and it is in frame, so exactly one
// plane rendered. This is the verb's whole point — it reports what the
// renderer DID, not what was asked for.
assert(live.activeActors === 1, "one plane actually rendered (got " + live.activeActors + ")");

// "auto" on resolution and shadows follows the budget: at 2 planes that is
// 1024 and shadows on (the Epic shape).
pr = world.setPlanarReflections({ resolution: "auto", shadows: "auto" });
assert(pr.resolution === 1024, "auto resolution follows a budget of 2 -> 1024");
assert(pr.shadows === true, "auto shadows follow a budget of 2 -> on");

// Budget 0 switches the whole feature off, and nothing renders.
pr = world.setPlanarReflections({ budget: 0 });
assert(pr.enabled === false, "budget 0 disables reflections");
editor.frame(3);
assert(world.planarReflections().activeActors === 0, "no planes render at budget 0");

var badBudget = false;
try { world.setPlanarReflections({ budget: 99 }); } catch (e) { badBudget = true; }
assert(badBudget, "an out-of-range budget is refused");

// ---- phase C: the World Mode row ---------------------------------------
// The budget is a World Mode row, so a tier writes it through, and an explicit
// value pins it against later tier changes — the same contract every other
// quality row has. Phase B set it explicitly, so it is PINNED right now and a
// tier would (correctly) not touch it: drop the pin first.
assert(world.settings()["planarBudget"].source === "override",
       "phase B's explicit budget left the row pinned");
world.clearOverride({ id: "planarBudget" });
world.mode({ mode: "low" });
assert(world.planarReflections().budget === 0, "Low mode: no planar reflections");
world.mode({ mode: "high" });
assert(world.planarReflections().budget === 1, "High mode: one plane");
world.mode({ mode: "epic" });
assert(world.planarReflections().budget === 2, "Epic mode: two planes");

var s = world.settings()["planarBudget"];
assert(s.available === true, "the planarBudget row is available");
assert(s.value === 2, "the row resolves to 2 under Epic");
assert(s.source === "mode", "the row's value comes from the mode");

world.setPlanarReflections({ budget: 4 });
assert(world.settings()["planarBudget"].source === "override", "an explicit budget pins the row");
world.mode({ mode: "low" });
assert(world.planarReflections().budget === 4, "the pin survives a mode switch to Low");
world.clearOverride({ id: "planarBudget" });
assert(world.planarReflections().budget === 0, "clearing the pin puts Low's value back");

// ---- phase D: save -> close -> open -------------------------------------
world.mode({ mode: "epic" });
world.setPlanarReflections({ budget: 3, resolution: 512, shadows: true });
assert(node.planarReflector(floor) === true, "the floor is still a reflector before saving");

assert(project.save() === true, "project.save");
assert(project.close() === true, "project.close");
assert(project.open(guid) === true, "project.open");
editor.frame(2);

var after = world.planarReflections();
assert(after.budget === 3, "budget survived the round trip (got " + after.budget + ")");
assert(after.resolution === 512, "resolution survived the round trip");
assert(after.shadows === true, "reflection shadows survived the round trip");

// The per-node flag is written only when true, so this is also the assertion
// that the writer emitted the key at all.
var nodes = scene.nodes();
var reopenedFloor = null;
for (var i = 0; i < nodes.length; ++i)
    if (nodes[i].name === "Plane") reopenedFloor = nodes[i].id;
assert(reopenedFloor !== null, "the plane came back");
assert(node.planarReflector(reopenedFloor) === true, "the reflector flag survived the round trip");

editor.frame(3);
assert(world.planarReflections().activeActors === 1, "the reopened plane renders its reflection");

console.log("scripting.e2e.planar: all checks passed");
