// scripting.e2e.player_verbs — the PLAYER space through its verbs
// (verb-coverage audit F1).
//
// The Player page had no verb surface at all before this: one play button, a
// human hand, and nothing else could reach the space the product ships as its
// runtime. So this suite is the first thing that has ever driven it from
// outside, and it asserts the two claims that matter:
//
//   1. player.* is a DIFFERENT state machine from editor.*. editor.play() is
//      play-in-place inside the editor viewport; this is the second engine
//      Scene, with its own mirror, its own PlayBack and the document's scene
//      camera. Both can be running, and reporting either through the other's
//      verb would be a lie.
//   2. player.screenshot renders THAT scene, through THAT camera, in a
//      throwaway offscreen view — the mechanism camera.screenshot uses. A shot
//      taken through the editor viewport would photograph the editor's world
//      state and call it the player, which is exactly the confusion every "it
//      looks different in the player" report is made of.
//
// NOT --headless, deliberately: a headless run has no player backend at all,
// and `available` is the honest report of that.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

project.create("player verbs " + Date.now());

// ---- state ----------------------------------------------------------------

var st = player.state();
console.log("player.state: " + JSON.stringify(st));
assert(st.available === true, "a windowed run HAS a player backend");
assert(st.playing === false, "nothing is running at rest");
assert(typeof st.active === "boolean", "`active` reports whether the page is on screen");
assert(player.playing() === false, "player.playing() agrees");

// ---- play / stop / restart, and independence from editor.play -------------

assert(player.play() === true, "player.play()");
assert(player.playing() === true, "the player's scene is running");
assert(player.state().playing === true, "...and state() says so too");
assert(editor.playing() === false,
       "editor.playing() is UNTOUCHED — the two spaces are independent state machines");

assert(player.play() === true, "player.play() is idempotent while already running");
assert(player.restart() === true, "player.restart()");
assert(player.playing() === true, "restart leaves it running");

assert(player.stop() === true, "player.stop()");
assert(player.playing() === false, "stopped");
assert(player.stop() === true, "player.stop() is safe when already stopped");

// The other direction: editor play-in-place must not start the player.
assert(editor.play(), "editor.play() — play IN PLACE");
assert(editor.playing() === true, "the editor viewport is running");
assert(player.playing() === false, "...and the PLAYER is still stopped");
assert(editor.stop(), "editor.stop()");

// ---- frame ----------------------------------------------------------------
//
// A --script run never SHOWS the Player page, so the player's on-screen view
// has never been created. player.frame refuses rather than answering true and
// drawing nothing — the honest half of the pair whose other half
// (player.screenshot) works anyway, because a readback needs no window.

var noView = false;
try { player.frame(2, 0.016); } catch (e) { noView = true; console.log("refusal: " + e.message); }
assert(noView, "player.frame refuses while the Player page has never been shown");

// ---- screenshot -----------------------------------------------------------
//
// Works before the Player page has ever been shown: the player's engine Scene
// and its mirror need no window, only the on-screen View does.

var shot = player.screenshot("player.png",
                             { width: 160, height: 120,
                               probes: [{ x: 0.5, y: 0.5 }, { x: 0.5, y: 0.9 }] });
console.log("player.screenshot: " + JSON.stringify(shot));
assert(shot.width === 160 && shot.height === 120, "the shot came back at the requested size");
assert(shot.path.length > 0, "...and was written to disk");
assert(shot.center && typeof shot.center.r === "number", "the centre pixel is reported");
assert(shot.probes.length === 2, "both probes came back");
// The default scene is a lit ground plane under a flat sky: neither probe may
// be pure black, which is what an unbound scene or an unapplied camera gives.
assert(shot.center.r + shot.center.g + shot.center.b > 30,
       "the player rendered actual pixels, not a black frame ("
       + shot.center.r + "," + shot.center.g + "," + shot.center.b + ")");

// ---- the project's HIDE FLOOR IN PLAYER setting (PLAYER-FLOOR-1) ----------
//
// Owner, 2026-09-18 (VR_INPUT_SPEC §16 row 6): "hide the floor in the Player"
// is a PROJECT SETTING. A scene that stands on its own level still wants the
// editor's checkered default floor while it is being built — it is where the
// grid is legible and what a dropped object lands on — and does not want it in
// the finished thing.
//
// THE MEASUREMENT, not a flag read: the player's shot is probed low in the
// frame, where the default floor is, and high, where the sky is. With the
// setting on the low probe must CHANGE (the floor is gone from the picture) and
// the EDITOR's own shot must not move by one byte — the setting is about the
// Player, and the mirror that draws both views is one object.
// y 0.35 is ON the floor in this pose and y 0.05 is sky above the horizon
// (measured row by row: with the floor the column reads 23, 40, 41, 38, 40,
// 40, 27, 38, 27, 26 from top to bottom; with it hidden it is a uniform 24).
var floorProbes = [{ x: 0.5, y: 0.35 }, { x: 0.5, y: 0.05 }];
// THE PLAYER'S OWN PICTURE for the player probes, and the PLAIN readback for
// the editor's. Two different grades on purpose: at the plain grade the default
// floor's linear radiance and the flat sky's sit within 2/255 of each other in
// this scene (measured — the film curve is what separates them into 40 and 24),
// so the plain grade cannot see the difference this case is about; while for
// the editor's "did not move by one byte" half the plain grade is the only
// honest instrument (the scene grade carries a measured exposure).
function floorShot(tag) {
    return player.screenshot("floor-" + tag + ".png",
                             { width: 160, height: 120, probes: floorProbes,
                               grade: "scene" });
}
function editorFloorShot(tag) {
    return editor.screenshot("floor-editor-" + tag + ".png", 160, 120, floorProbes);
}
function probeDelta(a, b, i) {
    return Math.max(Math.abs(a.probes[i].r - b.probes[i].r),
                    Math.abs(a.probes[i].g - b.probes[i].g),
                    Math.abs(a.probes[i].b - b.probes[i].b));
}
assert(player.stop(), "stopped for the floor case");
var floorOffPlayer = floorShot("off");
var floorOffEditor = editorFloorShot("off");
var settingsOff = world.settings();
assert(settingsOff.playerHidesFloor !== undefined,
       "the World registry carries the playerHidesFloor row");
assert(!settingsOff.playerHidesFloor.value,
       "...and it is OFF by default (a new project plays on its floor)");
assert(world.override({ id: "playerHidesFloor", value: true }) !== false,
       "world.override turns it on");
var onRow = world.settings().playerHidesFloor;
assert(onRow.value === true || onRow.value === 1,
       "...and the row reads back on (" + JSON.stringify(onRow.value) + ")");
var floorOnPlayer = floorShot("on");
var floorOnEditor = editorFloorShot("on");
console.log("player floor probe: off " + JSON.stringify(floorOffPlayer.probes[0]) +
            " -> on " + JSON.stringify(floorOnPlayer.probes[0]));
assert(probeDelta(floorOffPlayer, floorOnPlayer, 0) > 6,
       "the PLAYER's floor pixels change when the project hides the floor (delta " +
       probeDelta(floorOffPlayer, floorOnPlayer, 0) + ")");
assert(probeDelta(floorOffEditor, floorOnEditor, 0) === 0 &&
       probeDelta(floorOffEditor, floorOnEditor, 1) === 0,
       "...and the EDITOR's own picture does not move by one byte");
// Back off, and the player's picture comes back exactly — a hide is not a
// delete: the floor node, its material and its physics are untouched.
assert(world.override({ id: "playerHidesFloor", value: false }) !== false, "and off again");
var floorBack = floorShot("back");
assert(probeDelta(floorOffPlayer, floorBack, 0) === 0,
       "turning it off gives the Player its floor back, byte for byte");

var badOption = false;
try { player.screenshot("x.png", { zoom: 2 }); } catch (e) { badOption = true; }
assert(badOption, "an unknown screenshot option is REFUSED, not ignored");

var noPath = false;
try { player.screenshot(""); } catch (e) { noPath = true; }
assert(noPath, "a screenshot with no path is refused");

// A shot WHILE PLAYING is the interesting one — the player's own scene is the
// one being stepped, so this is what an assistant would look at.
assert(player.play(), "play again for the running shot");
var running = player.screenshot("player-running.png", { width: 64, height: 64, postFx: true });
assert(running.width === 64, "a postFx shot of the RUNNING player came back");

// EVERY GRADE THE VERB DOCUMENTS IS A GRADE THE VERB TAKES (SS1, lead review
// item 1). The first cut of that lane shipped the player's parser accepting
// only raw|tonemap|viewport while its own doc — and docs/SCRIPTING.md —
// promised "plain" and "scene", so a script written from the shipped
// documentation got "unknown grade", and the player's whole Scene branch was
// unreachable code. One call per spelling; "scene" is the player's own picture
// (the whole post chain at the player view's converged exposure).
["plain", "raw", "tonemap", "scene", "viewport"].forEach(function (g) {
    var shot = player.screenshot("player-grade-" + g + ".png",
                                 { width: 64, height: 64, grade: g });
    assert(shot.width === 64 && shot.height === 64,
           "player.screenshot takes the grade its documentation promises: \"" + g + "\"");
});
var badGrade = false;
try { player.screenshot("player-bad.png", { grade: "sepia" }); } catch (e) {
    badGrade = true;
    assert(e.message.indexOf("scene") > 0,
           "and the refusal LISTS the grades it takes: " + e.message);
}
assert(badGrade, "an unknown player grade is refused, catchably");

assert(player.stop(), "stop");

// ---- WHERE THE PLAYER SPAWNS (PLAYER-SPAWN-1, owner 2026-09-17) -----------
//
// "The player spawned the camera in the middle of the sky — it should share the
// editor viewpoint when we switch unless a camera node is present."  Three
// rules, and this is the half of them a script can see:
//
//   (a) with NO camera in the scene, the Player starts at the editor
//       viewport's exact pose and lens;
//   (b) the FIRST camera added becomes the active one, and the Player renders
//       through it;
//   (c) a SECOND never steals it;
//   (d) deleting the active one clears the choice, and the next camera ADDED
//       arms itself again.
//
// player.state().camera is the observable: `source` says which of the two the
// run is looking through, and the pose is WORLD.

function nearly(a, b, eps, msg) {
    var d = Math.abs(a - b);
    assert(d <= (eps || 1e-3), msg + " (" + a + " vs " + b + ")");
}

// A DISTINCTIVE EDITOR VIEWPOINT — nowhere near the default (0, 5, 14) and on a
// different lens, so "it matched" cannot mean "they were both at the default".
editor.setCamera({ position: { x: 8, y: 3, z: -12 }, lookAt: { x: 0, y: 0, z: 0 }, fov: 37 });
editor.frame(2);
var eye = editor.camera();
console.log("the editor viewpoint: " + JSON.stringify(eye.position) + " fov " + eye.fov);

// THE PAGE SWITCH IS THE GESTURE the owner described ("when we switch"), and
// showing the Player page starts its scene by design (audit F4).
app.space("player");
assert(app.columns().space === "player", "the Player page is up");
player.frame(2);

var pc = player.state().camera;
console.log("the player's camera: " + JSON.stringify(pc));
assert(pc.source === "viewport",
       "(a) with no camera in the scene the Player flies the free viewer");
nearly(pc.position.x, eye.position.x, 1e-3, "(a) the Player starts at the editor's x");
nearly(pc.position.y, eye.position.y, 1e-3, "…y");
nearly(pc.position.z, eye.position.z, 1e-3, "…z");
var dot = pc.rotation.x * eye.rotation.x + pc.rotation.y * eye.rotation.y +
          pc.rotation.z * eye.rotation.z + pc.rotation.scalar * eye.rotation.scalar;
assert(Math.abs(dot) > 0.9999, "(a) …facing the way the editor faced (|dot| " + dot + ")");
nearly(pc.fov, eye.fov, 1e-3, "(a) …through the lens the editor view is framing with");

// (b) A CAMERA NODE IS PRESENT. The first one arms itself, and the run renders
// through it — "unless a camera node is present", in pixels' terms.
var shotA = scene.addCamera({ position: { x: -4, y: 2, z: 6 }, name: "Shot A" });
assert(scene.activeCamera() === shotA, "(b) the first camera added is the active camera");
player.frame(2);
pc = player.state().camera;
assert(pc.source === "active" && pc.id === shotA,
       "(b) the Player renders through it, not through the editor's viewpoint");
nearly(pc.position.x, -4, 1e-2, "(b) …from where that camera stands");
nearly(pc.position.z, 6, 1e-2, "…z");

// (c) a second camera changes nothing.
var shotB = scene.addCamera({ position: { x: 9, y: 9, z: 9 }, name: "Shot B" });
assert(scene.activeCamera() === shotA, "(c) a second camera does not steal the shot");
player.frame(2);
assert(player.state().camera.id === shotA, "(c) …and the Player is still on the first");

// (d) delete the active one: nobody is promoted, and the next ADD arms itself.
assert(node.remove(shotA), "(d) remove the active camera");
player.frame(2);
pc = player.state().camera;
assert(pc.source === "viewport",
       "(d) with nothing armed the Player is back on the free viewer — the OTHER camera is " +
       "not promoted, because nobody chose it");
var shotC = scene.addCamera({ position: { x: 0, y: 1, z: -8 }, name: "Shot C" });
assert(scene.activeCamera() === shotC, "(d) the next camera ADDED arms itself");
player.frame(2);
assert(player.state().camera.id === shotC, "(d) …and the Player renders through it");

assert(node.remove(shotB) && node.remove(shotC), "tidy the cameras away");
app.space("editor");
assert(player.stop(), "stop the player again");

console.log("e2e_player_verbs: ALL OK");
