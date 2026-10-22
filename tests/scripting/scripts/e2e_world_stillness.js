// scripting.e2e.world_stillness — THE STILLNESS DOOR THROUGH THE SCRIPTING HOST (PHOTON-2 stage 0, API-first: the verb
// a Bistro measurement drives). On a new project's default scene: a 0.5 m walk, the stop, a short window and the path
// check. The ASSERTIONS are the door's SHAPE and its own invariants, never a picture: the verb answers ok with one
// STILLNESS line per window plus the path line, the TSV exists, the window frames are the ones asked for, the class
// split covers the rectangles, the walk window's mask is the gather's own when one runs, and a still view HOLDS inside
// each window, the settle after its last input write (PHOTON-2 stage 2's round, H1 and K6; below).

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

var guid = project.create(project.nextFreeName("world-stillness"), { template: "basic" });
assert(guid && guid.length > 10, "project.create -> " + guid);
editor.selectNone();

var r = world.stillness({
    script: "teleport:0,2,8,0,1,0/hold:20/walk:0.5,2,8,0.5,1,0:10",
    frames: 120,
    path: true,
    classes: [ { name: "upper", x: 0, y: 0, w: 1, h: 0.5 }, { name: "lower", x: 0, y: 0.5, w: 1, h: 0.5 } ],
    out: "e2e-world-stillness"
});
assert(r && r.ok === true, "world.stillness answered ok");
for (var i = 0; i < r.summary.length; ++i) console.log(r.summary[i]);
var lines = { walk: 0, teleport: 0, path: 0 };
for (var k = 0; k < r.summary.length; ++k) {
    var w = r.summary[k].split(" ")[1];
    if (lines.hasOwnProperty(w)) ++lines[w];
}
assert(lines.walk === 1 && lines.teleport === 1 && lines.path === 1,
       "a STILLNESS line per window and the path line: " + JSON.stringify(lines));
assert(r.windows.length === 2 && r.windows[0].name === "walk" && r.windows[1].name === "teleport",
       "two windows, walk then teleport");
assert(r.windows[0].frames === 120 && r.windows[1].frames === 120, "each window is the 120 frames asked for");
var px = r.windows[1].pixels;
assert(px.all > 0 && px.upper > 0 && px.lower > 0 && px.upper + px.lower === px.all,
       "the teleport window counts every pixel, split by the two rectangles: " + JSON.stringify(px));
var st = world.giStatus();
if (st.gather && st.gather.running === true)
    assert(r.windows[0].visibleFromGather === true, "the walk window's mask is the gather's own history count");
// THE HOLD (PHOTON-2 stage 2's round, H1; K6; round 2's L7): the gather SETTLES while its inputs are written - every
// write, the ones that file no GI row included (a ray-level refit, a shadow scan, a light write) - or owe work, and
// HOLDS the settle after the last such frame (the door's lastHoldKeyFrame): at most that frame (the stop, frame 0, when
// none came after it) + the settle N + 1, and never before it nor before the last GI row. A window long enough to
// hold (120 frames) that never holds FAILS - it is never skipped.
for (var w = 0; w < 2; ++w) {
    var win = r.windows[w];
    var lastWrite = Math.max(0, win.lastHoldKeyFrame);
    assert(win.settleFrames > 0, win.name + ": the gather's settle N is read (" + win.settleFrames + ")");
    assert(win.holdFrame >= 0, win.name + ": the view holds inside its " + win.frames + " frames (last GI row " +
           win.lastGiRowFrame + ", last hold key " + win.lastHoldKeyFrame + ")");
    assert(win.holdFrame > win.lastGiRowFrame && win.holdFrame > win.lastHoldKeyFrame &&
           win.holdFrame <= lastWrite + win.settleFrames + 1,
           win.name + ": the hold is the settle after the last write (" + win.holdFrame + ", last hold key " +
           win.lastHoldKeyFrame + ", last row " + win.lastGiRowFrame + ", settle " + win.settleFrames + ")");
}
assert(typeof r.tsv === "string" && r.tsv.length > 0, "the TSV path: " + r.tsv);
assert(r.path.directEqual.length >= 1, "the path check read the cascades: " + JSON.stringify(r.path));
console.log("world.stillness e2e: PASS");
