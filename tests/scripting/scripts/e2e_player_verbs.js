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
