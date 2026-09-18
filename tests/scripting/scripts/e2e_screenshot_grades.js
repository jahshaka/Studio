// scripting.e2e.screenshot_grades — WHAT A SCREENSHOT IS A PICTURE OF
// (SS1, owner 2026-09-13: "match the screenshot to the scene properly", and on
// the pixel suites: "your pixel tests can have their own screenshot or a
// screenshot setting that you can use the same function to get what you want
// and what the users want").
//
// ONE function, an explicit mode. This suite is the contract for all four
// modes of IEditorViewport::ScreenshotGrade, driven through the verb the
// editor's own Screenshot button calls into:
//
//   plain / raw  no post-processing at all — the exact readback every pixel
//                suite in the tree asserts. It MUST NOT MOVE, ever.
//   tonemap      the thumbnail picture: the deterministic filmic grade only,
//                at the SCENE's exposure (it used to ignore the World's value).
//   scene        THE EDITOR'S OWN PICTURE, and what a user gets: the whole
//                post chain as the world has it, at the exposure the on-screen
//                viewport has converged on.
//   viewport     the whole chain with the chain's own adaptive exposure
//                re-seeded (camera.screenshot's door).
//
// THE THREE DEFECTS IT PINS (measured by the diagnosis, spikes/screenshot-parity):
//   * the World's exposure never reached the shot at all — 0.6 -> 2.4 moved the
//     viewport's mean 63.9 -> 187.9 and the shot not at all (RMSE 0)  -> phase C
//   * the shot was graded even when the world has HDR switched OFF, where the
//     viewport is ungraded (Low tier: xwd vs shot RMSE 0.0859)        -> phase B
//   * SSAO, SSR, bloom and SMAA were simply absent from the picture    -> phase D
//
// FAIL-BEFORE: on the pre-SS1 binary phase A passes (nothing about the plain
// grade changes), and B, C and D all fail — with `grade:"scene"` refused
// outright and `grade:"tonemap"`, which is what the Screenshot button used to
// hand a user, failing B (it grades a non-HDR world), C (it pins +0.6) and D
// (it has no chain).
//
// ENGINE-UP and ON-SCREEN: the `scene` grade's exposure is READ OFF the
// viewport's own converged luminance history, so this suite needs a real
// viewport rendering real frames. It cannot run --headless.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

// A grid of probe points that avoids the exact centre (a single pixel is the
// least stable thing in a GI scene) and samples floor, walls and objects.
var PROBES = [];
for (var py = 0; py < 3; ++py)
    for (var px = 0; px < 3; ++px)
        PROBES.push({ x: 0.25 + px * 0.25, y: 0.3 + py * 0.22 });

var W = 480, H = 270, SHOTS = 0;

function shoot(grade) {
    var path = "shot-" + (++SHOTS) + "-" + grade + ".png";
    return editor.screenshot(path, W, H, PROBES, grade);
}
function luma(p) { return 0.2126 * p.r + 0.7152 * p.g + 0.0722 * p.b; }
function mean(shot) {
    var s = 0;
    for (var i = 0; i < shot.probes.length; ++i) s += luma(shot.probes[i]);
    return s / shot.probes.length;
}
function maxDelta(a, b) {
    var m = 0;
    for (var i = 0; i < a.probes.length; ++i) {
        m = Math.max(m, Math.abs(a.probes[i].r - b.probes[i].r));
        m = Math.max(m, Math.abs(a.probes[i].g - b.probes[i].g));
        m = Math.max(m, Math.abs(a.probes[i].b - b.probes[i].b));
    }
    return m;
}
function J(v) { return JSON.stringify(v); }

// The viewport's automatic exposure is a temporal filter that converges at 75%
// per second; the frames are the document's own 1/60 s grid, so this is a
// deterministic number of steps, not a wall-clock wait. 240 frames = 4 s of
// simulated time = 0.25^4, i.e. 99.6% of the way there.
function settle() { editor.frame(240, 1 / 60); }

// ---------------------------------------------------------------------------
// A room with a reflective floor, at 1 unit = 1 metre (the scene-scale
// convention). Deliberately synthetic rather than a shipped sample: the
// Showroom is not open-to-open pixel deterministic (VCT + autoRefresh), and
// every assertion below is a DIFFERENCE measured inside one open anyway.
assert(project.create("Shot Grades " + Date.now()).length > 10, "created the project");

function slab(name, pos, scale, color, rough, metal) {
    var id = scene.addPrimitive("cube", { position: pos });
    node.setProperty(id, "name", name);
    node.transform(id, { scale: scale });
    material.set(id, { baseColor: color, roughness: rough, metallic: metal === undefined ? 0 : metal });
    return id;
}

["Directional Light", "Point Light"].forEach(function (n) {
    var stray = scene.find(n);
    if (stray) node.remove(stray);
});


// THE REFLECTIVE FLOOR IS THIS SUITE'S OWN SLAB, not the scene's default
// ground. A near-mirror plane is what makes screen-space reflections visible at
// all (phase D), and the default ground is somebody else's subject: it has
// changed material, size and edge treatment twice in two days (GF1), and each
// time it moved this suite's SSR reading with it. A slab laid on top answers
// only to this file.
var mirrorFloor = slab("MirrorFloor", { x: 0, y: 0.02, z: 0 },
                       { x: 6, y: 0.02, z: 6 }, "#b8bcc4", 0.06, 1.0);
assert(mirrorFloor.length > 0, "the room stands on its own polished floor");

slab("BackWall",  { x: 0, y: 1.8, z: -4.5 }, { x: 6, y: 1.8, z: 0.25 }, "#d8d4cc", 0.85);
slab("LeftWall",  { x: -4.5, y: 1.8, z: 0 }, { x: 0.25, y: 1.8, z: 6 }, "#e02020", 0.85);
slab("RightWall", { x: 4.5, y: 1.8, z: 0 },  { x: 0.25, y: 1.8, z: 6 }, "#2040e0", 0.85);
slab("Pillar",    { x: -1.6, y: 1.0, z: -1.0 }, { x: 0.4, y: 1.0, z: 0.4 }, "#f0e8d0", 0.6);

var sun = scene.addLight("directional", { name: "Key" });
node.transform(sun, { rotation: { x: -55, y: 35, z: 0 } });
node.setProperty(sun, "intensity", 2.2);

// THE MOVABLE OBJECT (the brief's fourth test). A probe capture does not draw
// kMovableBit, so an object the document calls movable is exactly the thing a
// shot rendered through a probe-style pass would silently lose.
var ball = scene.addPrimitive("sphere", { position: { x: 1.3, y: 0.9, z: -0.6 } });
node.setProperty(ball, "name", "Mover");
node.transform(ball, { scale: { x: 0.9, y: 0.9, z: 0.9 } });
material.set(ball, { baseColor: "#20d050", roughness: 0.35, metallic: 0.0 });
node.setProperty(ball, "mobility", "movable");
assert(node.mobility(ball).resolved === "movable", "the ball is a MOVABLE object");

// One fixed pose for every picture in this suite. An offscreen shot's content
// follows the window's aspect only through a pose that was FRAMED for the
// window (F / focusSelection); a pose set with editor.setCamera renders the
// same picture at any size (CLAUDE.md, the 1920x1080 rig law).
editor.setCamera({ position: { x: 0.6, y: 2.1, z: 5.4 },
                   lookAt:   { x: 0.0, y: 0.9, z: -1.0 }, fov: 45 });

// NO EDITOR HELPERS for phases A-F, and that is not tidiness: since the owner's
// decision of 2026-09-13 a user's screenshot leaves them OUT, so a viewport with
// a grid up would make `plain` and `scene` differ by the GRID as well as by the
// chain — and every phase below is measuring the chain. Phase G is where the
// helpers are switched on, on purpose, and is the assertion that they stay out.
editor.setOverlays({ grid: false, lightWires: false });
editor.select(null);

// ---------------------------------------------------------------------------
// PHASE A — THE PLAIN GRADE IS THE TEST PICTURE, AND IT DOES NOT MOVE.
//
// Four spellings of "give me nothing": the default, the word every suite in the
// tree was written with, the new name that says what it is, and the boolean the
// pixel corpus passes. All four must be the SAME picture, pixel for pixel.
// THE SINGLE VOLUME, PINNED, and it is the subject that asks for it. Every SHOT
// below renders a frame, and a frame is where the renderer spends queued work:
// since PHOTON_SPEC §7 E2 (6) every tier builds the camera-centred cascade chain
// and the pose set above queues a re-centre per cascade, one per frame. Four
// shots taken back to back would be four moments of the same scroll — a true
// statement about the renderer and a useless one about the GRADES this file
// compares, which are a property of the compositor chain and not of GI. So the
// scene is put on the one volume, which stands still, and every phase below
// compares grades against grades.
world.gi({ cascades: false });
// ...AND SETTLED, which the push itself makes necessary: a GI push is a re-solve
// and a re-solve stales the whole reflection-probe grid, which then re-captures
// at the update budget — one probe per frame. Every SHOT renders a frame, so
// four shots taken mid-catch-up are four different pictures of the same scene
// and phase A would be comparing the catch-up, not the grades. Render until the
// renderer says nothing is owed. (The cameras.exposure lesson again: a fixed
// frame count is a wall-clock settle in disguise.)
// SETTLE ON THE PICTURE, not on a frame count and not on a single counter.
// Two things in this renderer converge over frames and neither is a GI counter:
// the reflection-probe catch-up (one probe per frame at the update budget) and
// the HDR EXPOSURE, which adapts per frame and is charged through the fixed
// clock — the `cameras.exposure` lesson, CLAUDE.md: "a wall-clock settle
// measures nothing in this engine; count frames, or read until the value stops
// moving". This reads until the value stops moving: shoot, step, shoot, and
// stop when two consecutive pictures are identical. Every phase below compares
// pictures taken several renders apart, so this is what makes those comparisons
// about the GRADES.
function settleGi(tag) {
    var prev = null;
    for (var r = 0; r < 40; r++) {
        editor.frame(10, 1 / 60);
        var now = editor.screenshot("settle-" + tag.replace(/[^a-z]/g, "") + ".png", W, H, PROBES);
        if (prev && maxDelta(prev, now) === 0) {
            console.log("settled (" + tag + ") after " + r + " rounds of 10 frames");
            return;
        }
        prev = now;
    }
    console.log("settled (" + tag + ") NEVER — the picture is still moving");
}
settleGi("phase A");

console.log("---- phase A: the plain grade ----");
var noGrade = editor.screenshot("shot-a-default.png", W, H, PROBES);
var raw     = shoot("raw");
var plain   = shoot("plain");
var boolean = editor.screenshot("shot-a-false.png", W, H, PROBES, false);

assert(maxDelta(noGrade, raw) === 0, "editor.screenshot's DEFAULT is the plain readback");
assert(maxDelta(raw, plain) === 0, "\"plain\" is \"raw\" — the same picture under an honest name");
assert(maxDelta(raw, boolean) === 0, "`false` is the plain readback too (the pixel corpus's spelling)");

var threw = false;
try { shoot("glorious"); } catch (e) { threw = true; console.log("   refused: " + e.message); }
assert(threw, "an unknown grade is refused, catchably, rather than guessed at");

// ---------------------------------------------------------------------------
// PHASE B — A WORLD WITH HDR OFF PHOTOGRAPHS UNGRADED.
//
// Isolated: every other chain row off, so the ONLY difference between the two
// pictures can be the tonemap. With the chain empty the `scene` grade must hand
// back exactly the plain readback — which is what the viewport is showing.
console.log("---- phase B: HDR off means an ungraded shot ----");
["ssao", "ssr", "smaa", "bloom"].forEach(function (id) {
    world.override({ id: id, value: (id === "smaa") ? "off" : ((id === "ssr") ? "off" : false) });
});
world.override({ id: "hdr", value: false });
settle();
var plainNoHdr = shoot("plain");
var sceneNoHdr = shoot("scene");
console.log("   HDR off: plain mean " + mean(plainNoHdr).toFixed(1) +
            ", scene mean " + mean(sceneNoHdr).toFixed(1));
assert(maxDelta(plainNoHdr, sceneNoHdr) <= 1,
       "HDR off: the shot is UNGRADED, like the viewport (max channel delta " +
       maxDelta(plainNoHdr, sceneNoHdr) + ")");

// ...and with HDR back on the same two pictures must SEPARATE, or the
// assertion above would be passing for the wrong reason. At the world's own
// +2.0 the film curve has something to do: a linear readback of this room is
// dim, the graded one is not.
world.override({ id: "hdr", value: true });
world.postFx({ exposureEv: 2.0 });
settle();
var plainHdr = shoot("plain");
var sceneHdr = shoot("scene");
console.log("   HDR on: plain mean " + mean(plainHdr).toFixed(1) +
            ", scene mean " + mean(sceneHdr).toFixed(1) +
            ", max channel delta " + maxDelta(plainHdr, sceneHdr));
assert(maxDelta(plainHdr, sceneHdr) > 20,
       "HDR on: the shot IS graded (max channel delta " + maxDelta(plainHdr, sceneHdr) + ")");
world.postFx({ exposureEv: 0.0 });

// ---------------------------------------------------------------------------
// PHASE C — THE WORLD'S EXPOSURE REACHES THE PICTURE.
//
// The exact defect the owner reported, and the exact fail-before the diagnosis
// measured: world exposure 0.6 -> 2.4 moved the viewport's mean from 63.9 to
// 187.9 and the built-in tool's shot not at all.
console.log("---- phase C: the world's exposure changes the shot ----");
world.postFx({ exposureEv: 0.0 });
settle();
var dimScene   = shoot("scene");
var dimThumb   = shoot("tonemap");

world.postFx({ exposureEv: 2.6 });
settle();
var brightScene = shoot("scene");
var brightThumb = shoot("tonemap");

console.log("   scene grade:   mean " + mean(dimScene).toFixed(1) + " -> " + mean(brightScene).toFixed(1));
console.log("   tonemap grade: mean " + mean(dimThumb).toFixed(1) + " -> " + mean(brightThumb).toFixed(1));
assert(mean(brightScene) - mean(dimScene) > 25,
       "the scene grade follows the World's exposure (+" +
       (mean(brightScene) - mean(dimScene)).toFixed(1) + " mean)");
// The thumbnail grade's own half of the same one-line fix: a thumbnail of a
// regraded world used to be a picture of the ungraded one.
assert(mean(brightThumb) - mean(dimThumb) > 25,
       "the thumbnail grade follows it too (+" +
       (mean(brightThumb) - mean(dimThumb)).toFixed(1) + " mean)");

// DETERMINISTIC: the exposure that reached the shot is a CONSTANT by the time
// the picture is taken, so the same viewport state photographs identically.
var again = shoot("scene");
assert(maxDelta(brightScene, again) === 0,
       "two shots of the same converged viewport are the same picture");

world.postFx({ exposureEv: 0.0 });
settle();

// ---------------------------------------------------------------------------
// PHASE D — THE SCENE'S WHOLE CHAIN IS IN THE PICTURE.
//
// Epic's rows, on a polished floor: what the diagnosis found missing.
console.log("---- phase D: the chain is in the shot ----");
["hdr", "ssao", "ssr", "smaa", "bloom"].forEach(function (id) { world.clearOverride({ id: id }); });
assert(world.photon({ tier: "epic" }).tier === "epic", "the world is at the Epic tier");
var rows = world.settings();
assert(rows.ssr.valueId !== "off", "Epic has screen-space reflections on: " + rows.ssr.valueId);
assert(rows.ssao.valueId !== "off", "Epic has ambient occlusion on: " + rows.ssao.valueId);
assert(rows.smaa.valueId !== "off", "Epic has SMAA on: " + rows.smaa.valueId);
assert(rows.hdr.value !== 0, "Epic has HDR on");
// A LIT room for the rest of the suite. The differences the chain makes are
// measured in 8-bit levels, and at the scene default (0 stops) this room sits
// in the bottom sixth of the range where quantisation eats them; +2 STOPS is
// the same picture with the grade opened up, not a different test. (EXPOSURE-1
// moved this verb to stops; these numbers are the same grades in the new unit.)
world.postFx({ exposureEv: 2.0 });
settle();

var epicPlain = shoot("plain");
var epicScene = shoot("scene");
assert(maxDelta(epicPlain, epicScene) > 8,
       "at Epic the shot carries the chain the plain readback has none of (delta " +
       maxDelta(epicPlain, epicScene) + ")");

// SSR, isolated: switching the reflections off must MOVE the picture the user
// gets. Probed on the POLISHED FLOOR under the green ball and the red wall,
// which is where a screen-space reflection has something to say; the grid
// above deliberately samples the whole frame and averages the effect away.
var FLOOR = [];
for (var fy = 0; fy < 4; ++fy)
    for (var fx = 0; fx < 5; ++fx)
        FLOOR.push({ x: 0.18 + fx * 0.16, y: 0.70 + fy * 0.08 });
function shootFloor(tag) { return editor.screenshot("shot-floor-" + tag + ".png", W, H, FLOOR, "scene"); }

var withSsr = shootFloor("ssr-on");
world.override({ id: "ssr", value: "off" });
settle();
var noSsr = shootFloor("ssr-off");
world.clearOverride({ id: "ssr" });
console.log("   SSR on/off max channel delta on the floor: " + maxDelta(withSsr, noSsr));
assert(maxDelta(withSsr, noSsr) > 4,
       "screen-space reflections are IN the user's shot — switching them off changes the floor (delta " +
       maxDelta(withSsr, noSsr) + ")");

// ...AND THE PLAIN GRADE HAS NONE OF THEM, WITH SSR ON — the other half of the
// same contract, and the half that was misread (lane SSR-2, round 2). A
// diagnosis compared two PLAIN shots of a mirror scene at ssr hq and ssr off,
// found them bit-identical over 1.44 Mpx and reported the offscreen chain as
// having lost SSR; it had not — Plain is the pixel-suites' readback and takes
// the offscreen early-out by construction (OgreView::chainDesc), and Plain is
// `editor.screenshot`'s DEFAULT, which is what made the misreading easy. So
// with the reflections ON, the same slab, in the same open: Scene carries them
// and Plain does not.
settle();
var plainSsr = editor.screenshot("shot-floor-plain-ssr-on.png", W, H, FLOOR, "plain");
console.log("   with SSR on, scene vs PLAIN on the floor: " + maxDelta(withSsr, plainSsr));
assert(maxDelta(withSsr, plainSsr) > 4,
       "the plain grade is the readback, not the picture: with SSR on it differs from the " +
       "scene grade on the reflective floor (delta " + maxDelta(withSsr, plainSsr) + ")");

// ---------------------------------------------------------------------------
// PHASE E — A MOVABLE OBJECT IS IN THE PICTURE.
//
// Not a probe-style pass: kMovableBit has to be drawn.
console.log("---- phase E: a movable object appears ----");
settle();
var withBall = editor.screenshot("shot-mover-in.png", W, H,
                                 [{ x: 0.62, y: 0.52 }], "scene");
node.setProperty(ball, "visible", false);
settle();
var withoutBall = editor.screenshot("shot-mover-out.png", W, H,
                                    [{ x: 0.62, y: 0.52 }], "scene");
node.setProperty(ball, "visible", true);
var p1 = withBall.probes[0], p0 = withoutBall.probes[0];
console.log("   with the mover " + J([p1.r, p1.g, p1.b]) + ", without it " + J([p0.r, p0.g, p0.b]));
assert(p1.g > p1.r + 12 && p1.g > p1.b + 12,
       "the MOVABLE ball is in the shot, and it is green");
assert((p1.g - p1.r) - (p0.g - p0.r) > 12,
       "hiding it takes the green with it — that pixel really is the ball");

// ---------------------------------------------------------------------------
// PHASE G — A USER'S SCREENSHOT LEAVES THE EDITOR'S HELPERS OUT.
//
// OWNER, 2026-09-13, answering the question this lane put to him: a screenshot
// is a picture of the SCENE. The gizmo, the selection outline, the light and
// camera wires, the grid and the GI boxes are all out of it, even while the
// viewport is showing them. Asserted the only way that cannot pass for the
// wrong reason: with the helpers switched ON and a node selected, the PLAIN
// readback must move a lot and the user's picture must not move at all.
//
// AND THE RESTORE (review item 3). The first cut cleared the helper switches by
// hand and trusted refreshOverlay() to put them back; it does not, so the next
// picture in the same run came back helper-less — and the next picture is very
// often the exact readback the whole pixel corpus swears never moves. Two
// plain shots either side of a scene shot, byte for byte, is that assertion.
console.log("---- phase G: the helpers are the editor's, not the picture's ----");
editor.setOverlays({ grid: true, lightWires: true });
assert(editor.select(ball), "the ball is selected — gizmo and outline are up");
settle();

var plainHelpersA = shoot("plain");
var sceneHelpers  = shoot("scene");
var plainHelpersB = shoot("plain");
assert(maxDelta(plainHelpersA, plainHelpersB) === 0,
       "A SCENE SHOT DOES NOT LEAK: the plain readback after one is byte-identical to the one before");

editor.select(null);
editor.setOverlays({ grid: false, lightWires: false });
settle();
var plainBare = shoot("plain");
var sceneBare = shoot("scene");

console.log("   plain  with helpers vs without: max channel delta " + maxDelta(plainHelpersA, plainBare));
console.log("   scene  with helpers vs without: max channel delta " + maxDelta(sceneHelpers, sceneBare));
assert(maxDelta(plainHelpersA, plainBare) > 20,
       "the helpers ARE in the viewport (a plain shot moves by " +
       maxDelta(plainHelpersA, plainBare) + " when they go)");
// Not exact equality, and the reason is worth stating: the helpers are out of
// the PICTURE, but they are still on screen, so they move the viewport's own
// measured exposure by a hair — and the user's shot borrows that exposure. A
// couple of 8-bit levels is that hair; the gizmo is 20+.
assert(maxDelta(sceneHelpers, sceneBare) <= 3,
       "and NONE of them is in the user's picture (delta " +
       maxDelta(sceneHelpers, sceneBare) + ")");

// ---------------------------------------------------------------------------
// PHASE F — THE OTHER TWO GRADES STILL EXIST AND STILL MEAN WHAT THEY SAY.
console.log("---- phase F: tonemap and viewport ----");
var tm = shoot("tonemap");
var vp = shoot("viewport");
assert(maxDelta(tm, vp) > 2,
       "\"tonemap\" (no chain) and \"viewport\" (the whole chain) are different pictures");
assert(maxDelta(shoot("scene"), tm) > 2,
       "\"scene\" is not the thumbnail picture either");

// ---------------------------------------------------------------------------
// PHASE H — THE SAME FOUR WORDS ON camera.screenshot (CLEANUP-1 item 5).
//
// There were three screenshot doors and two exposures between them: the shell
// button asked for `scene`, editor.screenshot and player.screenshot took the
// four words, and camera.screenshot still took a BOOLEAN — so the MCP tool,
// which renders a scene camera through it with postFx:true, was handing back
// the `viewport` grade (the whole chain with its adaptive exposure RE-SEEDED,
// in an offscreen view about two frames long that cannot converge) while its
// description promised what the user sees. The word list now lives once, in
// IEditorViewport::gradeFromString, and this is the verb that did not have it.
console.log("---- phase H: camera.screenshot takes the grade too ----");
var camId = scene.addCamera({ position: { x: 0, y: 2, z: 6 }, name: "Grade Cam" });
editor.frame(2);

function camShot(options) {
    options.width = W;
    options.height = H;
    options.probes = PROBES;
    return camera.screenshot(camId, "cam-shot-" + (++SHOTS) + ".png", options);
}

// SETTLED AGAIN before the camera doors: this phase takes six shots of one pose
// and compares the first against the last, and every shot renders a frame — so
// anything the renderer still owes (a probe re-capture after the edits above)
// would land between them and read as a grade difference.
settleGi("camera doors");
// THE TWO PICTURES THIS PHASE COMPARES ARE TAKEN BACK TO BACK, and that is not
// tidiness: a SCENE CAMERA has its own view and therefore its own HDR adaptation
// state, which advances only when that view renders — i.e. once per camShot. The
// comparison below used to be the FIRST shot against the SIXTH, with four
// spelling checks in between, so it was reading five frames of exposure
// adaptation as a grade difference (the `cameras.exposure` lesson: read until
// the value stops moving, or compare adjacent reads). The spelling checks follow.
// ...AND THE CAMERA'S OWN VIEW IS SETTLED FIRST. It is a SECOND view with its
// own HDR adaptation state, and that state advances only when IT renders — once
// per camShot — so its first few pictures are its exposure converging and
// nothing else. Shoot until two consecutive pictures are identical.
var camPlain = camShot({ grade: "plain" });
for (var camRound = 0; camRound < 40; camRound++) {
    var next = camShot({ grade: "plain" });
    if (maxDelta(camPlain, next) === 0) break;
    camPlain = next;
}
console.log("camera view settled after " + camRound + " shots");
var camRaw   = camShot({ grade: "raw" });
assert(camPlain.width === W && camPlain.probes.length === PROBES.length,
       "camera.screenshot({grade:'plain'}) renders and probes like the other doors");
assert(maxDelta(camPlain, camRaw) === 0, "\"raw\" and \"plain\" are the same picture");
// EVERY documented spelling parses — a verb whose parser is narrower than its
// own documentation is how player.screenshot's `scene` branch once shipped
// unreachable.
["raw", "tonemap", "scene", "viewport"].forEach(function (word) {
    var shot = camShot({ grade: word });
    assert(shot.width === W, "camera.screenshot accepts grade \"" + word + "\"");
});
var camPlain2 = camShot({ grade: "plain" });
var camScene = camShot({ grade: "scene" });
assert(maxDelta(camPlain2, camScene) > 2,
       "\"scene\" through a scene camera is a GRADED picture (delta " +
       maxDelta(camPlain2, camScene) + ")");
// The older boolean spelling still means what it always meant: false = plain,
// true = viewport. The pixel suites pass it that way.
// EVERY COMPARISON IN THIS PHASE IS BETWEEN ADJACENT SHOTS, for the reason at
// the top of it: the reference is re-taken beside the picture it is compared
// against, never carried across the spelling checks.
assert(maxDelta(camShot({ postFx: false }), camShot({ grade: "plain" })) === 0,
       "postFx:false is still the exact readback");
assert(maxDelta(camShot({ postFx: true }), camShot({ grade: "viewport" })) === 0,
       "postFx:true is still the \"viewport\" grade");
var refused = false;
try { camShot({ grade: "gorgeous" }); } catch (e) { refused = true; }
assert(refused, "an unknown grade is refused, catchably, rather than guessed at");

// ---------------------------------------------------------------------------
// PHASE H — THE COLOUR SPACE OF EACH GRADE, AND THE ARITHMETIC THAT SAYS WHICH
// (PLAIN-GRADE-1, 2026-09-18).
//
// The render audit asked whether an offscreen readback is gamma-encoded like
// the window (ON-20) and left it open; every offscreen diagnosis in this tree
// meanwhile read "too dark" and blamed the renderer. MEASURED, twice: the plain
// grade's bytes are LINEAR RADIANCE, the three graded answers are the WINDOW'S
// OWN BYTES (an xwd grab of the live window at a flat #8000C0 sky read
// (50, 0, 114) against the scene grade's (50, 0, 114)), and this engine's
// window swapchain is not sRGB at all — the tonemapper's output IS the display
// code. Encoding the plain readback would therefore have made it disagree with
// the window AND with every engine-side offscreen suite.
//
// So the fact worth pinning is the ARITHMETIC, and a flat sky is the one
// surface whose radiance a test knows exactly: the document decodes a picked
// colour sRGB->linear, so #808080 is radiance 0.2159 and nothing in the scene
// adds to a sky pixel.
//
//   plain  = round(255 x 0.2159) = 55        (the decode, un-encoded)
//   scene  = the film curve of that radiance at the scene's exposure
//
// FAILS BEFORE: on a build whose plain readback were display-encoded, the first
// assertion reads 124 instead of 55.
console.log("---- phase H: the two colour spaces ----");
world.sky("color", { color: "#808080" });
editor.setCamera({ position: { x: 0, y: 2, z: 6 }, lookAt: { x: 0, y: 40, z: -10 } });
settleGi("phase H");

// The sky fills the frame at this pose, so the CENTRE pixel is a sky pixel.
var skyPlain = editor.screenshot("shot-h-plain.png", W, H, [], "plain");
var skyScene = editor.screenshot("shot-h-scene.png", W, H, [], "scene");
assert(skyPlain.encoding === "linear" && skyPlain.grade === "plain",
       "the plain grade reports itself as linear (" + J(skyPlain.encoding) + ")");
assert(skyScene.encoding === "display" && skyScene.grade === "scene",
       "the scene grade reports itself as the display space (" + J(skyScene.encoding) + ")");
// The sRGB decode of 0x80, in the test rather than as a remembered number.
function srgbToLinear(c) {
    return c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4);
}
var wanted = Math.round(255 * srgbToLinear(128 / 255));
assert(Math.abs(skyPlain.center.r - wanted) <= 1 &&
       Math.abs(skyPlain.center.g - wanted) <= 1 &&
       Math.abs(skyPlain.center.b - wanted) <= 1,
       "a #808080 sky reads " + J(skyPlain.center) + " at the plain grade — the LINEAR " +
       "radiance " + wanted + "/255, not the " + 128 + "/255 a display-encoded readback " +
       "would give");
// ...and the graded picture is the film curve of that radiance: a DIFFERENT
// number, below it here (the shipped curve pulls a mid tone down), which is the
// whole of "a plain shot is not a dark picture of the scene".
assert(skyScene.center.r !== skyPlain.center.r,
       "the same sky develops to " + J(skyScene.center) + " in the editor's own picture");
// EVERY DOOR AGREES ABOUT THE SPACE IT IS IN — the fields come from one
// function (IEditorViewport::gradeEncoding), so this is the contract that a
// second door cannot answer differently.
["plain", "raw", "tonemap", "scene", "viewport"].forEach(function (word) {
    var shot = editor.screenshot("shot-h-" + word + ".png", 64, 64, [], word);
    var expect = (word === "plain" || word === "raw") ? "linear" : "display";
    assert(shot.encoding === expect,
           "editor.screenshot(grade: \"" + word + "\") reports encoding " + expect);
    assert(shot.grade === (word === "raw" ? "plain" : word),
           "...and names its grade canonically (" + shot.grade + ")");
});
var camSpace = camShot({ grade: "tonemap" });
assert(camSpace.encoding === "display" && camSpace.grade === "tonemap",
       "camera.screenshot reports the same pair through its own door");

console.log("PASS: every screenshot grade is a picture of what it says it is");
