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
    // (the walk's window 120 frames; the TELEPORT's long enough to OBSERVE its hold: its law is the derived bound below,
    // never the window's length - the 120 frames it had held at its very edge until F3's decision moved the hold to 141)
    frames: { walk: 120, teleport: 240 },
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
assert(r.windows[0].frames === 120 && r.windows[1].frames === 240, "each window is the frames asked for (120, 240)");
var px = r.windows[1].pixels;
assert(px.all > 0 && px.upper > 0 && px.lower > 0 && px.upper + px.lower === px.all,
       "the teleport window counts every pixel, split by the two rectangles: " + JSON.stringify(px));
var st = world.giStatus();
if (st.gather && st.gather.running === true)
    assert(r.windows[0].visibleFromGather === true, "the walk window's mask is the gather's own history count");
// THE HOLD (PHOTON-2 stage 2's round, H1; K6; round 2's L7): the GATHER SETTLES while its inputs are written - every
// write, the ones that file no GI row included (a ray-level refit, a shadow scan, a light write) - or owe work, and
// HOLDS the settle after the last such frame (the door's lastHoldKeyFrame): at most that frame (the stop, frame 0, when
// none came after it) + the settle N + 1, and never before it nor before the last GI row - its OWN hold
// (gatherHoldFrame: since PHOTON-7 stage 8B (B1) the start of the gather's LAST hold run - it rests per cell, and an input
// reaching a cell after a hold ends that hold; the hold that counts lasts to the window's end; the hold key, an input
// that reached a cell). THE VIEW HOLDS WHEN EVERY READER HOLDS (PHOTON-7 stage 10, the contract): its hold (holdFrame) is
// at or after the gather's and, where the reflection traces, the reflection's own (reflHoldFrame; its length is the
// reflection's rest rule, stage 11's to bound). A window that never holds FAILS.
// THE HOLD's DERIVED BOUND (PHOTON-7 stage 10-3, the lead's ruling; the door's one formula, stillnessDeriveLiveness -
// lab.stillness_control's): where the stop's one rest decision falls in the window, (2) the view holds AFTER the
// decision and its work's last GI row, (3) its first hold comes within holdBound = (N + due) + due + W + C + N + H + 1 frames of
// the readers' first rest (C, since PHOTON-7 8B B1: the cells' measured convergence after the work, convergeFrames; H, since
// 8B B3: the longest card-refinement hand-over after the work, handoverFrames), and W,
// the decision's work, keeps its pace (W <= workPace). The bound is derived from the
// window's own counts; the window's length is only how long it is observed.
// RED-CAPABLE (two arms, measured 2026-10-26, one run each): (a) EARLY - the readers' hold term OFF (the gather's
// in.holdOwed false): the gather held at frame 76 of the walk window, before the decision's work and its last write
// (101): red on "the gather's hold is the settle after the last write" (the view's hold still waited for the reflection,
// 115). (b) LATE - the hold DELAYED (the gather's hold owed 17 frames past the cards' last owed frame): the walk's first
// hold 58 frames after the readers' first rest against the bound 55: red on (3) LIVENESS (a 40-frame delay pushed the
// walk's hold past its window: red on "the hold inside its frames"). THE HOLD LANDS EXACTLY AT ITS BOUND here (55 of
// 55 in both windows) only because W, the decision's work, is bounded by its own pace law (W 4 frames against a pace of
// 11): the bound is the design's arithmetic with W as measured, not a number fitted to this scene.
for (var w = 0; w < 2; ++w) {
    var win = r.windows[w];
    var lastWrite = Math.max(0, win.lastHoldKeyFrame);
    assert(win.settleFrames > 0, win.name + ": the gather's settle N is read (" + win.settleFrames + ")");
    assert(win.gatherHoldFrame >= 0 && win.holdFrame >= 0, win.name + ": the gather and the view hold inside its " +
           win.frames + " frames (the gather " + win.gatherHoldFrame + ", the reflection " + win.reflHoldFrame +
           ", the view " + win.holdFrame + "; last GI row " + win.lastGiRowFrame + ", last hold key " +
           win.lastHoldKeyFrame + ")");
    assert(win.holdFrame >= win.gatherHoldFrame && (win.reflHoldFrame < 0 || win.holdFrame >= win.reflHoldFrame),
           win.name + ": THE VIEW HOLDS WHEN EVERY READER HOLDS (the view " + win.holdFrame + ", the gather " +
           win.gatherHoldFrame + ", the reflection " + win.reflHoldFrame + ")");
    if (win.decisionFrame >= 0) {
        assert(win.holdFrame > win.decisionWorkEnd && win.holdFrame >= win.decisionFrame,
               win.name + ": (2) THE HOLD WAITS FOR THE DECISION: the hold " + win.holdFrame + " after the decision " +
               win.decisionFrame + " and its last GI row " + win.decisionWorkEnd);
        assert(win.firstRestFrame >= 0 && win.holdFrame - win.firstRestFrame <= win.holdBound,
               win.name + ": (3) LIVENESS: the first hold " + (win.holdFrame - win.firstRestFrame) +
               " frames after the readers' first rest (" + win.firstRestFrame + "), within the derived bound " + win.holdBound);
        assert(win.workPace >= 0 && win.decisionWorkEnd - win.decisionFrame <= win.workPace,
               win.name + ": (3) W'S PACE: the decision's work " + (win.decisionWorkEnd - win.decisionFrame) +
               " frames within ceil(" + win.workPages.toFixed(1) + " pages / the tier's pages a frame) + due = " + win.workPace);
    }
    assert(win.gatherHoldFrame > win.lastGiRowFrame && win.gatherHoldFrame > win.lastHoldKeyFrame &&
           win.gatherHoldFrame <= lastWrite + win.lastKeyConverge + win.settleFrames + 1,
           win.name + ": the gather's hold is the settle after the last write (" + win.gatherHoldFrame + ", last hold key " +
           win.lastHoldKeyFrame + ", last row " + win.lastGiRowFrame + ", C " + win.lastKeyConverge + ", settle " + win.settleFrames + ")");
}
assert(typeof r.tsv === "string" && r.tsv.length > 0, "the TSV path: " + r.tsv);
assert(r.path.directEqual.length >= 1, "the path check read the cascades: " + JSON.stringify(r.path));

// THE DOOR BESIDE A HOST'S CAPTURE (PHOTON-2 round 2, G7): a perf.capture running before the run is never refused nor
// stopped - the door reads its own copy of the records (the tee), leaves the monitor at the level it found, and its
// cost line counts the run's frames only (a tail frame it renders while a record is owed is not the run's work).
var cap = perf.capture({ frames: 900, label: "e2e-world-stillness-g7" });
assert(cap && cap.started === true, "a host capture records: " + JSON.stringify(cap));
var planned = 1 + 2 + 20;   // the teleport, the hold, the window's 20 frames after the stop
var r2 = world.stillness({ script: "teleport:0,2,8,0,1,0/hold:2", frames: 20, out: "e2e-world-stillness-g7" });
assert(r2 && r2.ok === true, "world.stillness runs beside the host's capture");
var costLine = "";
for (var c = 0; c < r2.summary.length; ++c)
    if (r2.summary[c].indexOf("gpu ms") >= 0) costLine = r2.summary[c];
assert(costLine.length > 0, "the run's cost line: " + costLine.substring(0, 160));
var counts = costLine.match(/\((\d+)\)/g) || [];
var most = 0;
for (var q = 0; q < counts.length; ++q) most = Math.max(most, parseInt(counts[q].slice(1, -1), 10));
assert(counts.length > 0 && most <= planned, "the cost line counts the run's " + planned + " frames at most: " + most);
var ps = perf.status();
assert(ps.recording === true, "the host's capture still records after the run: " + JSON.stringify(ps.phase));
var stopped = perf.stop();
assert(stopped && stopped.stopped === true && stopped.frames > planned,
       "the host's bundle holds its frames, the run's among them: " + JSON.stringify(stopped));
console.log("world.stillness e2e: PASS");
