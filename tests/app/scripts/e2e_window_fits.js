// app.window_fits — a FRESH PROFILE'S WINDOW FITS ON THE SCREEN (hygiene lane,
// 2026-09-09).
//
// mainwindow.ui authors the window at 1612x1530. restoreGeometry() on an empty
// blob is a no-op that returns false, so before MainWindow::fitToScreen every
// first run — and every hermetic JAHSHAKA_DATA_ROOT session, which is now most
// of the suite — opened a window taller than a 1080p desktop. With a window
// manager that is rude; on the WM-less Xvfb the rig uses, nothing clamps
// anything ever, so the bottom of the window (the Materials palette lives
// there) was simply off the screen. app.input_keys's palette drag aimed at a
// per-mille point computed from the window height and hit the tab bar instead.
//
// Runs --headless (offscreen QPA, no engine, no display): the offscreen screen
// is smaller than the authored size, which is exactly the condition under test.
// No settings are restored here — JAHSHAKA_DATA_ROOT points at an empty tree —
// so this is the first-run path by construction.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

var w = app.window();
assert(w && typeof w.width === "number", "app.window() reports the window: " +
       JSON.stringify(w));
assert(w.screen && w.screen.availWidth > 0 && w.screen.availHeight > 0,
       "the window knows its screen: " + JSON.stringify(w.screen));

// THE ASSERTION, with the one floor Qt will not go below: a widget cannot be
// resized under its layout's minimum size hint, so on a screen narrower than
// that the honest bound is the minimum, not the screen. (This window's minimum
// width is ~1412 — reported upward as its own finding; it is not something a
// geometry clamp can fix.)
var maxW = Math.max(w.screen.availWidth, w.minWidth);
var maxH = Math.max(w.screen.availHeight, w.minHeight);
assert(w.width <= maxW, "window width " + w.width + " is within " + maxW +
       " (screen " + w.screen.availWidth + ", minimum " + w.minWidth + ")");
assert(w.height <= maxH, "window height " + w.height + " is within " + maxH +
       " (screen " + w.screen.availHeight + ", minimum " + w.minHeight + ")");

// ...and the clamp is a MINIMUM, not a rewrite: a screen with room keeps the
// authored size. Only assert the direction the clamp can be wrong in.
if (w.screen.availWidth >= 1612 && w.screen.availHeight >= 1530) {
    assert(w.width === 1612 && w.height === 1530,
           "a big enough screen keeps the authored 1612x1530");
} else {
    // The regression this suite exists for: unclamped, the height stayed at the
    // .ui's 1530 no matter how small the screen was.
    assert(w.height < 1530,
           "a smaller screen actually shrank the window (" + w.width + "x" + w.height + ")");
    assert(w.height <= Math.max(w.screen.availHeight, w.minHeight),
           "and shrank it to the screen, not to something arbitrary");
}

// The window must also START on the screen: a clamped size at a negative or
// off-screen origin is still unreachable with no window manager to move it.
assert(w.x >= 0 && w.y >= 0, "the window starts inside the screen at " + w.x + "," + w.y);
console.log("window_fits: all assertions passed");
