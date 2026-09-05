// perf.offscreen_isolation — an offscreen readback must not redraw the editor
// (fps audit F5; src/bridge/offscreenrenderscope.h).
//
// THE DEFECT THIS PINS. Engine::renderOneFrame() draws EVERY enabled View, and
// every readback in the program calls it twice. So each thumbnail, screenshot
// and preview snapshot also rendered the whole editor scene twice AND presented
// it twice — two blocking waits on the display, per readback, for frames nobody
// ever saw. A thumbnail sweep through a library therefore paced the editor at a
// fraction of its normal rate.
//
// THE MEASUREMENT is exact and needs no timing: EngineSceneViewport counts the
// frames its on-screen View has actually PRESENTED since its scene was bound
// (editor.viewportState().framesPresented). Before the fix a screenshot moved
// that counter by 2. After it, by 0 — the view is disabled for the duration and
// restored afterwards, which the last phase checks by making the counter move
// again.
//
// Engine up (no --headless): the whole point is an on-screen view existing.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

var name = "Offscreen Isolation " + Date.now();
var guid = project.create(name);
assert(guid.length > 10, "project.create -> " + guid);

var st = editor.viewportState();
if (st.state === "offscreen") {
    // No on-screen render target in this session (a document-only stand-in).
    // There is nothing to isolate, and asserting "0 === 0" would be a test that
    // passes for the wrong reason — say so instead.
    console.log("viewport is offscreen in this session — nothing to isolate, skipping");
} else {
    editor.frame(4);
    var before = editor.viewportState().framesPresented;
    // THE GUARD that keeps this suite honest: if the editor is not presenting
    // at all, "it did not present during the readback" proves nothing.
    assert(before > 0, "the editor viewport is presenting (" + before + " frames)");

    var shot = editor.screenshot("offscreen-isolation.png", 96, 96);
    assert(shot && shot.width === 96, "editor.screenshot rendered offscreen");
    var during = editor.viewportState().framesPresented;
    assert(during === before,
           "a screenshot readback presented NO editor frames (" + before + " -> " + during +
           "; it was +2 per readback before the fix)");

    // The scope RESTORES: the very next driver-equivalent frames must present
    // again, or the fix would have left the viewport dead after the first
    // screenshot.
    editor.frame(3);
    var after = editor.viewportState().framesPresented;
    assert(after > during, "the viewport presents again afterwards (" + during + " -> " + after + ")");

    // And it holds for a burst, which is what a thumbnail queue actually does.
    var burstStart = editor.viewportState().framesPresented;
    for (var i = 0; i < 5; ++i) editor.screenshot("offscreen-isolation.png", 64, 64);
    assert(editor.viewportState().framesPresented === burstStart,
           "five readbacks in a row still present nothing (a thumbnail sweep)");
}
