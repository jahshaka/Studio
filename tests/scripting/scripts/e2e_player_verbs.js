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
assert(player.stop(), "stop");

console.log("e2e_player_verbs: ALL OK");
