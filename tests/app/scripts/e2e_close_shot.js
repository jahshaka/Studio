// app.close_shot — A PROJECT'S DESKTOP TILE IS THE FRAME THE EDITOR PRESENTS
// (CLOSE-SHOT-2, DEVELOPMENT_PLAN row 9ao).
//
// The tile used to be a SECOND render: an offscreen shot view at the tile's
// size that paid its own GI settle (1.7-3.7 s per close) and, on a save, parked
// the scene's GI at OFF so the tile did not even show the lit world. It is now
// the on-screen view's own next frame, drawn once without the editor's
// furniture and read back from the window before the present
// (View::requestFrameCapture). The claims, in pixels:
//
//   1. NOTHING ON SCREEN CHANGES, AND THE CAPTURE HAS NO FURNITURE: the frame
//      PRESENTED on the capturing frame is pixel-identical to the frames before
//      and after it (gizmo, outline and grid all visible), while the clean
//      picture of that same frame differs from it on a real number of pixels.
//   2. project.save's TILE IS THE CLEAN PRESENTED FRAME: it matches a clean
//      capture of the same still scene (cropped and scaled the same way) within
//      a small tolerance, and differs from the frame as shown wherever the
//      furniture is.
//   3. THE CLOSE WRITES THE TILE TOO: an edit, then project.close() (whose
//      autosave asks for the frame, satisfied before the world is torn down),
//      and the stored tile shows the edit.
//
// TOLERANCES (stated, measured on the rig — see the numbers each line prints):
// two consecutive clean frames of a still scene are not bit-identical (the
// view's temporal history keeps refining), so "matches" is "at most 0.5 % of the
// tile's pixels more than 12 codes apart"; the furniture is hundreds of pixels
// of grid, outline and gizmo, so "carries furniture" is at least 2,000 pixels of
// the full frame more than 12 codes apart.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function J(x) { return JSON.stringify(x); }
var TOL = 12;

var guid = project.create("close-shot");
assert(guid && guid.length > 10, "project.create");
var cube = scene.addPrimitive("cube", { position: [0, 1, 0] });
assert(cube, "a cube in front of the editor camera");
editor.frame(60, 1 / 60);
editor.select(cube);
editor.frame(4, 1 / 60);

// ---- 1. the presented frame never changes; the capture drops the furniture ---
var before = editor.presentedFrame("cs_before.png", { helpers: true });
var clean = editor.presentedFrame("cs_clean.png", { presented: "cs_capture_shown.png" });
var after = editor.presentedFrame("cs_after.png", { helpers: true });
console.log("before " + J(before) + "  clean " + J(clean) + "  after " + J(after));
assert(before.width > 0 && before.width === clean.width && before.height === clean.height,
       "the captures are the full window (" + before.width + "x" + before.height + ")");
var sameBefore = app.compareImages(clean.presented, before.path, 0);
var sameAfter = app.compareImages(clean.presented, after.path, 0);
console.log("capture frame as presented vs before " + J(sameBefore) + "  vs after " + J(sameAfter));
assert(sameBefore.over === 0 && sameAfter.over === 0,
       "the PRESENTED frame of the capturing frame is pixel-identical to the frames either side " +
       "(" + sameBefore.over + " / " + sameAfter.over + " px differ)");
var furniture = app.compareImages(clean.presented, clean.path, TOL);
console.log("presented vs clean (one frame): " + J(furniture));
assert(furniture.over >= 2000,
       "the presented frame carries the furniture and the capture of the same frame drops it (" +
       furniture.over + " px)");
var clean2 = editor.presentedFrame("cs_clean2.png");
var still = app.compareImages(clean.path, clean2.path, TOL);
console.log("clean vs clean (the next frame): " + J(still));
assert(still.overFraction <= 0.005,
       "two clean captures of the still scene agree (" + still.over + " px over " + TOL + ")");

// ---- 2. project.save's tile -------------------------------------------------
assert(project.save() === true, "project.save");
var tile = project.thumbnail("cs_tile.png");
console.log("tile " + J(tile));
assert(!tile.empty && tile.width === 920 && tile.height === 430,
       "the stored tile is the tile size (" + tile.width + "x" + tile.height + ")");
var cleanTile = editor.presentedFrame("cs_clean_tile.png", { tile: true });
var shownTile = editor.presentedFrame("cs_shown_tile.png", { tile: true, helpers: true });
var vsClean = app.compareImages(tile.path, cleanTile.path, TOL);
var vsShown = app.compareImages(tile.path, shownTile.path, TOL);
var furnitureInTile = app.compareImages(shownTile.path, cleanTile.path, TOL);
console.log("tile vs clean " + J(vsClean) + "  tile vs shown " + J(vsShown) +
            "  shown vs clean (tile) " + J(furnitureInTile));
assert(vsClean.overFraction <= 0.005,
       "the tile IS the clean presented frame (" + vsClean.over + " px over " + TOL + ", mean " +
       vsClean.meanDelta.toFixed(2) + ")");
assert(furnitureInTile.over >= 200 && vsShown.over >= furnitureInTile.over / 2,
       "the tile has no gizmo, outline or grid: it differs from the frame as shown where they are (" +
       vsShown.over + " px; the furniture covers " + furnitureInTile.over + ")");

// ---- 3. the close writes the tile -------------------------------------------
node.setProperty(cube, "position", [3, 1, 0]);
editor.frame(30, 1 / 60);
var moved = editor.presentedFrame("cs_moved_tile.png", { tile: true });
assert(project.close() === true, "project.close (its autosave asks for the frame)");
var closed = project.thumbnail("cs_closed_tile.png", guid);
console.log("closed tile " + J(closed));
assert(!closed.empty, "the close stored a tile");
var vsMoved = app.compareImages(closed.path, moved.path, TOL);
var vsSaved = app.compareImages(closed.path, tile.path, TOL);
console.log("closed vs moved " + J(vsMoved) + "  closed vs saved " + J(vsSaved));
assert(vsMoved.overFraction <= 0.005 && vsSaved.over > vsMoved.over * 4,
       "the close's tile shows the edit (" + vsMoved.over + " px from the moved frame, " +
       vsSaved.over + " from the earlier save)");
