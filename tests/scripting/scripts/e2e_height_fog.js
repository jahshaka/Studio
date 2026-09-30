// scripting.e2e.height_fog — THE EXPONENTIAL HEIGHT FOG (SKY-DEFAULTS-1;
// iris::HeightFog, engine HeightFogDesc). End to end through the verb the World
// panel's Height Fog rows write:
//
//   * the DOCUMENT: `world.heightFog` reads and writes one `heightFog` block —
//     OFF in a scene built by hand, ON in the Basic template at Unreal's
//     density 0.02 / falloff 0.2 from 100 m — clamped, refused key by key before
//     anything is written, one undo step, and it survives a save and a reopen;
//   * the UNITS: the answer's per-metre pair is Unreal's dials / 10;
//   * THE START DISTANCE: a surface nearer than it is byte-identical with the fog
//     on and off, and the same surface IS fogged once the start distance is
//     brought in front of it;
//   * the MEDIUM: more density fogs a far surface more; raising the base height
//     above the eye fogs the sky above the horizon more;
//   * the World fog is untouched by any of it.
//
// Every picture is the "plain" grade (linear radiance), read as float.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function throws(fn, msg) {
    var threw = false;
    try { fn(); } catch (e) { threw = true; console.log("ok: " + msg + " (" + e.message + ")"); }
    if (!threw) throw new Error("assert failed (no error): " + msg);
}
function near(a, b, tol, msg) { assert(Math.abs(a - b) <= tol, msg + " (" + a + " vs " + b + ")"); }
function J(x) { return JSON.stringify(x); }
function dist(a, b) { return Math.sqrt((a.r - b.r) * (a.r - b.r) + (a.g - b.g) * (a.g - b.g) + (a.b - b.b) * (a.b - b.b)); }
function shot(tag, probes) {
    editor.frame(6, 1 / 60);
    return editor.screenshot("height_fog_" + tag + ".png", 480, 270, probes, "plain", { radiance: true })
        .probes.map(function (p) { return p.radiance; });
}

// ---- registry -----------------------------------------------------------------
var world_ = api.verbs().filter(function (m) { return m.module === "world"; })[0];
assert(world_.verbs.map(function (v) { return v.name; }).indexOf("heightFog") >= 0,
       "world.heightFog is registered");

// ---- the Basic template's block ---------------------------------------------
var guid = project.create("Height Fog " + Date.now());
var h0 = world.heightFog();
console.log("basic: " + J(h0));
assert(h0.enabled === true, "the Basic template switches it ON");
near(h0.density, 0.02, 1e-6, "density is Unreal's 0.02");
near(h0.heightFalloff, 0.2, 1e-6, "height falloff is Unreal's 0.2");
near(h0.baseHeight, 0, 1e-6, "base height 0");
near(h0.startDistance, 100, 1e-4, "start distance 100 m (past the Basic floor)");
near(h0.densityPerMetre, 0.002, 1e-7, "Unreal's dial / 10 is the density per metre");
near(h0.heightFalloffPerMetre, 0.02, 1e-7, "...and the falloff per metre");
assert(world.get().heightFog.enabled === true, "world.get() reports the block");
assert(world.get().fog.enabled === false, "the World fog is a separate row, and OFF");

// ---- refusals, clamps, undo --------------------------------------------------
throws(function () { world.heightFog({ density: 0.1, colour: "#fff" }); }, "an unknown key is refused");
throws(function () { world.heightFog({ density: "thick" }); }, "a density that is not a number is refused");
near(world.heightFog().density, 0.02, 1e-6, "a refused call wrote nothing");
function pushes() { return editor.undoState().pushes; }
var before = pushes();
var cl = world.heightFog({ density: 50, heightFalloff: 0, startDistance: -5 });
assert(cl.density === 10 && Math.abs(cl.heightFalloff - 0.001) < 1e-7 && cl.startDistance === 0,
       "every dial is clamped into its band: " + J(cl));
assert(pushes() === before + 1, "a three-dial call records exactly ONE undo step");
before = pushes();
world.heightFog({ density: 10 });
assert(pushes() === before, "writing the value it already has records nothing");
world.heightFog({ density: 0.02, heightFalloff: 0.2, startDistance: 100 });

// ---- save / reopen ------------------------------------------------------------
world.heightFog({ density: 0.05, heightFalloff: 0.3, baseHeight: 4, startDistance: 250 });
assert(project.save(), "project.save");
project.close();
project.open(guid);
var re = world.heightFog();
assert(re.enabled === true, "the block survives a save and a reopen");
near(re.density, 0.05, 1e-6, "...density");
near(re.heightFalloff, 0.3, 1e-6, "...falloff");
near(re.baseHeight, 4, 1e-6, "...base height");
near(re.startDistance, 250, 1e-4, "...start distance");
world.heightFog({ density: 0.02, heightFalloff: 0.2, baseHeight: 0, startDistance: 100 });

// ---- THE START DISTANCE ------------------------------------------------------
// A cube 40 m ahead of a level eye: nearer than the default 100 m.
var cube = scene.addPrimitive("cube", { position: { x: 0, y: 1, z: -40 }, scale: { x: 3, y: 3, z: 3 } });
editor.selectNone();
editor.setCamera({ position: { x: 0, y: 2, z: 0 }, lookAt: { x: 0, y: 1, z: -40 } });
var fogColour = world.heightFog().live.colour;
var F = { r: fogColour[0], g: fogColour[1], b: fogColour[2] };
var onNear = shot("near_on", [{ x: 0.5, y: 0.5 }])[0];
world.heightFog({ enabled: false });
var offNear = shot("near_off", [{ x: 0.5, y: 0.5 }])[0];
assert(dist(onNear, offNear) === 0, "a surface 40 m away, nearer than the start distance, is IDENTICAL with the fog on and off");
world.heightFog({ enabled: true, startDistance: 0, density: 0.5 });
var fogged = shot("near_start0", [{ x: 0.5, y: 0.5 }])[0];
assert(dist(fogged, F) < 0.9 * dist(offNear, F),
       "...and the same surface IS fogged once the start distance is 0 (moved " +
       (dist(offNear, F) - dist(fogged, F)).toFixed(4) + " towards the fog)");
world.heightFog({ density: 2 });
var denser = shot("near_denser", [{ x: 0.5, y: 0.5 }])[0];
assert(dist(denser, F) < dist(fogged, F), "more density fogs it more");
node.remove(cube);

// ---- THE BASE HEIGHT: a layer lifted over the eye fogs the sky above --------
world.heightFog({ density: 0.02, startDistance: 100 });
editor.setCamera({ position: { x: 0, y: 2, z: 0 }, lookAt: { x: 0, y: 2 + Math.tan(10 * Math.PI / 180), z: -1 } });
var low = shot("sky10_base0", [{ x: 0.5, y: 0.5 }])[0];
world.heightFog({ baseHeight: 300 });
var high = shot("sky10_base300", [{ x: 0.5, y: 0.5 }])[0];
assert(dist(high, F) < dist(low, F), "a layer lifted to 300 m fogs the sky 10 degrees up more (" +
       dist(high, F).toFixed(4) + " vs " + dist(low, F).toFixed(4) + " from the fog colour)");
world.heightFog({ baseHeight: 0 });

// ---- off is off ---------------------------------------------------------------
world.heightFog({ enabled: false });
editor.frame(4, 1 / 60);
var liveOff = world.heightFog().live;
assert(liveOff && liveOff.on === false, "switched off, the renderer draws none (" + J(liveOff) + ")");

console.log("PASS scripting.e2e.height_fog");
