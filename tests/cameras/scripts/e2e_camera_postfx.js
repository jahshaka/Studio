// cameras.e2e.postfx — CAMERA_LENS_SPEC §4 + §5 through the verbs, in the real
// app binary.
//
// Phase A: the exposure block as camera.settings keys — modes, stops, the
//          ordered window, and every refusal.
// Phase B: camera.postFx — the tri-state read, writing an override, clearing
//          one with null, `resolved` against the world, and `available`.
// Phase C: the refusals that ARE the design: exposure is not an override slot,
//          an SMAA preset is not a per-camera value, an unknown key is a typo.
// Phase D: camera.clearPostOverride, one row and all rows.
// Phase E: the reflected (keyframeable) half — node.setProperty reaches the
//          same fields, and a null clears an override.
// Phase F: SAVE -> CLOSE -> OPEN of the whole block through the real writer and
//          the real reader, plus the tolerated-absent contract.
// Phase G: undo — every write is one command in the run's macro, reads are none.
// Phase H: the generated panel builds, with and without overrides.
// Phase I: the ONE offscreen opt-in, in pixels.
// Phase J: the LOOKS stack — the one WHOLE-STACK override, its three states
//          (inherit / none / its own), the shared validator, the reflected
//          half, and the save/open round trip.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function near(a, b, eps, msg) {
    var d = Math.abs(a - b);
    assert(d <= (eps || 1e-3), msg + " (" + a + " vs " + b + ")");
}
function refuses(fn, msg) {
    var threw = false;
    try { fn(); } catch (e) { threw = true; }
    assert(threw, msg);
}

var guid = project.create("CamPost " + Date.now());
assert(guid.length > 10, "project.create -> " + guid);

var cam = scene.addCamera({ position: { x: 0, y: 0, z: 5 } });
assert(cam.length > 10, "scene.addCamera -> " + cam);

// ---- phase A: the exposure block -----------------------------------------
var s = camera.settings(cam);
assert(s.exposureMode === "inherit",
       "a new camera INHERITS the world's exposure — the only default that changes nothing");
assert(s.exposure === 0, "…at 0 stops");
assert(s.exposureMin < s.exposureMax,
       "…with an ordered auto window (" + s.exposureMin + ".." + s.exposureMax + ")");

s = camera.settings(cam, { exposureMode: "manual", exposure: -1.5 });
assert(s.exposureMode === "manual", "exposureMode writes and reads back as a word");
near(s.exposure, -1.5, 1e-5, "…and the exposure is in STOPS");

s = camera.settings(cam, { exposureMode: "auto", exposureMin: 2, exposureMax: -2 });
assert(s.exposureMin <= s.exposureMax,
       "the window stays ordered however it is written (" + s.exposureMin + ".." +
       s.exposureMax + ")");

refuses(function () { camera.settings(cam, { exposureMode: "cinematic" }); },
        "an exposureMode that is not inherit/auto/manual is refused, not guessed");
refuses(function () { camera.settings(cam, { exposure: 40 }); },
        "40 stops is a typo, not a shot — refused with the unit named");

// The whole block round-trips through settings(id, settings(id)) minus the three
// read-only echoes, exactly like every other row.
var block = camera.settings(cam);
// The three read-only echoes, plus focalLength — which is `angle` seen through
// the sensor, so passing both back is refused by design.
delete block.id; delete block.name; delete block.outputWidth; delete block.focalLength;
var again = camera.settings(cam, block);
assert(again.exposureMode === block.exposureMode && again.exposure === block.exposure,
       "the exposure block survives a read-modify-write round trip");

// ---- phase B: camera.postFx ----------------------------------------------
var post = camera.postFx(cam);
assert(post.overrides && Object.keys(post.overrides).length === 0,
       "a new camera overrides nothing");
assert(post.resolved && typeof post.resolved.bloom !== "undefined",
       "…but `resolved` still answers for every row (bloom = " + post.resolved.bloom + ")");
assert(typeof post.available.ssr !== "undefined",
       "…and `available` says whether the renderer serves each one (ssr = " +
       post.available.ssr + ")");

// THE TWO TABLES AGREE. The DOCUMENT decides which override slots exist; the
// WORLD row tables (services/worldmodes.h) decide what each is called, what it
// costs and whether the renderer serves it — and the panel and this verb are
// both generated from the join. If the two ever drift, a camera offers a row
// nothing can resolve, so the join is asserted in both directions here.
var worldParams = world.postFx();
var worldRows = world.settings();
var camKeys = Object.keys(post.resolved);
assert(camKeys.length > 0, "the camera declares " + camKeys.length + " override slots");
// THE JOIN IS THREE-WAY since POST_LOOKS (§4.1 / §7 R10). `looks` is a STACK
// key: deliberately NOT a World Mode row — looks are an art choice and a tier
// switch must never silently drop somebody's Old Movie — and not a ParamRow
// either, because it is an ordered array rather than a number. What answers for
// it on the world side is world.looks(), so that is what the join checks.
var worldLooks = world.looks();
for (var ck = 0; ck < camKeys.length; ck++) {
    var key = camKeys[ck];
    if (key === "looks") {
        // Length-tested rather than Object.prototype.toString'd: a QVariantList
        // crosses into QJSEngine as an array-LIKE value whose class tag is not
        // "Array", and asserting the tag tests the bridge rather than the verb.
        assert(worldLooks !== undefined && typeof worldLooks.length === "number",
               "override 'looks' is answered by world.looks() (a stack, not a row)");
        continue;
    }
    assert(typeof worldParams[key] !== "undefined" || typeof worldRows[key] !== "undefined",
           "override '" + key + "' is a real World row or parameter");
}
var wpKeys = Object.keys(worldParams);
for (var wk = 0; wk < wpKeys.length; wk++) {
    var wkey = wpKeys[wk];
    if (wkey.indexOf("exposure") === 0) continue;   // the camera's own §4 block
    assert(camKeys.indexOf(wkey) >= 0,
           "…and every World post PARAMETER except the exposure trio is overridable per camera "
           + "('" + wkey + "')");
}

// THE OWNER'S ROW: bloom, per camera, both ways round.
var worldBloom = post.resolved.bloom;
post = camera.postFx(cam, { bloom: true, bloomThreshold: 0.5 });
assert(post.overrides.bloom === 1 || post.overrides.bloom === true,
       "camera.postFx pins BLOOM on this camera");
near(post.resolved.bloomThreshold, 0.5, 1e-5,
     "…and its threshold resolves to the camera's value, not the world's");

post = camera.postFx(cam, { bloom: null });
assert(typeof post.overrides.bloom === "undefined",
       "a null value CLEARS an override — that is how 'inherit' is expressible");
assert(post.resolved.bloom === worldBloom,
       "…and the row goes straight back to resolving as the world's (" + worldBloom + ")");

// ---- phase C: the refusals that are the design ---------------------------
refuses(function () { camera.postFx(cam, { exposure: 1 }); },
        "exposure is NOT an override slot: a camera's exposure is its own block, in stops");
refuses(function () { camera.postFx(cam, { smaa: 2 }); },
        "an SMAA PRESET is not a per-camera value (a shader recompile would hitch on a cut)");
post = camera.postFx(cam, { smaa: -1 });
assert(post.overrides.smaa === -1,
       "…but switching SMAA OFF is compositor shape, and free, so a camera may");
refuses(function () { camera.postFx(cam, { blooom: true }); },
        "an unknown override key is a typo and is refused, never silently dropped");
refuses(function () { camera.postFx("not-a-guid", { bloom: true }); },
        "…and so is a guid that names no camera");

// A block that names one good key and one bad one must leave the camera ALONE.
var beforeBad = camera.postFx(cam).overrides;
refuses(function () { camera.postFx(cam, { ssao: true, nonsense: 1 }); },
        "a half-valid block is refused whole");
var afterBad = camera.postFx(cam).overrides;
assert(typeof afterBad.ssao === "undefined" &&
       Object.keys(afterBad).length === Object.keys(beforeBad).length,
       "…and nothing in it was applied");

// ---- phase D: clearPostOverride ------------------------------------------
camera.postFx(cam, { bloom: true, ssao: true, ssaoPower: 3 });
var cleared = camera.clearPostOverride(cam, "ssao");
assert(cleared.cleared.length === 1 && cleared.cleared[0] === "ssao",
       "clearPostOverride removes exactly the row it was given");
assert(typeof camera.postFx(cam).overrides.bloom !== "undefined",
       "…and leaves the others alone");
cleared = camera.clearPostOverride(cam);
assert(cleared.cleared.length >= 2,
       "clearPostOverride with no row puts the whole camera back on the world (" +
       cleared.cleared.join(", ") + ")");
assert(Object.keys(camera.postFx(cam).overrides).length === 0,
       "…leaving nothing overridden");
refuses(function () { camera.clearPostOverride(cam, "nonsense"); },
        "clearing a row that does not exist is a typo, and is refused");

// ---- phase E: the reflected half -----------------------------------------
node.setProperty(cam, "exposureMode", 2);      // Manual
node.setProperty(cam, "exposure", 1.25);
var refl = camera.settings(cam);
assert(refl.exposureMode === "manual" && Math.abs(refl.exposure - 1.25) < 1e-5,
       "node.setProperty reaches the exposure block (so it is keyframeable)");
// The OVERRIDES are deliberately NOT node properties: a property row always has
// a value, and an override is tri-state — a keyframe on one would have no way to
// say "inherit", and the first frame anything wrote it would silently pin the
// inherited value forever. camera.postFx is the door, and it is the only one.
refuses(function () { node.setProperty(cam, "postFx.bloomThreshold", 2.5); },
        "an override is NOT a plain node property (tri-state has no row shape)");
var props = node.properties(cam);
var names = [];
for (var pi = 0; pi < props.length; pi++) names.push(props[pi].name);
assert(names.indexOf("exposure") >= 0 && names.indexOf("exposureMode") >= 0,
       "…while the exposure block IS reflected, and therefore keyframeable");

// ---- phase F: save -> close -> open --------------------------------------
camera.settings(cam, { exposureMode: "auto", exposure: -0.75,
                       exposureMin: -4.25, exposureMax: 3.125 });
camera.postFx(cam, { bloom: true, bloomThreshold: 1.75, ssao: false, smaa: -1 });
var savedSettings = camera.settings(cam);
var savedPost = camera.postFx(cam).overrides;
assert(project.save(), "project.save()");
assert(project.close(), "project.close()");
assert(project.open(guid), "project.open() — the real reader");

var rows = scene.cameras();
var re = null;
for (var k = 0; k < rows.length; k++) if (rows[k].id === cam) re = rows[k];
assert(re !== null, "the camera came back");
var reSettings = camera.settings(cam);
assert(reSettings.exposureMode === "auto", "exposureMode survived the round trip");
near(reSettings.exposure, -0.75, 1e-5, "…exposure, in stops");
near(reSettings.exposureMin, -4.25, 1e-5, "…exposureMin");
near(reSettings.exposureMax, 3.125, 1e-5, "…exposureMax");
var rePost = camera.postFx(cam).overrides;
assert(Object.keys(rePost).length === Object.keys(savedPost).length,
       "…and every override came back (" + Object.keys(rePost).join(", ") + ")");
near(rePost.bloomThreshold, 1.75, 1e-5, "…including the continuous one");
assert(rePost.ssao === 0, "…and a row overridden to OFF, which is not the same as absent");
assert(rePost.smaa === -1, "…and the SMAA off switch");

// TOLERATED-ABSENT: a camera whose file never carried any of this loads as
// "inherit everything", which is exactly what every pre-phase file means.
var fresh = scene.addCamera({ position: { x: 3, y: 0, z: 0 } });
var fp = camera.settings(fresh);
assert(fp.exposureMode === "inherit" && fp.exposure === 0,
       "a camera with none of these keys inherits the world's exposure");
assert(Object.keys(camera.postFx(fresh).overrides).length === 0,
       "…and overrides nothing");

// ---- phase G: undo --------------------------------------------------------
var before = editor.undoState().pushes;
camera.postFx(fresh, { bloom: true, ssaoPower: 2 });
assert(editor.undoState().pushes - before >= 2,
       "every override row is one undoable command");
before = editor.undoState().pushes;
camera.clearPostOverride(fresh);
assert(editor.undoState().pushes - before >= 2, "…and so is every clear");
before = editor.undoState().pushes;
camera.postFx(fresh);
camera.settings(fresh);
assert(editor.undoState().pushes === before, "…while the READ verbs push nothing at all");

// ---- phase H: the panel ---------------------------------------------------
// SELECTING a camera builds the "Exposure & Post" section, which is generated
// from the same two tables this file just cross-checked. There is no way to
// assert its LOOK from a script, but there is every reason to assert that it
// builds — for a camera with overrides, for one without, and for a row the
// renderer does not serve — because a panel generated from a table is exactly
// the thing a table change breaks silently.
camera.postFx(cam, { bloom: true, bloomThreshold: 3, ssao: false, smaa: -1 });
assert(editor.select(cam), "editor.select(camera) — the camera panel builds with overrides");
assert(editor.select(fresh), "…and for a camera that overrides nothing");
camera.settings(fresh, { exposureMode: "auto" });
assert(editor.select(fresh), "…and with its own auto exposure (the window rows enable)");
camera.settings(fresh, { exposureMode: "manual" });
assert(editor.select(cam) && editor.select(fresh),
       "…and across a re-selection, which rebuilds it from scratch");
editor.select(null);

// ---- phase I: the ONE opt-in, in pixels ----------------------------------
// camera.screenshot({postFx:true}) is the single deliberate door through which
// a camera's grade reaches an OFFSCREEN render (everything else — thumbnails,
// previews, the pixel suites — is neutral by construction). Two stops down and
// two stops up through the same camera must produce two different pictures, and
// the neutral (postFx:false) shot must be unmoved by either.
world.override({ id: "hdr", value: 1 });     // the world must be graded at all
camera.settings(cam, { exposureMode: "manual", exposure: -2.5 });
var shotDark = camera.screenshot(cam, "camlens-exposure-dark.png",
                                 { width: 128, height: 128, postFx: true });
camera.settings(cam, { exposure: 2.5 });
var shotBright = camera.screenshot(cam, "camlens-exposure-bright.png",
                                   { width: 128, height: 128, postFx: true });
function lum(c) { return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b; }
assert(lum(shotBright.center) > lum(shotDark.center) + 8,
       "camera.screenshot({postFx:true}) grades with the CAMERA's exposure (" +
       lum(shotDark.center).toFixed(1) + " at -2.5 stops vs " +
       lum(shotBright.center).toFixed(1) + " at +2.5)");

camera.settings(cam, { exposure: -2.5 });
var neutralA = camera.screenshot(cam, "camlens-neutral-a.png", { width: 128, height: 128 });
camera.settings(cam, { exposure: 2.5 });
var neutralB = camera.screenshot(cam, "camlens-neutral-b.png", { width: 128, height: 128 });
assert(Math.abs(lum(neutralA.center) - lum(neutralB.center)) < 0.5,
       "…while a NEUTRAL readback is untouched by it, which is the determinism law (" +
       lum(neutralA.center).toFixed(2) + " vs " + lum(neutralB.center).toFixed(2) + ")");

// ---- phase J: the LOOKS stack override (POST_LOOKS_SPEC.md §4.1 / D4) -----
//
// The one WHOLE-STACK override in the key table, and the reason it is one: a
// looks stack is an ORDERED LIST, and a sparse per-parameter override over an
// ordered list stops meaning anything the moment the world reorders or removes
// an entry. So a camera REPLACES the world's stack or inherits it, and "no
// looks at all" is an override to the EMPTY array — a third state a boolean
// could not express.
var lookCat = world.lookCatalogue();
assert(lookCat.length >= 1, "the look catalogue is not empty (" + lookCat.length + ")");
var lookA = lookCat[0].id;
var lookB = lookCat.length > 1 ? lookCat[1].id : null;

world.addLook(lookA, { amount: 1 });
assert(world.looks().length === 1, "the WORLD has one look");

var post = camera.postFx(fresh);
assert(post.overrides.looks === undefined, "a fresh camera does not override looks");
assert(post.resolved.looks.length === 1,
       "…and RESOLVES to the world's stack (" + post.resolved.looks.length + ")");

// An EMPTY override is a real one: "this camera has no looks", over a world
// that has some.
post = camera.postFx(fresh, { looks: [] });
assert(post.overrides.looks !== undefined && post.overrides.looks.length === 0,
       "an EMPTY looks array is an override, not an absence");
assert(post.resolved.looks.length === 0, "…and resolves to no looks at all");

// A camera's own stack replaces the world's.
var wanted = [{ id: lookA, params: { amount: 0.25 } }];
if (lookB) wanted.push({ id: lookB });
post = camera.postFx(fresh, { looks: wanted });
assert(post.overrides.looks.length === wanted.length,
       "a camera's own stack is stored whole (" + post.overrides.looks.length + " entries)");
assert(post.overrides.looks[0].id === lookA, "…in the order it was given");
assert(Math.abs(post.overrides.looks[0].params.amount - 0.25) < 1e-6,
       "…with its parameters");

// The SAME rules as the world's stack, enforced by the SAME validator: unknown
// ids and duplicates are dropped, parameters clamped.
post = camera.postFx(fresh, { looks: [{ id: "no-such-look" },
                                      { id: lookA }, { id: lookA }] });
assert(post.overrides.looks.length === 1,
       "an unknown look and a duplicate are dropped (" + post.overrides.looks.length + " left)");

// The reflected (keyframeable) half reaches the same field.
var reflected = node.property(fresh, "postFx.looks");
assert(reflected && reflected.length === 1, "node.property reads the stack back");
node.setProperty(fresh, "postFx.looks", []);
assert(camera.postFx(fresh).overrides.looks.length === 0,
       "node.setProperty writes it too");

// SAVE -> CLOSE -> OPEN, through the real writer and the real reader.
camera.postFx(fresh, { looks: [{ id: lookA, params: { amount: 0.75 } }] });
project.save();
project.close();
project.open(guid);
post = camera.postFx(fresh);
assert(post.overrides.looks.length === 1, "the camera's stack survives save -> close -> open");
assert(Math.abs(post.overrides.looks[0].params.amount - 0.75) < 1e-6, "…with its parameter");

// …and null puts it back on the world.
post = camera.postFx(fresh, { looks: null });
assert(post.overrides.looks === undefined, "null clears the override back to inherit");
assert(post.resolved.looks.length === world.looks().length,
       "…and the camera resolves to the world's stack again");

// The panel builds for a camera with a stack override, which is the row that
// opens a whole sub-editor and is therefore the one most likely to break
// silently when the catalogue changes.
camera.postFx(fresh, { looks: [{ id: lookA }] });
assert(editor.select(fresh), "the camera panel builds with a looks override");
camera.postFx(fresh, { looks: [] });
assert(editor.select(fresh), "…and with an EMPTY looks override");
camera.postFx(fresh, { looks: null });
assert(editor.select(fresh), "…and back on inherit");
editor.select(null);
world.removeLook(lookA);

console.log("\nALL CAMERA POST-FX E2E CHECKS PASSED");
