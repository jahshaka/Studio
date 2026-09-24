// scripting.e2e.sky_swap_transient — THE ENVIRONMENT LANDS AS ONE SET
// (PHOTON-SKY-TRANSIENT-1).
//
// THE DEFECT IT GUARDS. A sky change used to replace the reflection cube bound
// to every datablock INSIDE the capture's frame with a newborn cube whose GGX
// convolution was queued for the top of the NEXT frame, so every draw of the
// change frame sampled a cube nothing had written (recycled VRAM: NaN and 3e4
// half-floats in mip 0). A ground pixel read 111/28/118 (or 31/255/32 — it is
// whatever the allocation held) on the first frame after a sky power change and
// 25/29/32 one frame later: a one-frame flash on every sky or sun edit, and a
// wrong screenshot when one was taken on that frame. The SH also reached the
// pixel one host push after the cube. The engine now keeps the previous set
// (cube + SH + gain) bound until the capture, the convolution and the SH of the
// next one have all landed and swaps them in one step (OgreSky.cpp,
// landEnvironmentIfComplete); a lone change lands inside its own frame.
//
// THE ASSERTION: for each kind of change that re-captures the sky — the sky's
// power, the sun's direction (the realistic sky follows the sun), the sky type
// both ways (realistic <-> gradient) and the cloud layer on and off — the FIRST
// picture drawn after the change equals the picture one frame later within
// 2/255 on every probe and channel. The probes are the repro's ground points and
// a glossy metal sphere (the reflection cube's own reader).
//
// WHY GI IS OFF FOR THE FIRST SIX. editor.screenshot settles GI before it
// photographs (a shot is the picture at rest); with GI off nothing owes a
// settle, so its frames ARE the first after the change — the sky/IBL swap in
// isolation, where the defect lived. The last two arms turn the Photon chain on
// (field off: the repro; field on: the shipped tier) and photograph through
// camera.screenshot, which never settles — frame 0 by construction.
//
// Engine UP (the assertion is a picture). Fresh home: the run starts from an
// empty data root, so nothing a previous run cached (a shader, a probe face)
// stands between the change and its first frame.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

var guid = project.create("Sky swap transient " + Date.now());
assert(guid.length > 10, "project.create -> " + guid);
world.gi({ mode: "off" });
var sun = world.sun();
node.transform(sun.light, { rotation: { x: -50, y: 30, z: 0 } });
editor.setCamera({ position: { x: 0, y: 40, z: 40 }, lookAt: { x: 0, y: 0, z: 0 } });
var ball = scene.addPrimitive("sphere", { position: { x: 10, y: 3, z: 6 },
                                          scale: { x: 6, y: 6, z: 6 } });
material.set(ball, { baseColor: "#e0e0e0", metallic: 1.0, roughness: 0.15 });
editor.selectNone();
world.sky("realistic", { power: 3.0 });
editor.frame(4, 1 / 60);

// ground (the repro's two points), then two points on the metal sphere
var probes = [{ x: 0.5, y: 0.5 }, { x: 0.75, y: 0.75 },
              { x: 0.60, y: 0.52 }, { x: 0.66, y: 0.62 }];
function shot(tag) {
    return editor.screenshot("sky_swap_" + tag + ".png", 480, 270, probes, "plain").probes;
}
function worst(a, b) {
    var w = 0;
    for (var i = 0; i < a.length; i++)
        w = Math.max(w, Math.abs(a[i].r - b[i].r), Math.abs(a[i].g - b[i].g),
                     Math.abs(a[i].b - b[i].b));
    return w;
}
function rgb(p) {
    var s = [];
    for (var i = 0; i < p.length; i++) s.push(p[i].r + "/" + p[i].g + "/" + p[i].b);
    return s.join(" ");
}
// One change: the picture before it, the FIRST picture after it, and the one a
// frame later. Returns how far the change moved the picture (so an arm that
// moves nothing cannot pass by measuring nothing).
function arm(label, change) {
    editor.frame(4, 1 / 60);
    var before = shot(label + "_before");
    change();
    var f0 = shot(label + "_f0");
    editor.frame(1, 1 / 60);
    var f1 = shot(label + "_f1");
    console.log("SKYSWAP " + label + " before " + rgb(before) + " | f0 " + rgb(f0) +
                " | f1 " + rgb(f1));
    // Every arm runs and reports; the verdict is at the end, so one run shows
    // every kind of change that flashes.
    var d = worst(f0, f1);
    var line = label + ": the first frame after the change equals the next within 2/255 (worst " +
               d + "; f0 " + rgb(f0) + " vs f1 " + rgb(f1) + ")";
    if (d <= 2) console.log("ok: " + line);
    else { console.log("FAIL: " + line); failures.push(label); }
    return worst(before, f1);
}
var failures = [];

var moved = arm("power", function () { world.sky("realistic", { power: 1.5 }); });
assert(moved >= 3, "the power change moved the picture (" + moved + ")");
moved = arm("sun", function () {
    node.transform(sun.light, { rotation: { x: -25, y: 110, z: 0 } });
});
assert(moved >= 3, "the sun move moved the picture (" + moved + ")");
moved = arm("to_gradient", function () {
    world.sky("gradient", { top: "#1a3a8a", mid: "#80a0d0", bottom: "#d8c8a0" });
});
assert(moved >= 3, "realistic -> gradient moved the picture (" + moved + ")");
moved = arm("to_realistic", function () { world.sky("realistic", { power: 1.5 }); });
assert(moved >= 3, "gradient -> realistic moved the picture (" + moved + ")");
// speed 0: no scroll, so the only capture is the change's own. A layer that
// failed to draw would pass the frame-0 check by measuring nothing: it must move
// the picture.
moved = arm("clouds_on", function () { world.clouds({ enabled: true, coverage: 0.7, speed: 0 }); });
assert(moved >= 3, "the cloud layer going on moved the picture (" + moved + ")");
moved = arm("clouds_off", function () { world.clouds({ enabled: false }); });
assert(moved >= 3, "the cloud layer going off moved the picture (" + moved + ")");

// ---- THE CHAIN ON: frame 0 BY CONSTRUCTION --------------------------------
// editor.screenshot settles GI before it photographs, and whether the first
// shot after a change is frame 0 then depends on whether the chain happened to
// be at rest when the settle looked. camera.screenshot takes no settle at all
// (cameraapi.cpp -> takeScreenshot): its frames ARE the first frames after the
// change, whatever GI owes. A scene camera at the editor camera's pose.
var cam = scene.addCamera({ position: { x: 0, y: 40, z: 40 } });
node.transform(cam, { rotation: { x: -45, y: 0, z: 0 } });
editor.selectNone();
function camShot(tag) {
    return camera.screenshot(cam, "sky_swap_cam_" + tag + ".png",
                             { width: 480, height: 270, probes: probes, grade: "plain" }).probes;
}
function armCam(label, change) {
    // THE CHAIN AT REST BEFORE THE CHANGE, through the camera's own view (the
    // shot view drives GI while it renders): otherwise the frames after the
    // change still carry the previous edit's settle and measure THAT.
    var before = camShot(label + "_before");
    for (var n = 0; n < 600 && !world.giStatus().giAtRest; n++) before = camShot(label + "_before");
    var rest = world.giStatus();
    console.log("SKYSWAP " + label + " at rest before the change: " + rest.giAtRest + " (" + n + " shots)");
    // ...and a LONE change: more than kSkyCaptureDragFrames (2) drawn frames
    // after the last capture, or the engine reads it as a drag (the drag arm).
    editor.frame(4, 1 / 60);
    change();
    var f0 = camShot(label + "_f0");
    editor.frame(1, 1 / 60);
    var f1 = camShot(label + "_f1");
    console.log("SKYSWAP " + label + " before " + rgb(before) + " | f0 " + rgb(f0) +
                " | f1 " + rgb(f1));
    var d = worst(f0, f1);
    var line = label + ": the first frame after the change equals the next within 2/255 (worst " +
               d + "; f0 " + rgb(f0) + " vs f1 " + rgb(f1) + ")";
    if (d <= 2) console.log("ok: " + line);
    else { console.log("FAIL: " + line); failures.push(label); }
    return worst(before, f1);
}
// THE REPRO ITSELF (CLOUDS-2D-1's ifd_magenta_repro, FIELD-ROTATE-1's frames2):
// the Photon chain ON with the irradiance field off, the power going 3.0 -> 1.5.
// Here the chain's own readers (the bounce injection's escape) carried the
// uncaptured cube into the frames after it — 111/28/118 on the ground at base.
world.clouds({ enabled: false });
world.gi({ mode: "vct", ddgi: false });
world.sky("realistic", { power: 3.0 });
moved = armCam("repro_power", function () { world.sky("realistic", { power: 1.5 }); });
assert(moved >= 3, "the repro's power change moved the picture (" + moved + ")");
// ...and the SHIPPED configuration: the chain and the irradiance field on.
world.gi({ mode: "vct", ddgi: true });
moved = armCam("field_power", function () { world.sky("realistic", { power: 3.0 }); });
assert(moved >= 3, "the field arm's power change moved the picture (" + moved + ")");

// ---- A DRAG: the second capture within two frames of the first -------------
// Its SH read is deferred (integrateSkyShFromCube), so its set lands a frame or
// more later — and until it does, the PREVIOUS set is drawn WHOLE. The first
// frame after the second change must therefore be either the picture the first
// change produced or the one the second produces, never a mixture of the two
// and never an unwritten cube. GI off again, as for the first six arms: the
// chain's own settle after a change is not what this arm measures.
world.gi({ mode: "off" });
editor.frame(8, 1 / 60);
world.sky("realistic", { power: 2.2 });
var dragA = camShot("drag_a");                 // the first change, landed (lone)
world.sky("realistic", { power: 1.5 });        // within two frames: a drag
var dragF0 = camShot("drag_f0");
editor.frame(4, 1 / 60);
var dragB = camShot("drag_b");
var dA = worst(dragF0, dragA), dB = worst(dragF0, dragB);
console.log("SKYSWAP drag a " + rgb(dragA) + " | f0 " + rgb(dragF0) + " | b " + rgb(dragB));
assert(worst(dragA, dragB) >= 3, "the drag's two skies differ (" + worst(dragA, dragB) + ")");
assert(Math.min(dA, dB) <= 2, "a drag frame draws ONE whole set: f0 is the first sky's picture (" + dA +
       ") or the second's (" + dB + "), within 2/255");
assert(failures.length === 0, "no change flashes its first frame (failed: " + failures.join(", ") + ")");
console.log("sky_swap_transient: PASS");
