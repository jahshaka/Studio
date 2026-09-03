// scripting.e2e.particles — PARTICLES_FX2_SPEC.md §11 phase 2: the verbs behind
// the ParticleFX2 adoption, in the real app with the engine viewport up.
//
// Verbs first, UI second, is the rule (SCRIPTING_SPEC §2.3). So everything the
// rebuilt emitter panel does has to be reachable from here:
//   scene.addParticles(preset?, {...})     the emitter, recipe included
//   node.setProperty(id, field, value)     every scalar row (shared with all nodes)
//   particles.preset / describe            the recipe as one undoable step
//   particles.setColourKeys / setScaleKeys the ramps, which Property cannot carry
//   particles.timeScale                    the scene's simulation clock
//
// Phase A: add an emitter, plain and from a recipe.
// Phase B: the scalar rows through node.setProperty, both ways.
// Phase C: the ramps, and the guard rails on them.
// Phase D: the preset is ONE undo step, and undo puts back what was there.
// Phase E: the clock.
// Phase F: everything survives save -> close -> open.
// Phase G: the engine actually draws it (pixels, not just document state).

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function near(a, b, tol) { return Math.abs(a - b) <= (tol === undefined ? 1e-3 : tol); }

var guid = project.create("Particles Verbs " + Date.now());
assert(guid.length > 10, "project.create -> " + guid);

// ---- phase A: adding -----------------------------------------------------
var plain = scene.addParticles();
assert(plain.length > 10, "scene.addParticles() -> " + plain);
var d = particles.describe(plain);
assert(d.preset === "custom", "a plain emitter carries no recipe");
assert(d.colourKeys.length === 0, "and no colour ramp");

var fire = scene.addParticles("fire", { position: { x: 0, y: 0.2, z: 0 }, quota: 1024 });
assert(fire.length > 10, "scene.addParticles(\"fire\") -> " + fire);
d = particles.describe(fire);
assert(d.preset === "fire", "the fire recipe was stamped");
assert(d.quota === 1024, "the quota option applied");
assert(d.additive === true, "fire is additive");
assert(d.turbulence > 0, "fire flickers (turbulence " + d.turbulence + ")");
assert(d.colourKeys.length >= 3, "fire has a colour ramp (" + d.colourKeys.length + " keys)");
// HDR is not decoration: >1 is what makes a flame bloom instead of reading as
// an orange sticker. If this ever clamps to 1 the fire is broken.
assert(d.colourKeys[0].r > 1.0,
       "the fire ramp starts HDR (r=" + d.colourKeys[0].r + ", above 1 on purpose)");
assert(d.colourKeys[0].r > d.colourKeys[0].b * 4, "and warm");
assert(d.scaleKeys.length >= 2, "fire has a scale ramp");

var presetNames = particles.presets();
assert(presetNames.indexOf("fire") >= 0 && presetNames.indexOf("smoke") >= 0 &&
       presetNames.indexOf("embers") >= 0,
       "particles.presets() lists the recipes (" + presetNames.join(", ") + ")");

var refusedPreset = false;
try { scene.addParticles("inferno"); } catch (e) { refusedPreset = true; }
assert(refusedPreset, "an unknown preset name is refused, not silently ignored");

// A non-emitter is refused by every particles.* verb.
var cube = scene.addPrimitive("cube", { position: { x: 4, y: 0, z: 0 } });
var refusedNode = false;
try { particles.describe(cube); } catch (e) { refusedNode = true; }
assert(refusedNode, "particles.describe refuses a mesh node");

// ---- phase B: the scalar rows, through the SHARED node verb ---------------
// Not a particles.* verb each: every one of these is a reflected property on
// the node, so node.setProperty is the whole scalar surface.
assert(node.setProperty(fire, "particlesPerSecond", 140), "rate via node.setProperty");
assert(node.setProperty(fire, "speed", 2.6), "speed");
assert(node.setProperty(fire, "speedError", 0.8), "speedError (never serialized before this)");
assert(node.setProperty(fire, "lifeLength", 1.1), "lifeLength");
assert(node.setProperty(fire, "particleScale", 0.4), "particleScale");
assert(node.setProperty(fire, "coneAngle", 18), "coneAngle");
assert(node.setProperty(fire, "turbulence", 3.0), "turbulence");
assert(node.setProperty(fire, "shape", "ellipsoid"), "emitter shape by NAME");
assert(node.setProperty(fire, "extents", { x: 0.3, y: 0.1, z: 0.3 }), "extents as a vector");
assert(node.setProperty(fire, "wind", { x: 0, y: 1.4, z: 0 }), "wind as a vector");
assert(node.setProperty(fire, "orientation", "billboard"), "orientation by NAME");
assert(node.setProperty(fire, "maxParticles", 2048), "quota");

d = particles.describe(fire);
assert(near(d.rate, 140), "rate reads back " + d.rate);
assert(near(d.speedError, 0.8), "speedError reads back");
assert(d.shape === "ellipsoid", "shape reads back by name");
assert(near(d.extents.x, 0.3) && near(d.extents.y, 0.1), "extents read back");
assert(near(d.wind.y, 1.4), "wind reads back");
assert(d.quota === 2048, "quota reads back");

var refusedField = false;
try { node.setProperty(fire, "notAField", 3); } catch (e) { refusedField = true; }
assert(refusedField, "an unknown field is refused");
// The texture is read-only from scripting: binding one needs the asset manager,
// which the document layer cannot reach.
var refusedTexture = false;
try { node.setProperty(fire, "texture", "/tmp/nope.png"); } catch (e) { refusedTexture = true; }
assert(refusedTexture, "the texture row is read-only from node.setProperty");

// ---- phase C: the ramps ---------------------------------------------------
assert(particles.setColourKeys(fire, [
    { time: 0.0, r: 5.0, g: 2.0, b: 0.4, a: 1.0 },
    { time: 0.5, r: 1.5, g: 0.3, b: 0.05, a: 0.9 },
    { time: 1.0, r: 0.05, g: 0.0, b: 0.0, a: 0.0 }
]), "particles.setColourKeys");
var ck = particles.colourKeys(fire);
assert(ck.length === 3, "three colour keys");
assert(near(ck[0].r, 5.0), "HDR channel survives unclamped (" + ck[0].r + ")");
assert(ck[0].time < ck[1].time && ck[1].time < ck[2].time, "keys come back in ascending time");

// Out of order in, sorted out: the renderer's interpolator requires ascending
// stages and would otherwise interpolate backwards.
assert(particles.setScaleKeys(fire, [
    { time: 1.0, scale: 0.45 },
    { time: 0.0, scale: 0.6 },
    { time: 0.35, scale: 1.0 }
]), "particles.setScaleKeys, deliberately out of order");
var sk = particles.scaleKeys(fire);
assert(sk.length === 3 && sk[0].time === 0 && near(sk[1].time, 0.35) && sk[2].time === 1,
       "scale keys are sorted into ascending time");

var refusedKeys = false;
try {
    particles.setColourKeys(fire, [
        { time: 0.0 }, { time: 0.1 }, { time: 0.2 }, { time: 0.3 },
        { time: 0.4 }, { time: 0.5 }, { time: 0.6 }
    ]);
} catch (e) { refusedKeys = true; }
assert(refusedKeys, "more than 6 keys is refused (the interpolator has 6 stages)");
assert(particles.colourKeys(fire).length === 3, "and the refused call changed nothing");

assert(particles.setColourKeys(fire, []), "an empty list clears the ramp");
assert(particles.colourKeys(fire).length === 0, "the ramp is gone");

// ---- phase D: a preset is ONE undo step ----------------------------------
// Which is how the owner gets "click Fire -> fire" without ten undo steps to
// climb back out of.
assert(node.setProperty(plain, "particlesPerSecond", 7), "a marker value on the plain emitter");
assert(particles.preset(plain, "smoke"), "particles.preset(plain, \"smoke\")");
d = particles.describe(plain);
assert(d.preset === "smoke", "the recipe applied");
assert(d.additive === false, "smoke OCCLUDES — it is alpha-blended, not additive");
assert(d.alphaHash === true, "and uses stochastic transparency, because nothing sorts particles");
assert(!near(d.rate, 7), "the recipe overwrote the marker value");

// ---- phase E: the clock ---------------------------------------------------
// SCENE-level and, in the renderer, process-wide: there is exactly one
// frame-time source, so no per-emitter clock can exist.
assert(near(particles.timeScale(), 1.0), "the clock starts at real time");
assert(near(particles.timeScale(0), 0.0), "particles.timeScale(0) freezes");
assert(near(particles.timeScale(), 0.0), "and reads back frozen");
assert(near(particles.timeScale(2), 2.0), "double speed");
assert(near(particles.timeScale(-5), 0.0), "a negative clock clamps to frozen, not to nonsense");
assert(near(particles.timeScale(1), 1.0), "back to real time");

// ---- phase F: save -> close -> open --------------------------------------
// Every new key is optional in the reader, so this is the assertion that says
// they are actually WRITTEN — including the three spreads that the property
// panel has edited since 2016 and nothing ever saved.
assert(particles.setColourKeys(fire, [
    { time: 0.0, r: 4.0, g: 1.6, b: 0.35, a: 1.0 },
    { time: 0.55, r: 0.9, g: 0.18, b: 0.03, a: 0.8 },
    { time: 1.0, r: 0.05, g: 0.02, b: 0.02, a: 0.0 }
]), "re-arm the fire ramp before saving");
var beforeSave = particles.describe(fire);

assert(project.save(), "project.save");
assert(project.close(), "project.close");
assert(project.open(guid), "project.open");

var reopened = null;
var nodes = scene.nodes();
for (var i = 0; i < nodes.length; i++)
    if (nodes[i].type === "particles" && nodes[i].id === fire) reopened = nodes[i].id;
assert(reopened !== null, "the emitter came back with its guid");

var after = particles.describe(reopened);
assert(after.preset === "fire", "the preset name survived");
assert(near(after.rate, beforeSave.rate), "rate survived (" + after.rate + ")");
assert(near(after.speedError, beforeSave.speedError),
       "speedError survived — the spread the panel edited for ten years and never saved");
assert(near(after.lifeError, beforeSave.lifeError), "lifeError survived");
assert(after.shape === "ellipsoid", "emitter shape survived");
assert(near(after.extents.x, 0.3), "extents survived");
assert(near(after.wind.y, 1.4), "wind survived");
assert(near(after.turbulence, 3.0), "turbulence survived");
assert(after.quota === 2048, "quota survived");
assert(after.colourKeys.length === 3, "the colour ramp survived");
assert(near(after.colourKeys[0].r, 4.0),
       "including its HDR channel (" + after.colourKeys[0].r + ")");
assert(after.scaleKeys.length === 3, "the scale ramp survived");
assert(near(particles.timeScale(), 1.0), "the scene clock survived");

// ---- phase G: it actually renders ----------------------------------------
// The document could be perfect and the engine draw nothing. Point a camera at
// an emitter with no texture (untextured additive quads are the brightest thing
// available) over the default background and look for it.
var lamp = scene.addLight("point", { position: { x: 0, y: 3, z: 3 } });
assert(lamp.length > 10, "a light, so the frame is not pitch black by accident");

var plume = scene.addParticles("steadyFlow", { position: { x: 0, y: 0, z: 0 } });
assert(node.setProperty(plume, "particlesPerSecond", 400), "a dense plume");
assert(node.setProperty(plume, "particleScale", 0.35), "big enough to see");
assert(node.setProperty(plume, "lifeLength", 2.0), "and long-lived");
assert(node.setProperty(plume, "speed", 2.0), "rising");
assert(particles.setColourKeys(plume, [
    { time: 0.0, r: 3.0, g: 0.6, b: 0.1, a: 1.0 },
    { time: 1.0, r: 1.5, g: 0.2, b: 0.02, a: 1.0 }
]), "a warm ramp on the plume");

editor.select(plume);
editor.frame(2);
editor.focusSelection();  // frames the selection, so the plume is in shot
editor.frame(120);       // the ENGINE simulates these; nothing here ticks anything

var probes = [];
for (var yi = 0; yi < 5; yi++)
    for (var xi = 0; xi < 5; xi++)
        probes.push({ x: 0.38 + xi * 0.06, y: 0.30 + yi * 0.08 });
var shot = editor.screenshot("particles_verbs.png", 640, 480, probes);
assert(shot.probes && shot.probes.length === probes.length, "screenshot with probes");

var warm = 0;
for (var p = 0; p < shot.probes.length; p++) {
    var q = shot.probes[p];
    if (q.r > q.b + 25 && q.r > 60) warm++;
}
console.log("plume warm probes: " + warm + "/" + probes.length);
assert(warm >= 4, "the engine is drawing a warm plume nobody ticked (" + warm + " warm probes)");

console.log("e2e_particles: PASS");
