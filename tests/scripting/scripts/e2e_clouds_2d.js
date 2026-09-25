// scripting.e2e.clouds_2d — THE 2D CLOUD LAYER (CLOUDS-2D-1; SPECS/CLOUDS_ASSESSMENT.md
// option C0). End to end through the verb the World panel's Clouds rows write:
//
//   * the DOCUMENT: `world.clouds` reads and writes one `clouds` block, OFF by default,
//     clamped, refused key by key before anything is written, one undo step, and it
//     survives a save and a reopen;
//   * the SKY: coverage 0 leaves every sky pixel BYTE-IDENTICAL to a sky with no
//     layer (the field is exactly zero and the premultiplied blend is an identity);
//     a covered sky moves the sky pixels; a still frame is byte-deterministic;
//   * the ENVIRONMENT: the layer is inside the sky capture, so the capture's SH
//     (skyMean, band 0) moves when the coverage moves;
//   * the GROUND SHADOW: a sheet of transmittance ~0 (full coverage, density 4)
//     removes the sun's DIRECT term from a named ground pixel within 3/255 — measured
//     against the direct term of the same pixel with no clouds at all — half the
//     shadow strength removes half of it, on a material that receives shadow maps
//     and on one that does not, and a clear sheet removes nothing, byte for byte;
//   * the CLOCK: a 60-frame sun drag over the realistic sky ticks it 60 times;
//   * the SCROLL: the sheet moves by the wind times the scene clock (editor.frame's
//     fixed steps), and re-captures the environment on its cadence, not per frame;
//   * an IMAGE SKY ignores the block (a photograph carries its own clouds);
//   * the EXPORT bakes the layer into the equirect the web viewer takes.
//
// Every picture is the "plain" grade: linear radiance clipped to 8 bits, no chain.

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

// ---- registry ---------------------------------------------------------------
var world_ = api.verbs().filter(function (m) { return m.module === "world"; })[0];
assert(world_.verbs.map(function (v) { return v.name; }).indexOf("clouds") >= 0,
       "world.clouds is registered");

var guid = project.create("Clouds 2D " + Date.now());

// ---- the document: defaults, clamps, refusals, undo ------------------------
var c0 = world.clouds();
assert(c0.enabled === false, "the layer is OFF by default");
near(c0.coverage, 0.5, 1e-6, "coverage defaults to 0.5");
near(c0.density, 1, 1e-6, "density defaults to 1");
near(c0.speed, 10, 1e-6, "speed defaults to 10 m/s");
near(c0.direction, 0, 1e-6, "direction defaults to 0");
near(c0.altitude, 2000, 1e-6, "altitude defaults to 2000 m");
near(c0.shadow, 1, 1e-6, "shadow defaults to 1");
assert(c0.weatherMap === "", "no weather map by default");
assert(c0.drawsOver === true, "the default (realistic) sky takes the layer");
assert(c0.live && c0.live.drawn === false && c0.live.reason === "off",
       "the renderer draws no layer for a scene that has none: " + JSON.stringify(c0.live));
assert(world.get().clouds.enabled === false, "world.get() reports the block");

throws(function () { world.clouds({ coverage: 0.2, cumulus: 1 }); }, "an unknown key is refused");
throws(function () { world.clouds({ coverage: "lots" }); }, "a coverage that is not a number is refused");
throws(function () { world.clouds({ weatherMap: "not-an-asset" }); }, "a weatherMap that is not a stored texture is refused");
near(world.clouds().coverage, 0.5, 1e-6, "a refused call wrote nothing");

// ONE UNDO STEP PER CALL. A script run is one open macro, so editor.undo() cannot
// reach the run's own edits — undoState().pushes is the honest measure (e2e_ray_row).
function pushes() { return editor.undoState().pushes; }
var before = pushes();
var cl = world.clouds({ coverage: 7, density: -1, altitude: 90, direction: -90, speed: 400, shadow: 3 });
assert(cl.coverage === 1 && cl.density === 0 && cl.altitude === 500 && cl.speed === 100 &&
       cl.shadow === 1, "every dial is clamped into its band: " + JSON.stringify(cl));
near(cl.direction, 270, 1e-4, "a heading wraps into 0..360");
assert(pushes() === before + 1, "a six-dial call records exactly ONE undo step");
before = pushes();
world.clouds({ coverage: 1 });
assert(pushes() === before, "writing the value it already has records nothing");
try { world.clouds({ coverage: 0.1, nope: 1 }); } catch (e) {}
assert(pushes() === before, "a refused call records nothing either");
world.clouds({ coverage: 0.5, density: 1, altitude: 2000, direction: 0, speed: 10, shadow: 1 });

// ---- save / reopen ------------------------------------------------------------
world.clouds({ enabled: true, coverage: 0.35, density: 1.5, speed: 12, direction: 45,
               altitude: 3000, shadow: 0.8 });
assert(project.save(), "project.save");
project.close();
project.open(guid);
var re = world.clouds();
assert(re.enabled === true, "the layer survives a save and a reopen");
near(re.coverage, 0.35, 1e-6, "...coverage");
near(re.density, 1.5, 1e-6, "...density");
near(re.direction, 45, 1e-4, "...direction");
near(re.altitude, 3000, 1e-6, "...altitude");
near(re.shadow, 0.8, 1e-6, "...shadow");
world.clouds({ enabled: false, coverage: 0.5, density: 1, speed: 0, direction: 0,
               altitude: 2000, shadow: 1 });

// ---- the sky: coverage 0 is the sky itself, byte for byte -------------------
var sunInfo = world.sun();
node.transform(sunInfo.light, { rotation: { x: -40, y: 25, z: 0 } });
world.sunDisc({ visible: false });          // measure the SKY, not the disc on it
editor.setCamera({ position: { x: 0, y: 2, z: 0 }, lookAt: { x: 0, y: 7, z: -10 } });
var grid = [];
for (var gy = 0; gy < 6; gy++)
    for (var gx = 0; gx < 10; gx++) grid.push({ x: (gx + 0.5) / 10, y: (gy + 0.5) / 8 });
function skyShot(tag) {
    editor.frame(4, 1 / 60);
    return editor.screenshot("clouds_2d_" + tag + ".png", 320, 180, grid, "plain").probes;
}
function sameProbes(a, b) {
    var worst = 0;
    for (var i = 0; i < a.length; i++)
        worst = Math.max(worst, Math.abs(a[i].r - b[i].r), Math.abs(a[i].g - b[i].g),
                         Math.abs(a[i].b - b[i].b));
    return worst;
}
var clear = skyShot("none");
var st = world.clouds({ enabled: true, coverage: 0, speed: 0 });
var zero = skyShot("coverage0");
var live0 = world.clouds().live;
assert(live0.drawn === true, "the layer is drawn at coverage 0: " + JSON.stringify(live0));
assert(sameProbes(clear, zero) === 0,
       "coverage 0: every sky pixel is BYTE-IDENTICAL to the sky with no layer (" +
       sameProbes(clear, zero) + ")");
var meanClear = live0.skyMean;

world.clouds({ coverage: 0.6 });
var covered = skyShot("coverage60");
assert(sameProbes(clear, covered) > 20,
       "a covered sky moves the sky pixels (worst " + sameProbes(clear, covered) + "/255)");
var again = skyShot("coverage60_again");
assert(sameProbes(covered, again) === 0, "a still layer is byte-deterministic across frames");

// ---- the environment sees it --------------------------------------------------
world.clouds({ coverage: 0.95 });
editor.frame(4, 1 / 60);
var over = world.clouds().live;
var moved = Math.abs(over.skyMean[0] - meanClear[0]) + Math.abs(over.skyMean[1] - meanClear[1]) +
            Math.abs(over.skyMean[2] - meanClear[2]);
console.log("skyMean clear " + JSON.stringify(meanClear) + " overcast " + JSON.stringify(over.skyMean));
assert(moved > 0.05 * (meanClear[0] + meanClear[1] + meanClear[2]),
       "the environment capture's SH moves with the coverage (" + moved.toFixed(4) + ")");
assert(over.fieldBakes >= 2 && over.changeCaptures >= 2,
       "coverage edits re-bake the field and re-capture the sky: " + JSON.stringify(over));

// ---- the ground shadow --------------------------------------------------------
// A COLOUR sky, so nothing about the sky depends on the sun and a sun-intensity
// arm measures the direct term alone — and GI OFF: the voxel bounce is lit by the
// sun too, and the sheet does not shade the bounce (a stated limit of the 2D
// layer), so with GI on the sun arm would carry a term the shadow cannot remove.
world.gi({ mode: "off" });
world.sky("color", { color: "#8090a8" });
world.clouds({ enabled: false });
editor.setCamera({ position: { x: 0, y: 6, z: 6 }, lookAt: { x: 0, y: 0, z: 0 } });
var groundPts = [{ x: 0.5, y: 0.75 }, { x: 0.3, y: 0.85 }, { x: 0.7, y: 0.85 }];
function groundShot(tag) {
    editor.frame(6, 1 / 60);
    return editor.screenshot("clouds_2d_ground_" + tag + ".png", 320, 180, groundPts, "plain").probes;
}
// ONE ARM, run on three surfaces: the default ground (it receives the sun's shadow
// map), the same ground with receiveShadows OFF (the fix round's F2: upstream
// leaves the first-light shadow piece undefined for it, and the sheet's
// transmittance must reach it all the same), and a sun with no shadow map.
function shadowArm(tag) {
    world.clouds({ enabled: false });
    node.setProperty(sunInfo.light, "intensity", 2.0);
    var sunOn = groundShot(tag + "_sun");
    node.setProperty(sunInfo.light, "intensity", 0.0);
    var sunOff = groundShot(tag + "_nosun");
    node.setProperty(sunInfo.light, "intensity", 2.0);
    var direct = sunOn.map(function (p, i) { return { r: p.r - sunOff[i].r, g: p.g - sunOff[i].g, b: p.b - sunOff[i].b }; });
    assert(direct[0].r + direct[0].g + direct[0].b > 30,
           tag + ": the named ground pixel carries a measurable direct term: " + JSON.stringify(direct[0]));
    world.clouds({ enabled: true, coverage: 1, density: 4, speed: 0, shadow: 0 });
    var noShadow = groundShot(tag + "_shadow0");
    world.clouds({ shadow: 1 });
    var full = groundShot(tag + "_shadow1");
    world.clouds({ shadow: 0.5 });
    var half = groundShot(tag + "_shadow05");
    for (var k = 0; k < groundPts.length; k++) {
        var chans = ["r", "g", "b"];
        for (var ch = 0; ch < 3; ch++) {
            var c = chans[ch];
            near(noShadow[k][c] - full[k][c], direct[k][c], 2,
                 tag + " point " + k + "." + c + ": an opaque sheet removes the sun's direct term");
            near(noShadow[k][c] - half[k][c], direct[k][c] * 0.5, 2,
                 tag + " point " + k + "." + c + ": half the shadow strength removes half of it");
        }
    }
}
shadowArm("ground");
var groundNode = scene.find("Ground");
assert(material.set(groundNode, { receiveShadows: false }) === true,
       "the ground stops receiving shadow maps");
assert(material.get(groundNode).receiveShadows === false, "...and reads back so");
shadowArm("noreceive");
assert(material.set(groundNode, { receiveShadows: true }) === true, "the ground receives again");
// ...and a SUN THAT CASTS NO SHADOW MAP: it is drawn in the non-caster
// directional loop, reached through the fork's custom_ps_darkenNonCasterDirectional.
var sunShadowType = node.property(sunInfo.light, "shadowMapType");
node.setProperty(sunInfo.light, "shadowMapType", 0);
assert(node.property(sunInfo.light, "shadowMapType") === 0, "the sun casts no shadow map");
shadowArm("nocast");
node.setProperty(sunInfo.light, "shadowMapType", sunShadowType);
// ---- THE BOUNCE SEES THE SAME SHADOW (CLOUDS-2D-2) ----------------------------
// The voxel light injection darkens a directional light's DIRECT term by the same
// transmittance the pixel's takes, at the voxel's world centre. ONE bounce, so the
// light volume holds the injection alone (no bounce, no sky — the sky is never
// injected): its peak (the lit ground under the sun) must drop by the factor the ground pixel's
// direct term dropped by above — 0.5 at half strength under an opaque deck
// (0.5 + 0.5 e^-tau, tau ~ 60: exactly 0.5 to 1e-20) — within the 8-bit direct
// store's step at that peak (bar 0.02). And coverage 0 must leave the injection
// byte-identical to no layer (the light volume's digest).
world.gi({ mode: "vct", bounces: 1 });
world.clouds({ enabled: false });
// A lit box — the innermost cascade's surface (the ground is a coarser cascade's).
var litBox = scene.addPrimitive("cube", { position: { x: 0, y: 0.5, z: 0 } });
editor.frame(90, 1 / 60);
function voxels(tag) {
    editor.frame(30, 1 / 60);
    var v = world.giVoxelStats({ cascade: 0 });
    assert(v.available === true, tag + ": the voxel light volume reads back");
    console.log("voxels " + tag + " peak " + v.peak + " meanLit " + v.meanLit + " digest " + v.lightDigest);
    return v;
}
var vClear = voxels("nolayer");
assert(vClear.peak > 0.05, "the sun lights the voxels (" + vClear.peak + ")");
world.clouds({ enabled: true, coverage: 0, density: 4, speed: 0, shadow: 1 });
var vZero = voxels("coverage0");
assert(vZero.lightDigest === vClear.lightDigest,
       "coverage 0: the injection is byte-identical to no layer (" + vZero.lightDigest + ")");
world.clouds({ coverage: 1, shadow: 0.5 });
var vHalf = voxels("deck_shadow05");
near(vHalf.peak / vClear.peak, 0.5, 0.02,
     "an opaque deck at half strength halves the voxels' direct sun, as it halved the ground pixel's");
world.clouds({ shadow: 1 });
var vFull = voxels("deck_shadow1");
assert(vFull.peak <= 0.01 * vClear.peak,
       "an opaque deck at full strength removes the voxels' direct sun (" + vFull.peak + ")");
near(vHalf.meanLit / vClear.meanLit, 0.5, 0.02, "...and so is their mean over the lit voxels");
node.remove(litBox);
world.gi({ mode: "off" });

world.clouds({ coverage: 0, shadow: 1 });
var clearShadow = groundShot("clear_shadow1");
world.clouds({ shadow: 0 });
var clearNoShadow = groundShot("clear_shadow0");
assert(sameProbes(clearShadow, clearNoShadow) === 0,
       "a clear sheet's shadow changes no ground pixel, byte for byte");

// ---- the scroll and the capture cadence --------------------------------------
world.clouds({ coverage: 0.5, density: 1, shadow: 1, speed: 20, direction: 0 });
editor.frame(1, 1 / 60);   // the renderer hears of a write on the next frame
var s0 = world.clouds().live;
assert(s0.capturePeriodFrames > 0, "a scrolling layer has a capture period: " + s0.capturePeriodFrames);
var cap0 = s0.scrollCaptures, scroll0 = s0.scroll[0];
editor.frame(s0.capturePeriodFrames + 2, 1 / 60);
var s1 = world.clouds().live;
var travelled = (scroll0 - s1.scroll[0] + 16000) % 16000;
near(travelled, 20 * (s0.capturePeriodFrames + 2) / 60, 0.05,
     "the sheet moved by the wind times the scene clock (metres)");
assert(s1.scrollCaptures === cap0 + 1,
       "one scroll capture per period, not one per frame (" + cap0 + " -> " + s1.scrollCaptures + ")");
world.clouds({ speed: 0 });
editor.frame(1, 1 / 60);
var still = world.clouds().live;
assert(still.capturePeriodFrames === 0, "a still layer schedules no scroll captures");

// ---- the clock ticks ONCE per drawn frame, whatever else moves (F1) --------------
// A dragged sun over the REALISTIC sky changes the sky and the sheet's look on every
// frame; neither may advance the sheet's clock or its capture cadence.
world.sky("realistic", {});
world.clouds({ coverage: 0.5, density: 1, shadow: 1, speed: 0 });
editor.frame(2, 1 / 60);
world.clouds({ speed: 20, direction: 0 });   // a wind that starts restarts the period
editor.frame(1, 1 / 60);
var d0 = world.clouds().live;
for (var f = 0; f < 60; f++) {
    node.transform(sunInfo.light, { rotation: { x: -30 - f * 0.25, y: 25 + f * 0.5, z: 0 } });
    editor.frame(1, 1 / 60);
}
var d1 = world.clouds().live;
assert(d1.clockTicks - d0.clockTicks === 60,
       "60 drawn frames of a sun drag tick the clock 60 times (" + (d1.clockTicks - d0.clockTicks) + ")");
near((d0.scroll[0] - d1.scroll[0] + 16000) % 16000, 60 * 20 / 60, 0.02,
     "...and move the sheet 60 frames' worth, not 180 (metres)");
assert(d1.fieldBakes === d0.fieldBakes, "a sun drag re-bakes no field (" + d0.fieldBakes + " -> " + d1.fieldBakes + ")");
assert(d1.scrollCaptures === d0.scrollCaptures, "a sun drag adds no scroll capture");
world.clouds({ speed: 0 });

// ---- an image sky ignores the block --------------------------------------------
world.skyPreset("cove");
world.clouds({ enabled: false });
editor.setCamera({ position: { x: 0, y: 2, z: 0 }, lookAt: { x: 0, y: 7, z: -10 } });
var cubeSky = skyShot("cube_none");
var ci = world.clouds({ enabled: true, coverage: 0.8 });
editor.frame(1, 1 / 60);
assert(ci.drawsOver === false, "a cubemap sky does not take the layer");
var cubeCovered = skyShot("cube_covered");
var ciLive = world.clouds().live;
assert(ciLive.drawn === false && ciLive.reason === "imageSky",
       "the renderer draws no layer over an image sky: " + JSON.stringify(ciLive));
assert(sameProbes(cubeSky, cubeCovered) === 0, "the image sky's pixels are untouched by the block");

// ---- the export bakes the layer ---------------------------------------------------
world.sky("realistic", {});
world.clouds({ enabled: true, coverage: 0.5 });
editor.frame(4, 1 / 60);
var ex = project.exportWeb();
assert(ex && ex.dir.length > 0, "project.exportWeb with a cloud layer");
assert(ex.warnings.join(" ").indexOf("cloud layer was not exported") < 0,
       "with a renderer the layer is baked, not dropped: " + JSON.stringify(ex.warnings));
console.log("clouds_2d: PASS");
