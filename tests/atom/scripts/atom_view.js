// atom.view — THE ATOM VIEW FROM THE SCRIPT SIDE (D0-ATOM-VIEW): world.setAtomView
// paints the visibility buffer in false colour over the finished frame, wherever an
// object the id pass draws is the visible surface, and 'off' is the picture it was.
//
// Every picture is the editor's own shot at 160 x 90 with EVERY pixel probed (its
// centre), so "changed" is a pixel count and "the same picture" is every pixel equal.
//   (a) each mode changes > 20 % of the default scene's pixels against 'off', in the
//       "scene" grade (the post chain) and the "plain" grade (the passthrough shape);
//   (b) 'off' after all four is the 'off' picture, pixel for pixel;
//   (c) with the split shut (world.setAtomDraw(false)) no view paints anything;
//   (d) a stock-PBR object (a two-sided cube, atomStatus().twoSided) standing on the
//       floor keeps its lit picture: the id image still names the floor under it, and
//       the view paints only where the final depth is the id pass's;
//   (e) the verb refuses an unknown name, and world.atomStatus() reports the view.
// Frames, never time: every shot renders until the scene is at rest.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function J(x) { return JSON.stringify(x); }

var W = 160, H = 90;
var probes = [];
for (var y = 0; y < H; ++y)
    for (var x = 0; x < W; ++x) probes.push({ x: (x + 0.5) / W, y: (y + 0.5) / H });

function shot(name, grade) {
    var r = editor.screenshot("atom_view_" + name + ".png", W, H, probes, grade);
    assert(r && r.probes && r.probes.length === W * H, "shot " + name + " (" + grade + ") read every pixel");
    return r.probes;
}
function changed(a, b) {
    var n = 0;
    for (var i = 0; i < a.length; ++i)
        if (a[i].r !== b[i].r || a[i].g !== b[i].g || a[i].b !== b[i].b) ++n;
    return n;
}
function same(a, b) { return changed(a, b) === 0; }

var proj = project.create("Atom View " + Date.now());
assert(proj.length > 10, "project.create");
var st = world.atomStatus();
for (var i = 0; i < 60 && (st.pending > 0 || st.atomItems < 1); ++i) {
    editor.frame(2);
    st = world.atomStatus();
}
console.log("atomStatus: " + J(st));
assert(st.live && st.on && st.atomItems >= 1, "the split is live and draws the floor");
assert(st.view === "off" && world.atomView() === "off", "a new scene starts with the view off");

// (e) the refusal
var refused = false;
try { world.setAtomView("wireframe"); } catch (e) { refused = /unknown view/.test(String(e)); }
assert(refused, "an unknown view is refused, by name");
assert(world.atomView() === "off", "...and changes nothing");

var modes = ["triangles", "levels", "buckets", "objects"];
var grades = ["scene", "plain"];
for (var g = 0; g < grades.length; ++g) {
    var grade = grades[g];
    var off = shot("off_" + grade, grade);
    for (var m = 0; m < modes.length; ++m) {
        assert(world.setAtomView(modes[m]) === true, "world.setAtomView('" + modes[m] + "')");
        assert(world.atomView() === modes[m] && world.atomStatus().view === modes[m],
               "world.atomView and atomStatus().view say " + modes[m]);
        var pic = shot(modes[m] + "_" + grade, grade);
        var frac = changed(off, pic) / (W * H);
        console.log(grade + " " + modes[m] + ": " + (100 * frac).toFixed(1) + " % of pixels changed");
        assert(frac > 0.2, grade + ": '" + modes[m] + "' paints more than 20 % of the default scene (" +
               (100 * frac).toFixed(1) + " %)");
    }
    // (b)
    assert(world.setAtomView("off") === true, "world.setAtomView('off')");
    var back = shot("off_again_" + grade, grade);
    assert(same(off, back), grade + ": 'off' restores the off picture exactly (" + changed(off, back) +
           " pixels differ)");
}

// (c) with the split shut there is no id image, and nothing is painted.
assert(world.setAtomDraw(false) === false, "world.setAtomDraw(false) shuts the split");
editor.frame(4);
var pbsOff = shot("pbs_off", "scene");
assert(world.setAtomView("triangles") === true, "the view is accepted with the split shut");
var pbsTri = shot("pbs_triangles", "scene");
assert(same(pbsOff, pbsTri), "with the split shut the view paints nothing (" + changed(pbsOff, pbsTri) +
       " pixels differ)");
assert(world.setAtomView("off") === true && world.setAtomDraw(true) === true, "the split is restored");
editor.frame(4);

// (d) a stock-PBR object in front of an atom surface keeps its lit picture. A thin
// slab held above the floor as a planar mirror stays on the stock shader ('planar':
// its reflection is bound per object) and writes depth over the floor the id image
// still names beneath it.
var offPlain = shot("off_plain_ref", "plain");
world.setAtomView("objects");
var objPlain = shot("objects_plain_ref", "plain");
world.setAtomView("off");
var cube = scene.addPrimitive("cube", { position: [0, 0.6, 0], scale: [2, 0.05, 2] });
assert(!!cube, "a slab above the floor");
assert(node.setProperty(cube, "planarReflector", true) === true, "...as a planar reflector");
var st2 = world.atomStatus();
for (var k = 0; k < 60 && (st2.planar < 1 || st2.pending > 0); ++k) { editor.frame(2); st2 = world.atomStatus(); }
console.log("with the slab: " + J(st2));
assert(st2.planar >= 1, "the slab stays on the stock PBR shader (planar " + st2.planar + ")");
var offCube = shot("off_cube", "plain");
world.setAtomView("objects");
var objCube = shot("objects_cube", "plain");
world.setAtomView("off");
// THE CUBE'S PIXELS as the view sees them: painted without the cube (the floor was
// there), left as the lit picture with it. Every one of them must be the lit picture
// of the scene with the cube — and there must be a cube's worth of them.
var cubePixels = 0, keptLit = 0, paintedOverCube = 0;
for (var p = 0; p < offCube.length; ++p) {
    var wasPainted = offPlain[p].r !== objPlain[p].r || offPlain[p].g !== objPlain[p].g || offPlain[p].b !== objPlain[p].b;
    var cubeHere = offCube[p].r !== offPlain[p].r || offCube[p].g !== offPlain[p].g || offCube[p].b !== offPlain[p].b;
    var lit = offCube[p].r === objCube[p].r && offCube[p].g === objCube[p].g && offCube[p].b === objCube[p].b;
    if (wasPainted && lit) ++keptLit;
    if (wasPainted && cubeHere) { ++cubePixels; if (!lit) ++paintedOverCube; }
}
console.log("slab: " + cubePixels + " pixels differ from the cube-less picture over painted floor; " +
            keptLit + " of the floor's painted pixels are now the lit picture; " + paintedOverCube +
            " of the changed pixels are painted");
assert(keptLit >= 100, "the slab keeps its lit picture where it stands in front of the floor (" + keptLit + " pixels)");
"ok";
