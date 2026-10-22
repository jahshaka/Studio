// scripting.e2e.world_stillness — THE STILLNESS DOOR THROUGH THE SCRIPTING HOST (PHOTON-2 stage 0, API-first: the verb
// a Bistro measurement drives). On a new project's default scene: a 0.5 m walk, the stop, a short window and the path
// check. The ASSERTIONS are the door's SHAPE and its own invariants, never a picture: the verb answers ok with one
// STILLNESS line per window plus the path line, the TSV exists, the window frames are the ones asked for, the class
// split covers the rectangles, the walk window's mask is the gather's own when one runs, and a still view after a
// window whose last GI row has landed holds (the gather's settle law: hold = the last row + the settle + 1, or never).

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

var guid = project.create(project.nextFreeName("world-stillness"), { template: "basic" });
assert(guid && guid.length > 10, "project.create -> " + guid);
editor.selectNone();

var r = world.stillness({
    script: "teleport:0,2,8,0,1,0/hold:20/walk:0.5,2,8,0.5,1,0:10",
    frames: 24,
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
assert(r.windows[0].frames === 24 && r.windows[1].frames === 24, "each window is the 24 frames asked for");
var px = r.windows[1].pixels;
assert(px.all > 0 && px.upper > 0 && px.lower > 0 && px.upper + px.lower === px.all,
       "the teleport window counts every pixel, split by the two rectangles: " + JSON.stringify(px));
var st = world.giStatus();
if (st.gather && st.gather.running === true)
    assert(r.windows[0].visibleFromGather === true, "the walk window's mask is the gather's own history count");
for (var w = 0; w < 2; ++w) {
    var win = r.windows[w];
    if (win.holdFrame >= 0)
        assert(win.holdFrame > win.lastGiRowFrame, win.name + ": the hold comes after the last GI row (" +
               win.holdFrame + " > " + win.lastGiRowFrame + ")");
}
assert(typeof r.tsv === "string" && r.tsv.length > 0, "the TSV path: " + r.tsv);
assert(r.path.directEqual.length >= 1, "the path check read the cascades: " + JSON.stringify(r.path));
console.log("world.stillness e2e: PASS");
