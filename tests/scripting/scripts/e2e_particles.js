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
//
// IN A FRESH PROJECT, and neither half of that is tidiness — until 2026-09-04
// this phase measured nothing at all.
//
//  * The untextured smoke emitter built in phase D asked for a shader
//    HlmsUnlit cannot generate (alpha hashing reads inPs.uv0, and only a bound
//    texture makes Unlit forward a uv interpolant). The PSO failure threw
//    inside the render pass on EVERY frame, so the screenshot read back an
//    incomplete target: pastel scanlines with no gizmo, no fire and no plume in
//    them, which the warm-probe test below then passed on by accident.
//    (STABILITY_PROGRAM_SPEC Lane 6e; the engine fix is in OgreParticles.cpp
//    ensureParticleDatablock.)
//  * With the frame real, phase F's world is unusable for pixels twice over:
//    the 2048-quota HDR fire sits at the origin the camera frames onto, and —
//    DEFECT, reported 2026-09-04, not this suite's to fix — a save/close/open
//    round trip of even an empty default project comes back massively
//    over-exposed (a floor that read 65,65,65 before the save reads 255,255,255
//    after it). Nothing can be measured against a white frame.
//
// So the pixels are taken in a brand new world. Everything above has already
// asserted the document; this phase is about ONE emitter and what the renderer
// does with it.
assert(project.create("Particles Pixels " + Date.now()).length > 10,
       "a fresh world for the pixel phase");

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
// A FIXED STEP, and the reason this assertion used to be a coin flip. The
// renderer charges each frame the WALL time it took, and an offscreen frame
// takes about a millisecond — so a bare editor.frame(120) buys a TENTH OF A
// SECOND of plume on a fast machine and half a second on a busy one, and how
// much fire this test sees was a measure of how loaded the box was rather than
// of anything the engine did. It stopped being occasional and became reliable
// the moment the persistent shader cache removed the compilation those first
// frames used to hide behind. 1/60 s per frame is exactly two seconds of
// simulation, every time, on every machine.
// (editor.frame's dt argument has always existed for precisely this;
//  the assertion simply never used it.)

// A DIFFERENTIAL MEASUREMENT, against the same camera with the emitter off.
// The old test looked for "warm" absolute pixels in a fixed 5x5 window and had
// two problems the moment the frame became real: the window straddles the
// selection gizmo (whose green arrow and yellow hub read as warm all by
// themselves — they were most of the probes that ever passed), and a dense
// ADDITIVE plume seen from the distance focusSelection picks saturates to pure
// white, where r == b and nothing is "warm" at all. Comparing the same probes
// with and without the plume has neither problem: it survives any framing, it
// cannot be satisfied by a gizmo or a background, and a blank readback fails it
// instead of passing it.
// The window is the COLUMN focusSelection puts in shot: it rises from the
// origin at the centre of the frame and is about a fifth of the width.
var probes = [];
for (var yi = 0; yi < 5; yi++)
    for (var xi = 0; xi < 5; xi++)
        probes.push({ x: 0.40 + xi * 0.05, y: 0.06 + yi * 0.09 });

// The reference: emitter off, then longer than lifeLength so every particle
// already in flight has expired. Same camera, same everything else.
assert(node.setProperty(plume, "particlesPerSecond", 0), "emitter off for the reference frame");
editor.frame(180, 1 / 60);   // 3 s > lifeLength 2 s
var empty = editor.screenshot("particles_none.png", 640, 480, probes);
assert(empty.probes && empty.probes.length === probes.length, "reference screenshot with probes");

assert(node.setProperty(plume, "particlesPerSecond", 400), "emitter back on");
editor.frame(120, 1 / 60);   // the ENGINE simulates these; nothing here ticks anything

var shot = editor.screenshot("particles_verbs.png", 640, 480, probes);
assert(shot.probes && shot.probes.length === probes.length, "screenshot with probes");

var lit = 0, warm = 0;
for (var p = 0; p < shot.probes.length; p++) {
    var q = shot.probes[p], z = empty.probes[p];
    var dr = q.r - z.r, dg = q.g - z.g, db = q.b - z.b;
    if (dr > 20 && dg > 20 && db > 20) {
        lit++;
        // The ramp is (3.0, 0.6, 0.1) fading to (1.5, 0.2, 0.02): red must
        // gain at least as much as blue anywhere the plume is not clipped.
        if (dr >= db) warm++;
    }
}
console.log("plume probes brighter than the empty frame: " + lit + "/" + probes.length +
            " (warm-biased: " + warm + ")");
// 14 of 25 is what a correct frame gives, deterministically, on this rig (the
// column narrows with height, so the outer probes in the top rows fall off it);
// a blank or torn readback gives 0. 10 is the discriminating line with room to
// spare — if this ever drops to single digits the renderer stopped drawing
// particles, which is exactly the sentence this suite exists to be able to say.
assert(lit >= 10, "the engine is drawing a plume nobody ticked (" + lit + " of " + probes.length + " probes lit up)");
assert(warm === lit, "and it is the warm ramp doing it (" + warm + " of " + lit + ")");

// ---- phase H: the renderer had nothing to complain about ------------------
// STABILITY_PROGRAM_SPEC Lane 1 + Lane 6e, asserted through the registry
// instead of by grepping a log. The engine swallows failures by design and
// SceneMirror ignores nearly every return value, so "no visible problem" has
// never meant "no problem" — until 2026-09-04 this very suite produced 248
// shader-compile errors and 124 Ogre exceptions per run and passed anyway.
var pumped = app.engineErrors();
assert(pumped.drains > 100,
       "the engine error pump ran once per rendered frame (" + pumped.drains + " drains)");
assert(pumped.entries.length === 0,
       "and the renderer refused NOTHING in this whole run: " +
       JSON.stringify(pumped.entries));

console.log("e2e_particles: PASS");
