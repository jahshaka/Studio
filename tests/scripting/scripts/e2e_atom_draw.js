// scripting.e2e.atom_draw — THE VISIBILITY BUFFER FROM THE SCRIPT SIDE (ATOM
// S3-DRAW): world.atomStatus() reads the split the renderer decided for the open
// scene, and world.setAtomDraw() is the measuring door that shuts it.
//
// The DEFAULT SCENE is the subject, because it carries both halves of the split:
// the floor is an Atom item (the id pass draws it, the decode shades it) and the
// floor's 4 km horizon plane is a BACKDROP — drawn by every view, in no world
// channel — which the id pass must leave to PBS (the reason `notWorld`). A split
// that routed the horizon to the Atom queue drew it nowhere: the default scene's
// far ground turned to sky (engine selftest pose 1, found while building the lane).
// The picture must not care which path drew it: the editor's own shot ("scene"
// grade) with the split on and off agrees to a code at every probe.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function J(x) { return JSON.stringify(x); }

var proj = project.create("Atom Draw " + Date.now());
assert(proj.length > 10, "project.create");
// Frames until the floor's textures have landed: a material whose textures are
// still baking is `pending` (PBS draws it for those frames).
var st = world.atomStatus();
for (var i = 0; i < 60 && (st.pending > 0 || st.atomItems < 1); ++i) {
    editor.frame(2);
    st = world.atomStatus();
}
console.log("atomStatus: " + J(st));
assert(st.live && st.on, "the split is live on this machine (live " + st.live + ", on " + st.on + ")");
assert(st.atomItems >= 1 && st.materials >= 1 && st.buckets >= 1 && st.screenDraws === st.buckets,
       "the floor is drawn by the id pass and shaded by one decode draw per bucket");
assert(st.notWorld === 1, "the floor's horizon plane stays on PBS as a backdrop (notWorld " + st.notWorld + ")");
assert(st.stereoViews === 0 && st.passthroughViews === 0,
       "no view of the default scene (Epic: the post chain) draws without the id pass");

var probes = [ { x: 0.5, y: 0.92 }, { x: 0.15, y: 0.75 }, { x: 0.85, y: 0.75 },
               { x: 0.5, y: 0.55 }, { x: 0.1, y: 0.5 }, { x: 0.9, y: 0.5 } ];
var on = editor.screenshot("atom_draw_on.png", 256, 256, probes, "scene");

assert(world.setAtomDraw(false) === false, "world.setAtomDraw(false) shuts the split");
editor.frame(4);
var off = world.atomStatus();
assert(!off.on && off.atomItems === st.atomItems,
       "shut, the stat reports what the split WOULD decide (on " + off.on + ", atomItems " + off.atomItems + ")");
var pbs = editor.screenshot("atom_draw_off.png", 256, 256, probes, "scene");
var worst = 0;
for (var p = 0; p < probes.length; ++p) {
    var a = on.probes[p], b = pbs.probes[p];
    worst = Math.max(worst, Math.abs(a.r - b.r), Math.abs(a.g - b.g), Math.abs(a.b - b.b));
}
assert(worst <= 2, "the editor's shot is the same picture through the decode and through PBS (worst probe " +
       worst + " codes)");

assert(world.setAtomDraw(true) === true, "world.setAtomDraw(true) restores the split");
editor.frame(4);
assert(world.atomStatus().on, "the split is live again");

// THE LOW TIER HAS NO POST CHAIN: the viewport's scene pass renders straight into
// the window (its anti-aliasing is the window's samples), whose depth Ogre pairs
// with nothing but the window — so that view draws the Atom items through PBS and
// says so, and nothing throws (a window shape that carried the id pass threw in
// 'Jahshaka opaque' every frame).
app.engineErrors(true);
world.mode({ mode: "low" });
editor.frame(6);
var low = world.atomStatus();
console.log("low: " + J(low));
assert(low.on && low.passthroughViews === 1, "at Low the viewport is a passthrough view (" + low.passthroughViews + ")");
var errs = app.engineErrors();
var thrown = 0;
for (var k = 0; k < (errs.entries || []).length; ++k)
    if (/incompatible|Jahshaka opaque/.test(errs.entries[k].message)) ++thrown;
assert(thrown === 0, "no compositor pass threw at Low (" + J(errs.entries) + ")");
world.mode({ mode: "epic" });
editor.frame(6);
assert(world.atomStatus().passthroughViews === 0, "back at Epic the viewport carries the id pass again");
"ok";
